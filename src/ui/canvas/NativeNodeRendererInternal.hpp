#pragma once

// ---------------------------------------------------------------------------
// NativeNodeRendererInternal — header privado para el split de
// NativeNodeRenderer.cpp (etapa 6K.F).
//
// Constantes locales (colores default, layout, zoom limits, hit-test radii,
// channels del drawList) y helper `drawPinShape`.  Antes vivían en un
// anon namespace dentro del .cpp; ahora `inline`/`inline constexpr` en
// `scinodes::ui::native_renderer_detail` para que los .cpp del split
// las compartan sin duplicar.
//
// NO incluir desde fuera de `src/ui/canvas/`.
// ---------------------------------------------------------------------------

#include "Canvas.hpp"

#include <imgui.h>

namespace scinodes::ui::native_renderer_detail {

// ===========================================================================
// Colores default cuando el color stack está vacío
// ===========================================================================
//
// El cuerpo del nodo es 100% opaco para que se integre con los widgets
// internos (DragFloat / InputText) que tampoco son transparentes — sin
// esto las "capas" del nodo se ven entrelazadas con el fondo, los edges
// y las celdas de los valores numéricos.
inline constexpr ImU32 kDefaultNodeBg     = IM_COL32( 40,  44,  52, 255);
inline constexpr ImU32 kDefaultNodeBorder = IM_COL32(180, 180, 200, 255);
inline constexpr ImU32 kDefaultTitleBg    = IM_COL32( 70,  90, 160, 255);
inline constexpr ImU32 kDefaultPin        = IM_COL32(200, 200, 200, 255);
inline constexpr ImU32 kDefaultLink       = IM_COL32(200, 200, 200, 220);
inline constexpr ImU32 kGridLine          = IM_COL32(50, 55, 65, 180);

// ===========================================================================
// Layout / interacción
// ===========================================================================
inline constexpr float kZoomMin = 0.25f;
inline constexpr float kZoomMax = 3.0f;

// Distancia (en píxeles modelo, multiplicada por zoom al dibujar) de
// los puntos de control de la Bézier de cada cable — define cuánto se
// "ondula" la curva.  Hereda escala con `state.zoom` para conservar
// proporciones al ampliar/reducir.
inline constexpr float kBezierControlDist = 40.f;

// Radio (en píxeles pantalla, escalado por zoom) del hit-test "ampliado"
// para snap al pin más cercano cuando el usuario está arrastrando
// (Drag::Pin).  Convención de Blender / Unreal Material Editor.
inline constexpr float kPinSnapRadiusPx = 30.f;

// Para nodos cuya posición no está en `state.positions`: cae a una
// rejilla diagonal (paso X×Y) en una pseudo-fila ciclando cada
// kDefaultNodeCols nodos.  Solo aparece si el caller no llamó
// `setNodePosition` antes de drawNode.
inline constexpr float kDefaultNodeStepX = 100.f;
inline constexpr float kDefaultNodeStepY =  60.f;
inline constexpr int   kDefaultNodeCols  =   5;

// Canales del drawList — sólo dos, para que cada nodo se dibuje como una
// unidad contigua sin interlavarse con otros nodos.  Channel 0 acumula
// grid + edges (fondo); channel 1 acumula los nodos, en orden de
// iteración (último nodo dibujado = encima).
inline constexpr int kChBackground = 0;
inline constexpr int kChForeground = 1;
inline constexpr int kNumChannels  = 2;

// ===========================================================================
// drawPinShape — dispatch del shape del pin (Circle / Triangle / Square)
// ===========================================================================
inline void drawPinShape(ImDrawList* dl, ImVec2 c, float r,
                         PortShape s, ImU32 fill) {
    switch (s) {
        case PortShape::CircleFilled: dl->AddCircleFilled(c, r, fill, 12); break;
        case PortShape::Circle:       dl->AddCircle(c, r, fill, 12, 1.5f); break;
        case PortShape::Triangle: {
            const ImVec2 p0 = { c.x - r, c.y - r };
            const ImVec2 p1 = { c.x + r, c.y     };
            const ImVec2 p2 = { c.x - r, c.y + r };
            dl->AddTriangleFilled(p0, p1, p2, fill);
            break;
        }
        case PortShape::Square: {
            dl->AddRectFilled({ c.x - r, c.y - r }, { c.x + r, c.y + r }, fill);
            break;
        }
    }
}

}  // namespace scinodes::ui::native_renderer_detail
