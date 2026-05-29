#include "WaveRenderer.hpp"

#include "../../core/ScilabBridge.hpp"   // BUFFER_SIZE

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace scinodes::ui::plots {

void renderWave(const char* label,
                const std::vector<float>& buf, int wIdx,
                float plotW, float plotH,
                ImU32 lineColor,
                PlotPanel::ZoomState& zs,
                float secondsPerSample,
                double currentSimTime) {
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

    constexpr float BTN_W = 28.0f;
    ImGui::PushID(label);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float  invW   = plotW - BTN_W;
    ImGui::InvisibleButton("##plot", ImVec2(invW, plotH));
    const bool  hov = ImGui::IsItemHovered();
    const bool  act = ImGui::IsItemActive();
    const bool  dbl = hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

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
            zs.center += dy * (zs.range / std::max(plotH - 26.0f, 1.0f));
        }
    }
    if (dbl) zs.manual = false;

    if (zs.manual) {
        vmin = zs.center - 0.5f * zs.range;
        vmax = zs.center + 0.5f * zs.range;
    } else {
        vmin = dmin;
        vmax = dmax;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(origin,
                      ImVec2(origin.x + invW, origin.y + plotH),
                      IM_COL32(12, 14, 18, 255));

    constexpr float ML = 52.0f, MR = 8.0f, MT = 8.0f, MB = 18.0f;
    const ImVec2 plotA(origin.x + ML,          origin.y + MT);
    const ImVec2 plotB(origin.x + invW - MR,   origin.y + plotH - MB);
    const float  innerW = plotB.x - plotA.x;
    const float  innerH = plotB.y - plotA.y;
    if (innerW <= 1 || innerH <= 1) { ImGui::PopID(); return; }

    const ImU32 axisCol = IM_COL32(140, 145, 158, 200);
    const ImU32 tickCol = IM_COL32(170, 180, 195, 230);

    dl->AddLine(ImVec2(plotA.x, plotA.y), ImVec2(plotA.x, plotB.y), axisCol);
    dl->AddLine(ImVec2(plotA.x, plotB.y), ImVec2(plotB.x, plotB.y), axisCol);

    auto mapY = [&](float v) {
        return plotB.y - (v - vmin) / (vmax - vmin) * innerH;
    };
    auto mapX = [&](int i) {
        return plotA.x + static_cast<float>(i) / (N - 1) * innerW;
    };

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

    auto xTick = [&](float xPx, const char* lab) {
        dl->AddLine(ImVec2(xPx, plotB.y), ImVec2(xPx, plotB.y + 3), axisCol);
        ImVec2 sz = ImGui::CalcTextSize(lab);
        dl->AddText(ImVec2(xPx - sz.x * 0.5f, plotB.y + 4), tickCol, lab);
    };
    const float windowSecs = (N - 1) * secondsPerSample;
    char xLeft[32], xRight[32];
    if (currentSimTime > 0.0) {
        const double tRight = currentSimTime;
        const double tLeft  = std::max(0.0, tRight - windowSecs);
        std::snprintf(xLeft,  sizeof(xLeft),  "%.2fs", tLeft);
        std::snprintf(xRight, sizeof(xRight), "%.2fs", tRight);
    } else {
        std::snprintf(xLeft,  sizeof(xLeft),  "-%.2fs", windowSecs);
        std::snprintf(xRight, sizeof(xRight), "0s");
    }
    xTick(plotA.x, xLeft);
    xTick(plotB.x, xRight);

    dl->PushClipRect(plotA, plotB, true);
    for (int i = 1; i < N; ++i) {
        ImVec2 p0(mapX(i - 1), mapY(display[i - 1]));
        ImVec2 p1(mapX(i),     mapY(display[i]));
        dl->AddLine(p0, p1, lineColor, 1.4f);
    }
    dl->PopClipRect();

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

    ImGui::SameLine(0.0f, 4.0f);
    ImGui::PushID(label);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    ImGui::BeginDisabled(!zs.manual);
    if (ImGui::SmallButton("A##autofit")) zs.manual = false;
    ImGui::EndDisabled();
    if (zs.manual && ImGui::IsItemHovered())
        ImGui::SetTooltip("Auto-escala (doble-click sobre el plot también)");
    ImGui::PopStyleVar();
    ImGui::PopID();
}

}  // namespace scinodes::ui::plots
