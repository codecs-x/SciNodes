#include "ScilabCodeGen.hpp"
#include "CustomNodeRegistry.hpp"
#include "NodeInstance.hpp"
#include "NodeType.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <utility>

// ===========================================================================
// Helpers (file-local)
// ===========================================================================
namespace {

double paramValue(const NodeInstance& n, const char* key, double fb) {
    auto it = n.params.find(key);
    return (it != n.params.end()) ? it->second : fb;
}

std::string lit(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return buf;
}

// Index of param `key` in this node's NodeDef::params, or -1 if absent.
int paramIndex(const NodeInstance& n, const char* key) {
    const auto& ps = defOf(n).params;
    for (size_t i = 0; i < ps.size(); ++i)
        if (ps[i].name == key) return (int)i;
    return -1;
}

// Scilab variable name holding a node's live-tunable param value.
std::string paramVar(int nodeId, int idx) {
    char b[32]; std::snprintf(b, sizeof(b), "p_%d_%d", nodeId, idx); return b;
}

// Resolve a param to its Scilab variable name. Falls back to a literal if
// the param is unknown to the registry (defensive — shouldn't happen).
std::string paramRef(const NodeInstance& n, const char* key, double fb) {
    int idx = paramIndex(n, key);
    if (idx < 0) return lit(fb);
    return paramVar(n.id, idx);
}

// Forward decls — definitions appear further below.
bool isStateful(NodeType t);
bool isPureState(NodeType t);

// Topological sort over the EVALUATION-DEPENDENCY graph: an edge A → B
// counts as a dependency only when B's output formula actually reads its
// input (i.e. B is not pure-state). Edges into pure-state nodes are
// dropped, which lets cycles that pass through Integrator / LPF /
// DCMotor sort cleanly. Returns empty on a remaining (algebraic) cycle.
// PID anti-windup: la entrada al puerto 1 del PIDController es la señal
// `u_sat` proveniente de un Saturation aguas abajo.  Esa señal afecta
// solamente a la *derivada* del estado integral (back-calculation), no
// a la salida instantánea del PID.  Para el grafo topológico se trata
// como una arista "rota" — igual que las que entran a pure-state nodes —
// para que el ciclo PID→Saturation→PID:port1 sea legal.
bool isStateOnlyInput(NodeType t, int port) {
    return t == NodeType::PIDController && port == 1;
}

std::vector<int> topoSort(const NodeGraph& g) {
    std::unordered_map<int, int>       indeg;
    std::unordered_map<int, NodeType>  typeOf;
    for (const auto& n : g.nodes()) {
        indeg[n.id]  = 0;
        typeOf[n.id] = n.type;
    }
    auto isBreakingEdge = [&](const Edge& e) -> bool {
        if (isPureState(typeOf[e.toNodeId])) return true;
        const int port = e.toAttrId % 10000;
        return isStateOnlyInput(typeOf[e.toNodeId], port);
    };
    for (const auto& e : g.edges()) {
        if (isBreakingEdge(e)) continue;
        indeg[e.toNodeId]++;
    }

    std::queue<int> q;
    for (const auto& [id, d] : indeg) if (d == 0) q.push(id);

    std::vector<int> order;
    order.reserve(g.nodeCount());
    while (!q.empty()) {
        int u = q.front(); q.pop();
        order.push_back(u);
        for (const auto& e : g.edges()) {
            if (e.fromNodeId != u) continue;
            if (isBreakingEdge(e)) continue;
            if (--indeg[e.toNodeId] == 0) q.push(e.toNodeId);
        }
    }
    if ((int)order.size() != g.nodeCount()) return {};
    return order;
}

// (sourceNodeId, sourcePort) feeding each input port; (-1, 0) if unconnected.
using SrcRef = std::pair<int, int>;
std::vector<SrcRef> inputSources(const NodeGraph& g, const NodeInstance& dst) {
    int inputs = defOf(dst).inputPorts;
    std::vector<SrcRef> src(inputs, { -1, 0 });
    for (const auto& e : g.edges()) {
        if (e.toNodeId != dst.id) continue;
        int port    = e.toAttrId   % 10000;
        int srcPort = (e.fromAttrId % 10000) - 9000;
        if (port >= 0 && port < inputs) src[port] = { e.fromNodeId, srcPort };
    }
    return src;
}

// Variable holding node <id>'s output at <port>.  Port 0 uses the short
// form (v<id>) so the bulk of single-output graphs read naturally; ports
// ≥ 1 get a suffix (v<id>_<port>).
std::string varName(int nodeId, int port = 0) {
    char b[32];
    if (port == 0) std::snprintf(b, sizeof(b), "v%d",     nodeId);
    else           std::snprintf(b, sizeof(b), "v%d_%d",  nodeId, port);
    return b;
}

bool isStateful(NodeType t) {
    switch (t) {
        case NodeType::Integrator:
        case NodeType::LowPassFilter:
        case NodeType::PIDController:
        case NodeType::DCMotorModel:
        case NodeType::Differentiator:
        case NodeType::TransferFunction:
        case NodeType::TransferFunction2:
        case NodeType::AirgapFluxDensity:
        case NodeType::ThermalMass:
        case NodeType::ThermalNode:
            return true;
        default:
            return false;
    }
}

// "Pure-state" = the node's output is a state variable x(slot), with NO
// direct dependency on its current input. Such nodes break algebraic
// loops in a feedback path: the cycle's value at time t is determined by
// the integrated state, not by an instantaneous expression.
//
// PIDController and Differentiator both have direct input feedthrough
// (PID: Kp·input; Diff: ωc·input) and so cannot break a cycle on their own.
bool isPureState(NodeType t) {
    switch (t) {
        case NodeType::Integrator:
        case NodeType::LowPassFilter:
        case NodeType::DCMotorModel:
        case NodeType::TransferFunction:
        case NodeType::TransferFunction2:
        case NodeType::AirgapFluxDensity:
        case NodeType::ThermalMass:
        case NodeType::ThermalNode:
            return true;
        default:
            return false;
    }
}

int stateWidth(NodeType t) {
    switch (t) {
        case NodeType::Integrator:        return 1;
        case NodeType::LowPassFilter:     return 1;
        case NodeType::PIDController:     return 2;
        case NodeType::Differentiator:    return 1;
        case NodeType::TransferFunction:  return 1;
        case NodeType::TransferFunction2: return 2;
        case NodeType::DCMotorModel:      return 2;
        case NodeType::AirgapFluxDensity: return 1;
        case NodeType::ThermalMass:       return 1;
        case NodeType::ThermalNode:       return 1;
        default:                          return 0;
    }
}

struct NodePlan {
    int                       id;
    // One Scilab expression per output port (size == NodeDef::outputPorts).
    std::vector<std::string>  outputExprs;
    int                       stateSlot   = 0;
    int                       stateWidth  = 0;
    std::vector<std::string>  ic;     // each entry is a Scilab expression
    std::vector<std::string>  deriv;
};

// Substitute Custom-node expression placeholders:
//   u<N>      → the Scilab variable holding source feeding input port N-1
//               (1-based to match the published grammar reference)
//   p_<name>  → the variable holding the live param value
//
// Tokens are recognised as full identifiers — we never replace inside
// the middle of a longer name. Unknown identifiers are passed through
// verbatim, so Scilab built-ins (sin, cos, t, %pi, ...) keep working.
std::string substituteCustom(const std::string& expr,
                              const NodeInstance& n,
                              const std::vector<SrcRef>& srcs) {
    auto srcVar = [&](int port) -> std::string {
        if (port >= 0 && port < (int)srcs.size() && srcs[port].first >= 0)
            return varName(srcs[port].first, srcs[port].second);
        return "0.0";
    };

    auto isIdStart = [](char c){
        return std::isalpha((unsigned char)c) || c == '_';
    };
    auto isIdCont  = [](char c){
        return std::isalnum((unsigned char)c) || c == '_';
    };

    std::string out;
    out.reserve(expr.size() * 2);

    size_t i = 0;
    while (i < expr.size()) {
        char c = expr[i];
        if (!isIdStart(c)) { out += c; ++i; continue; }

        size_t j = i + 1;
        while (j < expr.size() && isIdCont(expr[j])) ++j;
        std::string tok = expr.substr(i, j - i);

        // u<digits>?
        if (tok.size() >= 2 && tok[0] == 'u') {
            bool allDigits = true;
            for (size_t k = 1; k < tok.size(); ++k)
                if (!std::isdigit((unsigned char)tok[k])) { allDigits = false; break; }
            if (allDigits) {
                int port = std::stoi(tok.substr(1)) - 1;
                out += srcVar(port);
                i = j;
                continue;
            }
        }

        // p_<name>?
        if (tok.size() > 2 && tok[0] == 'p' && tok[1] == '_') {
            std::string pname = tok.substr(2);
            int idx = paramIndex(n, pname.c_str());
            if (idx >= 0) {
                out += paramVar(n.id, idx);
                i = j;
                continue;
            }
            // Unknown param — fall through to verbatim so a typo surfaces
            // as a Scilab "Undefined variable" error rather than silently
            // becoming 0.
        }

        out += tok;
        i = j;
    }
    return out;
}

NodePlan planNode(const NodeInstance& n, int slotStart,
                  const std::vector<SrcRef>& srcs) {
    NodePlan p;
    p.id          = n.id;
    p.stateSlot   = isStateful(n.type) ? slotStart : 0;
    p.stateWidth  = stateWidth(n.type);

    auto src = [&](int port) -> std::string {
        if (port < (int)srcs.size() && srcs[port].first >= 0)
            return varName(srcs[port].first, srcs[port].second);
        return "0.0";
    };

    // Default: 1 output. Multi-output cases (InverseKinematics) resize first.
    p.outputExprs.assign(1, std::string{});

    switch (n.type) {
        // ---- Sources ----------------------------------------------------
        case NodeType::VoltageSource:
            p.outputExprs[0] = paramRef(n, "Voltage", 12.0);
            break;
        case NodeType::CurrentSource:
            p.outputExprs[0] = paramRef(n, "Current", 1.0);
            break;
        case NodeType::StepSignal: {
            std::string t0  = paramRef(n, "Step Time", 0.0);
            std::string amp = paramRef(n, "Amplitude", 1.0);
            p.outputExprs[0] = "(t >= " + t0 + ") * " + amp;
            break;
        }
        case NodeType::SineSignal: {
            std::string A  = paramRef(n, "Amplitude", 1.0);
            std::string f  = paramRef(n, "Frequency", 1.0);
            std::string ph = paramRef(n, "Phase",     0.0);
            p.outputExprs[0] = A + " * sin(2*%pi*" + f + "*t + " + ph + ")";
            break;
        }
        case NodeType::RampSignal:
            p.outputExprs[0] = paramRef(n, "Slope", 1.0) + " * t";
            break;
        case NodeType::DesignTemplate: {
            // Four constant outputs — one per design-requirement param.
            // Order matches NodeDef::params so external consumers can
            // wire by port index.
            p.outputExprs.resize(4);
            p.outputExprs[0] = paramRef(n, "Target Torque",  10.0);
            p.outputExprs[1] = paramRef(n, "Target Speed",  150.0);
            p.outputExprs[2] = paramRef(n, "Bus Voltage",   400.0);
            p.outputExprs[3] = paramRef(n, "Cooling Class",   1.0);
            break;
        }

        // ---- Stateless transformers -------------------------------------
        case NodeType::Gain:
            p.outputExprs[0] = paramRef(n, "K", 1.0) + " * " + src(0);
            break;
        case NodeType::Summation:
            p.outputExprs[0] = paramRef(n, "Sign1", 1.0) + " * " + src(0) + " + "
                         + paramRef(n, "Sign2", 1.0) + " * " + src(1);
            break;
        case NodeType::Saturation: {
            std::string x  = src(0);
            std::string lo = paramRef(n, "Min", -1.0);
            std::string hi = paramRef(n, "Max",  1.0);
            p.outputExprs[0] = "max(min(" + x + ", " + hi + "), " + lo + ")";
            break;
        }
        case NodeType::GearTransmission: {
            // Reductor N:1 con pérdidas:  ω_load = (Eff / Ratio) · ω_motor.
            // Convención estándar de robótica (Spong Cap. 6, Jazar §1.2.6):
            // una reducción 50:1 hace que la carga gire 50× más lento que
            // el motor, modulado por la eficiencia (≤ 1).  Versión previa
            // multiplicaba (Ratio · Eff), que es físicamente incorrecto.
            std::string r = paramRef(n, "Ratio",      10.0);
            std::string e = paramRef(n, "Efficiency", 0.95);
            p.outputExprs[0] = "(" + e + " / " + r + ") * " + src(0);
            break;
        }
        case NodeType::InverseKinematics: {
            // 2-link planar IK, elbow-up:
            //   target (x, y) → joint angles (θ₁, θ₂)
            //   c2 = (x²+y² − L1² − L2²) / (2·L1·L2)         ← cos(θ₂)
            //   c2_clamped = clamp(c2, -1, 1)                   ← keep target reachable
            //   s2 = sqrt(1 − c2_clamped²)                     ← sin(θ₂), elbow up
            //   θ₂ = atan2(s2, c2_clamped)
            //   θ₁ = atan2(y, x) − atan2(L2·s2, L1 + L2·c2_clamped)
            // Stateless, two outputs. Scilab's atan(y,x) is the 2-arg
            // arctangent (== atan2).
            std::string L1 = paramRef(n, "Link 1 L", 0.3);
            std::string L2 = paramRef(n, "Link 2 L", 0.2);
            std::string x  = src(0);
            std::string y  = src(1);

            std::string c2 = "max(min(((" + x + ")^2 + (" + y + ")^2 - "
                           + L1 + "^2 - " + L2 + "^2) / (2*" + L1 + "*"
                           + L2 + "), 1), -1)";
            std::string s2 = "sqrt(1 - (" + c2 + ")^2)";

            p.outputExprs.resize(2);
            p.outputExprs[0] = "atan(" + y + ", " + x + ") - atan("
                             + L2 + "*(" + s2 + "), " + L1 + " + "
                             + L2 + "*(" + c2 + "))";
            p.outputExprs[1] = "atan((" + s2 + "), (" + c2 + "))";
            break;
        }
        case NodeType::PMSMEfficiency: {
            // Simple analytical efficiency model. Saturates to 0 when the
            // total input power is zero to avoid Scilab returning %nan at
            // T = omega = 0 (start of a sweep, for instance).
            std::string T   = src(0);
            std::string w   = src(1);
            std::string Ke  = src(2);
            std::string R   = paramRef(n, "Stator Resistance", 0.5);
            std::string Ki  = paramRef(n, "Iron Loss Coeff.",  1e-4);
            std::string Km  = paramRef(n, "Mech Loss Coeff.",  1e-3);

            // Guard Iq for Ke -> 0; Scilab's division returns %inf, which
            // would propagate as %nan after the eta formula. The (Ke + 1e-12)
            // term is a numerical safety net; physically Ke > 0 always.
            std::string Iq    = "(" + T + " / (" + Ke + " + 1e-12))";
            std::string P_out = "(" + T + " * " + w + ")";
            std::string P_cu  = "(1.5 * " + R + " * " + Iq + "^2)";
            std::string P_fe  = "(" + Ki + " * " + w + "^2)";
            std::string P_mech= "(" + Km + " * abs(" + w + "))";
            std::string P_in  = "(" + P_out + " + " + P_cu + " + "
                                    + P_fe + " + " + P_mech + ")";
            // Final guard: if P_in is non-positive (idle), eta = 0.
            p.outputExprs[0] =
                "bool2s(" + P_in + " > 1e-9) .* (" + P_out + " ./ ("
                + P_in + " + 1e-12))";
            break;
        }
        // ---- Stage v0.9 loss sources -----------------------------------
        case NodeType::JouleLoss: {
            // Stator copper loss. Iq = T / Ke; P_cu = (3/2) * R * Iq^2.
            // Guard Ke -> 0 the same way PMSMEfficiency does.
            std::string T  = src(0);
            std::string Ke = src(1);
            std::string R  = paramRef(n, "Stator Resistance", 0.5);
            std::string Iq = "(" + T + " / (" + Ke + " + 1e-12))";
            p.outputExprs[0] = "1.5 * " + R + " * " + Iq + "^2";
            break;
        }
        case NodeType::CoreLoss: {
            // Bertotti two-term iron loss.
            //   f_e   = p * omega / (2*pi)
            //   P_fe  = K_hys * f_e * B^2 + K_eddy * f_e^2 * B^2
            std::string w  = src(0);
            std::string B  = src(1);
            std::string Kh = paramRef(n, "Hysteresis Coeff.", 0.02);
            std::string Ke = paramRef(n, "Eddy Coeff.",       1e-5);
            std::string pp = paramRef(n, "Pole Pairs",        4.0);
            std::string fe = "(" + pp + " * abs(" + w + ") / (2 * %pi))";
            p.outputExprs[0] =
                Kh + " * " + fe + " * " + B + "^2 + "
              + Ke + " * " + fe + "^2 * " + B + "^2";
            break;
        }
        case NodeType::MechanicalLoss: {
            // Friction + windage:
            //   P_mech = K_visc * |omega| + K_drag * omega^2
            std::string w  = src(0);
            std::string Kv = paramRef(n, "Viscous Coeff.", 1e-3);
            std::string Kd = paramRef(n, "Drag Coeff.",    1e-5);
            p.outputExprs[0] = Kv + " * abs(" + w + ") + " + Kd + " * " + w + "^2";
            break;
        }
        case NodeType::ThermalMass: {
            // Single-node RC thermal mass. Pure-state stateful:
            //   x  = T_node (K)
            //   dx/dt = (P_in - (x - T_amb) / R_th) / C_th
            //   y  = x
            // Initial condition x(0) = T_ambient so the node starts at
            // room temperature rather than 0 K.
            char slot[16]; std::snprintf(slot, sizeof(slot), "x(%d)", p.stateSlot);
            std::string Tnode = slot;
            std::string C  = paramRef(n, "Thermal Capacitance", 500.0);
            std::string R  = paramRef(n, "Thermal Resistance",    0.5);
            std::string Ta = paramRef(n, "Ambient Temperature", 298.0);
            p.outputExprs[0] = Tnode;
            p.ic    = { Ta };
            p.deriv = { "(" + src(0) + " - (" + Tnode + " - " + Ta
                      + ") / " + R + ") / " + C };
            break;
        }
        case NodeType::ThermalNode: {
            // Pure heat-capacitance node: state x = T (K); the four
            // input ports carry signed heat flows, all summed before
            // division by C. Disconnected inputs yield "0.0" through
            // src(), so the user only wires what's actually present.
            char slot[16]; std::snprintf(slot, sizeof(slot), "x(%d)", p.stateSlot);
            std::string T  = slot;
            std::string C  = paramRef(n, "Thermal Capacitance", 500.0);
            std::string T0 = paramRef(n, "Initial Temperature", 298.0);
            p.outputExprs[0] = T;
            p.ic    = { T0 };
            p.deriv = { "(" + src(0) + " + " + src(1)
                      + " + " + src(2) + " + " + src(3) + ") / " + C };
            break;
        }
        case NodeType::ThermalResistance: {
            // Linear conduction / convection. Two outputs so the user
            // never needs a Summation to flip signs on the hot side.
            std::string Th = src(0);
            std::string Tc = src(1);
            std::string R  = paramRef(n, "Thermal Resistance", 1.0);
            p.outputExprs.resize(2);
            p.outputExprs[0] = "(" + Th + " - " + Tc + ") / " + R;
            p.outputExprs[1] = "(" + Tc + " - " + Th + ") / " + R;
            break;
        }
        case NodeType::CoolingSystem: {
            // Source of cooling-system knobs. Three constant outputs
            // matching the registry parameter order so downstream
            // consumers can wire by port index.
            p.outputExprs.resize(3);
            p.outputExprs[0] = paramRef(n, "Fan Flow",              5.0);
            p.outputExprs[1] = paramRef(n, "Water Flow",            0.0);
            p.outputExprs[2] = paramRef(n, "Ambient Temperature", 298.0);
            break;
        }
        case NodeType::MaxwellForce: {
            // Radial Maxwell stress from the air-gap flux density.
            //   sigma_r = B_g^2 / (2 * mu_0)
            // mu_0 = 4*pi*1e-7 inlined (same convention used by
            // PMSMElectromagnetic / CoreLoss).
            std::string B = src(0);
            p.outputExprs[0] = B + "^2 / (2 * 4 * %pi * 1e-7)";
            break;
        }
        case NodeType::TolerancePerturbator: {
            // Adds a uniform random perturbation in [-h, +h] to its
            // input each step. Scilab's rand() returns one uniform
            // [0,1] sample per call, so each emitTopoEval pass draws a
            // fresh perturbation per perturbator instance.
            std::string u = src(0);
            std::string h = paramRef(n, "Half Tolerance", 0.05);
            p.outputExprs[0] = u + " + " + h + " * (2 * rand() - 1)";
            break;
        }
        case NodeType::ModalFrequency: {
            // Thin-ring natural frequency for mode m:
            //   f_m = (t / (2*pi * R^2)) * sqrt(E / (12 * rho))
            //         * m * (m^2 - 1) / sqrt(m^2 + 1)
            // Mode m = 0 or 1 are rigid-body / translation — no
            // structural energy. Scilab's bool2s guards the m<=1 case
            // so the user can sweep m as a param without divide-or-
            // sqrt domain errors.
            std::string R = src(0);
            std::string E = paramRef(n, "Young's Modulus", 200.0e9);
            std::string rho = paramRef(n, "Density",       7850.0);
            std::string t   = paramRef(n, "Thickness",       0.02);
            std::string m   = paramRef(n, "Mode Order",      2.0);
            // shape_factor = m * (m^2 - 1) / sqrt(m^2 + 1), zeroed for m<=1
            std::string shape =
                "bool2s(" + m + " > 1.5) * " + m + " * "
                "(" + m + "^2 - 1) / sqrt(" + m + "^2 + 1)";
            p.outputExprs[0] =
                "(" + t + " / (2 * %pi * " + R + "^2 + 1e-12)) "
                "* sqrt(" + E + " / (12 * " + rho + ")) "
                "* (" + shape + ")";
            break;
        }
        case NodeType::ConvectiveCooling: {
            // q = h(flow) * (T_hot - T_cold)   with h = h_0 + h_slope * flow.
            std::string Th    = src(0);
            std::string Tc    = src(1);
            std::string flow  = src(2);
            std::string h0    = paramRef(n, "Base Coeff. h_0", 1.0);
            std::string hk    = paramRef(n, "Slope per Flow",  0.5);
            std::string h     = "(" + h0 + " + " + hk + " * " + flow + ")";
            p.outputExprs.resize(2);
            p.outputExprs[0] = h + " * (" + Th + " - " + Tc + ")";
            p.outputExprs[1] = h + " * (" + Tc + " - " + Th + ")";
            break;
        }
        case NodeType::AirgapFluxDensity: {
            // Pure-state stateful: x = rotor angle, dx/dt = omega.
            //   y = B_peak * ( sin(p*x) + a3*sin(3*p*x) + a_slot*sin(N_s*x) )
            char slot[16]; std::snprintf(slot, sizeof(slot), "x(%d)", p.stateSlot);
            std::string th = slot;
            std::string B  = paramRef(n, "Peak Flux Density",   0.85);
            std::string pp = paramRef(n, "Pole Pairs",          4.0);
            std::string a3 = paramRef(n, "3rd Harmonic Ratio",  0.10);
            std::string as = paramRef(n, "Slot Harmonic Ratio", 0.05);
            std::string Ns = paramRef(n, "Slot Count",         24.0);
            p.outputExprs[0] =
                B + " * (sin(" + pp + " * " + th + ") + "
                  + a3 + " * sin(3 * " + pp + " * " + th + ") + "
                  + as + " * sin(" + Ns + " * " + th + "))";
            p.ic    = { "0.0" };
            p.deriv = { src(0) };       // d(theta)/dt = omega
            break;
        }
        case NodeType::PMSMElectromagnetic: {
            // Surface-PMSM lumped-parameter electromagnetic model.
            //
            //   Ke   = (kw * Nph * p * Bg * L * D) / 2
            //   g_eff = g + hm / mu_r            (NdFeB μ_r = 1.05)
            //   L_ph = (mu0 * Nph^2 * kw^2 * pi * D * L) / (8 * p^2 * g_eff)
            //   Vrms = Ke * omega / sqrt(2)
            //   Tcog = (Bg^2 * D^2 * L) / (8 * mu0 * Nslots)
            //
            // All stateless — no ODE, no state slots.
            std::string D  = src(0);
            std::string L  = src(1);
            std::string w  = src(2);
            std::string N  = paramRef(n, "Turns per Phase",       100.0);
            std::string kw = paramRef(n, "Winding Factor",          0.95);
            std::string pp = paramRef(n, "Pole Pairs",              4.0);
            std::string Bg = paramRef(n, "Airgap Flux Density",     0.85);
            std::string g  = paramRef(n, "Mechanical Airgap",       0.001);
            std::string hm = paramRef(n, "Magnet Thickness",        0.003);
            std::string Ns = paramRef(n, "Slot Count",             24.0);
            const char* mu0  = "(4*%pi*1e-7)";
            const char* mu_r = "1.05";

            std::string Ke   =
                "(" + kw + " * " + N + " * " + pp + " * " + Bg
                + " * " + L + " * " + D + ") / 2";
            std::string g_eff = "(" + g + " + " + hm + " / " + mu_r + ")";
            std::string L_ph =
                "(" + std::string(mu0) + " * " + N + "^2 * " + kw + "^2"
                + " * %pi * " + D + " * " + L + ") / "
                "(8 * " + pp + "^2 * " + g_eff + ")";
            std::string Vrms = "(" + Ke + ") * " + w + " / sqrt(2)";
            std::string Tcog =
                "(" + Bg + "^2 * " + D + "^2 * " + L + ") / "
                "(8 * " + std::string(mu0) + " * " + Ns + ")";

            p.outputExprs.resize(4);
            p.outputExprs[0] = Ke;
            p.outputExprs[1] = L_ph;
            p.outputExprs[2] = Vrms;
            p.outputExprs[3] = Tcog;
            break;
        }
        case NodeType::IPMSizing: {
            // Same closed-form as PMSMSizing, with the achievable torque
            // multiplied by the saliency factor k_sal so the bore comes
            // out smaller for the same target torque.
            std::string T  = src(0);
            std::string w  = src(1);
            std::string B  = paramRef(n, "Magnetic Loading B",      0.85);
            std::string A  = paramRef(n, "Electric Loading A", 40000.0);
            std::string al = paramRef(n, "Aspect Ratio L/D",        1.2);
            std::string ks = paramRef(n, "Saliency Factor",         1.2);

            std::string D_cubed =
                "(2 * " + T + ") / (%pi * " + B + " * " + A + " * "
                + al + " * " + ks + ")";
            std::string D_expr = "((" + D_cubed + ")^(1.0/3.0))";

            p.outputExprs.resize(3);
            p.outputExprs[0] = D_expr;
            p.outputExprs[1] = al + " * " + D_expr;
            p.outputExprs[2] = T + " * " + w;
            break;
        }
        case NodeType::BLDCSizing: {
            // Same form as PMSMSizing but with a trapezoidal factor that
            // raises the effective torque density.
            std::string T  = src(0);
            std::string w  = src(1);
            std::string B  = paramRef(n, "Magnetic Loading B",      0.90);
            std::string A  = paramRef(n, "Electric Loading A", 35000.0);
            std::string al = paramRef(n, "Aspect Ratio L/D",        1.0);
            std::string kt = paramRef(n, "Trapezoidal Factor",      1.15);

            std::string D_cubed =
                "(2 * " + T + ") / (%pi * " + B + " * " + A + " * "
                + al + " * " + kt + ")";
            std::string D_expr = "((" + D_cubed + ")^(1.0/3.0))";

            p.outputExprs.resize(3);
            p.outputExprs[0] = D_expr;
            p.outputExprs[1] = al + " * " + D_expr;
            p.outputExprs[2] = T + " * " + w;
            break;
        }
        case NodeType::PMSMSizing: {
            // Surface-mount PMSM classical sizing.
            //   D^2*L = 2*T / (pi*B*A)
            //   L = alpha*D   →   D^3 = 2*T / (pi*B*A*alpha)
            //   P = T*omega
            // All stateless — outputs depend only on inputs and params.
            std::string T  = src(0);
            std::string w  = src(1);
            std::string B  = paramRef(n, "Magnetic Loading B",      0.85);
            std::string A  = paramRef(n, "Electric Loading A", 40000.0);
            std::string al = paramRef(n, "Aspect Ratio L/D",        1.2);

            std::string D_cubed =
                "(2 * " + T + ") / (%pi * " + B + " * " + A + " * " + al + ")";
            // Use `^(1/3)` for the cube root — equivalent to Scilab's nthroot.
            std::string D_expr = "((" + D_cubed + ")^(1.0/3.0))";

            p.outputExprs.resize(3);
            p.outputExprs[0] = D_expr;                              // bore D
            p.outputExprs[1] = al + " * " + D_expr;                 // stack L
            p.outputExprs[2] = T + " * " + w;                       // rated P
            break;
        }

        // ---- Stateful transformers (integrated by Scilab ode rk) --------
        case NodeType::Integrator: {
            char slot[16]; std::snprintf(slot, sizeof(slot), "x(%d)", p.stateSlot);
            p.outputExprs[0] = slot;
            p.ic    = { paramRef(n, "Initial Cond.", 0.0) };
            p.deriv = { src(0) };
            break;
        }
        case NodeType::LowPassFilter: {
            char slot[16]; std::snprintf(slot, sizeof(slot), "x(%d)", p.stateSlot);
            p.outputExprs[0] = slot;
            std::string fc = paramRef(n, "Cutoff Freq.", 100.0);
            p.ic    = { "0.0" };
            p.deriv = { "2*%pi*" + fc + " * (" + src(0) + " - " + slot + ")" };
            break;
        }
        case NodeType::Differentiator: {
            // Filtered derivative  H(s) = s / (1 + s/wc).
            // State x = first-order low-pass of input;  y = wc·(u − x).
            // dx/dt = wc·(u − x). At steady state for slow inputs, y ≈ du/dt.
            char slot[16]; std::snprintf(slot, sizeof(slot), "x(%d)", p.stateSlot);
            std::string fc = paramRef(n, "Cutoff Freq.", 100.0);
            std::string wc = "(2*%pi*" + fc + ")";
            p.outputExprs[0] = wc + " * (" + src(0) + " - " + slot + ")";
            p.ic    = { "0.0" };
            p.deriv = { wc + " * (" + src(0) + " - " + slot + ")" };
            break;
        }
        case NodeType::TransferFunction: {
            // First-order rational  H(s) = num[0] / (den[0] + den[1]·s).
            // State x = output;  a1·dx/dt + a0·x = b·u  →  dx/dt = (b·u − a0·x)/a1.
            // Pure-state (output = x, no feedthrough). a1 must be non-zero.
            char slot[16]; std::snprintf(slot, sizeof(slot), "x(%d)", p.stateSlot);
            std::string b  = paramRef(n, "num[0]", 1.0);
            std::string a0 = paramRef(n, "den[0]", 1.0);
            std::string a1 = paramRef(n, "den[1]", 1.0);
            p.outputExprs[0] = slot;
            p.ic    = { "0.0" };
            p.deriv = { "(" + b + "*" + src(0) + " - " + a0 + "*" + slot
                        + ") / " + a1 };
            break;
        }
        case NodeType::TransferFunction2: {
            // Second-order rational, monic denominator:
            //   H(s) = (b1·s + b0) / (s² + a1·s + a0)
            // Controllable canonical form, 2 states:
            //   x1 (= output), x2 (= dx1/dt)
            //   ẋ1 = x2
            //   ẋ2 = -a0·x1 - a1·x2 + u
            //   y  = b0·x1 + b1·x2     ← pure-state (no input feedthrough)
            char x1[16], x2[16];
            std::snprintf(x1, sizeof(x1), "x(%d)", p.stateSlot);
            std::snprintf(x2, sizeof(x2), "x(%d)", p.stateSlot + 1);
            std::string b0 = paramRef(n, "num[0]", 1.0);
            std::string b1 = paramRef(n, "num[1]", 0.0);
            std::string a0 = paramRef(n, "den[0]", 1.0);
            std::string a1 = paramRef(n, "den[1]", 0.0);
            p.outputExprs[0] = b0 + "*" + x1 + " + " + b1 + "*" + x2;
            p.ic    = { "0.0", "0.0" };
            p.deriv = {
                x2,
                "-" + a0 + "*" + x1 + " - " + a1 + "*" + x2 + " + " + src(0)
            };
            break;
        }
        case NodeType::PIDController: {
            // Standard PID with filtered derivative + optional back-calculation
            // anti-windup (Åström & Hägglund, 2006):
            //   y     = Kp·err + Ki·∫err + Kd·N·(err − err_lp)
            //   d(∫err)/dt = err + Kt·(u_sat − y)
            //   d(err_lp)/dt = N·(err − err_lp)
            //
            // Port 0 = err (signal feedback).
            // Port 1 = u_sat (post-saturation control signal).  Optional:
            //   if disconnected, u_sat is taken equal to y so the windup
            //   correction collapses to zero — PI/PID puro como antes.
            //
            // El input al puerto 1 sólo aparece en la derivada de ∫err, no
            // en la salida instantánea, por lo que cualquier ciclo que pase
            // por él se rompe topológicamente (ver isStateOnlyInput).
            char ei[16], el[16];
            std::snprintf(ei, sizeof(ei), "x(%d)", p.stateSlot);
            std::snprintf(el, sizeof(el), "x(%d)", p.stateSlot + 1);
            std::string Kp = paramRef(n, "Kp", 1.0);
            std::string Ki = paramRef(n, "Ki", 0.0);
            std::string Kd = paramRef(n, "Kd", 0.0);
            std::string N  = paramRef(n, "N (filter)", 100.0);
            std::string Kt = paramRef(n, "Kt (anti-windup)", 0.0);
            const std::string err = src(0);
            const std::string y   = Kp + " * " + err + " + " + Ki + " * " + ei
                                  + " + " + Kd + " * " + N + " * (" + err
                                  + " - " + el + ")";
            // u_sat: si el puerto 1 está conectado, usamos esa señal;
            // de lo contrario, asumimos saturación neutra (u_sat ≡ y).
            const std::string u_sat = (srcs.size() > 1 && srcs[1].first >= 0)
                                      ? src(1)
                                      : ("(" + y + ")");
            p.outputExprs[0] = y;
            p.ic    = { "0.0", "0.0" };
            p.deriv = {
                err + " + " + Kt + " * ((" + u_sat + ") - (" + y + "))",
                N + " * (" + err + " - " + el + ")"
            };
            break;
        }
        case NodeType::DCMotorModel: {
            char i[16], w[16];
            std::snprintf(i, sizeof(i), "x(%d)", p.stateSlot);
            std::snprintf(w, sizeof(w), "x(%d)", p.stateSlot + 1);
            std::string Ra = paramRef(n, "Ra", 1.0);
            std::string La = paramRef(n, "La", 0.01);
            std::string Ke = paramRef(n, "Ke", 0.1);
            std::string Kt = paramRef(n, "Kt", 0.1);
            std::string J  = paramRef(n, "J",  0.01);
            std::string B  = paramRef(n, "B",  0.001);
            p.outputExprs[0] = w;
            p.ic    = { "0.0", "0.0" };
            p.deriv = {
                "(" + src(0) + " - " + Ra + "*" + i + " - " + Ke + "*" + w + ") / " + La,
                "(" + Kt + "*" + i + " - " + B + "*" + w + ") / " + J
            };
            break;
        }

        // ---- Sinks: record their input(s) ------------------------------
        case NodeType::FFTAnalyzer:
        case NodeType::DataLogger:
        case NodeType::TerminalDisplay:
        case NodeType::View3DSink:
        case NodeType::View3DThermalSink:
        case NodeType::DistributionSink:
            p.outputExprs[0] = src(0);
            break;
        case NodeType::Oscilloscope: {
            // Multi-canal: emitimos una columna por cada puerto
            // CONECTADO (no por cada puerto declarado en el catálogo).
            // Así los Oscilloscopes con 1 input efectivo siguen
            // generando un solo canal y los tests legacy no rompen.
            const auto& def = defOf(n);
            std::vector<int> connectedPorts;
            connectedPorts.reserve(def.inputPorts);
            for (int i = 0; i < (int)srcs.size(); ++i)
                if (srcs[i].first >= 0) connectedPorts.push_back(i);
            if (connectedPorts.empty()) {
                p.outputExprs[0] = "0.0";  // sin conexiones: 1 canal cero
            } else {
                p.outputExprs.resize(connectedPorts.size());
                for (size_t k = 0; k < connectedPorts.size(); ++k)
                    p.outputExprs[k] = src(connectedPorts[k]);
            }
            break;
        }
        case NodeType::PhasePortrait:
            // Two channels: input 0 plots on the x-axis, input 1 on y.
            p.outputExprs.resize(2);
            p.outputExprs[0] = src(0);
            p.outputExprs[1] = src(1);
            break;
        case NodeType::HeatmapSink:
        case NodeType::View3DDeformationSink:
            // Three channels: x/y/c for heatmap; freq/mode/amp for the
            // 3-D deformation overlay. Same layout, both pure sinks.
            p.outputExprs.resize(3);
            p.outputExprs[0] = src(0);
            p.outputExprs[1] = src(1);
            p.outputExprs[2] = src(2);
            break;

        // ---- JSON-loaded custom nodes ----------------------------------
        // Stateless transformers / sources use the descriptor's
        // expression with u<N>/p_<name> placeholders substituted.
        // Sinks just record their first input, matching every builtin
        // sink. State-bearing custom nodes are out of scope (no
        // expression schema for dynamics yet).
        case NodeType::Custom: {
            const auto* cd =
                scinodes::customNodes().find(n.customType);
            NodeCategory cat = cd ? cd->category : NodeCategory::Transformer;
            if (cat == NodeCategory::Sink) {
                p.outputExprs[0] = src(0);
            } else if (cd && !cd->expression.empty()) {
                p.outputExprs[0] = substituteCustom(cd->expression, n, srcs);
            } else {
                p.outputExprs[0] = "0.0";
            }
            break;
        }

        default:
            p.outputExprs[0] = "0.0";
            break;
    }
    return p;
}

void emitTopoEval(std::ostringstream& out,
                  const std::vector<int>& order,
                  const std::unordered_map<int, NodePlan>& plans,
                  const char* indent) {
    for (int id : order) {
        auto it = plans.find(id);
        if (it == plans.end()) continue;
        for (int p = 0; p < (int)it->second.outputExprs.size(); ++p)
            out << indent << varName(id, p) << " = "
                << it->second.outputExprs[p] << ";\n";
    }
}

} // namespace

