#include "NativeNodeRenderer.hpp"

#include "../../core/I18n.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace scinodes::ui {

// ===========================================================================
// Constantes locales (los tamaños del nodo vienen del modelo vía
// Canvas.hpp::kNode*; aquí solo colores y umbrales de interacción)
// ===========================================================================
namespace {

// Colores por defecto cuando el color stack está vacío.
// El cuerpo del nodo es 100% opaco para que se integre con los widgets
// internos (DragFloat / InputText) que tampoco son transparentes — sin
// esto las "capas" del nodo se ven entrelazadas con el fondo, los edges
// y las celdas de los valores numéricos.
constexpr ImU32 kDefaultNodeBg     = IM_COL32( 40,  44,  52, 255);
constexpr ImU32 kDefaultNodeBorder = IM_COL32(180, 180, 200, 255);
constexpr ImU32 kDefaultTitleBg    = IM_COL32( 70,  90, 160, 255);
constexpr ImU32 kDefaultPin        = IM_COL32(200, 200, 200, 255);
constexpr ImU32 kDefaultLink       = IM_COL32(200, 200, 200, 220);
constexpr ImU32 kGridLine          = IM_COL32(50, 55, 65, 180);

constexpr float kZoomMin = 0.25f;
constexpr float kZoomMax = 3.0f;

// Distancia (en píxeles modelo, multiplicada por zoom al dibujar) de
// los puntos de control de la Bézier de cada cable — define cuánto se
// "ondula" la curva.  Hereda escala con `state.zoom` para conservar
// proporciones al ampliar/reducir.  El cálculo real es
// `std::max(kBezierControlDist * zoom, abs(x_b - x_a) * 0.5f)` para
// que cables muy separados horizontalmente tengan más arc.
constexpr float kBezierControlDist = 40.f;

// Radio (en píxeles pantalla, escalado por zoom) del hit-test "ampliado"
// para snap al pin más cercano cuando el usuario está arrastrando
// (Drag::Pin).  Sin el ampliado, el usuario tenía que aterrizar pixel-
// perfecto sobre el pin objetivo — flojo.  Convención de Blender /
// Unreal Material Editor.
constexpr float kPinSnapRadiusPx = 30.f;

// Para nodos cuya posición no está en `state.positions`: cae a una
// rejilla diagonal (paso X×Y) en una pseudo-fila ciclando cada
// kDefaultNodeCols nodos.  Solo aparece si el caller no llamó
// `setNodePosition` antes de drawNode.
constexpr float kDefaultNodeStepX = 100.f;
constexpr float kDefaultNodeStepY =  60.f;
constexpr int   kDefaultNodeCols  =   5;

// Canales del drawList — sólo dos, para que cada nodo se dibuje como una
// unidad contigua sin interlavarse con otros nodos (bug de Fase 2.0 al
// usar 4 canales: el título de un nodo terminaba pintado encima de la
// caja del nodo vecino).  Channel 0 acumula grid + edges (todo lo de
// fondo); channel 1 acumula los nodos, en orden de iteración (último
// nodo dibujado = encima).
constexpr int kChBackground = 0;   // grid + edges
constexpr int kChForeground = 1;   // node boxes + titles + pins + widgets
constexpr int kNumChannels  = 2;

void drawPinShape(ImDrawList* dl, ImVec2 c, float r,
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

}  // namespace

// ===========================================================================
// Lifecycle
// ===========================================================================
void NativeNodeRenderer::init()     { /* nada global por ahora */ }
void NativeNodeRenderer::shutdown() { m_states.clear(); m_savedKeys.clear(); }

// ===========================================================================
// Color stack helper
// ===========================================================================
ImU32 NativeNodeRenderer::colorOr(ColorKey key, ImU32 fallback) const {
    for (auto it = m_colorStack.rbegin(); it != m_colorStack.rend(); ++it) {
        if (it->key == key) return it->color;
    }
    return fallback;
}

void NativeNodeRenderer::pushColor(ColorKey key, unsigned int rgba) {
    m_colorStack.push_back({ key, rgba });
}
void NativeNodeRenderer::popColor(int count) {
    for (int i = 0; i < count && !m_colorStack.empty(); ++i)
        m_colorStack.pop_back();
}

// ===========================================================================
// Transformación model ↔ screen
// ===========================================================================
ImVec2 NativeNodeRenderer::modelToScreen(ImVec2 m) const {
    const float z = m_activeState ? m_activeState->zoom : 1.f;
    const ImVec2 p = m_activeState ? m_activeState->pan  : ImVec2{0,0};
    return { m_canvasOrigin.x + m.x * z + p.x,
             m_canvasOrigin.y + m.y * z + p.y };
}

ImVec2 NativeNodeRenderer::screenToModel(ImVec2 s) const {
    const float z = m_activeState ? m_activeState->zoom : 1.f;
    const ImVec2 p = m_activeState ? m_activeState->pan  : ImVec2{0,0};
    return { (s.x - m_canvasOrigin.x - p.x) / z,
             (s.y - m_canvasOrigin.y - p.y) / z };
}

// ===========================================================================
// Canvas lifecycle
// ===========================================================================
void NativeNodeRenderer::beginCanvas(const std::string& contextKey) {
    m_activeKey   = contextKey;
    m_activeState = &m_states[contextKey];

    m_framePins.clear();
    m_frameNodeRects.clear();
    m_frameEdgesByInput.clear();
    m_pendingLinkCreated  = false;
    m_pendingLinkDropped  = false;
    m_pendingLinkDetached = false;
    m_hoveredNodeId       = 0;
    m_hoveredLinkId       = 0;

    // BeginChild aísla el canvas como una sub-ventana propia (sin
    // scrollbar — el zoom/pan reemplaza al scroll).  Sin esto, las
    // posiciones absolutas que escribo vía SetCursorScreenPos hacen
    // crecer el contenido del panel hospedante y se activa el scroll
    // vertical de ImGui en paralelo con mi zoom.  Inspiración: la
    // región del editor de nodos en Blender es un área aparte con su
    // propio View2D \cite{hollister_core_2021}.
    ImGui::BeginChild("##nativecanvas", ImVec2(0, 0), false,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse |
                      ImGuiWindowFlags_NoMove);

    m_canvasOrigin = ImGui::GetCursorScreenPos();
    m_canvasSize   = ImGui::GetContentRegionAvail();
    if (m_canvasSize.x < 1.f) m_canvasSize.x = 1.f;
    if (m_canvasSize.y < 1.f) m_canvasSize.y = 1.f;

    // Reservamos el área entera con un Dummy ANTES de empezar a
    // mover el cursor a posiciones absolutas vía SetCursorScreenPos.
    // Sin esto, ImGui falla en EndChild con la aserción
    // ErrorCheckUsingSetCursorPosToExtendParentBoundaries: cada
    // SetCursorScreenPos extiende los límites del hijo sin que un
    // item los confirme, y el chequeo final de ImGui aborta.
    // Tras el Dummy, cursor = canvasOrigin + (0, canvasSize.y) — el
    // hijo conoce su tamaño y los SetCursorScreenPos siguientes
    // mueven el cursor hacia arriba sin extender nada nuevo.
    ImGui::Dummy(m_canvasSize);
    ImGui::SetCursorScreenPos(m_canvasOrigin);

    m_drawList = ImGui::GetWindowDrawList();
    m_drawList->PushClipRect(m_canvasOrigin,
                             { m_canvasOrigin.x + m_canvasSize.x,
                               m_canvasOrigin.y + m_canvasSize.y },
                             true);
    m_drawList->ChannelsSplit(kNumChannels);

    // 1. Grilla de fondo (canal 0).
    m_drawList->ChannelsSetCurrent(kChBackground);
    const float gridSpacing = 32.f * m_activeState->zoom;
    if (gridSpacing > 6.f) {
        const float ox = std::fmod(m_activeState->pan.x, gridSpacing);
        const float oy = std::fmod(m_activeState->pan.y, gridSpacing);
        for (float x = ox; x < m_canvasSize.x; x += gridSpacing) {
            m_drawList->AddLine(
                { m_canvasOrigin.x + x, m_canvasOrigin.y },
                { m_canvasOrigin.x + x, m_canvasOrigin.y + m_canvasSize.y },
                kGridLine);
        }
        for (float y = oy; y < m_canvasSize.y; y += gridSpacing) {
            m_drawList->AddLine(
                { m_canvasOrigin.x,                  m_canvasOrigin.y + y },
                { m_canvasOrigin.x + m_canvasSize.x, m_canvasOrigin.y + y },
                kGridLine);
        }
    }

    // 2. Contenido por defecto al canal foreground.
    m_drawList->ChannelsSetCurrent(kChForeground);

    // Zoom WYSIWYG: escalamos fuente y padding/spacing de ImGui durante
    // todo el contenido del canvas.  En Blender la región del editor de
    // nodos hace lo equivalente vía View2D + glScale — el resultado es
    // que todo (cajas, texto, widgets) se ve a la misma proporción a
    // cualquier zoom \cite{hollister_core_2021}.  Aquí lo replicamos
    // con la API de ImGui:
    //   - SetWindowFontScale → escala el bitmap del font del child.
    //   - FramePadding / ItemSpacing escalados → widgets más grandes
    //     o más pequeños en proporción.
    // Pop simétrico al inicio de endCanvas para mantener el resto de
    // la GUI a escala 1.0.
    const float z = m_activeState->zoom;
    ImGui::SetWindowFontScale(z);
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(style.FramePadding.x * z,
                               style.FramePadding.y * z));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(style.ItemSpacing.x * z,
                               style.ItemSpacing.y * z));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing,
                        ImVec2(style.ItemInnerSpacing.x * z,
                               style.ItemInnerSpacing.y * z));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,
                        style.FrameRounding * z);
    // PushItemWidth dicta el ancho por defecto de los widgets (DragFloat,
    // InputText, …) dentro del canvas.  Es la pieza que completaba la
    // gramática: kNodeWidgetWidth en model units · zoom = ancho en
    // pantalla.  Sin esto, el SetNextItemWidth(86) hardcoded de
    // NodeCanvas hacía que el widget no escalara.
    ImGui::PushItemWidth(kNodeWidgetWidth * z);
}

