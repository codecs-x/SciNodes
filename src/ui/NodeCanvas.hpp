#pragma once
#include "../app/FileDialog.hpp"
#include "../core/AssetMapping.hpp"
#include "../core/ContractRegistry.hpp"
#include "../core/DeviceAsset.hpp"
#include "../core/ISimSession.hpp"
#include "../core/NodeGraph.hpp"
#include "../core/ScnSerializer.hpp"
#include "../core/UndoRedoStack.hpp"
#include "AssetMappingPanel.hpp"
#include <functional>
#include <imgui.h>
#include <optional>
#include <string>
#include <unordered_map>

namespace scinodes::app { class AssetService; }
namespace scinodes::ui  { class INodeRenderer; }

// -----------------------------------------------------------------------
// NodeCanvas — imnodes editor wrapper for Stage 2.
//
// Owns the NodeGraph and UndoRedoStack.
//
// Keyboard shortcuts:
//   Shift+A        → Add-node popup (at cursor)
//   Delete/Backspace → Delete selected nodes and/or edges
//   Ctrl+Z         → Undo
//   Ctrl+Y / Ctrl+Shift+Z → Redo
//   Ctrl+scroll    → Zoom (0.25× – 3.0×)
// -----------------------------------------------------------------------
class NodeCanvas {
public:
    // Inyección del renderer (anti-corruption layer sobre imnodes/imgui).
    // AppWindow lo crea, inicializa y se lo pasa antes de init().  Sin
    // este puente la canvas no puede dibujar nada — patrón DIP idéntico
    // al de IComputeBackend.
    void setRenderer(scinodes::ui::INodeRenderer& r) { m_renderer = &r; }

    void init();
    // Render del contenido — sin ImGui::Begin/End (el host Area se encarga).
    void drawContent();
    void clear();
    int  addNode(NodeType type);                            // records undo, adds to graph; returns new id
    int  addCustomNode(const std::string& customType);      // for JSON-loaded types; returns new id
    void resetView();

    // ---- persistence (calls ScnSerializer) ------------------------------
    bool       saveToFile(const std::string& path);
    LoadReport loadFromFile(const std::string& path);

    // Resultado del import: si `ok` es false, `error` explica la falla
    // de I/O o de parseo; si es true, `report` puede traer `unknownTypes`
    // o `rejectedEdges` (mismo esquema que `loadFromFile`).  El caller
    // decide si muestra el popup de "violations" o si callado importó.
    struct ImportResult {
        bool        ok = false;
        std::string error;
        LoadReport  report;
    };

    // Carga un .scn en un grafo temporal y MERGE-fusiona su contenido
    // en el grafo actual (top-level): los nodos importados reciben IDs
    // frescos para no colisionar, sus posiciones se desplazan a la
    // derecha del bounding box actual, y las aristas se re-mapean a los
    // nuevos IDs.  Registra un solo snapshot en el undo stack para que
    // un Ctrl+Z deshaga la importación entera.
    //
    // No detiene la simulación: importar es aditivo, mantiene el
    // estado vivo (los nodos nuevos arrancan con sus IC default en el
    // siguiente Run, pero los existentes no se tocan).
    ImportResult importFromFile(const std::string& path);

    // ---- root metadata edition ------------------------------------------
    // Los wrappers escriben en `m_graph` (top-level) y bumpean dirty para
    // que el detector de "cambios sin guardar" de FileActions capture la
    // edición.  No registran un snapshot por keystroke — el caller (panel
    // "Sobre este grafo") agrupa la edición y graba un solo snapshot
    // antes de aplicar, si quiere undo discreto.
    void setGraphTitle      (const std::string& s);
    void setGraphDescription(const std::string& s);
    void setGraphTags       (std::vector<std::string> v);

