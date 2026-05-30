#include "NativeNodeRenderer.hpp"
#include "NativeNodeRendererInternal.hpp"

#include "../../core/I18n.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace scinodes::ui {

using namespace scinodes::ui::native_renderer_detail;

// ---------------------------------------------------------------------------
// NativeNodeRendererInteraction — split (etapa 6K.F).  Polling de eventos
// del frame (linkCreated, linkDropped, linkDetached, link/node hover) y la
// gran handleInteraction (drag de nodos, link-drag desde pin, snap-to-pin,
// pan/zoom de canvas, marquee selection, etc.).
// ---------------------------------------------------------------------------

// ===========================================================================
// Eventos del frame
// ===========================================================================
bool NativeNodeRenderer::pollLinkCreated(LinkCreatedEvent& out) {
    if (!m_pendingLinkCreated) return false;
    out.fromAttrId       = m_pendingLinkCreatedFrom;
    out.toAttrId         = m_pendingLinkCreatedTo;
    m_pendingLinkCreated = false;
    return true;
}
bool NativeNodeRenderer::pollLinkDropped(int& outStartAttrId) {
    if (!m_pendingLinkDropped) return false;
    outStartAttrId       = m_pendingLinkDroppedFrom;
    m_pendingLinkDropped = false;
    return true;
}
bool NativeNodeRenderer::pollLinkDetached(int& outLinkId) {
    if (!m_pendingLinkDetached) return false;
    outLinkId             = m_pendingLinkDetachedId;
    m_pendingLinkDetached = false;
    return true;
}
bool NativeNodeRenderer::isLinkHovered(int& outLinkId) {
    if (m_hoveredLinkId == 0) return false;
    outLinkId = m_hoveredLinkId;
    return true;
}
bool NativeNodeRenderer::isNodeHovered(int& outNodeId) {
    if (m_hoveredNodeId == 0) return false;
    outNodeId = m_hoveredNodeId;
    return true;
}

