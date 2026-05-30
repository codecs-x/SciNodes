#pragma once
#include "Edge.hpp"
#include "NodeInstance.hpp"
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class NodeGraph;   // forward — full def needed by snapshot impl in NodeGraph.cpp

// -----------------------------------------------------------------------
// GraphSnapshot — lightweight copy of the entire graph state.
// Used to implement snapshot-based undo/redo.
//
// `positions` mirrors what the NodeCanvas keeps in `m_positions`; it is
// captured at record time and re-applied on restore so undo of
// structural ops (encapsulate, paste, delete) lands the nodes back
// where the user had them.  Without it, imnodes would assign default
// positions and every restored node would stack at the origin.
//
// `subGraphs` carries a recursive deep-copy of any child NodeGraph that
// was attached to a SubGraph node, so encapsulate/decapsulate are also
// undoable.  Empty for graphs without SubGraphs.
// -----------------------------------------------------------------------
struct GraphSnapshot {
    std::vector<NodeInstance>      nodes;
    std::vector<Edge>              edges;
    int nextNodeId = 1;
    int nextEdgeId = 1;

    // (nodeId → screen-space position).  Optional: empty for snapshots
    // produced before this field existed.
    std::map<int, std::pair<float, float>> positions;

    // Root metadata (top-level grafo): id, title, description, tags.
    // Vacíos en SubGraphs anidados.  Se incluyen aquí para que
    // restoreSnapshot tras Open... limpie también la metadata previa,
    // y para que undo/redo mantenga sincronía si la edita.
    std::string              id;
    std::string              title;
    std::string              description;
    std::vector<std::string> tags;

    // Recursive snapshots of child SubGraphs, indexed by parent node id.
    // Stored by value via shared_ptr so the struct stays copyable.
    std::map<int, std::shared_ptr<GraphSnapshot>> subGraphs;
};

// -----------------------------------------------------------------------
// UndoRedoStack — 50-deep snapshot-based history.
//
// Usage pattern:
//   1. BEFORE mutation: stack.record(graph.snapshot())
//   2. Apply mutation to graph.
//   3. Undo: graph.restoreSnapshot(*stack.undo(graph.snapshot()))
//   4. Redo: graph.restoreSnapshot(*stack.redo(graph.snapshot()))
// -----------------------------------------------------------------------
class UndoRedoStack {
public:
    static constexpr int MAX_DEPTH = 50;

    // Save the current state before a mutation.
    void record(GraphSnapshot before);

    // Returns the state to restore (i.e. the "before" snapshot).
    // Pushes current state into the redo queue.
    std::optional<GraphSnapshot> undo(GraphSnapshot current);

    // Re-applies the next future state.
    std::optional<GraphSnapshot> redo(GraphSnapshot current);

    bool canUndo() const { return !m_past.empty(); }
    bool canRedo() const { return !m_future.empty(); }
    void clear()         { m_past.clear(); m_future.clear(); }

private:
    std::deque<GraphSnapshot> m_past;    // states before current
    std::deque<GraphSnapshot> m_future;  // states after current
};
