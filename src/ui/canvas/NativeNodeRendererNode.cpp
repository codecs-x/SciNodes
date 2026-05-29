#include "NativeNodeRenderer.hpp"
#include "NativeNodeRendererInternal.hpp"

#include "../../core/I18n.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace scinodes::ui {

using namespace scinodes::ui::native_renderer_detail;

// ---------------------------------------------------------------------------
// NativeNodeRendererNode — split (etapa 6K.F).  begin/endNode +
// titleBar + begin/end de los 4 tipos de attribute (Input, Output, Static,
// Param).
// ---------------------------------------------------------------------------

// ===========================================================================
// Nodos
// ===========================================================================
void NativeNodeRenderer::beginNode(int nodeId, CanvasDims dims,
                                   bool hasComment) {
    if (!m_activeState || !m_drawList) return;

    const float zoom = m_activeState->zoom;
    const ImVec2 posModel = m_activeState->positions.count(nodeId)
        ? m_activeState->positions[nodeId]
        : ImVec2{ kDefaultNodeStepX * (float)(nodeId % kDefaultNodeCols),
                  kDefaultNodeStepY * (float)(nodeId % kDefaultNodeCols) };
    const ImVec2 originScr = modelToScreen(posModel);
    const ImVec2 sizeScr   = { dims.w * zoom, dims.h * zoom };

    m_curNode = ActiveNode{
        nodeId,
        originScr,
        sizeScr,
        kNodeTitleHeight * zoom,
        /*cursorY*/ 0.f,
        /*inTitleBar*/ false,
        hasComment
    };
    m_currentNodePinAttrs.clear();

    // Dibujamos la caja AQUÍ (no en endNode) para que todo lo que el
    // nodo emite (título, pines, widgets) quede como una unidad
    // contigua en el canal `kChForeground`.  El orden de iteración de
    // los nodos en NodeCanvas determina el z-order entre nodos
    // superpuestos (último dibujado = al frente).  Es la única forma
    // de garantizar que cuando dos nodos se solapan, ninguna "parte"
    // (título, borde, pin) de un nodo se cuele en medio del otro.
    const ImVec2 maxScr = { originScr.x + sizeScr.x,
                            originScr.y + sizeScr.y };

    // Sombra suave detrás del nodo, desplazada (4, 4) en model units.
    // Replica el efecto de profundidad de los editores nodales modernos
    // (Blender la pinta vía blur; aquí aproximamos con un rect oscuro
    // sin contorno).  Dibujada ANTES del cuerpo, así queda atrás.
    const float shadowOffset = 4.f * zoom;
    m_drawList->AddRectFilled(
        { originScr.x + shadowOffset, originScr.y + shadowOffset },
        { maxScr.x   + shadowOffset, maxScr.y   + shadowOffset },
        IM_COL32(0, 0, 0, 100), 6.f * zoom);

    // Cuerpo del nodo.  El borde lo dibujamos en endNode (al final)
    // para que siempre quede ENCIMA del título bar y de los pines,
    // replicando la convención de Blender donde el outline del nodo
    // es siempre la capa más visible.
    m_drawList->AddRectFilled(originScr, maxScr, kDefaultNodeBg, 6.f * zoom);

    // Posicionar el cursor ImGui dentro del nodo para que los widgets
    // del usuario (Text, DragFloat) caigan dentro de la caja.
    ImGui::SetCursorScreenPos({ originScr.x + kNodePadInner * zoom,
                                originScr.y + kNodePadInner * zoom });
}