// ===========================================================================
// Public API
// ===========================================================================
bool ScilabCodeGen::isSupported(NodeType t) {
    switch (t) {
        // Sources
        case NodeType::VoltageSource: case NodeType::CurrentSource:
        case NodeType::StepSignal:    case NodeType::SineSignal:
        case NodeType::RampSignal:    case NodeType::DesignTemplate:
        case NodeType::CoolingSystem:
        // Stateless
        case NodeType::Gain:          case NodeType::Summation:
        case NodeType::Saturation:    case NodeType::GearTransmission:
        case NodeType::InverseKinematics:
        case NodeType::PMSMSizing:
        case NodeType::IPMSizing:
        case NodeType::BLDCSizing:
        case NodeType::PMSMElectromagnetic:
        case NodeType::AirgapFluxDensity:
        case NodeType::PMSMEfficiency:
        // Stage v0.9 thermal-network nodes
        case NodeType::JouleLoss:
        case NodeType::CoreLoss:
        case NodeType::MechanicalLoss:
        case NodeType::ThermalMass:
        case NodeType::ThermalNode:
        case NodeType::ThermalResistance:
        case NodeType::ConvectiveCooling:
        // Stage v1.0 structural / NVH
        case NodeType::MaxwellForce:
        case NodeType::ModalFrequency:
        case NodeType::TolerancePerturbator:
        // Stateful
        case NodeType::Integrator:    case NodeType::LowPassFilter:
        case NodeType::PIDController: case NodeType::DCMotorModel:
        case NodeType::Differentiator:case NodeType::TransferFunction:
        case NodeType::TransferFunction2:
        // Sinks
        case NodeType::Oscilloscope:  case NodeType::FFTAnalyzer:
        case NodeType::PhasePortrait: case NodeType::DataLogger:
        case NodeType::TerminalDisplay: case NodeType::View3DSink:
        case NodeType::View3DThermalSink:
        case NodeType::View3DDeformationSink:
        case NodeType::HeatmapSink:
        case NodeType::DistributionSink:
        // SubGraph y sus stubs viven sólo en grafos antes del flatten;
        // el `generate()` aplana antes de emitir, así que cualquier
        // instancia que llegue al codegen real se considera "soportada"
        // únicamente porque pasó el chequeo de catálogo.
        case NodeType::SubGraph:
        case NodeType::SubGraphInput:
        case NodeType::SubGraphOutput:
        // JSON-loaded user types
        case NodeType::Custom:
            return true;
        default:
            return false;
    }
}

