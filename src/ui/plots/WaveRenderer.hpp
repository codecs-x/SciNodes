#pragma once

#include "ZoomState.hpp"
#include <imgui.h>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// renderWave — rolling time-series con ejes labeled, ticks numéricos y
// Y-zoom interactivo.
//
// Interacciones (sobre el área del plot):
//   - rueda del mouse (cursor encima)   → zoom Y multiplicativo
//   - click-drag vertical                → pan Y
//   - doble-click izquierdo              → auto-escala
//   - botón [A] a la derecha del plot    → auto-escala
//
// Usado por Oscilloscope (y cualquier sink con un solo canal y serie
// temporal).  Reemplaza ImGui::PlotLines que normaliza el rango y hace
// que el ruido numérico parezca oscilación grande.
// -----------------------------------------------------------------------------
namespace scinodes::ui::plots {

// Versión single-canal — wrapper de conveniencia para Oscilloscope
// con un solo input.  Internamente llama a renderMultiWave.
void renderWave(const char* label,
                const std::vector<float>& buf, int wIdx,
                float plotW, float plotH,
                ImU32 lineColor,
                scinodes::ui::plots::ZoomState& zs,
                float secondsPerSample = 1.0f / 60.0f,
                double currentSimTime  = 0.0,
                float timeWindowSecs   = 0.0f); // 0 ⇒ default visible

// Versión multi-canal: cada canal tiene su buffer y color propios.
// El eje X (tiempo) y el eje Y (auto-fit + manual) son compartidos.
// Etiquetas de los canales aparecen sobre el último valor de cada uno.
void renderMultiWave(const char* label,
                     const std::vector<const std::vector<float>*>& bufs,
                     const std::vector<ImU32>&                     colors,
                     const std::vector<std::string>&               channelLabels,
                     float plotW, float plotH,
                     scinodes::ui::plots::ZoomState& zs,
                     float secondsPerSample = 1.0f / 60.0f,
                     float timeWindowSecs   = 0.0f);

}  // namespace scinodes::ui::plots
