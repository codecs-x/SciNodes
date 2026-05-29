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
void SimController::run(const NodeGraph& graph, int dirtyRev) {
    if (m_state == SimState::Paused) { resume(); return; }
    // Si ya estaba simulando, parar el solver actual antes de reiniciar
    // — evita que dos threads del bridge coexistan momentáneamente
    // durante un re-run automático tras un cambio estructural.
    if (m_state == SimState::Simulating) {
        m_bridge.stopSolverThread();
        m_bridge.stop();
    }
    if (!m_bridge.reset(graph)) {
        m_state = SimState::Error;
        return;
    }
    if (!m_bridge.startSolverThread(kSolverDt)) {
        m_state = SimState::Error;
        return;
    }
    m_state = SimState::Simulating;
    // Registrar el dirtyRev del grafo en este run — isStale() lo
    // comparará contra el actual para detectar mutaciones posteriores.
    if (dirtyRev >= 0) m_lastRunRev = dirtyRev;
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

void SimController::resume(const NodeGraph& graph, int dirtyRev) {
    if (m_state != SimState::Paused) return;

    // Si la topología no cambió desde el último Run, basta con
    // desencongelar el solver thread (camino rápido y barato).
    const bool topologyChanged =
        (m_lastRunRev >= 0 && dirtyRev != m_lastRunRev);
    if (!topologyChanged) { resume(); return; }

    // Hot-reload: captura el (t, x) del driver vivo, regenera el
    // driver con el nuevo grafo sembrando esos valores y reanuda.
    // El usuario ve continuidad de tiempo + estados acumulados de
    // los nodos preexistentes; los nodos nuevos parten en su IC.
    CodegenSeedState seed;
    m_bridge.stopSolverThread();
    const bool captured = m_bridge.captureState(seed);
    if (!captured) {
        // Backend no soporta o falló la captura — caer a un run
        // limpio para no quedarnos pegados.  El usuario pierde t y
        // estados, pero al menos la edición se aplica.
        run(graph, dirtyRev);
        return;
    }
    if (!m_bridge.reset(graph, seed)) {
        m_state = SimState::Error;
        return;
    }
    if (!m_bridge.startSolverThread(kSolverDt)) {
        m_state = SimState::Error;
        return;
    }
    m_state = SimState::Simulating;
    m_lastRunRev = dirtyRev;
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

void SimController::dispatch(SimAction action, const NodeGraph& graph,
                             int dirtyRev) {
    switch (action) {
        case SimAction::Run:    run(graph, dirtyRev);           break;
        case SimAction::Pause:  pause();                        break;
        case SimAction::Resume: resume(graph, dirtyRev);        break;
        case SimAction::Stop:   stop();                         break;
        case SimAction::Reset:  reset();                        break;
        case SimAction::None:                                   break;
    }
}

int SimController::detectErrors() {
    if (isActive() && m_bridge.status() == ScilabBridge::Status::Error) {
        m_state = SimState::Error;
    }
    return (m_state == SimState::Error) ? m_bridge.offendingNodeId() : 0;
}

void SimController::onParamEdit(const std::vector<int>& path,
                                int paramIdx, double value) {
    if (isActive()) {
        m_bridge.sendParameter(path, paramIdx, value);
    }
}

void SimController::ensureUpToDate(int dirtyRev, const NodeGraph& graph) {
    // En Idle/Error mantenemos el baseline al día para que el primer
    // Run no se vea como "stale".  En Paused NO sincronizamos: el
    // delta entre m_lastRunRev y el dirtyRev actual es justamente lo
    // que hace que resume(graph, dirtyRev) decida hot-reload vs
    // descongelar.  Pisar el baseline aquí mataría ese path.
    if (m_state == SimState::Idle || m_state == SimState::Error) {
        m_lastRunRev = dirtyRev;
        return;
    }
    if (m_state == SimState::Paused) return;
    if (dirtyRev == m_lastRunRev) return;
    // El usuario tiene auto-rerun deshabilitado por preferencia
    // explícita (feedback_no_auto_rerun).  Ya no llamamos a run() —
    // simplemente dejamos el delta presente para que isStale() lo
    // refleje y, en la próxima pausa+resume, el hot-reload aplique.
    (void)graph;
}

}  // namespace scinodes::app
