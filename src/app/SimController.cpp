#include "SimController.hpp"
#include "../core/NodeType.hpp"   // NodeType::SubGraph

namespace scinodes::app {

// ---------------------------------------------------------------------------
// Fingerprint recursivo del grafo.  Para cada nivel jerárquico (top y
// cada SubGraph anidado, identificado por su path desde la raíz),
// volcamos:
//   - el set de nodeId's presentes
//   - el set de aristas (fromAttrId, toAttrId)
//
// El path se incluye en cada entrada para distinguir, p. ej., un mismo
// nodeId entre el top-level y un SubGraph que aún no ha re-encapsulado
// sus ids.  AppWindow usa isAdditive() para decidir si la edición fue
// aditiva (todo lo viejo sigue + cosas nuevas) o destructiva (algo
// desapareció) — la regla semántica viene de la nota del usuario sobre
// identidad del sistema vs. perturbación.
// ---------------------------------------------------------------------------
static void fingerprintInto(GraphFingerprint& sig, const NodeGraph& g,
                            std::vector<int> path) {
    for (const auto& n : g.nodes())
        sig.nodes.insert({ path, n.id });
    for (const auto& e : g.edges())
        sig.edges.insert(std::make_tuple(path, e.fromAttrId, e.toAttrId));
    for (const auto& n : g.nodes()) {
        if (isSubGraphContainer(n.type)) {
            const NodeGraph* child = g.subGraphOf(n.id);
            if (child) {
                auto p = path; p.push_back(n.id);
                fingerprintInto(sig, *child, p);
            }
        }
    }
}

GraphFingerprint fingerprintGraph(const NodeGraph& g) {
    GraphFingerprint sig;
    fingerprintInto(sig, g, {});
    return sig;
}

bool isAdditive(const GraphFingerprint& base, const GraphFingerprint& cur) {
    for (const auto& n : base.nodes)
        if (!cur.nodes.count(n)) return false;
    for (const auto& e : base.edges)
        if (!cur.edges.count(e)) return false;
    return true;
}

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
    // run() siempre arranca desde t=0.  Antes había un atajo
    // `if Paused → resume()` "defensivo", pero rompía el flujo del
    // modal "Reiniciar desde t=0" cuando el usuario confirmaba que
    // quería reiniciar tras un cambio destructivo: la llamada caía a
    // resume() y conservaba t.  El botón Run del StatusBar solo
    // aparece en Idle/Error, así que llegar acá con state==Paused
    // ahora es siempre una intención de restart explícita.
    if (m_state == SimState::Simulating ||
        m_state == SimState::Paused) {
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
    // Baseline para el realTimeFactor.
    m_wallT0    = std::chrono::steady_clock::now();
    m_simT0     = m_bridge.time();
    m_rtfActive = true;
    // Baseline estructural para distinguir cambios aditivos vs
    // destructivos en el próximo Resume.
    m_baselineSig = fingerprintGraph(graph);
    m_hasBaseline = true;
}

void SimController::pause() {
    if (m_state == SimState::Simulating) {
        m_bridge.setPaused(true);
        m_state = SimState::Paused;
        m_rtfActive = false;
    }
}

void SimController::resume() {
    if (m_state == SimState::Paused) {
        m_bridge.setPaused(false);
        m_state = SimState::Simulating;
        m_wallT0    = std::chrono::steady_clock::now();
        m_simT0     = m_bridge.time();
        m_rtfActive = true;
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
    //
    // Esta ruta asume que la edición fue ADITIVA — AppWindow ya
    // descartó la otra rama (cambios destructivos) vía la modal de
    // confirmación que llama a run() en lugar de resume() en ese caso.
    CodegenSeedState seed;
    m_bridge.stopSolverThread();
    const bool captured = m_bridge.captureState(seed);
    if (!captured) {
        // Backend no soporta o falló la captura — caer a un run
        // limpio para no quedarnos pegados.
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
    // Refrescar baseline: el grafo actual es el nuevo "estable" para
    // futuras decisiones aditivo-vs-destructivo.
    m_baselineSig = fingerprintGraph(graph);
    m_hasBaseline = true;
    m_lastRunRev = dirtyRev;
    m_wallT0    = std::chrono::steady_clock::now();
    m_simT0     = m_bridge.time();
    m_rtfActive = true;
}

void SimController::stop() {
    m_bridge.stopSolverThread();
    m_bridge.stop();
    m_state = SimState::Idle;
    m_rtfActive = false;
    m_hasBaseline = false;
}

bool SimController::wouldBeDestructiveResume(const NodeGraph& g) const {
    if (m_state != SimState::Paused) return false;
    if (!m_hasBaseline) return false;
    return !isAdditive(m_baselineSig, fingerprintGraph(g));
}

void SimController::rebaselineForRefactor(const NodeGraph& g) {
    if (!m_hasBaseline) return;
    m_baselineSig = fingerprintGraph(g);
}

float SimController::realTimeFactor() const {
    if (!m_rtfActive) return 1.0f;
    // La sesión expone explícitamente cuándo está produciendo (primer
    // step exitoso tras spawn).  Mientras NO produzca, rebaseamos el
    // wall al "ahora" para no contar el intervalo muerto del boot.
    // Antes inferíamos esto comparando bridge.time() con simT0 — eso
    // dejaba a SimController leyendo el estado del backend de Scilab
    // para responder una pregunta de su propio dominio.  Pasarlo
    // vía ISimSession::isProducing() saca el acoplamiento: cualquier
    // backend (Scilab subprocess, call_scilab, otro) decide cuándo
    // dice "ya estoy produciendo" según su propio modelo.
    if (!m_bridge.isProducing()) {
        m_wallT0 = std::chrono::steady_clock::now();
        return 1.0f;
    }
    const auto wall = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - m_wallT0).count();
    if (wall < 0.5) return 1.0f;
    return static_cast<float>((m_bridge.time() - m_simT0) / wall);
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
    if (isActive() && m_bridge.status() == scinodes::ISimSession::Status::Error) {
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