void NativeNodeRenderer::endNode() {
    if (!m_activeState || !m_drawList) return;

    const float zoom = m_activeState->zoom;
    const ImVec2 minScr = m_curNode.originScr;
    const ImVec2 maxScr = { minScr.x + m_curNode.sizeScr.x,
                            minScr.y + m_curNode.sizeScr.y };

    // Borde dibujado encima del título y del contenido para definir
    // limpiamente el contorno del nodo (sin que el header lo corte).
    // Selección = borde brillante y más grueso (convención de Blender).
    const bool selected = m_activeState->selectedNodes.count(m_curNode.id) > 0;
    const ImU32 borderColor = selected
        ? IM_COL32(255, 200, 60, 255)
        : kDefaultNodeBorder;
    const float borderThickness = (selected ? 2.6f : 1.2f) * zoom;
    m_drawList->AddRect(minScr, maxScr, borderColor,
                        6.f * zoom, 0, borderThickness);

    // Los pines van DESPUÉS del borde (Blender los dibuja así: el círculo
    // queda por encima de la línea de contorno, no cortado por ella).
    // Pequeño "halo" oscuro de 1 px alrededor de cada pin para que se
    // vea limpio cuando coincide con el borde del nodo.
    for (int attrId : m_currentNodePinAttrs) {
        auto it = m_framePins.find(attrId);
        if (it == m_framePins.end()) continue;
        const PinInfo& p = it->second;
        const float r = kNodePinRadius * zoom;
        // Anillo de fondo para separar visualmente el pin del borde.
        m_drawList->AddCircleFilled(p.center, r + 1.5f * zoom,
                                    IM_COL32(20, 22, 28, 255), 12);
        drawPinShape(m_drawList, p.center, r, p.shape, p.color);
    }
    m_currentNodePinAttrs.clear();

    // Indicador de comentario en la esquina superior derecha del nodo.
    // Pequeño círculo amarillo, alineado con el borde del título.  Sin
    // este puntito los comentarios serían invisibles hasta que el usuario
    // intentara Ctrl+hover sobre cada nodo.
    if (m_curNode.hasComment) {
        constexpr float kCommentBadgeMargin = 6.f;
        constexpr float kCommentBadgeRadius = 3.f;
        const ImVec2 badgeCenter{
            maxScr.x - kCommentBadgeMargin * zoom,
            minScr.y + kCommentBadgeMargin * zoom
        };
        m_drawList->AddCircleFilled(badgeCenter,
                                    kCommentBadgeRadius * zoom,
                                    IM_COL32(255, 210, 80, 255), 10);
    }

    m_frameNodeRects[m_curNode.id] = { minScr, maxScr };

    // Hit-test simple: actualiza m_hoveredNodeId si el mouse está dentro.
    const ImVec2 mouse = ImGui::GetMousePos();
    if (mouse.x >= minScr.x && mouse.x <= maxScr.x &&
        mouse.y >= minScr.y && mouse.y <= maxScr.y) {
        m_hoveredNodeId = m_curNode.id;
    }

    m_curNode = ActiveNode{};
}

void NativeNodeRenderer::beginNodeTitleBar() {
    if (!m_drawList) return;
    m_curNode.inTitleBar = true;

    const ImU32 titleColor = colorOr(ColorKey::TitleBar, kDefaultTitleBg);
    const float zoom = m_activeState ? m_activeState->zoom : 1.f;
    // El título se dibuja al ancho completo del nodo.  El borde del nodo
    // se dibuja en endNode (después del título), así que la línea de
    // contorno queda ENCIMA y los píxeles del título no la cortan.
    const ImVec2 minScr = m_curNode.originScr;
    const ImVec2 maxScr = { minScr.x + m_curNode.sizeScr.x,
                            minScr.y + m_curNode.titleH };
    m_drawList->AddRectFilled(minScr, maxScr, titleColor,
                              6.f * zoom,
                              ImDrawFlags_RoundCornersTop);

    // Cursor ImGui dentro de la bandita.
    ImGui::SetCursorScreenPos({ minScr.x + kNodePadInner * zoom,
                                minScr.y + 4.f      * zoom });
}

void NativeNodeRenderer::endNodeTitleBar() {
    // Avanzamos titleH + kNodePadInner: el padding superior del cuerpo
    // separa la primera fila de atributos del título.  Coincide con
    // computeNodeDimensions que reserva titleH + 2·padInner + rows·rowH;
    // sin esto el primer pin quedaba pegado al borde inferior del
    // título y los nodos parecían "entrelazados".
    if (!m_activeState) return;
    const float zoom = m_activeState->zoom;
    m_curNode.cursorY += m_curNode.titleH + kNodePadInner * zoom;
    ImGui::SetCursorScreenPos({ m_curNode.originScr.x + kNodePadInner * zoom,
                                m_curNode.originScr.y + m_curNode.cursorY });
    m_curNode.inTitleBar = false;
}

// ===========================================================================
// Atributos
// ===========================================================================
void NativeNodeRenderer::beginInputAttribute(int attrId, PortShape shape) {
    if (!m_activeState) return;
    const float zoom = m_activeState->zoom;
    const float rowY = m_curNode.cursorY + kNodeRowHeight * zoom * 0.5f;
    const ImVec2 pinCenter = { m_curNode.originScr.x,
                               m_curNode.originScr.y + rowY };

    const ImU32 pinColor = colorOr(ColorKey::Pin, kDefaultPin);
    // El pin se DIBUJA en endNode, después del borde, para que la línea
    // de contorno del nodo no lo corte.  Aquí solo registramos su info.
    m_framePins[attrId] = PinInfo{ pinCenter, /*isOutput*/ false, shape, pinColor };
    m_currentNodePinAttrs.push_back(attrId);

    // Texto del puerto a la derecha del pin (input row).  En model units.
    ImGui::SetCursorScreenPos({ m_curNode.originScr.x + (kNodePinRadius + kNodeRowGap) * zoom,
                                m_curNode.originScr.y + m_curNode.cursorY });

    m_curAttr = ActiveAttr{ attrId, false, false, m_curNode.cursorY,
                            pinCenter, shape, pinColor };
}

void NativeNodeRenderer::endInputAttribute() {
    if (!m_activeState) return;
    m_curNode.cursorY += kNodeRowHeight * m_activeState->zoom;
    ImGui::SetCursorScreenPos({ m_curNode.originScr.x + kNodePadInner * m_activeState->zoom,
                                m_curNode.originScr.y + m_curNode.cursorY });
    m_curAttr = ActiveAttr{};
}