    // Catálogo de objetos 3D del proyecto.  Mismo patrón que los
    // setters de metadata: delegan al NodeGraph y bumpean el dirtyRev
    // para que FileActions detecte el cambio como "no guardado".
    void addImportedObject   (ImportedObject o);
    void removeImportedObject(const std::string& name);

    // ---- read-only mode (set automatically after a load with violations) -
    void setReadOnly(bool v) { m_readOnly = v; }
    bool isReadOnly() const  { return m_readOnly; }

    // Marca si la simulación está activa (Simulating o Paused).  Cuando
    // es true el canvas bloquea operaciones destructivas (Delete sobre
    // cables/nodos, detach-on-drag desde pins de entrada conectados) y
    // muestra el cursor "prohibido" al hover.  La regla semántica: una
    // desconexión cambia la identidad del sistema simulado, así que
    // debe forzar al usuario a Stop primero.  Operaciones aditivas
    // (crear cable nuevo a un puerto libre, añadir nodo) siguen
    // permitidas.
    void setSimActive(bool v) { m_simActive = v; }

    // Lee y limpia el flag "última edición fue un refactor estructural"
    // (encapsular, desempacar).  AppWindow lo consume cada frame para
    // pedirle a SimController que refresque la baseline sin disparar
    // el modal destructivo — el refactor cambia la jerarquía visible
    // pero NO las dinámicas aplanadas.
    bool consumeRefactorFlag() {
        bool v = m_refactorJustHappened;
        m_refactorJustHappened = false;
        return v;
    }

    // Callback fired on every DragFloat tick that changes a parameter.
    // Arguments: (path, paramIndex in NodeDef::params, new value).
    //
    // `path` es la cadena [sg1_id_en_top, ..., nodeId_en_active].  Para
    // un nodo top-level (canvasStack vacío) el path es {nodeId}.  Para un
    // nodo dentro de un SubGraph anidado, el path lleva los ids de cada
    // SubGraph atravesado terminando con el nodeId del nodo en su grafo
    // hijo.  AppWindow lo enruta a `ScilabBridge::sendParameter(path,...)`
    // que lo traduce a `flatId` usando el `idForPath` del último plan.
    using ParamCallback =
        std::function<void(const std::vector<int>& path,
                           int paramIdx, double value)>;
    void setParamCallback(ParamCallback cb) { m_paramCallback = std::move(cb); }

    // Construye el path canónico para `nodeId` desde el top-level: cada
    // entry del canvasStack + el nodeId mismo.  Helper público porque
    // tests y herramientas pueden quererlo.
    std::vector<int> pathFor(int nodeId) const {
        std::vector<int> p = m_canvasStack;
        p.push_back(nodeId);
        return p;
    }

    // Inyección del catálogo de contratos.  AppWindow lo llama al
    // iniciar tras cargar contracts/*.json.  Si nunca se llama, los
    // nodos Device muestran "(sin contrato registrado)" — equivalente
    // al estado pre-DI cuando no había contracts en disco.
    void setContractRegistry(const scinodes::ContractRegistry& reg) {
        m_contractRegistry = &reg;
    }
    const scinodes::ContractRegistry* contractRegistry() const {
        return m_contractRegistry;
    }

    // Inyección del facade de assets glTF.  Si nunca se llama, las
    // operaciones que requieren cargar/cachear assets son no-op (el
    // path se preserva en .scn pero no se valida visualmente).
    void setAssetService(scinodes::app::AssetService& svc) {
        m_assetService = &svc;
    }
    scinodes::app::AssetService* assetService() const { return m_assetService; }

    // Inyección opcional del bridge para que drawNode pueda leer valores
    // vivos al renderar etiquetas de puertos (ej. "rotación [rad] = 1.06"
    // al lado del TransformObject:in 2 cuando la simulación corre).
    // Si nunca se llama, drawNode no muestra valores vivos.  Puntero no-
    // owning; el caller (AppWindow) garantiza que el bridge sobrevive al
    // canvas.
    void setBridge(scinodes::ISimSession* bridge) { m_bridge = bridge; }

