#include "NativeNodeRenderer.hpp"
#include "NativeNodeRendererInternal.hpp"

#include "../../core/I18n.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace scinodes::ui {

// Constantes + drawPinShape viven en NativeNodeRendererInternal.hpp
// (etapa 6K.F) — compartidos con los .cpp del split.
using namespace scinodes::ui::native_renderer_detail;

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
                                    float viewportW, float viewportH,
                                    float maxZoom) {
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
    // Cap configurable — etapa 6M.c.  Para "encuadrar un solo nodo"
    // (find + E) un cap de 1.5× evita que un nodo chico en un viewport
    // grande aparezca exageradamente ampliado.  kZoomMax (3.0) se usa
    // para "framear todo el grafo" (auto-layout), donde el span es
    // grande y el cap rara vez se activa.
    const float capLimit = (maxZoom > 0.f) ? maxZoom : kZoomMax;
    m_activeState->zoom = std::clamp(std::min(zX, zY), kZoomMin, capLimit);
    const float cx = 0.5f * (modelMin.x + modelMax.x);
    const float cy = 0.5f * (modelMin.y + modelMax.y);
    m_activeState->pan = { 0.5f * viewportW - cx * m_activeState->zoom,
                           0.5f * viewportH - cy * m_activeState->zoom };
}

void NativeNodeRenderer::centerOn(CanvasPos modelPoint) {
    if (!m_activeState) return;
    // Preserva el zoom del usuario — sólo paneamos para que `modelPoint`
    // caiga en el centro del viewport.  Forma despejada de la transform
    // modelToScreen:  screen = canvasOrigin + zoom·model + pan
    // queremos  screen = canvasOrigin + viewport/2  →
    //   pan = viewport/2 − zoom·model
    const float z = m_activeState->zoom;
    m_activeState->pan = { m_canvasSize.x * 0.5f - modelPoint.x * z,
                           m_canvasSize.y * 0.5f - modelPoint.y * z };
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

}  // namespace scinodes::ui

