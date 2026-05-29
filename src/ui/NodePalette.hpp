#pragma once
#include "../core/NodeType.hpp"
#include <optional>

// -----------------------------------------------------------------------
// NodePalette — sidebar tree-view grouped by Source / Transformer / Sink.
// draw() returns the NodeType the user clicked, if any.
// -----------------------------------------------------------------------
class NodePalette {
public:
    std::optional<NodeType> draw();

private:
    char m_search[64] = {};

    static const NodeType s_sources[];
    static const NodeType s_transformers[];
    static const NodeType s_sinks[];
};
