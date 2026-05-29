#include "HeatmapRenderer.hpp"

#include "../../core/ScilabBridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace scinodes::ui::plots {

namespace {

// Three-stop piecewise interpolation en RGB lineal — aproximación a viridis.
ImU32 viridisLike(float t) {
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

}  // namespace

void renderHeatmap(const std::vector<float>& bufX, int wX,
                   const std::vector<float>& bufY, int wY,
                   const std::vector<float>& bufC, int wC,
                   float plotW, float plotH) {
    if (bufX.empty() || bufY.empty() || bufC.empty()) {
        ImGui::TextDisabled("  [no data yet]");
        return;
    }

    // Buffer acumulativo: tomamos los últimos `kVisible` samples de cada
    // canal directamente del std::vector subyacente.
    const int kVisible = ScilabBridge::DEFAULT_VISIBLE_SAMPLES;
    const int total = std::min({(int)bufX.size(), (int)bufY.size(), (int)bufC.size()});
    const int N = std::min(total, kVisible);
    if (N < 1) { ImGui::TextDisabled("  [no data yet]"); return; }
    const float* xs = bufX.data() + (bufX.size() - N);
    const float* ys = bufY.data() + (bufY.size() - N);
    const float* cs = bufC.data() + (bufC.size() - N);
    (void)wX; (void)wY; (void)wC;

    float xmin = *std::min_element(xs, xs + N);
    float xmax = *std::max_element(xs, xs + N);
    float ymin = *std::min_element(ys, ys + N);
    float ymax = *std::max_element(ys, ys + N);
    float cmin = *std::min_element(cs, cs + N);
    float cmax = *std::max_element(cs, cs + N);
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

    const ImU32 axisCol = IM_COL32(70, 70, 90, 180);
    if (xmin <= 0 && 0 <= xmax) {
        ImVec2 a = toPx(0, ymin), b = toPx(0, ymax);
        dl->AddLine(a, b, axisCol, 0.8f);
    }
    if (ymin <= 0 && 0 <= ymax) {
        ImVec2 a = toPx(xmin, 0), b = toPx(xmax, 0);
        dl->AddLine(a, b, axisCol, 0.8f);
    }

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

}  // namespace scinodes::ui::plots
