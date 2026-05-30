#include "NodeGraph.hpp"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

NodeGraph::NodeGraph() = default;

NodeGraph::NodeGraph(const NodeGraph& other)
    : m_nodes      (other.m_nodes),
      m_edges      (other.m_edges),
      m_nextNodeId (other.m_nextNodeId),
      m_nextEdgeId (other.m_nextEdgeId),
      m_parser     ()
{
    // Deep clone of child subgraphs so snapshots/undo are independent
    // of the live graph.  Defaulted copy would share the shared_ptr.
    for (const auto& [k, v] : other.m_subGraphs) {
        if (v) m_subGraphs[k] = std::make_shared<NodeGraph>(*v);
    }
}

NodeGraph& NodeGraph::operator=(const NodeGraph& other) {
    if (this == &other) return *this;
    NodeGraph tmp(other);
    using std::swap;
    swap(m_nodes,       tmp.m_nodes);
    swap(m_edges,       tmp.m_edges);
    swap(m_nextNodeId,  tmp.m_nextNodeId);
    swap(m_nextEdgeId,  tmp.m_nextEdgeId);
    swap(m_subGraphs,   tmp.m_subGraphs);
    return *this;
}

// ---------------------------------------------------------------------------
// node management
// ---------------------------------------------------------------------------
int NodeGraph::addNode(NodeType type) {
    int id = m_nextNodeId++;
    m_nodes.push_back(makeNode(id, type));
    return id;
}

int NodeGraph::addNodeWithId(NodeType type, int id) {
    m_nodes.push_back(makeNode(id, type));
    if (id >= m_nextNodeId) m_nextNodeId = id + 1;
    return id;
}

int NodeGraph::addCustomNodeWithId(const std::string& customType, int id) {
    m_nodes.push_back(makeCustomNode(id, customType));
    if (id >= m_nextNodeId) m_nextNodeId = id + 1;
    return id;
}

int NodeGraph::addSubGraphNode() {
    const int id = addNode(NodeType::SubGraph);
    // Inicializar grafo hijo con un input y un output stubs por defecto.
    auto child = std::make_shared<NodeGraph>();
    int inId  = child->addNode(NodeType::SubGraphInput);
    int outId = child->addNode(NodeType::SubGraphOutput);
    child->setParam(inId,  "Port", 0.0);
    child->setParam(outId, "Port", 0.0);
    m_subGraphs[id] = std::move(child);
    recomputeSubGraphPorts(id);
    return id;
}

NodeGraph* NodeGraph::subGraphOf(int nodeId) {
    auto it = m_subGraphs.find(nodeId);
    return (it != m_subGraphs.end()) ? it->second.get() : nullptr;
}
const NodeGraph* NodeGraph::subGraphOf(int nodeId) const {
    auto it = m_subGraphs.find(nodeId);
    return (it != m_subGraphs.end()) ? it->second.get() : nullptr;
}

