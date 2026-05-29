#include "PhaseRenderer.hpp"

#include "../../core/ScilabBridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace scinodes::ui::plots {

void renderPhase(const std::vector<float>& bufX, int wX,
                 const std::vector<float>& bufY, int wY,
                 float plotW, float plotH,
                 ImU32 lineColor,
                 PlotPanel::ZoomState& zs) {
    if (bufX.empty() || bufY.empty()) {
        ImGui::TextDisabled("  [no data yet]");
        return;
    }

    // Buffer acumulativo: usar los últimos `kVisible` samples de cada
    // canal, tomados directamente del std::vector (sin wrap).
    const int kVisible = ScilabBridge::DEFAULT_VISIBLE_SAMPLES;
    const int total = std::min<int>(bufX.size(), bufY.size());
    const int N = std::min(total, kVisible);
    if (N < 2) { ImGui::TextDisabled("  [no data yet]"); return; }
    const float* xs = bufX.data() + (bufX.size() - N);
    const float* ys = bufY.data() + (bufY.size() - N);
    (void)wX; (void)wY;

    float xmin = *std::min_element(xs, xs + N);
    float xmax = *std::max_element(xs, xs + N);
    float ymin = *std::min_element(ys, ys + N);
    float ymax = *std::max_element(ys, ys + N);
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

    if (zs.manual) {
        const float cx = 0.5f * (xmin + xmax);
        const float cy = 0.5f * (ymin + ymax);
        const float hx = 0.5f * (xmax - xmin) / zs.scale;
        const float hy = 0.5f * (ymax - ymin) / zs.scale;
        xmin = cx - hx;  xmax = cx + hx;
        ymin = cy - hy;  ymax = cy + hy;
    }

    constexpr float ML = 44.0f, MR = 8.0f, MT = 8.0f, MB = 22.0f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(12, 14, 18, 255));
    ImGui::BeginChild("##phase", { plotW, plotH }, true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0     = ImGui::GetCursorScreenPos();
    ImVec2 size   = ImGui::GetContentRegionAvail();

    ImGui::InvisibleButton("##phase_hit", size);
    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.f && io.KeyCtrl) {
            zs.manual = true;
            zs.scale  = std::clamp(zs.scale * (1.0f + io.MouseWheel * 0.10f),
                                   0.1f, 100.0f);
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            zs.manual = false;
            zs.scale  = 1.0f;
        }
    }

    const ImVec2 inA{ p0.x + ML,           p0.y + MT };
    const ImVec2 inB{ p0.x + size.x - MR,  p0.y + size.y - MB };
    const float  innerW = inB.x - inA.x;
    const float  innerH = inB.y - inA.y;
    if (innerW <= 1 || innerH <= 1) {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    auto toPx = [&](float x, float y) -> ImVec2 {
        float u = (x - xmin) / (xmax - xmin);
        float v = (y - ymin) / (ymax - ymin);
        return { inA.x + u * innerW, inA.y + (1.0f - v) * innerH };
    };
    size = { innerW, innerH };
    p0   = inA;

    const ImU32 axisCol = IM_COL32(70, 70, 90, 180);
    const ImU32 tickCol = IM_COL32(170, 180, 195, 230);
    if (xmin <= 0 && 0 <= xmax) {
        ImVec2 a = toPx(0, ymin), b = toPx(0, ymax);
        dl->AddLine(a, b, axisCol, 0.8f);
    }
    if (ymin <= 0 && 0 <= ymax) {
        ImVec2 a = toPx(xmin, 0), b = toPx(xmax, 0);
        dl->AddLine(a, b, axisCol, 0.8f);
    }

    dl->AddRect({ p0.x, p0.y }, { p0.x + size.x, p0.y + size.y },
                axisCol, 0.f, 0, 1.0f);

    auto xTick = [&](float vx) {
        ImVec2 px = toPx(vx, ymin);
        dl->AddLine({ px.x, p0.y + size.y },
                    { px.x, p0.y + size.y + 3 }, axisCol);
        char lab[24];
        std::snprintf(lab, sizeof(lab), "%.3g", vx);
        ImVec2 sz = ImGui::CalcTextSize(lab);
        dl->AddText({ px.x - sz.x * 0.5f, p0.y + size.y + 5 },
                    tickCol, lab);
    };
    xTick(xmin);
    xTick(0.5f * (xmin + xmax));
    xTick(xmax);
    auto yTick = [&](float vy) {
        ImVec2 px = toPx(xmin, vy);
        dl->AddLine({ p0.x - 3, px.y },
                    { p0.x,     px.y }, axisCol);
        char lab[24];
        std::snprintf(lab, sizeof(lab), "%.3g", vy);
        ImVec2 sz = ImGui::CalcTextSize(lab);
        dl->AddText({ p0.x - 6 - sz.x, px.y - sz.y * 0.5f },
                    tickCol, lab);
    };
    yTick(ymin);
    yTick(0.5f * (ymin + ymax));
    yTick(ymax);

    if (xmin <= 0 && 0 <= xmax) {
        ImVec2 a = toPx(0, ymin), b = toPx(0, ymax);
        dl->AddLine(a, b, axisCol, 0.8f);
    }
    if (ymin <= 0 && 0 <= ymax) {
        ImVec2 a = toPx(xmin, 0), b = toPx(xmax, 0);
        dl->AddLine(a, b, axisCol, 0.8f);
    }

    dl->PushClipRect({ p0.x, p0.y }, { p0.x + size.x, p0.y + size.y }, true);
    for (int i = 1; i < N; ++i)
        dl->AddLine(toPx(xs[i - 1], ys[i - 1]), toPx(xs[i], ys[i]),
                    lineColor, 1.2f);
    dl->AddCircleFilled(toPx(xs[N - 1], ys[N - 1]), 3.0f,
                        IM_COL32(255, 220, 60, 255), 12);
    dl->PopClipRect();

    char ov[64];
    std::snprintf(ov, sizeof(ov), "(%.3g, %.3g)", xs[N - 1], ys[N - 1]);
    ImVec2 ovSz = ImGui::CalcTextSize(ov);
    dl->AddText({ p0.x + size.x - ovSz.x - 6.f, p0.y + 4.f },
                IM_COL32(180, 180, 200, 220), ov);

    if (zs.manual) {
        char zlab[24];
        std::snprintf(zlab, sizeof(zlab), "zoom %.2fx", zs.scale);
        dl->AddText({ p0.x + 4.f,
                      p0.y + size.y - ImGui::GetTextLineHeight() - 4.f },
                    IM_COL32(255, 200, 110, 220), zlab);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

}  // namespace scinodes::ui::plots
