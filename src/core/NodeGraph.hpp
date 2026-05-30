#pragma once
#include "Edge.hpp"
#include "GrammarParser.hpp"
#include "NodeInstance.hpp"
#include "UndoRedoStack.hpp"
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// -----------------------------------------------------------------------
// NodeGraph — owns nodes + edges, validates via GrammarParser.
//
// All mutations go through this class so grammar invariants are
// maintained and snapshots are always consistent.
// -----------------------------------------------------------------------
class NodeGraph {
public:
    // ---- node management -------------------------------------------------
    int  addNode(NodeType type);          // returns new node ID
    int  addCustomNode(const std::string& customType);  // JSON-loaded type
    void removeNode(int nodeId);          // also removes incident edges

    // ---- edge management -------------------------------------------------
    // Returns nullopt on success; GrammarError if the edge is invalid.
    std::optional<GrammarError>
    tryAddEdge(int fromAttrId, int toAttrId);

    void removeEdge(int edgeId);

    // ---- accessors -------------------------------------------------------
    const std::vector<NodeInstance>& nodes() const { return m_nodes; }
    const std::vector<Edge>&         edges() const { return m_edges; }

    int nodeCount() const { return static_cast<int>(m_nodes.size()); }
    int edgeCount() const { return static_cast<int>(m_edges.size()); }

    const NodeInstance* findNode(int nodeId)  const;
    const Edge*         findEdge(int edgeId)  const;

    // Modify a live parameter value (does not validate grammar).
    // No-op if nodeId or name is not found.
    void setParam(int nodeId, const std::string& name, double value);

    // Set/insert a string-valued metadata entry on the node (etiquetas
    // editables por puerto del Oscilloscope, etc.).  Persistido en .scn.
    void setStringParam(int nodeId, const std::string& key,
                        const std::string& value);

    // Modify the asset path of a Device node (no-op para no-Device).
    // Vacío significa "sin asset asignado".
    void setAssetPath(int nodeId, const std::string& path);

    // ---- grammar ---------------------------------------------------------
    GrammarState grammarState()     const;
    const char*  grammarLabel()     const;

    // ---- SubGraph (SuperBlock) -------------------------------------------
    // Cada nodo de tipo `SubGraph` lleva asociado un grafo hijo recursivo
    // que vive en este side-table.  El nodo padre sólo guarda su id; el
    // contenido está aquí para que `NodeInstance` siga siendo trivialmente
    // copiable.  Las APIs:
    //
    //   addSubGraphNode()    → addNode(SubGraph) + crea grafo hijo vacío
    //   subGraphOf(id)       → puntero al grafo hijo (nullptr si no existe)
    //
    // El grafo hijo arranca con un `SubGraphInput` y un `SubGraphOutput`
    // por defecto (puertos 0/0); el llamador puede mutarlo libremente.
    int  addSubGraphNode();
    NodeGraph*       subGraphOf(int nodeId);
    const NodeGraph* subGraphOf(int nodeId) const;

    // Inserta un grafo hijo bajo `subGraphNodeId` (que debe existir en
    // este grafo como nodo SubGraph), reemplazando cualquier contenido
    // previo.  Usado por el serializer para restaurar SubGraphs
    // anidados desde .scn sin pasar por addSubGraphNode (que crearía
    // stubs default no deseados).
    void installSubGraph(int subGraphNodeId, NodeGraph&& child);

    // Re-cuenta los `SubGraphInput`/`SubGraphOutput` del grafo hijo y los
    // refleja en `NodeInstance::subGraphInputCount/outputCount` del nodo
    // padre.  Llamar tras cualquier mutación del subgrafo para que
    // `defOf()` retorne el conteo de puertos correcto.
    void recomputeSubGraphPorts(int subGraphNodeId);

    // Encapsulate (Ctrl+G headless).  Toma una lista explícita de nodeIds,
    // mueve esos nodos a un nuevo SubGraph, materializa stubs internos
    // para cada arista que cruza la frontera, y re-cablea las externas
    // por los puertos del SubGraph.
    //
    // Esta API es PURA: no consulta GUI ni mantiene posiciones.  El
    // adaptador `NodeCanvas` recoge los IDs de imnodes y delega aquí,
    // luego sincroniza posiciones por su cuenta usando `idMap`.
    struct EncapsulateResult {
        int sgId = 0;                                 // 0 = error
        std::unordered_map<int, int> idMap;           // oldId (padre) → newId (child)
    };
    EncapsulateResult encapsulateByIds(const std::vector<int>& ids);

    // ---- snapshot (for UndoRedoStack) ------------------------------------
    GraphSnapshot snapshot()                       const;
    void          restoreSnapshot(const GraphSnapshot& s);

private:
    // used only by restoreSnapshot — bypasses validation
    void restoreEdge(const Edge& e);

    std::vector<NodeInstance> m_nodes;
    std::vector<Edge>         m_edges;
    int m_nextNodeId = 1;
    int m_nextEdgeId = 1;

    // SubGraph contents indexed by the parent SubGraph node's id.  The
    // value is a shared_ptr so NodeGraph remains copyable (snapshots
    // capture a deep clone via the explicit copy ctor below).
    std::map<int, std::shared_ptr<NodeGraph>> m_subGraphs;

    GrammarParser m_parser;

public:
    // Deep copy: subgraphs are cloned recursively so snapshots are
    // independent of the live graph.  Defaulted copy would share the
    // shared_ptr, which would break undo for SubGraph contents.
    NodeGraph();
    NodeGraph(const NodeGraph& other);
    NodeGraph& operator=(const NodeGraph& other);
    NodeGraph(NodeGraph&&) noexcept            = default;
    NodeGraph& operator=(NodeGraph&&) noexcept = default;
    ~NodeGraph()                               = default;
};