void NativeNodeRenderer::endCanvas() {
    // Pop simétrico de los style vars / font scale / item width empujados
    // en beginCanvas.  Hay que hacerlo ANTES del handleZoomAndPan y del
    // anchor final (Dummy) para que cualquier widget que ImGui dibuje
    // después del canvas vuelva a escala 1.0.
    ImGui::PopItemWidth();
    ImGui::PopStyleVar(4);
    ImGui::SetWindowFontScale(1.0f);

    handleInteraction();

    if (m_drawList) {
        m_drawList->ChannelsMerge();
        m_drawList->PopClipRect();
        m_drawList = nullptr;
    }

    // Anclamos el cursor dentro de los bounds del child con un item
    // explícito.  Sin esto, la última `SetCursorScreenPos` que hicimos
    // dentro de un nodo (en endAttribute, posicionando la "siguiente
    // fila") queda sin commit y la verificación
    // ErrorCheckUsingSetCursorPosToExtendParentBoundaries de ImGui
    // aborta en EndChild.  El Dummy(1,1) en la esquina superior izq.
    // del canvas (siempre dentro de los bounds ya reservados por el
    // Dummy(canvasSize) inicial) basta para satisfacer el chequeo.
    ImGui::SetCursorScreenPos(m_canvasOrigin);
    ImGui::Dummy(ImVec2(1, 1));

    ImGui::EndChild();
}

