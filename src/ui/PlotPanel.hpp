#pragma once
#include "../app/FileDialog.hpp"
#include "../core/NodeGraph.hpp"
#include "../core/ScilabBridge.hpp"
#include <string>

// -----------------------------------------------------------------------
// PlotPanel — oscilloscope-style waveform display.
// Only renders plots for sink nodes that have at least one incoming edge.
// Reads per-sink ring buffers from ScilabBridge.
//
// DataLogger sinks also get an "Export CSV…" button that pops a native
// save dialog (via FileDialog) and writes the ring buffer to disk as
// `time,value` rows in chronological order.
// -----------------------------------------------------------------------
class PlotPanel {
public:
    void draw(const NodeGraph& graph, const ScilabBridge& bridge);

private:
    // ---- CSV export ---------------------------------------------------
    // Polls m_exportDialog every frame. While a sink id is "pending",
    // any returned path is written using values captured at click time.
    FileDialog  m_exportDialog;
    int         m_pendingExportSinkId = 0;
    std::string m_pendingExportLabel;
    std::string m_exportStatus;       // last result/error, shown briefly
};
