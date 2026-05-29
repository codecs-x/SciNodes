#pragma once

#include <limits>

// ---------------------------------------------------------------------------
// ZoomState — estado de zoom + pan compartido por todos los plot
// renderers (etapa 6K.D).
//
// Antes vivía como `PlotPanel::ZoomState`, lo que forzaba a cada plot
// renderer en `src/ui/plots/` a `#include "../PlotPanel.hpp"` — una
// dep invertida (renderers reachando hacia su host).  Se movió a la
// capa de plots, donde semánticamente pertenece: el estado lo CONSUME
// el renderer; PlotPanel sólo lo persiste por nodo-sumidero.
//
// PlotPanel mantiene un alias `using ZoomState = scinodes::ui::plots::ZoomState`
// para no romper el resto del código que lo referencia con el qualifier
// histórico.
// ---------------------------------------------------------------------------

namespace scinodes::ui::plots {

// Estado de zoom Y por sumidero.  `manual = false` → auto-escala
// "expand-only": el rango visible solo crece cuando aparecen valores
// fuera del rango actual; nunca se encoje automáticamente, así la
// gráfica no oscila cuando los datos viejos del ring buffer caen
// fuera del rango estable.  Doble-click recentra al rango actual y
// vuelve a auto.  `manual = true` → usa (center, range) explícitos
// controlados por rueda y drag.
struct ZoomState {
    bool  manual = false;
    float center = 0.0f;
    float range  = 1.0f;
    // Auto-bounds históricos (expand-only).  -inf/+inf = aún sin
    // datos.  Reset al hacer doble-click sobre el plot.
    float autoMin = +std::numeric_limits<float>::infinity();
    float autoMax = -std::numeric_limits<float>::infinity();
    // Pan horizontal: tiempo absoluto del borde derecho de la
    // cámara, en segundos.
    //   viewRightSec < 0   → "follow latest" (default): el borde
    //                        derecho sigue al último sample.
    //   viewRightSec >= 0  → bloqueado en ese tiempo absoluto;
    //                        la cámara muestra historia fija.
    // Se controla con left-click + drag horizontal sobre el plot.
    float viewRightSec = -1.0f;
    // Para plots 2-D (PhasePortrait, Heatmap) — factor de zoom
    // alrededor del centro auto.  1.0 = como auto-fit; >1 = zoom
    // in (rango visible más pequeño); <1 = zoom out.
    float scale = 1.0f;
};

}  // namespace scinodes::ui::plots
