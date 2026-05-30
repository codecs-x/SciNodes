#pragma once
#include "../app/FileDialog.hpp"
#include "../core/DimensionalAnalyzer.hpp"
#include "../core/ISimSession.hpp"
#include "../core/NodeGraph.hpp"
#include "plots/ZoomState.hpp"
#include <string>
#include <unordered_map>

// -----------------------------------------------------------------------
// PlotPanel — oscilloscope-style waveform display.
// Only renders plots for sink nodes that have at least one incoming edge.
// Reads per-sink ring buffers from the active simulation session.
//
// DataLogger sinks also get an "Export CSV…" button that pops a native
// save dialog (via FileDialog) and writes the ring buffer to disk as
// `time,value` rows in chronological order.
// -----------------------------------------------------------------------
class PlotPanel {
public:
    // Render del contenido, sin ImGui::Begin/End (el host Area se encarga).
    // El panel consume la sesión vía la interfaz `ISimSession` para no
    // acoplarse al backend concreto (Scilab subprocess, call_scilab, etc).
    void drawContent(const NodeGraph& graph,
                     const scinodes::ISimSession& session);

    // ZoomState vive ahora en `plots/ZoomState.hpp` (etapa 6K.D).  Lo
    // mantenemos como alias acá para que los call sites que lo
    // referencian como `PlotPanel::ZoomState` (m_zoomStates, AppWindow,
    // etc.) sigan funcionando sin tocarlos.
    using ZoomState = scinodes::ui::plots::ZoomState;

private:
    // ---- Plot dispatch (etapa 6J.7) -----------------------------------
    // Cada sink-type que tiene visualización propia (FFT, Phase, Heatmap,
    // Histogram, Oscilloscope) implementa un drawer dedicado.  La cadena
    // `if (n->type == FFTAnalyzer) ... else if (PhasePortrait) ...` se
    // reemplazó por una tabla `NodeType → member fn` que se construye
    // estáticamente en `lookupPlotDrawer`.  Agregar un sink-type nuevo
    // con plot propio = un drawer + una entrada en la tabla.
    //
    // El contexto que comparten todos los drawers se empaqueta en
    // `PlotCtx` para que la signature sea uniforme.
    struct PlotCtx {
        const NodeInstance&                       n;
        const NodeGraph&                          graph;
        const scinodes::ISimSession&              bridge;
        const scinodes::DimensionalAnalysis&      analysis;
        float                                     plotW;
        float                                     plotH;
    };
    using PlotDrawFn = void (PlotPanel::*)(const PlotCtx&);

    void drawSpectrum    (const PlotCtx&);
    void drawPhase       (const PlotCtx&);
    void drawHeatmap     (const PlotCtx&);
    void drawHistogram   (const PlotCtx&);
    void drawOscilloscope(const PlotCtx&);
    void drawWaveDefault (const PlotCtx&);   // fallback para sinks sin drawer

    static PlotDrawFn lookupPlotDrawer(NodeType t);

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
