#include "NodeCanvas.hpp"
#include "../core/CustomNodeRegistry.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imnodes.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

// ---------------------------------------------------------------------------
// Category colour helpers
// ---------------------------------------------------------------------------
static ImU32 titleCol(NodeCategory c) {
    switch (c) {
        case NodeCategory::Source:      return IM_COL32( 42, 138,  62, 255);
        case NodeCategory::Transformer: return IM_COL32( 48,  90, 178, 255);
        case NodeCategory::Sink:        return IM_COL32(175,  50,  50, 255);
    }
    return IM_COL32(100,100,100,255);
}
static ImU32 titleHovCol(NodeCategory c) {
    switch (c) {
        case NodeCategory::Source:      return IM_COL32( 60, 180,  80, 255);
        case NodeCategory::Transformer: return IM_COL32( 68, 120, 220, 255);
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
void NodeCanvas::draw() {
    // Drain a pending custom-node load dialog (started from the add popup).
    if (!m_loadCustomDialog.isOpen()) {
        std::string p = m_loadCustomDialog.take();
        if (!p.empty()) {
            std::string err;
            bool ok = scinodes::CustomNodeRegistry::instance()
                          .loadFromFile(p, &err);
            m_customLoadStatus = ok
                ? ("Loaded custom node from " + p)
                : ("Load failed: " + err);
        }
    }

    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(22, 22, 26, 255));
    ImGui::Begin("Node Editor");

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

    ImGui::End();
    ImGui::PopStyleColor();

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
                                NodeType::DCMotorModel, NodeType::GearTransmission,
                                NodeType::InverseKinematics })
                menuItem(t);
            ImGui::EndMenu();
        }

        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220,  80,  80, 255));
        bool snkOpen = ImGui::BeginMenu("  Sinks");
        ImGui::PopStyleColor();
        if (snkOpen) {
            for (NodeType t : { NodeType::Oscilloscope, NodeType::FFTAnalyzer,
                                NodeType::PhasePortrait, NodeType::DataLogger,
                                NodeType::TerminalDisplay, NodeType::View3DSink })
                menuItem(t);
            ImGui::EndMenu();
        }

        // ---- Custom (JSON-loaded) types ---------------------------------
        auto customIds = scinodes::CustomNodeRegistry::instance().typeIds();
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
                        scinodes::CustomNodeRegistry::instance().find(tid);
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
    m_graph.addNode(type);
}

void NodeCanvas::addCustomNode(const std::string& customType) {
    m_history.record(m_graph.snapshot());
    m_graph.addCustomNode(customType);
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
