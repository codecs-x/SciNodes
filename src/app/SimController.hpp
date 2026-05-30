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
    void run(const NodeGraph& graph);   // (re)inicia: reset+startSolver
    void pause();                       // congelar (mantiene bridge vivo)
    void resume();                      // descongelar
    void stop();                        // mata el proceso Scilab
    void reset();                       // stop + clearBuffers (t=0 sin trazas)

    // Despacha la acción que devuelve StatusBar::draw().  El grafo se
    // necesita solo para SimAction::Run.
    void dispatch(SimAction action, const NodeGraph& graph);

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

    int  m_lastRunRev = -1;     // dirtyRev observado en el último run()

    static constexpr float kSolverDt = 1.0f / 60.0f;
};

}  // namespace scinodes::app