// ===========================================================================
// Interacción del frame (Fase 2)
//
// State machine: m_drag ∈ {None, Node, Pin, Pan}.  Transiciones:
//
//   None  -- click-izq sobre pin    --> Pin
//   None  -- click-izq sobre nodo   --> Node     (+ selección)
//   None  -- click-izq en vacío     --> None     (limpia selección)
//   None  -- click-medio            --> Pan
//   Node  -- mouse-drag             --> mueve selectedNodes
//   Pin   -- mouse-drag             --> preview línea pin→mouse
//   Pan   -- mouse-drag             --> ajusta state.pan
//   *     -- release del botón      --> None     (Pin emite Created/Dropped)
//
// Además: wheel zoom centrado en el mouse (fórmula §8 del design doc).
// ===========================================================================
void NativeNodeRenderer::handleInteraction() {
    if (!m_activeState) return;
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
        return;

    auto& state = *m_activeState;
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = ImGui::GetMousePos();

    // ---- (1) Hover sobre pines ------------------------------------------
    // Hit-test contra m_framePins acumulado durante el draw.  Tomamos el
    // más cercano dentro del radio escalado.  Durante Drag::Pin el radio
    // se expande para implementar "snap" al pin más próximo: el usuario
    // no tiene que aterrizar pixel-perfecto, basta acercar el cable y se
    // engancha (convención de Blender / Unreal Material Editor).
    m_hoveredPinAttr = 0;
    {
        const bool snapping = (m_drag == Drag::Pin);
        const float hitR = (snapping
                            ? kPinSnapRadiusPx           // radio amplio al arrastrar
                            : (kNodePinRadius + 2.f))    // hit-test normal
                          * state.zoom;
        const float hitR2 = hitR * hitR;
        float bestDist2 = hitR2;
        for (const auto& [attr, pin] : m_framePins) {
            if (snapping && attr == m_dragPinAttr) continue;  // no snap al origen
            const float dx = mouse.x - pin.center.x;
            const float dy = mouse.y - pin.center.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestDist2) {
                m_hoveredPinAttr = attr;
                bestDist2 = d2;
            }
        }
    }

    // ---- (2) Click-press -------------------------------------------------
    // Ignoramos el click si activó un widget de ImGui (p. ej. un
    // DragFloat dentro del nodo).  Antes chequeábamos también
    // IsAnyItemHovered, pero eso incluía Text/TextDisabled, así que un
    // click sobre el título del nodo se descartaba — sin selección
    // visible.  Ahora solo IsAnyItemActive (=alguien tiene el foco de
    // input), que distingue el caso real de "widget capturó el click".
    const bool widgetCaptured = ImGui::IsAnyItemActive();
    if (m_drag == Drag::None &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !widgetCaptured) {
        if (m_hoveredPinAttr != 0) {
            m_drag = Drag::Pin;
            m_dragDetachLinkId = 0;
            // Si el pin clickeado es un INPUT y ya tiene un edge,
            // "agarramos" el cable existente: el drag arranca en el
            // OUTPUT del otro extremo y al soltar el modelo borra el
            // edge previo (vía pollLinkDetached).  Si el usuario suelta
            // sobre otro input válido, NodeCanvas crea uno nuevo —
            // efecto neto: reconectar.  Si suelta en vacío, se queda
            // borrado — efecto neto: desconectar.
            auto pinIt = m_framePins.find(m_hoveredPinAttr);
            const bool clickedInput =
                (pinIt != m_framePins.end() && !pinIt->second.isOutput);
            auto edgeIt = m_frameEdgesByInput.find(m_hoveredPinAttr);
            if (clickedInput && edgeIt != m_frameEdgesByInput.end()) {
                m_dragDetachLinkId = edgeIt->second.first;
                m_dragPinAttr      = edgeIt->second.second;  // output del otro lado
            } else {
                m_dragPinAttr = m_hoveredPinAttr;
            }
        } else if (m_hoveredLinkId != 0) {
            // Click sobre un cable: lo selecciona.  Mismas reglas de
            // modificadores que para nodos.
            const int id = m_hoveredLinkId;
            if (io.KeyCtrl) {
                if (state.selectedLinks.count(id))
                    state.selectedLinks.erase(id);
                else
                    state.selectedLinks.insert(id);
            } else {
                state.selectedLinks.clear();
                state.selectedLinks.insert(id);
                state.selectedNodes.clear();
            }
        } else if (m_hoveredNodeId != 0) {
            m_drag = Drag::Node;
            m_dragNodeId = m_hoveredNodeId;
            // Click sobre nodo limpia la selección de cables (la
            // selección es lógicamente exclusiva: nodos OR cables,
            // no mezclados).
            if (!io.KeyCtrl) state.selectedLinks.clear();
            if (io.KeyCtrl) {
                if (state.selectedNodes.count(m_hoveredNodeId))
                    state.selectedNodes.erase(m_hoveredNodeId);
                else
                    state.selectedNodes.insert(m_hoveredNodeId);
            } else if (!state.selectedNodes.count(m_hoveredNodeId)) {
                state.selectedNodes.clear();
                state.selectedNodes.insert(m_hoveredNodeId);
            }
        } else {
            // Click en vacío → empezar rect-select.  Si no hay
            // modificadores, la selección previa se descarta al
            // soltar; con Shift, se ADD; con Ctrl, se TOGGLE.
            m_drag = Drag::RectSelect;
            m_rectSelectStart = mouse;
            m_rectSelectBase = (io.KeyShift || io.KeyCtrl)
                ? state.selectedNodes
                : std::unordered_set<int>{};
            if (!io.KeyCtrl && !io.KeyShift) state.selectedLinks.clear();
        }
    }
    if (m_drag == Drag::None &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        m_drag = Drag::Pan;
    }

    // ---- (3) Drag continue ----------------------------------------------
    if (m_drag == Drag::Node &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.f)) {
        // Convertimos delta de pantalla a model units dividiendo por zoom.
        const float dx = io.MouseDelta.x / state.zoom;
        const float dy = io.MouseDelta.y / state.zoom;
        for (int id : state.selectedNodes) {
            auto it = state.positions.find(id);
            if (it != state.positions.end()) {
                it->second.x += dx;
                it->second.y += dy;
            }
        }
    }
    if (m_drag == Drag::Pan &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.f)) {
        state.pan.x += io.MouseDelta.x;
        state.pan.y += io.MouseDelta.y;
    }
    if (m_drag == Drag::RectSelect && m_drawList) {
        // Dibujamos el rectángulo de selección + recalculamos la
        // selección viva (cada frame): nodos cuyo rect intersecta el
        // bbox del drag.  Reglas con modificadores:
        //   sin modificador → reemplaza
        //   Shift           → ADD a la base
        //   Ctrl            → TOGGLE sobre la base
        const ImVec2 rmin = { std::min(m_rectSelectStart.x, mouse.x),
                              std::min(m_rectSelectStart.y, mouse.y) };
        const ImVec2 rmax = { std::max(m_rectSelectStart.x, mouse.x),
                              std::max(m_rectSelectStart.y, mouse.y) };
        m_drawList->AddRectFilled(rmin, rmax,
                                  IM_COL32(255, 220, 80,  35), 2.f);
        m_drawList->AddRect      (rmin, rmax,
                                  IM_COL32(255, 220, 80, 180), 2.f, 0, 1.5f);

        // Computar selección actual.
        std::unordered_set<int> inRect;
        for (const auto& [id, nf] : m_frameNodeRects) {
            const bool intersects =
                !(nf.maxScr.x < rmin.x || nf.minScr.x > rmax.x ||
                  nf.maxScr.y < rmin.y || nf.minScr.y > rmax.y);
            if (intersects) inRect.insert(id);
        }
        state.selectedNodes = m_rectSelectBase;
        for (int id : inRect) {
            if (io.KeyCtrl) {
                if (state.selectedNodes.count(id))
                    state.selectedNodes.erase(id);
                else
                    state.selectedNodes.insert(id);
            } else {
                state.selectedNodes.insert(id);
            }
        }
    }
    if (m_drag == Drag::Pin && m_drawList) {
        // Preview Bézier del cable en construcción, mismas reglas que
        // drawLink: tangente horizontal sale en la dirección "outward"
        // del pin de origen.  Si hay snap a otro pin (m_hoveredPinAttr
        // se computó arriba con radio amplio), el endpoint del preview
        // es el centro de ese pin y NO la posición del mouse.  Dibujamos
        // además un halo brillante alrededor del pin objetivo para que
        // el usuario vea hacia dónde se va a enganchar.
        auto itOrigin = m_framePins.find(m_dragPinAttr);
        if (itOrigin != m_framePins.end()) {
            const ImVec2 a = itOrigin->second.center;
            ImVec2 b = mouse;
            float dirB = -(itOrigin->second.isOutput ? 1.f : -1.f);
            if (m_hoveredPinAttr != 0) {
                auto itTarget = m_framePins.find(m_hoveredPinAttr);
                if (itTarget != m_framePins.end()) {
                    b    = itTarget->second.center;
                    dirB = itTarget->second.isOutput ? 1.f : -1.f;
                    // Halo amarillo brillante sobre el pin objetivo.
                    m_drawList->AddCircle(
                        b, (kNodePinRadius + 6.f) * state.zoom,
                        IM_COL32(255, 220, 80, 230), 16,
                        2.0f * state.zoom);
                }
            }
            const float dirA = itOrigin->second.isOutput ? 1.f : -1.f;
            const float ctrlDist = std::max(kBezierControlDist * state.zoom,
                                            std::fabs(b.x - a.x) * 0.5f);
            const ImVec2 p1 = { a.x + dirA * ctrlDist, a.y };
            const ImVec2 p2 = { b.x + dirB * ctrlDist, b.y };
            m_drawList->ChannelsSetCurrent(kChBackground);
            m_drawList->AddBezierCubic(a, p1, p2, b,
                                       IM_COL32(255, 220, 80, 220),
                                       2.0f * state.zoom, 0);
            m_drawList->ChannelsSetCurrent(kChForeground);
        }
    }

    // ---- (4) Release ----------------------------------------------------
    if (m_drag != Drag::None &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (m_drag == Drag::Pin) {
            // Si el drag empezó "agarrando" un cable existente
            // (m_dragDetachLinkId > 0), emitimos primero el detach
            // para que NodeCanvas borre el edge previo.  Si después el
            // usuario soltó sobre otro pin válido, NodeCanvas creará
            // un nuevo edge — efecto neto: reconectar.  Si soltó en
            // vacío, solo se borra — efecto neto: desconectar.
            if (m_dragDetachLinkId != 0) {
                m_pendingLinkDetached   = true;
                m_pendingLinkDetachedId = m_dragDetachLinkId;
                m_dragDetachLinkId      = 0;
            }
            if (m_hoveredPinAttr != 0 && m_hoveredPinAttr != m_dragPinAttr) {
                m_pendingLinkCreated     = true;
                m_pendingLinkCreatedFrom = m_dragPinAttr;
                m_pendingLinkCreatedTo   = m_hoveredPinAttr;
            } else if (!m_pendingLinkDetached) {
                // Solo emitimos LinkDropped si NO hubo detach — sin
                // este guard se abriría el popup "Add Node" al
                // desconectar.
                m_pendingLinkDropped     = true;
                m_pendingLinkDroppedFrom = m_dragPinAttr;
            }
            m_dragPinAttr = 0;
        }
        if (m_drag == Drag::Node) m_dragNodeId = 0;
        m_drag = Drag::None;
    }
    if (m_drag == Drag::Pan &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
        m_drag = Drag::None;
    }

    // ---- (5) Wheel zoom centrado en el mouse ---------------------------
    if (io.MouseWheel != 0.f) {
        const float oldZoom = state.zoom;
        const float factor  = std::exp(io.MouseWheel * 0.1f);
        const float newZoom = std::clamp(oldZoom * factor, kZoomMin, kZoomMax);
        if (newZoom != oldZoom) {
            const ImVec2 modelMouse = {
                (mouse.x - m_canvasOrigin.x - state.pan.x) / oldZoom,
                (mouse.y - m_canvasOrigin.y - state.pan.y) / oldZoom,
            };
            state.zoom = newZoom;
            state.pan  = {
                mouse.x - m_canvasOrigin.x - modelMouse.x * newZoom,
                mouse.y - m_canvasOrigin.y - modelMouse.y * newZoom,
            };
        }
        // Consumimos el wheel para que ImGui no haga scroll en paralelo.
        io.MouseWheel = 0.f;
    }
}

}  // namespace scinodes::ui
