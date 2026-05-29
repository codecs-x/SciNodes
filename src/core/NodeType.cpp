#include "NodeType.hpp"
#include "UnitCatalog.hpp"

const std::unordered_map<NodeType, NodeDef>& nodeRegistry() {
    static const std::unordered_map<NodeType, NodeDef> reg = {
        // --- Sources -----------------------------------------------------------
        { NodeType::VoltageSource, {
            NodeType::VoltageSource, NodeCategory::Source,
            "Voltage Source", "Ideal DC voltage source",
            0, 1,
            { {"Voltage", 12.0, "V"}, {"Int. Resistance", 0.1, "Ohm"} },
            /*stateWidth*/    0, /*isPureState*/ false,
            /*stateOnlyPorts*/{}, /*inputPortTypes*/{}, /*outputPortTypes*/{},
            /*inputPortLabels*/{}, /*outputPortLabels*/{},
            /*inputPortUnits*/ {},
            /*outputPortUnits*/{ scinodes::units::kVolt }
        }},
        { NodeType::CurrentSource, {
            NodeType::CurrentSource, NodeCategory::Source,
            "Current Source", "Ideal DC current source",
            0, 1,
            { {"Current", 1.0, "A"} },
            /*stateWidth*/    0, /*isPureState*/ false,
            /*stateOnlyPorts*/{}, /*inputPortTypes*/{}, /*outputPortTypes*/{},
            /*inputPortLabels*/{}, /*outputPortLabels*/{},
            /*inputPortUnits*/ {},
            /*outputPortUnits*/{ scinodes::units::kAmpere }
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
            { {"Initial Cond.", 0.0, ""} },
            /*stateWidth*/  1, /*isPureState*/ true,
            /*stateOnlyPorts*/{}, /*inputPortTypes*/{}, /*outputPortTypes*/{},
            /*inputPortLabels*/{}, /*outputPortLabels*/{},
            /*inputPortUnits*/ {}, /*outputPortUnits*/{},
            // ∫ in d(domain).  En time-domain: × s → rad/s pasa a rad.
            // El factor real lo aporta graph.domainUnit() al analyzer.
            /*unitTransformKind*/ NodeDef::UnitTransformKind::MultiplyDomain
        }},
        { NodeType::Differentiator, {
            NodeType::Differentiator, NodeCategory::Transformer,
            "Differentiator",
            "Filtered derivative: H(s) = s / (1 + s/wc)",
            1, 1,
            { {"Cutoff Freq.", 100.0, "Hz"} },
            /*stateWidth*/  1, /*isPureState*/ false,
            /*stateOnlyPorts*/{}, /*inputPortTypes*/{}, /*outputPortTypes*/{},
            /*inputPortLabels*/{}, /*outputPortLabels*/{},
            /*inputPortUnits*/ {}, /*outputPortUnits*/{},
            // d in/d(domain).  En time-domain: / s → rad pasa a rad/s.
            /*unitTransformKind*/ NodeDef::UnitTransformKind::DivideDomain
        }},
        { NodeType::LowPassFilter, {
            NodeType::LowPassFilter, NodeCategory::Transformer,
            "Low-Pass Filter", "First-order low-pass filter",
            1, 1,
            { {"Cutoff Freq.", 100.0, "Hz"} },
            /*stateWidth*/  1, /*isPureState*/ true
        }},
        { NodeType::PIDController, {
            NodeType::PIDController, NodeCategory::Transformer,
            "PID Controller",
            "Parallel PID con derivada filtrada (Kd·N·s/(s+N)). "
            "Port 0 = error. Port 1 (opcional) = u_sat (back-calculation "
            "anti-windup, Astrom & Hagglund 2006). Si Kt = 0 o port 1 "
            "no conectado, no se aplica anti-windup.",
            2, 1,
            { {"Kp", 1.0, ""}, {"Ki", 0.0, ""}, {"Kd", 0.0, ""},
              {"N (filter)", 100.0, ""}, {"Kt (anti-windup)", 0.0, ""} },
            /*stateWidth*/  2, /*isPureState*/ false,
            /*stateOnlyPorts*/ {1}   // u_sat: solo afecta derivada de integral
        }},
        { NodeType::TransferFunction, {
            NodeType::TransferFunction, NodeCategory::Transformer,
            "Transfer Function", "Rational transfer function H(s)=num/den",
            1, 1,
            { {"num[0]", 1.0, ""}, {"den[0]", 1.0, ""}, {"den[1]", 1.0, ""} },
            /*stateWidth*/  1, /*isPureState*/ true
        }},
        { NodeType::TransferFunction2, {
            NodeType::TransferFunction2, NodeCategory::Transformer,
            "Transfer Function (2nd)",
            "Second-order rational H(s) = (b1*s + b0) / (s^2 + a1*s + a0). "
            "Monic denominator (s^2 coefficient implicit 1).",
            1, 1,
            { {"num[0]", 1.0, ""}, {"num[1]", 0.0, ""},
              {"den[0]", 1.0, ""}, {"den[1]", 0.0, ""} },
            /*stateWidth*/  2, /*isPureState*/ true
        }},
        { NodeType::Saturation, {
            NodeType::Saturation, NodeCategory::Transformer,
            "Saturation", "Clamps output to [min, max]",
            1, 1,
            { {"Min", -1.0, ""}, {"Max", 1.0, ""} }
        }},
        { NodeType::DCMotorModel, {
            NodeType::DCMotorModel, NodeCategory::Device,
            "DC Motor Model", "Simplified DC motor: electrical + mechanical dynamics",
            1, 1,
            { {"Ra", 1.0, "Ohm"}, {"La", 0.01, "H"}, {"Ke", 0.1, "V/rad/s"},
              {"Kt", 0.1, "Nm/A"}, {"J", 0.01, "kgm2"}, {"B", 0.001, "Nm/rad/s"} },
            /*stateWidth*/  2, /*isPureState*/ true,
            /*stateOnlyPorts*/{}, /*inputPortTypes*/{}, /*outputPortTypes*/{},
            /*inputPortLabels*/{}, /*outputPortLabels*/{},
            /*inputPortUnits*/ { scinodes::units::kVolt },
            /*outputPortUnits*/{ scinodes::units::kRadianPerSec }
        }},
        { NodeType::GearTransmission, {
            NodeType::GearTransmission, NodeCategory::Transformer,
            "Gear Transmission", "Speed/torque ratio transformation",
            1, 1,
            { {"Ratio", 10.0, ""}, {"Efficiency", 0.95, ""} },
            /*stateWidth*/    0, /*isPureState*/ false,
            /*stateOnlyPorts*/{}, /*inputPortTypes*/{}, /*outputPortTypes*/{},
            /*inputPortLabels*/{}, /*outputPortLabels*/{},
            /*inputPortUnits*/ { scinodes::units::kRadianPerSec },
            /*outputPortUnits*/{ scinodes::units::kRadianPerSec }
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
              {"Slot Count",           24.0,  ""} },
            /*stateWidth*/  1, /*isPureState*/ true
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
              {"Initial Temperature", 298.0, "K"} },
            /*stateWidth*/  1, /*isPureState*/ true
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
              {"Ambient Temperature", 298.0, "K"} },
            /*stateWidth*/  1, /*isPureState*/ true
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
            "Oscilloscope",
            "Time-domain multi-channel waveform display. Acepta hasta "
            "8 entradas (un puerto extra aparece dinámicamente cada vez "
            "que se conecta otra señal). Cada canal se dibuja como una "
            "línea de color distinto sobre el mismo eje X de tiempo.",
            8, 0,
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
        // SuperBlock — paréntesis recursivo.
        // Las inputs/outputs declaradas aquí son sólo el default al
        // crear un SubGraph vacío; en runtime, defOf() ajusta el conteo
        // según el contenido (1 por cada `SubGraphInput`/`SubGraphOutput`
        // del grafo hijo).
        { NodeType::SubGraph, {
            NodeType::SubGraph, NodeCategory::Transformer,
            "SubGraph",
            "Agrupa nodos en un sub-grafo recursivo. Doble-click para "
            "entrar; los puertos visibles se materializan internamente "
            "como nodos `SubGraphInput`/`SubGraphOutput`.  Antes de "
            "generar Scilab el codegen aplana cada SubGraph inline, "
            "así la simulación es indistinguible de la versión sin "
            "agrupar.",
            0, 0,
            {}
        }},
        { NodeType::SubGraphInput, {
            NodeType::SubGraphInput, NodeCategory::Source,
            "SubGraph Input",
            "Stub interno: representa una entrada externa del SubGraph "
            "padre.  El parámetro `Port` indica qué puerto del padre "
            "alimenta a este stub (0-based).  Al aplanar, se reemplaza "
            "por la señal que llega a ese puerto externo.",
            0, 1,
            { {"Port", 0.0, ""} }
        }},
        { NodeType::SubGraphOutput, {
            NodeType::SubGraphOutput, NodeCategory::Sink,
            "SubGraph Output",
            "Stub interno: representa una salida externa del SubGraph "
            "padre.  El parámetro `Port` indica qué puerto del padre "
            "emite la señal que llega a este stub (0-based).  Al "
            "aplanar, los consumidores externos se redirigen a la "
            "señal que entra a este nodo.",
            1, 0,
            { {"Port", 0.0, ""} }
        }},

        // ---- Sub-lenguaje Geometry (3D scene graph) ----------------------
        // Ver `doc/3d_scene_graph_design.md`.  En esta etapa registramos
        // los nodos con sus declaraciones de port-type para que R6
        // (port-type matching) pueda validar grafos que los usan.  La
        // semántica de render se conecta en pasos posteriores
        // (View3DPanel refactor).
        { NodeType::Object3D, {
            NodeType::Object3D, NodeCategory::Source,
            "Object 3D",
            "Referencia un objeto importado del catálogo del proyecto "
            "(o una parte concreta del objeto, p. ej. 'Motor DC/shaft') "
            "y lo emite como geometría al sub-grafo de escena.",
            0, 1,
            { /* params reales — objectRef/asInstance — irán en stringParams */ },
            /*stateWidth*/    0,
            /*isPureState*/   false,
            /*stateOnlyPorts*/{},
            /*inputPortTypes*/ {},
            /*outputPortTypes*/{ exprGeometry() }
        }},
        { NodeType::TransformObject, {
            NodeType::TransformObject, NodeCategory::Transformer,
            "Transform Object",
            "Bridge entre el grafo de señales y el sub-grafo de "
            "escena: toma una geometría y la modula con tres "
            "vectores vec(3) — rotación (Euler XYZ, rad), traslación "
            "(m), escala (dimensionless).  Usar `Combine XYZ` para "
            "armar el vec(3) desde tres señales escalares.",
            4, 1,
            { /* no params escalares — todo entra por puertos */ },
            /*stateWidth*/    0,
            /*isPureState*/   false,
            /*stateOnlyPorts*/{},
            /*inputPortTypes*/{ exprGeometry(),  // port 0: geometría
                                exprVec(3),      // port 1: rotation
                                exprVec(3),      // port 2: translation
                                exprVec(3) },    // port 3: scale
            /*outputPortTypes*/{ exprGeometry() },
            /*inputPortLabels*/{ "geometría",
                                 "rotación  [rad, Euler XYZ]",
                                 "traslación [m]",
                                 "escala" }
        }},
        { NodeType::SceneOutput, {
            NodeType::SceneOutput, NodeCategory::Sink,
            "Scene Output",
            "Sink del sub-grafo Geometry: agrupa lo que se rendera en "
            "el panel View3D.  Acepta múltiples entradas de geometría "
            "que el render compone en una sola escena — una por cada "
            "objeto/parte independientemente transformable.",
            8, 0,    // capacidad práctica de la escena: 8 ramas
            { /* sin params */ },
            /*stateWidth*/    0,
            /*isPureState*/   false,
            /*stateOnlyPorts*/{},
            /*inputPortTypes*/{ exprGeometry(), exprGeometry(),
                                exprGeometry(), exprGeometry(),
                                exprGeometry(), exprGeometry(),
                                exprGeometry(), exprGeometry() },
            /*outputPortTypes*/{}
        }},

        // ---- Sub-lenguaje Vec3 (etapa 4 del upgrade gramatical) ----------
        { NodeType::Vec3Constant, {
            NodeType::Vec3Constant, NodeCategory::Source,
            "Vec3 Constant",
            "Constante vec(3) editable.  Útil como entrada de "
            "rotación/traslación/escala fijas a un Transform Object.",
            0, 1,
            { {"x", 0.0, ""}, {"y", 0.0, ""}, {"z", 0.0, ""} },
            /*stateWidth*/    0,
            /*isPureState*/   false,
            /*stateOnlyPorts*/{},
            /*inputPortTypes*/ {},
            /*outputPortTypes*/{ exprVec(3) },
            /*inputPortLabels*/{},
            /*outputPortLabels*/{ "vec(3)" }
        }},
        { NodeType::CombineXYZ, {
            NodeType::CombineXYZ, NodeCategory::Transformer,
            "Combine XYZ",
            "Bridge Signal → Vec3: empaqueta tres señales escalares en "
            "un vector vec(3).  Puertos desconectados = 0 en ese "
            "componente.",
            3, 1,
            { /* sin params */ },
            /*stateWidth*/    0,
            /*isPureState*/   false,
            /*stateOnlyPorts*/{},
            /*inputPortTypes*/{ exprScalar(), exprScalar(), exprScalar() },
            /*outputPortTypes*/{ exprVec(3) },
            /*inputPortLabels*/{ "x", "y", "z" },
            /*outputPortLabels*/{ "vec(3)" }
        }},
        { NodeType::SeparateXYZ, {
            NodeType::SeparateXYZ, NodeCategory::Transformer,
            "Separate XYZ",
            "Bridge Vec3 → Signal: desempaqueta un vec(3) en sus tres "
            "componentes escalares.  Útil para graficar cada componente "
            "por separado en un Oscilloscope.",
            1, 3,
            { /* sin params */ },
            /*stateWidth*/    0,
            /*isPureState*/   false,
            /*stateOnlyPorts*/{},
            /*inputPortTypes*/{ exprVec(3) },
            /*outputPortTypes*/{ exprScalar(), exprScalar(), exprScalar() },
            /*inputPortLabels*/{ "vec(3)" },
            /*outputPortLabels*/{ "x", "y", "z" }
        }},

        // ---- Vector Math (etapa 5 del upgrade gramatical) ----------------
        // Cada nodo es una operación pura del álgebra lineal.  El walker
        // del SceneCollector las evalúa recursivamente render-side.
        { NodeType::VectorAdd, {
            NodeType::VectorAdd, NodeCategory::Transformer,
            "Vector Add",
            "Suma vectorial componente a componente: out = a + b.",
            2, 1, {},
            0, false, {},
            { exprVec(3), exprVec(3) }, { exprVec(3) },
            { "a", "b" }, { "a + b" }
        }},
        { NodeType::VectorSub, {
            NodeType::VectorSub, NodeCategory::Transformer,
            "Vector Subtract",
            "Resta vectorial componente a componente: out = a - b.",
            2, 1, {},
            0, false, {},
            { exprVec(3), exprVec(3) }, { exprVec(3) },
            { "a", "b" }, { "a - b" }
        }},
        { NodeType::VectorScale, {
            NodeType::VectorScale, NodeCategory::Transformer,
            "Vector Scale",
            "Producto por escalar: out = k · v.  El escalar k entra por "
            "un puerto Signal; v y out son vec(3).",
            2, 1, {},
            0, false, {},
            { exprVec(3), exprScalar() }, { exprVec(3) },
            { "v", "k" }, { "k · v" }
        }},
        { NodeType::VectorDot, {
            NodeType::VectorDot, NodeCategory::Transformer,
            "Vector Dot",
            "Producto escalar: out = a · b = aₓ·bₓ + aᵧ·bᵧ + a_z·b_z. "
            "Devuelve un escalar — sin codegen Scilab, render-only.",
            2, 1, {},
            0, false, {},
            { exprVec(3), exprVec(3) }, { exprScalar() },
            { "a", "b" }, { "a · b" }
        }},
        { NodeType::VectorCross, {
            NodeType::VectorCross, NodeCategory::Transformer,
            "Vector Cross",
            "Producto vectorial: out = a × b.  Perpendicular a a y b.",
            2, 1, {},
            0, false, {},
            { exprVec(3), exprVec(3) }, { exprVec(3) },
            { "a", "b" }, { "a × b" }
        }},
        { NodeType::VectorLength, {
            NodeType::VectorLength, NodeCategory::Transformer,
            "Vector Length",
            "Norma euclidiana: out = √(vₓ² + vᵧ² + v_z²).  Devuelve un "
            "escalar — sin codegen Scilab, render-only.",
            1, 1, {},
            0, false, {},
            { exprVec(3) }, { exprScalar() },
            { "v" }, { "|v|" }
        }},
        { NodeType::VectorNormalize, {
            NodeType::VectorNormalize, NodeCategory::Transformer,
            "Vector Normalize",
            "Unitario en la dirección de v: out = v / |v|.  Si v es el "
            "vector cero, devuelve el vector cero (sin singularidad).",
            1, 1, {},
            0, false, {},
            { exprVec(3) }, { exprVec(3) },
            { "v" }, { "v̂" }
        }},

        // ---- Unit converters (etapa 6H del análisis dimensional) --------
        // Estos nodos SÍ emiten Scilab (multiplicación por constante),
        // a diferencia de los nodos Vec3* que viven render-side.
        // Declaran input y output con unidades EXPLÍCITAMENTE distintas
        // — el nodo es el bridge canónico entre convenciones.
        { NodeType::DegToRad, {
            NodeType::DegToRad, NodeCategory::Transformer,
            "Deg → Rad",
            "Convierte un ángulo en grados a radianes: out = in · π/180.",
            1, 1, { /* sin params */ },
            /*stateWidth*/    0, /*isPureState*/ false,
            /*stateOnlyPorts*/{}, /*inputPortTypes*/{}, /*outputPortTypes*/{},
            /*inputPortLabels*/{ "θ [deg]" }, /*outputPortLabels*/{ "θ [rad]" },
            /*inputPortUnits*/ { scinodes::units::kDegree },
            /*outputPortUnits*/{ scinodes::units::kRadian }
        }},
        { NodeType::RadToDeg, {
            NodeType::RadToDeg, NodeCategory::Transformer,
            "Rad → Deg",
            "Convierte un ángulo en radianes a grados: out = in · 180/π.",
            1, 1, { /* sin params */ },
            /*stateWidth*/    0, /*isPureState*/ false,
            /*stateOnlyPorts*/{}, /*inputPortTypes*/{}, /*outputPortTypes*/{},
            /*inputPortLabels*/{ "θ [rad]" }, /*outputPortLabels*/{ "θ [deg]" },
            /*inputPortUnits*/ { scinodes::units::kRadian },
            /*outputPortUnits*/{ scinodes::units::kDegree }
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

bool isPureStateNode(NodeType t) {
    // Consulta el registry — el campo `isPureState` de NodeDef es la
    // fuente de verdad (lista canónica que antes vivía como switch).
    // PIDController y Differentiator NO son pure-state porque tienen
    // feedthrough algebraico desde la entrada del mismo paso.
    const auto& reg = nodeRegistry();
    auto it = reg.find(t);
    return (it != reg.end()) && it->second.isPureState;
}

TypeExpr inputPortTypeOf(const NodeDef& def, int portIdx) {
    if (portIdx < 0 || portIdx >= static_cast<int>(def.inputPortTypes.size()))
        return exprScalar();
    return def.inputPortTypes[portIdx];
}

TypeExpr outputPortTypeOf(const NodeDef& def, int portIdx) {
    if (portIdx < 0 || portIdx >= static_cast<int>(def.outputPortTypes.size()))
        return exprScalar();
    return def.outputPortTypes[portIdx];
}

TypeExpr inputPortTypeOf(NodeType t, int portIdx) {
    const auto& reg = nodeRegistry();
    auto it = reg.find(t);
    if (it == reg.end()) return exprScalar();
    return inputPortTypeOf(it->second, portIdx);
}

TypeExpr outputPortTypeOf(NodeType t, int portIdx) {
    const auto& reg = nodeRegistry();
    auto it = reg.find(t);
    if (it == reg.end()) return exprScalar();
    return outputPortTypeOf(it->second, portIdx);
}

bool hasDeclaredInputUnit(const NodeDef& def, int portIdx) {
    return portIdx >= 0 &&
           portIdx < static_cast<int>(def.inputPortUnits.size());
}

bool hasDeclaredOutputUnit(const NodeDef& def, int portIdx) {
    return portIdx >= 0 &&
           portIdx < static_cast<int>(def.outputPortUnits.size());
}

scinodes::Unit inputPortUnitOf(const NodeDef& def, int portIdx) {
    if (!hasDeclaredInputUnit(def, portIdx)) return scinodes::Unit{};
    return def.inputPortUnits[portIdx];
}

scinodes::Unit outputPortUnitOf(const NodeDef& def, int portIdx) {
    if (!hasDeclaredOutputUnit(def, portIdx)) return scinodes::Unit{};
    return def.outputPortUnits[portIdx];
}

bool isSceneGraphNode(NodeType t) {
    const auto& reg = nodeRegistry();
    auto it = reg.find(t);
    if (it == reg.end()) return false;
    const NodeDef& def = it->second;
    for (const auto& te : def.inputPortTypes)
        if (isGeometryType(te)) return true;
    for (const auto& te : def.outputPortTypes)
        if (isGeometryType(te)) return true;
    return false;
}

// ===========================================================================
// TypeExpr — fundación de la gramática de tipos (etapa 1 del upgrade
// `doc/grammar_typesystem_upgrade.md`).  Coexiste con el enum PortType
// durante la migración; los call-sites legacy NO se tocan en esta etapa.
// ===========================================================================
bool typeMatches(const TypeExpr& a, const TypeExpr& b) {
    // Single rule: misma variant alternative + mismo contenido.  No hay
    // ramas por subtipo en este código — la discriminación la hace el
    // motor de std::variant.
    if (a.index() != b.index()) return false;
    if (auto* ta = std::get_if<TensorType>(&a))
        return *ta == std::get<TensorType>(b);
    if (auto* ga = std::get_if<GeometryType>(&a))
        return *ga == std::get<GeometryType>(b);
    return false;   // unreachable mientras TypeExpr tenga 2 alternativas
}

std::string describeType(const TypeExpr& t) {
    if (auto* tt = std::get_if<TensorType>(&t)) {
        if (tt->dims.empty()) return "scalar";
        const size_t n = tt->dims.size();
        std::string head;
        if      (n == 1) head = "vec(";
        else if (n == 2) head = "mat(";
        else             head = "tensor(";
        std::string out = head;
        for (size_t i = 0; i < n; ++i) {
            if (i > 0) out += ",";
            out += std::to_string(tt->dims[i]);
        }
        out += ")";
        return out;
    }
    if (std::holds_alternative<GeometryType>(t)) return "geometry";
    return "unknown";
}

unsigned int pinColorFromType(const TypeExpr& t) {
    // Empaquetado IM_COL32(R,G,B,A) compatible con ImGui: byte 0 = R,
    // byte 1 = G, byte 2 = B, byte 3 = A.  La UI lo consume sin saber
    // del backend gráfico.
    auto rgba = [](int r, int g, int b, int a) -> unsigned int {
        return  static_cast<unsigned int>(r        & 0xFF)
             | (static_cast<unsigned int>(g & 0xFF) <<  8)
             | (static_cast<unsigned int>(b & 0xFF) << 16)
             | (static_cast<unsigned int>(a & 0xFF) << 24);
    };
    if (auto* tt = std::get_if<TensorType>(&t)) {
        if (tt->dims.empty())       return rgba(120, 200, 250, 255); // scalar — azul
        if (tt->dims.size() == 1)   return rgba(180, 130, 220, 255); // vec(N) — violeta
        if (tt->dims.size() == 2)   return rgba(220, 160,  60, 255); // mat(R,C) — naranja
        return rgba(220, 120, 200, 255);                              // tensor>2D — magenta
    }
    if (std::holds_alternative<GeometryType>(t))
        return rgba( 80, 200, 200, 255);                              // geometry — cyan
    return rgba(140, 140, 150, 200);                                  // fallback gris
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
        { NodeType::SubGraph,          "SubGraph"          },
        { NodeType::SubGraphInput,     "SubGraphInput"     },
        { NodeType::SubGraphOutput,    "SubGraphOutput"    },
        { NodeType::Object3D,          "Object3D"          },
        { NodeType::TransformObject,   "TransformObject"   },
        { NodeType::SceneOutput,       "SceneOutput"       },
        { NodeType::Vec3Constant,      "Vec3Constant"      },
        { NodeType::CombineXYZ,        "CombineXYZ"        },
        { NodeType::SeparateXYZ,       "SeparateXYZ"       },
        { NodeType::VectorAdd,         "VectorAdd"         },
        { NodeType::VectorSub,         "VectorSub"         },
        { NodeType::VectorScale,       "VectorScale"       },
        { NodeType::VectorDot,         "VectorDot"         },
        { NodeType::VectorCross,       "VectorCross"       },
        { NodeType::VectorLength,      "VectorLength"      },
        { NodeType::VectorNormalize,   "VectorNormalize"   },
        { NodeType::DegToRad,          "DegToRad"          },
        { NodeType::RadToDeg,          "RadToDeg"          },
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
