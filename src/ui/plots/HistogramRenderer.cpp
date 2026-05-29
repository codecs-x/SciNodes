#include "HistogramRenderer.hpp"

#include "../../core/ScilabBridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace scinodes::ui::plots {

void renderHistogram(const std::vector<float>& buf, int wIdx,
                     int binCount,
                     float plotW, float plotH,
                     ImU32 barColor) {
    if (buf.empty()) {
        ImGui::TextDisabled("  [no data yet]");
        return;
    }

    const int N = ScilabBridge::BUFFER_SIZE;
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

}  // namespace scinodes::ui::plots
