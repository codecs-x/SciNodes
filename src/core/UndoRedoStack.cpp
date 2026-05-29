#include "UndoRedoStack.hpp"
#include "NodeGraph.hpp"   // for Edge definition

void UndoRedoStack::record(GraphSnapshot before) {
    m_past.push_back(std::move(before));
    if ((int)m_past.size() > MAX_DEPTH)
        m_past.pop_front();
    m_future.clear();   // new action invalidates redo history
}

std::optional<GraphSnapshot> UndoRedoStack::undo(GraphSnapshot current) {
    if (m_past.empty()) return std::nullopt;
    m_future.push_front(std::move(current));
    auto prev = std::move(m_past.back());
    m_past.pop_back();
    return prev;
}

std::optional<GraphSnapshot> UndoRedoStack::redo(GraphSnapshot current) {
    if (m_future.empty()) return std::nullopt;
    m_past.push_back(std::move(current));
    auto next = std::move(m_future.front());
    m_future.pop_front();
    return next;
}
