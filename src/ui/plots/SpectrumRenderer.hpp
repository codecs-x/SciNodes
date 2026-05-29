#pragma once

#include <imgui.h>
#include <vector>

// -----------------------------------------------------------------------------
// renderSpectrum — magnitude spectrum de los samples recientes de un sink.
//
// `binCount` es el window FFT deseado (param "Bin Count" en FFTAnalyzer);
// se snapea hacia abajo a la mayor potencia de 2 que no exceda min(req,
// ring-buffer size).  La bin DC se omite para mantener el rango Y útil.
// -----------------------------------------------------------------------------
namespace scinodes::ui::plots {

void renderSpectrum(const char* label,
                    const std::vector<float>& buf, int wIdx,
                    int binCount,
                    float plotW, float plotH,
                    ImU32 lineColor);

}  // namespace scinodes::ui::plots
