#pragma once
#include "../app/FileDialog.hpp"
#include "../core/NodeGraph.hpp"
#include "../core/ScilabBridge.hpp"
#include <string>
#include <unordered_map>

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

    // Estado de zoom Y por sumidero.  `manual = false` → auto-escala
    // basado en data + minRange floor (lo de toda la vida).  `manual =
    // true` → usa (center, range) explícitos, que el usuario controla
    // con la rueda y arrastrando.  Doble-click vuelve a auto.
    struct ZoomState {
        bool  manual = false;
        float center = 0.0f;
        float range  = 1.0f;
    };

private:
    // ---- CSV export ---------------------------------------------------
    // Polls m_exportDialog every frame. While a sink id is "pending",
    // any returned path is written using values captured at click time.
    FileDialog  m_exportDialog;
    int         m_pendingExportSinkId = 0;
    std::string m_pendingExportLabel;
    std::string m_exportStatus;       // last result/error, shown briefly

    // Cache de zoom por nodo sumidero.  No se limpia cuando un nodo
    // desaparece — la entrada huérfana ocupa poco y se desecha al
    // cerrar SciNodes.
    std::unordered_map<int, ZoomState> m_zoomStates;
};
