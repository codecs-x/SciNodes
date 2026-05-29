#pragma once
#include "../core/NodeGraph.hpp"
#include "../core/ScilabBridge.hpp"

// -----------------------------------------------------------------------
// PlotPanel — oscilloscope-style waveform display.
// Only renders plots for sink nodes that have at least one incoming edge.
// Reads per-sink ring buffers from ScilabBridge.
// -----------------------------------------------------------------------
class PlotPanel {
public:
    void draw(const NodeGraph& graph, const ScilabBridge& bridge);
};
