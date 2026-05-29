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
//   • Stateless:    Gain, Summation, Saturation, GearTransmission
//   • Stateful:     Integrator, LowPassFilter, PIDController (PI only),
//                   DCMotorModel  — integrated via Scilab ode("rk", ...)
//   • Sinks:        Oscilloscope, FFTAnalyzer, PhasePortrait,
//                   DataLogger, TerminalDisplay  (all record only)
//
// Still unsupported (clear error on generate): Differentiator,
// TransferFunction, InverseKinematics, and cyclic graphs (no [...]
// grammar support yet).
// -----------------------------------------------------------------------

struct GeneratedPlan {
    std::string      script;     // .sce text; empty if generation failed
    std::vector<int> sinkOrder;  // node ids of sinks, in the order their
                                 // values appear in the STATE line
    std::string      error;      // human-readable failure reason
};

class ScilabCodeGen {
public:
    // Top-level entry — see file header for the protocol it generates.
    static GeneratedPlan generate(const NodeGraph& graph);

    // True if a node type is currently emittable by this generator.
    static bool isSupported(NodeType t);
};
