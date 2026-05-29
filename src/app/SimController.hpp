#pragma once

#include "../core/NodeGraph.hpp"
#include "../core/ScilabBridge.hpp"
#include "../ui/StatusBar.hpp"   // SimState, SimAction

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

class SimController {
public:
    explicit SimController(ScilabBridge& bridge) : m_bridge(bridge) {}

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

private:
    ScilabBridge& m_bridge;
    SimState      m_state = SimState::Idle;

    int  m_lastRunRev = -1;     // dirtyRev observado en el último run().
                                 // -1 = aún no se ha llamado run().

    static constexpr float kSolverDt = 1.0f / 60.0f;
};

}  // namespace scinodes::app
