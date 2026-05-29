#include "ScilabCodeGen.hpp"
#include "NodeInstance.hpp"
#include "NodeType.hpp"

#include <algorithm>
#include <cstdio>
#include <queue>
#include <sstream>
#include <unordered_map>

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
    const auto& ps = nodeRegistry().at(n.type).params;
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

std::vector<int> inputSources(const NodeGraph& g, const NodeInstance& dst) {
    int inputs = nodeRegistry().at(dst.type).inputPorts;
    std::vector<int> src(inputs, -1);
    for (const auto& e : g.edges()) {
        if (e.toNodeId != dst.id) continue;
        int port = e.toAttrId % 10000;
        if (port >= 0 && port < inputs) src[port] = e.fromNodeId;
    }
    return src;
}

std::string varName(int nodeId) {
    char b[32]; std::snprintf(b, sizeof(b), "v%d", nodeId); return b;
}

bool isStateful(NodeType t) {
    switch (t) {
        case NodeType::Integrator:
        case NodeType::LowPassFilter:
        case NodeType::PIDController:
        case NodeType::DCMotorModel:
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
// PIDController has feedthrough (output = Kp·input + Ki·x), so it
// cannot break a cycle on its own.
bool isPureState(NodeType t) {
    switch (t) {
        case NodeType::Integrator:
        case NodeType::LowPassFilter:
        case NodeType::DCMotorModel:
            return true;
        default:
            return false;
    }
}

int stateWidth(NodeType t) {
    switch (t) {
        case NodeType::Integrator:    return 1;
        case NodeType::LowPassFilter: return 1;
        case NodeType::PIDController: return 1;
        case NodeType::DCMotorModel:  return 2;
        default:                       return 0;
    }
}

struct NodePlan {
    int         id;
    std::string outputExpr;
    int         stateSlot   = 0;
    int         stateWidth  = 0;
    std::vector<std::string> ic;     // each entry is a Scilab expression
    std::vector<std::string> deriv;
};

NodePlan planNode(const NodeInstance& n, int slotStart,
                  const std::vector<int>& srcs) {
    NodePlan p;
    p.id          = n.id;
    p.stateSlot   = isStateful(n.type) ? slotStart : 0;
    p.stateWidth  = stateWidth(n.type);

    auto src = [&](int port) -> std::string {
        if (port < (int)srcs.size() && srcs[port] >= 0)
            return varName(srcs[port]);
        return "0.0";
    };

    switch (n.type) {
        // ---- Sources ----------------------------------------------------
        case NodeType::VoltageSource:
            p.outputExpr = paramRef(n, "Voltage", 12.0);
            break;
        case NodeType::CurrentSource:
            p.outputExpr = paramRef(n, "Current", 1.0);
            break;
        case NodeType::StepSignal: {
            std::string t0  = paramRef(n, "Step Time", 0.0);
            std::string amp = paramRef(n, "Amplitude", 1.0);
            p.outputExpr = "(t >= " + t0 + ") * " + amp;
            break;
        }
        case NodeType::SineSignal: {
            std::string A  = paramRef(n, "Amplitude", 1.0);
            std::string f  = paramRef(n, "Frequency", 1.0);
            std::string ph = paramRef(n, "Phase",     0.0);
            p.outputExpr = A + " * sin(2*%pi*" + f + "*t + " + ph + ")";
            break;
        }
        case NodeType::RampSignal:
            p.outputExpr = paramRef(n, "Slope", 1.0) + " * t";
            break;

        // ---- Stateless transformers -------------------------------------
        case NodeType::Gain:
            p.outputExpr = paramRef(n, "K", 1.0) + " * " + src(0);
            break;
        case NodeType::Summation:
            p.outputExpr = paramRef(n, "Sign1", 1.0) + " * " + src(0) + " + "
                         + paramRef(n, "Sign2", 1.0) + " * " + src(1);
            break;
        case NodeType::Saturation: {
            std::string x  = src(0);
            std::string lo = paramRef(n, "Min", -1.0);
            std::string hi = paramRef(n, "Max",  1.0);
            p.outputExpr = "max(min(" + x + ", " + hi + "), " + lo + ")";
            break;
        }
        case NodeType::GearTransmission: {
            std::string r = paramRef(n, "Ratio",      10.0);
            std::string e = paramRef(n, "Efficiency", 0.95);
            p.outputExpr = "(" + r + " * " + e + ") * " + src(0);
            break;
        }

        // ---- Stateful transformers (integrated by Scilab ode rk) --------
        case NodeType::Integrator: {
            char slot[16]; std::snprintf(slot, sizeof(slot), "x(%d)", p.stateSlot);
            p.outputExpr = slot;
            p.ic    = { paramRef(n, "Initial Cond.", 0.0) };
            p.deriv = { src(0) };
            break;
        }
        case NodeType::LowPassFilter: {
            char slot[16]; std::snprintf(slot, sizeof(slot), "x(%d)", p.stateSlot);
            p.outputExpr = slot;
            std::string fc = paramRef(n, "Cutoff Freq.", 100.0);
            p.ic    = { "0.0" };
            p.deriv = { "2*%pi*" + fc + " * (" + src(0) + " - " + slot + ")" };
            break;
        }
        case NodeType::PIDController: {
            char slot[16]; std::snprintf(slot, sizeof(slot), "x(%d)", p.stateSlot);
            std::string Kp = paramRef(n, "Kp", 1.0);
            std::string Ki = paramRef(n, "Ki", 0.0);
            p.outputExpr = Kp + " * " + src(0) + " + " + Ki + " * " + slot;
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
            p.outputExpr = w;
            p.ic    = { "0.0", "0.0" };
            p.deriv = {
                "(" + src(0) + " - " + Ra + "*" + i + " - " + Ke + "*" + w + ") / " + La,
                "(" + Kt + "*" + i + " - " + B + "*" + w + ") / " + J
            };
            break;
        }

        // ---- Sinks: just record the input ------------------------------
        case NodeType::Oscilloscope:
        case NodeType::FFTAnalyzer:
        case NodeType::DataLogger:
        case NodeType::TerminalDisplay:
        case NodeType::PhasePortrait:
            p.outputExpr = src(0);
            break;

        default:
            p.outputExpr = "0.0";
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
        out << indent << varName(id) << " = " << it->second.outputExpr << ";\n";
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
        // Stateful
        case NodeType::Integrator:    case NodeType::LowPassFilter:
        case NodeType::PIDController: case NodeType::DCMotorModel:
        // Sinks
        case NodeType::Oscilloscope:  case NodeType::FFTAnalyzer:
        case NodeType::PhasePortrait: case NodeType::DataLogger:
        case NodeType::TerminalDisplay:
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

    // 4. Sink order (STATE column layout).
    for (int id : order) {
        const NodeInstance* n = graph.findNode(id);
        if (n && categoryOf(n->type) == NodeCategory::Sink)
            plan.sinkOrder.push_back(id);
    }

    // 5. Emit the .sce driver.
    std::ostringstream out;
    out << "// SciNodes driver script — autogenerated, do not edit.\n"
        << "// State vector length: " << totalState << "\n"
        << "// Sinks (STATE column order):";
    for (int s : plan.sinkOrder) out << ' ' << s;
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
        const auto& def = nodeRegistry().at(n->type);
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

    out << "            mprintf(\"STATE";
    for (size_t i = 0; i < plan.sinkOrder.size(); ++i) out << " %.6e";
    out << "\\n\"";
    for (int sid : plan.sinkOrder) out << ", " << varName(sid);
    out << ");\n";

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
        const auto& def = nodeRegistry().at(n->type);
        for (size_t i = 0; i < def.params.size(); ++i) {
            const char* kw = firstBranch ? "if" : "elseif";
            out << "            " << kw
                << " pn == " << id << " & pi == " << i << " then "
                << paramVar(id, (int)i) << " = pv;\n";
            firstBranch = false;
        }
    }
    if (!firstBranch) out << "            end\n";

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
