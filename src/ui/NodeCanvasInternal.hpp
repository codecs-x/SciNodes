#pragma once

// ---------------------------------------------------------------------------
// NodeCanvasInternal — helpers privados compartidos por los .cpp del split
// del NodeCanvas (etapa 6K.C).
//
// El monolítico NodeCanvas.cpp (~2750 líneas) se está partiendo por
// secciones: rendering, popups, panels, ops, IO.  Cada uno necesita las
// tablas de colores por categoría — las exponemos como `inline` para
// que vivan en UN solo header y cada .cpp las inlinee sin conflictos
// de linkage.
//
// Este header NO es público — no debería incluirse desde fuera de
// `src/ui/`.  No tiene parte de la API del NodeCanvas; sólo utilities.
// ---------------------------------------------------------------------------

#include "../core/NodeInstance.hpp"
#include "../core/NodeType.hpp"

#include <imgui.h>

namespace scinodes::ui::canvas_detail {

inline ImU32 titleCol(NodeCategory c) {
    switch (c) {
        case NodeCategory::Source:      return IM_COL32( 42, 138,  62, 255);
        case NodeCategory::Transformer: return IM_COL32( 48,  90, 178, 255);
        case NodeCategory::Device:      return IM_COL32(112,  78, 178, 255);
        case NodeCategory::Sink:        return IM_COL32(175,  50,  50, 255);
    }
    return IM_COL32(100, 100, 100, 255);
}

inline ImU32 titleHovCol(NodeCategory c) {
    switch (c) {
        case NodeCategory::Source:      return IM_COL32( 60, 180,  80, 255);
        case NodeCategory::Transformer: return IM_COL32( 68, 120, 220, 255);
        case NodeCategory::Device:      return IM_COL32(140, 100, 220, 255);
        case NodeCategory::Sink:        return IM_COL32(220,  70,  70, 255);
    }
    return IM_COL32(140, 140, 140, 255);
}

// Color del cable: verde para Source, azul para Transformer, gris para
// el resto.  La categoría se resuelve por el lado del source (nodo
// emisor) — la dirección del flujo se "siente" visualmente.
inline ImU32 wireCol(int fromNodeId, const NodeGraph& g) {
    const NodeInstance* n = g.findNode(fromNodeId);
    if (!n) return IM_COL32(200, 200, 200, 220);
    switch (categoryOf(*n)) {
        case NodeCategory::Source:      return IM_COL32( 80, 200,  80, 220);
        case NodeCategory::Transformer: return IM_COL32( 80, 140, 220, 220);
        default:                        return IM_COL32(200, 200, 200, 220);
    }
}

}  // namespace scinodes::ui::canvas_detail
