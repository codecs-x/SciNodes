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

class INodeRenderer {
public:
    virtual ~INodeRenderer() = default;

    // ---- Inicialización del backend (una sola vez por proceso) ---------
    // Crea el contexto global de la librería + aplica el tema oscuro
    // por defecto.  Llamado por AppWindow tras crear ImGui.
    virtual void init() = 0;
    // Libera el contexto global; espejo de init().
    virtual void shutdown() = 0;

    // ---- Canvas (lifecycle por frame) ----------------------------------
    // `contextKey` identifica el canvas (top-level "/", anidado "/5/", …)
    // para que el renderer mantenga estados por canvas (zoom, pan,
    // selección imnodes-side, …).
    virtual void beginCanvas(const std::string& contextKey) = 0;
    virtual void endCanvas()                                = 0;
    // Centra/encuadra todos los nodos al cargar un canvas vacío.
    virtual void resetCanvasView() = 0;
    // Encuadra la vista del canvas activo para que el rectángulo
    // [modelMin, modelMax] (en model units) caiga completo dentro del
    // viewport.  El caller calcula la bbox real con las dimensiones
    // verdaderas de cada nodo (computeNodeDimensions); pasarle solo
    // ids al renderer no basta porque éste no conoce el modelo y
    // estimaría los tamaños — se obtienen sub-bboxes y los nodos
    // anchos se salen del encuadre.
    // viewport(W,H) ≤ 0 → el renderer usa el tamaño actual del canvas
    // (registrado en beginCanvas).
    virtual void frameToBox(CanvasPos modelMin, CanvasPos modelMax,
                            float viewportW, float viewportH) = 0;
    // Cambio temporal de contexto (no rendering): permite leer/escribir
    // posiciones en un canvas distinto al actual sin abrir un ciclo
    // beginCanvas/endCanvas.  Útil para paste, encapsulate y otras
    // operaciones que tocan un sub-canvas que el usuario aún no abrió.
    // Las llamadas se anidan: pop restaura el contexto previo.
    virtual void pushCanvas(const std::string& contextKey) = 0;
    virtual void popCanvas() = 0;

    // ---- Nodos ---------------------------------------------------------
    // `dims` viene del modelo (computeNodeDimensions); el renderer la usa
    // para reservar el área de la caja y para alinear contenido relativo
    // a los bordes (p. ej. el label "out" cerca del pin derecho).
    // Renderers que auto-miden el contenido (imnodes) pueden ignorarla.
    virtual void beginNode(int nodeId, CanvasDims dims) = 0;
    virtual void endNode()                              = 0;
    virtual void beginNodeTitleBar()   = 0;
    virtual void endNodeTitleBar()     = 0;

    // ---- Atributos (puertos y campos estáticos dentro del nodo) -------
    virtual void beginInputAttribute (int attrId, PortShape shape) = 0;
    virtual void endInputAttribute   ()                            = 0;
    virtual void beginOutputAttribute(int attrId, PortShape shape) = 0;
    virtual void endOutputAttribute  ()                            = 0;
    virtual void beginStaticAttribute(int attrId)                  = 0;
    virtual void endStaticAttribute  ()                            = 0;

    // ---- Edges / Links -------------------------------------------------
    virtual void drawLink(int linkId, int fromAttrId, int toAttrId) = 0;

    // ---- Posicionamiento (sync con Canvas) -----------------------------
    // El Canvas posee las posiciones; el renderer las traduce a su backend.
    //
    // CoordSpace::Screen → coordenadas en píxeles relativas a la ventana
    //   donde se dibuja el canvas (incluye el efecto del pan del usuario).
    //   Útil para colocar un nodo "donde está el cursor".
    // CoordSpace::Editor → coordenadas independientes del pan, locales al
    //   canvas.  Útil para persistir en disco (un nodo guardado en (300,200)
    //   queda en (300,200) sin importar dónde haya paneado el usuario).
    enum class CoordSpace { Screen, Editor };
    virtual void setNodePosition(int nodeId, CanvasPos pos,
                                 CoordSpace space = CoordSpace::Screen) = 0;
    // Lectura inversa: el usuario arrastró el nodo en la UI; el caller
    // sincroniza el modelo.
    virtual CanvasPos getNodePosition(int nodeId,
                                      CoordSpace space = CoordSpace::Screen) const = 0;

    // ---- Selección (estado lo posee la librería; consultable por el caller) -
    virtual void clearNodeSelection()             = 0;
    virtual void selectNode(int nodeId)           = 0;
    virtual int  selectedNodeCount() const        = 0;
    virtual void getSelectedNodes(std::vector<int>& outIds) const = 0;
    virtual int  selectedLinkCount() const        = 0;
    virtual void getSelectedLinks(std::vector<int>& outIds) const = 0;

    // ---- Queries del frame ---------------------------------------------
    // El usuario soltó una conexión nueva en el frame actual.
    virtual bool pollLinkCreated(LinkCreatedEvent& out) = 0;
    virtual bool pollLinkDropped(int& outStartAttrId)   = 0;
    // El usuario hizo click sobre un pin de entrada que ya tenía un
    // edge conectado: el renderer "agarra" el cable existente, lo
    // separa del input y transfiere el drag al pin de salida del otro
    // extremo (UX clásica de Blender / TouchDesigner: el cable se
    // puede recolocar o tirar al vacío para borrarlo).  Devuelve true
    // si hubo detach en este frame; `outLinkId` es la edge a borrar
    // del modelo.
    virtual bool pollLinkDetached(int& outLinkId)       = 0;
    virtual bool isLinkHovered(int& outLinkId)          = 0;
    virtual bool isNodeHovered(int& outNodeId)          = 0;

    // ---- Estilo --------------------------------------------------------
    // `colorKey` es un enum nuestro (Title, TitleHovered, TitleSelected,
    // Pin, PinHovered).  ImU32 ABGR sin ataduras a imgui_internal_t.
    enum class ColorKey {
        TitleBar,
        TitleBarHovered,
        TitleBarSelected,
        Pin,
        PinHovered,
        Link,
        LinkHovered,
        LinkSelected,
    };
    virtual void pushColor(ColorKey key, unsigned int rgba) = 0;
    virtual void popColor(int count = 1) = 0;
};

}  // namespace scinodes::ui
