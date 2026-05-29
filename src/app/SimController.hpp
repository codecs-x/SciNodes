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
    void onParamEdit(int nodeId, int paramIdx, double value);

private:
    ScilabBridge& m_bridge;
    SimState      m_state = SimState::Idle;

    static constexpr float kSolverDt = 1.0f / 60.0f;
};

}  // namespace scinodes::app
