#include "SpectrumRenderer.hpp"

#include "../../core/Fft.hpp"
#include "../../core/I18n.hpp"

#include <algorithm>
#include <cstdio>

namespace scinodes::ui::plots {

namespace {

// Mayor potencia de 2 no mayor que `n`.
int floorPow2(int n) {
    int p = 1;
    while ((p << 1) <= n) p <<= 1;
    return p;
}

}  // namespace

void renderSpectrum(const char* label,
                    const std::vector<float>& buf, int wIdx,
                    int binCount,
                    float plotW, float plotH,
                    ImU32 lineColor) {
    if (buf.empty()) {
        ImGui::TextDisabled("%s", scinodes::tr("plots.no_data_yet").c_str());
        return;
    }

    // Buffer acumulativo: tomamos los últimos `win` samples directamente
    // del std::vector subyacente; sin wrap.
    const int total = static_cast<int>(buf.size());
    int win = floorPow2(std::min(binCount, total));
    if (win < 4) {
        ImGui::TextDisabled("%s", scinodes::tr("plots.accumulating").c_str());
        return;
    }
    (void)wIdx;

    std::vector<float> samples(win);
    const int srcStart = total - win;
    for (int i = 0; i < win; ++i)
        samples[i] = buf[srcStart + i];

    auto mag = scinodes::magnitudeSpectrum(samples.data(), win);
    if (mag.empty()) {
        ImGui::TextDisabled("%s", scinodes::tr("plots.bin_count_pow2").c_str());
        return;
    }

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

}  // namespace scinodes::ui::plots
