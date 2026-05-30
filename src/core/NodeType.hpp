#pragma once
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

// -----------------------------------------------------------------------
// NodeType — one enum value per node type in the grammar
// -----------------------------------------------------------------------
enum class NodeType {
    // Sources (grammar terminal-left)
    VoltageSource,
    CurrentSource,
    StepSignal,
    SineSignal,
    RampSignal,
    DesignTemplate,        // Phase 2 — design requirements bundle (v0.8)

    // Transformers (grammar non-terminal)
    Gain,
    Summation,
    Integrator,
    Differentiator,
    LowPassFilter,
    PIDController,
    TransferFunction,
    TransferFunction2,
    Saturation,
    DCMotorModel,
    GearTransmission,
    InverseKinematics,
    PMSMSizing,            // Phase 2 — classical sizing equation (v0.8)
    IPMSizing,             // Phase 2 — IPM with reluctance-torque boost (v0.8)
    BLDCSizing,            // Phase 2 — BLDC with trapezoidal factor (v0.8)
    PMSMElectromagnetic,   // Phase 2 — Ke, Ld=Lq, V_rms, T_cog (v0.8)
    AirgapFluxDensity,     // Phase 2 — B_g(t) waveform at stator point (v0.8)
    PMSMEfficiency,        // Phase 2 — η from (T, ω, Ke) + loss params (v0.8)

    // Stage v0.9 — Thermal Network: losses + lumped RC nodes.
    JouleLoss,             // Phase 1 — copper loss from (T, Ke)         (v0.9)
    CoreLoss,              // Phase 1 — iron loss from (ω, B_g)          (v0.9)
    MechanicalLoss,        // Phase 1 — friction + windage from ω        (v0.9)
    ThermalMass,           // Phase 1 — single-node RC thermal           (v0.9)
    ThermalNode,           // Phase 3 — pure capacitance, 4 heat inputs  (v0.9)
    ThermalResistance,     // Phase 3 — dual-output q_HtoC / q_CtoH      (v0.9)
    CoolingSystem,         // Phase 4 — fan / water / ambient knobs      (v0.9)
    ConvectiveCooling,     // Phase 4 — h(flow)·ΔT cooling block         (v0.9)

    // Stage v1.0 — Structural & NVH.
    MaxwellForce,          // Phase 1 — radial Maxwell pressure σ=B²/2μ₀ (v1.0)
    ModalFrequency,        // Phase 1 — thin-ring mode-m natural freq    (v1.0)
    TolerancePerturbator,  // Phase 3 — uniform-random noise within ±h   (v1.0)

    // Sinks (grammar terminal-right)
    Oscilloscope,
    FFTAnalyzer,
    PhasePortrait,
    DataLogger,
    TerminalDisplay,
    View3DSink,
    View3DThermalSink,     // Stage v0.9 — tints the procedural mesh
    View3DDeformationSink, // Stage v1.0 — animated mode-shape overlay
    HeatmapSink,           // Phase 2 — 2-D heatmap (x, y, value) (v0.8)
    DistributionSink,      // Stage v1.0 — histogram of accumulated samples

    // SuperBlock — recursive grouping ("paréntesis" en la gramática).
    // Un `SubGraph` agrupa otros nodos en un grafo hijo; los puertos
    // visibles desde fuera se materializan en su interior con los
    // stubs `SubGraphInput` (signal entering) y `SubGraphOutput`
    // (signal leaving).  El codegen aplana cada SubGraph antes de
    // emitir Scilab, así la simulación es indistinguible de la
    // versión sin agrupación.
    SubGraph,
    SubGraphInput,    // dentro del SubGraph: 0 inputs, 1 output (= la señal del puerto i del padre)
    SubGraphOutput,   // dentro del SubGraph: 1 input,  0 outputs (= la señal hacia el puerto j del padre)

    // Sentinel for JSON-loaded node types — see CustomNodeRegistry.
    // The actual descriptor lives in NodeInstance::customType.
    Custom,
};

// Device se comporta como Transformer en las reglas gramaticales (puede
// recibir y emitir señales en el medio de una cadena), pero está
// diferenciado para que el resto del sistema (UI, asset binding,
// outliner) pueda tratarlo como "dispositivo físico con geometría
// asignable".  Ver doc/geometry-contracts-design.md.
enum class NodeCategory { Source, Transformer, Device, Sink };

struct ParamDef {
    std::string name;
    double      defaultValue;
    std::string unit;       // display only
};

struct NodeDef {
    NodeType     type;
    NodeCategory category;
    std::string  label;          // display name
    std::string  description;    // tooltip text
    int          inputPorts;     // 0 for Sources
    int          outputPorts;    // 0 for Sinks
    std::vector<ParamDef> params;

    // Semántica de estado para el solver — centraliza lo que antes vivía
    // como switches sobre NodeType en ScilabCodeGen.cpp:
    //
    //   stateWidth  : número de slots reservados en el vector `x` de la
    //                 dinámica.  0 significa "stateless" (algebraic).
    //   isPureState : la salida del nodo ES x(slot), sin feedthrough
    //                 algebraico desde la entrada del mismo paso — eso
    //                 le permite romper ciclos algebraicos en feedback
    //                 loops.  Implica stateWidth > 0.  PIDController y
    //                 Differentiator tienen estado PERO feedthrough
    //                 directo, así que no son pure-state.
    //   stateOnlyPorts : puertos cuya señal afecta SOLO la derivada del
    //                 estado, no la salida instantánea (p. ej. anti-windup
    //                 back-calculation del PID en port 1).  Permiten
    //                 cerrar ciclos PID → Saturation → PID:port1 sin
    //                 generar bucle algebraico.
    //
    // CustomNodeRegistry rellena estos campos en su descriptor sintetizado
    // — por defecto custom nodes son stateless (stateWidth = 0).
    int                  stateWidth     = 0;
    bool                 isPureState    = false;
    std::vector<int>     stateOnlyPorts;
};

// Returns the full registry of all node definitions (indexed by NodeType).
const std::unordered_map<NodeType, NodeDef>& nodeRegistry();

// Convenience helpers
NodeCategory categoryOf(NodeType t);
const char*  labelOf(NodeType t);

// "Pure-state" — la salida del nodo es la variable de estado integrada,
// SIN feedthrough algebraico desde la entrada del mismo paso.  Estos
// nodos rompen lazos algebraicos: el ciclo cierra a través de la
// integración, no instantáneamente.  Lo usa ScilabCodeGen::topoSort
// para permitir feedback loops y Canvas::autoLayout para asignar
// niveles de profundidad a grafos con realimentación.
//
// Wrapper sobre NodeDef::isPureState — lookup en el registry.  Se
// mantiene la forma libre por compatibilidad con call sites que sólo
// tienen el NodeType (no la NodeInstance completa).
bool isPureStateNode(NodeType t);

// Predicados de SubGraph — reemplazan las cadenas
// `t == NodeType::SubGraphInput || t == NodeType::SubGraphOutput` y
// `t == NodeType::SubGraph` repetidas en 32 sitios del código.  El
// nombre semántico es más legible y centraliza la definición.
constexpr bool isSubGraphStub(NodeType t) {
    return t == NodeType::SubGraphInput || t == NodeType::SubGraphOutput;
}
constexpr bool isSubGraphContainer(NodeType t) {
    return t == NodeType::SubGraph;
}

// Stable enum-name conversion for serialization (e.g. "VoltageSource").
// Distinct from labelOf() which returns the human display label ("Voltage Source").
const char*               typeName(NodeType t);
std::optional<NodeType>   typeFromName(const std::string& name);
