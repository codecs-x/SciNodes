#pragma once
#include "../core/SimTypes.hpp"   // SimState, SimAction
#include "../app/FrameClock.hpp"

// SimState y SimAction viven ahora en `core/SimTypes.hpp` — este header
// los re-expone para que el código de UI legado pueda seguir incluyendo
// `StatusBar.hpp` sin enterarse del movimiento.  SimController dejó de
// depender de este header como consecuencia.

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
                   bool stale = false,
                   float realTimeFactor = 1.0f);

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
