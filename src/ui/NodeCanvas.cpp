#include "NodeCanvas.hpp"
#include "../app/AssetService.hpp"
#include "../core/ContractRegistry.hpp"
#include "../core/CsvParamIO.hpp"
#include "../core/CustomNodeRegistry.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imnodes.h>

#include <cstring>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// Category colour helpers
// ---------------------------------------------------------------------------
static ImU32 titleCol(NodeCategory c) {
    switch (c) {
        case NodeCategory::Source:      return IM_COL32( 42, 138,  62, 255);
        case NodeCategory::Transformer: return IM_COL32( 48,  90, 178, 255);
        case NodeCategory::Device:      return IM_COL32(112,  78, 178, 255);  // morado
        case NodeCategory::Sink:        return IM_COL32(175,  50,  50, 255);
    }
    return IM_COL32(100,100,100,255);
}
static ImU32 titleHovCol(NodeCategory c) {
    switch (c) {
        case NodeCategory::Source:      return IM_COL32( 60, 180,  80, 255);
        case NodeCategory::Transformer: return IM_COL32( 68, 120, 220, 255);
        case NodeCategory::Device:      return IM_COL32(140, 100, 220, 255);
        case NodeCategory::Sink:        return IM_COL32(220,  70,  70, 255);
    }
    return IM_COL32(140,140,140,255);
}

// Wire colour: green for Source output, blue for Transformer output
static ImU32 wireCol(int fromNodeId, const NodeGraph& g) {
    const NodeInstance* n = g.findNode(fromNodeId);
    if (!n) return IM_COL32(200,200,200,220);
    switch (categoryOf(*n)) {
        case NodeCategory::Source:      return IM_COL32( 80, 200,  80, 220);
        case NodeCategory::Transformer: return IM_COL32( 80, 140, 220, 220);
        default:                        return IM_COL32(200,200,200,220);
    }
}

// ---------------------------------------------------------------------------
void NodeCanvas::init() {
    ImNodes::CreateContext();
    ImNodes::StyleColorsDark();

    ImNodesStyle& s       = ImNodes::GetStyle();
    s.NodeCornerRounding  = 6.0f;
    s.NodePadding         = {10, 5};
    s.NodeBorderThickness = 1.2f;
    s.LinkThickness       = 2.5f;
    s.PinCircleRadius     = 5.0f;
    s.PinLineThickness    = 1.5f;

    ImNodes::GetIO().EmulateThreeButtonMouse.Modifier = &ImGui::GetIO().KeyAlt;
}

// ---------------------------------------------------------------------------
void NodeCanvas::drawContent() {
    // Drain a pending custom-node load dialog (started from the add popup).
    if (!m_loadCustomDialog.isOpen()) {
        std::string p = m_loadCustomDialog.take();
        if (!p.empty()) {
            std::string err;
            bool ok = scinodes::customNodes()
                          .loadFromFile(p, &err);
            m_customLoadStatus = ok
                ? ("Loaded custom node from " + p)
                : ("Load failed: " + err);
        }
    }

    // Drain a pending param CSV dialog (Import / Export from the param panel).
    if (m_paramCsvAction != ParamCsvAction::None && !m_paramCsvDialog.isOpen()) {
        std::string path = m_paramCsvDialog.take();
        if (!path.empty()) {
            // Append .csv if the user omitted an extension.
            auto dot = path.rfind('.');
            auto sep = path.find_last_of("/\\");
            if (dot == std::string::npos ||
                (sep != std::string::npos && dot < sep))
                path += ".csv";

            const NodeInstance* n = active().findNode(m_paramCsvNodeId);
            if (!n) {
                m_paramCsvStatus = "Param CSV failed: node no longer exists.";
            } else if (m_paramCsvAction == ParamCsvAction::Export) {
                std::string err;
                char label[80];
                std::snprintf(label, sizeof(label), "%s #%d",
                              defOf(*n).label.c_str(), n->id);
                bool ok = scinodes::writeNodeParamsCsv(path, *n, label, &err);
                m_paramCsvStatus = ok
                    ? ("Exported to " + path)
                    : ("Export failed: " + err);
            } else {
                // Import — snapshot for undo, mutate a temp copy, write back.
                NodeInstance tmp = *n;
                std::string err;
                std::vector<std::string> warns;
                bool ok = scinodes::readNodeParamsCsv(path, tmp, &err, &warns);
                if (ok) {
                    recordSnapshot(m_graph.snapshot());
                    for (const auto& [name, value] : tmp.params)
                        active().setParam(n->id, name, value);
                    if (m_paramCallback) {
                        // Re-emit each param so the live bridge updates too.
                        const auto& def = defOf(*n);
                        for (int i = 0; i < (int)def.params.size(); ++i) {
                            auto it = tmp.params.find(def.params[i].name);
                            if (it != tmp.params.end())
                                m_paramCallback(pathFor(n->id), i, it->second);
                        }
                    }
                    m_paramCsvStatus = "Imported " + path
                        + (warns.empty() ? "" :
                           " (" + std::to_string(warns.size()) +
                           " warnings)");
                } else {
                    m_paramCsvStatus = "Import failed: " + err;
                }
            }
        }
        m_paramCsvAction = ParamCsvAction::None;
        m_paramCsvNodeId = 0;
    }

    // Drain del file dialog para asignar asset 3D a un nodo Device.
    if (m_assetDialogNodeId != 0 && !m_assetDialog.isOpen()) {
        std::string path = m_assetDialog.take();
        if (!path.empty()) {
            const int nodeId = m_assetDialogNodeId;
            active().setAssetPath(nodeId, path);
            reloadAssetFor(nodeId);

            // Si el asset cargado no satisface el contrato Y no hay
            // sidecar todavía, abrir el panel de mapping
            // automáticamente — el usuario probablemente quiere
            // mapear nodo-por-nodo a mano, no re-exportar.
            const scinodes::DeviceAsset* asset =
                m_assetService ? m_assetService->find(nodeId) : nullptr;
            if (asset && !asset->valid() &&
                !scinodes::app::AssetService::sidecarExists(path)) {
                openMappingPanelFor(nodeId);
            }
        }
        m_assetDialogNodeId = 0;
    }

    // Render del mapping panel — un solo frame por ciclo.  Cuando el
    // usuario confirma, persistimos el sidecar y recargamos el asset
    // para que el nuevo binding tome efecto.
    if (m_mappingPanel.drawFrame()) {
        std::string err;
        const auto& map = m_mappingPanel.result();
        if (map.saveToFile(m_mappingPanel.sidecarPath(), &err)) {
            if (m_mappingNodeId != 0) reloadAssetFor(m_mappingNodeId);
        } else {
            m_errorMsg   = "No se pudo guardar el mapping: " + err;
            m_errorTimer = 5.0f;
        }
        m_mappingNodeId = 0;
    }

    // Focus-follows-mouse estilo Blender.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::IsAnyItemActive()) {
        ImGui::SetWindowFocus();
    }

    handleZoom();
    if (!m_readOnly) handleUndoRedo();

    // Read-only banner
    if (m_readOnly) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 170, 60, 255));
        ImGui::TextUnformatted("  READ-ONLY — loaded graph has grammar violations.");
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    // Shift+A → add-node popup at cursor (browse mode, no auto-connect).
    if (!m_readOnly &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        ImGui::GetIO().KeyShift &&
        ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        openAddPopup(ImGui::GetMousePos());
    }

    drawAddPopup();
    drawRenamePopup();
    showErrorTooltip();

    // Breadcrumb (visible solo cuando hay sub-niveles activos).
    if (!m_canvasStack.empty()) drawBreadcrumb();

    // Activar el imnodes EditorContext del nivel actual antes de empezar
    // el editor.  Cada subgrafo tiene su propio espacio de ids/posiciones
    // así no colisionan con el padre.
    ImNodes::EditorContextSet(contextFor(canvasPathKey()));

    ImGui::SetWindowFontScale(m_zoom);
    ImNodes::BeginNodeEditor();

    // Apply any positions queued by a recent load — must happen inside the
    // editor scope so imnodes treats them as authoritative for this frame.
    if (m_applyPositionsPending) {
        applyPositionsToImnodes();
        m_applyPositionsPending = false;
    }

    if (m_readOnly) ImGui::BeginDisabled();
    NodeGraph& g = active();
    for (const auto& n : g.nodes())
        drawNode(n);

    drawEdges();

    ImNodes::EndNodeEditor();
    if (m_readOnly) ImGui::EndDisabled();
    ImGui::SetWindowFontScale(1.0f);

    // Overlay con los atajos de navegación.  Esquina inferior izquierda.
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 winPos  = ImGui::GetWindowPos();
        const ImVec2 winSize = ImGui::GetWindowSize();
        const char* overlay = "Home  fit all   |   middle-drag  pan";
        const ImVec2 sz = ImGui::CalcTextSize(overlay);
        const float pad = 6.0f;
        const ImVec2 p0(winPos.x + pad,
                        winPos.y + winSize.y - sz.y - pad - 2.0f);
        const ImVec2 p1(p0.x + sz.x + 12.0f, p0.y + sz.y + 4.0f);
        dl->AddRectFilled(p0, p1, IM_COL32(20, 22, 28, 200), 4.0f);
        dl->AddText(ImVec2(p0.x + 6.0f, p0.y + 2.0f),
                    IM_COL32(180, 190, 205, 230), overlay);
    }

    if (!m_readOnly) {
        handleLinkCreated();
        handleLinkDropped();
        handleEdgeContextMenu();
        handleDeletion();
        handleCopyPaste();
        handleEncapsulate();
        handleRename();
    }
    handleParamPanelTrigger();

    // The parameter panel renders as its own top-level ImGui window
    // outside of the canvas Begin/End — that way the user can move it
    // independently of the canvas and it does not get clipped.
    drawParamPanel();
}

