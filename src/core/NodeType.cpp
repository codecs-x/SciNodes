#include "NodeType.hpp"

const std::unordered_map<NodeType, NodeDef>& nodeRegistry() {
    static const std::unordered_map<NodeType, NodeDef> reg = {
        // --- Sources -----------------------------------------------------------
        { NodeType::VoltageSource, {
            NodeType::VoltageSource, NodeCategory::Source,
            "Voltage Source", "Ideal DC voltage source",
            0, 1,
            { {"Voltage", 12.0, "V"}, {"Int. Resistance", 0.1, "Ohm"} }
        }},
        { NodeType::CurrentSource, {
            NodeType::CurrentSource, NodeCategory::Source,
            "Current Source", "Ideal DC current source",
            0, 1,
            { {"Current", 1.0, "A"} }
        }},
        { NodeType::StepSignal, {
            NodeType::StepSignal, NodeCategory::Source,
            "Step Signal", "Heaviside step function",
            0, 1,
            { {"Amplitude", 1.0, ""}, {"Step Time", 0.0, "s"} }
        }},
        { NodeType::SineSignal, {
            NodeType::SineSignal, NodeCategory::Source,
            "Sine Signal", "Sinusoidal waveform",
            0, 1,
            { {"Amplitude", 1.0, ""}, {"Frequency", 1.0, "Hz"}, {"Phase", 0.0, "rad"} }
        }},
        { NodeType::RampSignal, {
            NodeType::RampSignal, NodeCategory::Source,
            "Ramp Signal", "Linear ramp starting at t=0",
            0, 1,
            { {"Slope", 1.0, "/s"} }
        }},

        // --- Transformers -------------------------------------------------------
        { NodeType::Gain, {
            NodeType::Gain, NodeCategory::Transformer,
            "Gain", "Multiplies the input by a constant gain",
            1, 1,
            { {"K", 1.0, ""} }
        }},
        { NodeType::Summation, {
            NodeType::Summation, NodeCategory::Transformer,
            "Summation", "Adds two signals (second port subtracted when sign=-1)",
            2, 1,
            { {"Sign1", 1.0, ""}, {"Sign2", 1.0, ""} }
        }},
        { NodeType::Integrator, {
            NodeType::Integrator, NodeCategory::Transformer,
            "Integrator", "Continuous-time integrator (1/s)",
            1, 1,
            { {"Initial Cond.", 0.0, ""} }
        }},
        { NodeType::Differentiator, {
            NodeType::Differentiator, NodeCategory::Transformer,
            "Differentiator",
            "Filtered derivative: H(s) = s / (1 + s/wc)",
            1, 1,
            { {"Cutoff Freq.", 100.0, "Hz"} }
        }},
        { NodeType::LowPassFilter, {
            NodeType::LowPassFilter, NodeCategory::Transformer,
            "Low-Pass Filter", "First-order low-pass filter",
            1, 1,
            { {"Cutoff Freq.", 100.0, "Hz"} }
        }},
        { NodeType::PIDController, {
            NodeType::PIDController, NodeCategory::Transformer,
            "PID Controller", "Parallel PID with anti-windup",
            1, 1,
            { {"Kp", 1.0, ""}, {"Ki", 0.0, ""}, {"Kd", 0.0, ""}, {"N (filter)", 100.0, ""} }
        }},
        { NodeType::TransferFunction, {
            NodeType::TransferFunction, NodeCategory::Transformer,
            "Transfer Function", "Rational transfer function H(s)=num/den",
            1, 1,
            { {"num[0]", 1.0, ""}, {"den[0]", 1.0, ""}, {"den[1]", 1.0, ""} }
        }},
        { NodeType::TransferFunction2, {
            NodeType::TransferFunction2, NodeCategory::Transformer,
            "Transfer Function (2nd)",
            "Second-order rational H(s) = (b1*s + b0) / (s^2 + a1*s + a0). "
            "Monic denominator (s^2 coefficient implicit 1).",
            1, 1,
            { {"num[0]", 1.0, ""}, {"num[1]", 0.0, ""},
              {"den[0]", 1.0, ""}, {"den[1]", 0.0, ""} }
        }},
        { NodeType::Saturation, {
            NodeType::Saturation, NodeCategory::Transformer,
            "Saturation", "Clamps output to [min, max]",
            1, 1,
            { {"Min", -1.0, ""}, {"Max", 1.0, ""} }
        }},
        { NodeType::DCMotorModel, {
            NodeType::DCMotorModel, NodeCategory::Transformer,
            "DC Motor Model", "Simplified DC motor: electrical + mechanical dynamics",
            1, 1,
            { {"Ra", 1.0, "Ohm"}, {"La", 0.01, "H"}, {"Ke", 0.1, "V/rad/s"},
              {"Kt", 0.1, "Nm/A"}, {"J", 0.01, "kgm2"}, {"B", 0.001, "Nm/rad/s"} }
        }},
        { NodeType::GearTransmission, {
            NodeType::GearTransmission, NodeCategory::Transformer,
            "Gear Transmission", "Speed/torque ratio transformation",
            1, 1,
            { {"Ratio", 10.0, ""}, {"Efficiency", 0.95, ""} }
        }},
        { NodeType::InverseKinematics, {
            NodeType::InverseKinematics, NodeCategory::Transformer,
            "Inverse Kinematics",
            "2-link planar IK (elbow-up). Inputs: target (x, y). "
            "Outputs: joint angles (theta1, theta2). Target is clamped "
            "to the reachable workspace |c2| <= 1.",
            2, 2,
            { {"Link 1 L", 0.3, "m"}, {"Link 2 L", 0.2, "m"} }
        }},

        // --- Sinks -------------------------------------------------------------
        { NodeType::Oscilloscope, {
            NodeType::Oscilloscope, NodeCategory::Sink,
            "Oscilloscope", "Time-domain waveform display",
            1, 0,
            { {"Time Window", 5.0, "s"} }
        }},
        { NodeType::FFTAnalyzer, {
            NodeType::FFTAnalyzer, NodeCategory::Sink,
            "FFT Analyzer", "Frequency spectrum of the input signal",
            1, 0,
            { {"Bin Count", 256.0, ""} }
        }},
        { NodeType::PhasePortrait, {
            NodeType::PhasePortrait, NodeCategory::Sink,
            "Phase Portrait", "Phase-space plot: in 1 = x(t), in 2 = dx/dt(t)",
            2, 0,
            {}
        }},
        { NodeType::DataLogger, {
            NodeType::DataLogger, NodeCategory::Sink,
            "Data Logger", "Exports signal to CSV",
            1, 0,
            { {"Sample Rate", 1000.0, "Hz"} }
        }},
        { NodeType::TerminalDisplay, {
            NodeType::TerminalDisplay, NodeCategory::Sink,
            "Terminal Display", "Prints current value to status console",
            1, 0,
            {}
        }},
    };
    return reg;
}

