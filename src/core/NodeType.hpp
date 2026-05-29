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

    // Sinks (grammar terminal-right)
    Oscilloscope,
    FFTAnalyzer,
    PhasePortrait,
    DataLogger,
    TerminalDisplay,
    View3DSink,
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