// ---------------------------------------------------------------------------
// Double-click on a node → open the parameter panel for it.
// ---------------------------------------------------------------------------
void NodeCanvas::handleParamPanelTrigger() {
    int hoveredId = 0;
    if (!ImNodes::IsNodeHovered(&hoveredId)) return;
    if (!ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) return;
    if (hoveredId <= 0) return;
    // Doble-click sobre un SubGraph: navegar al subgrafo en vez de
    // abrir el panel de parámetros.  El resto de tipos abren panel.
    if (const NodeInstance* n = active().findNode(hoveredId)) {
        if (n->type == NodeType::SubGraph) {
            enterSubGraph(hoveredId);
            return;
        }
    }
    m_openParamPanelNodeId = hoveredId;
    m_paramPanelPos        = ImGui::GetMousePos();
}

// ---------------------------------------------------------------------------
// Floating parameter panel — one DragFloat per param, plus a close button.
// Undo behaves the same as inline editing: snapshot on activate, commit on
// deactivate. Disabled when the canvas is in read-only mode.
// ---------------------------------------------------------------------------
void NodeCanvas::drawParamPanel() {
    if (m_openParamPanelNodeId == 0) return;
    const NodeInstance* n = active().findNode(m_openParamPanelNodeId);
    if (!n) { m_openParamPanelNodeId = 0; return; }
    const NodeDef& def = defOf(*n);

    char title[80];
    std::snprintf(title, sizeof(title), "%s  #%d###paramPanel",
                  def.label.c_str(), n->id);

    ImGui::SetNextWindowPos(m_paramPanelPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize({340, 0}, ImGuiCond_Appearing);
    bool open = true;
    if (ImGui::Begin(title, &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("%s", def.description.c_str());

        // ---- Import / Export row (only if the node has any params) ----
        if (!def.params.empty()) {
            bool busy = m_paramCsvDialog.isOpen() ||
                        m_paramCsvAction != ParamCsvAction::None;
            ImGui::BeginDisabled(busy);

            if (ImGui::SmallButton("Import CSV…")) {
                m_paramCsvAction = ParamCsvAction::Import;
                m_paramCsvNodeId = n->id;
                m_paramCsvDialog.open(FileDialog::Mode::Open,
                                      "Import parameters from CSV",
                                      { "CSV file (*.csv)", "*.csv" });
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Export CSV…")) {
                m_paramCsvAction = ParamCsvAction::Export;
                m_paramCsvNodeId = n->id;
                char suggested[64];
                std::snprintf(suggested, sizeof(suggested),
                              "params_%s_%d.csv",
                              typeName(n->type), n->id);
                m_paramCsvDialog.open(FileDialog::Mode::Save,
                                      "Export parameters to CSV",
                                      { "CSV file (*.csv)", "*.csv" },
                                      suggested);
            }
            ImGui::EndDisabled();
            if (!m_paramCsvStatus.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", m_paramCsvStatus.c_str());
            }
        }

        // ---- Asset 3D (sólo para nodos de categoría Device) ---------------
        // Botón "Cargar modelo 3D…" + estado de validación contra el
        // contrato del tipo.  El path queda en n.assetPath (persistido
        // en .scn) y la validación se cachea en m_assetService.
        if (def.category == NodeCategory::Device) {
            ImGui::Separator();
            ImGui::TextUnformatted("Modelo 3D");

            const auto* contract =
                (m_contractRegistry ? m_contractRegistry->find(typeName(n->type)) : nullptr);
            if (!contract) {
                ImGui::TextDisabled("(sin contrato registrado para %s)",
                                    typeName(n->type));
            } else {
                // Solo bloqueamos si ya hay un diálogo de asset abierto
                // (zenity en marcha) — antes incluíamos m_readOnly, pero
                // reasignar un asset no agrava las violaciones del grafo
                // y conviene poder arreglarlo precisamente cuando hay
                // problemas.  Log a stderr para diagnóstico cuando el
                // diálogo no parezca abrir (zenity ausente, etc.).
                bool busy = m_assetDialog.isOpen() && m_assetDialogNodeId != 0;
                ImGui::BeginDisabled(busy);
                if (ImGui::SmallButton("Cargar modelo 3D…")) {
                    m_assetDialogNodeId = n->id;
                    m_assetDialog.open(FileDialog::Mode::Open,
                                       "Cargar asset glTF",
                                       { "glTF", "*.gltf *.glb" });
                }
                if (busy) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(esperando dialogo...)");
                }
                ImGui::EndDisabled();

                if (!n->assetPath.empty()) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Editar mapping…")) {
                        openMappingPanelFor(n->id);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Quitar")) {
                        active().setAssetPath(n->id, "");
                        if (m_assetService) m_assetService->detach(n->id);
                    }
                }

                // Estado.
                if (n->assetPath.empty()) {
                    ImGui::TextDisabled("(ningún asset asignado)");
                } else {
                    const scinodes::DeviceAsset* asset =
                        m_assetService ? m_assetService->find(n->id) : nullptr;
                    if (!asset) {
                        // Aún no se ha cargado (post-deserialización o
                        // primer pase).  Forzar carga ahora.
                        reloadAssetFor(n->id);
                        asset = m_assetService ? m_assetService->find(n->id) : nullptr;
                    }
                    ImGui::TextWrapped("%s", n->assetPath.c_str());
                    if (asset) {
                        if (asset->valid()) {
                            ImGui::TextColored({0.3f, 0.9f, 0.5f, 1.0f},
                                "✓ Cumple contrato '%s'",
                                contract->device_type.c_str());
                        } else {
                            ImGui::TextColored({0.95f, 0.5f, 0.3f, 1.0f},
                                "✗ Faltan elementos del contrato:");
                            for (const auto& m : asset->missing) {
                                ImGui::BulletText("%s", m.c_str());
                            }
                        }
                        if (!asset->warnings.empty()) {
                            ImGui::TextDisabled("Advertencias:");
                            for (const auto& w : asset->warnings) {
                                ImGui::BulletText("%s", w.c_str());
                            }
                        }
                    }
                }
            }
        }

        ImGui::Separator();

        if (m_readOnly) ImGui::BeginDisabled();

        for (int i = 0; i < (int)def.params.size(); ++i) {
            const auto& pd = def.params[i];
            float val = (float)n->params.at(pd.name);

            ImGui::PushID(i);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(pd.name.c_str());
            ImGui::SameLine(160.f);
            ImGui::SetNextItemWidth(120.f);

            const ImGuiID wid = ImGui::GetID("##v");
            const bool changed = ImGui::DragFloat("##v", &val, 0.01f,
                                                  0.f, 0.f, "%.4g");
            // En modo text-input (Ctrl-click o doble-click → escritura
            // dígito a dígito) cada keystroke dispara "changed", lo que
            // mandaría valores intermedios al solver (escribir "50"
            // pasaría primero por "5").  Solo enviamos al solver en
            // modo drag (mouse arrastrando); en text-input esperamos
            // al commit (Enter o focus loss) — IsItemDeactivatedAfterEdit.
            const bool isTextInput = ImGui::TempInputIsActive(wid);

            if (ImGui::IsItemActivated())
                m_pendingParamBefore = m_graph.snapshot();

            if (changed) {
                active().setParam(n->id, pd.name, (double)val);
                if (!isTextInput && m_paramCallback)
                    m_paramCallback(pathFor(n->id), i, (double)val);
            }

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                if (m_paramCallback) m_paramCallback(pathFor(n->id), i, (double)val);
                if (m_pendingParamBefore) {
                    recordSnapshot(*m_pendingParamBefore);
                    m_pendingParamBefore = std::nullopt;
                }
            }

            if (!pd.unit.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", pd.unit.c_str());
            }
            ImGui::PopID();
        }

        if (m_readOnly) ImGui::EndDisabled();

        // -----------------------------------------------------------------
        // Sección \"Canales\" (Oscilloscope multi-canal): por cada puerto
        // conectado, dos InputText (Name + Unit) que se persisten en
        // n.stringParams["portLabel<i>"] y "portUnit<i>".  El nodo y la
        // leyenda del plot leen estos valores.
        // -----------------------------------------------------------------
        if (n->type == NodeType::Oscilloscope) {
            ImGui::Separator();
            ImGui::TextDisabled("Canales conectados");
            const NodeDef& d = defOf(*n);
            int countShown = 0;
            for (int port = 0; port < d.inputPorts; ++port) {
                int srcId = -1;
                for (const auto& e : active().edges()) {
                    if (e.toNodeId == n->id &&
                        (e.toAttrId % 10000) == port) {
                        srcId = e.fromNodeId; break;
                    }
                }
                if (srcId < 0) continue;
                ++countShown;
                char keyL[32], keyU[32];
                std::snprintf(keyL, sizeof(keyL), "portLabel%d", port);
                std::snprintf(keyU, sizeof(keyU), "portUnit%d",  port);
                std::string curL = n->stringParams.count(keyL)
                    ? n->stringParams.at(keyL) : std::string{};
                std::string curU = n->stringParams.count(keyU)
                    ? n->stringParams.at(keyU) : std::string{};
                char bufL[128], bufU[32];
                std::strncpy(bufL, curL.c_str(), sizeof(bufL) - 1); bufL[sizeof(bufL) - 1] = 0;
                std::strncpy(bufU, curU.c_str(), sizeof(bufU) - 1); bufU[sizeof(bufU) - 1] = 0;

                ImGui::PushID(port);
                ImGui::Text("in %d", port + 1);
                ImGui::SameLine(60.f);
                ImGui::SetNextItemWidth(180.f);
                if (ImGui::InputTextWithHint("##lab", "nombre (ej. Codo A pos)",
                                             bufL, sizeof(bufL)))
                    active().setStringParam(n->id, keyL, bufL);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.f);
                if (ImGui::InputTextWithHint("##unit", "unit (ej. rad)",
                                             bufU, sizeof(bufU)))
                    active().setStringParam(n->id, keyU, bufU);
                ImGui::PopID();
            }
            if (countShown == 0) {
                ImGui::TextDisabled("  (conecta un cable a una entrada "
                                    "para etiquetarla)");
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Close", {100, 0}))
            m_openParamPanelNodeId = 0;
    }
    ImGui::End();
    if (!open) m_openParamPanelNodeId = 0;
}