NodeCategory categoryOf(NodeType t) {
    return nodeRegistry().at(t).category;
}

const char* labelOf(NodeType t) {
    return nodeRegistry().at(t).label.c_str();
}

// ---------------------------------------------------------------------------
// Stable enum-name table for serialization. The names here must never change
// without a .scn format-version bump — they are the on-disk identifiers.
// ---------------------------------------------------------------------------
static const std::vector<std::pair<NodeType, const char*>>& nameTable() {
    static const std::vector<std::pair<NodeType, const char*>> t = {
        { NodeType::VoltageSource,     "VoltageSource"     },
        { NodeType::CurrentSource,     "CurrentSource"     },
        { NodeType::StepSignal,        "StepSignal"        },
        { NodeType::SineSignal,        "SineSignal"        },
        { NodeType::RampSignal,        "RampSignal"        },
        { NodeType::Gain,              "Gain"              },
        { NodeType::Summation,         "Summation"         },
        { NodeType::Integrator,        "Integrator"        },
        { NodeType::Differentiator,    "Differentiator"    },
        { NodeType::LowPassFilter,     "LowPassFilter"     },
        { NodeType::PIDController,     "PIDController"     },
        { NodeType::TransferFunction,  "TransferFunction"  },
        { NodeType::TransferFunction2, "TransferFunction2" },
        { NodeType::Saturation,        "Saturation"        },
        { NodeType::DCMotorModel,      "DCMotorModel"      },
        { NodeType::GearTransmission,  "GearTransmission"  },
        { NodeType::InverseKinematics, "InverseKinematics" },
        { NodeType::Oscilloscope,      "Oscilloscope"      },
        { NodeType::FFTAnalyzer,       "FFTAnalyzer"       },
        { NodeType::PhasePortrait,     "PhasePortrait"     },
        { NodeType::DataLogger,        "DataLogger"        },
        { NodeType::TerminalDisplay,   "TerminalDisplay"   },
    };
    return t;
}

const char* typeName(NodeType t) {
    for (const auto& p : nameTable())
        if (p.first == t) return p.second;
    return "Unknown";
}

std::optional<NodeType> typeFromName(const std::string& name) {
    for (const auto& p : nameTable())
        if (name == p.second) return p.first;
    return std::nullopt;
}