void NativeNodeRenderer::resetCanvasView() {
    if (!m_activeState) return;
    m_activeState->pan  = {0, 0};
    m_activeState->zoom = 1.f;
}

void NativeNodeRenderer::frameToBox(CanvasPos modelMin, CanvasPos modelMax,
                                    float viewportW, float viewportH) {
    if (!m_activeState) return;
    // viewport(W,H) ≤ 0 → usar el tamaño del canvas actual.  Útil
    // cuando frameToBox se invoca fuera del bloque de dibujo
    // (p. ej. tras autoLayout) donde el caller no tiene acceso a
    // ImGui::GetContentRegionAvail correcto.
    if (viewportW <= 0.f) viewportW = m_canvasSize.x;
    if (viewportH <= 0.f) viewportH = m_canvasSize.y;
    const float spanX = std::max(1.f, modelMax.x - modelMin.x);
    const float spanY = std::max(1.f, modelMax.y - modelMin.y);
    // 80 px de aire alrededor del bbox para que los nodos no toquen
    // los bordes del viewport.
    const float zX = viewportW / (spanX + 80.f);
    const float zY = viewportH / (spanY + 80.f);
    m_activeState->zoom = std::clamp(std::min(zX, zY), kZoomMin, kZoomMax);
    const float cx = 0.5f * (modelMin.x + modelMax.x);
    const float cy = 0.5f * (modelMin.y + modelMax.y);
    m_activeState->pan = { 0.5f * viewportW - cx * m_activeState->zoom,
                           0.5f * viewportH - cy * m_activeState->zoom };
}

