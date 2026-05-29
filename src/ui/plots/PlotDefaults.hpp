#pragma once

// ---------------------------------------------------------------------------
// PlotDefaults — constantes de la capa de visualización (etapa 6K.D).
//
// Antes vivían en `ScilabBridge.hpp` como `DEFAULT_VISIBLE_SAMPLES`, lo
// que obligaba a los plot renderers a incluir el header entero del
// bridge sólo para leer una constante de presentación.  La capa de
// plots ahora tiene su propio header de defaults, semánticamente más
// honesto: "cuántos samples mostrar por defecto" es una decisión de
// visualización, no de simulación.
//
// El bridge sigue usando su propia constante de reserva de ring buffer
// (`ScilabBridge.cpp`/`hpp` internamente) — pueden coincidir en valor
// pero no en concepto.
// ---------------------------------------------------------------------------

namespace scinodes::ui::plots {

// Ventana de muestras visible por defecto en plots tipo wave / phase /
// heatmap cuando el usuario no ha definido `Time Window` explícito.
inline constexpr int kDefaultVisibleSamples = 512;

}  // namespace scinodes::ui::plots