    // Mark a node as "first to go non-finite" — it is painted with a red
    // title bar. 0 means no node is currently highlighted.
    void setHighlightedNode(int nodeId) { m_highlightNodeId = nodeId; }

    // Accessors used by AppWindow
    const NodeGraph& graph()       const { return m_graph; }
    const std::vector<NodeInstance>& nodes() const { return m_graph.nodes(); }
    int   nodeCount()    const { return m_graph.nodeCount(); }
    int   edgeCount()    const { return m_graph.edgeCount(); }
    const char* grammarLabel() const { return m_graph.grammarLabel(); }

    // Revisión monotónica de la topología (nodos + aristas, incluidos los
    // contenidos de SubGraphs anidados).  Se incrementa cada vez que una
    // operación de edición muta el grafo.  Consumido por SimController
    // para detectar cambios estructurales y disparar un re-run automático
    // del bridge sin que el usuario tenga que pulsar Run a mano.
    int dirtyRevision() const { return m_dirtyRev; }

private:
    void drawNode(const NodeInstance& n);
    void drawEdges();
    void handleLinkCreated();
    void handleDeletion();
    void handleUndoRedo();
    void handleZoom();
    void handleParamPanelTrigger();   // sets m_openParamPanelNodeId on double-click
    void handleLinkDropped();         // opens add-popup with auto-connect on dangling drag
    void handleEdgeContextMenu();     // opens add-popup with insert-in-edge on RMB on link
    void handleCopyPaste();           // Ctrl+C copies selection, Ctrl+V pastes at cursor
    void copySelectionToClipboard();
    void pasteClipboard();
    void handleEncapsulate();         // Ctrl+G wraps selection into a SubGraph
    void encapsulateSelection();

    // SubGraph navigation: doble-click on a SubGraph node pushes its id
    // on the canvas stack; the breadcrumb at the top lets the user climb
    // back to any ancestor.  The "active" graph is the one currently
    // displayed in the canvas; mutations (add/remove/connect, encapsulate,
    // copy-paste) operate on it.  Top-level (`m_graph`) remains the
    // anchor for snapshots, save/load and history.
    NodeGraph&       active();
    const NodeGraph& active() const;
    void enterSubGraph(int subGraphId);
    void exitToLevel(int depth);      // 0 = top-level; pop until size==depth
    void drawBreadcrumb();
    void handleRename();              // F2 — rename selected SubGraph
    void handleAutoLayout();          // Ctrl+L — BFS layered layout
    // Aplica BFS-layered layout al grafo activo: agrupa los nodos por
    // su profundidad de longest-path desde las fuentes (in-degree 0 o
    // SubGraphInput) y los reparte en columnas equiespaciadas; los
    // SubGraphOutput van a la última columna.  Sobreescribe las
    // posiciones existentes en el EditorContext activo.
    void applyAutoLayout();
    void drawRenamePopup();

    // Wrappers that thread current imnodes positions through the
    // GraphSnapshot — undo/redo of structural ops needs them or the
    // restored nodes pile up at the origin.
    void recordSnapshot(GraphSnapshot snap);
    bool doUndoOrRedo(bool isUndo);   // returns true if a state was restored
    void openAddPopup(ImVec2 screenPos,
                      int autoConnectAttr = 0,
                      int insertEdgeId    = 0);
    // Returns the id of the newly created node, or 0 on failure.
    int  addNodeAt(NodeType type, ImVec2 canvasPos);
    void drawAddPopup();
    void drawParamPanel();            // floating window with sliders per param
    void showErrorTooltip();

    NodeGraph     m_graph;
    UndoRedoStack m_history;

    float  m_zoom      = 1.0f;
    ImVec2 m_popupPos  = {};