void NativeNodeRenderer::beginOutputAttribute(int attrId, PortShape shape,
                                              int labelChars) {
    if (!m_activeState) return;
    const float zoom = m_activeState->zoom;
    const float rowY = m_curNode.cursorY + kNodeRowHeight * zoom * 0.5f;
    const ImVec2 pinCenter = { m_curNode.originScr.x + m_curNode.sizeScr.x,
                               m_curNode.originScr.y + rowY };

    const ImU32 pinColor = colorOr(ColorKey::Pin, kDefaultPin);
    // Dibujo diferido: ver beginInputAttribute.
    m_framePins[attrId] = PinInfo{ pinCenter, /*isOutput*/ true, shape, pinColor };
    m_currentNodePinAttrs.push_back(attrId);

    // Texto del puerto cerca del pin derecho.  Reservamos el ancho real
    // del label que el caller informó (etapa 6I.F.2) — antes hardcoded
    // a 5 chars y "out [rad/s]" (11) se salía del nodo.  El caller
    // mide su texto y pasa `labelChars`; default 5 cubre "out N".
    const float labelW = kNodeCharWidth * static_cast<float>(labelChars);
    const float xText  = m_curNode.originScr.x + m_curNode.sizeScr.x
                       - (labelW + kNodeRowGap + kNodePinRadius) * zoom;
    ImGui::SetCursorScreenPos({ xText,
                                m_curNode.originScr.y + m_curNode.cursorY });

    m_curAttr = ActiveAttr{ attrId, true, false, m_curNode.cursorY,
                            pinCenter, shape, pinColor };
}

void NativeNodeRenderer::endOutputAttribute() {
    if (!m_activeState) return;
    m_curNode.cursorY += kNodeRowHeight * m_activeState->zoom;
    ImGui::SetCursorScreenPos({ m_curNode.originScr.x + kNodePadInner * m_activeState->zoom,
                                m_curNode.originScr.y + m_curNode.cursorY });
    m_curAttr = ActiveAttr{};
}

void NativeNodeRenderer::beginStaticAttribute(int attrId) {
    if (!m_activeState) return;
    ImGui::SetCursorScreenPos({ m_curNode.originScr.x + kNodePadInner * m_activeState->zoom,
                                m_curNode.originScr.y + m_curNode.cursorY });
    m_curAttr = ActiveAttr{ attrId, false, /*isStatic*/ true, m_curNode.cursorY,
                            {0,0}, PortShape::CircleFilled, 0 };
}

void NativeNodeRenderer::endStaticAttribute() {
    if (!m_activeState) return;
    // Los static attrs suelen contener DragFloat, que ocupa una fila;
    // avanzamos el cursor de manera consistente con input/output.
    m_curNode.cursorY += kNodeRowHeight * m_activeState->zoom;
    ImGui::SetCursorScreenPos({ m_curNode.originScr.x + kNodePadInner * m_activeState->zoom,
                                m_curNode.originScr.y + m_curNode.cursorY });
    m_curAttr = ActiveAttr{};
}

void NativeNodeRenderer::beginParamAttribute(int attrId, PortShape shape) {
    if (!m_activeState) return;
    // Igual que beginInputAttribute en cuanto a pin (lado izquierdo del
    // nodo, hit-test pleno), pero NO emite el texto del puerto: el
    // caller (NodeCanvas) dibuja después el row "label + DragFloat +
    // unidad" del parámetro.  Posicionamos el cursor justo después del
    // pin para que el row tenga el espacio reducido correcto.
    const float zoom = m_activeState->zoom;
    const float rowY = m_curNode.cursorY + kNodeRowHeight * zoom * 0.5f;
    const ImVec2 pinCenter = { m_curNode.originScr.x,
                               m_curNode.originScr.y + rowY };
    const ImU32 pinColor = colorOr(ColorKey::Pin, kDefaultPin);
    m_framePins[attrId] = PinInfo{ pinCenter, /*isOutput*/ false, shape, pinColor };
    m_currentNodePinAttrs.push_back(attrId);
    ImGui::SetCursorScreenPos({ m_curNode.originScr.x +
                                (kNodePinRadius + kNodeRowGap) * zoom,
                                m_curNode.originScr.y + m_curNode.cursorY });
    m_curAttr = ActiveAttr{ attrId, false, false, m_curNode.cursorY,
                            pinCenter, shape, pinColor };
}

void NativeNodeRenderer::endParamAttribute() {
    if (!m_activeState) return;
    m_curNode.cursorY += kNodeRowHeight * m_activeState->zoom;
    ImGui::SetCursorScreenPos({ m_curNode.originScr.x + kNodePadInner * m_activeState->zoom,
                                m_curNode.originScr.y + m_curNode.cursorY });
    m_curAttr = ActiveAttr{};
}

}  // namespace scinodes::ui
