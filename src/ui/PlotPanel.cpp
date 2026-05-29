#include "PlotPanel.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstdio>

static bool isSink(NodeType t) {
    return t == NodeType::Oscilloscope  ||
           t == NodeType::FFTAnalyzer   ||
           t == NodeType::PhasePortrait ||
           t == NodeType::DataLogger    ||
           t == NodeType::TerminalDisplay;
}

static bool hasIncomingEdge(int nodeId, const NodeGraph& g) {
    for (const auto& e : g.edges())
        if (e.toNodeId == nodeId) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Render one scrolling waveform.
// ---------------------------------------------------------------------------
static void renderWave(const char* label,
                       const std::vector<float>& buf, int wIdx,
                       float plotW, float plotH,
                       ImU32 lineColor) {
    if (buf.empty()) {
        ImGui::TextDisabled("  [no data yet]");
        return;
    }

    const int N = ScilabBridge::BUFFER_SIZE;
    static float display[ScilabBridge::BUFFER_SIZE];
    int start = wIdx % N;
    for (int i = 0; i < N; ++i)
        display[i] = buf[(start + i) % N];

    float vmin = *std::min_element(display, display + N);
    float vmax = *std::max_element(display, display + N);
    if (vmax - vmin < 1e-4f) { vmin -= 0.5f; vmax += 0.5f; }

    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%.4g", display[N - 1]);

    ImGui::PushStyleColor(ImGuiCol_PlotLines, lineColor);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,   IM_COL32(12, 14, 18, 255));
    ImGui::PlotLines(label, display, N, 0, overlay, vmin, vmax, { plotW, plotH });
    ImGui::PopStyleColor(2);
}

// ---------------------------------------------------------------------------
void PlotPanel::draw(const NodeGraph& graph, const ScilabBridge& bridge) {
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(18, 18, 22, 255));
    ImGui::Begin("Plots");

    // Only show plots for sink nodes that have at least one incoming edge
    std::vector<const NodeInstance*> sinks;
    for (const auto& n : graph.nodes())
        if (isSink(n.type) && hasIncomingEdge(n.id, graph))
            sinks.push_back(&n);

    if (sinks.empty()) {
        // No plotter nodes — show a hint instead of a spurious waveform
        float avail = ImGui::GetContentRegionAvail().y;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + avail * 0.4f);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 100, 100, 200));
        const char* msg = "Connect a signal to an Oscilloscope,\nFFT Analyzer or Data Logger to see plots.";
        float tw = ImGui::CalcTextSize(msg).x;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - tw) * 0.5f);
        ImGui::TextUnformatted(msg);
        ImGui::PopStyleColor();
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }

    // Divide available height among sink plots
    int   totalPlots = static_cast<int>(sinks.size());
    float avail      = ImGui::GetContentRegionAvail().y;
    float plotH      = std::max(55.f, (avail - totalPlots * 32.f) /
                                      static_cast<float>(totalPlots));
    float plotW      = ImGui::GetContentRegionAvail().x;

    // ---- Per-sink plots ----------------------------------------------------
    for (const NodeInstance* n : sinks) {
        char label[64];
        std::snprintf(label, sizeof(label), "  %s  #%d", labelOf(n->type), n->id);

        ImGui::PushID(n->id);
        ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32(43, 80, 140, 200));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(55,100, 170, 200));
        ImGui::Selectable(label, true,
                          ImGuiSelectableFlags_Disabled, { plotW, 18.f });
        ImGui::PopStyleColor(2);

        renderWave("##sink",
                   bridge.buffer(n->id), bridge.writeIndex(n->id),
                   plotW, plotH,
                   IM_COL32(100, 160, 230, 255));  // blue

        ImGui::Spacing();
        ImGui::PopID();
    }

    ImGui::End();
    ImGui::PopStyleColor();
}