    // Add-popup typeahead + auto-connect / insert-in-edge state.  Filled by
    // openAddPopup() and consumed by drawAddPopup() on the same or next frame.
    //
    //   m_popupSearch         — typeahead filter; empty => browse-by-category
    //   m_popupAutoConnectAttr— attribute id from which a link was dragged-then-
    //                           dropped in empty space; 0 = no autoconnect
    //   m_popupInsertEdgeId   — edge id that should be replaced by the new node
    //                           inserted between its endpoints; 0 = no insert
    //   m_popupFocusSearch    — request InputText focus once after opening
    char  m_popupSearch[64]      = {0};
    int   m_popupAutoConnectAttr = 0;
    int   m_popupInsertEdgeId    = 0;
    bool  m_popupFocusSearch     = false;

    // Revisión estructural — ver dirtyRevision().
    int m_dirtyRev = 0;
    void bumpDirty() { ++m_dirtyRev; }

    // Popup F2 — funciona sobre cualquier nodo seleccionado.  El popup
    // tiene dos campos: "Name" (solo visible para SubGraphs, escribe en
    // stringParams["Name"]) y "Comment" (visible siempre, escribe en
    // n.comment).  Ambos vivientes simultáneamente; el usuario edita lo
    // que quiera y aplica con Enter o el botón Apply.
    int   m_renameNodeId         = 0;     // 0 = no popup open
    char  m_renameBuf[64]        = {0};   // buffer del campo Name
    char  m_commentBuf[512]      = {0};   // buffer del campo Comment
    bool  m_renameFocusPending   = false;

    // SubGraph navigation state.  `m_canvasStack` is a path of SubGraph
    // node ids from the top-level down to the currently displayed
    // sub-graph (empty = top-level).  El cache de editor-contexts vive
    // ahora dentro del INodeRenderer; NodeCanvas solo conoce el path-key
    // ("/" para top-level, "/5/", "/5/7/", …).
    std::vector<int> m_canvasStack;
    std::string canvasPathKey() const;
    scinodes::ui::INodeRenderer* m_renderer = nullptr;

    // Clipboard for Ctrl+C / Ctrl+V.  Stores a snapshot of the selected
    // nodes and the edges that live entirely between them, plus each
    // node's screen-space position at copy time so the paste preserves
    // the relative layout.  Edges crossing the selection boundary are
    // not captured.  Empty until the first copy.
    //
    // Para nodos `SubGraph`, además del NodeInstance guardamos una copia
    // profunda del grafo hijo — sin esto el paste produciría un SubGraph
    // sin contenido y la simulación / navegación quedarían rotas.
    struct ClipboardEntry {
        NodeInstance              node;
        ImVec2                    pos;       // screen-space at copy time
        std::shared_ptr<NodeGraph> childGraph;  // null si node.type != SubGraph
        // Para SubGraph: mapa (internalNodeId → screen-pos) capturado del
        // EditorContext del child al momento de copiar.  Sin esto, al
        // pegar el SubGraph el contexto del nuevo child está vacío de
        // posiciones y todos los nodos internos quedan amontonados en
        // el origen.  Solo se captura un nivel; SubGraphs anidados
        // adentro mantienen sus posiciones a través de su propio
        // childGraph, pero las posiciones de NIETOS no se persisten
        // en clipboard (limitación conocida — los nietos quedan
        // colapsados al entrar a un SubGraph anidado de un paste).
        std::unordered_map<int, ImVec2> internalPositions;
    };
    std::vector<ClipboardEntry> m_clipNodes;
    std::vector<Edge>           m_clipEdges;   // attrIds reference original node ids
    ImVec2                      m_clipAnchor   = {0, 0};   // top-left of copied bbox

    // Error tooltip
    std::string m_errorMsg;
    float       m_errorTimer = 0.0f;

    // Undo snapshot taken at the start of a parameter drag/edit.
    // Committed to history when the widget loses focus after editing.
    std::optional<GraphSnapshot> m_pendingParamBefore;

