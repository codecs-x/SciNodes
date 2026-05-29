#pragma once
#include "../../core/NodeGraph.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace scinodes::ui {

// =============================================================================
// Capa de traducción sobre librerías de edición de nodos (hoy: imnodes).
//
// El propósito de Canvas/INodeRenderer es que el resto del código hable EL
// idioma del proyecto (Canvas, CanvasPos, CanvasDims, autoLayout…) y que la
// API de la librería externa quede aislada detrás del renderer.  Patrón
// anti-corruption layer (Eric Evans, DDD), aplicado con la misma intención
// que IComputeBackend con Scilab.
//
// Reglas duras:
//   1. Posiciones y dimensiones de los nodos viven en este modelo, no en la
//      librería.  El renderer las consume y reporta cambios (drag), pero la
//      fuente de verdad es el Canvas.
//   2. `Canvas::autoLayout()` es función pura sobre el modelo: testeable
//      headless, determinista, sin necesidad de ventana ni de la librería.
//   3. La estructura es recursiva: cada SubGraph del NodeGraph tiene un
//      SubCanvas asociado.  Espejo exacto del modelo, igual que la gramática
//      del codegen.
// =============================================================================

struct CanvasPos  { float x = 0.f; float y = 0.f; };
struct CanvasDims { float w = 0.f; float h = 0.f; };

// =============================================================================
// Constantes de layout — única fuente de verdad para el tamaño de los nodos.
//
// Estas constantes las consume tanto la función `computeNodeDimensions`
// (cálculo determinista del tamaño del nodo desde el modelo) como el
// renderer concreto al dibujar (avanzar el cursor por fila, colocar la
// bandita, etc.).  Mantenerlas en un solo sitio evita el bug que
// vivimos en Fase 1.0 del NativeNodeRenderer: el renderer avanzaba 22 px
// por fila pero `Canvas::dimensionsOf` calculaba la altura con 14/18 px;
// la caja quedaba más chica que el contenido y el texto se salía.
//
// Inspiración directa: en Blender el struct `bNode` guarda `width`/
// `height` y la pasada de layout `node_update_basis()` deja las
// posiciones de cada socket en `socket->runtime->location`.  El
// renderer no inventa nada — lee lo que el modelo ya dejó listo
// \cite{hollister_core_2021}.  En SciNodes la información es lo
// suficientemente simple (tipo + ports + params del registry) como
// para calcularla bajo demanda en una función pura, sin necesidad de
// un cache `runtime` en la NodeInstance.
// =============================================================================
constexpr float kNodeTitleHeight = 24.f;
constexpr float kNodeRowHeight   = 22.f;
constexpr float kNodeMinHeight   = 80.f;
constexpr float kNodeMinWidth    = 160.f;
constexpr float kNodePadInner    = 8.f;
constexpr float kNodeCharWidth   = 8.f;
constexpr float kNodePinRadius   = 5.f;
// Ancho del widget de valor (DragFloat).  En MODEL UNITS — el renderer
// hace `PushItemWidth(kNodeWidgetWidth * zoom)` para mantener la
// proporción a cualquier zoom.  NodeCanvas no debe pasar este número
// como literal (era el bug del Fase 1.x: SetNextItemWidth(86) en
// píxeles fijos rompía la regla invariante de "todo en model units").
constexpr float kNodeWidgetWidth = 86.f;
// Gap horizontal entre items dentro de una fila (label - widget - unit).
constexpr float kNodeRowGap      = 6.f;

// Calcula el tamaño del nodo a partir del tipo + ports + params del
// modelo.  Determinista, pura, sin dependencia del renderer.  Llamada
// desde Canvas (autoLayout) y desde NodeCanvas (al pasar dims al
// renderer en beginNode).
CanvasDims computeNodeDimensions(const NodeInstance& n);

class INodeRenderer;

class Canvas {
public:
    // El canvas referencia un NodeGraph existente pero no lo posee.  El
    // NodeGraph es propiedad de quien lo creó (el NodeCanvas top-level
    // posee m_graph; los SubGraph viven dentro vía side-table).
    explicit Canvas(NodeGraph& graph);

