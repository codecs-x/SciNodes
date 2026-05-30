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
};

// Equivalente estructurado de GeneratedPlan, pensado para los backends
// in-process que consumen IComputeBackend. La spec separa "datos" (cuerpo
// de dynamics, expresiones, parámetros) del "protocolo" (que en el
// subprocess era el while-loop con `step` y `STATE`).
struct GeneratedSpec {
    scinodes::BackendPrepareSpec spec;
    std::string                  error;        // vacío si OK
};

class ScilabCodeGen {
public:
    // Camino histórico — emite el script .sce con bucle REPL para el
    // bridge basado en subproceso.
    static GeneratedPlan generate(const NodeGraph& graph);

    // Camino nuevo — emite una BackendPrepareSpec consumible por cualquier
    // implementación de IComputeBackend (call_scilab, mock, etc.).  Reusa
    // toda la lógica de planeación de nodos del path anterior.
    static GeneratedSpec generateSpec(const NodeGraph& graph);

    // True if a node type is currently emittable by this generator.
    static bool isSupported(NodeType t);
};
