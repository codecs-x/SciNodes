#pragma once

#include <imgui.h>
#include <vector>

// -----------------------------------------------------------------------------
// renderHeatmap — 2-D scatter coloreado.  Inputs son tuplas (x, y, c) en
// los tres canales del sink; cada sample se renderiza como un círculo
// pequeño con color siguiendo un gradiente tipo viridis sobre el rango
// [min, max] del canal c.
//
// Usado por HeatmapSink — típicamente para mapas térmicos espaciales
// (B_g vs θ_eléctrico, T vs posición axial, etc.).
// -----------------------------------------------------------------------------
namespace scinodes::ui::plots {

void renderHeatmap(const std::vector<float>& bufX, int wX,
                   const std::vector<float>& bufY, int wY,
                   const std::vector<float>& bufC, int wC,
                   float plotW, float plotH);

}  // namespace scinodes::ui::plots
