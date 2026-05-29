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
bool isPureStateNode(NodeType t);

// Stable enum-name conversion for serialization (e.g. "VoltageSource").
// Distinct from labelOf() which returns the human display label ("Voltage Source").
const char*               typeName(NodeType t);
std::optional<NodeType>   typeFromName(const std::string& name);
