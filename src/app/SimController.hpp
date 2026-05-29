#pragma once

#include "../core/ISimSession.hpp"
#include "../core/NodeGraph.hpp"
#include "../core/ScilabCodeGen.hpp"   // CodegenSeedState
#include "../core/SimTypes.hpp"         // SimState, SimAction (dominio, no UI)

#include <chrono>
#include <set>
#include <tuple>
#include <vector>

// -----------------------------------------------------------------------------
// SimController — máquina de estados de la simulación.
//
// Saca de AppWindow toda la lógica de start/pause/resume/stop/reset
// y la detección de errores del bridge.  Pattern: Use Case (Martin
// Clean Architecture Cap 16) — un orquestador con responsabilidad
// única (gobernar el ciclo de vida de una simulación).
//
// Dependencias inyectadas:
//   - ScilabBridge& : el subsistema de cómputo numérico (no owning).
//
// El NodeGraph se pasa por parámetro a `run()` para no acoplar el
// controller al canvas — sigue siendo testeable sin GUI.
// -----------------------------------------------------------------------------
namespace scinodes::app {

// Fingerprint recursivo del grafo — captura nodos y aristas en cada
// nivel (top-level + cada SubGraph anidado por path).  Sirve para
// decidir si una edición del usuario es ADITIVA (todo lo que estaba
// sigue estando + posiblemente cosas nuevas) o DESTRUCTIVA (algo se
// removió).  La regla semántica viene del usuario: una desconexión
// cambia la identidad del sistema → restart desde t=0; una conexión
// nueva a un puerto antes libre = perturbación sobre el sistema vivo
// → preserva t.
struct GraphFingerprint {
    // `path` = cadena de IDs de SubGraph desde la raíz hasta el grafo
    // donde vive el nodo/arista.  {} = top-level.
    std::set<std::pair<std::vector<int>, int>> nodes;
    std::set<std::tuple<std::vector<int>, int, int>> edges;
};
GraphFingerprint fingerprintGraph(const NodeGraph& g);
bool             isAdditive(const GraphFingerprint& base,
                            const GraphFingerprint& cur);

class SimController {
public:
    explicit SimController(scinodes::ISimSession& session)
        : m_bridge(session) {}

    // ---- Transiciones de estado ------------------------------------------
    // Si se pasa `dirtyRev` distinto del default, queda como
    // baseline para isStale: cualquier cambio posterior del grafo
    // (topología) hará que isStale(currentRev) == true mientras la
    // simulación siga corriendo.
    void run(const NodeGraph& graph, int dirtyRev = -1);
    void pause();                       // congelar (mantiene bridge vivo)
    // Reanudar.  Si la topología cambió desde el último run (dirtyRev
    // diferente del baseline), hace hot-reload: captura estado, regenera
    // el driver con el nuevo grafo y siembra los estados acumulados
    // junto con `t`.  Resultado: la sim sigue desde donde se quedó,
    // con los nuevos nodos/cables aplicados.
    void resume(const NodeGraph& graph, int dirtyRev);
    // Sobrecarga sin grafo: usa para los flujos donde la topología
    // sabemos que no cambió (p. ej. tests).  Equivale a resume con
    // dirtyRev = m_lastRunRev.
    void resume();
    void stop();                        // mata el proceso Scilab
    void reset();                       // stop + clearBuffers (t=0 sin trazas)

    // Despacha la acción que devuelve StatusBar::draw().  El grafo se
    // necesita solo para SimAction::Run.
    void dispatch(SimAction action, const NodeGraph& graph,
                  int dirtyRev = -1);

    // ¿La sim activa está corriendo un plan viejo?  El "baseline" es
    // el dirtyRev observado cuando se llamó a run() — si el grafo
    // mutó desde entonces y la sim sigue activa, devuelve true.  La
    // UI usa este flag para mostrar un aviso visual en el StatusBar:
    // los cambios estructurales del usuario no se ven reflejados en
    // el plot hasta que pulse Run de nuevo (deliberadamente — el
    // auto-rerun queda fuera por preferencia explícita del usuario).
    bool isStale(int currentDirtyRev) const {
        return (m_state == SimState::Simulating ||
                m_state == SimState::Paused) &&
               m_lastRunRev >= 0 &&
               currentDirtyRev != m_lastRunRev;
    }