NodeGraph::EncapsulateResult
NodeGraph::encapsulateByIds(const std::vector<int>& ids) {
    EncapsulateResult result;
    if (ids.empty()) return result;
    std::unordered_set<int> selSet(ids.begin(), ids.end());

    // Rechazar si la selección contiene port stubs.
    for (int id : ids) {
        if (const auto* n = findNode(id)) {
            if (isSubGraphStub(n->type))
                return result;   // sgId = 0
        }
    }

    // Particionar aristas en internas/entrantes/salientes.
    std::vector<Edge> internalEdges, inEdges, outEdges;
    for (const Edge& e : m_edges) {
        bool f = selSet.count(e.fromNodeId) > 0;
        bool t = selSet.count(e.toNodeId)   > 0;
        if (f && t)         internalEdges.push_back(e);
        else if (!f && t)   inEdges.push_back(e);
        else if (f && !t)   outEdges.push_back(e);
    }

    std::vector<int> extInSources, mappedInTargetAttr;
    for (const Edge& e : inEdges) {
        extInSources.push_back(e.fromAttrId);
        mappedInTargetAttr.push_back(e.toAttrId);
    }
    std::vector<std::pair<int,int>> mappedOutSrc;
    std::vector<std::vector<int>>   outConsumers;
    {
        std::map<std::pair<int,int>, int> grouper;
        for (const Edge& e : outEdges) {
            int fromPort = attrOutputPort(e.fromAttrId);
            auto key = std::make_pair(e.fromNodeId, fromPort);
            auto [it, ins] = grouper.try_emplace(key,
                static_cast<int>(mappedOutSrc.size()));
            if (ins) { mappedOutSrc.push_back(key);
                       outConsumers.emplace_back(); }
            outConsumers[it->second].push_back(e.toAttrId);
        }
    }

    // Crear el SubGraph y limpiar sus stubs default.
    const int sgId = addSubGraphNode();
    NodeGraph* child = subGraphOf(sgId);
    if (!child) return result;
    result.sgId = sgId;
    for (const NodeInstance& s : std::vector<NodeInstance>(child->nodes())) {
        if (isSubGraphStub(s.type)) child->removeNode(s.id);
    }

    // Copiar nodos seleccionados al child PRESERVANDO sus IDs.  Sin
    // esto, encapsular renumera los nodos internos y un Resume tras
    // la operación pierde todo el estado acumulado: el seed devuelto
    // por captureState está indexado por (oldId, slot), pero el plan
    // generado del nuevo grafo busca (newId, slot) → no encuentra
    // nada → cae a las IC default → la respuesta empieza desde cero
    // en t actual (efecto "función escalón corrida").
    auto& idMap = result.idMap;
    for (int oldId : ids) {
        const NodeInstance* src = findNode(oldId);
        if (!src) continue;
        int newId;
        if (src->type == NodeType::Custom)
            newId = child->addCustomNodeWithId(src->customType, oldId);
        else if (isSubGraphContainer(src->type)) {
            // SubGraph anidado: tomar el grafo hijo del padre original
            // y reinstalarlo bajo el mismo ID en el nuevo nivel.
            newId = child->addNodeWithId(NodeType::SubGraph, oldId);
            if (auto* nested = subGraphOf(oldId)) {
                NodeGraph copy = *nested;
                child->installSubGraph(newId, std::move(copy));
            }
            child->recomputeSubGraphPorts(newId);
        }
        else newId = child->addNodeWithId(src->type, oldId);
        idMap[oldId] = newId;
        for (const auto& [k, v] : src->params)       child->setParam(newId, k, v);
        for (const auto& [k, v] : src->stringParams) child->setStringParam(newId, k, v);
        if (!src->assetPath.empty())                 child->setAssetPath(newId, src->assetPath);
    }

    // Aristas internas.
    for (const Edge& e : internalEdges) {
        auto fIt = idMap.find(e.fromNodeId);
        auto tIt = idMap.find(e.toNodeId);
        if (fIt == idMap.end() || tIt == idMap.end()) continue;
        child->tryAddEdge(attrRemap(e.fromAttrId, fIt->second),
                          attrRemap(e.toAttrId,   tIt->second));
    }

    // Stubs primero (todos), luego recompute, luego cablear.
    std::vector<int> inStubIds, outStubIds;
    for (size_t k = 0; k < extInSources.size(); ++k) {
        int stubId = child->addNode(NodeType::SubGraphInput);
        child->setParam(stubId, "Port", double(k));
        inStubIds.push_back(stubId);
    }
    for (size_t k = 0; k < mappedOutSrc.size(); ++k) {
        int stubId = child->addNode(NodeType::SubGraphOutput);
        child->setParam(stubId, "Port", double(k));
        outStubIds.push_back(stubId);
    }
    recomputeSubGraphPorts(sgId);

    // Cablear stubs internamente.
    for (size_t k = 0; k < extInSources.size(); ++k) {
        int origTarget = mappedInTargetAttr[k];
        auto tIt = idMap.find(attrNodeId(origTarget));
        if (tIt == idMap.end()) continue;
        int newTarget = attrRemap(origTarget, tIt->second);
        // El stub SubGraphInput emite por su puerto de salida 0.
        child->tryAddEdge(inStubIds[k] * kAttrIdNodeStride + kAttrIdOutputBase,
                          newTarget);
    }
    for (size_t k = 0; k < mappedOutSrc.size(); ++k) {
        auto [origNodeId, origPort] = mappedOutSrc[k];
        auto fIt = idMap.find(origNodeId);
        if (fIt == idMap.end()) continue;
        int newSource = fIt->second * kAttrIdNodeStride + kAttrIdOutputBase + origPort;
        // El stub SubGraphOutput recibe por su input 0.
        child->tryAddEdge(newSource, outStubIds[k] * kAttrIdNodeStride);
    }

    // Borrar nodos viejos (limpia aristas externas viejas).
    for (int oldId : ids) removeNode(oldId);

    // Crear aristas externas nuevas hacia/desde el SubGraph.
    for (size_t k = 0; k < extInSources.size(); ++k)
        tryAddEdge(extInSources[k], sgId * kAttrIdNodeStride + int(k));
    for (size_t k = 0; k < mappedOutSrc.size(); ++k) {
        int sgOutAttr = sgId * kAttrIdNodeStride + kAttrIdOutputBase + int(k);
        for (int toAttr : outConsumers[k])
            tryAddEdge(sgOutAttr, toAttr);
    }
    return result;
}

