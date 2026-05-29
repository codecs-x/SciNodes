#include "PlotPanel.hpp"
#include "../core/CsvExport.hpp"
#include "../core/Fft.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

static bool isSink(NodeType t) {
    return t == NodeType::Oscilloscope  ||
           t == NodeType::FFTAnalyzer   ||
           t == NodeType::PhasePortrait ||
           t == NodeType::DataLogger    ||
           t == NodeType::TerminalDisplay ||
           t == NodeType::HeatmapSink ||
           t == NodeType::DistributionSink;
}

static bool hasIncomingEdge(int nodeId, const NodeGraph& g) {
    for (const auto& e : g.edges())
        if (e.toNodeId == nodeId) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Render one scrolling waveform with explicit, labeled axes and Y-zoom.
//
// Interacciones sobre el área del plot:
//   - rueda del mouse (cursor encima)   → zoom Y (multiplicativo)
//   - click-drag vertical               → pan Y
//   - doble-click izquierdo             → auto-escala
//   - botón [A] a la derecha del plot   → auto-escala
//
// Reemplaza ImGui::PlotLines (que escala la curva para llenar el frame
// completo y hace que el ruido numérico parezca oscilación grande) con
// un dibujo manual: la curva vive dentro de un área interna delimitada,
// y los ticks numerados del eje Y le dicen al usuario la escala real.
// ---------------------------------------------------------------------------
static void renderWave(const char* label,
                       const std::vector<float>& buf, int wIdx,
                       float plotW, float plotH,
                       ImU32 lineColor,
                       PlotPanel::ZoomState& zs,
                       float secondsPerSample = 1.0f / 60.0f) {
    if (buf.empty()) {
        ImGui::TextDisabled("  [no data yet]");
        return;
    }

    const int N = ScilabBridge::BUFFER_SIZE;
    static float display[ScilabBridge::BUFFER_SIZE];
    int start = wIdx % N;
    for (int i = 0; i < N; ++i)
        display[i] = buf[(start + i) % N];

    // Auto vmin/vmax desde los datos (con piso de rango mínimo).
    float dmin = *std::min_element(display, display + N);
    float dmax = *std::max_element(display, display + N);
    {
        const float center   = 0.5f * (dmin + dmax);
        const float minRange = std::max(1.0f, 0.05f * std::fabs(center));
        if ((dmax - dmin) < minRange) {
            dmin = center - 0.5f * minRange;
            dmax = center + 0.5f * minRange;
        }
    }

    // Rango final para dibujar.  En auto se sigue el dato; en manual se
    // usa el (center, range) que el usuario ha ido manipulando.
    float vmin, vmax;
    if (zs.manual) {
        vmin = zs.center - 0.5f * zs.range;
        vmax = zs.center + 0.5f * zs.range;
    } else {
        vmin = dmin;
        vmax = dmax;
        zs.center = 0.5f * (vmin + vmax);
        zs.range  = vmax - vmin;
    }

    // Reservar el área del plot.  Dejamos 28 px a la derecha para el
    // botón [A] de auto-fit (fuera del InvisibleButton para no enredar
    // el manejo de hover/click).
    constexpr float BTN_W = 28.0f;
    ImGui::PushID(label);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float  invW   = plotW - BTN_W;
    ImGui::InvisibleButton("##plot", ImVec2(invW, plotH));
    const bool  hov = ImGui::IsItemHovered();
    const bool  act = ImGui::IsItemActive();
    const bool  dbl = hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    // Aplicar interacciones del frame ANTES de dibujar, para que el dibujo
    // refleje el zoom recién aplicado y no haya un frame de retraso.
    if (hov) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            if (!zs.manual) {
                zs.manual = true;
                zs.center = 0.5f * (vmin + vmax);
                zs.range  = vmax - vmin;
            }
            zs.range *= std::pow(1.15f, -wheel);
            zs.range  = std::max(zs.range, 1e-6f);
        }
    }
    if (act) {
        float dy = ImGui::GetIO().MouseDelta.y;
        if (dy != 0.0f) {
            if (!zs.manual) {
                zs.manual = true;
                zs.center = 0.5f * (vmin + vmax);
                zs.range  = vmax - vmin;
            }
            // Drag positivo (hacia abajo) → desplazar la vista hacia
            // valores mayores (yCenter sube).
            zs.center += dy * (zs.range / std::max(plotH - 26.0f, 1.0f));
        }
    }
    if (dbl) {
        zs.manual = false;
    }

    // Recalcular vmin/vmax con el estado tras las interacciones.
    if (zs.manual) {
        vmin = zs.center - 0.5f * zs.range;
        vmax = zs.center + 0.5f * zs.range;
    } else {
        vmin = dmin;
        vmax = dmax;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Fondo del área del plot.
    dl->AddRectFilled(origin,
                      ImVec2(origin.x + invW, origin.y + plotH),
                      IM_COL32(12, 14, 18, 255));

    // Márgenes para los ejes.
    constexpr float ML = 52.0f, MR = 8.0f, MT = 8.0f, MB = 18.0f;
    const ImVec2 plotA(origin.x + ML,          origin.y + MT);
    const ImVec2 plotB(origin.x + invW - MR,   origin.y + plotH - MB);
    const float  innerW = plotB.x - plotA.x;
    const float  innerH = plotB.y - plotA.y;
    if (innerW <= 1 || innerH <= 1) { ImGui::PopID(); return; }

    const ImU32 axisCol = IM_COL32(140, 145, 158, 200);
    const ImU32 tickCol = IM_COL32(170, 180, 195, 230);

    // Ejes.
    dl->AddLine(ImVec2(plotA.x, plotA.y), ImVec2(plotA.x, plotB.y), axisCol);
    dl->AddLine(ImVec2(plotA.x, plotB.y), ImVec2(plotB.x, plotB.y), axisCol);

    auto mapY = [&](float v) {
        return plotB.y - (v - vmin) / (vmax - vmin) * innerH;
    };
    auto mapX = [&](int i) {
        return plotA.x + static_cast<float>(i) / (N - 1) * innerW;
    };

    // Y-ticks: vmin, medio, vmax.
    auto yTick = [&](float v) {
        float y = mapY(v);
        dl->AddLine(ImVec2(plotA.x - 3, y), ImVec2(plotA.x, y), axisCol);
        char lab[24];
        std::snprintf(lab, sizeof(lab), "%.4g", v);
        ImVec2 sz = ImGui::CalcTextSize(lab);
        dl->AddText(ImVec2(plotA.x - 6 - sz.x, y - sz.y * 0.5f), tickCol, lab);
    };
    yTick(vmax);
    yTick(0.5f * (vmin + vmax));
    yTick(vmin);

    // X-ticks: extremos del rango temporal mostrado.
    auto xTick = [&](float xPx, const char* lab) {
        dl->AddLine(ImVec2(xPx, plotB.y), ImVec2(xPx, plotB.y + 3), axisCol);
        ImVec2 sz = ImGui::CalcTextSize(lab);
        dl->AddText(ImVec2(xPx - sz.x * 0.5f, plotB.y + 4), tickCol, lab);
    };
    char xLeft[24], xRight[6];
    std::snprintf(xLeft,  sizeof(xLeft),  "-%.2fs",
                  (N - 1) * secondsPerSample);
    std::snprintf(xRight, sizeof(xRight), "0s");
    xTick(plotA.x, xLeft);
    xTick(plotB.x, xRight);

    // Polyline.  Recortar contra el rect interno para que cuando el
    // usuario haga zoom muy fuerte, los segmentos que salen no pinten
    // por encima de los ejes ni del fondo.
    dl->PushClipRect(plotA, plotB, true);
    for (int i = 1; i < N; ++i) {
        ImVec2 p0(mapX(i - 1), mapY(display[i - 1]));
        ImVec2 p1(mapX(i),     mapY(display[i]));
        dl->AddLine(p0, p1, lineColor, 1.4f);
    }
    dl->PopClipRect();

    // Marcador del último valor + texto.
    {
        ImVec2 p(mapX(N - 1), mapY(display[N - 1]));
        if (p.y >= plotA.y && p.y <= plotB.y) {
            dl->AddCircleFilled(p, 3.0f, lineColor);
        }
        char lab[16];
        std::snprintf(lab, sizeof(lab), "%.4g", display[N - 1]);
        ImVec2 sz = ImGui::CalcTextSize(lab);
        float labX = (p.x + 6 + sz.x < plotB.x) ? p.x + 6 : p.x - 6 - sz.x;
        float labY = std::clamp(p.y - sz.y * 0.5f,
                                plotA.y, plotB.y - sz.y);
        dl->AddText(ImVec2(labX, labY), tickCol, lab);
    }

    ImGui::PopID();

    // Botón [A] de auto-fit al lado derecho del plot.  Deshabilitado
    // cuando ya está en auto: indicación visual del modo actual.
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::PushID(label);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    ImGui::BeginDisabled(!zs.manual);
    if (ImGui::SmallButton("A##autofit")) {
        zs.manual = false;
    }
    ImGui::EndDisabled();
    if (zs.manual && ImGui::IsItemHovered())
        ImGui::SetTooltip("Auto-escala (doble-click sobre el plot también)");
    ImGui::PopStyleVar();
    ImGui::PopID();
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
    // Garantizar rango mínimo visible — mismo fix que renderTimeSeries.
    auto ensureRange = [](float& lo, float& hi) {
        const float center   = 0.5f * (lo + hi);
        const float minRange = std::max(1.0f, 0.05f * std::fabs(center));
        if ((hi - lo) < minRange) {
            lo = center - 0.5f * minRange;
            hi = center + 0.5f * minRange;
        }
    };
    ensureRange(xmin, xmax);
    ensureRange(ymin, ymax);

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
    // Mismo fix de rango mínimo visible que en renderTimeSeries.
    auto ensureRange = [](float& lo, float& hi) {
        const float center   = 0.5f * (lo + hi);
        const float minRange = std::max(1.0f, 0.05f * std::fabs(center));
        if ((hi - lo) < minRange) {
            lo = center - 0.5f * minRange;
            hi = center + 0.5f * minRange;
        }
    };
    ensureRange(xmin, xmax);
    ensureRange(ymin, ymax);
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
// Histogram of the ring buffer — bins the (min, max) range into N bins
// and draws bars. Used by DistributionSink for live Monte-Carlo
// tolerance distributions. Bin count is taken from the sink's
// "Bin Count" param (clamped 4..64 to keep the bars visible).
// ---------------------------------------------------------------------------
static void renderHistogram(const std::vector<float>& buf, int wIdx,
                            int binCount,
                            float plotW, float plotH,
                            ImU32 barColor) {
    if (buf.empty()) {
        ImGui::TextDisabled("  [no data yet]");
        return;
    }

    const int N = ScilabBridge::BUFFER_SIZE;
    // Only the entries actually written so far participate in the
    // histogram (writeIdx samples or N, whichever is smaller). The
    // unwritten initial-zero slots would otherwise pull the mean
    // toward zero with the first few samples.
    int count = std::min(wIdx, N);
    if (count <= 1) {
        ImGui::TextDisabled("  [accumulating samples…]");
        return;
    }

    int firstRing = ((wIdx - count) % N + N) % N;
    float vmin =  std::numeric_limits<float>::infinity();
    float vmax = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < count; ++i) {
        float v = buf[(firstRing + i) % N];
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }
    if (!(vmax > vmin)) { vmin -= 0.5f; vmax += 0.5f; }

    int bins = std::clamp(binCount, 4, 64);
    std::vector<int> hist(bins, 0);
    for (int i = 0; i < count; ++i) {
        float v = buf[(firstRing + i) % N];
        int b = static_cast<int>((v - vmin) / (vmax - vmin) * bins);
        if (b == bins) b = bins - 1;
        if (b >= 0 && b < bins) ++hist[b];
    }
    int hmax = *std::max_element(hist.begin(), hist.end());
    if (hmax <= 0) hmax = 1;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(12, 14, 18, 255));
    ImGui::BeginChild("##hist", { plotW, plotH }, true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0     = ImGui::GetCursorScreenPos();
    ImVec2 size   = ImGui::GetContentRegionAvail();
    float barW    = size.x / bins;

    for (int b = 0; b < bins; ++b) {
        float h = (static_cast<float>(hist[b]) / hmax) * size.y;
        ImVec2 a = { p0.x + b * barW + 1.0f, p0.y + size.y - h };
        ImVec2 c = { p0.x + (b + 1) * barW - 1.0f, p0.y + size.y };
        dl->AddRectFilled(a, c, barColor);
    }

    // Mean / stdev overlay.
    double sum = 0, sq = 0;
    for (int i = 0; i < count; ++i) {
        float v = buf[(firstRing + i) % N];
        sum += v; sq += v * v;
    }
    double mean = sum / count;
    double var  = sq / count - mean * mean;
    double stdev = (var > 0) ? std::sqrt(var) : 0.0;
    char ov[120];
    std::snprintf(ov, sizeof(ov),
                  "n=%d  μ=%.4g  σ=%.4g  range=[%.4g, %.4g]",
                  count, mean, stdev,
                  static_cast<double>(vmin), static_cast<double>(vmax));
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
        } else if (n->type == NodeType::DistributionSink) {
            auto it = n->params.find("Bin Count");
            int bins = (it != n->params.end()) ? (int)it->second : 20;
            renderHistogram(bridge.buffer(n->id), bridge.writeIndex(n->id),
                            bins, plotW, plotH,
                            IM_COL32(220, 110, 170, 220));   // structural-pink
        } else {
            // ZoomState per-nodo: la primera llamada inserta una entrada
            // con manual=false y los siguientes frames la reutilizan.
            renderWave("##sink",
                       bridge.buffer(n->id), bridge.writeIndex(n->id),
                       plotW, plotH,
                       IM_COL32(100, 160, 230, 255),       // blue
                       m_zoomStates[n->id],
                       bridge.solverDt() > 0 ? bridge.solverDt() : 1.0f/60.0f);
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