    // ---- Lectura de estado -----------------------------------------------
    SimState state() const { return m_state; }
    bool isActive() const {
        return m_state == SimState::Simulating ||
               m_state == SimState::Paused;
    }

    // Llamado una vez por frame al inicio de la fase de update.  Detecta
    // si el bridge cayó a Error mientras corría y promueve el estado.
    // Devuelve el id del nodo culpable (0 si no hay error).
    int detectErrors();

    // Forward de edits en vivo desde el canvas.  Solo se reenvía al
    // bridge mientras la simulación está activa; idle/error/stopped
    // descartan silenciosamente.
    //
    // El `path` es la cadena [sg1_id, ..., nodeId_en_active] que el
    // NodeCanvas construye con `pathFor()`.  Bridge usa idForPath para
    // traducir a flatId — esencial cuando el nodo vive dentro de un
    // SubGraph y su id fue reasignado durante el flatten.
    void onParamEdit(const std::vector<int>& path, int paramIdx, double value);

    // Llamado cada frame con la `dirtyRevision()` actual del NodeCanvas.
    // Si el sim está activo (Simulating) y la revisión cambió respecto a
    // la última corrida, dispara `run(graph)` para regenerar el plan.
    // Sin esto, tras un Ctrl+G / Ctrl+V / Delete el bridge queda con el
    // plan viejo y el plot muestra datos rancios.
    void ensureUpToDate(int dirtyRev, const NodeGraph& graph);

    // Factor tiempo-real medido: (simT - simT0) / (wallT - wallT0) desde
    // el último Run/Resume.  Útil para ver si la sim corre a la par del
    // reloj externo (≈1.00) o se queda atrás (< 1.00 = la sim lagea).
    // Devuelve 1.0 si no hay sim activa o ha corrido < 0.5 s de wall.
    float realTimeFactor() const;

    // Lee la baseline si el sim está en Paused/Simulating con un
    // run/resume previo.  AppWindow lo usa para interceptar el Resume
    // y mostrar modal de confirmación antes de hacer hot-reload —
    // ver SimController docstring para la regla semántica.
    bool wouldBeDestructiveResume(const NodeGraph& g) const;

    // Refresca la baseline al fingerprint del grafo actual SIN parar
    // ni reiniciar la sim.  Lo usa AppWindow tras un refactor
    // estructural (encapsular, desempacar) que cambia la jerarquía
    // visible pero NO las dinámicas aplanadas — sin esto el siguiente
    // Resume se vería como destructivo (los nodos pasaron de top-level
    // a vivir dentro del SubGraph y el fingerprint estructural los ve
    // como "removidos").
    void rebaselineForRefactor(const NodeGraph& g);

private:
    scinodes::ISimSession& m_bridge;
    SimState               m_state = SimState::Idle;

    int  m_lastRunRev = -1;     // dirtyRev observado en el último run().
                                 // -1 = aún no se ha llamado run().

    // Reloj de pared y tiempo simulado al iniciar/reanudar el run
    // actual, para calcular el realTimeFactor.  m_wallT0 es mutable
    // porque realTimeFactor() lo reajusta mientras la sim aún no haya
    // avanzado desde el seed — eso esconde el costo del primer step
    // tras un hot-reload (el spawn de Scilab + carga inicial del
    // script toma ~1-2 s; sin este reajuste, el RT baja a rojo durante
    // esa ventana aunque la sim luego corra al ritmo correcto).
    mutable std::chrono::steady_clock::time_point m_wallT0{};
    float                                          m_simT0 = 0.0f;
    bool                                           m_rtfActive = false;

    // Fingerprint del grafo cuando arrancó la sim (run / hot-reload
    // exitoso).  Sirve para decidir si la próxima reanudación es
    // aditiva o destructiva.  Vacío hasta el primer run().
    GraphFingerprint m_baselineSig;
    bool             m_hasBaseline = false;

    static constexpr float kSolverDt = 1.0f / 60.0f;
};

}  // namespace scinodes::app