// ---------------------------------------------------------------------------
// drawNode
// ---------------------------------------------------------------------------
void NodeCanvas::drawNode(const NodeInstance& n) {
    const NodeDef& def = defOf(n);

    const bool  highlighted   = (m_highlightNodeId == n.id);
    const ImU32 titleColor    = highlighted ? IM_COL32(220, 50, 50, 255)
                                            : titleCol(def.category);
    const ImU32 titleHovColor = highlighted ? IM_COL32(240, 80, 80, 255)
                                            : titleHovCol(def.category);
    ImNodes::PushColorStyle(ImNodesCol_TitleBar,         titleColor);
    ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered,  titleHovColor);
    ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, titleHovColor);

    ImNodes::BeginNode(n.id);

    ImNodes::BeginNodeTitleBar();
    // SubGraph: si tiene `Name` en stringParams, usarlo como título.
    {
        const char* title = def.label.c_str();
        if (n.type == NodeType::SubGraph) {
            auto it = n.stringParams.find("Name");
            if (it != n.stringParams.end() && !it->second.empty())
                title = it->second.c_str();
        }
        ImGui::TextUnformatted(title);
    }
    ImNodes::EndNodeTitleBar();

    // Para sinks multi-canal dinámicos (Oscilloscope) renderizamos
    // sólo los puertos en uso + 1 extra "vacío" para conectar la
    // siguiente señal.  Si todos los puertos están ocupados, no
    // mostramos extra.  Resto de nodos: número fijo de puertos.
    int portsToShow = def.inputPorts;
    if (n.type == NodeType::Oscilloscope) {
        int used = 0;
        for (const auto& e : active().edges())
            if (e.toNodeId == n.id) ++used;
        portsToShow = std::min(def.inputPorts, used + 1);
    }
    for (int p = 0; p < portsToShow; ++p) {
        ImNodes::BeginInputAttribute(n.inputAttrId(p));
        // Label custom por puerto (Oscilloscope): "portLabel<p>" en
        // stringParams.  Si existe, se muestra junto al "in N".
        std::string custom;
        if (n.type == NodeType::Oscilloscope) {
            char k[32]; std::snprintf(k, sizeof(k), "portLabel%d", p);
            auto it = n.stringParams.find(k);
            if (it != n.stringParams.end()) custom = it->second;
        }
        if (def.inputPorts == 1)
            ImGui::TextUnformatted("in");
        else if (!custom.empty())
            ImGui::Text("in %d  %s", p + 1, custom.c_str());
        else
            ImGui::Text("in %d", p + 1);
        ImNodes::EndInputAttribute();
    }

    for (int i = 0; i < (int)def.params.size(); ++i) {
        const auto& pd  = def.params[i];
        float val       = (float)n.params.at(pd.name);

        ImNodes::BeginStaticAttribute(n.paramAttrId(i));

        // Label
        ImGui::TextDisabled("%s", pd.name.c_str());
        ImGui::SameLine();

        // Widget: DragFloat — drag to change, double-click to type exact value
        char wid[32];
        std::snprintf(wid, sizeof(wid), "##p%d_%d", n.id, i);
        ImGui::SetNextItemWidth(86.f);
        const ImGuiID wgtId = ImGui::GetID(wid);
        const bool changed = ImGui::DragFloat(wid, &val, 0.01f,
                                              0.f, 0.f, "%.4g");
        // Modo drag vs modo text-input: cada keystroke en text-input
        // dispara "changed".  Solo enviamos al solver en modo drag;
        // en text-input esperamos al commit (Enter / focus loss).
        const bool isTextInput = ImGui::TempInputIsActive(wgtId);

        // Capture snapshot on first frame of activation (before value changes)
        if (ImGui::IsItemActivated())
            m_pendingParamBefore = m_graph.snapshot();

        if (changed) {
            active().setParam(n.id, pd.name, (double)val);
            if (!isTextInput && m_paramCallback)
                m_paramCallback(pathFor(n.id), i, (double)val);
        }

        // Commit: envío final al solver + undo entry.
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (m_paramCallback) m_paramCallback(pathFor(n.id), i, (double)val);
            if (m_pendingParamBefore) {
                recordSnapshot(*m_pendingParamBefore);
                m_pendingParamBefore = std::nullopt;
            }
        }

        // Unit label
        if (!pd.unit.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", pd.unit.c_str());
        }

        ImNodes::EndStaticAttribute();
    }

    for (int p = 0; p < def.outputPorts; ++p) {
        ImNodes::BeginOutputAttribute(n.outputAttrId(p));
        ImGui::Indent(60.f);
        if (def.outputPorts == 1) ImGui::TextUnformatted("out");
        else                      ImGui::Text("out %d", p + 1);
        ImNodes::EndOutputAttribute();
    }

    ImNodes::EndNode();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
}

// ---------------------------------------------------------------------------
// drawEdges — renders all edges with category-coded wire colours
// ---------------------------------------------------------------------------
void NodeCanvas::drawEdges() {
    for (const auto& e : active().edges()) {
        ImU32 wc = wireCol(e.fromNodeId, m_graph);
        ImNodes::PushColorStyle(ImNodesCol_Link,         wc);
        ImNodes::PushColorStyle(ImNodesCol_LinkHovered,  wc);
        ImNodes::PushColorStyle(ImNodesCol_LinkSelected, IM_COL32(255, 220, 50, 255));
        ImNodes::Link(e.id, e.fromAttrId, e.toAttrId);
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }
}

// ---------------------------------------------------------------------------
// handleLinkCreated — called after EndNodeEditor
// ---------------------------------------------------------------------------
void NodeCanvas::handleLinkCreated() {
    int fromAttr = 0, toAttr = 0;
    if (!ImNodes::IsLinkCreated(&fromAttr, &toAttr)) return;

    auto before = m_graph.snapshot();
    auto err    = active().tryAddEdge(fromAttr, toAttr);

    if (err) {
        // Show error tooltip and do NOT record undo
        m_errorMsg   = "[" + err->rule + "]  " + err->message;
        m_errorTimer = 3.5f;
    } else {
        recordSnapshot(before);   // record "before" for undo
        bumpDirty();
    }
}

// ---------------------------------------------------------------------------
// handleDeletion — Delete / Backspace removes selected nodes and edges
// ---------------------------------------------------------------------------
void NodeCanvas::handleDeletion() {
    // Don't steal Delete/Backspace while a parameter field has keyboard focus.
    if (ImGui::IsAnyItemActive()) return;

    if (!ImGui::IsKeyPressed(ImGuiKey_Delete) &&
        !ImGui::IsKeyPressed(ImGuiKey_Backspace))
        return;

    bool changed = false;

    // Selected edges
    {
        int n = ImNodes::NumSelectedLinks();
        if (n > 0) {
            std::vector<int> sel(n);
            ImNodes::GetSelectedLinks(sel.data());
            auto before = m_graph.snapshot();
            for (int id : sel) active().removeEdge(id);
            recordSnapshot(before);
            changed = true;
        }
    }

    // Selected nodes (also removes their edges)
    {
        int n = ImNodes::NumSelectedNodes();
        if (n > 0) {
            std::vector<int> sel(n);
            ImNodes::GetSelectedNodes(sel.data());
            auto before = m_graph.snapshot();
            for (int id : sel) active().removeNode(id);
            recordSnapshot(before);
            changed = true;
        }
    }

    if (changed) bumpDirty();
}

