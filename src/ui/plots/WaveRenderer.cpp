#include "WaveRenderer.hpp"

#include "../../core/ScilabBridge.hpp"   // DEFAULT_VISIBLE_SAMPLES

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace scinodes::ui::plots {

// ---------------------------------------------------------------------------
// renderMultiWave — implementación principal.
// Recibe un vector de canales (buffer + color + label).  El eje X (tiempo)
// y el eje Y (auto-fit / manual / pan) son compartidos entre canales.
// Cada canal dibuja su propia línea + label sobre el último valor visible.
// ---------------------------------------------------------------------------
void renderMultiWave(const char* label,
                     const std::vector<const std::vector<float>*>& bufs,
                     const std::vector<ImU32>&                     colors,
                     const std::vector<std::string>&               channelLabels,
                     float plotW, float plotH,
                     PlotPanel::ZoomState& zs,
                     float secondsPerSample,
                     float timeWindowSecs) {
    if (bufs.empty()) {
        ImGui::TextDisabled("  [no channels]");
        return;
    }

    // --- Geometría y InvisibleButton --------------------------------------
    if (timeWindowSecs <= 0.0f)
        timeWindowSecs = ScilabBridge::DEFAULT_VISIBLE_SAMPLES * secondsPerSample;
    const int kVisible = std::max(2,
        static_cast<int>(std::round(timeWindowSecs / secondsPerSample)));

    constexpr float BTN_W = 28.0f;
    ImGui::PushID(label);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float  invW   = plotW - BTN_W;
    ImGui::InvisibleButton("##plot", ImVec2(invW, plotH));
    const bool  hov = ImGui::IsItemHovered();
    const bool  act = ImGui::IsItemActive();
    const bool  dbl = hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    constexpr float ML = 52.0f, MR = 8.0f, MT = 8.0f, MB = 18.0f;
    const ImVec2 plotA(origin.x + ML,          origin.y + MT);
    const ImVec2 plotB(origin.x + invW - MR,   origin.y + plotH - MB);
    const float  innerW = plotB.x - plotA.x;
    const float  innerH = plotB.y - plotA.y;
    if (innerW <= 1 || innerH <= 1) { ImGui::PopID(); return; }
    const float  pxPerSec = innerW / timeWindowSecs;

    // --- Total + pan input ------------------------------------------------
    // El total efectivo se toma del canal con más samples (los canales
    // empiezan vacíos hasta que el solver entrega su primera muestra).
    int total = 0;
    for (const auto* b : bufs) if (b) total = std::max(total, (int)b->size());
    const float totalSecs = std::max(0.0f,
        static_cast<float>(total - 1) * secondsPerSample);

    float dragDx = 0.0f, dragDy = 0.0f;
    if (act) {
        dragDx = ImGui::GetIO().MouseDelta.x;
        dragDy = ImGui::GetIO().MouseDelta.y;
    }

    if (dragDx != 0.0f && pxPerSec > 0.0f) {
        if (zs.viewRightSec < 0.0f) zs.viewRightSec = totalSecs;
        zs.viewRightSec -= dragDx / pxPerSec;
    }
    float viewRightSec;
    if (zs.viewRightSec < 0.0f) {
        viewRightSec = totalSecs;
    } else if (zs.viewRightSec >= totalSecs) {
        zs.viewRightSec = -1.0f;
        viewRightSec   = totalSecs;
    } else {
        zs.viewRightSec = std::max(zs.viewRightSec, 0.0f);
        viewRightSec   = zs.viewRightSec;
    }

    // --- Ventana de samples (común a todos los canales) -------------------
    int rightIdx = std::clamp(
        static_cast<int>(std::round(viewRightSec / secondsPerSample)),
        0, std::max(0, total - 1));
    const int srcStart = std::max(0, rightIdx - kVisible + 1);
    const int N        = rightIdx - srcStart + 1;
    if (N < 2) {
        ImGui::TextDisabled("  [no data yet]");
        ImGui::PopID();
        return;
    }

    // dmin/dmax a través de todos los canales con datos suficientes.
    float dmin = +std::numeric_limits<float>::infinity();
    float dmax = -std::numeric_limits<float>::infinity();
    for (const auto* b : bufs) {
        if (!b || (int)b->size() < srcStart + N) continue;
        const float* d = b->data() + srcStart;
        for (int i = 0; i < N; ++i) {
            if (d[i] < dmin) dmin = d[i];
            if (d[i] > dmax) dmax = d[i];
        }
    }
    if (!std::isfinite(dmin) || !std::isfinite(dmax)) {
        // Ningún canal con datos suficientes todavía.
        ImGui::TextDisabled("  [no data yet]");
        ImGui::PopID();
        return;
    }

    // --- Y auto-fit (expand-only) o manual --------------------------------
    float vmin, vmax;
    if (zs.manual) {
        vmin = zs.center - 0.5f * zs.range;
        vmax = zs.center + 0.5f * zs.range;
    } else {
        const float kPad = 0.20f;
        if (dmin < zs.autoMin)
            zs.autoMin = dmin - kPad * std::max(1.0f, std::fabs(dmin));
        if (dmax > zs.autoMax)
            zs.autoMax = dmax + kPad * std::max(1.0f, std::fabs(dmax));
        vmin = zs.autoMin;
        vmax = zs.autoMax;
        const float center   = 0.5f * (vmin + vmax);
        const float minRange = std::max(1.0f, 0.05f * std::fabs(center));
        if ((vmax - vmin) < minRange) {
            vmin = center - 0.5f * minRange;
            vmax = center + 0.5f * minRange;
        }
        zs.center = 0.5f * (vmin + vmax);
        zs.range  = vmax - vmin;
    }

    // --- Wheel: zoom Y ----------------------------------------------------
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
            vmin = zs.center - 0.5f * zs.range;
            vmax = zs.center + 0.5f * zs.range;
        }
    }

    // --- Drag vertical: pan Y --------------------------------------------
    if (dragDy != 0.0f) {
        if (!zs.manual) {
            zs.manual = true;
            zs.center = 0.5f * (vmin + vmax);
            zs.range  = vmax - vmin;
        }
        zs.center += dragDy * (zs.range / std::max(plotH - 26.0f, 1.0f));
        vmin = zs.center - 0.5f * zs.range;
        vmax = zs.center + 0.5f * zs.range;
    }

    // --- Doble-click: auto-fit centrado + follow latest -----------------
    if (dbl) {
        zs.manual       = false;
        zs.viewRightSec = -1.0f;
        // Centro = promedio de los últimos valores de cada canal con datos.
        float lastAvg = 0.0f; int lastN = 0;
        for (const auto* b : bufs) {
            if (b && (int)b->size() >= srcStart + N) {
                lastAvg += (*b)[srcStart + N - 1];
                ++lastN;
            }
        }
        if (lastN > 0) lastAvg /= lastN;
        const float halfRange = (1.0f + 0.20f) *
            std::max(std::fabs(dmax - lastAvg), std::fabs(lastAvg - dmin));
        const float minHalf = std::max(0.5f, 0.025f * std::fabs(lastAvg));
        const float h = std::max(halfRange, minHalf);
        zs.autoMin = lastAvg - h;
        zs.autoMax = lastAvg + h;
        vmin = zs.autoMin;
        vmax = zs.autoMax;
    }

    // --- Fondo + ejes ---------------------------------------------------
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin,
                      ImVec2(origin.x + invW, origin.y + plotH),
                      IM_COL32(12, 14, 18, 255));

    const ImU32 axisCol = IM_COL32(140, 145, 158, 200);
    const ImU32 tickCol = IM_COL32(170, 180, 195, 230);
    const ImU32 gridCol = IM_COL32(70, 76, 90, 110);

    dl->AddLine(ImVec2(plotA.x, plotA.y), ImVec2(plotA.x, plotB.y), axisCol);
    dl->AddLine(ImVec2(plotA.x, plotB.y), ImVec2(plotB.x, plotB.y), axisCol);

    // --- Rango temporal del axis ----------------------------------------
    const float windowSecs = timeWindowSecs;
    double tLeft, tRight;
    if (viewRightSec < windowSecs) {
        tLeft = 0.0; tRight = windowSecs;
    } else {
        tRight = viewRightSec;
        tLeft  = tRight - windowSecs;
    }
    const double tSpan = std::max(1e-9, tRight - tLeft);

    auto mapY = [&](float v) {
        return plotB.y - (v - vmin) / (vmax - vmin) * innerH;
    };
    auto sampleTime = [&](int i) {
        return static_cast<double>(srcStart + i) * secondsPerSample;
    };
    auto mapXFromTime = [&](double t) {
        return plotA.x + static_cast<float>((t - tLeft) / tSpan) * innerW;
    };

    // --- Ticks Y + grid horizontal --------------------------------------
    auto yTick = [&](float v) {
        float y = mapY(v);
        dl->AddLine(ImVec2(plotA.x, y), ImVec2(plotB.x, y), gridCol);
        dl->AddLine(ImVec2(plotA.x - 3, y), ImVec2(plotA.x, y), axisCol);
        char lab[24];
        std::snprintf(lab, sizeof(lab), "%.4g", v);
        ImVec2 sz = ImGui::CalcTextSize(lab);
        dl->AddText(ImVec2(plotA.x - 6 - sz.x, y - sz.y * 0.5f), tickCol, lab);
    };
    for (int k = 0; k <= 4; ++k) {
        const float t = static_cast<float>(k) / 4.0f;
        yTick(vmin + t * (vmax - vmin));
    }

    auto xTick = [&](double t) {
        float x = mapXFromTime(t);
        dl->AddLine(ImVec2(x, plotA.y), ImVec2(x, plotB.y), gridCol);
        dl->AddLine(ImVec2(x, plotB.y), ImVec2(x, plotB.y + 3), axisCol);
        char lab[24];
        std::snprintf(lab, sizeof(lab), "%.2fs", t);
        ImVec2 sz = ImGui::CalcTextSize(lab);
        dl->AddText(ImVec2(x - sz.x * 0.5f, plotB.y + 4), tickCol, lab);
    };
    for (int k = 0; k <= 4; ++k) {
        xTick(tLeft + (static_cast<double>(k) / 4.0) * tSpan);
    }

    // --- Curvas por canal -----------------------------------------------
    dl->PushClipRect(plotA, plotB, true);
    for (size_t ch = 0; ch < bufs.size(); ++ch) {
        const auto* b = bufs[ch];
        if (!b || (int)b->size() < srcStart + N) continue;
        const float* d = b->data() + srcStart;
        const ImU32 color = (ch < colors.size()) ? colors[ch]
                                                 : IM_COL32(180, 180, 180, 255);
        for (int i = 1; i < N; ++i) {
            const double t0 = sampleTime(i - 1);
            const double t1 = sampleTime(i);
            if (t1 < tLeft || t0 > tRight) continue;
            ImVec2 p0(mapXFromTime(t0), mapY(d[i - 1]));
            ImVec2 p1(mapXFromTime(t1), mapY(d[i]));
            dl->AddLine(p0, p1, color, 1.4f);
        }
    }
    dl->PopClipRect();

    // --- Dot en el último valor + leyenda fija en top-right --------------
    // El dot marca el último sample visible de cada canal.  El texto vive
    // anclado a la esquina superior derecha del plot (apilado en orden de
    // canal), no encima de la curva — así nunca tapa puntos de la señal.
    struct LastEntry { ImU32 color; std::string text; };
    std::vector<LastEntry> entries;
    entries.reserve(bufs.size());
    for (size_t ch = 0; ch < bufs.size(); ++ch) {
        const auto* b = bufs[ch];
        if (!b || (int)b->size() < srcStart + N) continue;
        const float last = (*b)[srcStart + N - 1];
        const float yPx  = mapY(last);
        ImU32 color = (ch < colors.size()) ? colors[ch]
                                           : IM_COL32(180, 180, 180, 255);
        if (yPx >= plotA.y && yPx <= plotB.y) {
            const float xPx = mapXFromTime(sampleTime(N - 1));
            if (xPx >= plotA.x && xPx <= plotB.x)
                dl->AddCircleFilled(ImVec2(xPx, yPx), 3.0f, color);
        }

        char buf[64];
        const char* chName = (ch < channelLabels.size() && !channelLabels[ch].empty())
                             ? channelLabels[ch].c_str() : nullptr;
        if (chName)
            std::snprintf(buf, sizeof(buf), "%s = %.4g", chName, last);
        else
            std::snprintf(buf, sizeof(buf), "%.4g", last);
        entries.push_back({ color, std::string(buf) });
    }
    const float lineH = ImGui::GetTextLineHeightWithSpacing();
    float labY = plotA.y + 4.0f;
    for (const auto& e : entries) {
        ImVec2 sz   = ImGui::CalcTextSize(e.text.c_str());
        float  labX = plotB.x - sz.x - 6.0f;
        dl->AddRectFilled(ImVec2(labX - 3, labY - 1),
                          ImVec2(labX + sz.x + 3, labY + sz.y + 1),
                          IM_COL32(20, 22, 28, 200), 2.0f);
        dl->AddText(ImVec2(labX, labY), e.color, e.text.c_str());
        labY += lineH;
    }

    // --- Crosshair on hover (t-y readout) -------------------------------
    // Cuando el mouse pasa sobre el plot, dibujamos:
    //   - una guía vertical desde el cursor hasta el eje X (muestra el t),
    //   - una guía horizontal hasta el eje Y,
    //   - un círculo sobre el sample más cercano de cada canal,
    //   - un tooltip flotante con el par ordenado (t, y_chN ...).
    if (hov) {
        const ImVec2 mp = ImGui::GetMousePos();
        if (mp.x >= plotA.x && mp.x <= plotB.x &&
            mp.y >= plotA.y && mp.y <= plotB.y) {
            const double tCursor = tLeft +
                static_cast<double>(mp.x - plotA.x) / innerW * tSpan;

            // Guías rectoras (alfa baja para no opacar la curva).
            const ImU32 chCol = IM_COL32(220, 220, 220, 110);
            dl->AddLine(ImVec2(mp.x, plotA.y),
                        ImVec2(mp.x, plotB.y), chCol, 1.0f);
            dl->AddLine(ImVec2(plotA.x, mp.y),
                        ImVec2(plotB.x, mp.y), chCol, 1.0f);

            // Sample más cercano al cursor (en índice global del buffer).
            const int idx = std::clamp(
                static_cast<int>(std::round(tCursor / secondsPerSample)),
                0, total - 1);
            const double tSnap = static_cast<double>(idx) * secondsPerSample;
            const float  xSnap = mapXFromTime(tSnap);

            // Tooltip: par ordenado (t, y) por canal.
            ImGui::BeginTooltip();
            ImGui::Text("t = %.4f s", tSnap);
            for (size_t ch = 0; ch < bufs.size(); ++ch) {
                const auto* b = bufs[ch];
                if (!b || idx >= (int)b->size()) continue;
                const float yVal = (*b)[idx];
                const float yPx  = mapY(yVal);

                const ImU32 color = (ch < colors.size()) ? colors[ch]
                    : IM_COL32(180, 180, 180, 255);
                if (yPx >= plotA.y && yPx <= plotB.y &&
                    xSnap >= plotA.x && xSnap <= plotB.x) {
                    dl->AddCircleFilled(ImVec2(xSnap, yPx), 4.0f, color);
                    dl->AddCircle(ImVec2(xSnap, yPx), 4.0f,
                                  IM_COL32(255, 255, 255, 200), 0, 1.0f);
                }

                const char* chName =
                    (ch < channelLabels.size() && !channelLabels[ch].empty())
                    ? channelLabels[ch].c_str()
                    : (bufs.size() == 1 ? "y" : "ch");
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::Text("%s = %.6g", chName, yVal);
                ImGui::PopStyleColor();
            }
            ImGui::EndTooltip();
        }
    }

    // --- Indicador \"paused\" si el usuario está mirando historia ------
    if (zs.viewRightSec >= 0.0f && totalSecs > windowSecs) {
        const char* tag = "◀ paused — drag right edge or [A] to follow";
        ImVec2 sz = ImGui::CalcTextSize(tag);
        dl->AddRectFilled(ImVec2(plotA.x + 4, plotA.y + 4),
                          ImVec2(plotA.x + 4 + sz.x + 8, plotA.y + 4 + sz.y + 4),
                          IM_COL32(60, 35, 35, 220), 3.0f);
        dl->AddText(ImVec2(plotA.x + 8, plotA.y + 6),
                    IM_COL32(240, 200, 200, 255), tag);
    }

    ImGui::PopID();

    // --- Botón [A] ------------------------------------------------------
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::PushID(label);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    if (ImGui::SmallButton("A##autofit")) {
        zs.manual       = false;
        zs.viewRightSec = -1.0f;
        // Centro = promedio de los últimos valores.
        float lastAvg = 0.0f; int lastN = 0;
        for (const auto* b : bufs) {
            if (b && (int)b->size() >= srcStart + N) {
                lastAvg += (*b)[srcStart + N - 1];
                ++lastN;
            }
        }
        if (lastN > 0) lastAvg /= lastN;
        const float halfRange = (1.0f + 0.20f) *
            std::max(std::fabs(dmax - lastAvg), std::fabs(lastAvg - dmin));
        const float minHalf = std::max(0.5f, 0.025f * std::fabs(lastAvg));
        const float h = std::max(halfRange, minHalf);
        zs.autoMin = lastAvg - h;
        zs.autoMax = lastAvg + h;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Auto-escala Y al promedio de los últimos valores\n"
                          "+ volver a seguir el último sample");
    ImGui::PopStyleVar();
    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// renderWave — wrapper single-canal (delegación pura).
// ---------------------------------------------------------------------------
void renderWave(const char* label,
                const std::vector<float>& buf, int wIdx,
                float plotW, float plotH,
                ImU32 lineColor,
                PlotPanel::ZoomState& zs,
                float secondsPerSample,
                double currentSimTime,
                float timeWindowSecs) {
    (void)wIdx; (void)currentSimTime;
    std::vector<const std::vector<float>*> bufs{ &buf };
    std::vector<ImU32>                     colors{ lineColor };
    std::vector<std::string>               labels{ "" };
    renderMultiWave(label, bufs, colors, labels,
                    plotW, plotH, zs, secondsPerSample, timeWindowSecs);
}

}  // namespace scinodes::ui::plots
