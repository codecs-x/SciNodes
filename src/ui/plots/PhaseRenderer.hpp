#pragma once

#include "../PlotPanel.hpp"   // ZoomState
#include <imgui.h>
#include <vector>

// -----------------------------------------------------------------------------
// renderPhase — 2-D phase portrait.  Channel 0 → eje x, channel 1 → eje y.
// Traza la trayectoria como polyline a través del ring buffer; el último
// sample se marca con un círculo.
//
// Ticks numéricos en ambos ejes + Ctrl+wheel para zoom manual con
// indicador "zoom Nx".  Doble-click resetea a auto-fit.
// -----------------------------------------------------------------------------
namespace scinodes::ui::plots {

void renderPhase(const std::vector<float>& bufX, int wX,
                 const std::vector<float>& bufY, int wY,
                 float plotW, float plotH,
                 ImU32 lineColor,
                 PlotPanel::ZoomState& zs);

}  // namespace scinodes::ui::plots