// ---------------------------------------------------------------------------
// handleUndoRedo
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// handleCopyPaste — Ctrl+C captures the current selection; Ctrl+V duplicates
// the clipboard at the cursor preserving relative layout.  Only edges that
// connect two *selected* nodes are captured; edges crossing the selection
// boundary are dropped on the floor.  Each paste creates fresh node ids
// (via NodeGraph::addNode) so the new copy is fully independent.
// ---------------------------------------------------------------------------
void NodeCanvas::handleCopyPaste() {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl)               return;
    if (ImGui::IsAnyItemActive())  return;   // a text input has focus
    if (ImGui::IsKeyPressed(ImGuiKey_C, false)) copySelectionToClipboard();
    if (ImGui::IsKeyPressed(ImGuiKey_V, false)) pasteClipboard();
}

void NodeCanvas::copySelectionToClipboard() {
    const int n = ImNodes::NumSelectedNodes();
    if (n <= 0) return;

    std::vector<int> sel(n);
    ImNodes::GetSelectedNodes(sel.data());
    std::unordered_set<int> selSet(sel.begin(), sel.end());

    m_clipNodes.clear();
    m_clipEdges.clear();

    float minX =  std::numeric_limits<float>::infinity();
    float minY =  std::numeric_limits<float>::infinity();

    NodeGraph& g = active();
    for (int id : sel) {
        const NodeInstance* node = g.findNode(id);
        if (!node) continue;
        ImVec2 pos = ImNodes::GetNodeScreenSpacePos(id);
        ClipboardEntry ent;
        ent.node = *node;
        ent.pos  = pos;
        // SubGraph: clonar el contenido del child para que el paste
        // re-instale la topología interna intacta.  Sin esto el paste
        // crearía un SubGraph "vacío" sin grafo hijo y la simulación o
        // navegación posterior podrían derefenciar punteros nulos.
        if (node->type == NodeType::SubGraph) {
            if (const NodeGraph* child = g.subGraphOf(id))
                ent.childGraph = std::make_shared<NodeGraph>(*child);
        }
        m_clipNodes.push_back(std::move(ent));
        minX = std::min(minX, pos.x);
        minY = std::min(minY, pos.y);
    }
    m_clipAnchor = { minX, minY };

    // Sólo capturamos aristas internas — ambos extremos seleccionados.
    for (const Edge& e : active().edges()) {
        if (selSet.count(e.fromNodeId) && selSet.count(e.toNodeId))
            m_clipEdges.push_back(e);
    }
}

void NodeCanvas::pasteClipboard() {
    if (m_clipNodes.empty()) return;

    recordSnapshot(m_graph.snapshot());
    bumpDirty();

    // Offset desde el anchor del clipboard hasta el cursor actual.
    // Pequeño nudge para que el paste no quede exactamente encima de la
    // selección original cuando se pega en el mismo grafo (Ctrl+C, Ctrl+V).
    const ImVec2 mouse = ImGui::GetMousePos();
    ImVec2 offset = { mouse.x - m_clipAnchor.x,
                      mouse.y - m_clipAnchor.y };
    if (std::fabs(offset.x) < 6.0f && std::fabs(offset.y) < 6.0f) {
        offset.x += 24.0f; offset.y += 24.0f;
    }

    // Mapeo oldNodeId → newNodeId para reescribir los attrIds de las aristas.
    std::unordered_map<int, int> idMap;
    std::vector<int> newIds;
    newIds.reserve(m_clipNodes.size());

    for (const ClipboardEntry& ent : m_clipNodes) {
        int newId = 0;
        if (ent.node.type == NodeType::Custom) {
            newId = active().addCustomNode(ent.node.customType);
        } else if (ent.node.type == NodeType::SubGraph) {
            // Crear el SubGraph correctamente (con stubs default que luego
            // serán reemplazados al instalar el child del clipboard).
            newId = active().addSubGraphNode();
            if (ent.childGraph) {
                NodeGraph childCopy = *ent.childGraph;
                active().installSubGraph(newId, std::move(childCopy));
                active().recomputeSubGraphPorts(newId);
            }
        } else {
            newId = active().addNode(ent.node.type);
        }
        if (newId <= 0) continue;
        idMap[ent.node.id] = newId;
        newIds.push_back(newId);

        // Copiar todos los parámetros (numéricos + string + assetPath).
        for (const auto& [k, v] : ent.node.params)
            active().setParam(newId, k, v);
        for (const auto& [k, v] : ent.node.stringParams)
            active().setStringParam(newId, k, v);
        if (!ent.node.assetPath.empty())
            active().setAssetPath(newId, ent.node.assetPath);

        // Posición: misma posición relativa que en el clipboard + offset.
        const ImVec2 np = { ent.pos.x + offset.x, ent.pos.y + offset.y };
        ImNodes::SetNodeScreenSpacePos(newId, np);
    }

    // Re-cablear las aristas internas remapeando los attrIds.
    for (const Edge& e : m_clipEdges) {
        auto fIt = idMap.find(e.fromNodeId);
        auto tIt = idMap.find(e.toNodeId);
        if (fIt == idMap.end() || tIt == idMap.end()) continue;
        const int newFromAttr = fIt->second * 10000 +
                                (e.fromAttrId % 10000);
        const int newToAttr   = tIt->second * 10000 +
                                (e.toAttrId   % 10000);
        active().tryAddEdge(newFromAttr, newToAttr);
    }

    // Selección actualizada: los nodos recién pegados quedan seleccionados,
    // así un paste seguido de un drag mueve todo el bloque junto.
    ImNodes::ClearNodeSelection();
    for (int id : newIds) ImNodes::SelectNode(id);
}

// ---------------------------------------------------------------------------
// Ctrl+G — encapsulate the selection into a SubGraph.  Edges that cross the
// selection boundary become external ports of the new SubGraph (materialized
// internally as `SubGraphInput`/`SubGraphOutput` stubs).
// ---------------------------------------------------------------------------
void NodeCanvas::handleEncapsulate() {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl) return;
    if (ImGui::IsAnyItemActive()) return;
    if (!ImGui::IsKeyPressed(ImGuiKey_G, false)) return;
    encapsulateSelection();
}

void NodeCanvas::encapsulateSelection() {
    const int n = ImNodes::NumSelectedNodes();
    if (n <= 0) return;
    std::vector<int> sel(n);
    ImNodes::GetSelectedNodes(sel.data());

    // Centro de masa de la selección — la lógica pura no conoce posiciones,
    // así que lo calculamos aquí y se lo aplicamos al nodo resultante.
    ImVec2 acc{0, 0};
    for (int id : sel) {
        ImVec2 p = ImNodes::GetNodeScreenSpacePos(id);
        acc = { acc.x + p.x, acc.y + p.y };
    }
    const ImVec2 center{ acc.x / n, acc.y / n };

    // Capturar posiciones de imnodes antes de mutar — las usamos para
    // sembrar el contexto del child y preservar el layout interno.
    std::unordered_map<int, ImVec2> oldPos;
    for (int id : sel) oldPos[id] = ImNodes::GetNodeScreenSpacePos(id);

    recordSnapshot(m_graph.snapshot());
    bumpDirty();
    auto res = active().encapsulateByIds(sel);
    if (res.sgId == 0) {
        m_errorMsg   = "[encapsulate] selección inválida (¿stubs?).";
        m_errorTimer = 3.5f;
        return;
    }
    ImNodes::SetNodeScreenSpacePos(res.sgId, center);
    ImNodes::ClearNodeSelection();
    ImNodes::SelectNode(res.sgId);

    // Sembrar las posiciones internas del SubGraph antes de que el
    // usuario entre.  Como el child vive en su propio EditorContext,
    // cambiamos a él temporalmente, fijamos las posiciones via
    // SetNodeEditorSpacePos (que opera en el contexto activo) y
    // restauramos el contexto del padre.  Las posiciones relativas
    // entre los nodos seleccionados se preservan.
    {
        // Path-key del child = path-actual + sgId + "/"
        const std::string childKey = canvasPathKey() +
                                     std::to_string(res.sgId) + "/";
        ImNodesEditorContext* childCtx  = contextFor(childKey);
        ImNodesEditorContext* parentCtx = contextFor(canvasPathKey());
        ImNodes::EditorContextSet(childCtx);
        for (const auto& [oldId, newId] : res.idMap) {
            auto it = oldPos.find(oldId);
            if (it == oldPos.end()) continue;
            // Convertimos screen-space del padre a screen-space del child:
            // restamos el origen del padre, así el layout queda anclado al
            // (0,0) del child y el usuario lo ve compacto al entrar.
            ImNodes::SetNodeScreenSpacePos(newId, it->second);
        }
        ImNodes::EditorContextSet(parentCtx);
    }
}

