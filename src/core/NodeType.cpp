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

        // --- Stage v0.9 thermal-network nodes ----------------------------------
        { NodeType::JouleLoss, {
            NodeType::JouleLoss, NodeCategory::Transformer,
            "Joule Loss",
            "Copper / Joule loss in the stator winding of a surface "
            "PMSM. Inputs: shaft torque T (Nm), back-EMF constant Ke "
            "(V*s/rad). Computes Iq = T/Ke and outputs:\n"
            "  P_cu = (3/2) * R_phase * Iq^2\n"
            "Param: phase resistance R_phase (Ohm).",
            2, 1,
            { {"Stator Resistance", 0.5, "Ohm"} }
        }},
        { NodeType::CoreLoss, {
            NodeType::CoreLoss, NodeCategory::Transformer,
            "Core Loss",
            "Iron / core loss using a Bertotti-style two-term model. "
            "Inputs: mechanical ω (rad/s), peak airgap flux density B_g "
            "(T). Computes electrical frequency f = p*ω/(2*pi) and "
            "outputs:\n"
            "  P_fe = K_hys * f * B_g^2 + K_eddy * f^2 * B_g^2\n"
            "Params: hysteresis coefficient K_hys, eddy coefficient "
            "K_eddy, pole pairs p.",
            2, 1,
            { {"Hysteresis Coeff.", 0.02,  "W*s/T^2"},
              {"Eddy Coeff.",       1e-5,  "W*s^2/T^2"},
              {"Pole Pairs",        4.0,   ""} }
        }},
        { NodeType::MechanicalLoss, {
            NodeType::MechanicalLoss, NodeCategory::Transformer,
            "Mechanical Loss",
            "Friction + windage losses. Input: mechanical ω (rad/s). "
            "Output:\n"
            "  P_mech = K_visc * |ω| + K_drag * ω^2\n"
            "Params: viscous-friction coefficient K_visc (W*s/rad), "
            "windage-drag coefficient K_drag (W*s^2/rad^2).",
            1, 1,
            { {"Viscous Coeff.",  1e-3, "W*s/rad"},
              {"Drag Coeff.",     1e-5, "W*s^2/rad^2"} }
        }},
        { NodeType::CoolingSystem, {
            NodeType::CoolingSystem, NodeCategory::Source,
            "Cooling System",
            "Bundle of cooling-system knobs the engineer drags in real "
            "time. Three output ports (in order):\n"
            "  0: Fan Flow         (m^3/h)\n"
            "  1: Water Flow       (L/min)\n"
            "  2: Ambient Temperature (K)\n"
            "Wire the ambient-temperature output to a thermal "
            "resistance's cold side; route Fan / Water Flow into a "
            "ConvectiveCooling block for active heat removal that "
            "scales with flow rate.",
            0, 3,
            { {"Fan Flow",            5.0,   "m^3/h"},
              {"Water Flow",          0.0,   "L/min"},
              {"Ambient Temperature", 298.0, "K"} }
        }},
        { NodeType::ConvectiveCooling, {
            NodeType::ConvectiveCooling, NodeCategory::Transformer,
            "Convective Cooling",
            "Active cooling — extracts heat from a hot thermal node to "
            "a cold side (typically a CoolingSystem's ambient output) "
            "with a heat-transfer coefficient that scales linearly with "
            "an input flow rate.\n"
            "Inputs (port order): T_hot (K), T_cold (K), flow rate "
            "(m^3/h or any flow unit consistent with h_slope).\n"
            "Outputs:\n"
            "  0: q_hot_to_cold = h * (T_hot - T_cold)\n"
            "  1: q_cold_to_hot = -q_hot_to_cold\n"
            "with h = h_0 + h_slope * flow.",
            3, 2,
            { {"Base Coeff. h_0",  1.0, "W/K"},
              {"Slope per Flow",   0.5, "W/K/(m^3/h)"} }
        }},
        { NodeType::ThermalNode, {
            NodeType::ThermalNode, NodeCategory::Transformer,
            "Thermal Node",
            "Pure heat capacitance — one body of a lumped thermal "
            "chain. Four input ports (any subset may be left "
            "unconnected) carry heat flows entering the node; the "
            "internal state x = T (K) integrates "
            "  dT/dt = (sum of all four inputs) / C\n"
            "Output: T (K). Initial condition x(0) = Initial "
            "Temperature.\n"
            "Pair with ThermalResistance to build winding → magnet → "
            "frame chains. The dual-output ThermalResistance gives you "
            "+q for the cold side and −q for the hot side directly, so "
            "you never need a Summation just to flip signs.",
            4, 1,
            { {"Thermal Capacitance", 500.0, "J/K"},
              {"Initial Temperature", 298.0, "K"} }
        }},
        { NodeType::ThermalResistance, {
            NodeType::ThermalResistance, NodeCategory::Transformer,
            "Thermal Resistance",
            "Linear conduction / convection between two thermal nodes. "
            "Inputs (port order): T_hot, T_cold (both K). Outputs "
            "(port order):\n"
            "  0: q_hot_to_cold = (T_hot - T_cold) / R   [W]\n"
            "  1: q_cold_to_hot = -q_hot_to_cold         [W]\n"
            "Wire output 0 into the cold node's heat-in port and "
            "output 1 into the hot node's heat-in port — each "
            "ThermalNode just sums its inputs, so the two outputs let "
            "you couple the two sides without a Summation in between.",
            2, 2,
            { {"Thermal Resistance", 1.0, "K/W"} }
        }},
        { NodeType::ThermalMass, {
            NodeType::ThermalMass, NodeCategory::Transformer,
            "Thermal Mass",
            "Single-node RC thermal mass — pure-state stateful. Input: "
            "heat power P_in (W). Internal state x = node temperature "
            "T (K) with ODE\n"
            "  C_th * dT/dt = P_in - (T - T_amb) / R_th\n"
            "Initial condition T(0) = T_ambient. Output: T (K). Params: "
            "thermal capacitance C_th (J/K), thermal resistance to "
            "ambient R_th (K/W), ambient temperature T_ambient (K).",
            1, 1,
            { {"Thermal Capacitance", 500.0, "J/K"},
              {"Thermal Resistance",    0.5, "K/W"},
              {"Ambient Temperature", 298.0, "K"} }
        }},

        // --- Stage v1.0 structural / NVH nodes ---------------------------------
        { NodeType::MaxwellForce, {
            NodeType::MaxwellForce, NodeCategory::Transformer,
            "Maxwell Force",
            "Radial Maxwell pressure on the stator tooth from the "
            "instantaneous air-gap flux density. Input: B_g (T). "
            "Output:\n"
            "  sigma_r = B_g^2 / (2 * mu_0)         [Pa]\n"
            "Stateless. Multiply by tooth area (slot_pitch * stack "
            "length * tooth_arc_fraction) downstream to get a tooth "
            "force time-series ready for FFT analysis of the radial "
            "force harmonics.",
            1, 1,
            {}
        }},
        { NodeType::TolerancePerturbator, {
            NodeType::TolerancePerturbator, NodeCategory::Transformer,
            "Tolerance Perturbator",
            "Adds a uniform-random perturbation within ±Half Tolerance "
            "to its input each step. One simulation step is one Monte-"
            "Carlo trial; over a 10-second run at 60 Hz the downstream "
            "chain sees 600 fresh samples from the tolerance band, and "
            "a DistributionSink at the far end builds the resulting "
            "histogram. Use as a drop-in between a nominal-value source "
            "(StepSignal) and a sizing / EM block to study how tightly "
            "the output is concentrated around the nominal design.",
            1, 1,
            { {"Half Tolerance", 0.05, ""} }
        }},
        { NodeType::ModalFrequency, {
            NodeType::ModalFrequency, NodeCategory::Transformer,
            "Modal Frequency",
            "Natural frequency of the mode-m circumferential "
            "deflection of a thin ring (a coarse stator-yoke proxy). "
            "Input: mean radius R (m). Output:\n"
            "  f_m = (t / (2*pi * R^2)) * sqrt(E / (12 * rho))\n"
            "             * m * (m^2 - 1) / sqrt(m^2 + 1)   [Hz]\n"
            "Returns 0 for m <= 1 (rigid-body modes carry no "
            "structural energy). Params: Young's modulus E (Pa), "
            "density rho (kg/m^3), ring thickness t (m), mode order m.",
            1, 1,
            { {"Young's Modulus", 200.0e9, "Pa"},
              {"Density",         7850.0,  "kg/m^3"},
              {"Thickness",          0.02, "m"},
              {"Mode Order",         2.0,  ""} }
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
        { NodeType::View3DThermalSink, {
            NodeType::View3DThermalSink, NodeCategory::Sink,
            "3D Thermal Tint",
            "Tints the procedural rotor / stator wireframe in the 3-D "
            "View panel by a temperature signal. Connect a ThermalMass "
            "output (or any value with a heat-like meaning) here and the "
            "mesh will gradient cool blue at Cold Temperature, through "
            "yellow at the midpoint, to deep red at Hot Temperature. "
            "Re-tint fires only when the signal moves by >= 1 K to "
            "avoid 60 Hz VBO churn.",
            1, 0,
            { {"Cold Temperature", 290.0, "K"},
              {"Hot Temperature",  390.0, "K"} }
        }},
        { NodeType::View3DDeformationSink, {
            NodeType::View3DDeformationSink, NodeCategory::Sink,
            "3D Deformation Overlay",
            "Animates a circumferential mode-shape vibration on the "
            "procedural rotor / stator wireframe. Three inputs (in "
            "port order):\n"
            "  0: frequency f (Hz)         — animation speed\n"
            "  1: mode order m (—)         — number of circumferential lobes\n"
            "  2: amplitude  A (m)         — peak radial displacement\n"
            "Per-vertex radial displacement: Δr(θ, t) = A · cos(m·θ) · "
            "sin(2π·f·t). Use a ModalFrequency node for f and a "
            "constant for m (typical 2..4). Amplitude is exaggerated "
            "for visibility — pick ~ 5..15 % of the mesh radius.",
            3, 0,
            {}
        }},
        { NodeType::DistributionSink, {
            NodeType::DistributionSink, NodeCategory::Sink,
            "Distribution Sink",
            "Accumulates samples in the standard ring buffer and asks "
            "PlotPanel to render them as a live histogram instead of "
            "a scrolling waveform. Use after a `TolerancePerturbator` "
            "chain to visualise the spread of an output metric under "
            "Monte-Carlo sampling of geometric tolerances. The bin "
            "count controls histogram resolution — typical values are "
            "10..40 for a 512-sample ring.",
            1, 0,
            { {"Bin Count", 20.0, ""} }
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
        { NodeType::JouleLoss,           "JouleLoss"           },
        { NodeType::CoreLoss,            "CoreLoss"            },
        { NodeType::MechanicalLoss,      "MechanicalLoss"      },
        { NodeType::ThermalMass,         "ThermalMass"         },
        { NodeType::ThermalNode,         "ThermalNode"         },
        { NodeType::ThermalResistance,   "ThermalResistance"   },
        { NodeType::CoolingSystem,       "CoolingSystem"       },
        { NodeType::ConvectiveCooling,   "ConvectiveCooling"   },
        { NodeType::MaxwellForce,        "MaxwellForce"        },
        { NodeType::ModalFrequency,      "ModalFrequency"      },
        { NodeType::TolerancePerturbator,"TolerancePerturbator"},
        { NodeType::DistributionSink,    "DistributionSink"    },
        { NodeType::Oscilloscope,      "Oscilloscope"      },
        { NodeType::FFTAnalyzer,       "FFTAnalyzer"       },
        { NodeType::PhasePortrait,     "PhasePortrait"     },
        { NodeType::DataLogger,        "DataLogger"        },
        { NodeType::TerminalDisplay,   "TerminalDisplay"   },
        { NodeType::View3DSink,        "View3DSink"        },
        { NodeType::View3DThermalSink, "View3DThermalSink" },
        { NodeType::View3DDeformationSink, "View3DDeformationSink" },
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
