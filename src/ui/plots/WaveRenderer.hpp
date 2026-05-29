#pragma once

#include "../PlotPanel.hpp"   // ZoomState
#include <imgui.h>
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

void renderWave(const char* label,
                const std::vector<float>& buf, int wIdx,
                float plotW, float plotH,
                ImU32 lineColor,
                PlotPanel::ZoomState& zs,
                float secondsPerSample = 1.0f / 60.0f,
                double currentSimTime  = 0.0);

}  // namespace scinodes::ui::plots