// Flatten one SubGraph node: replace it inline with its child contents,
// rewiring the child's `SubGraphInput`/`SubGraphOutput` stubs to the
// external sources/consumers that used to connect to the SubGraph's
// external ports.  Returns true on success.
//
// `g` is mutated: the SubGraph node and its external edges disappear;
// the child's non-stub nodes are added with fresh ids and the internal
// wiring is re-emitted.  Recursive SubGraphs inside the child are
// handled by the caller's outer loop (one call per SubGraph node).
// Map nodeId-en-grafo-aplanado → path canónico [sg_padre, ..., original_id].
// `flattenAll` la inicializa con la identidad para todos los top-level nodes,
// y `flattenSubGraphInPlace` la extiende cada vez que un SubGraph se expande.
// El resultado se invierte al final para llenar `GeneratedPlan::idForPath`.
using PathTable = std::map<int, std::vector<int>>;

static bool flattenSubGraphInPlace(NodeGraph& g, int sgId, PathTable* pathOf) {
    const NodeInstance* sg = g.findNode(sgId);
    if (!sg || sg->type != NodeType::SubGraph) return false;
    const NodeGraph* child = g.subGraphOf(sgId);
    if (!child) return false;

    // 1. Recolectar conexiones externas del SubGraph en el grafo padre.
    //    inExternal[k] = attrId externo (output port de quien alimenta el
    //                   puerto k del SubGraph).  Sólo una entrada por puerto
    //                   por R5 de la gramática.
    //    outExternal[k] = lista de attrIds externos (input ports de
    //                   consumidores del puerto k de salida del SubGraph).
    std::unordered_map<int, int>              inExternal;
    std::unordered_map<int, std::vector<int>> outExternal;
    std::vector<int> sgEdgeIds;
    for (const Edge& e : g.edges()) {
        if (e.toNodeId == sgId) {
            const int port = e.toAttrId % 10000;
            inExternal[port] = e.fromAttrId;
            sgEdgeIds.push_back(e.id);
        } else if (e.fromNodeId == sgId) {
            const int port = (e.fromAttrId % 10000) - 9000;
            outExternal[port].push_back(e.toAttrId);
            sgEdgeIds.push_back(e.id);
        }
    }
    // Borrar las aristas externas viejas ahora — si lo hiciéramos después
    // de cablear las nuevas, R5 (input port already connected) rechazaría
    // cualquier nueva conexión a un consumidor externo del SubGraph.
    for (int eid : sgEdgeIds) g.removeEdge(eid);

    // 2. Materializar cada nodo no-stub del grafo hijo en el grafo padre,
    //    construyendo un mapeo oldId → newId para reescribir aristas.
    //    Los stubs SubGraphInput/Output NO se materializan — son frontera.
    std::unordered_map<int, int> idMap;
    for (const NodeInstance& cn : child->nodes()) {
        if (cn.type == NodeType::SubGraphInput  ||
            cn.type == NodeType::SubGraphOutput) continue;
        int newId;
        if (cn.type == NodeType::Custom) {
            newId = g.addCustomNode(cn.customType);
        } else if (cn.type == NodeType::SubGraph) {
            // Sub-SubGraph: lo creamos vacío y le copiamos el contenido del
            // hijo.  El bucle de generate() lo volverá a aplanar en la
            // siguiente iteración.
            newId = g.addSubGraphNode();
            if (auto* dst = g.subGraphOf(newId)) {
                if (auto* csub = child->subGraphOf(cn.id)) *dst = *csub;
                g.recomputeSubGraphPorts(newId);
            }
        } else {
            newId = g.addNode(cn.type);
        }
        idMap[cn.id] = newId;
        for (const auto& [k, v] : cn.params)       g.setParam(newId, k, v);
        for (const auto& [k, v] : cn.stringParams) g.setStringParam(newId, k, v);
        if (!cn.assetPath.empty())                 g.setAssetPath(newId, cn.assetPath);
        // Propagar el path: el nuevo nodo en el grafo aplanado se identifica
        // por (path del SubGraph padre) ++ [child's original id].
        if (pathOf) {
            std::vector<int> p = (*pathOf)[sgId];   // path al SubGraph que estamos expandiendo
            p.push_back(cn.id);
            (*pathOf)[newId] = std::move(p);
        }
    }

    auto stubPort = [&](int childNodeId) -> int {
        const NodeInstance* cn = child->findNode(childNodeId);
        if (!cn) return -1;
        auto it = cn->params.find("Port");
        return (it != cn->params.end()) ? static_cast<int>(it->second) : 0;
    };

    // 3. Cablear las aristas del hijo en el padre.  Tres casos:
    //    (a) ambos extremos son nodos materializados — re-cablear con ids nuevos.
    //    (b) origen es un SubGraphInput stub — el origen real es inExternal[Port].
    //    (c) destino es un SubGraphOutput stub — los destinos reales son
    //        cada attrId en outExternal[Port].
    //    (d) extremos son ambos stubs (bypass directo) — conectar
    //        inExternal[from-Port] a cada attrId de outExternal[to-Port].
    for (const Edge& ce : child->edges()) {
        const NodeInstance* nFrom = child->findNode(ce.fromNodeId);
        const NodeInstance* nTo   = child->findNode(ce.toNodeId);
        if (!nFrom || !nTo) continue;
        const bool fromStub = (nFrom->type == NodeType::SubGraphInput);
        const bool toStub   = (nTo->type   == NodeType::SubGraphOutput);

        if (fromStub && toStub) {
            // Bypass: el subgrafo simplemente reenvía la señal del puerto
            // de entrada al puerto de salida sin transformarla.
            int p = stubPort(nFrom->id);
            int q = stubPort(nTo->id);
            auto it = inExternal.find(p);
            if (it == inExternal.end()) continue;
            for (int toAttr : outExternal[q])
                g.tryAddEdge(it->second, toAttr);
            continue;
        }
        if (fromStub) {
            int p = stubPort(nFrom->id);
            auto it = inExternal.find(p);
            if (it == inExternal.end()) continue;     // puerto sin conectar
            auto tIt = idMap.find(ce.toNodeId);
            if (tIt == idMap.end()) continue;
            const int newToAttr = tIt->second * 10000 + (ce.toAttrId % 10000);
            g.tryAddEdge(it->second, newToAttr);
            continue;
        }
        if (toStub) {
            int q = stubPort(nTo->id);
            auto fIt = idMap.find(ce.fromNodeId);
            if (fIt == idMap.end()) continue;
            const int newFromAttr = fIt->second * 10000 + (ce.fromAttrId % 10000);
            for (int toAttr : outExternal[q])
                g.tryAddEdge(newFromAttr, toAttr);
            continue;
        }
        // Caso (a): ambos extremos son nodos materializados.
        auto fIt = idMap.find(ce.fromNodeId);
        auto tIt = idMap.find(ce.toNodeId);
        if (fIt == idMap.end() || tIt == idMap.end()) continue;
        const int newFromAttr = fIt->second * 10000 + (ce.fromAttrId % 10000);
        const int newToAttr   = tIt->second * 10000 + (ce.toAttrId   % 10000);
        g.tryAddEdge(newFromAttr, newToAttr);
    }

    // 4. Eliminar el SubGraph node (también lleva sus aristas externas y su
    //    grafo hijo del side-table).
    if (pathOf) pathOf->erase(sgId);
    g.removeNode(sgId);
    return true;
}

