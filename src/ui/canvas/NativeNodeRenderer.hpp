#pragma once
#include "Canvas.hpp"

#include <imgui.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace scinodes::ui {

// =============================================================================
// NativeNodeRenderer — implementación propia de INodeRenderer sobre
// ImDrawList con zoom WYSIWYG.  Implementación única (post-retiro de
// imnodes); ver doc/native_renderer_design.md para la motivación
// histórica y la frontera completa.
//
// Capacidades: dibujo de cajas + pines + bezier de cables con
// scaling WYSIWYG; drag + selección rectangular + selección de
// cables; pan + zoom; reconexión por drag-detach desde input ya
// conectado; hover detection nodo/cable/pin con snap radius en Drag::Pin.
//
// Reglas duras:
//   1. Ningún tipo de ImGui cruza la frontera de INodeRenderer.  Las
//      posiciones se pasan como CanvasPos del proyecto.
//   2. Una CanvasState por path-key (top-level "/", anidado "/5/", …)
//      preserva zoom/pan/selección al navegar SubGraphs.
//   3. setNodePosition con CoordSpace::Screen aplica la inversa del
//      pan+zoom para guardar en editor space; la fuente de verdad
//      siempre es editor space.
// =============================================================================
class NativeNodeRenderer : public INodeRenderer {
public:
    NativeNodeRenderer()  = default;
    ~NativeNodeRenderer() override = default;

    NativeNodeRenderer(const NativeNodeRenderer&)            = delete;
    NativeNodeRenderer& operator=(const NativeNodeRenderer&) = delete;

    // INodeRenderer ------------------------------------------------------
    void init() override;
    void shutdown() override;

    void beginCanvas(const std::string& contextKey) override;
    void endCanvas() override;
    void resetCanvasView() override;
    void frameToBox(CanvasPos modelMin, CanvasPos modelMax,
                    float viewportW, float viewportH) override;
    void pushCanvas(const std::string& contextKey) override;
    void popCanvas() override;

    void beginNode(int nodeId, CanvasDims dims,
                   bool hasComment = false) override;
    void endNode() override;
    void beginNodeTitleBar() override;
    void endNodeTitleBar() override;

    void beginInputAttribute (int attrId, PortShape shape) override;
    void endInputAttribute   () override;
    void beginOutputAttribute(int attrId, PortShape shape,
                              int labelChars = 5) override;
    void endOutputAttribute  () override;
    void beginStaticAttribute(int attrId) override;
    void endStaticAttribute  () override;
    void beginParamAttribute (int attrId, PortShape shape) override;
    void endParamAttribute   () override;

    void drawLink(int linkId, int fromAttrId, int toAttrId) override;

    void setNodePosition(int nodeId, CanvasPos pos,
                         CoordSpace space = CoordSpace::Screen) override;
    CanvasPos getNodePosition(int nodeId,
                              CoordSpace space = CoordSpace::Screen) const override;

    void clearNodeSelection() override;
    void selectNode(int nodeId) override;
    int  selectedNodeCount() const override;
    void getSelectedNodes(std::vector<int>& outIds) const override;
    int  selectedLinkCount() const override;
    void getSelectedLinks(std::vector<int>& outIds) const override;

    bool pollLinkCreated(LinkCreatedEvent& out) override;
    bool pollLinkDropped(int& outStartAttrId) override;
    bool pollLinkDetached(int& outLinkId) override;
    bool isLinkHovered(int& outLinkId) override;
    bool isNodeHovered(int& outNodeId) override;

    void pushColor(ColorKey key, unsigned int rgba) override;
    void popColor(int count = 1) override;

private:
    // ---- Estado persistente por canvas (path-key) -----------------------
    struct CanvasState {
        float                            zoom = 1.0f;     // [0.1, 4.0]
        ImVec2                           pan  = {0, 0};   // píxeles, screen
        std::unordered_map<int, ImVec2>  positions;       // editor space
        std::unordered_set<int>          selectedNodes;
        std::unordered_set<int>          selectedLinks;
    };
    std::unordered_map<std::string, CanvasState> m_states;
    // Espejo del contexto activo + stack para push/popCanvas.
    std::string                     m_activeKey;
    CanvasState*                    m_activeState = nullptr;
    std::vector<std::string>        m_savedKeys;

    // ---- Estado del frame (limpio en cada beginCanvas) ------------------
    struct PinInfo {
        ImVec2    center   = {0, 0};
        bool      isOutput = false;
        PortShape shape    = PortShape::CircleFilled;
        ImU32     color    = 0xFFFFFFFFu;
    };
    std::unordered_map<int, PinInfo> m_framePins;
    // Pines del nodo activo (entre beginNode/endNode).  Se dibujan en
    // endNode, DESPUÉS del borde, para que el outline del nodo no corte
    // los círculos de los puertos (convención de Blender: el pin queda
    // por encima del borde con un margen suave alrededor).
    std::vector<int> m_currentNodePinAttrs;

