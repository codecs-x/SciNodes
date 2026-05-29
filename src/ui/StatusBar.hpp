#pragma once
#include "../app/FrameClock.hpp"

// -----------------------------------------------------------------------
// SimState — drives which controls the StatusBar exposes.
// Matches the state machine in doc/analysis.md §A.3.5 (collapsed: we keep
// Editing+Valid in the canvas/grammar layer, and only model the run/pause
// lifecycle here).
// -----------------------------------------------------------------------
enum class SimState {
    Idle,        // not running (grammar may or may not be valid)
    Simulating,  // Scilab bridge is being stepped each frame
    Paused,      // bridge alive but frame loop is not calling step()
    Error        // bridge died or generator rejected the graph
};

// What the user requested by clicking a button this frame.
enum class SimAction {
    None,
    Run,         // Idle  → Simulating  (regenerate driver, restart bridge)
    Pause,       // Simulating → Paused
    Resume,      // Paused → Simulating
    Stop,        // Simulating/Paused → Idle (kill bridge)
    Reset        // any → Idle (kill bridge, time back to 0)
};

// -----------------------------------------------------------------------
// StatusBar — bottom strip with simulation controls + graph stats.
// -----------------------------------------------------------------------
class StatusBar {
public:
    SimAction draw(int nodeCount, int edgeCount,
                   const char* grammarLabel,
                   SimState state,
                   bool grammarValid,
                   float simTime,
                   const char* lastError /* may be null */,
                   bool stale = false);

    // Telemetría del frame loop (Fase D del refactor). AppWindow llama a
    // setFrameStats() al final de cada frame; la barra muestra
    // \"in/up/rd/pr\" con los ms de cada etapa cuando profiling==true.
    void setFrameStats(const scinodes::app::FrameStats& s) { m_stats = s; }
    void setProfilingEnabled(bool on) { m_profiling = on; }
    bool profilingEnabled() const { return m_profiling; }

private:
    scinodes::app::FrameStats m_stats;
    bool                      m_profiling = true;  // cheap; on by default
};
