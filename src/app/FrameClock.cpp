#include "FrameClock.hpp"

namespace scinodes::app {

FrameClock::FrameClock()
    : m_lastTick(std::chrono::steady_clock::now()) {}

double FrameClock::tick() {
    const auto now = std::chrono::steady_clock::now();
    if (m_firstTick) {
        m_firstTick = false;
        m_lastTick  = now;
        return 0.0;
    }
    const double dt =
        std::chrono::duration<double>(now - m_lastTick).count();
    m_lastTick = now;
    return dt;
}

}  // namespace scinodes::app