    // No copia — un canvas tiene tablas de posiciones que no deberían
    // duplicarse implícitamente.
    Canvas(const Canvas&)            = delete;
    Canvas& operator=(const Canvas&) = delete;
    Canvas(Canvas&&)                 = default;
    Canvas& operator=(Canvas&&)      = default;

    // ---- Estado del modelo visual ----------------------------------------
    CanvasPos  positionOf(int nodeId) const;
    void       setPositionOf(int nodeId, CanvasPos pos);

    // Dimensiones calculadas a partir de tipo + ports + params del nodo.
    // No depende de la librería de render: una pura función del modelo.
    // El renderer concreto puede usar estas dimensiones para pedirle a la
    // librería que reserve espacio coherente con el layout calculado.
    CanvasDims dimensionsOf(int nodeId) const;

    // ---- Sub-canvases (recursivos sobre los SubGraph del modelo) ---------
    Canvas*       subCanvasOf(int subGraphNodeId);
    const Canvas* subCanvasOf(int subGraphNodeId) const;

    // ---- Layout determinista --------------------------------------------
    // Asigna posiciones a todos los nodos del canvas vía topo-sort con
    // ruptura de feedback en nodos pure-state (reusa el predicado
    // isPureStateNode() del modelo).  Nodos en ciclos sin ruptura caen al
    // nivel 0.  Sources a la izquierda, Sinks/SubGraphOutput a la derecha.
    //
    // El método NO recursa en sub-canvases; el caller decide si quiere
    // aplicar layout en cada nivel por separado.
    void autoLayout();

    // ---- Acceso al modelo subyacente ------------------------------------
    NodeGraph&       graph()       { return *m_graph; }
    const NodeGraph& graph() const { return *m_graph; }

private:
    NodeGraph*                                                  m_graph;
    std::unordered_map<int, CanvasPos>                           m_positions;
    mutable std::unordered_map<int, std::unique_ptr<Canvas>>     m_subCanvases;
};

// =============================================================================
// INodeRenderer — frontera con la librería de edición de nodos.
//
// La interfaz expone TODO lo que el editor de nodos necesita en NUESTRO
// idioma: estructura (canvas → nodos → atributos), interacción (mouse,
// drag, selección, link creation), posicionamiento (CanvasPos en
// coordenadas del modelo) y estilo (colores via ImU32 que es el formato
// común de ImGui pero opaco para el resto del código).
//
// Implementación concreta inicial: ImNodesRenderer (delega a imnodes).
// Implementación futura: NativeNodeRenderer sobre ImDrawList con zoom.
// Implementación para tests UI: MockRenderer que captura llamadas.
//
// Reglas:
//   1. No exponer tipos de la librería externa (nada de `ImNodesEditorContext*`
//      ni `ImNodesPinShape_*` cruzando la frontera).
//   2. Los port shapes son un enum NUESTRO (`PortShape`); el renderer mapea.
//   3. Los attrIds (encoding `nodeId*10000 + portCode`) los pasa el caller
//      ya armados — la lógica del encoding vive en Canvas/NodeCanvas, no
//      en el renderer.
// =============================================================================

// Forma del pin (punto de conexión de un puerto).  El renderer mapea a la
// primitiva equivalente en su backend (imnodes tiene su propio enum;
// el renderer nativo dibuja la forma con ImDrawList).
enum class PortShape {
    CircleFilled,
    Circle,
    Triangle,
    Square,
};

// Resultado de pollInteractions / queries del frame.
struct LinkCreatedEvent {
    int fromAttrId = 0;
    int toAttrId   = 0;
};

// -----------------------------------------------------------------------------
// Enums del renderer — viven en el namespace para que las sub-interfaces
// puedan referenciarlos sin depender de la fachada combinada.  La
// fachada `INodeRenderer` los re-expone vía `using` para preservar los
// call sites legados (`INodeRenderer::ColorKey`, `INodeRenderer::CoordSpace`).
// -----------------------------------------------------------------------------
enum class NodeCoordSpace { Screen, Editor };
enum class NodeColorKey {
    TitleBar,
    TitleBarHovered,
    TitleBarSelected,
    Pin,
    PinHovered,
    Link,
    LinkHovered,
    LinkSelected,
};