// Mantengo este bloque como muerto — el `encapsulateByIds` del modelo lo
// reemplaza.  Lo dejo entre `#if 0` para que sirva de referencia mientras
// estabilizamos la implementación; se eliminará en el próximo commit.
#if 0
static void encapsulate_inline_legacy() {
    std::unordered_set<int> selSet;

    // 1. Particionar aristas en internas/entrantes/salientes.  Por R5 cada
    //    puerto-input recibe a lo sumo una arista, así inEdges no necesita
    //    deduplicación; outEdges sí puede repetir por fan-out.
    std::vector<Edge> internalEdges, inEdges, outEdges;
    for (const Edge& e : active().edges()) {
        const bool f = selSet.count(e.fromNodeId) > 0;
        const bool t = selSet.count(e.toNodeId)   > 0;
        if (f && t)         internalEdges.push_back(e);
        else if (!f && t)   inEdges.push_back(e);
        else if (f && !t)   outEdges.push_back(e);
    }

    // 2. Determinar cuántos puertos externos in/out tiene el SubGraph y
    //    el (sourceAttr externo / lista de toAttr externos) que va a cada uno.
    //    Cada inEdge se vuelve un nuevo input-port; cada outEdge agrupa por
    //    (fromNodeId, fromPort) para fan-out.
    std::vector<int> extInSources;                // attrId origen externo por puerto
    std::vector<int> mappedInTargetAttr;          // attrId destino externo (en el espacio del child) para cada inEdge
    std::vector<std::pair<int,int>> mappedOutSrc; // (childNodeId, childAttrId-offset) por puerto out
    std::vector<std::vector<int>>   outConsumers; // attrIds externos por puerto out

    for (const Edge& e : inEdges) {
        extInSources.push_back(e.fromAttrId);
        mappedInTargetAttr.push_back(e.toAttrId);   // se remapea con idMap más tarde
    }
    {
        std::map<std::pair<int,int>, int> grouper; // (fromNodeId, fromPort) -> portIndex
        for (const Edge& e : outEdges) {
            const int fromPort = (e.fromAttrId % 10000) - 9000;
            auto key = std::make_pair(e.fromNodeId, fromPort);
            auto [it, inserted] = grouper.try_emplace(key,
                static_cast<int>(mappedOutSrc.size()));
            if (inserted) {
                mappedOutSrc.push_back(key);
                outConsumers.emplace_back();
            }
            outConsumers[it->second].push_back(e.toAttrId);
        }
    }

    // 3. Centro de masa de la selección para colocar el SubGraph nuevo.
    ImVec2 acc{0, 0};
    for (int id : sel) acc = { acc.x + ImNodes::GetNodeScreenSpacePos(id).x,
                               acc.y + ImNodes::GetNodeScreenSpacePos(id).y };
    ImVec2 center{ acc.x / n, acc.y / n };

    // 4. Crear el SubGraph en el grafo padre.
    const int sgId = active().addSubGraphNode();
    NodeGraph* child = active().subGraphOf(sgId);
    if (!child) return;
    ImNodes::SetNodeScreenSpacePos(sgId, center);

    // El addSubGraphNode crea 1 in / 1 out stub por defecto — los borramos
    // y creamos exactamente los que necesita la selección.
    for (const NodeInstance& s : std::vector<NodeInstance>(child->nodes())) {
        if (s.type == NodeType::SubGraphInput  ||
            s.type == NodeType::SubGraphOutput)
            child->removeNode(s.id);
    }

    // 5. Mover los nodos seleccionados al subgrafo.  No hay "move" directo:
    //    creamos copias dentro del child y luego borramos los originales.
    std::unordered_map<int, int> idMap;          // oldId (padre) → newId (child)
    std::unordered_map<int, ImVec2> oldPos;
    for (int id : sel) oldPos[id] = ImNodes::GetNodeScreenSpacePos(id);

    for (int oldId : sel) {
        const NodeInstance* src = active().findNode(oldId);
        if (!src) continue;
        int newId = 0;
        if (src->type == NodeType::Custom)        newId = child->addCustomNode(src->customType);
        else if (src->type == NodeType::SubGraph) {
            // Anidado: clonar el sub-grafo hijo del original al nuevo SubGraph.
            newId = child->addSubGraphNode();
            if (auto* nested = active().subGraphOf(oldId)) {
                if (auto* dst = child->subGraphOf(newId)) *dst = *nested;
                child->recomputeSubGraphPorts(newId);
            }
        }
        else                                       newId = child->addNode(src->type);
        idMap[oldId] = newId;
        for (const auto& [k, v] : src->params)        child->setParam(newId, k, v);
        for (const auto& [k, v] : src->stringParams)  child->setStringParam(newId, k, v);
        if (!src->assetPath.empty())                  child->setAssetPath(newId, src->assetPath);
    }

    // 6. Re-cablear las aristas internas en el child.
    for (const Edge& e : internalEdges) {
        auto fIt = idMap.find(e.fromNodeId);
        auto tIt = idMap.find(e.toNodeId);
        if (fIt == idMap.end() || tIt == idMap.end()) continue;
        const int newFrom = fIt->second * 10000 + (e.fromAttrId % 10000);
        const int newTo   = tIt->second * 10000 + (e.toAttrId   % 10000);
        child->tryAddEdge(newFrom, newTo);
    }

    // 7. Crear TODOS los SubGraphInput / SubGraphOutput stubs *primero* y
    //    sólo entonces llamar a `recomputeSubGraphPorts`.  Si creáramos
    //    los stubs intercalados con los `tryAddEdge` externos, R5 de la
    //    gramática rechazaría el segundo input externo (el SubGraph
    //    todavía pensaría que tiene 1 input port).
    std::vector<int> inStubIds, outStubIds;
    inStubIds.reserve(extInSources.size());
    outStubIds.reserve(mappedOutSrc.size());
    for (size_t k = 0; k < extInSources.size(); ++k) {
        const int stubId = child->addNode(NodeType::SubGraphInput);
        child->setParam(stubId, "Port", static_cast<double>(k));
        inStubIds.push_back(stubId);
    }
    for (size_t k = 0; k < mappedOutSrc.size(); ++k) {
        const int stubId = child->addNode(NodeType::SubGraphOutput);
        child->setParam(stubId, "Port", static_cast<double>(k));
        outStubIds.push_back(stubId);
    }
    active().recomputeSubGraphPorts(sgId);

    // 8. Cablear los stubs internamente (dentro del child).
    for (size_t k = 0; k < extInSources.size(); ++k) {
        const int origTarget = mappedInTargetAttr[k];
        auto tIt = idMap.find(origTarget / 10000);
        if (tIt == idMap.end()) continue;
        const int newTarget = tIt->second * 10000 + (origTarget % 10000);
        child->tryAddEdge(inStubIds[k] * 10000 + 9000, newTarget);
    }
    for (size_t k = 0; k < mappedOutSrc.size(); ++k) {
        const auto [origNodeId, origPort] = mappedOutSrc[k];
        auto fIt = idMap.find(origNodeId);
        if (fIt == idMap.end()) continue;
        const int newSource = fIt->second * 10000 + 9000 + origPort;
        child->tryAddEdge(newSource, outStubIds[k] * 10000);
    }

    // 9. Borrar los nodos originales del grafo padre.  Esto también
    //    elimina las aristas externas viejas, liberando los puertos
    //    externos para que los siguientes `tryAddEdge` desde el SubGraph
    //    no choquen con R5 (input port already connected).
    for (int oldId : sel) active().removeNode(oldId);

    // 10. Ya con los puertos externos libres, crear las nuevas aristas
    //     externas hacia/desde el SubGraph.
    for (size_t k = 0; k < extInSources.size(); ++k)
        active().tryAddEdge(extInSources[k],
                           sgId * 10000 + static_cast<int>(k));
    for (size_t k = 0; k < mappedOutSrc.size(); ++k) {
        const int sgOutAttr = sgId * 10000 + 9000 + static_cast<int>(k);
        for (int toAttr : outConsumers[k])
            active().tryAddEdge(sgOutAttr, toAttr);
    }

    // 10. Selección: queda seleccionado el nuevo SubGraph.
    ImNodes::ClearNodeSelection();
    ImNodes::SelectNode(sgId);
}
#endif

// ---------------------------------------------------------------------------
// SubGraph navigation
// ---------------------------------------------------------------------------
NodeGraph& NodeCanvas::active() {
    NodeGraph* g = &m_graph;
    // Recorre el stack por índice (NO por range-for) para poder recortarlo
    // de forma segura si un ancestro desaparece tras un undo.
    for (size_t i = 0; i < m_canvasStack.size(); ++i) {
        NodeGraph* child = g->subGraphOf(m_canvasStack[i]);
        if (!child) {
            // Recortar al ancestro válido más profundo en vez de saltar
            // al top-level — preserva la mayor parte del contexto del
            // usuario cuando sólo un sub-sub-grafo dejó de existir.
            m_canvasStack.resize(i);
            return *g;
        }
        g = child;
    }
    return *g;
}
const NodeGraph& NodeCanvas::active() const {
    const NodeGraph* g = &m_graph;
    for (size_t i = 0; i < m_canvasStack.size(); ++i) {
        const NodeGraph* child = g->subGraphOf(m_canvasStack[i]);
        if (!child) return *g;
        g = child;
    }
    return *g;
}

std::string NodeCanvas::canvasPathKey() const {
    std::string s = "/";
    for (int id : m_canvasStack) {
        s += std::to_string(id);
        s += '/';
    }
    return s;
}

ImNodesEditorContext* NodeCanvas::contextFor(const std::string& key) {
    auto it = m_editorContexts.find(key);
    if (it != m_editorContexts.end()) return it->second;
    ImNodesEditorContext* ctx = ImNodes::EditorContextCreate();
    m_editorContexts[key] = ctx;
    return ctx;
}

void NodeCanvas::enterSubGraph(int subGraphId) {
    if (!active().subGraphOf(subGraphId)) return;
    m_canvasStack.push_back(subGraphId);
    m_applyPositionsPending = false;   // child context has its own positions
}

void NodeCanvas::exitToLevel(int depth) {
    if (depth < 0) depth = 0;
    while (static_cast<int>(m_canvasStack.size()) > depth)
        m_canvasStack.pop_back();
}

