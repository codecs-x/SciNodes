#pragma once

#include <imgui.h>
#include <vector>

// -----------------------------------------------------------------------------
// renderHistogram — bin del ring buffer en N bins sobre [min, max] y
// dibuja barras + overlay con n / μ / σ / range.
//
// Usado por DistributionSink para distribuciones live de Monte-Carlo
// (TolerancePerturbator).  Solo los samples ya escritos (writeIdx) entran
// al histograma — los slots iniciales no-escritos no sesgan la media.
// -----------------------------------------------------------------------------
namespace scinodes::ui::plots {

void renderHistogram(const std::vector<float>& buf, int wIdx,
                     int binCount,
                     float plotW, float plotH,
                     ImU32 barColor);

}  // namespace scinodes::ui::plots
