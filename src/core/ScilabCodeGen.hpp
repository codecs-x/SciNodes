#pragma once
#include "NodeGraph.hpp"
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
};

class ScilabCodeGen {
public:
    // Top-level entry — see file header for the protocol it generates.
    static GeneratedPlan generate(const NodeGraph& graph);

    // True if a node type is currently emittable by this generator.
    static bool isSupported(NodeType t);
};
