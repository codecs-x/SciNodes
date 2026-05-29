#pragma once
#include "Edge.hpp"
#include "GrammarParser.hpp"
#include "NodeInstance.hpp"
#include "UndoRedoStack.hpp"
#include <optional>
#include <string>
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

    // ---- grammar ---------------------------------------------------------
    GrammarState grammarState()     const;
    const char*  grammarLabel()     const;

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

    GrammarParser m_parser;
};
