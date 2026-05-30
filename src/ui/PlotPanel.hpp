#pragma once
#include "../app/FileDialog.hpp"
#include "../core/NodeGraph.hpp"
#include "../core/ScilabBridge.hpp"
#include <limits>
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
    // Render del contenido, sin ImGui::Begin/End (el host Area se encarga).
    void drawContent(const NodeGraph& graph, const ScilabBridge& bridge);

    // Estado de zoom Y por sumidero.  `manual = false` → auto-escala
    // \"expand-only\": el rango visible solo crece cuando aparecen
    // valores fuera del rango actual; nunca se encoje
    // automáticamente, así la gráfica no oscila cuando los datos
    // viejos del ring buffer caen fuera del rango estable.  Doble-click
    // recentra al rango actual y vuelve a auto.  `manual = true` →
    // usa (center, range) explícitos controlados por rueda y drag.
    struct ZoomState {
        bool  manual = false;
        float center = 0.0f;
        float range  = 1.0f;
        // Auto-bounds históricos (expand-only).  -inf/+inf = aún sin
        // datos.  Reset al hacer doble-click sobre el plot.
        float autoMin = +std::numeric_limits<float>::infinity();
        float autoMax = -std::numeric_limits<float>::infinity();
        // Pan horizontal: tiempo absoluto del borde derecho de la
        // cámara, en segundos.
        //   viewRightSec < 0   → \"follow latest\" (default): el borde
        //                        derecho sigue al último sample.
        //   viewRightSec >= 0  → bloqueado en ese tiempo absoluto;
        //                        la cámara muestra historia fija.
        // Se controla con left-click + drag horizontal sobre el plot.
        float viewRightSec = -1.0f;
        // Para plots 2-D (PhasePortrait, Heatmap) — factor de zoom
        // alrededor del centro auto.  1.0 = como auto-fit; >1 = zoom
        // in (rango visible más pequeño); <1 = zoom out.
        float scale  = 1.0f;
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
