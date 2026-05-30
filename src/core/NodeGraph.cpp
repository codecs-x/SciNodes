#include "NodeGraph.hpp"
#include <algorithm>

// ---------------------------------------------------------------------------
// node management
// ---------------------------------------------------------------------------
int NodeGraph::addNode(NodeType type) {
    int id = m_nextNodeId++;
    m_nodes.push_back(makeNode(id, type));
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
}

// ---------------------------------------------------------------------------
// edge management
// ---------------------------------------------------------------------------
std::optional<GrammarError>
NodeGraph::tryAddEdge(int fromAttrId, int toAttrId) {
    // Normalise: output attrs occupy the 9000..9999 slot range.
    auto isOutput = [](int a) { int m = a % 10000; return m >= 9000 && m <= 9999; };
    if (!isOutput(fromAttrId)) std::swap(fromAttrId, toAttrId);

    int fromNodeId = fromAttrId / 10000;
    int toNodeId   = toAttrId   / 10000;

    const NodeInstance* from = findNode(fromNodeId);
    const NodeInstance* to   = findNode(toNodeId);
    if (!from || !to)
        return GrammarError{"R0", "Unknown node in connection.", fromNodeId, toNodeId};

    auto err = m_parser.validateEdge(*from, *to, m_edges);
    if (err) return err;

    // Check that the specific input port (toAttrId) is not already occupied.
    // GrammarParser only checks whether *all* ports are full; this catches
    // the case where two wires try to land on the same port of a multi-input node.
    for (const auto& e : m_edges)
        if (e.toAttrId == toAttrId)
            return GrammarError{"R5",
                std::string("That input port of \"")
                    + nodeRegistry().at(to->type).label
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

const Edge* NodeGraph::findEdge(int edgeId) const {
    for (const auto& e : m_edges)
        if (e.id == edgeId) return &e;
    return nullptr;
}

// ---------------------------------------------------------------------------
// grammar
// ---------------------------------------------------------------------------
GrammarState NodeGraph::grammarState() const {
    return m_parser.validateGraph(m_nodes, m_edges);
}

const char* NodeGraph::grammarLabel() const {
    return GrammarParser::label(grammarState());
}

// ---------------------------------------------------------------------------
// snapshot / restore
// ---------------------------------------------------------------------------
GraphSnapshot NodeGraph::snapshot() const {
    return { m_nodes, m_edges, m_nextNodeId, m_nextEdgeId };
}

void NodeGraph::restoreSnapshot(const GraphSnapshot& s) {
    m_nodes      = s.nodes;
    m_edges      = s.edges;
    m_nextNodeId = s.nextNodeId;
    m_nextEdgeId = s.nextEdgeId;
}
