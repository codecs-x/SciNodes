#include "PlotPanel.hpp"
#include "../core/CsvExport.hpp"
#include "../core/Fft.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

static bool isSink(NodeType t) {
    return t == NodeType::Oscilloscope  ||
           t == NodeType::FFTAnalyzer   ||
           t == NodeType::PhasePortrait ||
           t == NodeType::DataLogger    ||
           t == NodeType::TerminalDisplay ||
           t == NodeType::HeatmapSink;
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

// Round to the largest power-of-two not exceeding `n`.
static int floorPow2(int n) {
    int p = 1;
    while ((p << 1) <= n) p <<= 1;
    return p;
}

// ---------------------------------------------------------------------------
// 2-D phase portrait. Channel 0 → x-axis, channel 1 → y-axis. The
// trajectory is rendered as a polyline through the ring buffer's most
// recent samples; the latest sample is marked as a filled dot.
// ---------------------------------------------------------------------------
static void renderPhase(const std::vector<float>& bufX, int wX,
                        const std::vector<float>& bufY, int wY,
                        float plotW, float plotH,
                        ImU32 lineColor) {
    if (bufX.empty() || bufY.empty()) {
        ImGui::TextDisabled("  [no data yet]");
        return;
    }

    const int N = ScilabBridge::BUFFER_SIZE;
    static float xs[ScilabBridge::BUFFER_SIZE];
    static float ys[ScilabBridge::BUFFER_SIZE];
    int sx = wX % N, sy = wY % N;
    for (int i = 0; i < N; ++i) {
        xs[i] = bufX[(sx + i) % N];
        ys[i] = bufY[(sy + i) % N];
    }

    float xmin = *std::min_element(xs, xs + N);
    float xmax = *std::max_element(xs, xs + N);
    float ymin = *std::min_element(ys, ys + N);
    float ymax = *std::max_element(ys, ys + N);
    if (xmax - xmin < 1e-4f) { xmin -= 0.5f; xmax += 0.5f; }
    if (ymax - ymin < 1e-4f) { ymin -= 0.5f; ymax += 0.5f; }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(12, 14, 18, 255));
    ImGui::BeginChild("##phase", { plotW, plotH }, true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0     = ImGui::GetCursorScreenPos();
    ImVec2 size   = ImGui::GetContentRegionAvail();

    auto toPx = [&](float x, float y) -> ImVec2 {
        float u = (x - xmin) / (xmax - xmin);
        float v = (y - ymin) / (ymax - ymin);
        return { p0.x + u * size.x, p0.y + (1.0f - v) * size.y };
    };

    // Axis cross at the origin if visible.
    const ImU32 axisCol = IM_COL32(70, 70, 90, 180);
    if (xmin <= 0 && 0 <= xmax) {
        ImVec2 a = toPx(0, ymin), b = toPx(0, ymax);
        dl->AddLine(a, b, axisCol, 0.8f);
    }
    if (ymin <= 0 && 0 <= ymax) {
        ImVec2 a = toPx(xmin, 0), b = toPx(xmax, 0);
        dl->AddLine(a, b, axisCol, 0.8f);
    }

    // Trajectory polyline.
    for (int i = 1; i < N; ++i)
        dl->AddLine(toPx(xs[i - 1], ys[i - 1]), toPx(xs[i], ys[i]),
                    lineColor, 1.2f);

    // Recent sample dot.
    dl->AddCircleFilled(toPx(xs[N - 1], ys[N - 1]), 3.0f,
                        IM_COL32(255, 220, 60, 255), 12);

    // Numeric overlay (top-left, current x/y values).
    char ov[64];
    std::snprintf(ov, sizeof(ov), "(%.3g, %.3g)", xs[N - 1], ys[N - 1]);
    dl->AddText({ p0.x + 6.f, p0.y + 4.f },
                IM_COL32(180, 180, 200, 220), ov);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// 2-D heatmap. Inputs are (x, y, c) tuples in the sink's three channels;
// each sample becomes a small filled circle whose colour follows a
// viridis-like gradient over the [min, max] of the c channel.
// ---------------------------------------------------------------------------
static ImU32 viridisLike(float t) {
    // Three-stop piecewise interpolation in linear RGB.
    t = std::clamp(t, 0.0f, 1.0f);
    const float r0 = 0.27f, g0 = 0.00f, b0 = 0.33f;   // dark purple
    const float r1 = 0.13f, g1 = 0.57f, b1 = 0.55f;   // teal
    const float r2 = 0.99f, g2 = 0.91f, b2 = 0.14f;   // yellow
    float r, g, b;
    if (t < 0.5f) {
        float k = t * 2.0f;
        r = r0 + (r1 - r0) * k;
        g = g0 + (g1 - g0) * k;
        b = b0 + (b1 - b0) * k;
    } else {
        float k = (t - 0.5f) * 2.0f;
        r = r1 + (r2 - r1) * k;
        g = g1 + (g2 - g1) * k;
        b = b1 + (b2 - b1) * k;
    }
    return IM_COL32(static_cast<int>(r * 255), static_cast<int>(g * 255),
                    static_cast<int>(b * 255), 230);
}

static void renderHeatmap(const std::vector<float>& bufX, int wX,
                          const std::vector<float>& bufY, int wY,
                          const std::vector<float>& bufC, int wC,
                          float plotW, float plotH) {
    if (bufX.empty() || bufY.empty() || bufC.empty()) {
        ImGui::TextDisabled("  [no data yet]");
        return;
    }

    const int N = ScilabBridge::BUFFER_SIZE;
    static float xs[ScilabBridge::BUFFER_SIZE];
    static float ys[ScilabBridge::BUFFER_SIZE];
    static float cs[ScilabBridge::BUFFER_SIZE];
    int sx = wX % N, sy = wY % N, sc = wC % N;
    for (int i = 0; i < N; ++i) {
        xs[i] = bufX[(sx + i) % N];
        ys[i] = bufY[(sy + i) % N];
        cs[i] = bufC[(sc + i) % N];
    }

    float xmin = *std::min_element(xs, xs + N);
    float xmax = *std::max_element(xs, xs + N);
    float ymin = *std::min_element(ys, ys + N);
    float ymax = *std::max_element(ys, ys + N);
    float cmin = *std::min_element(cs, cs + N);
    float cmax = *std::max_element(cs, cs + N);
    if (xmax - xmin < 1e-4f) { xmin -= 0.5f; xmax += 0.5f; }
    if (ymax - ymin < 1e-4f) { ymin -= 0.5f; ymax += 0.5f; }
    if (cmax - cmin < 1e-6f) { cmax = cmin + 1.0f; }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(12, 14, 18, 255));
    ImGui::BeginChild("##heatmap", { plotW, plotH }, true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0     = ImGui::GetCursorScreenPos();
    ImVec2 size   = ImGui::GetContentRegionAvail();

    auto toPx = [&](float x, float y) -> ImVec2 {
        float u = (x - xmin) / (xmax - xmin);
        float v = (y - ymin) / (ymax - ymin);
        return { p0.x + u * size.x, p0.y + (1.0f - v) * size.y };
    };

    // Axis cross at the origin if visible.
    const ImU32 axisCol = IM_COL32(70, 70, 90, 180);
    if (xmin <= 0 && 0 <= xmax) {
        ImVec2 a = toPx(0, ymin), b = toPx(0, ymax);
        dl->AddLine(a, b, axisCol, 0.8f);
    }
    if (ymin <= 0 && 0 <= ymax) {
        ImVec2 a = toPx(xmin, 0), b = toPx(xmax, 0);
        dl->AddLine(a, b, axisCol, 0.8f);
    }

    // Scatter — older samples slightly transparent so a fresh sweep is
    // visually obvious without losing the historical map.
    for (int i = 0; i < N; ++i) {
        float t = (cs[i] - cmin) / (cmax - cmin);
        ImU32 col = viridisLike(t);
        dl->AddCircleFilled(toPx(xs[i], ys[i]), 2.5f, col, 8);
    }

    char ov[80];
    std::snprintf(ov, sizeof(ov),
                  "x=%.3g  y=%.3g  c=%.3g   range c=[%.3g, %.3g]",
                  xs[N - 1], ys[N - 1], cs[N - 1], cmin, cmax);
    dl->AddText({ p0.x + 6.f, p0.y + 4.f },
                IM_COL32(180, 180, 200, 220), ov);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Render the magnitude spectrum of a sink's recent samples.
//
// `binCount` is the desired FFT window size (param "Bin Count" on the
// FFTAnalyzer); it gets snapped down to the largest power-of-two not
// exceeding the smaller of the request and the ring-buffer size.
// ---------------------------------------------------------------------------
static void renderSpectrum(const char* label,
                           const std::vector<float>& buf, int wIdx,
                           int binCount,
                           float plotW, float plotH,
                           ImU32 lineColor) {
    if (buf.empty()) {
        ImGui::TextDisabled("  [no data yet]");
        return;
    }

    const int N = ScilabBridge::BUFFER_SIZE;
    int win = floorPow2(std::min(binCount, N));
    if (win < 4) win = 4;

    // Most recent `win` samples in chronological order.
    std::vector<float> samples(win);
    int start = (wIdx % N) + (N - win);
    for (int i = 0; i < win; ++i)
        samples[i] = buf[(start + i) % N];

    auto mag = scinodes::magnitudeSpectrum(samples.data(), win);
    if (mag.empty()) {
        ImGui::TextDisabled("  [bin count must be power of 2]");
        return;
    }

    // Skip the DC bin in the visualisation to keep the y-range useful.
    const float* data = mag.data() + 1;
    int          n    = static_cast<int>(mag.size()) - 1;
    float vmax = *std::max_element(data, data + n);
    if (vmax < 1e-6f) vmax = 1.0f;

    char overlay[40];
    int kPeak = static_cast<int>(std::max_element(data, data + n) - data) + 1;
    std::snprintf(overlay, sizeof(overlay), "peak bin %d  /  %d", kPeak, win);

    ImGui::PushStyleColor(ImGuiCol_PlotLines, lineColor);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,   IM_COL32(12, 14, 18, 255));
    ImGui::PlotLines(label, data, n, 0, overlay, 0.0f, vmax, { plotW, plotH });
    ImGui::PopStyleColor(2);
}

// ---------------------------------------------------------------------------
void PlotPanel::draw(const NodeGraph& graph, const ScilabBridge& bridge) {
    // ---- Drain any pending CSV save dialog --------------------------------
    if (m_pendingExportSinkId != 0 && !m_exportDialog.isOpen()) {
        std::string path = m_exportDialog.take();
        if (!path.empty()) {
            // Default suffix
            if (path.size() < 4 ||
                path.substr(path.size() - 4) != ".csv")
                path += ".csv";
            std::string err;
            float dt = bridge.solverDt();
            if (dt <= 0.0f) dt = 1.0f / 60.0f;     // fallback when never stepped
            bool ok = scinodes::writeSinkCsv(
                path,
                bridge.buffer(m_pendingExportSinkId),
                bridge.writeIndex(m_pendingExportSinkId),
                bridge.time(), dt,
                m_pendingExportLabel,
                &err);
            m_exportStatus = ok ? ("Exported to " + path)
                                : ("Export failed: " + err);
        }
        m_pendingExportSinkId = 0;
        m_pendingExportLabel.clear();
    }

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

        if (n->type == NodeType::FFTAnalyzer) {
            auto it = n->params.find("Bin Count");
            int binCount = (it != n->params.end()) ? (int)it->second : 256;
            renderSpectrum("##fft",
                           bridge.buffer(n->id), bridge.writeIndex(n->id),
                           binCount, plotW, plotH,
                           IM_COL32(230, 160, 100, 255));   // orange
        } else if (n->type == NodeType::PhasePortrait) {
            renderPhase(bridge.buffer(n->id, 0), bridge.writeIndex(n->id, 0),
                        bridge.buffer(n->id, 1), bridge.writeIndex(n->id, 1),
                        plotW, plotH,
                        IM_COL32(180, 230, 130, 255));       // green
        } else if (n->type == NodeType::HeatmapSink) {
            renderHeatmap(bridge.buffer(n->id, 0), bridge.writeIndex(n->id, 0),
                          bridge.buffer(n->id, 1), bridge.writeIndex(n->id, 1),
                          bridge.buffer(n->id, 2), bridge.writeIndex(n->id, 2),
                          plotW, plotH);
        } else {
            renderWave("##sink",
                       bridge.buffer(n->id), bridge.writeIndex(n->id),
                       plotW, plotH,
                       IM_COL32(100, 160, 230, 255));       // blue
        }

        // ---- Export button for DataLogger sinks --------------------------
        if (n->type == NodeType::DataLogger) {
            bool busy = m_exportDialog.isOpen() || m_pendingExportSinkId != 0;
            ImGui::BeginDisabled(busy);
            if (ImGui::SmallButton("Export CSV…")) {
                m_pendingExportSinkId = n->id;
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "%s #%d",
                              labelOf(n->type), n->id);
                m_pendingExportLabel = lbl;
                char suggested[64];
                std::snprintf(suggested, sizeof(suggested),
                              "scinodes_logger_%d.csv", n->id);
                m_exportDialog.open(FileDialog::Mode::Save,
                                    "Export DataLogger to CSV",
                                    { "CSV file (*.csv)", "*.csv" },
                                    suggested);
            }
            ImGui::EndDisabled();
        }

        ImGui::Spacing();
        ImGui::PopID();
    }

    if (!m_exportStatus.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", m_exportStatus.c_str());
    }

    ImGui::End();
    ImGui::PopStyleColor();
}