// =============================================================================
// Segregación ISP: el contrato del renderer se rompe en tres roles
// independientes.  Los implementadores (NativeNodeRenderer) cumplen los
// tres mediante herencia múltiple; la fachada `INodeRenderer` los compone
// para que callers que necesitan los tres (NodeCanvas) sigan tomando una
// sola referencia.  Tests/clientes futuros que solo necesiten un rol
// pueden tomar la sub-interfaz adecuada.
// =============================================================================

// -----------------------------------------------------------------------------
// INodeRendererCore — ciclo de vida + dibujo + transformación de
// coordenadas + estilo.  Es el rol "rendering puro": cómo se pintan las
// cosas en el frame y dónde están.
// -----------------------------------------------------------------------------
class INodeRendererCore {
public:
    virtual ~INodeRendererCore() = default;

    // ---- Inicialización del backend (una sola vez por proceso) ---------
    // Crea el contexto global de la librería + aplica el tema oscuro por
    // defecto.  Llamado por AppWindow tras crear ImGui.
    virtual void init() = 0;
    // Libera el contexto global; espejo de init().
    virtual void shutdown() = 0;

    // ---- Canvas (lifecycle por frame) ----------------------------------
    // `contextKey` identifica el canvas (top-level "/", anidado "/5/", …)
    // para que el renderer mantenga estados por canvas (zoom, pan,
    // selección, …).
    virtual void beginCanvas(const std::string& contextKey) = 0;
    virtual void endCanvas()                                = 0;
    // Centra/encuadra todos los nodos al cargar un canvas vacío.
    virtual void resetCanvasView() = 0;
    // Encuadra la vista del canvas activo para que el rectángulo
    // [modelMin, modelMax] (en model units) caiga completo dentro del
    // viewport.  El caller calcula la bbox real con
    // computeNodeDimensions.  viewport(W,H) ≤ 0 → el renderer usa el
    // tamaño actual del canvas (registrado en beginCanvas).
    virtual void frameToBox(CanvasPos modelMin, CanvasPos modelMax,
                            float viewportW, float viewportH) = 0;
    // Cambio temporal de contexto (no rendering): permite leer/escribir
    // posiciones en un canvas distinto al actual sin abrir un ciclo
    // beginCanvas/endCanvas.  Las llamadas se anidan; pop restaura el
    // contexto previo.
    virtual void pushCanvas(const std::string& contextKey) = 0;
    virtual void popCanvas() = 0;

    // ---- Nodos ---------------------------------------------------------
    // `dims` viene del modelo (computeNodeDimensions); el renderer la usa
    // para reservar el área de la caja y alinear contenido relativo a
    // los bordes (p. ej. el label "out" cerca del pin derecho).
    //
    // `hasComment` (opcional): cuando es true, el renderer dibuja un
    // pequeño indicador (típicamente un punto amarillo en la esquina
    // superior derecha del nodo) que invita al usuario a usar
    // `Ctrl+hover` para leer el comentario.  Sin indicador, los
    // comentarios serían invisibles.
    virtual void beginNode(int nodeId, CanvasDims dims,
                           bool hasComment = false) = 0;
    virtual void endNode()                          = 0;
    virtual void beginNodeTitleBar()   = 0;
    virtual void endNodeTitleBar()     = 0;

    // ---- Atributos (puertos y campos estáticos dentro del nodo) -------
    virtual void beginInputAttribute (int attrId, PortShape shape) = 0;
    virtual void endInputAttribute   ()                            = 0;
    // `labelChars`: ancho aproximado del texto del output en caracteres.
    // El renderer reserva esa cantidad a la izquierda del pin para que
    // el texto no se salga del nodo.  Default 5 cubre "out N"; outputs
    // con sufijo de unidad (etapa 6I.F) deben pasar el tamaño real
    // (p. ej. "out [rad/s]" = 11).
    virtual void beginOutputAttribute(int attrId, PortShape shape,
                                      int labelChars = 5) = 0;
    virtual void endOutputAttribute  ()                            = 0;
    virtual void beginStaticAttribute(int attrId)                  = 0;
    virtual void endStaticAttribute  ()                            = 0;

