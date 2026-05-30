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
std::vector<int> topoSort(const NodeGraph& g) {
    std::unordered_map<int, int>       indeg;
    std::unordered_map<int, NodeType>  typeOf;
    for (const auto& n : g.nodes()) {
        indeg[n.id]  = 0;
        typeOf[n.id] = n.type;
    }
    for (const auto& e : g.edges()) {
        if (isPureState(typeOf[e.toNodeId])) continue;  // breaks cycle
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
            if (isPureState(typeOf[e.toNodeId])) continue;
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
            return true;
        default:
            return false;
    }
}

int stateWidth(NodeType t) {
    switch (t) {
        case NodeType::Integrator:       return 1;
        case NodeType::LowPassFilter:    return 1;
        case NodeType::PIDController:    return 1;
        case NodeType::Differentiator:   return 1;
        case NodeType::TransferFunction: return 1;
        case NodeType::TransferFunction2:return 2;
        case NodeType::DCMotorModel:     return 2;
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
            std::string r = paramRef(n, "Ratio",      10.0);
            std::string e = paramRef(n, "Efficiency", 0.95);
            p.outputExprs[0] = "(" + r + " * " + e + ") * " + src(0);
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
            char slot[16]; std::snprintf(slot, sizeof(slot), "x(%d)", p.stateSlot);
            std::string Kp = paramRef(n, "Kp", 1.0);
            std::string Ki = paramRef(n, "Ki", 0.0);
            p.outputExprs[0] = Kp + " * " + src(0) + " + " + Ki + " * " + slot;
            p.ic    = { "0.0" };
            p.deriv = { src(0) };
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
        case NodeType::Oscilloscope:
        case NodeType::FFTAnalyzer:
        case NodeType::DataLogger:
        case NodeType::TerminalDisplay:
        case NodeType::View3DSink:
            p.outputExprs[0] = src(0);
            break;
        case NodeType::PhasePortrait:
            // Two channels: input 0 plots on the x-axis, input 1 on y.
            p.outputExprs.resize(2);
            p.outputExprs[0] = src(0);
            p.outputExprs[1] = src(1);
            break;

        // ---- JSON-loaded custom nodes ----------------------------------
        // Stateless transformers / sources use the descriptor's
        // expression with u<N>/p_<name> placeholders substituted.
        // Sinks just record their first input, matching every builtin
        // sink. State-bearing custom nodes are out of scope (no
        // expression schema for dynamics yet).
        case NodeType::Custom: {
            const auto* cd =
                scinodes::CustomNodeRegistry::instance().find(n.customType);
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
        case NodeType::RampSignal:
        // Stateless
        case NodeType::Gain:          case NodeType::Summation:
        case NodeType::Saturation:    case NodeType::GearTransmission:
        case NodeType::InverseKinematics:
        // Stateful
        case NodeType::Integrator:    case NodeType::LowPassFilter:
        case NodeType::PIDController: case NodeType::DCMotorModel:
        case NodeType::Differentiator:case NodeType::TransferFunction:
        case NodeType::TransferFunction2:
        // Sinks
        case NodeType::Oscilloscope:  case NodeType::FFTAnalyzer:
        case NodeType::PhasePortrait: case NodeType::DataLogger:
        case NodeType::TerminalDisplay: case NodeType::View3DSink:
        // JSON-loaded user types
        case NodeType::Custom:
            return true;
        default:
            return false;
    }
}

GeneratedPlan ScilabCodeGen::generate(const NodeGraph& graph) {
    GeneratedPlan plan;

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
                scinodes::CustomNodeRegistry::instance().find(n->customType);
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

    // 5a. dynamics(t, x) — params and x are read-only inside the ODE call.
    if (totalState > 0) {
        out << "function dxdt = dynamics(t, x)\n";
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

    // Parameter variables (live-tunable via the `param` command).
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