void NativeNodeRenderer::pushCanvas(const std::string& contextKey) {
    m_savedKeys.push_back(m_activeKey);
    m_activeKey   = contextKey;
    m_activeState = &m_states[contextKey];
}

void NativeNodeRenderer::popCanvas() {
    if (m_savedKeys.empty()) return;
    m_activeKey = m_savedKeys.back();
    m_savedKeys.pop_back();
    auto it = m_states.find(m_activeKey);
    m_activeState = (it != m_states.end()) ? &it->second : nullptr;
}

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
    const float ctrlDist = std::max(kBezierControlDist * m_activeState->zoom,
                                    std::fabs(b.x - a.x) * 0.5f);
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
void NativeNodeRenderer::setNodePosition(int nodeId, CanvasPos pos,
                                         CoordSpace space) {
    if (!m_activeState) return;
    if (space == CoordSpace::Editor) {
        m_activeState->positions[nodeId] = ImVec2{ pos.x, pos.y };
    } else {
        // CoordSpace::Screen → convertir a editor space restando el pan
        // y dividiendo por el zoom.  El canvasOrigin no se conoce fuera
        // del frame de dibujo; usamos solo pan + zoom (asumiendo que el
        // caller pasa coordenadas relativas al origin actual, que es
        // como las maneja el resto del código durante el frame).
        const float z = m_activeState->zoom;
        const ImVec2 p = m_activeState->pan;
        const ImVec2 editor = { (pos.x - m_canvasOrigin.x - p.x) / z,
                                (pos.y - m_canvasOrigin.y - p.y) / z };
        m_activeState->positions[nodeId] = editor;
    }
}

CanvasPos NativeNodeRenderer::getNodePosition(int nodeId, CoordSpace space) const {
    if (!m_activeState) return { 0.f, 0.f };
    auto it = m_activeState->positions.find(nodeId);
    if (it == m_activeState->positions.end()) return { 0.f, 0.f };
    if (space == CoordSpace::Editor) {
        return { it->second.x, it->second.y };
    }
    // Screen: aplicar pan + zoom + origin.
    const float z = m_activeState->zoom;
    const ImVec2 p = m_activeState->pan;
    return { m_canvasOrigin.x + it->second.x * z + p.x,
             m_canvasOrigin.y + it->second.y * z + p.y };
}

// ===========================================================================
// Selección
// ===========================================================================
void NativeNodeRenderer::clearNodeSelection() {
    if (m_activeState) m_activeState->selectedNodes.clear();
}
void NativeNodeRenderer::selectNode(int nodeId) {
    if (m_activeState) m_activeState->selectedNodes.insert(nodeId);
}
int NativeNodeRenderer::selectedNodeCount() const {
    return m_activeState ? (int)m_activeState->selectedNodes.size() : 0;
}
void NativeNodeRenderer::getSelectedNodes(std::vector<int>& outIds) const {
    outIds.clear();
    if (!m_activeState) return;
    outIds.reserve(m_activeState->selectedNodes.size());
    for (int id : m_activeState->selectedNodes) outIds.push_back(id);
}
int NativeNodeRenderer::selectedLinkCount() const {
    return m_activeState ? (int)m_activeState->selectedLinks.size() : 0;
}
void NativeNodeRenderer::getSelectedLinks(std::vector<int>& outIds) const {
    outIds.clear();
    if (!m_activeState) return;
    outIds.reserve(m_activeState->selectedLinks.size());
    for (int id : m_activeState->selectedLinks) outIds.push_back(id);
}

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
