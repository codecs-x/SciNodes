#pragma once
#include "Edge.hpp"
#include "NodeInstance.hpp"
#include <optional>
#include <string>
#include <vector>

// -----------------------------------------------------------------------
// GrammarError — returned when a connection violates a production rule.
// -----------------------------------------------------------------------
struct GrammarError {
    std::string rule;       // symbolic rule name, e.g. "R1"
    std::string message;    // human-readable explanation
    int fromNodeId = -1;
    int toNodeId   = -1;
};

// -----------------------------------------------------------------------
// GrammarState — overall validity of the whole graph.
// -----------------------------------------------------------------------
enum class GrammarState {
    Empty,        // no nodes at all
    Incomplete,   // has nodes but no complete Source → ... → Sink path
    Valid         // at least one complete pipeline exists
};

// -----------------------------------------------------------------------
// GrammarParser — stateless validator.
//
// Connection rule table (from_category × to_category):
//
//             │ Source │ Transformer │ Sink
//  ───────────┼────────┼─────────────┼──────
//  Source     │   ✗    │      ✓      │  ✓
//  Transformer│   ✗    │      ✓      │  ✓
//  Sink       │   ✗    │      ✗      │  ✗
//
// Rules enforced per edge:
//   R1 — Sinks have no output port
//   R2 — Sources have no input port
//   R3 — Self-connections are not allowed
//   R4 — Duplicate connection already exists
//   R5 — Input port already has an incoming edge
// -----------------------------------------------------------------------
class GrammarParser {
public:
    // Validate a single proposed edge.
    // Returns nullopt if valid; a GrammarError describing the violation otherwise.
    std::optional<GrammarError>
    validateEdge(const NodeInstance& fromNode,
                 const NodeInstance& toNode,
                 const std::vector<Edge>& existingEdges) const;

    // Validate the whole graph and return its overall state.
    GrammarState
    validateGraph(const std::vector<NodeInstance>& nodes,
                  const std::vector<Edge>& edges) const;

    // Human-readable label for the StatusBar.
    static const char* label(GrammarState s);

private:
    // BFS reachability: can we reach a Sink from any Source via edges?
    bool reachable(const std::vector<NodeInstance>& nodes,
                   const std::vector<Edge>& edges) const;
};