void NodeCanvas::drawBreadcrumb() {
    ImGui::TextDisabled(" path:");
    ImGui::SameLine();
    if (ImGui::SmallButton("Top")) exitToLevel(0);
    NodeGraph* g = &m_graph;
    for (size_t i = 0; i < m_canvasStack.size(); ++i) {
        ImGui::SameLine(); ImGui::TextUnformatted(" / "); ImGui::SameLine();
        int id = m_canvasStack[i];
        // Usar el `Name` (stringParam) si existe, sino "SubGraph[id]".
        std::string label = "SubGraph";
        if (auto* n = g->findNode(id)) {
            auto it = n->stringParams.find("Name");
            if (it != n->stringParams.end() && !it->second.empty())
                label = it->second;
        }
        char btn[96];
        std::snprintf(btn, sizeof(btn), "%s##bc%zu", label.c_str(), i);
        if (ImGui::SmallButton(btn)) {
            exitToLevel(static_cast<int>(i) + 1);
            return;
        }
        if (auto* child = g->subGraphOf(id)) g = child;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("   |  F2 to rename selected SubGraph");
    ImGui::Separator();
}

// F2 sobre selección — si hay exactamente un SubGraph seleccionado, abre el
// popup de rename.  Otros tipos se ignoran (sin error: la futura
// extensión a "renombrar cualquier nodo" cabe aquí).
void NodeCanvas::handleRename() {
    if (m_renameNodeId != 0) return;   // popup ya abierto
    if (ImGui::IsAnyItemActive()) return;
    if (!ImGui::IsKeyPressed(ImGuiKey_F2, false)) return;

    const int n = ImNodes::NumSelectedNodes();
    if (n != 1) return;
    int id = 0;
    ImNodes::GetSelectedNodes(&id);
    if (id <= 0) return;
    const NodeInstance* node = active().findNode(id);
    if (!node || node->type != NodeType::SubGraph) return;

    m_renameNodeId       = id;
    m_renameFocusPending = true;
    auto it = node->stringParams.find("Name");
    const std::string cur = (it != node->stringParams.end()) ? it->second
                                                              : std::string();
    std::strncpy(m_renameBuf, cur.c_str(), sizeof(m_renameBuf) - 1);
    m_renameBuf[sizeof(m_renameBuf) - 1] = '\0';
    ImGui::OpenPopup("##RenameSubGraph");
}

void NodeCanvas::drawRenamePopup() {
    if (m_renameNodeId == 0) return;

    ImGui::SetNextWindowSize({280, 0}, ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##RenameSubGraph")) {
        ImGui::TextDisabled(" Rename SubGraph");
        ImGui::Separator();
        if (m_renameFocusPending) {
            ImGui::SetKeyboardFocusHere();
            m_renameFocusPending = false;
        }
        ImGui::SetNextItemWidth(-1);
        const bool entered = ImGui::InputText("##rn",
            m_renameBuf, sizeof(m_renameBuf),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        bool apply = entered || ImGui::Button("Apply");
        ImGui::SameLine();
        bool cancel = ImGui::Button("Cancel");
        if (apply) {
            recordSnapshot(m_graph.snapshot());
            active().setStringParam(m_renameNodeId, "Name", m_renameBuf);
            m_renameNodeId = 0;
            ImGui::CloseCurrentPopup();
        } else if (cancel) {
            m_renameNodeId = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else {
        // El popup fue cerrado (Esc o click fuera): limpiar estado.
        m_renameNodeId = 0;
    }
}

void NodeCanvas::handleUndoRedo() {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl) return;

    bool undo = ImGui::IsKeyPressed(ImGuiKey_Z);
    bool redo = ImGui::IsKeyPressed(ImGuiKey_Y) ||
                (ImGui::IsKeyPressed(ImGuiKey_Z) && io.KeyShift);

    if (undo && !io.KeyShift && m_history.canUndo()) doUndoOrRedo(true);
    if (redo                  && m_history.canRedo()) doUndoOrRedo(false);
}

void NodeCanvas::recordSnapshot(GraphSnapshot snap) {
    // Sync imnodes positions into the snapshot so the inverse op can
    // restore them.  Captures both top-level positions (which is what
    // imnodes knows about right now) — SubGraph contents that aren't
    // currently visible keep whatever positions they had captured by
    // an earlier record.
    syncPositionsFromImnodes();
    snap.positions.clear();
    for (const auto& [id, p] : m_positions)
        snap.positions[id] = { p.x, p.y };
    m_history.record(std::move(snap));
}

bool NodeCanvas::doUndoOrRedo(bool isUndo) {
    syncPositionsFromImnodes();
    GraphSnapshot cur = m_graph.snapshot();
    for (const auto& [id, p] : m_positions)
        cur.positions[id] = { p.x, p.y };

    auto restored = isUndo ? m_history.undo(std::move(cur))
                           : m_history.redo(std::move(cur));
    if (!restored) return false;

    m_graph.restoreSnapshot(*restored);
    bumpDirty();
    m_positions.clear();
    for (const auto& [id, p] : restored->positions)
        m_positions[id] = ScnVec2{ p.first, p.second };
    m_applyPositionsPending = !m_positions.empty();

    // El restore puede eliminar SubGraphs por los que estamos navegando.
    // `active()` recorta el stack al ancestro válido más profundo; lo
    // forzamos aquí para que la próxima llamada a `contextFor` resuelva
    // a un path-key consistente con el estado del grafo restaurado.
    (void)active();
    return true;
}

// ---------------------------------------------------------------------------
// handleZoom
// ---------------------------------------------------------------------------
void NodeCanvas::handleZoom() {
    // Nota: imnodes 0.5 no soporta zoom real del layout (no expone una
    // transformación global de coordenadas).  Lo que sí ofrece es pan vía
    // arrastre del medio-click sobre el canvas vacío + recentrado vía
    // ResetPanning.  En vez de mantener un \"zoom\" que sólo cambia el
    // tamaño del texto y no se nota visiblemente, mantenemos m_zoom = 1.0
    // y exponemos un atajo Home para encuadrar todos los nodos al centro.
    m_zoom = 1.0f;

    const bool hov = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

    // Home → frame all: recentra el panning de imnodes para que todos los
    // nodos queden visibles dentro del editor.  Calcula el bounding box de
    // las posiciones de los nodos y mueve el panning para centrarlo.
    if (hov && ImGui::IsKeyPressed(ImGuiKey_Home, false) &&
        active().nodeCount() > 0) {
        ImVec2 lo( 1e9f,  1e9f), hi(-1e9f, -1e9f);
        for (const auto& n : active().nodes()) {
            const ImVec2 p = ImNodes::GetNodeEditorSpacePos(n.id);
            lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
            hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
        }
        const ImVec2 bboxCenter(0.5f * (lo.x + hi.x), 0.5f * (lo.y + hi.y));
        const ImVec2 winSize = ImGui::GetContentRegionAvail();
        ImNodes::EditorContextResetPanning(
            ImVec2(0.5f * winSize.x - bboxCenter.x,
                   0.5f * winSize.y - bboxCenter.y));
    }
}

// ---------------------------------------------------------------------------
// showErrorTooltip — fades out after m_errorTimer seconds
// ---------------------------------------------------------------------------
void NodeCanvas::showErrorTooltip() {
    if (m_errorTimer <= 0.f) return;
    m_errorTimer -= ImGui::GetIO().DeltaTime;

    ImGui::SetNextWindowPos(
        { ImGui::GetWindowPos().x + 12.f,
          ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - 60.f });
    ImGui::SetNextWindowBgAlpha(std::min(1.f, m_errorTimer));
    ImGui::BeginTooltip();
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 90, 90, 255));
    ImGui::TextUnformatted(m_errorMsg.c_str());
    ImGui::PopStyleColor();
    ImGui::EndTooltip();
}

// ---------------------------------------------------------------------------
// drawAddPopup — Blender-style Add menu at cursor
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// openAddPopup — single entry point used by Shift+A, link-dropped and
// insert-in-edge flows. screenPos is the mouse position at the moment the
// gesture was completed; the popup will spawn there and the newly created
// node (if any) will be placed at the same coordinates.
// ---------------------------------------------------------------------------
void NodeCanvas::openAddPopup(ImVec2 screenPos,
                              int autoConnectAttr,
                              int insertEdgeId) {
    m_popupPos             = screenPos;
    m_popupAutoConnectAttr = autoConnectAttr;
    m_popupInsertEdgeId    = insertEdgeId;
    m_popupSearch[0]       = '\0';
    m_popupFocusSearch     = true;
    ImGui::OpenPopup("##AddNode");
}

// ---------------------------------------------------------------------------
// handleLinkDropped — when the user drags a link from a port and drops it on
// empty canvas (no target attribute), open the add-popup with auto-connect
// state set to the originating attribute. Picking a node from the popup will
// create it and immediately wire it up.
// ---------------------------------------------------------------------------
void NodeCanvas::handleLinkDropped() {
    int startedAt = 0;
    // The second argument 'including_detached_links' is true so we still
    // get a drop event when the user grabs an existing link's endpoint and
    // releases it in empty space — same as a fresh drag.
    if (!ImNodes::IsLinkDropped(&startedAt, /*including_detached=*/true)) return;
    if (startedAt == 0) return;
    openAddPopup(ImGui::GetMousePos(), startedAt);
}

// ---------------------------------------------------------------------------
// handleEdgeContextMenu — right-click on a link opens the add-popup with the
// edge marked for replacement. Picking a node inserts it between the edge's
// endpoints: old edge is removed, two new edges are wired (from → newNode:in0
// and newNode:out0 → to).
// ---------------------------------------------------------------------------
void NodeCanvas::handleEdgeContextMenu() {
    int hoveredLink = 0;
    if (!ImNodes::IsLinkHovered(&hoveredLink)) return;
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Right)) return;
    if (hoveredLink <= 0) return;
    openAddPopup(ImGui::GetMousePos(), /*autoConnect=*/0, hoveredLink);
}

