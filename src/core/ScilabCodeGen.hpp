#pragma once
#include "IComputeBackend.hpp"
#include "NodeGraph.hpp"
#include <map>
#include <string>
#include <vector>

// -----------------------------------------------------------------------
// ScilabCodeGen — translates a NodeGraph into a Scilab driver script.
//
// The driver script implements an interactive REPL on stdin / stdout:
//
//   stdin commands               stdout responses
//   ───────────────              ─────────────────
//   step <t>                     STATE v1 v2 ... vN     (one per sink)
//   quit                         BYE                    (then exits)
//
// On startup, the driver prints "READY\n" so ScilabBridge can synchronise.
//
// Scope (v0.4 phase 2) — supports:
//   • Sources:      VoltageSource, CurrentSource, StepSignal, SineSignal,
//                   RampSignal
//   • Stateless:    Gain, Summation, Saturation, GearTransmission,
//                   InverseKinematics (2-input/2-output planar IK)
//   • Stateful:     Integrator, LowPassFilter, PIDController (PI only),
//                   DCMotorModel, Differentiator (filtered derivative),
//                   TransferFunction  (1st-order H(s)=b/(a0+a1·s)),
//                   TransferFunction2 (2nd-order with monic denom)
//                   — all integrated via Scilab ode("rk", ...)
//   • Sinks:        Oscilloscope, FFTAnalyzer, PhasePortrait,
//                   DataLogger, TerminalDisplay  (all record only)
//   • Custom:       JSON-loaded transformers / sources / sinks from
//                   CustomNodeRegistry. Stateless only; the descriptor's
//                   `expression` is substituted with u1..uN (input
//                   sources) and p_<name> (live param values).
//
// All NodeTypes in the registry are now emittable. Cyclic graphs work
// when every cycle passes through a pure-state block (Integrator /
// LowPassFilter / DCMotorModel / TransferFunction).
// -----------------------------------------------------------------------

// Each entry corresponds to one scalar field of the STATE line. A sink
// usually contributes exactly one channel; PhasePortrait contributes
// two (input 0 and input 1).
struct SinkChannel {
    int nodeId;
    int channel;
};

struct GeneratedPlan {
    std::string                 script;        // .sce text; empty if generation failed
    std::vector<SinkChannel>    sinkChannels;  // one entry per scalar in STATE
    std::string                 error;         // human-readable failure reason

    // Mapeo "path canónico" → id del nodo en el grafo aplanado.  El path
    // es la lista [sg1_id_en_padre, sg2_id_en_sg1, ..., original_node_id].
    // Para un nodo top-level, path = [nodeId].  Permite que la UI envíe
    // `sendParameter(path, idx, value)` y el bridge traduzca al `flatId`
    // correcto aunque el flatten haya reasignado ids al expandir SubGraphs.
    std::map<std::vector<int>, int> idForPath;

    // Layout del vector de estado `x` que el driver maneja: una entrada
    // por slot, con (nodeId, slotIdx) para identificar de qué nodo viene
    // y cuál es el sub-slot dentro de ese nodo (la mayoría tienen 1 slot;
    // PIDController, TF2 y DCMotorModel tienen 2).  Lo usa el bridge para
    // (a) parsear el `dump_state` que el driver imprime al pausar, y
    // (b) reconstruir el seed para regenerar el driver con los valores
    // de cada nodo después de una edición del grafo durante Paused.
    std::vector<std::pair<int,int>> stateLayout;
};

// Equivalente estructurado de GeneratedPlan, pensado para los backends
// in-process que consumen IComputeBackend. La spec separa "datos" (cuerpo
// de dynamics, expresiones, parámetros) del "protocolo" (que en el
// subprocess era el while-loop con `step` y `STATE`).
struct GeneratedSpec {
    scinodes::BackendPrepareSpec spec;
    std::string                  error;        // vacío si OK
};

// Seed para "hot-reload" de la simulación: cuando el usuario pausa,
// edita el grafo y reanuda, el bridge pasa este seed al codegen y la
// nueva sesión arranca con los estados acumulados (y el `t` vigente)
// en vez de t=0 con ICs por defecto.  Cualquier slot del nuevo plan
// cuya (nodeId, slotIdx) no esté en el mapa se queda en su IC
// original (nodos nuevos parten en 0).
struct CodegenSeedState {
    double t = 0.0;                                       // t inicial
    std::map<std::pair<int,int>, double> values;          // (nodeId, slot) → x
};

class ScilabCodeGen {
public:
    // Camino histórico — emite el script .sce con bucle REPL para el
    // bridge basado en subproceso.  Si `seed` es no-null, sus valores
    // sustituyen las ICs y el `t_prev` inicial del driver.
    static GeneratedPlan generate(const NodeGraph& graph,
                                  const CodegenSeedState* seed = nullptr);

    // Camino nuevo — emite una BackendPrepareSpec consumible por cualquier
    // implementación de IComputeBackend (call_scilab, mock, etc.).  Reusa
    // toda la lógica de planeación de nodos del path anterior.
    static GeneratedSpec generateSpec(const NodeGraph& graph);

    // True if a node type is currently emittable by this generator.
    static bool isSupported(NodeType t);
};
