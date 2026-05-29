#include "SimController.hpp"

namespace scinodes::app {

// ===========================================================================
// Transiciones — la tabla efectiva de la máquina:
//
//     Idle/Error   ──run──>  Simulating
//     Simulating   ──pause─> Paused
//     Paused       ──resume─> Simulating
//     {Sim, Paus}  ──stop──> Idle
//     {*}          ──reset─> Idle  (+ clearBuffers para volver a t=0)
//
// pause()/resume() son no-op desde Idle/Error: protege contra clicks
// en estados que no aplican.
// ===========================================================================
void SimController::run(const NodeGraph& graph) {
    if (m_state == SimState::Paused) { resume(); return; }
    if (!m_bridge.reset(graph)) {
        m_state = SimState::Error;
        return;
    }
    if (!m_bridge.startSolverThread(kSolverDt)) {
        m_state = SimState::Error;
        return;
    }
    m_state = SimState::Simulating;
}

void SimController::pause() {
    if (m_state == SimState::Simulating) {
        m_bridge.setPaused(true);
        m_state = SimState::Paused;
    }
}

void SimController::resume() {
    if (m_state == SimState::Paused) {
        m_bridge.setPaused(false);
        m_state = SimState::Simulating;
    }
}

void SimController::stop() {
    m_bridge.stopSolverThread();
    m_bridge.stop();
    m_state = SimState::Idle;
}

void SimController::reset() {
    // Stop limpia el proceso pero conserva los datos.  Reset además
    // vacía los ring buffers para que las gráficas vuelvan a t=0.
    m_bridge.stopSolverThread();
    m_bridge.stop();
    m_bridge.clearBuffers();
    m_state = SimState::Idle;
}

void SimController::dispatch(SimAction action, const NodeGraph& graph) {
    switch (action) {
        case SimAction::Run:    run(graph);  break;
        case SimAction::Pause:  pause();     break;
        case SimAction::Resume: resume();    break;
        case SimAction::Stop:   stop();      break;
        case SimAction::Reset:  reset();     break;
        case SimAction::None:                break;
    }
}

int SimController::detectErrors() {
    if (isActive() && m_bridge.status() == ScilabBridge::Status::Error) {
        m_state = SimState::Error;
    }
    return (m_state == SimState::Error) ? m_bridge.offendingNodeId() : 0;
}

void SimController::onParamEdit(int nodeId, int paramIdx, double value) {
    if (isActive()) {
        m_bridge.sendParameter(nodeId, paramIdx, value);
    }
}

}  // namespace scinodes::app