    // Persistence — per-node canvas positions (kept synced from imnodes at
    // save time, applied to imnodes after a load).
    ScnPositions m_positions;
    bool         m_applyPositionsPending = false;
    bool         m_readOnly              = false;
    bool         m_simActive             = false;
    bool         m_refactorJustHappened  = false;

    ParamCallback m_paramCallback;
    int           m_highlightNodeId = 0;

    // Catálogo de contratos device, inyectado por AppWindow vía
    // setContractRegistry().  Antes era singleton.
    const scinodes::ContractRegistry* m_contractRegistry = nullptr;

    // Facade de assets — encapsula contract lookup + DeviceAssetLoader
    // + cache.  Inyectado por AppWindow vía setAssetService().  Pre-C.8,
    // estas tres dependencias estaban embebidas inline aquí.
    scinodes::app::AssetService*      m_assetService     = nullptr;
    scinodes::ISimSession*            m_bridge           = nullptr;

    // Per-node parameter panel — opened by double-clicking a node.
    int    m_openParamPanelNodeId = 0;
    ImVec2 m_paramPanelPos        = {};

    // "Load Custom Node from JSON…" entry in the add-popup. The dialog
    // runs in a worker thread; we poll its result each frame and feed
    // the path into CustomNodeRegistry::loadFromFile.
    FileDialog  m_loadCustomDialog;
    std::string m_customLoadStatus;     // last result/error, shown briefly

    // Param-block CSV import/export (per-node), driven from the param
    // panel. Polls every frame like the custom-node loader. The node
    // id is captured at click time so a later graph mutation doesn't
    // race the dialog.
    enum class ParamCsvAction { None, Export, Import };
    FileDialog        m_paramCsvDialog;
    ParamCsvAction    m_paramCsvAction = ParamCsvAction::None;
    int               m_paramCsvNodeId = 0;
    std::string       m_paramCsvStatus;

    // Asset glTF (modelo 3D) binding para nodos NodeCategory::Device.
    // Cuando el usuario asigna un asset desde el panel de parámetros,
    // se valida contra el contrato del tipo y se cachea en m_assetService.
    FileDialog                                    m_assetDialog;
    int                                           m_assetDialogNodeId = 0;

    // Re-carga el asset del nodo desde n.assetPath via m_assetService.
    // No-op si no hay service inyectado, no hay contrato registrado, o
    // assetPath está vacío.
    void reloadAssetFor(int nodeId);

    // Mapping panel (in-app authoring del sidecar JSON).  Se abre
    // desde el botón "Editar mapping…" del panel del nodo, y
    // automáticamente cuando un asset recién cargado falla la
    // validación contra el contrato y aún no tiene sidecar.
    AssetMappingPanel m_mappingPanel;
    int               m_mappingNodeId = 0;   // nodo dueño del panel abierto

    // Abre el mapping panel para `nodeId`.  Resuelve el contrato y
    // re-parsea el asset.  No-op si el nodo no tiene assetPath o
    // tipo sin contrato.
    void openMappingPanelFor(int nodeId);

public:
    // Accesores usados por el Outliner (lectura de la caché + acciones
    // sobre el asset asignado a un nodo).  La cache vive en
    // AssetService; este wrapper retorna el mapa interno o, si no hay
    // service, una vista vacía estática (caso de tests).
    const std::unordered_map<int, scinodes::DeviceAsset>& loadedAssets() const;

    // Re-evalúa el asset del nodo (útil tras editar el .gltf en
    // disco).  Equivale a borrar la entrada y dejar que la UI la
    // re-cargue en el próximo frame.
    void reloadAsset(int nodeId) { reloadAssetFor(nodeId); }

    // Quita el asset del nodo: assetPath vacío + entrada de caché borrada.
    void detachAsset(int nodeId);

private:
    void syncPositionsFromImnodes();
    void applyPositionsToImnodes();
};
