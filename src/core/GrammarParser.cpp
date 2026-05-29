#include "GrammarParser.hpp"
#include "NodeGraph.hpp"   // for Edge definition
#include <algorithm>
#include <queue>
#include <unordered_map>

// ---------------------------------------------------------------------------
// validateEdge
// ---------------------------------------------------------------------------
std::optional<GrammarError>
GrammarParser::validateEdge(const NodeInstance& fromNode,
                             const NodeInstance& toNode,
                             const std::vector<Edge>& existingEdges) const {
    const NodeDef& fromDef = nodeRegistry().at(fromNode.type);
    const NodeDef& toDef   = nodeRegistry().at(toNode.type);

    // R3 — self-loop
    if (fromNode.id == toNode.id)
        return GrammarError{
            "R3", "Self-connections are not allowed.",
            fromNode.id, toNode.id
        };

    // R1 — Sink has no output
    if (fromDef.outputPorts == 0)
        return GrammarError{
            "R1",
            std::string("\"") + fromDef.label + "\" is a Sink — it has no output port.",
            fromNode.id, toNode.id
        };

    // R2 — Source has no input
    if (toDef.inputPorts == 0)
        return GrammarError{
            "R2",
            std::string("\"") + toDef.label + "\" is a Source — it has no input port.",
            fromNode.id, toNode.id
        };

    // R4 — duplicate
    for (const auto& e : existingEdges)
        if (e.fromNodeId == fromNode.id && e.toNodeId == toNode.id)
            return GrammarError{
                "R4", "This connection already exists.",
                fromNode.id, toNode.id
            };

    // R5 — the specific input port is already occupied.
    // Count how many existing edges already land on toNode, then check
    // whether all input ports are taken (supports multi-input nodes like Summation).
    {
        int usedPorts = 0;
        for (const auto& e : existingEdges)
            if (e.toNodeId == toNode.id)
                ++usedPorts;
        if (usedPorts >= toDef.inputPorts)
            return GrammarError{
                "R5",
                std::string("All input ports of \"") + toDef.label
                    + "\" are already connected.",
                fromNode.id, toNode.id
            };
    }

    // Grammar table check (redundant with R1/R2 but explicit)
    NodeCategory fc = fromDef.category;
    NodeCategory tc = toDef.category;

    // Sink → anything is already caught by R1.
    // anything → Source is already caught by R2.
    // The remaining valid cases: Source→Tx, Source→Sk, Tx→Tx, Tx→Sk.
    bool valid = (fc == NodeCategory::Source      && tc == NodeCategory::Transformer) ||
                 (fc == NodeCategory::Source      && tc == NodeCategory::Sink)        ||
                 (fc == NodeCategory::Transformer && tc == NodeCategory::Transformer) ||
                 (fc == NodeCategory::Transformer && tc == NodeCategory::Sink);

    if (!valid)
        return GrammarError{
            "R0",
            std::string("Cannot connect ") + fromDef.label + " → " + toDef.label
                + "  (violates S|>T|>Sk rule).",
            fromNode.id, toNode.id
        };

    return std::nullopt;   // valid
}

// ---------------------------------------------------------------------------
// reachable — BFS from every Source; returns true if any Sink is reached.
// ---------------------------------------------------------------------------
bool GrammarParser::reachable(const std::vector<NodeInstance>& nodes,
                               const std::vector<Edge>& edges) const {
    // Build adjacency list (fromNodeId → [toNodeId])
    std::unordered_map<int, std::vector<int>> adj;
    for (const auto& e : edges)
        adj[e.fromNodeId].push_back(e.toNodeId);

    // BFS seed: all Source nodes
    std::queue<int> q;
    std::unordered_map<int, bool> visited;
    for (const auto& n : nodes)
        if (categoryOf(n.type) == NodeCategory::Source) {
            q.push(n.id);
            visited[n.id] = true;
        }

    while (!q.empty()) {
        int cur = q.front(); q.pop();
        // Find this node in the list
        for (const auto& n : nodes) {
            if (n.id != cur) continue;
            if (categoryOf(n.type) == NodeCategory::Sink)
                return true;   // reached a sink
            break;
        }
        for (int next : adj[cur])
            if (!visited[next]) {
                visited[next] = true;
                q.push(next);
            }
    }
    return false;
}

// ---------------------------------------------------------------------------
// validateGraph
// ---------------------------------------------------------------------------
GrammarState
GrammarParser::validateGraph(const std::vector<NodeInstance>& nodes,
                              const std::vector<Edge>& edges) const {
    if (nodes.empty()) return GrammarState::Empty;
    if (reachable(nodes, edges)) return GrammarState::Valid;
    return GrammarState::Incomplete;
}

// ---------------------------------------------------------------------------
// label
// ---------------------------------------------------------------------------
const char* GrammarParser::label(GrammarState s) {
    switch (s) {
        case GrammarState::Empty:      return "Empty";
        case GrammarState::Incomplete: return "Incomplete";
        case GrammarState::Valid:      return "Valid";
    }
    return "Unknown";
}
