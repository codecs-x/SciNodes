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

    // Sinks (grammar terminal-right)
    Oscilloscope,
    FFTAnalyzer,
    PhasePortrait,
    DataLogger,
    TerminalDisplay,
    View3DSink,
    View3DThermalSink,     // Stage v0.9 — tints the procedural mesh
    HeatmapSink,           // Phase 2 — 2-D heatmap (x, y, value) (v0.8)

    // Sentinel for JSON-loaded node types — see CustomNodeRegistry.
    // The actual descriptor lives in NodeInstance::customType.
    Custom,
};

enum class NodeCategory { Source, Transformer, Sink };

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

// Stable enum-name conversion for serialization (e.g. "VoltageSource").
// Distinct from labelOf() which returns the human display label ("Voltage Source").
const char*               typeName(NodeType t);
std::optional<NodeType>   typeFromName(const std::string& name);