    struct NodeFrameInfo {
        ImVec2 minScr = {0, 0};
        ImVec2 maxScr = {0, 0};
    };
    std::unordered_map<int, NodeFrameInfo> m_frameNodeRects;

    // Origen del canvas en pantalla y tamaño visible (calculados en
    // beginCanvas a partir de la ventana ImGui hospedante).
    ImVec2 m_canvasOrigin = {0, 0};
    ImVec2 m_canvasSize   = {0, 0};
    ImDrawList* m_drawList = nullptr;

    // ---- Nodo actualmente abierto (entre beginNode/endNode) -------------
    struct ActiveNode {
        int    id          = 0;
        ImVec2 originScr   = {0, 0};   // esquina superior izquierda en screen
        ImVec2 sizeScr     = {0, 0};   // dimensiones escaladas
        float  titleH      = 0.f;      // altura de bandita superior * zoom
        float  cursorY     = 0.f;      // píxeles desde origin, próxima fila
        bool   inTitleBar  = false;
        bool   hasComment  = false;    // dibujar puntito en esquina sup-der
    };
    ActiveNode m_curNode;

    // ---- Atributo actualmente abierto (entre beginAttr/endAttr) ---------
    struct ActiveAttr {
        int       attrId   = 0;
        bool      isOutput = false;
        bool      isStatic = false;
        float     startY   = 0.f;      // píxeles desde origin del nodo
        ImVec2    pinCenter = {0, 0};  // screen
        PortShape shape    = PortShape::CircleFilled;
        ImU32     pinColor = 0xFFFFFFFFu;
    };
    ActiveAttr m_curAttr;

    // ---- Color stack (overrides aplicados por NodeCanvas) ---------------
    struct ColorOverride {
        ColorKey key;
        ImU32    color;
    };
    std::vector<ColorOverride> m_colorStack;
    ImU32 colorOr(ColorKey key, ImU32 fallback) const;

    // ---- Eventos pendientes (consumidos por poll*) ----------------------
    bool m_pendingLinkCreated      = false;
    int  m_pendingLinkCreatedFrom  = 0;
    int  m_pendingLinkCreatedTo    = 0;
    bool m_pendingLinkDropped      = false;
    int  m_pendingLinkDroppedFrom  = 0;
    // Cable que el usuario "desenchufó" arrastrando desde un input
    // pin conectado.  El modelo (NodeCanvas) debe borrar este edge.
    bool m_pendingLinkDetached     = false;
    int  m_pendingLinkDetachedId   = 0;
    // Para el flujo de detach: si el drag arrancó en un input pin con
    // edge previo, conservamos el id del edge para emitirlo al soltar.
    int  m_dragDetachLinkId        = 0;
    int  m_hoveredNodeId           = 0;
    int  m_hoveredLinkId           = 0;
    int  m_hoveredPinAttr          = 0;
    // Edges del frame indexadas por su input-pin attrId — permite
    // saber instantáneamente cuál es el output del otro extremo al
    // hacer click sobre un input conectado.  Valor: {linkId, fromAttr}.
    std::unordered_map<int, std::pair<int,int>> m_frameEdgesByInput;

    // ---- Estado del drag (Fase 2-3) --------------------------------------
    enum class Drag { None, Node, Pin, Pan, RectSelect };
    Drag   m_drag = Drag::None;
    int    m_dragNodeId      = 0;     // id del nodo "ancla" en Drag::Node
    int    m_dragPinAttr     = 0;     // attrId del pin de origen en Drag::Pin
    ImVec2 m_rectSelectStart = {0,0}; // origen del rectángulo de selección
    // Snapshot de la selección al iniciar un rect-select con Shift/Ctrl —
    // permite componer "selección previa ⊕ nodos del rect" sin perder
    // la base entre frames.
    std::unordered_set<int> m_rectSelectBase;

    // ---- Transformación model ↔ screen ----------------------------------
    ImVec2 modelToScreen(ImVec2 m) const;
    ImVec2 screenToModel(ImVec2 s) const;

    // (las dimensiones del nodo ya no se estiman aquí: NodeCanvas las
    // calcula vía computeNodeDimensions y las pasa a beginNode)

    // ---- Interacciones del frame (zoom/pan/drag/links) ------------------
    void handleInteraction();
};

}  // namespace scinodes::ui
