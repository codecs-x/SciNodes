#pragma once
#include <chrono>
#include <utility>

namespace scinodes::app {

// ---------------------------------------------------------------------------
// FrameClock + FrameStats — desacopla la medición del frame loop arquitectónico
// (Gregory, Cap.~8 "Game Engine Architecture"). El loop principal de AppWindow
// queda como cuatro fases (input, update, render, present), cada una medida
// independientemente. measureMs(fn) envuelve cualquier callable y devuelve los
// milisegundos transcurridos sin acoplarse al tipo de retorno de fn — útil
// para inyectar mediciones sin modificar la firma de cada método.
// ---------------------------------------------------------------------------

class FrameClock {
public:
    FrameClock();

    // Avanza el reloj un frame; retorna delta-time en segundos desde el
    // último tick (o 0.0 en el primer tick).  Usa steady_clock para
    // inmunidad ante ajustes del reloj de sistema (NTP, DST).
    double tick();

private:
    std::chrono::steady_clock::time_point m_lastTick;
    bool                                  m_firstTick = true;
};

struct FrameStats {
    double inputMs   = 0.0;
    double updateMs  = 0.0;
    double renderMs  = 0.0;
    double presentMs = 0.0;

    double totalMs() const {
        return inputMs + updateMs + renderMs + presentMs;
    }
};

// Mide los milisegundos que tarda `fn` y los retorna.  fn() debe ser
// invocable sin argumentos (puede ser una lambda con captura).  El retorno
// de fn se descarta — sólo nos interesa el tiempo.
template <typename Fn>
double measureMs(Fn&& fn) {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    std::forward<Fn>(fn)();
    return std::chrono::duration<double, std::milli>(clock::now() - t0).count();
}

}  // namespace scinodes::app