// Repetir flatten hasta que no quede ningún SubGraph en el grafo.
static NodeGraph flattenAll(const NodeGraph& src, PathTable* pathOf = nullptr) {
    NodeGraph g = src;     // deep copy via NodeGraph(const NodeGraph&)
    if (pathOf) {
        pathOf->clear();
        for (const NodeInstance& n : g.nodes())
            (*pathOf)[n.id] = { n.id };
    }
    for (;;) {
        int sgId = 0;
        for (const NodeInstance& n : g.nodes()) {
            if (n.type == NodeType::SubGraph) { sgId = n.id; break; }
        }
        if (sgId == 0) break;
        if (!flattenSubGraphInPlace(g, sgId, pathOf)) {
            // Imposible aplanar (sin grafo hijo, p.ej.): lo dejamos en su
            // sitio; isSupported() lo rechazará con un mensaje legible.
            break;
        }
    }
    return g;
}

GeneratedPlan ScilabCodeGen::generate(const NodeGraph& graphIn) {
    GeneratedPlan plan;

    // 0. Aplanar SubGraphs antes de cualquier procesamiento.  Tras este
    //    paso, el grafo no contiene SubGraph/SubGraphInput/SubGraphOutput
    //    y el resto del codegen opera exactamente igual que antes.  El
    //    pathTable se invierte al final para llenar plan.idForPath.
    PathTable pathOf;
    NodeGraph graphStorage = flattenAll(graphIn, &pathOf);
    const NodeGraph& graph = graphStorage;
    for (const auto& [flatId, path] : pathOf)
        plan.idForPath[path] = flatId;

    // 1. Topological sort (cycles allowed if they pass through pure-state
    //    blocks; see topoSort comment).
    auto order = topoSort(graph);
    if (order.empty() && graph.nodeCount() > 0) {
        plan.error = "Graph has an algebraic loop — a feedback cycle "
                     "without a pure-state block (Integrator, LowPassFilter, "
                     "or DCMotorModel) to break it.";
        return plan;
    }

    // 2. Reject unsupported node types.
    for (int id : order) {
        const NodeInstance* n = graph.findNode(id);
        if (!n) continue;
        if (!isSupported(n->type)) {
            plan.error = std::string("Node type \"") + typeName(n->type) +
                         "\" is not yet supported by the Scilab generator.";
            return plan;
        }
        if (n->type == NodeType::Custom) {
            // Surface a clear error when the descriptor disappeared between
            // node creation and codegen (e.g., registry was cleared).
            const auto* cd =
                scinodes::customNodes().find(n->customType);
            if (!cd) {
                plan.error = "Custom node references unknown type id \""
                           + n->customType + "\".";
                return plan;
            }
            if (cd->category == NodeCategory::Transformer &&
                cd->outputPorts != 1) {
                plan.error = "Custom transformer \"" + n->customType +
                             "\" declares output_ports != 1, but the "
                             "Scilab generator currently emits one "
                             "expression per node.";
                return plan;
            }
        }
    }

    // 3. Plan every node (assigning state slots in topo order).
    std::unordered_map<int, NodePlan> plans;
    int slotCursor = 1;
    for (int id : order) {
        const NodeInstance* n = graph.findNode(id);
        if (!n) continue;
        auto srcs = inputSources(graph, *n);
        NodePlan np = planNode(*n, slotCursor, srcs);
        if (np.stateWidth > 0) slotCursor += np.stateWidth;
        plans.emplace(id, std::move(np));
    }
    int totalState = slotCursor - 1;

    // 4. Sink channel list (STATE column layout). A sink contributes one
    //    scalar per outputExprs entry; PhasePortrait contributes 2.
    for (int id : order) {
        const NodeInstance* n = graph.findNode(id);
        if (!n || categoryOf(*n) != NodeCategory::Sink) continue;
        int chans = std::max(1, (int)plans.at(id).outputExprs.size());
        for (int c = 0; c < chans; ++c)
            plan.sinkChannels.push_back({ id, c });
    }

    // 5. Emit the .sce driver.
    std::ostringstream out;
    out << "// SciNodes driver script — autogenerated, do not edit.\n"
        << "// State vector length: " << totalState << "\n"
        << "// STATE columns (node:channel):";
    for (const auto& sc : plan.sinkChannels) out << ' ' << sc.nodeId << ':' << sc.channel;
    out << "\n\n";

    // Helper para emitir la lista `global p_X_0 p_X_1 ...` dentro de cada
    // función que consume parámetros.  Las funciones que en Scilab no
    // tengan declaradas estas variables como `global` reciben *copias*
    // locales del valor INICIAL — el `param N I V` del driver actualiza
    // la copia local del driver pero `dynamics()` sigue viendo la
    // inicial.  Declararlas global garantiza que ambos miren la misma
    // tabla y el live-tuning funcione.
    auto emitParamGlobals = [&](const std::string& indent) {
        bool any = false;
        for (int id : order) {
            const NodeInstance* n = graph.findNode(id);
            if (!n) continue;
            const auto& def = defOf(*n);
            for (size_t i = 0; i < def.params.size(); ++i) {
                if (!any) { out << indent << "global"; any = true; }
                out << ' ' << paramVar(id, (int)i);
            }
        }
        if (any) out << ";\n";
    };

    // 5a. dynamics(t, x) — params se leen como variables `global`.
    if (totalState > 0) {
        out << "function dxdt = dynamics(t, x)\n";
        emitParamGlobals("    ");
        emitTopoEval(out, order, plans, "    ");
        out << "    dxdt = zeros(" << totalState << ", 1);\n";
        for (int id : order) {
            const NodePlan& p = plans.at(id);
            for (int k = 0; k < p.stateWidth; ++k)
                out << "    dxdt(" << (p.stateSlot + k) << ") = "
                    << p.deriv[k] << ";\n";
        }
        out << "endfunction\n\n";
    }

    // 5b. driver() — REPL with `step` and `param` commands.
    out << "function driver()\n";
    emitParamGlobals("    ");

    // Parameter variables (live-tunable via the `param` command).  Inicialización
    // de cada `p_X_Y` a su valor del .scn; las globals ya fueron declaradas arriba.
    out << "    // Parameter table (live-tunable via \"param N I V\")\n";
    for (int id : order) {
        const NodeInstance* n = graph.findNode(id);
        if (!n) continue;
        const auto& def = defOf(*n);
        for (size_t i = 0; i < def.params.size(); ++i) {
            const auto& pd = def.params[i];
            double dv = paramValue(*n, pd.name.c_str(), pd.defaultValue);
            out << "    " << paramVar(id, (int)i) << " = " << lit(dv) << ";\n";
        }
    }

    if (totalState > 0) {
        out << "    x = [";
        bool first = true;
        for (int id : order) {
            const NodePlan& p = plans.at(id);
            for (int k = 0; k < p.stateWidth; ++k) {
                if (!first) out << "; ";
                out << p.ic[k];
                first = false;
            }
        }
        out << "];\n"
            << "    t_prev = 0;\n";
    }

    // History accumulators for "save <path>" — one time vector plus one
    // value vector per sink channel. Growing column matrices so Scilab's
    // save() lays them out as variable-length numeric arrays.
    out << "    t_hist = [];\n";
    for (const auto& sc : plan.sinkChannels)
        out << "    " << varName(sc.nodeId, sc.channel) << "_hist = [];\n";

    out << "    mprintf(\"READY\\n\");\n"
        << "    while %t\n"
        << "        cmd = mfscanf(1, %io(1), \"%s\");\n"
        << "        if cmd == \"step\" then\n"
        << "            t = mfscanf(1, %io(1), \"%f\");\n";

    if (totalState > 0) {
        out << "            if t > t_prev then\n"
            << "                x = ode(\"rk\", x, t_prev, t, dynamics);\n"
            << "            end\n"
            << "            t_prev = t;\n";
    }

    emitTopoEval(out, order, plans, "            ");

    // NaN/Inf detection. The first node (in topo order) whose output is
    // non-finite wins; nanid==0 means everything is finite. The id is sent
    // back as the first integer field of the STATE line so the bridge can
    // surface it to the UI (red-highlight that node on the canvas).
    out << "            nanid = 0;\n";
    bool firstNanBranch = true;
    for (int id : order) {
        const NodePlan& p = plans.at(id);
        int ports = std::max(1, (int)p.outputExprs.size());
        for (int port = 0; port < ports; ++port) {
            std::string v = varName(id, port);
            const char* kw = firstNanBranch ? "if" : "elseif";
            out << "            " << kw
                << " isnan(" << v << ") | isinf(" << v
                << ") then nanid = " << id << ";\n";
            firstNanBranch = false;
        }
    }
    if (!firstNanBranch) out << "            end\n";

    out << "            mprintf(\"STATE %d";
    for (size_t i = 0; i < plan.sinkChannels.size(); ++i) out << " %.6e";
    out << "\\n\", nanid";
    for (const auto& sc : plan.sinkChannels)
        out << ", " << varName(sc.nodeId, sc.channel);
    out << ");\n";

    // Append the current step to history (used by the "save" command).
    out << "            t_hist($+1) = t;\n";
    for (const auto& sc : plan.sinkChannels)
        out << "            " << varName(sc.nodeId, sc.channel)
            << "_hist($+1) = " << varName(sc.nodeId, sc.channel) << ";\n";

    // Param-update branch.
    out << "        elseif cmd == \"param\" then\n"
        << "            pn = mfscanf(1, %io(1), \"%d\");\n"
        << "            pi = mfscanf(1, %io(1), \"%d\");\n"
        << "            pv = mfscanf(1, %io(1), \"%f\");\n";

    // Generate dispatch.
    bool firstBranch = true;
    for (int id : order) {
        const NodeInstance* n = graph.findNode(id);
        if (!n) continue;
        const auto& def = defOf(*n);
        for (size_t i = 0; i < def.params.size(); ++i) {
            const char* kw = firstBranch ? "if" : "elseif";
            out << "            " << kw
                << " pn == " << id << " & pi == " << i << " then "
                << paramVar(id, (int)i) << " = pv;\n";
            firstBranch = false;
        }
    }
    if (!firstBranch) out << "            end\n";

    // "save <path>" — dump t_hist and every per-sink-channel history
    // vector to <path> using Scilab's native binary save() (HDF5-backed
    // .sod files). Path must not contain spaces (mfscanf reads a single
    // whitespace-delimited token).
    out << "        elseif cmd == \"save\" then\n"
        << "            spath = mfscanf(1, %io(1), \"%s\");\n"
        << "            save(spath, \"t_hist\"";
    for (const auto& sc : plan.sinkChannels)
        out << ", \"" << varName(sc.nodeId, sc.channel) << "_hist\"";
    out << ");\n"
        << "            mprintf(\"SAVED %s\\n\", spath);\n";

    out << "        elseif cmd == \"quit\" then\n"
        << "            mprintf(\"BYE\\n\");\n"
        << "            return;\n"
        << "        end\n"
        << "    end\n"
        << "endfunction\n\n"
        << "driver();\n";

    plan.script = out.str();
    return plan;
}

