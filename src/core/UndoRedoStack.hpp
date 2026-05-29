#pragma once
#include "Edge.hpp"
#include "NodeInstance.hpp"
#include <deque>
#include <optional>
#include <vector>

// -----------------------------------------------------------------------
// GraphSnapshot — lightweight copy of the entire graph state.
// Used to implement snapshot-based undo/redo.
// -----------------------------------------------------------------------
struct GraphSnapshot {
    std::vector<NodeInstance> nodes;
    std::vector<Edge>         edges;
    int nextNodeId = 1;
    int nextEdgeId = 1;
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
