#include "NodeCanvas.hpp"
#include "canvas/Canvas.hpp"
#include "../app/AssetService.hpp"
#include "../core/ContractRegistry.hpp"
#include "../core/CsvParamIO.hpp"
#include "../core/CustomNodeRegistry.hpp"
#include "../core/DeviceAsset.hpp"
#include "../core/DimensionalAnalyzer.hpp"
#include "../core/I18n.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstring>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <queue>
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
// Estado interno del canvas que no depende del renderer.  El renderer
// (ImNodesRenderer) se inicializa en AppWindow antes de inyectarlo aquí
// vía setRenderer(); este init() es solo para futuras inicializaciones
// específicas del NodeCanvas (hoy, ninguna).
void NodeCanvas::init() {
    // Por ahora no hay estado propio que inicializar.  El renderer
    // ya fue inicializado por AppWindow.
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

    // Activar el contexto del nivel actual.  Cada subgrafo tiene su
    // propio espacio de ids/posiciones — la cache vive ahora dentro
    // del renderer (ImNodesRenderer mantiene el unordered_map por
    // path-key); aquí solo le decimos qué contexto abrir.
    ImGui::SetWindowFontScale(m_zoom);
    m_renderer->beginCanvas(canvasPathKey());

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

    m_renderer->endCanvas();
    if (m_readOnly) ImGui::EndDisabled();
    ImGui::SetWindowFontScale(1.0f);


    // Tooltip de comentario via Ctrl+hover.  Solo aparece cuando el
    // usuario tiene Ctrl presionado y el cursor sobre un nodo con
    // comentario no-vacío.  Sin Ctrl el tooltip no se dispara — esa
    // es la regla unificada de la UI ("Ctrl para más info").
    if (ImGui::GetIO().KeyCtrl) {
        int hoveredNodeId = 0;
        if (m_renderer->isNodeHovered(hoveredNodeId)) {
            if (const NodeInstance* hn = active().findNode(hoveredNodeId)) {
                if (!hn->comment.empty()) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(360.f);
                    ImGui::TextUnformatted(hn->comment.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }
        }
    }

    // El overlay con los atajos vive ahora dentro del native renderer
    // (NativeNodeRenderer::endCanvas), donde tiene acceso al child
    // window correcto del canvas.  Antes se dibujaba aquí pero quedaba
    // fuera del clip rect tras el refactor del renderer.

    if (!m_readOnly) {
        handleLinkCreated();
        handleLinkDropped();
        handleEdgeContextMenu();
        handleDeletion();
        handleCopyPaste();
        handleEncapsulate();
        handleRename();
        handleAutoLayout();
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
    if (!m_renderer->isNodeHovered(hoveredId)) return;
    if (!ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) return;
    if (hoveredId <= 0) return;
    // Si el doble-click recayó sobre un widget activo (DragFloat
    // entrando a modo text-input, InputText, …), ese widget lo consume
    // y el panel flotante NO debe abrirse — sería un editor encima del
    // editor que el usuario quería usar.
    if (ImGui::IsAnyItemActive()) return;
    // Doble-click sobre un SubGraph: navegar al subgrafo en vez de
    // abrir el panel de parámetros.  El resto de tipos abren panel.
    if (const NodeInstance* n = active().findNode(hoveredId)) {
        if (isSubGraphContainer(n->type)) {
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

    const std::string nodeLbl = scinodes::trOr(
        std::string("node.") + typeName(n->type) + ".label", def.label);
    char title[80];
    std::snprintf(title, sizeof(title), "%s  #%d###paramPanel",
                  nodeLbl.c_str(), n->id);

    ImGui::SetNextWindowPos(m_paramPanelPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize({340, 0}, ImGuiCond_Appearing);
    bool open = true;
    if (ImGui::Begin(title, &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Descripción wrappeada — sin esto AlwaysAutoResize hacía el
        // panel tan ancho como la oración más larga (los nodos del
        // sub-grafo Geometry tienen descripciones de ~150 chars).
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 310);
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", scinodes::trOr(
            std::string("node.") + typeName(n->type) + ".description",
            def.description).c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();

        // ---- Import / Export row (only if the node has any params) ----
        // Alias (etapa 6I.U): sus params (target_node_id, target_port) son
        // identificadores no numéricos.  Bulk-edit via CSV no aplica.
        const bool hasCsvParams = !def.params.empty()
                                  && n->type != NodeType::Alias;
        if (hasCsvParams) {
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

        // ---- Asset 3D legacy (PATH A) — sólo informativo / quitar ---------
        // Desde el refactor del sub-grafo de escena (`Object3D` +
        // `TransformObject` + `SceneOutput`), los Device nodes ya NO
        // poseen su asset 3D — la geometría vive en el catálogo del
        // proyecto y se referencia desde Object3D.  La sección sólo
        // aparece cuando el nodo arrastra un `assetPath` heredado de un
        // .scn 0.4 legacy.  La acción ofrecida es QUITAR el legacy y
        // un texto que dirige al usuario al flujo nuevo — no hay forma
        // desde la UI de re-cargar un asset al Device.
        if (def.category == NodeCategory::Device && !n->assetPath.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Asset 3D heredado (legacy)");

            const auto* contract =
                (m_contractRegistry ? m_contractRegistry->find(typeName(n->type)) : nullptr);

            ImGui::TextWrapped("%s", n->assetPath.c_str());

            if (contract) {
                const scinodes::DeviceAsset* asset =
                    m_assetService ? m_assetService->find(n->id) : nullptr;
                if (!asset) {
                    reloadAssetFor(n->id);
                    asset = m_assetService ? m_assetService->find(n->id) : nullptr;
                }
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
            } else {
                ImGui::TextDisabled("(sin contrato registrado para %s)",
                                    typeName(n->type));
            }

            ImGui::TextDisabled("Para asignar geometría a este device usá "
                                "el sub-grafo de escena: Archivo → Importar "
                                "modelo 3D, luego Object3D + Transform Object "
                                "+ Scene Output.");
            if (ImGui::SmallButton("Quitar asset heredado")) {
                active().setAssetPath(n->id, "");
                if (m_assetService) m_assetService->detach(n->id);
            }
        }

        ImGui::Separator();

        if (m_readOnly) ImGui::BeginDisabled();

        for (int i = 0; i < (int)def.params.size(); ++i) {
            const auto& pd = def.params[i];
            float val = (float)n->params.at(pd.name);

            ImGui::PushID(i);
            ImGui::AlignTextToFramePadding();
            // Label traducido (clave node.<type>.param.<paramId>),
            // fallback al pd.name literal.
            const std::string paramLbl = scinodes::trOr(
                std::string("node.") + typeName(n->type) + ".param." + pd.name,
                pd.name);
            ImGui::TextUnformatted(paramLbl.c_str());
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
                // Drag mode: mutamos el modelo + notificamos al solver
                // por cada delta — el usuario espera feedback fluido.
                // Text-input mode: ImGui mantiene el buffer interno con
                // el valor en edición; NO mutamos el modelo ni mandamos
                // al solver hasta IsItemDeactivatedAfterEdit (Enter o
                // focus loss).  Sin esto, escribir "1.5708" pasaba por
                // "1", "15", "157", "1570", "15708" como valores
                // intermedios — visible inmediatamente en el render
                // del sub-grafo de escena (que evalúa cada frame).
                if (!isTextInput) {
                    active().setParam(n->id, pd.name, (double)val);
                    if (m_paramCallback)
                        m_paramCallback(pathFor(n->id), i, (double)val);
                }
            }

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                // Commit final tras text-input (Enter o focus loss) o
                // tras un drag completo.  Mutamos modelo + solver una
                // sola vez con el valor definitivo.  En drag mode
                // muchas de estas escrituras serán redundantes con la
                // última actualización del bloque `changed`; el costo
                // es trivial y la simetría worth it.
                active().setParam(n->id, pd.name, (double)val);
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
        // Object3D — combo "objectRef" alimentado por el catálogo del
        // proyecto (NodeGraph::importedObjects()).  Cada entrada del
        // combo es "<objectName>" o "<objectName>/<partName>".  Sin esto
        // los Object3D recién creados no saben qué del catálogo
        // referenciar y el View3DPanel los rendera como placeholder.
        // -----------------------------------------------------------------
        if (n->type == NodeType::Object3D) {
            ImGui::Separator();
            ImGui::TextDisabled("Referencia al catálogo");
            const std::string curRef =
                n->stringParams.count("objectRef")
                    ? n->stringParams.at("objectRef")
                    : std::string{};

            // Build options: "<name>" + "<name>/<partName>" por cada part.
            std::vector<std::string> options;
            options.push_back("");   // <ninguno> — limpia el ref
            for (const auto& obj : m_graph.importedObjects()) {
                options.push_back(obj.name);
                for (const auto& part : obj.parts)
                    options.push_back(obj.name + "/" + part);
            }

            const char* preview = curRef.empty() ? "(sin asignar)" : curRef.c_str();
            ImGui::SetNextItemWidth(260.f);
            if (ImGui::BeginCombo("##objectRef", preview)) {
                for (const auto& opt : options) {
                    const bool sel = (opt == curRef);
                    const char* lbl = opt.empty() ? "(sin asignar)" : opt.c_str();
                    if (ImGui::Selectable(lbl, sel))
                        active().setStringParam(n->id, "objectRef", opt);
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (m_graph.importedObjects().empty()) {
                ImGui::TextDisabled("  (catálogo vacío — Archivo → Importar modelo 3D)");
            }
        }

        // -----------------------------------------------------------------
        // Alias (etapa 6I.U): selector de "referenciar a..." — dropdown
        // con todos los nodos del grafo, agrupados por nodo y mostrando
        // sus output ports.  Al elegir, se persisten target_node_id y
        // target_port en params.  Sin asignación, el alias emite 0.0
        // y el analyzer dimensional NO puede resolver su unit.
        // -----------------------------------------------------------------
        if (n->type == NodeType::Alias) {
            ImGui::Separator();
            ImGui::TextDisabled("Referenciar nodo");
            const int curTid  = static_cast<int>(n->params.count("target_node_id")
                                                  ? n->params.at("target_node_id")
                                                  : 0);
            const int curPort = static_cast<int>(n->params.count("target_port")
                                                  ? n->params.at("target_port")
                                                  : 0);
            const NodeInstance* cur = active().findNode(curTid);
            std::string preview;
            if (cur) {
                auto it = cur->stringParams.find("Name");
                std::string nm = (it != cur->stringParams.end() && !it->second.empty())
                    ? it->second
                    : std::string(labelOf(cur->type));
                preview = nm + "  [out " + std::to_string(curPort + 1) + "]";
            } else {
                preview = "(sin asignar)";
            }
            ImGui::SetNextItemWidth(280.f);
            if (ImGui::BeginCombo("##aliasTarget", preview.c_str())) {
                // Opción para borrar.
                if (ImGui::Selectable("(sin asignar)", curTid == 0)) {
                    active().setParam(n->id, "target_node_id", 0.0);
                    active().setParam(n->id, "target_port",    0.0);
                }
                ImGui::Separator();
                // Listar todos los nodos del grafo (excluyéndose a sí mismo
                // y a otros Aliases para evitar cadenas) con sus outputs.
                for (const NodeInstance& other : active().nodes()) {
                    if (other.id == n->id) continue;
                    if (other.type == NodeType::Alias) continue;
                    const NodeDef& d = defOf(other);
                    if (d.outputPorts <= 0) continue;
                    auto it = other.stringParams.find("Name");
                    std::string nm = (it != other.stringParams.end() && !it->second.empty())
                        ? it->second
                        : std::string(labelOf(other.type));
                    for (int p = 0; p < d.outputPorts; ++p) {
                        std::string label = nm + "  [out " + std::to_string(p + 1) + "]";
                        if (d.outputPortLabels.size() > (size_t)p &&
                            !d.outputPortLabels[p].empty()) {
                            label = nm + "  [" + d.outputPortLabels[p] + "]";
                        }
                        const bool isSel = (other.id == curTid && p == curPort);
                        ImGui::PushID(other.id * 1000 + p);
                        if (ImGui::Selectable(label.c_str(), isSel)) {
                            active().setParam(n->id, "target_node_id",
                                              static_cast<double>(other.id));
                            active().setParam(n->id, "target_port",
                                              static_cast<double>(p));
                        }
                        if (isSel) ImGui::SetItemDefaultFocus();
                        ImGui::PopID();
                    }
                }
                ImGui::EndCombo();
            }
            if (curTid > 0 && !cur) {
                ImGui::TextColored({0.95f, 0.5f, 0.3f, 1.0f},
                    "✗ El nodo referenciado (id=%d) no existe", curTid);
            } else if (cur) {
                // Diagnóstico: mostrar el ID del target para que el
                // usuario pueda correlacionar "qué nodo es" cuando
                // dos nodos comparten Name o el grafo es grande.
                ImGui::TextDisabled("(id del target: %d)", curTid);
            }
        }

        if (n->type == NodeType::Oscilloscope) {
            ImGui::Separator();
            ImGui::TextDisabled("Canales conectados");
            const NodeDef& d = defOf(*n);
            // Etapa 6I.J: la unidad ya NO se tipea acá — el analyzer
            // la infiere desde la señal upstream.  El panel sólo
            // conserva el label editable (anotación libre del usuario,
            // ej. "θ(t) — posición articular") y muestra la unidad
            // detectada como TextDisabled read-only.
            const auto analysis = scinodes::analyzeUnits(active());
            int countShown = 0;
            for (int port = 0; port < d.inputPorts; ++port) {
                int srcId = -1;
                for (const auto& e : active().edges()) {
                    if (e.toNodeId == n->id &&
                        attrInputPort(e.toAttrId) == port) {
                        srcId = e.fromNodeId; break;
                    }
                }
                if (srcId < 0) continue;
                ++countShown;
                char keyL[32];
                std::snprintf(keyL, sizeof(keyL), "portLabel%d", port);
                std::string curL = n->stringParams.count(keyL)
                    ? n->stringParams.at(keyL) : std::string{};
                char bufL[128];
                std::strncpy(bufL, curL.c_str(), sizeof(bufL) - 1);
                bufL[sizeof(bufL) - 1] = 0;

                // Unidad inferida (read-only).  Sin sufijo si el
                // analyzer no resolvió o el resultado es adimensional×1.
                std::string inferredUnit;
                const int inAttr = n->inputAttrId(port);
                if (analysis.isResolved(inAttr)) {
                    scinodes::Unit u = analysis.unitAt(inAttr);
                    if (!u.isDimensionless() ||
                        std::fabs(u.magnitude - 1.0) > 1e-12) {
                        inferredUnit = u.toCanonicalString();
                    }
                }

                ImGui::PushID(port);
                ImGui::Text("in %d", port + 1);
                ImGui::SameLine(60.f);
                ImGui::SetNextItemWidth(200.f);
                if (ImGui::InputTextWithHint("##lab", "nombre (ej. θ(t))",
                                             bufL, sizeof(bufL)))
                    active().setStringParam(n->id, keyL, bufL);
                ImGui::SameLine();
                if (inferredUnit.empty())
                    ImGui::TextDisabled("[u?]");
                else
                    ImGui::TextDisabled("[%s]", inferredUnit.c_str());
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
    // Alias (etapa 6I.U): color distinto al verde Source para que se
    // vea de un vistazo que es un nodo "virtual" — amarillo/oro. Las
    // Sources reales (StepSignal, VoltageSource) se mantienen verdes.
    auto categoryColor = [&]() -> ImU32 {
        if (n.type == NodeType::Alias)
            return IM_COL32(170, 130,  40, 255);   // ámbar
        return titleCol(def.category);
    };
    auto categoryHovColor = [&]() -> ImU32 {
        if (n.type == NodeType::Alias)
            return IM_COL32(210, 170,  70, 255);
        return titleHovCol(def.category);
    };
    const ImU32 titleColor    = highlighted ? IM_COL32(220, 50, 50, 255)
                                            : categoryColor();
    const ImU32 titleHovColor = highlighted ? IM_COL32(240, 80, 80, 255)
                                            : categoryHovColor();
    using CK = scinodes::ui::INodeRenderer::ColorKey;
    m_renderer->pushColor(CK::TitleBar,         titleColor);
    m_renderer->pushColor(CK::TitleBarHovered,  titleHovColor);
    m_renderer->pushColor(CK::TitleBarSelected, titleHovColor);

    m_renderer->beginNode(n.id, scinodes::ui::computeNodeDimensions(n),
                          /*hasComment*/ !n.comment.empty());

    m_renderer->beginNodeTitleBar();
    // SubGraph: si tiene `Name` en stringParams, usarlo como título.
    // Para el resto: clave i18n `node.<typeName>.label` con fallback al
    // def.label del registry (mantiene el nombre original si no hay
    // traducción).  Custom nodes y stubs caen al fallback también.
    {
        // El nombre custom (stringParams["Name"]) tiene prioridad sobre
        // el label del registry para TODOS los nodos (etapa 6I.T).
        // Para Alias (etapa 6I.U), el título por default refleja el
        // TARGET al que apunta: "→ <Name del target>" o el id si no
        // hay target asignado.  El user puede sobrescribir con F2.
        std::string title;
        auto it = n.stringParams.find("Name");
        if (it != n.stringParams.end() && !it->second.empty()) {
            title = it->second;
        }
        if (title.empty() && n.type == NodeType::Alias) {
            auto pt = n.params.find("target_node_id");
            const int tid = (pt != n.params.end())
                ? static_cast<int>(pt->second) : 0;
            const NodeInstance* tgt = active().findNode(tid);
            if (tgt) {
                auto tit = tgt->stringParams.find("Name");
                if (tit != tgt->stringParams.end() && !tit->second.empty())
                    title = "→ " + tit->second;
                else
                    title = "→ " + std::string(labelOf(tgt->type));
            } else {
                title = "Alias (sin asignar)";
            }
        }
        if (title.empty()) {
            const std::string key =
                std::string("node.") + typeName(n.type) + ".label";
            title = scinodes::trOr(key, def.label);
        }
        ImGui::TextUnformatted(title.c_str());
    }
    m_renderer->endNodeTitleBar();

    // Para sinks multi-canal dinámicos (Oscilloscope, SceneOutput)
    // renderizamos sólo los puertos en uso + 1 extra "vacío" para
    // conectar la siguiente señal/geometría.  Si todos los puertos
    // están ocupados, no mostramos extra.  Resto de nodos: número
    // fijo de puertos.
    int portsToShow = def.inputPorts;
    if (n.type == NodeType::Oscilloscope ||
        n.type == NodeType::SceneOutput) {
        int used = 0;
        for (const auto& e : active().edges())
            if (e.toNodeId == n.id) ++used;
        portsToShow = std::min(def.inputPorts, used + 1);
    }
    // Helper: true si algún edge del grafo activo toca este attrId.
    // Sirve para distinguir visualmente pines conectados (color
    // brillante) de pines libres (gris tenue) — feedback que faltaba
    // y se identificó como gap UX durante el run de E1.
    auto isAttrConnected = [&](int attrId) -> bool {
        for (const auto& e : active().edges())
            if (e.fromAttrId == attrId || e.toAttrId == attrId)
                return true;
        return false;
    };
    // Color del pin: la GRAMÁTICA decide.  `pinColorFromType` deriva
    // el color de la forma del TypeExpr (scalar=azul, vec=violeta,
    // mat=naranja, geometry=cyan, ...).  Si el pin está desconectado,
    // atenuamos el alpha; si está conectado, full brillo.
    // Antes (etapa 1): tabla enum→color hardcoded.  Ahora: una sola
    // llamada que future-proof cualquier TypeExpr nuevo.
    constexpr unsigned int kPinDisconnectedAlpha = 200;
    constexpr unsigned int kPinConnectedAlpha    = 255;
    auto attenuate = [](unsigned int color, unsigned int alpha) -> unsigned int {
        return (color & 0x00FFFFFFu) | ((alpha & 0xFFu) << 24);
    };
    using CKp = scinodes::ui::INodeRenderer::ColorKey;

    auto pinColorIn = [&](int port) -> unsigned int {
        const bool conn = isAttrConnected(n.inputAttrId(port));
        const unsigned int base = pinColorFromType(inputPortTypeOf(def, port));
        return attenuate(base, conn ? kPinConnectedAlpha : kPinDisconnectedAlpha);
    };
    auto pinColorOut = [&](int port) -> unsigned int {
        const bool conn = isAttrConnected(n.outputAttrId(port));
        const unsigned int base = pinColorFromType(outputPortTypeOf(def, port));
        return attenuate(base, conn ? kPinConnectedAlpha : kPinDisconnectedAlpha);
    };


    for (int p = 0; p < portsToShow; ++p) {
        const int aid = n.inputAttrId(p);
        m_renderer->pushColor(CKp::Pin, pinColorIn(p));
        m_renderer->beginInputAttribute(aid,
                                        scinodes::ui::PortShape::CircleFilled);
        // Label del puerto.  Dos fuentes con semánticas distintas:
        //   - stringParams["portLabel<p>"]  → ANOTACIÓN per-instance
        //     (Oscilloscope: "in N  θ(t)" preserva el índice + la
        //     etiqueta editable del canal).
        //   - def.inputPortLabels[p]        → label CANÓNICO per-type
        //     desde el registry (TransformObject: "geometría" sin
        //     prefijo "in 1" — el nombre identifica al puerto).
        std::string instanceLabel;
        {
            char k[32]; std::snprintf(k, sizeof(k), "portLabel%d", p);
            auto it = n.stringParams.find(k);
            if (it != n.stringParams.end()) instanceLabel = it->second;
        }
        const std::string typeLabel =
            (p < static_cast<int>(def.inputPortLabels.size()))
                ? def.inputPortLabels[p] : std::string{};

        // Live-value: para puertos signal de TransformObject (paso 5c
        // del refactor 3D + UX), si hay bridge y hay un Sink tap
        // downstream del source, mostramos el valor actual al lado del
        // label.  Da feedback de "qué está transformando ahora".  El
        // walker comparte la lógica con SceneCollector::readLiveSampleAt.
        std::string liveSuffix;
        if (m_bridge && n.type == NodeType::TransformObject &&
            p >= 1 && p <= 3 && isAttrConnected(aid)) {
            const Edge* inEdge = nullptr;
            for (const auto& e : active().edges()) {
                if (e.toNodeId == n.id &&
                    attrInputPort(e.toAttrId) == p) { inEdge = &e; break; }
            }
            if (inEdge) {
                // Buscar un Sink "tap" en el mismo cable del source.
                const int srcId   = inEdge->fromNodeId;
                const int srcPort = attrOutputPort(inEdge->fromAttrId);
                for (const auto& e : active().edges()) {
                    if (e.fromNodeId != srcId)               continue;
                    if (attrOutputPort(e.fromAttrId) != srcPort) continue;
                    const NodeInstance* dst = active().findNode(e.toNodeId);
                    if (!dst) continue;
                    if (defOf(*dst).category != NodeCategory::Sink) continue;
                    const int channel = attrIsInput(e.toAttrId)
                                          ? attrInputPort(e.toAttrId) : 0;
                    auto buf = m_bridge->buffer(dst->id, channel);
                    if (buf.empty()) continue;
                    char tmp[32];
                    std::snprintf(tmp, sizeof(tmp), " = %.3g", buf.back());
                    liveSuffix = tmp;
                    break;
                }
            }
        }

        // Sufijo de unidad declarada — read-only, parte del label para
        // no añadir widgets que se salgan del nodo.  Sin sufijo si el
        // puerto es polimórfico (la unidad se decide por contexto, no
        // tiene sentido mostrarla acá; el override va por otro flujo).
        std::string unitSuffix;
        if (hasDeclaredInputUnit(def, p)) {
            std::string u = inputPortUnitOf(def, p).toCanonicalString();
            if (u.empty()) u = "1";   // adimensional canonicalizada
            unitSuffix = "  [" + u + "]";
        }

        if (def.inputPorts == 1 && instanceLabel.empty() && typeLabel.empty())
            ImGui::Text("in%s%s", liveSuffix.c_str(), unitSuffix.c_str());
        else if (!typeLabel.empty())
            // Canonical type-level label — reemplaza "in N" porque el
            // nombre del puerto ES su identidad ("geometría" no necesita
            // que le diga "in 1").
            ImGui::Text("%s%s%s", typeLabel.c_str(), liveSuffix.c_str(),
                                  unitSuffix.c_str());
        else if (!instanceLabel.empty())
            // Anotación per-instance — preserva el índice ordinal.
            ImGui::Text("in %d  %s%s%s", p + 1, instanceLabel.c_str(),
                                          liveSuffix.c_str(),
                                          unitSuffix.c_str());
        else
            ImGui::Text("in %d%s%s", p + 1, liveSuffix.c_str(),
                                      unitSuffix.c_str());
        m_renderer->endInputAttribute();
        m_renderer->popColor();
    }

    // Columna de valores alineada: el DragFloat de cada parámetro empieza
    // a la misma X dentro del nodo, sin importar el largo del label.
    // Medimos primero el label más ancho con la fuente actual (que ya
    // refleja el zoom via SetWindowFontScale).  Luego cada fila usa
    // SetCursorScreenPos para llevar el cursor a esa columna.
    // El label puede ser una traducción i18n (clave
    // `node.<type>.param.<paramId>`) o el `pd.name` literal como
    // fallback — el identificador del param sigue siendo `pd.name`,
    // que es key del mapa n.params y nombre Scilab emitido.
    auto paramDisplay = [&](const ParamDef& pd) -> std::string {
        const std::string key = std::string("node.")
                              + typeName(n.type) + ".param." + pd.name;
        return scinodes::trOr(key, pd.name);
    };

    float labelColMaxW = 0.f;
    for (const auto& pd : def.params) {
        const float w = ImGui::CalcTextSize(paramDisplay(pd).c_str()).x;
        if (w > labelColMaxW) labelColMaxW = w;
    }
    const float valueColGap = ImGui::GetStyle().ItemSpacing.x;

    for (int i = 0; i < (int)def.params.size(); ++i) {
        const auto& pd  = def.params[i];

        // Alias (etapa 6I.U): los params `target_node_id` y `target_port`
        // son IDENTIFICADORES del nodo referenciado, no señales
        // numéricas.  No tiene sentido un pin para enrutar otra señal
        // hacia ellos (no son magnitudes físicas).  El selector vive
        // en el param panel (dropdown).  Acá los escondemos del cuerpo.
        if (n.type == NodeType::Alias) continue;

        // Per-param pin (PR2 v1.1): cada row tiene un pin a la izquierda
        // que acepta un edge de una señal upstream.  Si está conectado,
        // el valor del widget queda decorativo — el solver consume la
        // expresión del source en su lugar (ver ScilabCodeGen).
        const int  paramAttr   = n.paramAttrId(i);
        const bool paramDriven = isAttrConnected(paramAttr);
        const unsigned int paramBase = pinColorFromType(exprScalar());
        m_renderer->pushColor(CKp::Pin,
            attenuate(paramBase,
                      paramDriven ? kPinConnectedAlpha : kPinDisconnectedAlpha));
        m_renderer->beginParamAttribute(paramAttr,
                                        scinodes::ui::PortShape::CircleFilled);

        // Label (traducido si hay clave i18n; fallback al pd.name).
        ImGui::TextDisabled("%s", paramDisplay(pd).c_str());
        const float labelStartX = ImGui::GetItemRectMin().x;
        ImGui::SameLine();
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos({ labelStartX + labelColMaxW + valueColGap,
                                    cp.y });

        // ---- QuantityField (etapa 6I.F) ------------------------------
        // Único InputText que muestra "<value> <unit>" combinado.
        // Reemplaza el DragFloat + TextDisabled(unit) anterior: el
        // parser de unidades opera sobre el texto entero, así que
        // tipear "100cm" o "3.3kV" funciona end-to-end.
        //
        // Display: canonicalizamos la Quantity al displayUnits del
        // grafo antes de formatearla — si el proyecto declara
        // length=m, un field con {100, cm} se muestra como "1 m".
        // Sin perder el storage interno (que sigue siendo {100, cm}
        // hasta que el usuario edite).
        //
        // Commit: parseQuantity al string editado.  Si !hasUnit
        // (usuario tipeó sólo un número), preservamos la unidad
        // anterior — comportamiento estilo Blender que evita borrar
        // accidentalmente la unidad al ajustar un valor.
        scinodes::Quantity curQ;
        auto fIt = n.fields.find(pd.name);
        if (fIt != n.fields.end()) curQ = fIt->second;
        else                       curQ.value = n.params.at(pd.name);
        const scinodes::Quantity displayQ =
            active().canonicalizeForDisplay(curQ);
        const std::string display = scinodes::toDisplayString(displayQ);

        char buf[64];
        std::strncpy(buf, display.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        char wid[32];
        std::snprintf(wid, sizeof(wid), "##qf%d_%d", n.id, i);
        const ImGuiID wgtId = ImGui::GetID(wid);
        if (paramDriven) ImGui::BeginDisabled();

        if (ImGui::IsItemActivated())
            m_pendingParamBefore = m_graph.snapshot();

        // Ancho específico del QuantityField — más amplio que el
        // default del renderer (kNodeWidgetWidth=86, pensado para
        // DragFloat de sólo número).  Escalamos por la ratio de
        // model-units para respetar el zoom (que ya está aplicado en
        // CalcItemWidth via el renderer).
        const float qfRatio = scinodes::ui::kNodeQuantityFieldWidth
                            / scinodes::ui::kNodeWidgetWidth;
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() * qfRatio);
        const bool committed = ImGui::InputTextWithHint(
            wid, "value unit",
            buf, sizeof(buf),
            ImGuiInputTextFlags_EnterReturnsTrue);

        if (committed) {
            // Etapa 6I.M: parseQuantity context-aware — "2k" en un
            // Ohm field se interpreta como "2 kΩ", "2k" en V-field como
            // "2 kV", etc.  El contexto es la unidad ACTUAL del field
            // (no la declarada del registry — para que el usuario pueda
            // empezar con un override y seguir usando prefijos).
            auto pr = scinodes::parseQuantity(buf, curQ.unit);
            // Aceptación en tres casos:
            //   1. parse OK + sólo número  → preserva unidad anterior
            //   2. parse OK + curQ adimensional → polimórfico, acepta
            //      cualquier dimensión (Kp puede pasar de "1" a "5V/V")
            //   3. parse OK + sameDimension(curQ.unit, new) → cambio
            //      coherente (m↔cm, V↔kV, Ohm↔kΩ)
            // En cualquier otro caso, el commit se descarta — el frame
            // siguiente reconstruye `buf` desde el estado persistido y
            // el edit visualmente desaparece, indicando rechazo.  Este
            // es R7 a nivel de field: la dimensión declarada por el
            // registry NO se corrompe escribiendo metros en una
            // resistencia.
            if (pr.ok()) {
                scinodes::Quantity q = pr.quantity;
                bool accept = false;
                if (!pr.hasUnit) {
                    // Etapa 6I.N: bare number → reset a la unidad
                    // CANÓNICA SI de la dimensión del field, NO preserva
                    // el prefijo previo.  El grafo trabaja en SI; "1"
                    // en un Ohm-field es "1 Ohm" aunque el último input
                    // hubiera sido "5 mΩ".  Mantiene la dimensión
                    // (exp signature) pero resetea magnitude=1.0.
                    q.unit = curQ.unit;
                    q.unit.magnitude = 1.0;
                    accept = true;
                } else if (curQ.unit.isDimensionless()) {
                    // Etapa 6I.P: el field es IDEAL (Kp, signos,
                    // coeficientes).  Sólo acepta inputs adimensionales.
                    // Tipear "5 mV" en Kp → rechaza (Kp no transporta
                    // V; lo que se construye con Kp es una expresión
                    // que adquiere unidad por el contexto, no Kp).
                    // Acepta prefijos (k, M, m, μ) que aplican sólo
                    // al escalar.
                    if (q.unit.isDimensionless()) accept = true;
                } else if (q.unit.sameDimension(curQ.unit)) {
                    accept = true;
                }
                if (accept) {
                    active().setFieldQuantity(n.id, pd.name, q);
                    if (m_paramCallback)
                        m_paramCallback(pathFor(n.id), i, q.toSI());
                }
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && m_pendingParamBefore) {
            recordSnapshot(*m_pendingParamBefore);
            m_pendingParamBefore = std::nullopt;
        }

        if (paramDriven) ImGui::EndDisabled();
        (void)wgtId;  // reservado para futura introspección de TempInputIsActive

        m_renderer->endParamAttribute();
        m_renderer->popColor();
    }

    for (int p = 0; p < def.outputPorts; ++p) {
        const int aid = n.outputAttrId(p);
        // Construimos el texto ANTES de beginOutputAttribute para
        // poder pasar el ancho real (etapa 6I.F.2 — antes el renderer
        // hardcoded 5 chars y "out [rad/s]" se salía del nodo).
        const std::string outLabel =
            (p < static_cast<int>(def.outputPortLabels.size()))
                ? def.outputPortLabels[p] : std::string{};
        std::string unitSuffix;
        if (hasDeclaredOutputUnit(def, p)) {
            std::string u = outputPortUnitOf(def, p).toCanonicalString();
            if (u.empty()) u = "1";
            unitSuffix = "  [" + u + "]";
        }
        std::string fullLabel;
        if (!outLabel.empty())                fullLabel = outLabel;
        else if (def.outputPorts == 1)        fullLabel = "out";
        else                                  fullLabel = "out " + std::to_string(p + 1);
        fullLabel += unitSuffix;

        m_renderer->pushColor(CKp::Pin, pinColorOut(p));
        m_renderer->beginOutputAttribute(aid,
                                         scinodes::ui::PortShape::CircleFilled,
                                         static_cast<int>(fullLabel.size()));
        ImGui::TextUnformatted(fullLabel.c_str());
        m_renderer->endOutputAttribute();
        m_renderer->popColor();
    }

    m_renderer->endNode();
    m_renderer->popColor(3);
}

// ---------------------------------------------------------------------------
// drawEdges — renders all edges with category-coded wire colours
// ---------------------------------------------------------------------------
void NodeCanvas::drawEdges() {
    using CK = scinodes::ui::INodeRenderer::ColorKey;
    for (const auto& e : active().edges()) {
        ImU32 wc = wireCol(e.fromNodeId, m_graph);
        m_renderer->pushColor(CK::Link,         wc);
        m_renderer->pushColor(CK::LinkHovered,  wc);
        m_renderer->pushColor(CK::LinkSelected, IM_COL32(255, 220, 50, 255));
        m_renderer->drawLink(e.id, e.fromAttrId, e.toAttrId);
        m_renderer->popColor(3);
    }
}

// ---------------------------------------------------------------------------
// handleLinkCreated — called after EndNodeEditor
// ---------------------------------------------------------------------------
void NodeCanvas::handleLinkCreated() {
    scinodes::ui::LinkCreatedEvent ev;
    if (!m_renderer->pollLinkCreated(ev)) return;
    int fromAttr = ev.fromAttrId, toAttr = ev.toAttrId;

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

    // Sim activa con selección presente: bloqueo + mensaje claro al
    // usuario.  Una desconexión cambia la identidad del sistema
    // simulado, así que exigimos detener la sim primero.  Mostramos
    // el mensaje solo si HABÍA algo seleccionado para no spamear con
    // toques accidentales de Delete sobre el vacío.
    if (m_simActive) {
        std::vector<int> selN, selL;
        m_renderer->getSelectedNodes(selN);
        m_renderer->getSelectedLinks(selL);
        if (!selN.empty() || !selL.empty()) {
            m_errorMsg   = "Detén la simulación para eliminar — esto "
                           "cambia el sistema que está siendo simulado.";
            m_errorTimer = 3.5f;
        }
        return;
    }

    bool changed = false;

    // Selected edges
    {
        std::vector<int> sel;
        m_renderer->getSelectedLinks(sel);
        if (!sel.empty()) {
            auto before = m_graph.snapshot();
            for (int id : sel) active().removeEdge(id);
            recordSnapshot(before);
            changed = true;
        }
    }

    // Selected nodes (also removes their edges)
    {
        std::vector<int> sel;
        m_renderer->getSelectedNodes(sel);
        if (!sel.empty()) {
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
    std::vector<int> sel;
    m_renderer->getSelectedNodes(sel);
    if (sel.empty()) return;
    std::unordered_set<int> selSet(sel.begin(), sel.end());

    m_clipNodes.clear();
    m_clipEdges.clear();

    float minX =  std::numeric_limits<float>::infinity();
    float minY =  std::numeric_limits<float>::infinity();

    NodeGraph& g = active();
    for (int id : sel) {
        const NodeInstance* node = g.findNode(id);
        if (!node) continue;
        auto rp = m_renderer->getNodePosition(id);
        ImVec2 pos{ rp.x, rp.y };
        ClipboardEntry ent;
        ent.node = *node;
        ent.pos  = pos;
        // SubGraph: clonar el contenido del child para que el paste
        // re-instale la topología interna intacta.  Sin esto el paste
        // crearía un SubGraph "vacío" sin grafo hijo y la simulación o
        // navegación posterior podrían derefenciar punteros nulos.
        if (isSubGraphContainer(node->type)) {
            if (const NodeGraph* child = g.subGraphOf(id)) {
                ent.childGraph = std::make_shared<NodeGraph>(*child);
                // Capturar posiciones del EditorContext del child.
                // imnodes guarda las posiciones por contexto/nodo-id;
                // al pegar creamos un nuevo SubGraph con un nuevo
                // context-key, así que tenemos que llevar las
                // posiciones explícitamente.  Los node-ids dentro del
                // child se preservan al instalar, así que la misma
                // tabla {id → pos} se reutiliza tal cual en el paste.
                const std::string childKey = canvasPathKey() +
                                             std::to_string(id) + "/";
                m_renderer->pushCanvas(childKey);
                for (const NodeInstance& cn : child->nodes()) {
                    auto p = m_renderer->getNodePosition(cn.id);
                    ent.internalPositions[cn.id] = ImVec2{ p.x, p.y };
                }
                m_renderer->popCanvas();
            }
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
        } else if (isSubGraphContainer(ent.node.type)) {
            // Crear el SubGraph correctamente (con stubs default que luego
            // serán reemplazados al instalar el child del clipboard).
            newId = active().addSubGraphNode();
            if (ent.childGraph) {
                NodeGraph childCopy = *ent.childGraph;
                active().installSubGraph(newId, std::move(childCopy));
                active().recomputeSubGraphPorts(newId);

                // Sembrar las posiciones internas en el EditorContext
                // del nuevo child.  Los node-ids del childGraph se
                // preservaron al instalar, así que las posiciones
                // capturadas en copy aplican directamente.
                if (!ent.internalPositions.empty()) {
                    const std::string childKey = canvasPathKey() +
                                                 std::to_string(newId) + "/";
                    m_renderer->pushCanvas(childKey);
                    for (const auto& [cid, cpos] : ent.internalPositions) {
                        m_renderer->setNodePosition(cid, { cpos.x, cpos.y });
                    }
                    m_renderer->popCanvas();
                }
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
        m_renderer->setNodePosition(newId,
            { ent.pos.x + offset.x, ent.pos.y + offset.y });
    }

    // Re-cablear las aristas internas remapeando los attrIds.
    for (const Edge& e : m_clipEdges) {
        auto fIt = idMap.find(e.fromNodeId);
        auto tIt = idMap.find(e.toNodeId);
        if (fIt == idMap.end() || tIt == idMap.end()) continue;
        active().tryAddEdge(attrRemap(e.fromAttrId, fIt->second),
                            attrRemap(e.toAttrId,   tIt->second));
    }

    // Selección actualizada: los nodos recién pegados quedan seleccionados,
    // así un paste seguido de un drag mueve todo el bloque junto.
    m_renderer->clearNodeSelection();
    for (int id : newIds) m_renderer->selectNode(id);
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
    std::vector<int> sel;
    m_renderer->getSelectedNodes(sel);
    if (sel.empty()) return;
    const int n = static_cast<int>(sel.size());

    // Centro de masa de la selección — la lógica pura no conoce posiciones,
    // así que lo calculamos aquí y se lo aplicamos al nodo resultante.
    ImVec2 acc{0, 0};
    for (int id : sel) {
        auto p = m_renderer->getNodePosition(id);
        acc = { acc.x + p.x, acc.y + p.y };
    }
    const ImVec2 center{ acc.x / n, acc.y / n };

    // Capturar posiciones antes de mutar — las usamos para sembrar el
    // contexto del child y preservar el layout interno.
    std::unordered_map<int, ImVec2> oldPos;
    for (int id : sel) {
        auto p = m_renderer->getNodePosition(id);
        oldPos[id] = ImVec2{ p.x, p.y };
    }

    recordSnapshot(m_graph.snapshot());
    bumpDirty();
    auto res = active().encapsulateByIds(sel);
    if (res.sgId == 0) {
        m_errorMsg   = "[encapsulate] selección inválida (¿stubs?).";
        m_errorTimer = 3.5f;
        return;
    }
    m_renderer->setNodePosition(res.sgId, { center.x, center.y });
    m_renderer->clearNodeSelection();
    m_renderer->selectNode(res.sgId);

    // Encapsular es un refactor estructural — la jerarquía visible
    // cambia pero el grafo aplanado (= las dinámicas que ve el solver)
    // queda equivalente.  Marcamos el flag para que AppWindow le pida
    // a SimController refrescar la baseline; sin eso, el siguiente
    // Resume vería los nodos viejos como "removidos" del top-level y
    // dispararía el modal destructivo de forma falsa.
    m_refactorJustHappened = true;

    // Sembrar las posiciones internas del SubGraph antes de que el
    // usuario entre.  Como el child vive en su propio EditorContext,
    // cambiamos a él temporalmente, fijamos las posiciones via
    // SetNodeScreenSpacePos (que opera en el contexto activo) y
    // restauramos el contexto del padre.  Las posiciones relativas
    // entre los nodos seleccionados se preservan; los stubs
    // SubGraphInput / SubGraphOutput, que vienen sin posición,
    // se colocan en dos columnas a la izquierda y derecha del
    // bbox de la selección movida (un stub por fila, ordenados
    // por su parámetro "Port").  Si el usuario quiere reorganizar
    // a mano lo puede hacer; el objetivo es solo que al entrar
    // por primera vez al SubGraph todo se vea legible, no que
    // todos los stubs queden amontonados en el origen.
    {
        const std::string childKey = canvasPathKey() +
                                     std::to_string(res.sgId) + "/";
        m_renderer->pushCanvas(childKey);

        // 1) Posiciones de los nodos movidos (preservan layout relativo).
        constexpr float kInf = std::numeric_limits<float>::infinity();
        float minX =  kInf, maxX = -kInf;
        float minY =  kInf, maxY = -kInf;
        for (const auto& [oldId, newId] : res.idMap) {
            auto it = oldPos.find(oldId);
            if (it == oldPos.end()) continue;
            m_renderer->setNodePosition(newId, { it->second.x, it->second.y });
            minX = std::min(minX, it->second.x);
            maxX = std::max(maxX, it->second.x);
            minY = std::min(minY, it->second.y);
            maxY = std::max(maxY, it->second.y);
        }
        // Si no había posiciones válidas (caso degenerado), centrar
        // el layout en (200,200) — sirve como punto de partida.
        if (minX > maxX) { minX = 200.f; maxX = 600.f; minY = 200.f; maxY = 400.f; }

        // 2) Stubs en dos columnas.  Sort por Port para layout estable.
        std::vector<std::pair<int,int>> inStubs;   // (port, nodeId)
        std::vector<std::pair<int,int>> outStubs;
        const NodeGraph* child = active().subGraphOf(res.sgId);
        if (child) {
            for (const NodeInstance& n : child->nodes()) {
                int port = 0;
                auto pit = n.params.find("Port");
                if (pit != n.params.end()) port = static_cast<int>(pit->second);
                if (n.type == NodeType::SubGraphInput)
                    inStubs.emplace_back(port, n.id);
                else if (n.type == NodeType::SubGraphOutput)
                    outStubs.emplace_back(port, n.id);
            }
        }
        std::sort(inStubs.begin(),  inStubs.end());
        std::sort(outStubs.begin(), outStubs.end());

        constexpr float kStubMargin  = 220.0f;   // separación del bbox
        constexpr float kStubSpacing = 80.0f;    // entre stubs verticalmente
        const float midY = 0.5f * (minY + maxY);
        // Centrar verticalmente la columna de stubs respecto al midY del bbox.
        auto columnTopY = [&](size_t count) {
            return midY - 0.5f * static_cast<float>(count - 1) * kStubSpacing;
        };

        if (!inStubs.empty()) {
            const float y0 = columnTopY(inStubs.size());
            for (size_t k = 0; k < inStubs.size(); ++k) {
                m_renderer->setNodePosition(
                    inStubs[k].second,
                    { minX - kStubMargin, y0 + k * kStubSpacing });
            }
        }
        if (!outStubs.empty()) {
            const float y0 = columnTopY(outStubs.size());
            for (size_t k = 0; k < outStubs.size(); ++k) {
                m_renderer->setNodePosition(
                    outStubs[k].second,
                    { maxX + kStubMargin, y0 + k * kStubSpacing });
            }
        }

        m_renderer->popCanvas();
    }
}


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

void NodeCanvas::enterSubGraph(int subGraphId) {
    if (!active().subGraphOf(subGraphId)) return;
    m_canvasStack.push_back(subGraphId);
    m_applyPositionsPending = false;   // child context has its own positions

    // Auto-layout heurístico: si al entrar al child detectamos que las
    // posiciones de sus nodos están "amontonadas" (bbox total más chica
    // que un umbral pequeño), aplicamos auto-layout.  Esto cubre dos
    // casos: (a) un SubGraph recién creado vacío y luego poblado con
    // nodos sin layout y (b) cargas de .scn legacy sin posiciones
    // internas.  Los casos con layout válido (encapsulate, paste, .scn
    // moderno) producen un bbox grande y se saltan el auto-layout.
    const std::string childKey = canvasPathKey();
    m_renderer->pushCanvas(childKey);
    const NodeGraph& child = active();
    if (!child.nodes().empty()) {
        constexpr float kInf = std::numeric_limits<float>::infinity();
        float minX = kInf, maxX = -kInf, minY = kInf, maxY = -kInf;
        for (const NodeInstance& n : child.nodes()) {
            auto p = m_renderer->getNodePosition(n.id);
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
        const float spreadX = maxX - minX;
        const float spreadY = maxY - minY;
        if (spreadX < 50.0f && spreadY < 50.0f) {
            // Nodos amontonados — aplicar auto-layout.  Opera sobre
            // active() = child (estamos dentro del contexto ya).
            applyAutoLayout();
        }
    }
    m_renderer->popCanvas();
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

// F2 sobre selección — abre el popup de edición sobre cualquier nodo
// seleccionado.  El popup tiene dos campos:
//   - "Name" (visible solo si el nodo es un SubGraph) edita
//      stringParams["Name"].
//   - "Comment" (siempre visible) edita n.comment — texto libre para
//      anotar el "por qué" detrás del nodo.
// Multi-selección se ignora (no tiene sentido un comentario compartido).
void NodeCanvas::handleRename() {
    if (m_renameNodeId != 0) return;   // popup ya abierto
    if (ImGui::IsAnyItemActive()) return;
    if (!ImGui::IsKeyPressed(ImGuiKey_F2, false)) return;

    std::vector<int> ids;
    m_renderer->getSelectedNodes(ids);
    if (ids.size() != 1) return;
    int id = ids[0];
    if (id <= 0) return;
    const NodeInstance* node = active().findNode(id);
    if (!node) return;

    m_renameNodeId       = id;
    m_renameFocusPending = true;

    // Cargar Name actual — etapa 6I.T: todos los nodos pueden tener
    // nombre custom (antes era exclusivo de SubGraphs).  Permite
    // distinguir "PID velocidad" de "PID posición", y habilita el
    // futuro nodo Alias que referencia por nombre.
    {
        auto it = node->stringParams.find("Name");
        const std::string cur = (it != node->stringParams.end()) ? it->second
                                                                  : std::string();
        std::strncpy(m_renameBuf, cur.c_str(), sizeof(m_renameBuf) - 1);
        m_renameBuf[sizeof(m_renameBuf) - 1] = '\0';
    }
    // Cargar comentario actual.
    std::strncpy(m_commentBuf, node->comment.c_str(), sizeof(m_commentBuf) - 1);
    m_commentBuf[sizeof(m_commentBuf) - 1] = '\0';
    ImGui::OpenPopup("##NodeMetaEdit");
}

void NodeCanvas::drawRenamePopup() {
    if (m_renameNodeId == 0) return;

    const NodeInstance* node = active().findNode(m_renameNodeId);
    const bool isSG = node && isSubGraphContainer(node->type);

    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##NodeMetaEdit")) {
        ImGui::TextDisabled(" Editar metadatos del nodo");
        ImGui::Separator();

        // Etapa 6I.T: campo Name editable para TODOS los nodos.  Vacío
        // ⇒ se muestra el label del registry (el comportamiento previo).
        // Con texto ⇒ pisa al label.
        ImGui::TextDisabled("Nombre  (vacío = label del tipo)");
        if (m_renameFocusPending) {
            ImGui::SetKeyboardFocusHere();
            m_renameFocusPending = false;
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##rn", m_renameBuf, sizeof(m_renameBuf));
        ImGui::Spacing();

        ImGui::TextDisabled("Comentario  (Ctrl+hover sobre el nodo para verlo)");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextMultiline("##cmt", m_commentBuf, sizeof(m_commentBuf),
                                  ImVec2(-1, 90));
        ImGui::Spacing();

        const bool apply  = ImGui::Button("Apply");
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel");
        if (apply) {
            recordSnapshot(m_graph.snapshot());
            // Vacío = borrar el override → fallback al label del registry.
            if (m_renameBuf[0] == '\0')
                active().setStringParam(m_renameNodeId, "Name", "");
            else
                active().setStringParam(m_renameNodeId, "Name", m_renameBuf);
            active().setComment(m_renameNodeId, m_commentBuf);
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
    (void)isSG;
}

// ---------------------------------------------------------------------------
// Ctrl+L — Auto-layout BFS-layered del grafo activo.
// ---------------------------------------------------------------------------
void NodeCanvas::handleAutoLayout() {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl) return;
    if (ImGui::IsAnyItemActive()) return;
    if (!ImGui::IsKeyPressed(ImGuiKey_L, false)) return;
    applyAutoLayout();
}

void NodeCanvas::applyAutoLayout() {
    NodeGraph& g = active();
    if (g.nodes().empty()) return;

    // Delegamos al `Canvas` propio (src/ui/canvas/Canvas.cpp): construimos
    // un Canvas efímero sobre el grafo activo, llamamos autoLayout (que
    // devuelve posiciones en MODEL space, no en screen-space) y se las
    // pasamos al renderer en Editor space para que se persistan
    // independientes del pan/zoom actual.
    using Space = scinodes::ui::INodeRenderer::CoordSpace;
    scinodes::ui::Canvas canvas(g);
    canvas.autoLayout();
    constexpr float kInf = 1e9f;
    float minX = kInf, minY = kInf, maxX = -kInf, maxY = -kInf;
    for (const NodeInstance& n : g.nodes()) {
        const auto pos = canvas.positionOf(n.id);
        m_renderer->setNodePosition(n.id, { pos.x, pos.y }, Space::Editor);
        const auto dims = scinodes::ui::computeNodeDimensions(n);
        if (pos.x < minX) minX = pos.x;
        if (pos.y < minY) minY = pos.y;
        if (pos.x + dims.w > maxX) maxX = pos.x + dims.w;
        if (pos.y + dims.h > maxY) maxY = pos.y + dims.h;
    }
    // Encuadrar la vista al bbox real (incluyendo dimensiones de cada
    // nodo, no aproximaciones).  Sin esto los nodos anchos (p. ej.
    // Oscilloscope con port-label custom largo) quedan parcialmente
    // fuera del viewport.
    if (minX <= maxX)
        m_renderer->frameToBox({minX, minY}, {maxX, maxY}, 0.f, 0.f);

    recordSnapshot(m_graph.snapshot());
    bumpDirty();
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

    // Frame-all: Home o F (convención de Blender) encuadra todos los
    // nodos en la vista actual.  El renderer calcula bbox + ajusta
    // pan/zoom para que entren con un margen de aire.
    const bool frameAllPressed =
        ImGui::IsKeyPressed(ImGuiKey_Home, false) ||
        (ImGui::IsKeyPressed(ImGuiKey_F, false) && !ImGui::IsAnyItemActive());
    if (hov && frameAllPressed && active().nodeCount() > 0) {
        using Space = scinodes::ui::INodeRenderer::CoordSpace;
        constexpr float kInf = 1e9f;
        float minX = kInf, minY = kInf, maxX = -kInf, maxY = -kInf;
        for (const NodeInstance& n : active().nodes()) {
            const auto pos = m_renderer->getNodePosition(n.id, Space::Editor);
            const auto dims = scinodes::ui::computeNodeDimensions(n);
            if (pos.x < minX) minX = pos.x;
            if (pos.y < minY) minY = pos.y;
            if (pos.x + dims.w > maxX) maxX = pos.x + dims.w;
            if (pos.y + dims.h > maxY) maxY = pos.y + dims.h;
        }
        if (minX <= maxX)
            m_renderer->frameToBox({minX, minY}, {maxX, maxY}, 0.f, 0.f);
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
    // (a) Detach: el usuario "agarró" un input conectado y arrastró —
    // el renderer ya transfirió el drag al output del otro extremo; aquí
    // solo borramos del modelo el edge previo.  Si el drag terminó
    // sobre otro pin, handleLinkCreated en el frame actual creará el
    // nuevo edge (efecto neto: reconexión).  Si terminó en vacío, no
    // hay LinkCreated y solo queda el detach (efecto neto: desconexión).
    int detachedId = 0;
    if (m_renderer->pollLinkDetached(detachedId)) {
        if (detachedId > 0) {
            if (m_simActive) {
                // Drag-detach durante sim = misma operación destructiva
                // que Delete sobre un cable.  Mostramos el mismo
                // mensaje que handleDeletion para coherencia.
                m_errorMsg   = "Detén la simulación para mover/quitar "
                               "esta conexión — cambia el sistema.";
                m_errorTimer = 3.5f;
            } else {
                auto before = m_graph.snapshot();
                active().removeEdge(detachedId);
                recordSnapshot(before);
                bumpDirty();
            }
        }
    }

    // (b) Drop convencional: drag desde un pin que NO partió de un
    // edge previo, soltado en vacío → abrir popup Add Node con
    // auto-connect.
    int startedAt = 0;
    if (!m_renderer->pollLinkDropped(startedAt)) return;
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
    if (!m_renderer->isLinkHovered(hoveredLink)) return;
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
        const std::string& header =
            (m_popupInsertEdgeId    != 0) ? scinodes::tr("popup.header.insert_node") :
            (m_popupAutoConnectAttr != 0) ? scinodes::tr("popup.header.connect_node") :
                                            scinodes::tr("popup.header.add_node");
        ImGui::TextDisabled("%s", header.c_str());
        ImGui::Separator();

        // Typeahead search box, auto-focused on first frame of the popup.
        if (m_popupFocusSearch) {
            ImGui::SetKeyboardFocusHere();
            m_popupFocusSearch = false;
        }
        ImGui::SetNextItemWidth(-1);
        const bool searchEntered =
            ImGui::InputTextWithHint("##search",
                                     scinodes::tr("popup.search_hint").c_str(),
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
                // First port on the other end of the new node:
                //   came from an output → connect to its input 0;
                //   came from an input  → connect to its output 0.
                const int otherEnd = attrIsOutput(m_popupAutoConnectAttr)
                    ? (newNodeId * kAttrIdNodeStride)
                    : (newNodeId * kAttrIdNodeStride + kAttrIdOutputBase);
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
                    auto e1 = active().tryAddEdge(
                        fromAttr, newNodeId * kAttrIdNodeStride);
                    auto e2 = active().tryAddEdge(
                        newNodeId * kAttrIdNodeStride + kAttrIdOutputBase, toAttr);
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
        // Pick existing node as ALIAS target (etapa 6I.V).  Para el caso
        // "drag desde input" — crea un Alias apuntando al targetNodeId/
        // port y lo cablea al input.
        auto pickExistingAlias = [&](int targetNodeId, int targetPort) {
            const int newId = addNode(NodeType::Alias);
            active().setParam(newId, "target_node_id",
                              static_cast<double>(targetNodeId));
            active().setParam(newId, "target_port",
                              static_cast<double>(targetPort));
            wireNewNode(newId);
            ImGui::CloseCurrentPopup();
        };
        // Pick existing INPUT pin (caso "drag desde output").  Edge directo
        // del attr origen al input del nodo existente — sin Alias porque
        // el destino YA está en el canvas.  Reseteamos el popup state
        // antes de tryAddEdge para que wireNewNode no lo use de nuevo.
        auto pickExistingDirectInput = [&](int destNodeId, int destPort) {
            const int dstAttr = destNodeId * kAttrIdNodeStride + destPort;
            auto err = active().tryAddEdge(m_popupAutoConnectAttr, dstAttr);
            if (err) {
                m_errorMsg   = "[" + err->rule + "]  " + err->message;
                m_errorTimer = 3.5f;
            } else {
                bumpDirty();
            }
            m_popupAutoConnectAttr = 0;
            m_popupInsertEdgeId    = 0;
            ImGui::CloseCurrentPopup();
        };
        // ¿Mostrar nodos existentes en el popup?  Sí, en ambas
        // direcciones del drag — la semántica difiere:
        //   - Drag desde INPUT  ⇒ necesita un SOURCE  ⇒ lista
        //     outputs de nodos existentes ⇒ crear un Alias en el pin
        //     destino (cerca del lugar donde soltaron el cable).
        //   - Drag desde OUTPUT ⇒ necesita un CONSUMER ⇒ lista inputs
        //     de nodos existentes ⇒ crear edge directo al destino
        //     elegido (sin Alias — el destino YA existe en el canvas).
        const bool offerExisting = (m_popupAutoConnectAttr != 0);
        const bool fromInput     = offerExisting &&
                                   attrIsInput(m_popupAutoConnectAttr);
        // Ayuda para construir el label de un nodo existente.
        auto displayNodeName = [&](const NodeInstance& other) -> std::string {
            auto it = other.stringParams.find("Name");
            if (it != other.stringParams.end() && !it->second.empty())
                return it->second;
            return std::string(labelOf(other.type));
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

        // Helpers locales — el label visible viene de i18n (con fallback
        // al label del registry); el search filter compara contra el
        // label traducido para que un usuario en español pueda buscar
        // "señal" y encontrar Step Signal traducido a "Señal escalón".
        auto displayLabel = [&](NodeType t, const std::string& fallback) {
            return scinodes::trOr(std::string("node.") + typeName(t) + ".label",
                                  fallback);
        };
        auto displayDesc  = [&](NodeType t, const std::string& fallback) {
            return scinodes::trOr(std::string("node.") + typeName(t) + ".description",
                                  fallback);
        };
        // Tooltip de descripción con wrap a ~320 px para que las
        // descripciones largas (SubGraph, PIDController, …) no se
        // muestren en una sola línea fuera del viewport.  Sólo aparece
        // mientras el usuario tenga Ctrl presionado — regla unificada
        // "Ctrl para más info" del editor (comentarios de nodos,
        // descripciones del menú "Añadir nodo", etc).  Sin Ctrl los
        // tooltips no interrumpen la lectura del menú.
        auto descTooltip = [&](const std::string& text) {
            if (text.empty()) return;
            if (!ImGui::GetIO().KeyCtrl) return;
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.f);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        };

        auto menuItem = [&](NodeType t) {
            const NodeDef& d = nodeRegistry().at(t);
            const std::string lbl = displayLabel(t, d.label);
            if (ImGui::MenuItem(lbl.c_str())) pickType(t);
            if (ImGui::IsItemHovered()) descTooltip(displayDesc(t, d.description));
        };

        // -------- Flat typeahead result list (replaces categorized view) ---
        if (searchActive) {
            int shown = 0;
            std::optional<NodeType>  firstBuiltin;
            std::optional<std::string> firstCustom;

            for (const auto& [t, d] : nodeRegistry()) {
                const std::string lbl = displayLabel(t, d.label);
                if (!matchesSearch(lbl)) continue;
                if (!firstBuiltin && !firstCustom) firstBuiltin = t;
                if (ImGui::MenuItem(lbl.c_str())) pickType(t);
                if (ImGui::IsItemHovered())
                    descTooltip(displayDesc(t, d.description));
                ++shown;
            }
            for (const auto& tid : scinodes::customNodes().typeIds()) {
                const auto* cd = scinodes::customNodes().find(tid);
                if (!cd) continue;
                if (!matchesSearch(cd->label)) continue;
                if (!firstBuiltin && !firstCustom) firstCustom = tid;
                if (ImGui::MenuItem(cd->label.c_str())) pickCustom(tid);
                if (ImGui::IsItemHovered()) descTooltip(cd->description);
                ++shown;
            }
            // Nodos existentes (etapa 6I.V): según la dirección del drag.
            if (offerExisting) {
                bool sepShown = false;
                for (const NodeInstance& other : active().nodes()) {
                    if (other.type == NodeType::Alias) continue;
                    const NodeDef& d = defOf(other);
                    const std::string nm = displayNodeName(other);
                    if (!matchesSearch(nm)) continue;
                    // From-INPUT: listar outputs.  From-OUTPUT: listar inputs.
                    const int portCount = fromInput ? d.outputPorts : d.inputPorts;
                    if (portCount <= 0) continue;
                    for (int p = 0; p < portCount; ++p) {
                        const auto& labels = fromInput ? d.outputPortLabels
                                                       : d.inputPortLabels;
                        std::string portLbl;
                        if (labels.size() > (size_t)p && !labels[p].empty())
                            portLbl = labels[p];
                        else
                            portLbl = (fromInput ? "out " : "in ")
                                    + std::to_string(p + 1);
                        const std::string lbl = "→ " + nm + "  [" + portLbl + "]";
                        if (!sepShown) {
                            ImGui::Separator();
                            ImGui::TextDisabled(" Nodos en el canvas");
                            sepShown = true;
                        }
                        ImGui::PushID(other.id * 1000 + p);
                        if (ImGui::MenuItem(lbl.c_str())) {
                            if (fromInput) pickExistingAlias(other.id, p);
                            else           pickExistingDirectInput(other.id, p);
                        }
                        ImGui::PopID();
                        ++shown;
                    }
                }
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

        // Helper para etiqueta de categoría con sangría inicial.
        auto catLabel = [&](const std::string& key) -> std::string {
            return "  " + scinodes::tr(key);
        };

        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32( 90, 200, 110, 255));
        bool srcOpen = ImGui::BeginMenu(catLabel("popup.category.sources").c_str());
        ImGui::PopStyleColor();
        if (srcOpen) {
            for (NodeType t : { NodeType::VoltageSource, NodeType::CurrentSource,
                                NodeType::StepSignal, NodeType::SineSignal,
                                NodeType::RampSignal,
                                NodeType::Alias })
                menuItem(t);
            ImGui::EndMenu();
        }

        // Etapa 6I.V: sección "Nodos en el canvas".  Aparece en ambas
        // direcciones del drag:
        //   from-INPUT  → outputs existentes → crea Alias.
        //   from-OUTPUT → inputs existentes  → edge directo (sin alias).
        if (offerExisting && active().nodeCount() > 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 160,  90, 255));
            bool exOpen = ImGui::BeginMenu(catLabel("popup.category.existing").c_str());
            ImGui::PopStyleColor();
            if (exOpen) {
                bool anyShown = false;
                for (const NodeInstance& other : active().nodes()) {
                    if (other.type == NodeType::Alias) continue;
                    const NodeDef& d = defOf(other);
                    const int portCount = fromInput ? d.outputPorts : d.inputPorts;
                    if (portCount <= 0) continue;
                    const std::string nm = displayNodeName(other);
                    for (int p = 0; p < portCount; ++p) {
                        const auto& labels = fromInput ? d.outputPortLabels
                                                       : d.inputPortLabels;
                        std::string portLbl;
                        if (labels.size() > (size_t)p && !labels[p].empty())
                            portLbl = labels[p];
                        else
                            portLbl = (fromInput ? "out " : "in ")
                                    + std::to_string(p + 1);
                        const std::string lbl = "→ " + nm + "  [" + portLbl + "]";
                        ImGui::PushID(other.id * 1000 + p);
                        if (ImGui::MenuItem(lbl.c_str())) {
                            if (fromInput) pickExistingAlias(other.id, p);
                            else           pickExistingDirectInput(other.id, p);
                        }
                        ImGui::PopID();
                        anyShown = true;
                    }
                }
                if (!anyShown) {
                    const char* msg = fromInput
                        ? "  (no hay outputs disponibles)"
                        : "  (no hay inputs disponibles)";
                    ImGui::TextDisabled("%s", msg);
                }
                ImGui::EndMenu();
            }
        }

        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32( 90, 130, 220, 255));
        bool txOpen = ImGui::BeginMenu(catLabel("popup.category.transformers").c_str());
        ImGui::PopStyleColor();
        if (txOpen) {
            for (NodeType t : { NodeType::Gain, NodeType::Summation,
                                NodeType::Integrator, NodeType::Differentiator,
                                NodeType::LowPassFilter, NodeType::PIDController,
                                NodeType::TransferFunction,
                                NodeType::TransferFunction2,
                                NodeType::Saturation,
                                NodeType::GearTransmission,
                                NodeType::InverseKinematics,
                                NodeType::DegToRad,
                                NodeType::RadToDeg })
                menuItem(t);
            ImGui::EndMenu();
        }

        // Devices — categoría gramatical Device.  Comportamiento de
        // transformador en R1-R5 pero llevan modelo 3-D asociado vía
        // contrato (sec. geometry-contracts).  Coloreado púrpura.
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160,  90, 200, 255));
        bool dvOpen = ImGui::BeginMenu(catLabel("popup.category.devices").c_str());
        ImGui::PopStyleColor();
        if (dvOpen) {
            for (NodeType t : { NodeType::DCMotorModel })
                menuItem(t);
            ImGui::EndMenu();
        }

        // Sizing & Electromagnetics (v0.8) — multiphysics-design nodes.
        // Coloured purple to distinguish from generic Source/Transformer/Sink.
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 120, 220, 255));
        bool szOpen = ImGui::BeginMenu(catLabel("popup.category.sizing").c_str());
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
        bool snkOpen = ImGui::BeginMenu(catLabel("popup.category.sinks").c_str());
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
        bool nvhOpen = ImGui::BeginMenu(catLabel("popup.category.structural").c_str());
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
        bool thOpen = ImGui::BeginMenu(catLabel("popup.category.thermal").c_str());
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

        // 3D Scene — sub-lenguaje Geometry del grafo (R6 los separa de
        // los nodos de Signal).  Object3D referencia el catálogo del
        // proyecto, TransformObject es el bridge bilingüe, SceneOutput
        // colecta lo que el panel View3D rendera.  Cyan-tinted para
        // leerse distinto de cualquier otra categoría.
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32( 80, 200, 200, 255));
        bool scOpen = ImGui::BeginMenu(catLabel("popup.category.scene_3d").c_str());
        ImGui::PopStyleColor();
        if (scOpen) {
            for (NodeType t : { NodeType::Object3D,
                                NodeType::TransformObject,
                                NodeType::SceneOutput,
                                NodeType::Vec3Constant,
                                NodeType::CombineXYZ,
                                NodeType::SeparateXYZ,
                                NodeType::VectorAdd,
                                NodeType::VectorSub,
                                NodeType::VectorScale,
                                NodeType::VectorDot,
                                NodeType::VectorCross,
                                NodeType::VectorLength,
                                NodeType::VectorNormalize })
                menuItem(t);
            ImGui::EndMenu();
        }

        // ---- Custom (JSON-loaded) types ---------------------------------
        auto customIds = scinodes::customNodes().typeIds();
        std::sort(customIds.begin(), customIds.end());

        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 160, 80, 255));
        bool cusOpen = ImGui::BeginMenu(catLabel("popup.category.custom").c_str());
        ImGui::PopStyleColor();
        if (cusOpen) {
            if (customIds.empty()) {
                ImGui::TextDisabled("  %s",
                    scinodes::tr("popup.custom.empty").c_str());
            } else {
                for (const auto& tid : customIds) {
                    const auto* cd =
                        scinodes::customNodes().find(tid);
                    if (!cd) continue;
                    if (ImGui::MenuItem(cd->label.c_str())) pickCustom(tid);
                    if (ImGui::IsItemHovered() && ImGui::GetIO().KeyCtrl &&
                        !cd->description.empty()) {
                        ImGui::BeginTooltip();
                        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.f);
                        ImGui::TextUnformatted(cd->description.c_str());
                        ImGui::PopTextWrapPos();
                        ImGui::EndTooltip();
                    }
                }
            }
            ImGui::Separator();
            bool busy = m_loadCustomDialog.isOpen();
            ImGui::BeginDisabled(busy);
            if (ImGui::MenuItem(scinodes::tr("popup.custom.load_from_json").c_str())) {
                m_loadCustomDialog.open(
                    FileDialog::Mode::Open,
                    scinodes::tr("dialog.load_custom_node"),
                    { "JSON descriptor (*.json)", "*.json" });
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }

        // Status del load custom (no es un "hint" sino feedback de la
        // última operación de carga; aparece solo cuando hay mensaje).
        if (!m_customLoadStatus.empty()) {
            ImGui::Separator();
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
        m_renderer->setNodePosition(newId, { m_popupPos.x, m_popupPos.y });
    }
    return newId;
}

int NodeCanvas::addCustomNode(const std::string& customType) {
    recordSnapshot(m_graph.snapshot());
    bumpDirty();
    const int newId = active().addCustomNode(customType);
    if (m_popupPos.x != 0.f || m_popupPos.y != 0.f) {
        m_renderer->setNodePosition(newId, { m_popupPos.x, m_popupPos.y });
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
    m_renderer->setNodePosition(newId, { canvasPos.x, canvasPos.y });
    return newId;
}

void NodeCanvas::setGraphTitle(const std::string& s) {
    m_graph.setTitle(s);
    bumpDirty();
}
void NodeCanvas::setGraphDescription(const std::string& s) {
    m_graph.setDescription(s);
    bumpDirty();
}
void NodeCanvas::setGraphTags(std::vector<std::string> v) {
    m_graph.setTags(std::move(v));
    bumpDirty();
}

void NodeCanvas::addImportedObject(ImportedObject o) {
    m_graph.addImportedObject(std::move(o));
    bumpDirty();
}
void NodeCanvas::removeImportedObject(const std::string& name) {
    m_graph.removeImportedObject(name);
    bumpDirty();
}

void NodeCanvas::clear() {
    m_history.clear();
    m_graph = NodeGraph{};
    m_positions.clear();
    m_readOnly = false;
    m_applyPositionsPending = false;
    // Bumpear dirty para que cualquier observador (SimController para
    // hot-reload, FileActions para "unsaved changes") sepa que la
    // estructura cambió.  El caller (FileActions::requestNew) llama
    // a markSaved() inmediatamente después si esto fue un "New" limpio.
    bumpDirty();
}

void NodeCanvas::resetView() {
    m_renderer->resetCanvasView();
}

// ---------------------------------------------------------------------------
// Persistence — sync positions, then hand the graph to ScnSerializer
// ---------------------------------------------------------------------------
void NodeCanvas::syncPositionsFromImnodes() {
    // Persistence only handles the top-level graph for now (Fase E
    // pending).  Cambiamos temporalmente al contexto top-level vía
    // pushCanvas/popCanvas; el renderer abstrae el manejo de contextos.
    using Space = scinodes::ui::INodeRenderer::CoordSpace;
    m_renderer->pushCanvas("/");
    m_positions.clear();
    for (const auto& n : m_graph.nodes()) {
        auto p = m_renderer->getNodePosition(n.id, Space::Editor);
        m_positions[n.id] = ScnVec2{ p.x, p.y };
    }
    m_renderer->popCanvas();
}

void NodeCanvas::applyPositionsToImnodes() {
    // m_positions contiene IDs del top-level.  Si el usuario está
    // navegando dentro de un SubGraph, aplicar posiciones con esos IDs
    // sobre el contexto del subgrafo corrompería estado.  Cambiamos al
    // contexto top-level (push/pop) para que la escritura siempre vaya
    // al editor correcto.
    using Space = scinodes::ui::INodeRenderer::CoordSpace;
    m_renderer->pushCanvas("/");
    for (const auto& [id, p] : m_positions)
        m_renderer->setNodePosition(id, { p.x, p.y }, Space::Editor);
    m_renderer->popCanvas();
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

        // Auto-load del catálogo by-name (esquema 0.5): hidrata el cache
        // de AssetService con cada entrada de `importedObjects()` parseando
        // su .gltf vía loadCatalog (contract-less).  Sin esto, los nodos
        // Object3D que referencian al catálogo dan placeholder al abrir
        // un .scn — el ImportedObject existe pero ningún DeviceAsset se
        // resuelve via `resolveByName()` hasta que el usuario reimporte
        // manualmente.  Cada entrada del catálogo es independiente: si
        // un .gltf falta o no parsea, se ignora silenciosamente (el
        // Outliner mostrará la entrada del catálogo con 0 parts útiles,
        // y el render dará el placeholder de nuevo).
        for (const auto& obj : m_graph.importedObjects()) {
            if (obj.name.empty() || obj.path.empty()) continue;
            const std::string resolved = m_assetService->resolveAssetPath(obj.path);
            std::string err;
            auto asset = scinodes::DeviceAssetLoader::loadCatalog(resolved, &err);
            if (asset.parts.empty()) continue;
            m_assetService->installNamedAsset(obj.name, std::move(asset));
        }
    }
    return report;
}

// ---------------------------------------------------------------------------
// Import — merge un .scn al grafo actual sin destruirlo.
//
// Es esencialmente "paste from file": cargamos en un grafo temporal,
// asignamos IDs frescos a todo lo importado (sin esto chocaríamos
// con IDs existentes), desplazamos las posiciones a la derecha del
// bounding box actual para que la importación quede visible y no
// encima del grafo existente, y re-mapeamos las aristas a los nuevos
// IDs.  Un solo snapshot al inicio hace que Ctrl+Z deshaga la
// importación entera en un golpe.
// ---------------------------------------------------------------------------
NodeCanvas::ImportResult NodeCanvas::importFromFile(const std::string& path) {
    ImportResult out;

    // 1. Cargar a un grafo temporal sin tocar el actual.
    NodeGraph     tempGraph;
    ScnPositions  tempPositions;
    LoadReport rep = ScnSerializer::loadFromFile(path, tempGraph, tempPositions);
    if (!rep.ok) {
        out.ok    = false;
        out.error = rep.fatalError.empty()
                        ? std::string("No pude leer el archivo importado.")
                        : rep.fatalError;
        return out;
    }
    if (tempGraph.nodeCount() == 0) {
        out.ok    = false;
        out.error = "El archivo no contiene nodos para importar.";
        return out;
    }

    // 2. Snapshot único para que el undo deshaga el import entero
    //    — usa recordSnapshot para que las posiciones actuales viajen
    //    con el snapshot (sin esto, Ctrl+Z restauraría el grafo viejo
    //    pero las posiciones quedarían como las del import).
    recordSnapshot(m_graph.snapshot());

    // 3. Bbox del grafo actual a top-level — los nodos importados se
    //    desplazan ABAJO del último nodo (mayor Y).  Importar a la
    //    derecha tiende a salirse de pantalla porque los grafos crecen
    //    horizontalmente; abajo deja al usuario el espacio vertical
    //    libre para reorganizar.  Si no hay nada todavía, offset queda
    //    en (0, 0) y el import cae en el origen.
    constexpr float kImportPaddingY = 120.0f;
    float maxY = -1e30f;
    for (const auto& [id, p] : m_positions)
        if (p.y > maxY) maxY = p.y;
    if (maxY < -1e29f) maxY = 0.0f;
    const float yOffset = m_positions.empty() ? 0.0f
                                              : (maxY + kImportPaddingY);

    // 4. Renumeración: oldId → newId.  Recorremos los nodos del temp
    //    y usamos addNode/addCustomNode que asignan IDs frescos en el
    //    grafo actual.  Para SubGraphs, transplantamos su grafo hijo
    //    al nuevo id vía installSubGraph (sin perder posiciones
    //    internas, que viven en n.position de cada NodeInstance).
    std::unordered_map<int, int> idMap;
    for (const NodeInstance& src : tempGraph.nodes()) {
        int newId = 0;
        if (src.type == NodeType::Custom) {
            newId = m_graph.addCustomNode(src.customType);
        } else if (isSubGraphContainer(src.type)) {
            newId = m_graph.addNode(NodeType::SubGraph);
            if (const NodeGraph* child = tempGraph.subGraphOf(src.id)) {
                NodeGraph copy = *child;
                m_graph.installSubGraph(newId, std::move(copy));
                m_graph.recomputeSubGraphPorts(newId);
            }
        } else {
            newId = m_graph.addNode(src.type);
        }
        idMap[src.id] = newId;

        // Propagar params, stringParams, asset, comment al nodo recién
        // creado en el grafo destino.
        for (const auto& [k, v] : src.params)        m_graph.setParam(newId, k, v);
        for (const auto& [k, v] : src.stringParams)  m_graph.setStringParam(newId, k, v);
        if (!src.assetPath.empty()) m_graph.setAssetPath(newId, src.assetPath);
        if (!src.comment.empty())   m_graph.setComment(newId, src.comment);

        // Posición desplazada hacia abajo — mantenemos también la copia
        // en m_positions para que applyPositionsToImnodes la dibuje en
        // el siguiente frame.
        const ScnVec2 displaced{ src.position.x,
                                 src.position.y + yOffset };
        m_positions[newId] = displaced;
    }

    // 5. Re-mapear aristas: cada attrId codifica un nodeId; lo
    //    reemplazamos por el nuevo.  Si tryAddEdge rechaza alguna,
    //    la registramos en el report — el caller decide si avisa.
    LoadReport reportOut;
    for (const Edge& e : tempGraph.edges()) {
        auto fIt = idMap.find(e.fromNodeId);
        auto tIt = idMap.find(e.toNodeId);
        if (fIt == idMap.end() || tIt == idMap.end()) continue;
        const int newFromAttr = attrRemap(e.fromAttrId, fIt->second);
        const int newToAttr   = attrRemap(e.toAttrId,   tIt->second);
        auto err = m_graph.tryAddEdge(newFromAttr, newToAttr);
        if (err) {
            reportOut.rejectedEdges.push_back({
                fIt->second, tIt->second, err->rule, err->message
            });
        }
    }

    m_applyPositionsPending = true;
    bumpDirty();

    // 6. Marcar los nodos recién importados como seleccionados — el
    //    usuario los puede mover en bloque inmediatamente sin tener
    //    que cazarlos uno por uno.  Sólo a top-level: si la
    //    importación trae SubGraphs, los nodos internos no entran a
    //    la selección (no son visibles al volver al canvas raíz).
    if (m_renderer) {
        m_renderer->pushCanvas("/");
        m_renderer->clearNodeSelection();
        for (const auto& [oldId, newId] : idMap)
            m_renderer->selectNode(newId);
        m_renderer->popCanvas();
    }

    // 7. Cargar assets glTF para los devices importados (mismo flujo
    //    que loadFromFile, pero sólo para los nodos nuevos).
    if (m_assetService) {
        for (const auto& [oldId, newId] : idMap) {
            const NodeInstance* n = m_graph.findNode(newId);
            if (!n || n->assetPath.empty()) continue;
            m_assetService->reload(newId, typeName(n->type), n->assetPath);
        }
    }

    out.ok          = true;
    out.report      = reportOut;
    out.report.ok   = true;
    out.report.nodesLoaded = static_cast<int>(idMap.size());
    out.report.edgesLoaded = tempGraph.edgeCount()
                             - static_cast<int>(reportOut.rejectedEdges.size());
    return out;
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
