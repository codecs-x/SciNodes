#include "NodeCanvas.hpp"
#include "../app/AssetService.hpp"
#include "../core/ContractRegistry.hpp"
#include "../core/CsvParamIO.hpp"
#include "../core/CustomNodeRegistry.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imnodes.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
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

            const NodeInstance* n = m_graph.findNode(m_paramCsvNodeId);
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
                    m_history.record(m_graph.snapshot());
                    for (const auto& [name, value] : tmp.params)
                        m_graph.setParam(n->id, name, value);
                    if (m_paramCallback) {
                        // Re-emit each param so the live bridge updates too.
                        const auto& def = defOf(*n);
                        for (int i = 0; i < (int)def.params.size(); ++i) {
                            auto it = tmp.params.find(def.params[i].name);
                            if (it != tmp.params.end())
                                m_paramCallback(n->id, i, it->second);
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
            m_graph.setAssetPath(nodeId, path);
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

    // Shift+A → add-node popup
    if (!m_readOnly &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        ImGui::GetIO().KeyShift &&
        ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        m_popupPos = ImGui::GetMousePos();
        ImGui::OpenPopup("##AddNode");
    }

    drawAddPopup();
    showErrorTooltip();

    ImGui::SetWindowFontScale(m_zoom);
    ImNodes::BeginNodeEditor();

    // Apply any positions queued by a recent load — must happen inside the
    // editor scope so imnodes treats them as authoritative for this frame.
    if (m_applyPositionsPending) {
        applyPositionsToImnodes();
        m_applyPositionsPending = false;
    }

    if (m_readOnly) ImGui::BeginDisabled();
    for (const auto& n : m_graph.nodes())
        drawNode(n);

    drawEdges();

    ImNodes::EndNodeEditor();
    if (m_readOnly) ImGui::EndDisabled();
    ImGui::SetWindowFontScale(1.0f);

    if (!m_readOnly) {
        handleLinkCreated();
        handleDeletion();
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
    const NodeInstance* n = m_graph.findNode(m_openParamPanelNodeId);
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
                bool busy = m_assetDialog.isOpen() && m_assetDialogNodeId != 0;
                ImGui::BeginDisabled(busy || m_readOnly);
                if (ImGui::SmallButton("Cargar modelo 3D…")) {
                    m_assetDialogNodeId = n->id;
                    m_assetDialog.open(FileDialog::Mode::Open,
                                       "Cargar asset glTF",
                                       { "glTF (*.gltf, *.glb)",
                                         "*.gltf;*.glb" });
                }
                ImGui::EndDisabled();

                if (!n->assetPath.empty()) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Editar mapping…")) {
                        openMappingPanelFor(n->id);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Quitar")) {
                        m_graph.setAssetPath(n->id, "");
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

            const bool changed = ImGui::DragFloat("##v", &val, 0.01f,
                                                  0.f, 0.f, "%.4g");

            if (ImGui::IsItemActivated())
                m_pendingParamBefore = m_graph.snapshot();

            if (changed) {
                m_graph.setParam(n->id, pd.name, (double)val);
                if (m_paramCallback) m_paramCallback(n->id, i, (double)val);
            }

            if (ImGui::IsItemDeactivatedAfterEdit() && m_pendingParamBefore) {
                m_history.record(*m_pendingParamBefore);
                m_pendingParamBefore = std::nullopt;
            }

            if (!pd.unit.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", pd.unit.c_str());
            }
            ImGui::PopID();
        }

        if (m_readOnly) ImGui::EndDisabled();

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
    ImGui::TextUnformatted(def.label.c_str());
    ImNodes::EndNodeTitleBar();

    for (int p = 0; p < def.inputPorts; ++p) {
        ImNodes::BeginInputAttribute(n.inputAttrId(p));
        if (def.inputPorts == 1)
            ImGui::TextUnformatted("in");
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
        const bool changed = ImGui::DragFloat(wid, &val, 0.01f,
                                              0.f, 0.f, "%.4g");

        // Capture snapshot on first frame of activation (before value changes)
        if (ImGui::IsItemActivated())
            m_pendingParamBefore = m_graph.snapshot();

        if (changed) {
            m_graph.setParam(n.id, pd.name, (double)val);
            if (m_paramCallback) m_paramCallback(n.id, i, (double)val);
        }

        // Commit undo entry when the user finishes editing
        if (ImGui::IsItemDeactivatedAfterEdit() && m_pendingParamBefore) {
            m_history.record(*m_pendingParamBefore);
            m_pendingParamBefore = std::nullopt;
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
    for (const auto& e : m_graph.edges()) {
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
    auto err    = m_graph.tryAddEdge(fromAttr, toAttr);

    if (err) {
        // Show error tooltip and do NOT record undo
        m_errorMsg   = "[" + err->rule + "]  " + err->message;
        m_errorTimer = 3.5f;
    } else {
        m_history.record(before);   // record "before" for undo
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
            for (int id : sel) m_graph.removeEdge(id);
            m_history.record(before);
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
            for (int id : sel) m_graph.removeNode(id);
            m_history.record(before);
            changed = true;
        }
    }

    (void)changed;
}

// ---------------------------------------------------------------------------
// handleUndoRedo
// ---------------------------------------------------------------------------
void NodeCanvas::handleUndoRedo() {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl) return;

    bool undo = ImGui::IsKeyPressed(ImGuiKey_Z);
    bool redo  = ImGui::IsKeyPressed(ImGuiKey_Y) ||
                 (ImGui::IsKeyPressed(ImGuiKey_Z) && io.KeyShift);

    if (undo && !io.KeyShift && m_history.canUndo()) {
        auto snap = m_history.undo(m_graph.snapshot());
        if (snap) m_graph.restoreSnapshot(*snap);
    }
    if (redo && m_history.canRedo()) {
        auto snap = m_history.redo(m_graph.snapshot());
        if (snap) m_graph.restoreSnapshot(*snap);
    }
}

// ---------------------------------------------------------------------------
// handleZoom
// ---------------------------------------------------------------------------
void NodeCanvas::handleZoom() {
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) return;
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && io.MouseWheel != 0.f)
        m_zoom = std::clamp(m_zoom + io.MouseWheel * 0.08f, 0.25f, 3.0f);
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
void NodeCanvas::drawAddPopup() {
    ImGui::SetNextWindowPos(m_popupPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize({220, 0}, ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_PopupBg,      IM_COL32( 30,  32,  36, 245));
    ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32( 43,  80, 140, 200));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32( 60, 110, 190, 220));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  IM_COL32( 75, 130, 210, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 8});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   {6, 4});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.f);

    if (ImGui::BeginPopup("##AddNode")) {
        ImGui::TextDisabled(" Add Node");
        ImGui::Separator();
        ImGui::Spacing();

        auto menuItem = [&](NodeType t) {
            const NodeDef& d = nodeRegistry().at(t);
            if (ImGui::MenuItem(d.label.c_str())) {
                addNode(t);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", d.description.c_str());
        };

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
                    if (ImGui::MenuItem(cd->label.c_str())) {
                        addCustomNode(tid);
                        ImGui::CloseCurrentPopup();
                    }
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
        ImGui::TextUnformatted("  Shift+A  |  Esc to close");
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
void NodeCanvas::addNode(NodeType type) {
    m_history.record(m_graph.snapshot());
    const int newId = m_graph.addNode(type);
    // Spawn en la posición del cursor cuando se abrió el popup.
    // m_popupPos está en screen-space; imnodes opera en grid-space.
    // Aplicamos la transformación inversa para que el nodo aparezca
    // exactamente donde el usuario invocó Shift+A.  Si m_popupPos es
    // (0,0) (no hubo popup, p.ej. add programático), dejamos la
    // posición por defecto de imnodes.
    if (m_popupPos.x != 0.f || m_popupPos.y != 0.f) {
        ImNodes::SetNodeScreenSpacePos(newId, m_popupPos);
    }
}

void NodeCanvas::addCustomNode(const std::string& customType) {
    m_history.record(m_graph.snapshot());
    const int newId = m_graph.addCustomNode(customType);
    if (m_popupPos.x != 0.f || m_popupPos.y != 0.f) {
        ImNodes::SetNodeScreenSpacePos(newId, m_popupPos);
    }
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
    m_positions.clear();
    for (const auto& n : m_graph.nodes()) {
        ImVec2 p = ImNodes::GetNodeEditorSpacePos(n.id);
        m_positions[n.id] = ScnVec2{ p.x, p.y };
    }
}

void NodeCanvas::applyPositionsToImnodes() {
    for (const auto& [id, p] : m_positions)
        ImNodes::SetNodeEditorSpacePos(id, ImVec2{ p.x, p.y });
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
    return report;
}

void NodeCanvas::reloadAssetFor(int nodeId) {
    if (!m_assetService) return;
    const NodeInstance* n = m_graph.findNode(nodeId);
    if (!n) { m_assetService->detach(nodeId); return; }
    m_assetService->reload(nodeId, typeName(n->type), n->assetPath);
}

void NodeCanvas::detachAsset(int nodeId) {
    m_graph.setAssetPath(nodeId, "");
    if (m_assetService) m_assetService->detach(nodeId);
}

const std::unordered_map<int, scinodes::DeviceAsset>&
NodeCanvas::loadedAssets() const {
    static const std::unordered_map<int, scinodes::DeviceAsset> kEmpty;
    return m_assetService ? m_assetService->all() : kEmpty;
}

void NodeCanvas::openMappingPanelFor(int nodeId) {
    const NodeInstance* n = m_graph.findNode(nodeId);
    if (!n || n->assetPath.empty()) return;

    const auto* contract =
        (m_contractRegistry ? m_contractRegistry->find(typeName(n->type)) : nullptr);
    if (!contract) return;

    if (m_mappingPanel.openFor(n->assetPath, *contract)) {
        m_mappingNodeId = nodeId;
    } else {
        m_errorMsg   = "No se pudo abrir el panel de mapping (¿glTF inválido?)";
        m_errorTimer = 5.0f;
    }
}