    // Row de un PARÁMETRO con pin de entrada del lado izquierdo
    // (per-param-pins feature, estilo Blender).  El pin se registra
    // como cualquier otro attr para hit-test y selección; el caller
    // dibuja después la etiqueta + DragFloat + unidad del parámetro
    // alineadas a la derecha del pin.  El cursor queda posicionado
    // justo después del pin, como en `beginInputAttribute`, pero sin
    // emitir texto automáticamente.
    virtual void beginParamAttribute (int attrId, PortShape shape) = 0;
    virtual void endParamAttribute   ()                            = 0;

    // ---- Edges / Links -------------------------------------------------
    virtual void drawLink(int linkId, int fromAttrId, int toAttrId) = 0;

    // ---- Posicionamiento (sync con Canvas) -----------------------------
    // El Canvas posee las posiciones; el renderer las traduce a su backend.
    //
    // NodeCoordSpace::Screen → coordenadas en píxeles relativas a la
    //   ventana donde se dibuja el canvas (incluye el efecto del pan).
    //   Útil para colocar un nodo "donde está el cursor".
    // NodeCoordSpace::Editor → coordenadas independientes del pan,
    //   locales al canvas.  Útil para persistir en disco.
    virtual void setNodePosition(int nodeId, CanvasPos pos,
                                 NodeCoordSpace space = NodeCoordSpace::Screen) = 0;
    virtual CanvasPos getNodePosition(int nodeId,
                                      NodeCoordSpace space = NodeCoordSpace::Screen) const = 0;

    // ---- Estilo --------------------------------------------------------
    virtual void pushColor(NodeColorKey key, unsigned int rgba) = 0;
    virtual void popColor(int count = 1) = 0;
};

// -----------------------------------------------------------------------------
// INodeRendererSelection — mutación y lectura del estado de selección.
// Rol independiente porque hay clientes (p. ej. tests de selección) que
// no necesitan ni dibujar ni leer hovers.
// -----------------------------------------------------------------------------
class INodeRendererSelection {
public:
    virtual ~INodeRendererSelection() = default;

    virtual void clearNodeSelection()             = 0;
    virtual void selectNode(int nodeId)           = 0;
    virtual int  selectedNodeCount() const        = 0;
    virtual void getSelectedNodes(std::vector<int>& outIds) const = 0;
    virtual int  selectedLinkCount() const        = 0;
    virtual void getSelectedLinks(std::vector<int>& outIds) const = 0;
};

// -----------------------------------------------------------------------------
// INodeRendererQuery — lectura del estado de interacción del frame
// actual (qué se clickeó, qué está hover).  Lo consume el canvas para
// despachar acciones del usuario.
// -----------------------------------------------------------------------------
class INodeRendererQuery {
public:
    virtual ~INodeRendererQuery() = default;

    // El usuario soltó una conexión nueva en el frame actual.
    virtual bool pollLinkCreated(LinkCreatedEvent& out) = 0;
    virtual bool pollLinkDropped(int& outStartAttrId)   = 0;
    // El usuario hizo click sobre un pin de entrada conectado: el
    // renderer "agarra" el cable existente, lo separa del input y
    // transfiere el drag al pin de salida del otro extremo (UX
    // clásica Blender / TouchDesigner).  Devuelve true si hubo
    // detach en este frame; `outLinkId` es la edge a borrar del modelo.
    virtual bool pollLinkDetached(int& outLinkId)       = 0;
    virtual bool isLinkHovered(int& outLinkId)          = 0;
    virtual bool isNodeHovered(int& outNodeId)          = 0;
};

// -----------------------------------------------------------------------------
// INodeRenderer — fachada que compone los tres roles.  La usan los
// clientes que necesitan acceso a todo (NodeCanvas).  Los aliases
// internos preservan los call sites legados
// (`INodeRenderer::ColorKey`, `INodeRenderer::CoordSpace`).
// -----------------------------------------------------------------------------
class INodeRenderer : public virtual INodeRendererCore,
                      public virtual INodeRendererSelection,
                      public virtual INodeRendererQuery {
public:
    ~INodeRenderer() override = default;

    using ColorKey   = NodeColorKey;
    using CoordSpace = NodeCoordSpace;
};

}  // namespace scinodes::ui