void NodeCanvas::drawAddPopup() {
    ImGui::SetNextWindowPos(m_popupPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize({240, 0}, ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_PopupBg,      IM_COL32( 30,  32,  36, 245));
    ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32( 43,  80, 140, 200));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32( 60, 110, 190, 220));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  IM_COL32( 75, 130, 210, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 8});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   {6, 4});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.f);

    if (ImGui::BeginPopup("##AddNode")) {
        // Header reflects the gesture that opened the popup.
        const char* header =
            (m_popupInsertEdgeId    != 0) ? " Insert Node Between" :
            (m_popupAutoConnectAttr != 0) ? " Connect To New Node" :
                                            " Add Node";
        ImGui::TextDisabled("%s", header);
        ImGui::Separator();

        // Typeahead search box, auto-focused on first frame of the popup.
        if (m_popupFocusSearch) {
            ImGui::SetKeyboardFocusHere();
            m_popupFocusSearch = false;
        }
        ImGui::SetNextItemWidth(-1);
        const bool searchEntered =
            ImGui::InputTextWithHint("##search", "type to search…",
                                     m_popupSearch, sizeof(m_popupSearch),
                                     ImGuiInputTextFlags_EnterReturnsTrue);
        const bool searchActive = (m_popupSearch[0] != '\0');
        ImGui::Spacing();

        // ---------------- helpers ------------------------------------------
        // Wires the newly created node into the surrounding graph as dictated
        // by the popup state (auto-connect or insert-in-edge), then resets
        // the state.  Errors are surfaced via the existing tooltip.
        auto wireNewNode = [&](int newNodeId) {
            if (m_popupAutoConnectAttr != 0) {
                const bool fromIsOutput =
                    (m_popupAutoConnectAttr % 10000) >= 9000;
                // First port on the other end of the new node:
                //   came from an output → connect to its input 0;
                //   came from an input  → connect to its output 0.
                const int otherEnd = fromIsOutput
                    ? (newNodeId * 10000)
                    : (newNodeId * 10000 + 9000);
                auto err = active().tryAddEdge(m_popupAutoConnectAttr, otherEnd);
                if (err) {
                    m_errorMsg   = "[" + err->rule + "]  " + err->message;
                    m_errorTimer = 3.5f;
                }
            }
            if (m_popupInsertEdgeId != 0) {
                if (const Edge* e = active().findEdge(m_popupInsertEdgeId)) {
                    const int fromAttr = e->fromAttrId;
                    const int toAttr   = e->toAttrId;
                    active().removeEdge(m_popupInsertEdgeId);
                    auto e1 = active().tryAddEdge(fromAttr,
                                                 newNodeId * 10000);
                    auto e2 = active().tryAddEdge(newNodeId * 10000 + 9000,
                                                 toAttr);
                    if (e1 || e2) {
                        const auto& err = e1 ? *e1 : *e2;
                        m_errorMsg   = "[" + err.rule + "]  " + err.message;
                        m_errorTimer = 3.5f;
                    }
                }
            }
            m_popupAutoConnectAttr = 0;
            m_popupInsertEdgeId    = 0;
        };

        // Common pick action used by both the categorized menus and the
        // flat search results.
        auto pickType = [&](NodeType t) {
            const int newId = addNode(t);
            wireNewNode(newId);
            ImGui::CloseCurrentPopup();
        };
        auto pickCustom = [&](const std::string& tid) {
            const int newId = addCustomNode(tid);
            wireNewNode(newId);
            ImGui::CloseCurrentPopup();
        };

        // Case-insensitive substring match.
        auto matchesSearch = [&](const std::string& label) -> bool {
            if (!searchActive) return true;
            std::string a = label;
            std::string b = m_popupSearch;
            for (char& c : a) c = (char)std::tolower((unsigned char)c);
            for (char& c : b) c = (char)std::tolower((unsigned char)c);
            return a.find(b) != std::string::npos;
        };

        auto menuItem = [&](NodeType t) {
            const NodeDef& d = nodeRegistry().at(t);
            if (ImGui::MenuItem(d.label.c_str())) pickType(t);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", d.description.c_str());
        };

        // -------- Flat typeahead result list (replaces categorized view) ---
        if (searchActive) {
            int shown = 0;
            std::optional<NodeType>  firstBuiltin;
            std::optional<std::string> firstCustom;

            for (const auto& [t, d] : nodeRegistry()) {
                if (!matchesSearch(d.label)) continue;
                if (!firstBuiltin && !firstCustom) firstBuiltin = t;
                if (ImGui::MenuItem(d.label.c_str())) pickType(t);
                if (ImGui::IsItemHovered() && !d.description.empty())
                    ImGui::SetTooltip("%s", d.description.c_str());
                ++shown;
            }
            for (const auto& tid : scinodes::customNodes().typeIds()) {
                const auto* cd = scinodes::customNodes().find(tid);
                if (!cd || !matchesSearch(cd->label)) continue;
                if (!firstBuiltin && !firstCustom) firstCustom = tid;
                if (ImGui::MenuItem(cd->label.c_str())) pickCustom(tid);
                if (ImGui::IsItemHovered() && !cd->description.empty())
                    ImGui::SetTooltip("%s", cd->description.c_str());
                ++shown;
            }
            if (shown == 0) {
                ImGui::TextDisabled("  no matches");
            }
            // Enter on the search box picks the first match.
            if (searchEntered) {
                if (firstBuiltin)     pickType(*firstBuiltin);
                else if (firstCustom) pickCustom(*firstCustom);
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(140,140,140,180));
            ImGui::TextUnformatted("  Enter: pick first   Esc: close");
            ImGui::PopStyleColor();
            ImGui::EndPopup();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(4);
            return;
        }
        // -------- end flat typeahead ---------------------------------------

        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32( 90, 200, 110, 255));
        bool srcOpen = ImGui::BeginMenu("  Sources");
        ImGui::PopStyleColor();
        if (srcOpen) {
            for (NodeType t : { NodeType::VoltageSource, NodeType::CurrentSource,
                                NodeType::StepSignal, NodeType::SineSignal,
                                NodeType::RampSignal })
                menuItem(t);
            ImGui::EndMenu();
        }

        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32( 90, 130, 220, 255));
        bool txOpen = ImGui::BeginMenu("  Transformers");
        ImGui::PopStyleColor();
        if (txOpen) {
            for (NodeType t : { NodeType::Gain, NodeType::Summation,
                                NodeType::Integrator, NodeType::Differentiator,
                                NodeType::LowPassFilter, NodeType::PIDController,
                                NodeType::TransferFunction,
                                NodeType::TransferFunction2,
                                NodeType::Saturation,
                                NodeType::GearTransmission,
                                NodeType::InverseKinematics })
                menuItem(t);
            ImGui::EndMenu();
        }

        // Devices — categoría gramatical Device.  Comportamiento de
        // transformador en R1-R5 pero llevan modelo 3-D asociado vía
        // contrato (sec. geometry-contracts).  Coloreado púrpura.
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160,  90, 200, 255));
        bool dvOpen = ImGui::BeginMenu("  Devices");
        ImGui::PopStyleColor();
        if (dvOpen) {
            for (NodeType t : { NodeType::DCMotorModel })
                menuItem(t);
            ImGui::EndMenu();
        }

        // Sizing & Electromagnetics (v0.8) — multiphysics-design nodes.
        // Coloured purple to distinguish from generic Source/Transformer/Sink.
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 120, 220, 255));
        bool szOpen = ImGui::BeginMenu("  Sizing");
        ImGui::PopStyleColor();
        if (szOpen) {
            for (NodeType t : { NodeType::DesignTemplate,
                                NodeType::PMSMSizing,
                                NodeType::IPMSizing,
                                NodeType::BLDCSizing,
                                NodeType::PMSMElectromagnetic,
                                NodeType::AirgapFluxDensity,
                                NodeType::PMSMEfficiency })
                menuItem(t);
            ImGui::EndMenu();
        }

        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220,  80,  80, 255));
        bool snkOpen = ImGui::BeginMenu("  Sinks");
        ImGui::PopStyleColor();
        if (snkOpen) {
            for (NodeType t : { NodeType::Oscilloscope, NodeType::FFTAnalyzer,
                                NodeType::PhasePortrait, NodeType::DataLogger,
                                NodeType::TerminalDisplay, NodeType::View3DSink,
                                NodeType::View3DThermalSink,
                                NodeType::View3DDeformationSink,
                                NodeType::HeatmapSink,
                                NodeType::DistributionSink })
                menuItem(t);
            ImGui::EndMenu();
        }

        // Structural & NVH (v1.0) — Maxwell forces + modal frequencies.
        // Pink-tinted so it reads distinct from Thermal's orange.
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 110, 170, 255));
        bool nvhOpen = ImGui::BeginMenu("  Structural");
        ImGui::PopStyleColor();
        if (nvhOpen) {
            for (NodeType t : { NodeType::MaxwellForce,
                                NodeType::ModalFrequency,
                                NodeType::TolerancePerturbator })
                menuItem(t);
            ImGui::EndMenu();
        }

        // Thermal Network (v0.9) — losses + lumped RC nodes. Coloured
        // orange to read as "heat".
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 140,  60, 255));
        bool thOpen = ImGui::BeginMenu("  Thermal");
        ImGui::PopStyleColor();
        if (thOpen) {
            for (NodeType t : { NodeType::JouleLoss,
                                NodeType::CoreLoss,
                                NodeType::MechanicalLoss,
                                NodeType::ThermalMass,
                                NodeType::ThermalNode,
                                NodeType::ThermalResistance,
                                NodeType::CoolingSystem,
                                NodeType::ConvectiveCooling })
                menuItem(t);
            ImGui::EndMenu();
        }

        // ---- Custom (JSON-loaded) types ---------------------------------
        auto customIds = scinodes::customNodes().typeIds();
        std::sort(customIds.begin(), customIds.end());

        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 160, 80, 255));
        bool cusOpen = ImGui::BeginMenu("  Custom");
        ImGui::PopStyleColor();
        if (cusOpen) {
            if (customIds.empty()) {
                ImGui::TextDisabled("  No custom nodes loaded.");
            } else {
                for (const auto& tid : customIds) {
                    const auto* cd =
                        scinodes::customNodes().find(tid);
                    if (!cd) continue;
                    if (ImGui::MenuItem(cd->label.c_str())) pickCustom(tid);
                    if (ImGui::IsItemHovered() && !cd->description.empty())
                        ImGui::SetTooltip("%s", cd->description.c_str());
                }
            }
            ImGui::Separator();
            bool busy = m_loadCustomDialog.isOpen();
            ImGui::BeginDisabled(busy);
            if (ImGui::MenuItem("Load from JSON…")) {
                m_loadCustomDialog.open(
                    FileDialog::Mode::Open,
                    "Load Custom Node Descriptor",
                    { "JSON descriptor (*.json)", "*.json" });
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(140,140,140,180));
        ImGui::TextUnformatted("  Shift+A  |  RMB on link: insert");
        ImGui::TextUnformatted("  drag from port + release: connect");
        ImGui::PopStyleColor();
        if (!m_customLoadStatus.empty()) {
            ImGui::TextDisabled("  %s", m_customLoadStatus.c_str());
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
}

// ---------------------------------------------------------------------------
// addNode — records undo then adds
// ---------------------------------------------------------------------------
int NodeCanvas::addNode(NodeType type) {
    recordSnapshot(m_graph.snapshot());
    bumpDirty();
    const int newId = active().addNode(type);
    // Spawn en la posición del cursor cuando se abrió el popup.
    // m_popupPos está en screen-space; imnodes opera en grid-space.
    // Aplicamos la transformación inversa para que el nodo aparezca
    // exactamente donde el usuario invocó Shift+A.  Si m_popupPos es
    // (0,0) (no hubo popup, p.ej. add programático), dejamos la
    // posición por defecto de imnodes.
    if (m_popupPos.x != 0.f || m_popupPos.y != 0.f) {
        ImNodes::SetNodeScreenSpacePos(newId, m_popupPos);
    }
    return newId;
}

int NodeCanvas::addCustomNode(const std::string& customType) {
    recordSnapshot(m_graph.snapshot());
    bumpDirty();
    const int newId = active().addCustomNode(customType);
    if (m_popupPos.x != 0.f || m_popupPos.y != 0.f) {
        ImNodes::SetNodeScreenSpacePos(newId, m_popupPos);
    }
    return newId;
}

// ---------------------------------------------------------------------------
// addNodeAt — used by auto-connect / insert-in-edge flows that need to know
// the new node's id so they can wire it up immediately. Equivalent to
// addNode(type) but takes the desired screen-space position explicitly.
// ---------------------------------------------------------------------------
int NodeCanvas::addNodeAt(NodeType type, ImVec2 canvasPos) {
    recordSnapshot(m_graph.snapshot());
    bumpDirty();
    const int newId = active().addNode(type);
    ImNodes::SetNodeScreenSpacePos(newId, canvasPos);
    return newId;
}

void NodeCanvas::clear() {
    m_history.clear();
    m_graph = NodeGraph{};
    m_positions.clear();
    m_readOnly = false;
    m_applyPositionsPending = false;
}

void NodeCanvas::resetView() {
    ImNodes::EditorContextResetPanning({0, 0});
}

// ---------------------------------------------------------------------------
// Persistence — sync positions, then hand the graph to ScnSerializer
// ---------------------------------------------------------------------------
void NodeCanvas::syncPositionsFromImnodes() {
    // Persistence only handles the top-level graph for now (Fase E
    // pending).  We may be navigating inside a SubGraph context where
    // top-level node ids aren't known, so explicitly switch to the
    // top-level imnodes editor context, read positions, then restore
    // whatever context the user was on.
    ImNodesEditorContext* topCtx = contextFor("/");
    ImNodes::EditorContextSet(topCtx);
    m_positions.clear();
    for (const auto& n : m_graph.nodes()) {
        ImVec2 p = ImNodes::GetNodeEditorSpacePos(n.id);
        m_positions[n.id] = ScnVec2{ p.x, p.y };
    }
    ImNodes::EditorContextSet(contextFor(canvasPathKey()));
}

void NodeCanvas::applyPositionsToImnodes() {
    // m_positions contiene IDs del top-level.  Si el usuario está
    // navegando dentro de un SubGraph, el imnodes context activo es el
    // del subgrafo y aplicarle SetNodeEditorSpacePos con IDs ajenos
    // corrompe estado interno (crash al refrescar las posiciones tras un
    // undo dentro del subgrafo).  Cambiamos al top-level context para
    // aplicar y restauramos el contexto del nivel actual al salir.
    ImNodesEditorContext* topCtx = contextFor("/");
    ImNodes::EditorContextSet(topCtx);
    for (const auto& [id, p] : m_positions)
        ImNodes::SetNodeEditorSpacePos(id, ImVec2{ p.x, p.y });
    ImNodes::EditorContextSet(contextFor(canvasPathKey()));
}

bool NodeCanvas::saveToFile(const std::string& path) {
    syncPositionsFromImnodes();
    return ScnSerializer::saveToFile(path, m_graph, m_positions);
}

LoadReport NodeCanvas::loadFromFile(const std::string& path) {
    m_history.clear();
    m_positions.clear();
    auto report = ScnSerializer::loadFromFile(path, m_graph, m_positions);
    m_applyPositionsPending = !m_positions.empty();
    m_readOnly = report.ok && report.hasViolations();
    bumpDirty();
    // Auto-load de assets glTF: para cada Device node con `assetPath`
    // no vacío, validar el asset contra el contrato del tipo y cachearlo
    // en m_assetService.  Sin esto el View3DPanel cae al renderer
    // procedural aunque el .scn especifique un .gltf.
    if (m_assetService) {
        // Propaga el directorio del .scn al AssetService para que pueda
        // resolver rutas relativas (assetPath = "examples/dc_motor/...")
        // contra él y sus ancestros — necesario cuando el binario corre
        // desde build/.
        m_assetService->setBaseDir(
            std::filesystem::path(path).parent_path().string());
        for (const auto& n : m_graph.nodes()) {
            if (n.assetPath.empty()) continue;
            if (defOf(n).category != NodeCategory::Device) continue;
            reloadAssetFor(n.id);
        }
    }
    return report;
}

void NodeCanvas::reloadAssetFor(int nodeId) {
    if (!m_assetService) return;
    const NodeInstance* n = active().findNode(nodeId);
    if (!n) { m_assetService->detach(nodeId); return; }
    m_assetService->reload(nodeId, typeName(n->type), n->assetPath);
}

void NodeCanvas::detachAsset(int nodeId) {
    active().setAssetPath(nodeId, "");
    if (m_assetService) m_assetService->detach(nodeId);
}

const std::unordered_map<int, scinodes::DeviceAsset>&
NodeCanvas::loadedAssets() const {
    static const std::unordered_map<int, scinodes::DeviceAsset> kEmpty;
    return m_assetService ? m_assetService->all() : kEmpty;
}

void NodeCanvas::openMappingPanelFor(int nodeId) {
    const NodeInstance* n = active().findNode(nodeId);
    if (!n || n->assetPath.empty()) return;

    const auto* contract =
        (m_contractRegistry ? m_contractRegistry->find(typeName(n->type)) : nullptr);
    if (!contract) return;

    const std::string resolved =
        m_assetService ? m_assetService->resolveAssetPath(n->assetPath)
                       : n->assetPath;
    if (m_mappingPanel.openFor(resolved, *contract)) {
        m_mappingNodeId = nodeId;
    } else {
        m_errorMsg   = "No se pudo abrir el panel de mapping (¿glTF inválido?)";
        m_errorTimer = 5.0f;
    }
}
