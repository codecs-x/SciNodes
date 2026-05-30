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
        { NodeType::DesignTemplate, {
            NodeType::DesignTemplate, NodeCategory::Source,
            "Design Template",
            "Bundle of machine-design requirements: target torque, speed, "
            "bus voltage and cooling class. Each requirement appears on a "
            "separate output port so downstream sizing nodes can consume "
            "the spec explicitly.",
            0, 4,
            { {"Target Torque",  10.0, "Nm"},
              {"Target Speed",  150.0, "rad/s"},
              {"Bus Voltage",   400.0, "V"},
              {"Cooling Class",   1.0, ""} }
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
        { NodeType::AirgapFluxDensity, {
            NodeType::AirgapFluxDensity, NodeCategory::Transformer,
            "Air-Gap Flux Density",
            "Instantaneous radial air-gap flux density at a fixed stator "
            "observation point. Input: mechanical angular speed omega "
            "(rad/s). The internal state is the rotor angle theta, with "
            "dtheta/dt = omega.\n"
            "Output: B_g(t) = B_peak * ( sin(p*theta) + a3*sin(3*p*theta) "
            "+ a_slot*sin(N_s*theta) ) — fundamental + 3rd magnet harmonic "
            "+ slot-passing harmonic. Feeds nicely into FFTAnalyzer for "
            "spectral inspection.",
            1, 1,
            { {"Peak Flux Density",     0.85, "T"},
              {"Pole Pairs",            4.0,  ""},
              {"3rd Harmonic Ratio",    0.10, ""},
              {"Slot Harmonic Ratio",   0.05, ""},
              {"Slot Count",           24.0,  ""} }
        }},
        { NodeType::PMSMEfficiency, {
            NodeType::PMSMEfficiency, NodeCategory::Transformer,
            "PMSM Efficiency",
            "Simple analytical efficiency for a surface PMSM. Inputs: "
            "shaft torque T (Nm), mechanical speed omega (rad/s), back-EMF "
            "constant Ke (V*s/rad). Computes:\n"
            "  Iq    = T / Ke\n"
            "  P_cu  = (3/2) * R_phase * Iq^2\n"
            "  P_iron = K_iron * omega^2\n"
            "  P_mech = K_mech * omega\n"
            "  P_out  = T * omega\n"
            "  eta    = P_out / (P_out + P_cu + P_iron + P_mech)\n"
            "Output: eta (dimensionless). Returns 0 when total input "
            "power is zero (singularity at T=omega=0).",
            3, 1,
            { {"Stator Resistance",  0.5,    "Ohm"},
              {"Iron Loss Coeff.",   1e-4,   "W/(rad/s)^2"},
              {"Mech Loss Coeff.",   1e-3,   "W/(rad/s)"} }
        }},
        { NodeType::PMSMElectromagnetic, {
            NodeType::PMSMElectromagnetic, NodeCategory::Transformer,
            "PMSM Electromagnetic",
            "Lumped-parameter electromagnetic model for a surface-mount "
            "PMSM. Inputs: bore diameter D (m), stack length L (m), "
            "mechanical angular speed omega (rad/s).\n"
            "Outputs (in port order):\n"
            "  0: Ke   — back-EMF constant       (V*s/rad)\n"
            "  1: L_ph — per-phase synchronous inductance (H)\n"
            "  2: Vrms — line-to-line RMS back-EMF at the input omega (V)\n"
            "  3: Tcog — cogging-torque peak amplitude (Nm)\n"
            "Formulas use 4*pi*1e-7 for mu0 and an effective airgap "
            "g_eff = g + h_m / mu_r with mu_r = 1.05 (NdFeB).",
            3, 4,
            { {"Turns per Phase",        100.0,  ""},
              {"Winding Factor",           0.95, ""},
              {"Pole Pairs",               4.0,  ""},
              {"Airgap Flux Density",      0.85, "T"},
              {"Mechanical Airgap",        0.001,"m"},
              {"Magnet Thickness",         0.003,"m"},
              {"Slot Count",              24.0,  ""} }
        }},
        { NodeType::IPMSizing, {
            NodeType::IPMSizing, NodeCategory::Transformer,
            "IPM Sizing",
            "Interior-PM machine sizing. Same closed-form bore/stack as "
            "PMSMSizing but the achievable torque is boosted by a "
            "saliency factor k_sal (1.0 ≡ surface PMSM; typical IPM "
            "values 1.10..1.30) that accounts for the reluctance-torque "
            "contribution (3/2)*p*(Ld-Lq)*id*iq. Outputs (port order): "
            "bore D (m), stack L (m), rated power P (W).",
            2, 3,
            { {"Magnetic Loading B",   0.85,  "T"},
              {"Electric Loading A", 40000.0, "A/m"},
              {"Aspect Ratio L/D",     1.2,   ""},
              {"Saliency Factor",      1.20,  ""},
              {"Slot Count",          12.0,   ""},
              {"Pole Count",           8.0,   ""} }
        }},
        { NodeType::BLDCSizing, {
            NodeType::BLDCSizing, NodeCategory::Transformer,
            "BLDC Sizing",
            "Brushless-DC (trapezoidal commutation) sizing. Same bore/"
            "stack closed-form as PMSMSizing with a trapezoidal-waveform "
            "factor k_trap (default 1.15) reflecting the higher torque "
            "per ampere of square-wave commutation over sinusoidal. "
            "Outputs (port order): bore D (m), stack L (m), rated "
            "power P (W).",
            2, 3,
            { {"Magnetic Loading B",   0.90,  "T"},
              {"Electric Loading A", 35000.0, "A/m"},
              {"Aspect Ratio L/D",     1.0,   ""},
              {"Trapezoidal Factor",   1.15,  ""},
              {"Slot Count",           6.0,   ""},
              {"Pole Count",           4.0,   ""} }
        }},
        { NodeType::PMSMSizing, {
            NodeType::PMSMSizing, NodeCategory::Transformer,
            "PMSM Sizing",
            "Classical analytical sizing for a surface-mount PMSM. "
            "Given target torque T (input 0) and target speed omega "
            "(input 1), and the conventional magnetic loading B and "
            "electric loading A, computes:\n"
            "    D = (2*T / (pi*B*A*alpha))^(1/3)\n"
            "    L = alpha * D\n"
            "    P = T * omega\n"
            "Outputs (in port order): bore diameter D (m), stack length "
            "L (m), rated power P (W).",
            2, 3,
            { {"Magnetic Loading B",   0.85,  "T"},
              {"Electric Loading A", 40000.0, "A/m"},
              {"Aspect Ratio L/D",     1.2,   ""},
              {"Slot Count",          12.0,   ""},
              {"Pole Count",           4.0,   ""} }
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
        { NodeType::View3DSink, {
            NodeType::View3DSink, NodeCategory::Sink,
            "3D View Sink",
            "Drives the shaft angle (in radians) of the procedural motor "
            "shown in the 3D View panel.",
            1, 0,
            {}
        }},
        { NodeType::HeatmapSink, {
            NodeType::HeatmapSink, NodeCategory::Sink,
            "Heatmap Sink",
            "Operating-point heatmap. Three inputs (in port order): "
            "x-axis value, y-axis value, color/intensity value. The "
            "PlotPanel renders the accumulated samples as a scatter "
            "with each point coloured by its third input (viridis-like "
            "gradient). A typical use is (torque, speed, efficiency) "
            "to visualise the η(T, ω) map of a machine while a sweep "
            "ramps T and ω through the simulation.",
            3, 0,
            {}
        }},
    };
    return reg;
}

NodeCategory categoryOf(NodeType t) {
    // Custom nodes have no entry in the static registry — their category
    // depends on which JSON descriptor an instance points at. Callers that
    // work with a NodeInstance should use `categoryOf(const NodeInstance&)`
    // from NodeInstance.hpp; the placeholder Transformer answer here is
    // only ever read by code paths that pre-filter Custom.
    if (t == NodeType::Custom) return NodeCategory::Transformer;
    return nodeRegistry().at(t).category;
}

const char* labelOf(NodeType t) {
    if (t == NodeType::Custom) return "Custom";
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
        { NodeType::DesignTemplate,    "DesignTemplate"    },
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
        { NodeType::PMSMSizing,        "PMSMSizing"        },
        { NodeType::IPMSizing,         "IPMSizing"         },
        { NodeType::BLDCSizing,        "BLDCSizing"        },
        { NodeType::PMSMElectromagnetic, "PMSMElectromagnetic" },
        { NodeType::AirgapFluxDensity,   "AirgapFluxDensity"   },
        { NodeType::PMSMEfficiency,      "PMSMEfficiency"      },
        { NodeType::HeatmapSink,         "HeatmapSink"         },
        { NodeType::Oscilloscope,      "Oscilloscope"      },
        { NodeType::FFTAnalyzer,       "FFTAnalyzer"       },
        { NodeType::PhasePortrait,     "PhasePortrait"     },
        { NodeType::DataLogger,        "DataLogger"        },
        { NodeType::TerminalDisplay,   "TerminalDisplay"   },
        { NodeType::View3DSink,        "View3DSink"        },
        { NodeType::Custom,            "Custom"            },
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
