#include "ScilabCodeGen.hpp"
#include "CustomNodeRegistry.hpp"
#include "NodeInstance.hpp"
#include "NodeType.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <utility>

// ===========================================================================
// Helpers (file-local)
// ===========================================================================
namespace {

// Capacidad inicial del buffer histórico que el driver Scilab pre-aloca.
// Cuando el contador `hist_idx` la rebasa, el driver duplica la capacidad
// (amortizado O(1) por step).  4096 = ~68 s a 60 Hz antes de la primera
// duplicación — suficiente para que la mayoría de sesiones no alocen más.
constexpr int kDriverHistCapInitial = 4096;

// Sub-steps RK4 por cada step exterior del bridge.  Con 8 sub-steps a
// h = (1/60)/8 = 1/480 s, RK4 es estable hasta polos de ~1300 rad/s
// (frontera de estabilidad RK4 |h·λ| < 2.78 con margen).  Cubre el filtro
// del PIDController con N = 100 (h·λ = 0.21) y el del Differentiator con
// ω_c = 2π·100 ≈ 628 rad/s (h·λ = 1.31).  Costo: 8·4 = 32 evals de
// `dynamics` por step exterior — constante, no se atora.
// RK4 con substeps fijos.  El producto h·|λ_max| debe estar bajo
// 2.78 para estabilidad.  Con h_outer = 1/60 s, 16 substeps dan
// h_sub = 1/960 s → polos estables hasta ~2670 rad/s.  Cubre el
// caso usuario "Ra=14 Ohm" (pole = Ra/La = 1400 rad/s) que con 8
// substeps disparaba inf.  Trade-off: 64 evals/step (vs 32 antes),
// despreciable comparado al overhead IPC del subprocess Scilab.
constexpr int kDriverRk4Substeps = 16;

// Umbral anti-denormal.  Estados que decaen exponencialmente terminan
// en rango denormal IEEE 754 (< 2.2e-308); la FPU x86 los procesa
// ~100× más lento.  Snap a 0 al final de cada step exterior elimina
// la patología sin afectar precisión útil — el umbral 1e-30 está muy
// por encima del rango denormal pero muy por debajo de cualquier valor
// físicamente significativo.
constexpr double kDriverDenormalThreshold = 1e-30;

// Etapa 6I.E: el codegen consume el valor en SI CANÓNICO, no el doble
// crudo que el usuario tipeó.  Si el field declara `3 mV`, el solver
// recibe 0.003; si declara `100 cm`, recibe 1.0; si declara `Ra=14 Ohm`,
// recibe 14 (Ohm es SI base).  Esto cierra el último gap del refactor
// dimensional: hasta ahora `params[name]` era el valor crudo y los
// prefijos SI eran cosméticos para el solver.
//
// Fallback al map legacy `n.params` cuando el field no existe — cubre
// Custom nodes que no siembran fields todavía y tests viejos.
double paramValue(const NodeInstance& n, const char* key, double fb) {
    auto fit = n.fields.find(key);
    if (fit != n.fields.end())
        return fit->second.toSI();
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

// "Override" para parámetros con pin de entrada conectado: si el caller
// pasa una entrada no vacía en `paramSrcOverride[idx]`, la referencia
// del param resuelve a esa expresión Scilab (la salida del source
// upstream) en lugar de a la constante `p_X_Y`.  Filosofía: el edge
// SIEMPRE pisa al widget — el campo numérico queda "decorativo"
// mientras hay cable conectado.  Disconnect para volver al constante.
//
// Resolve a param to its Scilab variable name (o expresión override).
// Falls back to a literal if the param is unknown to the registry.
std::string paramRefResolved(const NodeInstance& n, const char* key, double fb,
                             const std::vector<std::string>& paramSrcOverride) {
    int idx = paramIndex(n, key);
    if (idx < 0) return lit(fb);
    if (idx < (int)paramSrcOverride.size() && !paramSrcOverride[idx].empty())
        return paramSrcOverride[idx];
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
//
// Anti-windup: la entrada a un puerto declarado en `NodeDef::stateOnlyPorts`
// afecta solamente a la *derivada* del estado, no a la salida instantánea
// — por eso se trata como una arista "rota" igual que las que entran a
// pure-state nodes.  Hoy sólo lo usa el PIDController (port 1 = u_sat
// del back-calculation, Åström & Hägglund 2006); el registro de
// stateOnlyPorts en NodeDef hace que añadir más nodos con este patrón no
// requiera modificar este predicado.
bool isStateOnlyInput(NodeType t, int port) {
    const auto& reg = nodeRegistry();
    auto it = reg.find(t);
    if (it == reg.end()) return false;
    const auto& sop = it->second.stateOnlyPorts;
    return std::find(sop.begin(), sop.end(), port) != sop.end();
}

std::vector<int> topoSort(const NodeGraph& g) {
    std::unordered_map<int, int>       indeg;
    std::unordered_map<int, NodeType>  typeOf;
    // Etapa 6I.U: aliases referencian otros nodos sin edge visible.
    // Recolectamos esos targets virtuales como dependencias para que
    // el codegen procese el target ANTES del alias (sin esto, la
    // variable `v<target>` no estaría definida cuando el alias la usa).
    std::unordered_map<int, std::vector<int>> aliasDeps;
    for (const auto& n : g.nodes()) {
        indeg[n.id]  = 0;
        typeOf[n.id] = n.type;
        if (n.type == NodeType::Alias) {
            auto it = n.params.find("target_node_id");
            if (it != n.params.end()) {
                const int tid = static_cast<int>(it->second);
                if (tid > 0 && tid != n.id) aliasDeps[tid].push_back(n.id);
            }
        }
    }
    auto isBreakingEdge = [&](const Edge& e) -> bool {
        if (isPureState(typeOf[e.toNodeId])) return true;
        return isStateOnlyInput(typeOf[e.toNodeId], attrInputPort(e.toAttrId));
    };
    for (const auto& e : g.edges()) {
        if (isBreakingEdge(e)) continue;
        indeg[e.toNodeId]++;
    }
    // Sumamos las deps virtuales del Alias al indeg.
    for (const auto& [target, aliases] : aliasDeps)
        for (int a : aliases)
            indeg[a]++;

    std::queue<int> q;
    for (const auto& [id, d] : indeg) if (d == 0) q.push(id);

    std::vector<int> order;
    order.reserve(g.nodeCount());
    while (!q.empty()) {
        int u = q.front(); q.pop();
        order.push_back(u);
        // Edges normales.
        for (const auto& e : g.edges()) {
            if (e.fromNodeId != u) continue;
            if (isBreakingEdge(e)) continue;
            if (--indeg[e.toNodeId] == 0) q.push(e.toNodeId);
        }
        // Edges virtuales hacia Aliases que referencian a `u`.
        auto it = aliasDeps.find(u);
        if (it != aliasDeps.end()) {
            for (int a : it->second)
                if (--indeg[a] == 0) q.push(a);
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
        const int port    = attrInputPort(e.toAttrId);
        const int srcPort = attrOutputPort(e.fromAttrId);
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

// Los tres predicados de estado se delegan al registry de NodeDef.
// Antes eran switches de 8-10 cases que había que mantener sincronizados
// cuando se añadía un nodo nuevo built-in con dinámica.  Ahora la fuente
// de verdad es la entrada del registry — y los nodos Custom también
// pueden declarar estado si su descriptor JSON lo configura (futuro).
bool isStateful(NodeType t) {
    const auto& reg = nodeRegistry();
    auto it = reg.find(t);
    return (it != reg.end()) && it->second.stateWidth > 0;
}

bool isPureState(NodeType t) {
    return isPureStateNode(t);   // wrapper en NodeType.cpp ya consulta el registry
}

int stateWidth(NodeType t) {
    const auto& reg = nodeRegistry();
    auto it = reg.find(t);
    return (it != reg.end()) ? it->second.stateWidth : 0;
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
                  const std::vector<SrcRef>& srcs,
                  const std::vector<std::string>& paramSrcOverride) {
    NodePlan p;
    p.id          = n.id;
    p.stateSlot   = isStateful(n.type) ? slotStart : 0;
    p.stateWidth  = stateWidth(n.type);

    auto src = [&](int port) -> std::string {
        if (port < (int)srcs.size() && srcs[port].first >= 0)
            return varName(srcs[port].first, srcs[port].second);
        return "0.0";
    };

    // Lambda local que captura `n` y el override map.  Sombrea la
    // función libre y permite a los 90+ call sites del switch llamar
    // `paramRef("key", fb)` sin tener que propagar el override por
    // todas las firmas.
    auto paramRef = [&](const char* key, double fb) -> std::string {
        return paramRefResolved(n, key, fb, paramSrcOverride);
    };

    // Default: 1 output. Multi-output cases (InverseKinematics) resize first.
    p.outputExprs.assign(1, std::string{});

    switch (n.type) {
        // ---- Sources ----------------------------------------------------
        case NodeType::Alias: {
            // Etapa 6I.U: emite identidad — alias.out = target.out_<port>.
            // Como el target se procesa antes en topo-order (el grafo
            // garantiza que cualquier nodo referenciado vive en el
            // grafo y se planifica), su varName ya está reservado.
            const int targetId   = static_cast<int>(paramValue(n, "target_node_id", 0.0));
            const int targetPort = static_cast<int>(paramValue(n, "target_port",    0.0));
            if (targetId <= 0) {
                // Sin target asignado todavía — emitimos 0.0.  El
                // analyzer reporta el problema dimensional/grammar.
                p.outputExprs[0] = "0.0";
            } else {
                p.outputExprs[0] = varName(targetId, targetPort);
            }
            break;
        }
        case NodeType::VoltageSource:
            p.outputExprs[0] = paramRef("Voltage", 12.0);
            break;
        case NodeType::CurrentSource:
            p.outputExprs[0] = paramRef("Current", 1.0);
            break;
        case NodeType::StepSignal: {
            std::string t0  = paramRef("Step Time", 0.0);
            std::string amp = paramRef("Amplitude", 1.0);
            p.outputExprs[0] = "(t >= " + t0 + ") * " + amp;
            break;
        }
        case NodeType::SineSignal: {
            std::string A  = paramRef("Amplitude", 1.0);
            std::string f  = paramRef("Frequency", 1.0);
            std::string ph = paramRef("Phase",     0.0);
            p.outputExprs[0] = A + " * sin(2*%pi*" + f + "*t + " + ph + ")";
            break;
        }
        case NodeType::RampSignal:
            p.outputExprs[0] = paramRef("Slope", 1.0) + " * t";
            break;
        case NodeType::DesignTemplate: {
            // Four constant outputs — one per design-requirement param.
            // Order matches NodeDef::params so external consumers can
            // wire by port index.
            p.outputExprs.resize(4);
            p.outputExprs[0] = paramRef("Target Torque",  10.0);
            p.outputExprs[1] = paramRef("Target Speed",  150.0);
            p.outputExprs[2] = paramRef("Bus Voltage",   400.0);
            p.outputExprs[3] = paramRef("Cooling Class",   1.0);
            break;
        }

        // ---- Stateless transformers -------------------------------------
        case NodeType::Gain:
            p.outputExprs[0] = paramRef("K", 1.0) + " * " + src(0);
            break;
        case NodeType::DegToRad:
            // out = in · π/180.  Constante literal en lugar de %pi/180
            // para que el script funcione idéntico bajo subprocess y
            // call_scilab (algunos backends no resuelven %pi de la
            // misma forma).  El valor está al límite de double — 17
            // dígitos significativos como recomienda IEEE 754.
            p.outputExprs[0] = "(0.017453292519943295) * " + src(0);
            break;
        case NodeType::RadToDeg:
            // out = in · 180/π.  Mismo razonamiento que DegToRad.
            p.outputExprs[0] = "(57.29577951308232) * " + src(0);
            break;
        case NodeType::Summation:
            p.outputExprs[0] = paramRef("Sign1", 1.0) + " * " + src(0) + " + "
                         + paramRef("Sign2", 1.0) + " * " + src(1);
            break;
        case NodeType::Saturation: {
            std::string x  = src(0);
            std::string lo = paramRef("Min", -1.0);
            std::string hi = paramRef("Max",  1.0);
            p.outputExprs[0] = "max(min(" + x + ", " + hi + "), " + lo + ")";
            break;
        }
        case NodeType::GearTransmission: {
            // Reductor N:1 con pérdidas:  ω_load = (Eff / Ratio) · ω_motor.
            // Convención estándar de robótica (Spong Cap. 6, Jazar §1.2.6):
            // una reducción 50:1 hace que la carga gire 50× más lento que
            // el motor, modulado por la eficiencia (≤ 1).  Versión previa
            // multiplicaba (Ratio · Eff), que es físicamente incorrecto.
            std::string r = paramRef("Ratio",      10.0);
            std::string e = paramRef("Efficiency", 0.95);
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
            std::string L1 = paramRef("Link 1 L", 0.3);
            std::string L2 = paramRef("Link 2 L", 0.2);
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
            std::string R   = paramRef("Stator Resistance", 0.5);
            std::string Ki  = paramRef("Iron Loss Coeff.",  1e-4);
            std::string Km  = paramRef("Mech Loss Coeff.",  1e-3);

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
            std::string R  = paramRef("Stator Resistance", 0.5);
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
            std::string Kh = paramRef("Hysteresis Coeff.", 0.02);
            std::string Ke = paramRef("Eddy Coeff.",       1e-5);
            std::string pp = paramRef("Pole Pairs",        4.0);
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
            std::string Kv = paramRef("Viscous Coeff.", 1e-3);
            std::string Kd = paramRef("Drag Coeff.",    1e-5);
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
            std::string C  = paramRef("Thermal Capacitance", 500.0);
            std::string R  = paramRef("Thermal Resistance",    0.5);
            std::string Ta = paramRef("Ambient Temperature", 298.0);
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
            std::string C  = paramRef("Thermal Capacitance", 500.0);
            std::string T0 = paramRef("Initial Temperature", 298.0);
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
            std::string R  = paramRef("Thermal Resistance", 1.0);
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
            p.outputExprs[0] = paramRef("Fan Flow",              5.0);
            p.outputExprs[1] = paramRef("Water Flow",            0.0);
            p.outputExprs[2] = paramRef("Ambient Temperature", 298.0);
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
            std::string h = paramRef("Half Tolerance", 0.05);
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
            std::string E = paramRef("Young's Modulus", 200.0e9);
            std::string rho = paramRef("Density",       7850.0);
            std::string t   = paramRef("Thickness",       0.02);
            std::string m   = paramRef("Mode Order",      2.0);
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
            std::string h0    = paramRef("Base Coeff. h_0", 1.0);
            std::string hk    = paramRef("Slope per Flow",  0.5);
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
            std::string B  = paramRef("Peak Flux Density",   0.85);
            std::string pp = paramRef("Pole Pairs",          4.0);
            std::string a3 = paramRef("3rd Harmonic Ratio",  0.10);
            std::string as = paramRef("Slot Harmonic Ratio", 0.05);
            std::string Ns = paramRef("Slot Count",         24.0);
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
            std::string N  = paramRef("Turns per Phase",       100.0);
            std::string kw = paramRef("Winding Factor",          0.95);
            std::string pp = paramRef("Pole Pairs",              4.0);
            std::string Bg = paramRef("Airgap Flux Density",     0.85);
            std::string g  = paramRef("Mechanical Airgap",       0.001);
            std::string hm = paramRef("Magnet Thickness",        0.003);
            std::string Ns = paramRef("Slot Count",             24.0);
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
            std::string B  = paramRef("Magnetic Loading B",      0.85);
            std::string A  = paramRef("Electric Loading A", 40000.0);
            std::string al = paramRef("Aspect Ratio L/D",        1.2);
            std::string ks = paramRef("Saliency Factor",         1.2);

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
            std::string B  = paramRef("Magnetic Loading B",      0.90);
            std::string A  = paramRef("Electric Loading A", 35000.0);
            std::string al = paramRef("Aspect Ratio L/D",        1.0);
            std::string kt = paramRef("Trapezoidal Factor",      1.15);

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
            std::string B  = paramRef("Magnetic Loading B",      0.85);
            std::string A  = paramRef("Electric Loading A", 40000.0);
            std::string al = paramRef("Aspect Ratio L/D",        1.2);

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
            p.ic    = { paramRef("Initial Cond.", 0.0) };
            p.deriv = { src(0) };
            break;
        }
        case NodeType::LowPassFilter: {
            char slot[16]; std::snprintf(slot, sizeof(slot), "x(%d)", p.stateSlot);
            p.outputExprs[0] = slot;
            std::string fc = paramRef("Cutoff Freq.", 100.0);
            p.ic    = { "0.0" };
            p.deriv = { "2*%pi*" + fc + " * (" + src(0) + " - " + slot + ")" };
            break;
        }
        case NodeType::Differentiator: {
            // Filtered derivative  H(s) = s / (1 + s/wc).
            // State x = first-order low-pass of input;  y = wc·(u − x).
            // dx/dt = wc·(u − x). At steady state for slow inputs, y ≈ du/dt.
            char slot[16]; std::snprintf(slot, sizeof(slot), "x(%d)", p.stateSlot);
            std::string fc = paramRef("Cutoff Freq.", 100.0);
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
            std::string b  = paramRef("num[0]", 1.0);
            std::string a0 = paramRef("den[0]", 1.0);
            std::string a1 = paramRef("den[1]", 1.0);
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
            std::string b0 = paramRef("num[0]", 1.0);
            std::string b1 = paramRef("num[1]", 0.0);
            std::string a0 = paramRef("den[0]", 1.0);
            std::string a1 = paramRef("den[1]", 0.0);
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
            std::string Kp = paramRef("Kp", 1.0);
            std::string Ki = paramRef("Ki", 0.0);
            std::string Kd = paramRef("Kd", 0.0);
            std::string N  = paramRef("N (filter)", 100.0);
            std::string Kt = paramRef("Kt (anti-windup)", 0.0);
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
            std::string Ra = paramRef("Ra", 1.0);
            std::string La = paramRef("La", 0.01);
            std::string Ke = paramRef("Ke", 0.1);
            std::string Kt = paramRef("Kt", 0.1);
            std::string J  = paramRef("J",  0.01);
            std::string B  = paramRef("B",  0.001);
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

        // Sub-lenguaje Vec3 (etapa 4 del upgrade gramatical) — el
        // codegen Scilab no soporta vec3 nativo todavía (los buffers
        // del bridge son escalares).  Emitimos "0.0" por cada output
        // port para que ningún Sink downstream lea una variable no
        // definida.  Documentación: SeparateXYZ → Oscilloscope
        // graficará cero por ahora; la evaluación real del vec3
        // ocurre render-side via el SceneCollector mini-intérprete.
        case NodeType::Vec3Constant:
        case NodeType::CombineXYZ:
        case NodeType::SeparateXYZ:
        case NodeType::VectorAdd:
        case NodeType::VectorSub:
        case NodeType::VectorScale:
        case NodeType::VectorDot:
        case NodeType::VectorCross:
        case NodeType::VectorLength:
        case NodeType::VectorNormalize: {
            const auto& def = defOf(n);
            p.outputExprs.assign(def.outputPorts, "0.0");
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
        case NodeType::DegToRad:      case NodeType::RadToDeg:
        case NodeType::Alias:
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
        // Sub-lenguaje Vec3 (etapas 4–5 del upgrade gramatical) —
        // render-only, pero emitimos zeros placeholder para que un Sink
        // downstream no rompa con variable no definida.
        case NodeType::Vec3Constant:
        case NodeType::CombineXYZ:
        case NodeType::SeparateXYZ:
        case NodeType::VectorAdd:
        case NodeType::VectorSub:
        case NodeType::VectorScale:
        case NodeType::VectorDot:
        case NodeType::VectorCross:
        case NodeType::VectorLength:
        case NodeType::VectorNormalize:
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
    if (!sg || !isSubGraphContainer(sg->type)) return false;
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
            inExternal[attrInputPort(e.toAttrId)] = e.fromAttrId;
            sgEdgeIds.push_back(e.id);
        } else if (e.fromNodeId == sgId) {
            outExternal[attrOutputPort(e.fromAttrId)].push_back(e.toAttrId);
            sgEdgeIds.push_back(e.id);
        }
    }
    // Borrar las aristas externas viejas ahora — si lo hiciéramos después
    // de cablear las nuevas, R5 (input port already connected) rechazaría
    // cualquier nueva conexión a un consumidor externo del SubGraph.
    for (int eid : sgEdgeIds) g.removeEdge(eid);

    // 2. Materializar cada nodo no-stub del grafo hijo en el grafo padre,
    //    PRESERVANDO el ID original si no colisiona con otro nodo del
    //    padre.  Sin preservación, el flatten renumera los slots de
    //    estado en cada generate(), y un hot-reload tras encapsular
    //    pierde toda la continuidad: el seed (capturado con el plan
    //    viejo) está indexado por (oldId, slot) pero el plan nuevo
    //    usa (newId, slot) — el lookup falla y los estados caen a IC.
    //    Visualmente: la respuesta se reinicia desde 0 en el t actual.
    std::unordered_map<int, int> idMap;
    for (const NodeInstance& cn : child->nodes()) {
        if (isSubGraphStub(cn.type)) continue;
        const bool collides = (g.findNode(cn.id) != nullptr);
        int newId;
        if (cn.type == NodeType::Custom) {
            newId = collides
                ? g.addCustomNode(cn.customType)
                : g.addCustomNodeWithId(cn.customType, cn.id);
        } else if (isSubGraphContainer(cn.type)) {
            // Sub-SubGraph: addSubGraphNode siempre allocá su shared_ptr
            // hijo + stubs default, lo cual reasigna IDs.  Para preservar
            // los IDs del sub-sub-grafo lo creamos crudo con addNodeWithId
            // y luego instalamos el child copiado (que ya tiene su propia
            // tabla de IDs preservada por la rama de encapsulate).  El
            // bucle de generate() lo volverá a aplanar en la siguiente
            // iteración.
            if (collides) {
                newId = g.addSubGraphNode();
                if (auto* dst = g.subGraphOf(newId)) {
                    if (auto* csub = child->subGraphOf(cn.id)) *dst = *csub;
                    g.recomputeSubGraphPorts(newId);
                }
            } else {
                newId = g.addNodeWithId(NodeType::SubGraph, cn.id);
                if (auto* csub = child->subGraphOf(cn.id)) {
                    g.installSubGraph(newId, NodeGraph(*csub));
                    g.recomputeSubGraphPorts(newId);
                }
            }
        } else {
            newId = collides
                ? g.addNode(cn.type)
                : g.addNodeWithId(cn.type, cn.id);
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
            g.tryAddEdge(it->second, attrRemap(ce.toAttrId, tIt->second));
            continue;
        }
        if (toStub) {
            int q = stubPort(nTo->id);
            auto fIt = idMap.find(ce.fromNodeId);
            if (fIt == idMap.end()) continue;
            const int newFromAttr = attrRemap(ce.fromAttrId, fIt->second);
            for (int toAttr : outExternal[q])
                g.tryAddEdge(newFromAttr, toAttr);
            continue;
        }
        // Caso (a): ambos extremos son nodos materializados.
        auto fIt = idMap.find(ce.fromNodeId);
        auto tIt = idMap.find(ce.toNodeId);
        if (fIt == idMap.end() || tIt == idMap.end()) continue;
        g.tryAddEdge(attrRemap(ce.fromAttrId, fIt->second),
                     attrRemap(ce.toAttrId,   tIt->second));
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
            if (isSubGraphContainer(n.type)) { sgId = n.id; break; }
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

GeneratedPlan ScilabCodeGen::generate(const NodeGraph& graphIn,
                                      const CodegenSeedState* seed) {
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

    // 2a. Filtrar el sub-lenguaje Geometry del topo order.  Estos nodos
    //     son puramente visuales (los rendera View3DPanel vía
    //     SceneCollector) — no aportan al pipeline del solver, no
    //     reservan slots de estado ni emiten expresiones Scilab.
    //     Quitarlos de `order` antes de los pasos 2b/3/4 simplifica todo
    //     el codegen downstream: ningún loop tiene que volver a
    //     filtrarlos.
    order.erase(std::remove_if(order.begin(), order.end(),
        [&](int id) {
            const NodeInstance* n = graph.findNode(id);
            return n && isSceneGraphNode(n->type);
        }), order.end());

    // 2b. Reject unsupported node types.
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
    // Por-nodo recolectamos el "param-source override": para cada
    // parámetro con un edge entrante en su param-pin (attrIsParam),
    // qué variable Scilab emite el source upstream.  El plan del nodo
    // usará esa variable en vez de `p_X_Y` cuando se referencie ese
    // parámetro — el edge pisa al widget.
    auto buildParamOverride = [&](const NodeInstance& n) -> std::vector<std::string> {
        const auto& def = defOf(n);
        std::vector<std::string> ov(def.params.size());
        for (const auto& e : graph.edges()) {
            if (e.toNodeId != n.id) continue;
            if (!attrIsParam(e.toAttrId)) continue;
            const int idx = attrParamIdx(e.toAttrId);
            if (idx < 0 || idx >= (int)ov.size()) continue;
            ov[idx] = varName(e.fromNodeId, attrOutputPort(e.fromAttrId));
        }
        return ov;
    };

    for (int id : order) {
        const NodeInstance* n = graph.findNode(id);
        if (!n) continue;
        auto srcs = inputSources(graph, *n);
        auto pov  = buildParamOverride(*n);
        NodePlan np = planNode(*n, slotCursor, srcs, pov);
        if (np.stateWidth > 0) slotCursor += np.stateWidth;
        plans.emplace(id, std::move(np));
    }
    int totalState = slotCursor - 1;

    // Layout del vector de estado: orden absoluto de los slots con su
    // (nodeId, slotIdx) — para que el bridge pueda mapear (a) el dump
    // del Scilab al pausar y (b) el seed al regenerar.
    for (int id : order) {
        const NodePlan& p = plans.at(id);
        for (int k = 0; k < p.stateWidth; ++k)
            plan.stateLayout.push_back({ id, k });
    }

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
    // Construimos dxdt como UN literal vectorial (una sola alocación)
    // en lugar de `zeros(n,1)` + N asignaciones indexadas (n+1 ops).
    // Cada step ejecuta dynamics 32 veces (RK4·8 subs) → menos
    // alocaciones = menos presión sobre el GC del JVM de Scilab.
    if (totalState > 0) {
        out << "function dxdt = dynamics(t, x)\n";
        emitParamGlobals("    ");
        emitTopoEval(out, order, plans, "    ");
        // stateLayout ya es ordenado por (id, slot) en topo order.
        out << "    dxdt = [";
        bool firstSlot = true;
        for (int id : order) {
            const NodePlan& p = plans.at(id);
            for (int k = 0; k < p.stateWidth; ++k) {
                if (!firstSlot) out << "; ";
                out << p.deriv[k];
                firstSlot = false;
            }
        }
        out << "];\n";
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
        // ICs por defecto (expresiones Scilab generadas por planNode),
        // OVERRIDE por seed si está presente — así el hot-reload tras
        // una edición durante Paused empieza con los estados acumulados.
        // Nodos nuevos (no en el seed) caen al IC del nodo (típicamente
        // una expresión que evalúa a 0 o a un parámetro).
        out << "    x = [";
        bool first = true;
        for (int id : order) {
            const NodePlan& p = plans.at(id);
            for (int k = 0; k < p.stateWidth; ++k) {
                if (!first) out << "; ";
                bool seeded = false;
                if (seed) {
                    auto it = seed->values.find({ id, k });
                    if (it != seed->values.end()) {
                        // Literal numérico con precisión completa.
                        std::ostringstream lit;
                        lit << std::setprecision(15) << it->second;
                        out << lit.str();
                        seeded = true;
                    }
                }
                if (!seeded) out << p.ic[k];
                first = false;
            }
        }
        out << "];\n"
            << "    t_prev = " << (seed ? std::setprecision(15) : std::setprecision(6))
            << (seed ? seed->t : 0.0) << ";\n";
    }

    // History accumulators for "save <path>".  Pre-asignamos una
    // capacidad inicial (kDriverHistCapInitial) y duplicamos cuando se
    // desborda — así cada step es O(1) amortizado, en vez del $+1
    // ingenuo que era O(N) (cada append realloca el vector entero →
    // simulación cada vez más lenta hasta detenerse).
    out << "    hist_cap = " << kDriverHistCapInitial << ";\n"
        << "    hist_idx = 0;\n"
        << "    t_hist = zeros(1, hist_cap);\n";
    for (const auto& sc : plan.sinkChannels)
        out << "    " << varName(sc.nodeId, sc.channel)
            << "_hist = zeros(1, hist_cap);\n";

    out << "    mprintf(\"READY\\n\");\n"
        << "    while %t\n"
        << "        cmd = mfscanf(1, %io(1), \"%s\");\n"
        << "        if cmd == \"step\" then\n"
        << "            t = mfscanf(1, %io(1), \"%f\");\n";

    if (totalState > 0) {
        // RK4 de paso fijo con SUBSTEPS internos.  Antes usábamos
        // ode("rk", ...) — el integrador adaptativo de Scilab — pero
        // cerca del steady-state los estados decaen a ~1e-12 y el
        // control de error del adaptativo se atrapa intentando
        // mantener precisión en valores microscópicos → 2 ms/step
        // explota a 200 ms/step alrededor de t=37 (caso Ogata 8-1).
        //
        // Con 16 sub-steps a h_sub = (1/60)/16 = 1/960 s, RK4 es
        // estable hasta polos de ~2670 rad/s (frontera RK4 |hλ|<2.78).
        // Cubre Ra/La hasta 26.7 Ω con La=0.01 H (motores reales).
        // Subimos de 8 → 16 después que un usuario reportó "inf" al
        // tipear Ra=14 (pole=1400 rad/s, fuera del límite previo).
        // Coste: 64 evaluaciones de `dynamics` por step exterior —
        // constante, despreciable comparado al overhead IPC.
        //
        // TODO etapa futura: hacer kDriverRk4Substeps adaptativo a
        // los params declarados.  En codegen-time calcular el
        // |λ_max| del grafo (Ra/La, B/J, ω_c del filtro, etc.) y
        // emitir el conteo justo de substeps necesario.
        out << "            if t > t_prev then\n"
            << "                nsub = " << kDriverRk4Substeps << ";\n"
            << "                h = (t - t_prev) / nsub;\n"
            << "                ts = t_prev;\n"
            << "                for sub = 1:nsub\n"
            << "                    k1 = dynamics(ts,       x);\n"
            << "                    k2 = dynamics(ts + h/2, x + (h/2)*k1);\n"
            << "                    k3 = dynamics(ts + h/2, x + (h/2)*k2);\n"
            << "                    k4 = dynamics(ts + h,   x +  h   *k3);\n"
            << "                    x  = x + (h/6) * (k1 + 2*k2 + 2*k3 + k4);\n"
            << "                    ts = ts + h;\n"
            << "                end\n"
            // Anti-denormal: ver kDriverDenormalThreshold.  Snap a 0
            // todo lo que esté por debajo del umbral evita la patología
            // de FPU sin afectar precisión numérica útil.
            << "                x(abs(x) < " << kDriverDenormalThreshold
            << ") = 0;\n"
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

    // Append the current step to history.  Si pasamos la capacidad,
    // duplicamos (amortizado O(1)).
    out << "            hist_idx = hist_idx + 1;\n"
        << "            if hist_idx > hist_cap then\n"
        << "                t_hist = [t_hist, zeros(1, hist_cap)];\n";
    for (const auto& sc : plan.sinkChannels)
        out << "                " << varName(sc.nodeId, sc.channel)
            << "_hist = [" << varName(sc.nodeId, sc.channel)
            << "_hist, zeros(1, hist_cap)];\n";
    out << "                hist_cap = hist_cap * 2;\n"
        << "            end\n"
        << "            t_hist(hist_idx) = t;\n";
    for (const auto& sc : plan.sinkChannels)
        out << "            " << varName(sc.nodeId, sc.channel)
            << "_hist(hist_idx) = " << varName(sc.nodeId, sc.channel) << ";\n";

    // Param-update branch.  Antes generábamos una cascada
    // `if pn==X & pi==Y then ... elseif ... end` con un branch por
    // (nodo, paramIdx).  Con muchos nodos (p.ej. 3 SubGraphs duplicados
    // tras un encapsular+copiar+pegar) la cascada superaba los ~45
    // elseif y el parser de Scilab fallaba con "memory exhausted" —
    // Scilab nunca emitía READY y SciNodes timeouteaba en el handshake.
    //
    // Sustitución: `execstr` arma el nombre de la variable en runtime
    // y asigna.  Una línea, sin límite de profundidad, y solo se
    // ejecuta en live-tune del usuario (no en el loop del solver) —
    // el overhead de execstr es irrelevante en frecuencia humana.
    out << "        elseif cmd == \"param\" then\n"
        << "            pn = mfscanf(1, %io(1), \"%d\");\n"
        << "            pi = mfscanf(1, %io(1), \"%d\");\n"
        << "            pv = mfscanf(1, %io(1), \"%f\");\n"
        << "            execstr(\"p_\" + string(pn) + \"_\" + string(pi)"
        << " + \" = \" + string(pv));\n";

    // "save <path>" — dump t_hist and every per-sink-channel history
    // vector to <path> using Scilab's native binary save() (HDF5-backed
    // .sod files). Path must not contain spaces (mfscanf reads a single
    // whitespace-delimited token).
    // "dump_state" — escribe el vector de estado actual al stdout en
    // orden de slot (mismo orden que `stateLayout` del GeneratedPlan,
    // que el bridge guarda para asociar cada valor a su (nodeId, slot)).
    // Formato: `STATE_BEGIN <t> <n> <v_1> <v_2> ... <v_n> STATE_END\n`
    // donde n = totalState.  El bridge lee n primero para saber cuántos
    // doubles esperar.
    out << "        elseif cmd == \"dump_state\" then\n"
        << "            mprintf(\"STATE_BEGIN %.15e " << totalState
        << "\", t_prev);\n";
    if (totalState > 0) {
        out << "            for k = 1:" << totalState << "\n"
            << "                mprintf(\" %.15e\", x(k));\n"
            << "            end\n";
    }
    out << "            mprintf(\" STATE_END\\n\");\n";

    out << "        elseif cmd == \"save\" then\n"
        << "            spath = mfscanf(1, %io(1), \"%s\");\n"
        // Reasignamos cada hist a su rebanada usada antes del save
        // (después restauramos el preallocate para no romper los
        // siguientes appends).  Sin esto guardábamos miles de ceros
        // tras el último sample real.
        << "            t_full = t_hist;\n"
        << "            t_hist = t_hist(1:hist_idx);\n";
    for (const auto& sc : plan.sinkChannels) {
        const std::string vn = varName(sc.nodeId, sc.channel) + "_hist";
        out << "            " << vn << "_full = " << vn << ";\n"
            << "            " << vn << " = " << vn << "(1:hist_idx);\n";
    }
    out << "            save(spath, \"t_hist\"";
    for (const auto& sc : plan.sinkChannels)
        out << ", \"" << varName(sc.nodeId, sc.channel) << "_hist\"";
    out << ");\n"
        << "            t_hist = t_full;\n";
    for (const auto& sc : plan.sinkChannels) {
        const std::string vn = varName(sc.nodeId, sc.channel) + "_hist";
        out << "            " << vn << " = " << vn << "_full;\n";
    }
    out << "            mprintf(\"SAVED %s\\n\", spath);\n";

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

    // 2a. Filtrar el sub-lenguaje Geometry del topo order — espejo de
    //     generate(): los nodos visuales no participan del codegen.
    order.erase(std::remove_if(order.begin(), order.end(),
        [&](int id) {
            const NodeInstance* n = graph.findNode(id);
            return n && isSceneGraphNode(n->type);
        }), order.end());

    // 2b) Rechazar tipos no soportados (mismo criterio que generate()).
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
    // Por-nodo recolectamos el "param-source override": para cada
    // parámetro con un edge entrante en su param-pin (attrIsParam),
    // qué variable Scilab emite el source upstream.  El plan del nodo
    // usará esa variable en vez de `p_X_Y` cuando se referencie ese
    // parámetro — el edge pisa al widget.
    auto buildParamOverride = [&](const NodeInstance& n) -> std::vector<std::string> {
        const auto& def = defOf(n);
        std::vector<std::string> ov(def.params.size());
        for (const auto& e : graph.edges()) {
            if (e.toNodeId != n.id) continue;
            if (!attrIsParam(e.toAttrId)) continue;
            const int idx = attrParamIdx(e.toAttrId);
            if (idx < 0 || idx >= (int)ov.size()) continue;
            ov[idx] = varName(e.fromNodeId, attrOutputPort(e.fromAttrId));
        }
        return ov;
    };

    for (int id : order) {
        const NodeInstance* n = graph.findNode(id);
        if (!n) continue;
        auto srcs = inputSources(graph, *n);
        auto pov  = buildParamOverride(*n);
        NodePlan np = planNode(*n, slotCursor, srcs, pov);
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

    // 9) stepFunction: empaqueta todo el ciclo de un tick en una función
    //    Scilab definida una sola vez en prepare().  Cada step() del
    //    backend in-process la invoca con una sola línea barata de parsear
    //    en lugar de re-mandar el outputEvalScript completo cada tick — la
    //    diferencia es enorme para grafos con muchos sinks/nodos.
    //
    //    Convenciones de scope:
    //      - x_in, t_prev, t_new: pass-by-value (no globals para estado).
    //      - p_X_Y... : globales declaradas dentro de la función (única
    //        forma escalable de compartir configuración entre dynamics y
    //        el outputEval sin pasar un vector explícito; coincide con
    //        cómo dynamics ya las usa).
    //      - x_out: devuelto.
    //      - y: vector columna con los valores de los sumideros, devuelto.
    {
        std::ostringstream fn;
        fn << "function [x_out, y] = scn_step(t_new, t_prev, x_in)\n";

        // Declarar como globals todos los p_X_Y referenciados por
        // outputEvalScript (configuración compartida con dynamics; cero
        // globals de estado).
        bool anyParam = false;
        for (int id : order) {
            const NodeInstance* n = graph.findNode(id);
            if (!n) continue;
            const auto& def = defOf(*n);
            for (size_t i = 0; i < def.params.size(); ++i) {
                if (!anyParam) { fn << "    global"; anyParam = true; }
                fn << ' ' << paramVar(id, (int)i);
            }
        }
        if (anyParam) fn << ";\n";

        // Integrar (si hay estados); el solver `rk` adapta su propio paso.
        if (totalState > 0) {
            fn << "    x_out = ode(\"rk\", x_in, t_prev, t_new, dynamics);\n"
               << "    x = x_out;\n";
        } else {
            fn << "    x_out = [];\n"
               << "    x = [];\n";
        }
        fn << "    t = t_new;\n";

        // Cuerpo del outputEvalScript inline (referencia x, t, p_X_Y).
        emitTopoEval(fn, order, plans, "    ");

        // Empaquetar sinks en un vector columna y.
        if (gs.spec.sinkChannels.empty()) {
            fn << "    y = [];\n";
        } else {
            fn << "    y = [";
            for (size_t i = 0; i < gs.spec.sinkChannels.size(); ++i) {
                if (i) fn << "; ";
                fn << gs.spec.sinkChannels[i].expression;
            }
            fn << "];\n";
        }
        fn << "endfunction";
        gs.spec.stepFunction = fn.str();
    }

    return gs;
}
