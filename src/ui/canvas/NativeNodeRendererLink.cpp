#include "NativeNodeRenderer.hpp"
#include "NativeNodeRendererInternal.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace scinodes::ui {

using namespace scinodes::ui::native_renderer_detail;

// ---------------------------------------------------------------------------
// NativeNodeRendererLink — split (etapa 6K.F).  drawLink: cables Bézier
// con corner control points, hit-test y color por categoría.
// ---------------------------------------------------------------------------

// ===========================================================================
// Edges
// ===========================================================================
void NativeNodeRenderer::drawLink(int linkId, int fromAttrId, int toAttrId) {
    if (!m_drawList || !m_activeState) return;
    auto itF = m_framePins.find(fromAttrId);
    auto itT = m_framePins.find(toAttrId);
    if (itF == m_framePins.end() || itT == m_framePins.end()) return;

    // Registrar la edge indexada por el input attrId — al hacer click
    // sobre un input conectado, handleInteraction usa este mapa para
    // saber a qué output transferir el drag.
    m_frameEdgesByInput[toAttrId] = { linkId, fromAttrId };

    // Color del cable: brillante si está seleccionado directamente o
    // si alguno de los nodos extremo está seleccionado (iluminación de
    // las conexiones del nodo activo — UX clásica de editor).
    const int fromNode = attrNodeId(fromAttrId);
    const int toNode   = attrNodeId(toAttrId);
    const auto& sel    = m_activeState->selectedNodes;
    const bool nodeSel = sel.count(fromNode) > 0 || sel.count(toNode) > 0;
    const bool linkSel = m_activeState->selectedLinks.count(linkId) > 0;
    const ImU32 col = (linkSel || nodeSel)
        ? IM_COL32(255, 220, 80, 255)
        : colorOr(ColorKey::Link, kDefaultLink);
    const float thick = (linkSel ? 4.0f : 2.5f) * m_activeState->zoom;
    const ImVec2 a = itF->second.center;
    const ImVec2 b = itT->second.center;

    // Bézier cúbica con tangentes horizontales: el cable sale horizontal
    // del output (a la derecha) y entra horizontal al input (desde la
    // izquierda).  Es la convención de los editores nodales profesionales
    // (Blender, Unreal Material Editor, TouchDesigner).
    //
    // dirX = +1 si el pin es output (lado derecho del nodo → tangente
    // hacia la derecha), -1 si es input (lado izquierdo del nodo →
    // tangente hacia la izquierda).  Los control points van EN la
    // dirección de la tangente: p1 = a + dirA·d, p2 = b + dirB·d.
    // kCtrlDist crece con la distancia horizontal entre pines para que
    // el "ondulado" del cable sea proporcional al espacio disponible.
    const float dirA = itF->second.isOutput ?  1.f : -1.f;
    const float dirB = itT->second.isOutput ?  1.f : -1.f;
    // Antes: ctrlDist = max(min, |dx|/2) sin cap → conexiones largas o
    // feedback edges (destino a la izquierda del origen) generaban
    // bucles enormes que se salían del área visual.  Cap a 3× el mínimo
    // para que cables de cualquier distancia tengan curvas razonables.
    const float zoom = m_activeState->zoom;
    const float dx   = std::fabs(b.x - a.x);
    const float ctrlDist = std::clamp(dx * 0.5f,
                                      kBezierControlDist * zoom,
                                      kBezierControlDist * 3.f * zoom);
    const ImVec2 p1 = { a.x + dirA * ctrlDist, a.y };
    const ImVec2 p2 = { b.x + dirB * ctrlDist, b.y };

    m_drawList->ChannelsSetCurrent(kChBackground);
    m_drawList->AddBezierCubic(a, p1, p2, b, col, thick, /*segments*/ 0);
    m_drawList->ChannelsSetCurrent(kChForeground);

    // Hover test sobre la Bézier: muestreamos N puntos y medimos la
    // distancia del cursor a CADA SEGMENTO consecutivo (no a los
    // vértices).  Con sólo distancia-a-vértice quedaban zonas muertas
    // entre muestras donde la selección fallaba — el cable se ve
    // continuo pero el clic se "cae" si aterriza lejos de un sample.
    // 24 segmentos dan ~16 px de paso para un cable de 400 px y la
    // distancia-a-segmento cubre todo lo que hay entre vértices.
    const ImVec2 mouse = ImGui::GetMousePos();
    const float hitR2 = 8.f * 8.f;
    constexpr int kSamples = 24;
    auto bezier = [&](float t) -> ImVec2 {
        const float u = 1.f - t;
        return {
            u*u*u*a.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*b.x,
            u*u*u*a.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*b.y,
        };
    };
    auto distToSegSq = [](ImVec2 p, ImVec2 v, ImVec2 w) -> float {
        const float dx = w.x - v.x, dy = w.y - v.y;
        const float len2 = dx*dx + dy*dy;
        float t = 0.f;
        if (len2 > 1e-6f)
            t = std::max(0.f, std::min(1.f,
                ((p.x - v.x) * dx + (p.y - v.y) * dy) / len2));
        const float qx = v.x + t * dx, qy = v.y + t * dy;
        const float ex = p.x - qx, ey = p.y - qy;
        return ex*ex + ey*ey;
    };
    ImVec2 prev = bezier(0.f);
    for (int i = 1; i <= kSamples; ++i) {
        const ImVec2 q = bezier(i / float(kSamples));
        if (distToSegSq(mouse, prev, q) < hitR2) {
            m_hoveredLinkId = linkId;
            break;
        }
        prev = q;
    }
}

// ===========================================================================
// Posiciones
// ===========================================================================

}  // namespace scinodes::ui