void NodeGraph::installSubGraph(int subGraphNodeId, NodeGraph&& child) {
    m_subGraphs[subGraphNodeId] =
        std::make_shared<NodeGraph>(std::move(child));
}

void NodeGraph::recomputeSubGraphPorts(int subGraphNodeId) {
    auto it = m_subGraphs.find(subGraphNodeId);
    if (it == m_subGraphs.end() || !it->second) return;
    int nIn = 0, nOut = 0;
    for (const NodeInstance& c : it->second->nodes()) {
        if (c.type == NodeType::SubGraphInput)  ++nIn;
        if (c.type == NodeType::SubGraphOutput) ++nOut;
    }
    for (NodeInstance& n : m_nodes) {
        if (n.id == subGraphNodeId && isSubGraphContainer(n.type)) {
            n.subGraphInputCount  = nIn;
            n.subGraphOutputCount = nOut;
            break;
        }
    }
}

int NodeGraph::addCustomNode(const std::string& customType) {
    int id = m_nextNodeId++;
    m_nodes.push_back(makeCustomNode(id, customType));
    return id;
}

void NodeGraph::removeNode(int nodeId) {
    // Remove all edges incident to this node first
    m_edges.erase(
        std::remove_if(m_edges.begin(), m_edges.end(),
            [nodeId](const Edge& e) {
                return e.fromNodeId == nodeId || e.toNodeId == nodeId;
            }),
        m_edges.end());

    m_nodes.erase(
        std::remove_if(m_nodes.begin(), m_nodes.end(),
            [nodeId](const NodeInstance& n) { return n.id == nodeId; }),
        m_nodes.end());

    // Drop the child graph if this was a SubGraph node.
    m_subGraphs.erase(nodeId);
}

// ---------------------------------------------------------------------------
// edge management
// ---------------------------------------------------------------------------
std::optional<GrammarError>
NodeGraph::tryAddEdge(int fromAttrId, int toAttrId) {
    // Normalise: la convención es que el primer arg sea el OUTPUT y el
    // segundo el INPUT o PARAM.  Si el caller los pasó al revés
    // (un output como toAttr), intercambiamos.
    if (!attrIsOutput(fromAttrId)) std::swap(fromAttrId, toAttrId);

    int fromNodeId = attrNodeId(fromAttrId);
    int toNodeId   = attrNodeId(toAttrId);

    const NodeInstance* from = findNode(fromNodeId);
    const NodeInstance* to   = findNode(toNodeId);
    if (!from || !to)
        return GrammarError{"R0", "Unknown node in connection.", fromNodeId, toNodeId};

    // R4 (port-aware duplicate). The grammar parser only knows about the
    // node-pair level; here we have the proposed attribute pair, so
    // forbid only the genuinely-duplicate (fromAttrId, toAttrId) pair.
    // Multi-output sources are free to drive multiple inputs of the same
    // destination as long as each wire lands on a distinct (port, port).
    for (const auto& e : m_edges)
        if (e.fromAttrId == fromAttrId && e.toAttrId == toAttrId)
            return GrammarError{"R4",
                "This connection already exists.",
                fromNodeId, toNodeId};

    const bool toIsParam = attrIsParam(toAttrId);
    auto err = m_parser.validateEdge(*from, *to, m_edges, toIsParam);
    if (err) return err;

    // Check that the specific input port (toAttrId) is not already occupied.
    // GrammarParser only checks whether *all* ports are full; this catches
    // the case where two wires try to land on the same port of a multi-input node.
    for (const auto& e : m_edges)
        if (e.toAttrId == toAttrId)
            return GrammarError{"R5",
                std::string("That input port of \"")
                    + defOf(*to).label
                    + "\" is already connected.",
                fromNodeId, toNodeId};

    m_edges.push_back({ m_nextEdgeId++, fromNodeId, toNodeId, fromAttrId, toAttrId });
    return std::nullopt;
}