// =============================================================================
// generateSpec — Versión estructurada de generate() para backends
// in-process. Reusa topoSort / planNode / emitTopoEval y emite directamente
// los campos del BackendPrepareSpec en vez de un script .sce.
// =============================================================================
GeneratedSpec ScilabCodeGen::generateSpec(const NodeGraph& graph) {
    GeneratedSpec gs;

    // 1) Topo sort (con ruptura de ciclos a través de estado puro).
    auto order = topoSort(graph);
    if (order.empty() && graph.nodeCount() > 0) {
        gs.error = "Graph has an algebraic loop — a feedback cycle "
                   "without a pure-state block (Integrator, LowPassFilter, "
                   "or DCMotorModel) to break it.";
        return gs;
    }

    // 2) Rechazar tipos no soportados (mismo criterio que generate()).
    for (int id : order) {
        const NodeInstance* n = graph.findNode(id);
        if (!n) continue;
        if (!isSupported(n->type)) {
            gs.error = std::string("Node type \"") + typeName(n->type) +
                       "\" is not yet supported by the Scilab generator.";
            return gs;
        }
        if (n->type == NodeType::Custom) {
            const auto* cd =
                scinodes::customNodes().find(n->customType);
            if (!cd) {
                gs.error = "Custom node references unknown type id \""
                         + n->customType + "\".";
                return gs;
            }
            if (cd->category == NodeCategory::Transformer &&
                cd->outputPorts != 1) {
                gs.error = "Custom transformer \"" + n->customType +
                           "\" declares output_ports != 1.";
                return gs;
            }
        }
    }

    // 3) Planear cada nodo, asignando rangos del vector de estado.
    std::unordered_map<int, NodePlan> plans;
    int slotCursor = 1;
    for (int id : order) {
        const NodeInstance* n = graph.findNode(id);
        if (!n) continue;
        auto srcs = inputSources(graph, *n);
        NodePlan np = planNode(*n, slotCursor, srcs);
        if (np.stateWidth > 0) slotCursor += np.stateWidth;
        plans.emplace(id, std::move(np));
    }
    int totalState = slotCursor - 1;
    gs.spec.stateSize = totalState;

    // 4) Cuerpo de la función dynamics (idéntico al que generate() emite).
    if (totalState > 0) {
        std::ostringstream fn;
        fn << "function dxdt = dynamics(t, x)\n";
        emitTopoEval(fn, order, plans, "    ");
        fn << "    dxdt = zeros(" << totalState << ", 1);\n";
        for (int id : order) {
            const NodePlan& p = plans.at(id);
            for (int k = 0; k < p.stateWidth; ++k)
                fn << "    dxdt(" << (p.stateSlot + k) << ") = "
                   << p.deriv[k] << ";\n";
        }
        fn << "endfunction";
        gs.spec.dynamicsFunction = fn.str();
    }

    // 5) Vector inicial de estado.
    if (totalState > 0) {
        gs.spec.initialState.reserve(totalState);
        for (int id : order) {
            const NodePlan& p = plans.at(id);
            for (int k = 0; k < p.stateWidth; ++k) {
                // p.ic[k] es una cadena literal como "0.0" o "1.5"; convertir
                // a double. Si no parsea, dejar cero — el backend recargará
                // si fuese necesario.
                try {
                    gs.spec.initialState.push_back(std::stod(p.ic[k]));
                } catch (...) {
                    gs.spec.initialState.push_back(0.0);
                }
            }
        }
    }

    // 6) outputEvalScript — recomputa todas las v<id>_<port> después de
    //    ode().  Sin indentación, una sentencia por línea.
    {
        std::ostringstream eval;
        emitTopoEval(eval, order, plans, "");
        gs.spec.outputEvalScript = eval.str();
    }

    // 7) Sumideros: una entrada por cada canal, leyendo v<id>_<channel>.
    for (int id : order) {
        const NodeInstance* n = graph.findNode(id);
        if (!n || categoryOf(*n) != NodeCategory::Sink) continue;
        int chans = std::max(1, (int)plans.at(id).outputExprs.size());
        for (int c = 0; c < chans; ++c) {
            scinodes::BackendPrepareSpec::SinkChannel sc;
            sc.nodeId     = id;
            sc.channel    = c;
            sc.expression = varName(id, c);
            gs.spec.sinkChannels.push_back(std::move(sc));
        }
    }

    // 8) Parámetros vivos — nombre Scilab + valor inicial.
    for (int id : order) {
        const NodeInstance* n = graph.findNode(id);
        if (!n) continue;
        const auto& def = defOf(*n);
        for (size_t i = 0; i < def.params.size(); ++i) {
            const auto& pd = def.params[i];
            scinodes::BackendPrepareSpec::ParamSlot ps;
            ps.nodeId       = id;
            ps.paramIdx     = static_cast<int>(i);
            ps.scilabName   = paramVar(id, static_cast<int>(i));
            ps.initialValue = paramValue(*n, pd.name.c_str(), pd.defaultValue);
            gs.spec.params.push_back(std::move(ps));
        }
    }

    return gs;
}