void NodeGraph::removeEdge(int edgeId) {
    m_edges.erase(
        std::remove_if(m_edges.begin(), m_edges.end(),
            [edgeId](const Edge& e) { return e.id == edgeId; }),
        m_edges.end());
}

// ---------------------------------------------------------------------------
// accessors
// ---------------------------------------------------------------------------
const NodeInstance* NodeGraph::findNode(int nodeId) const {
    for (const auto& n : m_nodes)
        if (n.id == nodeId) return &n;
    return nullptr;
}

void NodeGraph::setStringParam(int nodeId, const std::string& key,
                               const std::string& value) {
    for (auto& n : m_nodes) {
        if (n.id == nodeId) {
            n.stringParams[key] = value;
            return;
        }
    }
}

void NodeGraph::setParam(int nodeId, const std::string& name, double value) {
    for (auto& n : m_nodes) {
        if (n.id == nodeId) {
            auto it = n.params.find(name);
            if (it != n.params.end())
                it->second = value;
            return;
        }
    }
}

void NodeGraph::setAssetPath(int nodeId, const std::string& path) {
    for (auto& n : m_nodes) {
        if (n.id == nodeId) { n.assetPath = path; return; }
    }
}

const Edge* NodeGraph::findEdge(int edgeId) const {
    for (const auto& e : m_edges)
        if (e.id == edgeId) return &e;
    return nullptr;
}

// ---------------------------------------------------------------------------
// grammar
// ---------------------------------------------------------------------------
GrammarState NodeGraph::grammarState() const {
    // Pasamos *this para que el parser pueda recursar en cada SubGraph.
    // Sin la recursión, un SubGraph internamente roto (p. ej. su output
    // stub no recibe ninguna señal) pasaría como Valid desde el padre y
    // la simulación arrancaría con salidas a 0.0.
    return m_parser.validateGraph(*this);
}

const char* NodeGraph::grammarLabel() const {
    return GrammarParser::label(grammarState());
}

// ---------------------------------------------------------------------------
// snapshot / restore
// ---------------------------------------------------------------------------
GraphSnapshot NodeGraph::snapshot() const {
    GraphSnapshot s;
    s.nodes      = m_nodes;
    s.edges      = m_edges;
    s.nextNodeId = m_nextNodeId;
    s.nextEdgeId = m_nextEdgeId;
    // Recursive snapshot of child SubGraphs.
    for (const auto& [k, v] : m_subGraphs) {
        if (v) s.subGraphs[k] = std::make_shared<GraphSnapshot>(v->snapshot());
    }
    return s;
}

void NodeGraph::restoreSnapshot(const GraphSnapshot& s) {
    m_nodes      = s.nodes;
    m_edges      = s.edges;
    m_nextNodeId = s.nextNodeId;
    m_nextEdgeId = s.nextEdgeId;
    // Restore child SubGraphs (clean slate then repopulate).
    m_subGraphs.clear();
    for (const auto& [k, v] : s.subGraphs) {
        if (!v) continue;
        auto child = std::make_shared<NodeGraph>();
        child->restoreSnapshot(*v);
        m_subGraphs[k] = std::move(child);
    }
}
