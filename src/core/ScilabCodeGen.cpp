#include "ScilabCodeGen.hpp"
#include "CustomNodeRegistry.hpp"
#include "NodeInstance.hpp"
#include "NodeKind.hpp"
#include "NodeType.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

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

// Etapa 6J.6 — validación pre-codegen específica de Custom.  Centraliza
// lo que vivía duplicado en `generate` y `generateSimulation`: ambas
// pasadas ejecutaban el mismo `if (n->type == NodeType::Custom) {…}` para
// chequear que el descriptor JSON existe y respeta la restricción de 1
// salida.  Cualquier nueva regla pre-codegen para Custom (p. ej. require
// expression presente para Transformer) entra acá una sola vez.
//
// Devuelve `nullopt` cuando el nodo es válido (incluye los no-Custom).
// Devuelve el mensaje de error para asignar a `plan.error` / `gs.error`
// si la validación falla.
std::optional<std::string>
validateCustomDescriptor(const NodeInstance& n) {
    if (n.type != NodeType::Custom) return std::nullopt;
    const auto* cd = scinodes::customNodes().find(n.customType);
    if (!cd) {
        return std::string("Custom node references unknown type id \"")
             + n.customType + "\".";
    }
    if (cd->category == NodeCategory::Transformer && cd->outputPorts != 1) {
        return std::string("Custom transformer \"") + n.customType +
               "\" declares output_ports != 1, but the Scilab generator "
               "currently emits one expression per node.";
    }
    return std::nullopt;
}

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
        // Etapa 6J.5: predicado alias-like centralizado en NodeKind.
        if (auto target = scinodes::aliasTargetOf(n)) {
            const int tid = target->first;
            if (tid != n.id) aliasDeps[tid].push_back(n.id);
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

// ---------------------------------------------------------------------------
// Emisores Scilab por NodeType (etapa 6J.3).
//
// Cada NodeType soportado por el codegen tiene su propia función libre
// que escribe en `c.plan` los expresiones de salida (y derivadas / IC
// si es stateful).  Las funciones comparten una signature uniforme
// `void(PlanCtx&)` para que el dispatch sea por tabla — agregar un
// NodeType nuevo al codegen son TRES líneas:
//
//   1.  void emitFoo(PlanCtx& c) { c.plan.outputExprs[0] = ...; }
//   2.  { NodeType::Foo, &emitFoo } en `kPlanEmits`.
//   3.  (si lleva estado) añadir entrada en isStateful / stateWidth.
//
// Distinción explícita:
//   - Sources    : sólo escriben outputExprs[0..N].
//   - Stateless  : leen `c.src(port)` / `c.paramRef(...)` y escriben outputs.
//   - Stateful   : adicionalmente ponen `c.plan.ic` y `c.plan.deriv`,
//                  referenciando estados vía `c.stateVar(offset)`.
//   - Sinks      : suelen ser pass-through (outputs[0] = src(0)) — varios
//                  comparten emisor.
//   - Custom     : delega al `expression` del descriptor JSON.
//   - Vec3 group : placeholder cero — el sub-lenguaje vive render-side.
// ---------------------------------------------------------------------------
struct PlanCtx {
    const NodeInstance&             n;
    const std::vector<SrcRef>&      srcs;
    const std::vector<std::string>& paramSrcOverride;
    NodePlan&                       plan;

    std::string src(int port) const {
        if (port < (int)srcs.size() && srcs[port].first >= 0)
            return varName(srcs[port].first, srcs[port].second);
        return "0.0";
    }
    std::string paramRef(const char* key, double fb) const {
        return paramRefResolved(n, key, fb, paramSrcOverride);
    }
    std::string stateVar(int offset) const {
        return "x(" + std::to_string(plan.stateSlot + offset) + ")";
    }
};

using NodePlanEmit = void(*)(PlanCtx&);

// ---- Sources --------------------------------------------------------------

void emitAlias(PlanCtx& c) {
    // Etapa 6I.U / 6J.5: emite identidad — alias.out = target.out_<port>.
    // El target se procesa antes en topo-order (la dep virtual del
    // topoSort lo garantiza), así que su varName ya está reservado.
    // `aliasTargetOf` centraliza la resolución de params; si no hay
    // target válido cae a "0.0" (el analyzer lo señalará).
    if (auto target = scinodes::aliasTargetOf(c.n))
        c.plan.outputExprs[0] = varName(target->first, target->second);
    else
        c.plan.outputExprs[0] = "0.0";
}
void emitVoltageSource(PlanCtx& c) { c.plan.outputExprs[0] = c.paramRef("Voltage", 12.0); }
void emitCurrentSource(PlanCtx& c) { c.plan.outputExprs[0] = c.paramRef("Current",  1.0); }
void emitStepSignal(PlanCtx& c) {
    c.plan.outputExprs[0] = "(t >= " + c.paramRef("Step Time", 0.0)
                          + ") * " + c.paramRef("Amplitude", 1.0);
}
void emitSineSignal(PlanCtx& c) {
    const std::string A  = c.paramRef("Amplitude", 1.0);
    const std::string f  = c.paramRef("Frequency", 1.0);
    const std::string ph = c.paramRef("Phase",     0.0);
    c.plan.outputExprs[0] = A + " * sin(2*%pi*" + f + "*t + " + ph + ")";
}
void emitRampSignal(PlanCtx& c) {
    c.plan.outputExprs[0] = c.paramRef("Slope", 1.0) + " * t";
}
void emitDesignTemplate(PlanCtx& c) {
    c.plan.outputExprs.resize(4);
    c.plan.outputExprs[0] = c.paramRef("Target Torque",  10.0);
    c.plan.outputExprs[1] = c.paramRef("Target Speed",  150.0);
    c.plan.outputExprs[2] = c.paramRef("Bus Voltage",   400.0);
    c.plan.outputExprs[3] = c.paramRef("Cooling Class",   1.0);
}
void emitCoolingSystem(PlanCtx& c) {
    c.plan.outputExprs.resize(3);
    c.plan.outputExprs[0] = c.paramRef("Fan Flow",              5.0);
    c.plan.outputExprs[1] = c.paramRef("Water Flow",            0.0);
    c.plan.outputExprs[2] = c.paramRef("Ambient Temperature", 298.0);
}

// ---- Stateless transformers ----------------------------------------------

void emitGain(PlanCtx& c) {
    c.plan.outputExprs[0] = c.paramRef("K", 1.0) + " * " + c.src(0);
}
void emitDegToRad(PlanCtx& c) {
    // out = in · π/180.  Constante literal (17 dígitos significativos
    // como recomienda IEEE 754) en lugar de %pi/180 — algunos backends
    // no resuelven %pi de la misma forma.
    c.plan.outputExprs[0] = "(0.017453292519943295) * " + c.src(0);
}
void emitRadToDeg(PlanCtx& c) {
    c.plan.outputExprs[0] = "(57.29577951308232) * " + c.src(0);
}
void emitSummation(PlanCtx& c) {
    c.plan.outputExprs[0] =
        c.paramRef("Sign1", 1.0) + " * " + c.src(0) + " + " +
        c.paramRef("Sign2", 1.0) + " * " + c.src(1);
}
void emitSaturation(PlanCtx& c) {
    const std::string x  = c.src(0);
    const std::string lo = c.paramRef("Min", -1.0);
    const std::string hi = c.paramRef("Max",  1.0);
    c.plan.outputExprs[0] = "max(min(" + x + ", " + hi + "), " + lo + ")";
}
void emitGearTransmission(PlanCtx& c) {
    // Reductor N:1 con pérdidas:  ω_load = (Eff / Ratio) · ω_motor.
    // Convención estándar de robótica (Spong Cap. 6, Jazar §1.2.6).
    const std::string r = c.paramRef("Ratio",      10.0);
    const std::string e = c.paramRef("Efficiency", 0.95);
    c.plan.outputExprs[0] = "(" + e + " / " + r + ") * " + c.src(0);
}
void emitInverseKinematics(PlanCtx& c) {
    // 2-link planar IK, elbow-up (formulación de atan2 doble — ver doc).
    const std::string L1 = c.paramRef("Link 1 L", 0.3);
    const std::string L2 = c.paramRef("Link 2 L", 0.2);
    const std::string x  = c.src(0);
    const std::string y  = c.src(1);
    const std::string c2 = "max(min(((" + x + ")^2 + (" + y + ")^2 - "
                         + L1 + "^2 - " + L2 + "^2) / (2*" + L1 + "*"
                         + L2 + "), 1), -1)";
    const std::string s2 = "sqrt(1 - (" + c2 + ")^2)";
    c.plan.outputExprs.resize(2);
    c.plan.outputExprs[0] = "atan(" + y + ", " + x + ") - atan("
                          + L2 + "*(" + s2 + "), " + L1 + " + "
                          + L2 + "*(" + c2 + "))";
    c.plan.outputExprs[1] = "atan((" + s2 + "), (" + c2 + "))";
}
void emitPMSMEfficiency(PlanCtx& c) {
    // Analytical efficiency con guard P_in ≈ 0 (bool2s) y guard Ke→0.
    const std::string T   = c.src(0);
    const std::string w   = c.src(1);
    const std::string Ke  = c.src(2);
    const std::string R   = c.paramRef("Stator Resistance", 0.5);
    const std::string Ki  = c.paramRef("Iron Loss Coeff.",  1e-4);
    const std::string Km  = c.paramRef("Mech Loss Coeff.",  1e-3);
    const std::string Iq    = "(" + T + " / (" + Ke + " + 1e-12))";
    const std::string P_out = "(" + T + " * " + w + ")";
    const std::string P_cu  = "(1.5 * " + R + " * " + Iq + "^2)";
    const std::string P_fe  = "(" + Ki + " * " + w + "^2)";
    const std::string P_mech= "(" + Km + " * abs(" + w + "))";
    const std::string P_in  = "(" + P_out + " + " + P_cu + " + "
                                  + P_fe + " + " + P_mech + ")";
    c.plan.outputExprs[0] =
        "bool2s(" + P_in + " > 1e-9) .* (" + P_out + " ./ ("
        + P_in + " + 1e-12))";
}

// ---- Loss sources (v0.9) -------------------------------------------------

void emitJouleLoss(PlanCtx& c) {
    const std::string T  = c.src(0);
    const std::string Ke = c.src(1);
    const std::string R  = c.paramRef("Stator Resistance", 0.5);
    const std::string Iq = "(" + T + " / (" + Ke + " + 1e-12))";
    c.plan.outputExprs[0] = "1.5 * " + R + " * " + Iq + "^2";
}
void emitCoreLoss(PlanCtx& c) {
    // Bertotti two-term iron loss.
    const std::string w  = c.src(0);
    const std::string B  = c.src(1);
    const std::string Kh = c.paramRef("Hysteresis Coeff.", 0.02);
    const std::string Ke = c.paramRef("Eddy Coeff.",       1e-5);
    const std::string pp = c.paramRef("Pole Pairs",        4.0);
    const std::string fe = "(" + pp + " * abs(" + w + ") / (2 * %pi))";
    c.plan.outputExprs[0] =
        Kh + " * " + fe + " * " + B + "^2 + "
      + Ke + " * " + fe + "^2 * " + B + "^2";
}
void emitMechanicalLoss(PlanCtx& c) {
    const std::string w  = c.src(0);
    const std::string Kv = c.paramRef("Viscous Coeff.", 1e-3);
    const std::string Kd = c.paramRef("Drag Coeff.",    1e-5);
    c.plan.outputExprs[0] = Kv + " * abs(" + w + ") + " + Kd + " * " + w + "^2";
}

// ---- Thermal -------------------------------------------------------------

void emitThermalMass(PlanCtx& c) {
    // Single-node RC: state x = T, IC = T_ambient.
    const std::string Tnode = c.stateVar(0);
    const std::string C  = c.paramRef("Thermal Capacitance", 500.0);
    const std::string R  = c.paramRef("Thermal Resistance",    0.5);
    const std::string Ta = c.paramRef("Ambient Temperature", 298.0);
    c.plan.outputExprs[0] = Tnode;
    c.plan.ic    = { Ta };
    c.plan.deriv = { "(" + c.src(0) + " - (" + Tnode + " - " + Ta
                   + ") / " + R + ") / " + C };
}
void emitThermalNode(PlanCtx& c) {
    const std::string T  = c.stateVar(0);
    const std::string C  = c.paramRef("Thermal Capacitance", 500.0);
    const std::string T0 = c.paramRef("Initial Temperature", 298.0);
    c.plan.outputExprs[0] = T;
    c.plan.ic    = { T0 };
    c.plan.deriv = { "(" + c.src(0) + " + " + c.src(1)
                   + " + " + c.src(2) + " + " + c.src(3) + ") / " + C };
}
void emitThermalResistance(PlanCtx& c) {
    const std::string Th = c.src(0);
    const std::string Tc = c.src(1);
    const std::string R  = c.paramRef("Thermal Resistance", 1.0);
    c.plan.outputExprs.resize(2);
    c.plan.outputExprs[0] = "(" + Th + " - " + Tc + ") / " + R;
    c.plan.outputExprs[1] = "(" + Tc + " - " + Th + ") / " + R;
}
void emitConvectiveCooling(PlanCtx& c) {
    const std::string Th    = c.src(0);
    const std::string Tc    = c.src(1);
    const std::string flow  = c.src(2);
    const std::string h0    = c.paramRef("Base Coeff. h_0", 1.0);
    const std::string hk    = c.paramRef("Slope per Flow",  0.5);
    const std::string h     = "(" + h0 + " + " + hk + " * " + flow + ")";
    c.plan.outputExprs.resize(2);
    c.plan.outputExprs[0] = h + " * (" + Th + " - " + Tc + ")";
    c.plan.outputExprs[1] = h + " * (" + Tc + " - " + Th + ")";
}

// ---- Mechanical / modal / aux --------------------------------------------

void emitMaxwellForce(PlanCtx& c) {
    // sigma_r = B_g^2 / (2·mu_0).  mu_0 = 4·pi·1e-7 inlined.
    const std::string B = c.src(0);
    c.plan.outputExprs[0] = B + "^2 / (2 * 4 * %pi * 1e-7)";
}
void emitTolerancePerturbator(PlanCtx& c) {
    // Adds U[-h, +h] perturbation to its input each step.
    const std::string u = c.src(0);
    const std::string h = c.paramRef("Half Tolerance", 0.05);
    c.plan.outputExprs[0] = u + " + " + h + " * (2 * rand() - 1)";
}
void emitModalFrequency(PlanCtx& c) {
    // Thin-ring natural frequency; shape factor zeroed for m≤1.
    const std::string R = c.src(0);
    const std::string E = c.paramRef("Young's Modulus", 200.0e9);
    const std::string rho = c.paramRef("Density",       7850.0);
    const std::string t   = c.paramRef("Thickness",       0.02);
    const std::string m   = c.paramRef("Mode Order",      2.0);
    const std::string shape =
        "bool2s(" + m + " > 1.5) * " + m + " * "
        "(" + m + "^2 - 1) / sqrt(" + m + "^2 + 1)";
    c.plan.outputExprs[0] =
        "(" + t + " / (2 * %pi * " + R + "^2 + 1e-12)) "
        "* sqrt(" + E + " / (12 * " + rho + ")) "
        "* (" + shape + ")";
}
void emitAirgapFluxDensity(PlanCtx& c) {
    // Pure-state: x = rotor angle θ, dθ/dt = ω = src(0).
    const std::string th = c.stateVar(0);
    const std::string B  = c.paramRef("Peak Flux Density",   0.85);
    const std::string pp = c.paramRef("Pole Pairs",          4.0);
    const std::string a3 = c.paramRef("3rd Harmonic Ratio",  0.10);
    const std::string as = c.paramRef("Slot Harmonic Ratio", 0.05);
    const std::string Ns = c.paramRef("Slot Count",         24.0);
    c.plan.outputExprs[0] =
        B + " * (sin(" + pp + " * " + th + ") + "
          + a3 + " * sin(3 * " + pp + " * " + th + ") + "
          + as + " * sin(" + Ns + " * " + th + "))";
    c.plan.ic    = { "0.0" };
    c.plan.deriv = { c.src(0) };
}
void emitPMSMElectromagnetic(PlanCtx& c) {
    const std::string D  = c.src(0);
    const std::string L  = c.src(1);
    const std::string w  = c.src(2);
    const std::string N  = c.paramRef("Turns per Phase",       100.0);
    const std::string kw = c.paramRef("Winding Factor",          0.95);
    const std::string pp = c.paramRef("Pole Pairs",              4.0);
    const std::string Bg = c.paramRef("Airgap Flux Density",     0.85);
    const std::string g  = c.paramRef("Mechanical Airgap",       0.001);
    const std::string hm = c.paramRef("Magnet Thickness",        0.003);
    const std::string Ns = c.paramRef("Slot Count",             24.0);
    const char* mu0  = "(4*%pi*1e-7)";
    const char* mu_r = "1.05";
    const std::string Ke   =
        "(" + kw + " * " + N + " * " + pp + " * " + Bg
        + " * " + L + " * " + D + ") / 2";
    const std::string g_eff = "(" + g + " + " + hm + " / " + mu_r + ")";
    const std::string L_ph =
        "(" + std::string(mu0) + " * " + N + "^2 * " + kw + "^2"
        + " * %pi * " + D + " * " + L + ") / "
        "(8 * " + pp + "^2 * " + g_eff + ")";
    const std::string Vrms = "(" + Ke + ") * " + w + " / sqrt(2)";
    const std::string Tcog =
        "(" + Bg + "^2 * " + D + "^2 * " + L + ") / "
        "(8 * " + std::string(mu0) + " * " + Ns + ")";
    c.plan.outputExprs.resize(4);
    c.plan.outputExprs[0] = Ke;
    c.plan.outputExprs[1] = L_ph;
    c.plan.outputExprs[2] = Vrms;
    c.plan.outputExprs[3] = Tcog;
}

// ---- Sizing (PMSM / IPM / BLDC comparten estructura) ---------------------

void emitPMSMSizing(PlanCtx& c) {
    // Surface-mount PMSM classical sizing.  D^2·L = 2·T / (π·B·A).
    const std::string T  = c.src(0);
    const std::string w  = c.src(1);
    const std::string B  = c.paramRef("Magnetic Loading B",      0.85);
    const std::string A  = c.paramRef("Electric Loading A", 40000.0);
    const std::string al = c.paramRef("Aspect Ratio L/D",        1.2);
    const std::string D_cubed =
        "(2 * " + T + ") / (%pi * " + B + " * " + A + " * " + al + ")";
    const std::string D_expr = "((" + D_cubed + ")^(1.0/3.0))";
    c.plan.outputExprs.resize(3);
    c.plan.outputExprs[0] = D_expr;
    c.plan.outputExprs[1] = al + " * " + D_expr;
    c.plan.outputExprs[2] = T + " * " + w;
}
void emitIPMSizing(PlanCtx& c) {
    const std::string T  = c.src(0);
    const std::string w  = c.src(1);
    const std::string B  = c.paramRef("Magnetic Loading B",      0.85);
    const std::string A  = c.paramRef("Electric Loading A", 40000.0);
    const std::string al = c.paramRef("Aspect Ratio L/D",        1.2);
    const std::string ks = c.paramRef("Saliency Factor",         1.2);
    const std::string D_cubed =
        "(2 * " + T + ") / (%pi * " + B + " * " + A + " * "
        + al + " * " + ks + ")";
    const std::string D_expr = "((" + D_cubed + ")^(1.0/3.0))";
    c.plan.outputExprs.resize(3);
    c.plan.outputExprs[0] = D_expr;
    c.plan.outputExprs[1] = al + " * " + D_expr;
    c.plan.outputExprs[2] = T + " * " + w;
}
void emitBLDCSizing(PlanCtx& c) {
    const std::string T  = c.src(0);
    const std::string w  = c.src(1);
    const std::string B  = c.paramRef("Magnetic Loading B",      0.90);
    const std::string A  = c.paramRef("Electric Loading A", 35000.0);
    const std::string al = c.paramRef("Aspect Ratio L/D",        1.0);
    const std::string kt = c.paramRef("Trapezoidal Factor",      1.15);
    const std::string D_cubed =
        "(2 * " + T + ") / (%pi * " + B + " * " + A + " * "
        + al + " * " + kt + ")";
    const std::string D_expr = "((" + D_cubed + ")^(1.0/3.0))";
    c.plan.outputExprs.resize(3);
    c.plan.outputExprs[0] = D_expr;
    c.plan.outputExprs[1] = al + " * " + D_expr;
    c.plan.outputExprs[2] = T + " * " + w;
}

// ---- Stateful transformers (integrated by Scilab ode rk) -----------------

void emitIntegrator(PlanCtx& c) {
    const std::string x0 = c.stateVar(0);
    c.plan.outputExprs[0] = x0;
    c.plan.ic    = { c.paramRef("Initial Cond.", 0.0) };
    c.plan.deriv = { c.src(0) };
}
void emitLowPassFilter(PlanCtx& c) {
    const std::string x0 = c.stateVar(0);
    const std::string fc = c.paramRef("Cutoff Freq.", 100.0);
    c.plan.outputExprs[0] = x0;
    c.plan.ic    = { "0.0" };
    c.plan.deriv = { "2*%pi*" + fc + " * (" + c.src(0) + " - " + x0 + ")" };
}
void emitDifferentiator(PlanCtx& c) {
    // Filtered derivative  H(s) = s / (1 + s/wc).  x = LP(input); y = wc·(u − x).
    const std::string x0 = c.stateVar(0);
    const std::string fc = c.paramRef("Cutoff Freq.", 100.0);
    const std::string wc = "(2*%pi*" + fc + ")";
    c.plan.outputExprs[0] = wc + " * (" + c.src(0) + " - " + x0 + ")";
    c.plan.ic    = { "0.0" };
    c.plan.deriv = { wc + " * (" + c.src(0) + " - " + x0 + ")" };
}
void emitTransferFunction(PlanCtx& c) {
    // H(s) = num[0] / (den[0] + den[1]·s).  Pure-state: y = x.
    const std::string x0 = c.stateVar(0);
    const std::string b  = c.paramRef("num[0]", 1.0);
    const std::string a0 = c.paramRef("den[0]", 1.0);
    const std::string a1 = c.paramRef("den[1]", 1.0);
    c.plan.outputExprs[0] = x0;
    c.plan.ic    = { "0.0" };
    c.plan.deriv = { "(" + b + "*" + c.src(0) + " - " + a0 + "*" + x0
                   + ") / " + a1 };
}
void emitTransferFunction2(PlanCtx& c) {
    // H(s) = (b1·s + b0)/(s² + a1·s + a0). Controllable canonical, 2 states.
    const std::string x1 = c.stateVar(0);
    const std::string x2 = c.stateVar(1);
    const std::string b0 = c.paramRef("num[0]", 1.0);
    const std::string b1 = c.paramRef("num[1]", 0.0);
    const std::string a0 = c.paramRef("den[0]", 1.0);
    const std::string a1 = c.paramRef("den[1]", 0.0);
    c.plan.outputExprs[0] = b0 + "*" + x1 + " + " + b1 + "*" + x2;
    c.plan.ic    = { "0.0", "0.0" };
    c.plan.deriv = {
        x2,
        "-" + a0 + "*" + x1 + " - " + a1 + "*" + x2 + " + " + c.src(0)
    };
}
void emitPIDController(PlanCtx& c) {
    // Standard PID con filtered derivative + back-calculation anti-windup
    // (Åström & Hägglund, 2006).  Port 1 (u_sat) opcional; si está
    // desconectado, u_sat ≡ y → la corrección colapsa a cero.
    const std::string ei = c.stateVar(0);
    const std::string el = c.stateVar(1);
    const std::string Kp = c.paramRef("Kp", 1.0);
    const std::string Ki = c.paramRef("Ki", 0.0);
    const std::string Kd = c.paramRef("Kd", 0.0);
    const std::string N  = c.paramRef("N (filter)", 100.0);
    const std::string Kt = c.paramRef("Kt (anti-windup)", 0.0);
    const std::string err = c.src(0);
    const std::string y   = Kp + " * " + err + " + " + Ki + " * " + ei
                          + " + " + Kd + " * " + N + " * (" + err
                          + " - " + el + ")";
    const std::string u_sat = (c.srcs.size() > 1 && c.srcs[1].first >= 0)
                                ? c.src(1)
                                : ("(" + y + ")");
    c.plan.outputExprs[0] = y;
    c.plan.ic    = { "0.0", "0.0" };
    c.plan.deriv = {
        err + " + " + Kt + " * ((" + u_sat + ") - (" + y + "))",
        N + " * (" + err + " - " + el + ")"
    };
}
void emitDCMotorModel(PlanCtx& c) {
    const std::string i = c.stateVar(0);
    const std::string w = c.stateVar(1);
    const std::string Ra = c.paramRef("Ra", 1.0);
    const std::string La = c.paramRef("La", 0.01);
    const std::string Ke = c.paramRef("Ke", 0.1);
    const std::string Kt = c.paramRef("Kt", 0.1);
    const std::string J  = c.paramRef("J",  0.01);
    const std::string B  = c.paramRef("B",  0.001);
    c.plan.outputExprs[0] = w;
    c.plan.ic    = { "0.0", "0.0" };
    c.plan.deriv = {
        "(" + c.src(0) + " - " + Ra + "*" + i + " - " + Ke + "*" + w + ") / " + La,
        "(" + Kt + "*" + i + " - " + B + "*" + w + ") / " + J
    };
}

// ---- Sinks ---------------------------------------------------------------

// Pass-through: outputs[0] = src(0).  Compartido por los Sinks de un solo
// canal (FFTAnalyzer, DataLogger, TerminalDisplay, View3DSink,
// View3DThermalSink, DistributionSink).
void emitSinkPassthrough(PlanCtx& c) { c.plan.outputExprs[0] = c.src(0); }

void emitOscilloscope(PlanCtx& c) {
    // Multi-canal: emitimos una columna por puerto CONECTADO (no por puerto
    // declarado en el catálogo).  Oscilloscopes con 1 input efectivo
    // siguen generando un solo canal — los tests legacy no rompen.
    std::vector<int> connectedPorts;
    connectedPorts.reserve(c.srcs.size());
    for (int i = 0; i < (int)c.srcs.size(); ++i)
        if (c.srcs[i].first >= 0) connectedPorts.push_back(i);
    if (connectedPorts.empty()) {
        c.plan.outputExprs[0] = "0.0";
    } else {
        c.plan.outputExprs.resize(connectedPorts.size());
        for (size_t k = 0; k < connectedPorts.size(); ++k)
            c.plan.outputExprs[k] = c.src(connectedPorts[k]);
    }
}
void emitPhasePortrait(PlanCtx& c) {
    c.plan.outputExprs.resize(2);
    c.plan.outputExprs[0] = c.src(0);
    c.plan.outputExprs[1] = c.src(1);
}
void emitThreeChannelSink(PlanCtx& c) {
    // Heatmap (x, y, c) y View3DDeformationSink (freq, mode, amp).
    c.plan.outputExprs.resize(3);
    c.plan.outputExprs[0] = c.src(0);
    c.plan.outputExprs[1] = c.src(1);
    c.plan.outputExprs[2] = c.src(2);
}

// ---- Custom (JSON-loaded) ------------------------------------------------

void emitCustom(PlanCtx& c) {
    const auto* cd = scinodes::customNodes().find(c.n.customType);
    const NodeCategory cat = cd ? cd->category : NodeCategory::Transformer;
    if (cat == NodeCategory::Sink) {
        c.plan.outputExprs[0] = c.src(0);
    } else if (cd && !cd->expression.empty()) {
        c.plan.outputExprs[0] = substituteCustom(cd->expression, c.n, c.srcs);
    } else {
        c.plan.outputExprs[0] = "0.0";
    }
}

// ---- Sub-lenguaje Vec3 ---------------------------------------------------

// SubGraph y stubs llegan al codegen sólo si flatten falló (bug río
// arriba).  Los marcamos como "supported" por consistencia con la API
// histórica (isSupported = catálogo conocido) pero el emisor no escribe
// nada — el "0.0" default queda.
void emitNoOp(PlanCtx&) {}

void emitVec3Placeholder(PlanCtx& c) {
    // El codegen Scilab no soporta vec3 nativo (buffers del bridge son
    // escalares).  Emitimos "0.0" por output port para que ningún Sink
    // downstream lea variable no definida.  La evaluación real vive
    // render-side via el SceneCollector (etapa 6J.2).
    c.plan.outputExprs.assign(defOf(c.n).outputPorts, "0.0");
}

// ---- Tabla maestra: NodeType → emisor ------------------------------------
//
// Single source of truth.  `isSupported` deriva su veredicto de la
// presencia de entrada acá.  Agregar un NodeType nuevo al codegen
// requiere una sola línea — no editar el dispatcher.
NodePlanEmit lookupPlanEmit(NodeType t) {
    static const std::unordered_map<NodeType, NodePlanEmit> kPlanEmits = {
        // Sources
        { NodeType::Alias,                &emitAlias                },
        { NodeType::VoltageSource,        &emitVoltageSource        },
        { NodeType::CurrentSource,        &emitCurrentSource        },
        { NodeType::StepSignal,           &emitStepSignal           },
        { NodeType::SineSignal,           &emitSineSignal           },
        { NodeType::RampSignal,           &emitRampSignal           },
        { NodeType::DesignTemplate,       &emitDesignTemplate       },
        { NodeType::CoolingSystem,        &emitCoolingSystem        },
        // Stateless transformers
        { NodeType::Gain,                 &emitGain                 },
        { NodeType::DegToRad,             &emitDegToRad             },
        { NodeType::RadToDeg,             &emitRadToDeg             },
        { NodeType::Summation,            &emitSummation            },
        { NodeType::Saturation,           &emitSaturation           },
        { NodeType::GearTransmission,     &emitGearTransmission     },
        { NodeType::InverseKinematics,    &emitInverseKinematics    },
        { NodeType::PMSMEfficiency,       &emitPMSMEfficiency       },
        // Loss sources
        { NodeType::JouleLoss,            &emitJouleLoss            },
        { NodeType::CoreLoss,             &emitCoreLoss             },
        { NodeType::MechanicalLoss,       &emitMechanicalLoss       },
        // Thermal
        { NodeType::ThermalMass,          &emitThermalMass          },
        { NodeType::ThermalNode,          &emitThermalNode          },
        { NodeType::ThermalResistance,    &emitThermalResistance    },
        { NodeType::ConvectiveCooling,    &emitConvectiveCooling    },
        // Mechanical / modal / aux
        { NodeType::MaxwellForce,         &emitMaxwellForce         },
        { NodeType::TolerancePerturbator, &emitTolerancePerturbator },
        { NodeType::ModalFrequency,       &emitModalFrequency       },
        { NodeType::AirgapFluxDensity,    &emitAirgapFluxDensity    },
        { NodeType::PMSMElectromagnetic,  &emitPMSMElectromagnetic  },
        // Sizing
        { NodeType::PMSMSizing,           &emitPMSMSizing           },
        { NodeType::IPMSizing,            &emitIPMSizing            },
        { NodeType::BLDCSizing,           &emitBLDCSizing           },
        // Stateful
        { NodeType::Integrator,           &emitIntegrator           },
        { NodeType::LowPassFilter,        &emitLowPassFilter        },
        { NodeType::Differentiator,       &emitDifferentiator       },
        { NodeType::TransferFunction,     &emitTransferFunction     },
        { NodeType::TransferFunction2,    &emitTransferFunction2    },
        { NodeType::PIDController,        &emitPIDController        },
        { NodeType::DCMotorModel,         &emitDCMotorModel         },
        // Sinks pass-through (single channel)
        { NodeType::FFTAnalyzer,          &emitSinkPassthrough      },
        { NodeType::DataLogger,           &emitSinkPassthrough      },
        { NodeType::TerminalDisplay,      &emitSinkPassthrough      },
        { NodeType::View3DSink,           &emitSinkPassthrough      },
        { NodeType::View3DThermalSink,    &emitSinkPassthrough      },
        { NodeType::DistributionSink,     &emitSinkPassthrough      },
        // Multi-channel sinks
        { NodeType::Oscilloscope,         &emitOscilloscope         },
        { NodeType::PhasePortrait,        &emitPhasePortrait        },
        { NodeType::HeatmapSink,          &emitThreeChannelSink     },
        { NodeType::View3DDeformationSink,&emitThreeChannelSink     },
        // Custom (JSON-loaded)
        { NodeType::Custom,               &emitCustom               },
        // SubGraph + stubs — el codegen los aplana antes; entran al map
        // sólo para que isSupported() devuelva true (catálogo conocido).
        { NodeType::SubGraph,             &emitNoOp                 },
        { NodeType::SubGraphInput,        &emitNoOp                 },
        { NodeType::SubGraphOutput,       &emitNoOp                 },
        // Vec3 group (placeholder zeros — eval real vive render-side)
        { NodeType::Vec3Constant,         &emitVec3Placeholder      },
        { NodeType::CombineXYZ,           &emitVec3Placeholder      },
        { NodeType::SeparateXYZ,          &emitVec3Placeholder      },
        { NodeType::VectorAdd,            &emitVec3Placeholder      },
        { NodeType::VectorSub,            &emitVec3Placeholder      },
        { NodeType::VectorScale,          &emitVec3Placeholder      },
        { NodeType::VectorDot,            &emitVec3Placeholder      },
        { NodeType::VectorCross,          &emitVec3Placeholder      },
        { NodeType::VectorLength,         &emitVec3Placeholder      },
        { NodeType::VectorNormalize,      &emitVec3Placeholder      },
    };
    if (auto it = kPlanEmits.find(t); it != kPlanEmits.end())
        return it->second;
    return nullptr;
}

NodePlan planNode(const NodeInstance& n, int slotStart,
                  const std::vector<SrcRef>& srcs,
                  const std::vector<std::string>& paramSrcOverride) {
    NodePlan p;
    p.id          = n.id;
    p.stateSlot   = isStateful(n.type) ? slotStart : 0;
    p.stateWidth  = stateWidth(n.type);

    // Default: 1 output con expresión vacía.  Los emisores que producen
    // múltiples salidas hacen resize() antes de escribir.  Los nodos sin
    // emisor registrado se quedan con "0.0" — Scilab no se queja porque
    // varName() de un nodo no soportado nunca se referencia (isSupported
    // filtra antes).
    p.outputExprs.assign(1, "0.0");

    PlanCtx ctx{ n, srcs, paramSrcOverride, p };
    if (NodePlanEmit fn = lookupPlanEmit(n.type)) fn(ctx);
    return p;
}

// Vestigio del switch original — un dispatcher dummy que mantiene la
// firma histórica para los call-sites internos que no migraron al
// nuevo PlanCtx.  Se borrará cuando todos los caminos pasen por
// `lookupPlanEmit`.

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
    // Single source of truth: si el codegen tiene un emisor registrado
    // para `t`, lo soportamos.  Antes esta función mantenía una lista
    // paralela al switch de planNode — fuente clásica de bugs cuando los
    // dos listados divergían.  Etapa 6J.3 los unificó.
    //
    // SubGraph / SubGraphInput / SubGraphOutput intencionalmente NO
    // están en el map: el `generate()` aplana antes de emitir, así que
    // cualquier stub que llegue al codegen real es un bug río arriba.
    return lookupPlanEmit(t) != nullptr;
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
        // Etapa 6J.6: dispatch sobre Custom/SubGraph/Builtin centralizada
        // en `NodeGraph::addNodeMirroring`.  Pasamos `child` como
        // `srcContainer` para que la rama SubGraph copie el sub-sub-grafo
        // hijo, preservando IDs (el siguiente paso del bucle generate()
        // lo aplanará en la próxima iteración).  preferredId = cn.id si
        // no colisiona con el padre; si colisiona, el helper aloca uno
        // fresco.  Sin preservación de IDs, los seeds capturados con el
        // plan viejo no encuentran sus slots de estado y la simulación
        // se reinicia desde IC en cada hot-reload.
        const int newId = g.addNodeMirroring(cn, cn.id, child);
        idMap[cn.id] = newId;
        for (const auto& [k, v] : cn.params)       g.setParam(newId, k, v);
        for (const auto& [k, v] : cn.stringParams) g.setStringParam(newId, k, v);
        if (!cn.assetPath.empty())                 g.setAssetPath(newId, cn.assetPath);
        // Transferir overrides de unidad per-puerto (etapa 6G).  Sin
        // esto, al aplanar un SubGraph cuyo PID declara
        // "in=rad/s, out=V" via override, el PID en el grafo plano
        // queda polimórfico, la propagación backward asigna V a todo
        // el lazo, y los `tryAddEdge` de feedback abajo entran en
        // conflict (R7 ON) y se pierden silenciosamente.
        for (const auto& [key, text] : cn.portUnitOverrides)
            g.setPortUnitOverride(newId, key, text);
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
        if (auto err = validateCustomDescriptor(*n)) {
            plan.error = std::move(*err);
            return plan;
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

    // 4a. Sink channel list — sólo nodos categoryOf==Sink.  La UI itera
    //     este vector para renderear los paneles de plot.  No cambió de
    //     semántica histórica.
    for (int id : order) {
        const NodeInstance* n = graph.findNode(id);
        if (!n || categoryOf(*n) != NodeCategory::Sink) continue;
        int chans = std::max(1, (int)plans.at(id).outputExprs.size());
        for (int c = 0; c < chans; ++c)
            plan.sinkChannels.push_back({ id, c });
    }
    // 4b. Buffered channel list (etapa 6J.8) — TODOS los outputs
    //     escalares que el bridge debería guardar en su ring buffer.
    //     Superset de sinkChannels.  El walker 3D lee directo aquí
    //     `bridge.buffer(nodeId, port)` para encontrar el valor en
    //     cualquier punto del cable sin buscar Sinks downstream
    //     (un converter Rad→Deg ya no rompe la lectura).  Outputs
    //     Geometry / Vec3 se omiten (no son escalares útiles).
    for (int id : order) {
        const NodeInstance* n = graph.findNode(id);
        if (!n) continue;
        const NodeDef& def = defOf(*n);
        int chans = std::max(1, (int)plans.at(id).outputExprs.size());
        for (int c = 0; c < chans; ++c) {
            if (!isScalarType(outputPortTypeOf(def, c))) continue;
            plan.bufferedChannels.push_back({ id, c });
        }
    }

    // 5. Emit the .sce driver.
    std::ostringstream out;
    out << "// SciNodes driver script — autogenerated, do not edit.\n"
        << "// State vector length: " << totalState << "\n"
        << "// STATE columns (node:channel):";
    for (const auto& sc : plan.bufferedChannels) out << ' ' << sc.nodeId << ':' << sc.channel;
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
    for (const auto& sc : plan.bufferedChannels)
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
    for (size_t i = 0; i < plan.bufferedChannels.size(); ++i) out << " %.6e";
    out << "\\n\", nanid";
    for (const auto& sc : plan.bufferedChannels)
        out << ", " << varName(sc.nodeId, sc.channel);
    out << ");\n";

    // Append the current step to history.  Si pasamos la capacidad,
    // duplicamos (amortizado O(1)).
    out << "            hist_idx = hist_idx + 1;\n"
        << "            if hist_idx > hist_cap then\n"
        << "                t_hist = [t_hist, zeros(1, hist_cap)];\n";
    for (const auto& sc : plan.bufferedChannels)
        out << "                " << varName(sc.nodeId, sc.channel)
            << "_hist = [" << varName(sc.nodeId, sc.channel)
            << "_hist, zeros(1, hist_cap)];\n";
    out << "                hist_cap = hist_cap * 2;\n"
        << "            end\n"
        << "            t_hist(hist_idx) = t;\n";
    for (const auto& sc : plan.bufferedChannels)
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
    for (const auto& sc : plan.bufferedChannels) {
        const std::string vn = varName(sc.nodeId, sc.channel) + "_hist";
        out << "            " << vn << "_full = " << vn << ";\n"
            << "            " << vn << " = " << vn << "(1:hist_idx);\n";
    }
    out << "            save(spath, \"t_hist\"";
    for (const auto& sc : plan.bufferedChannels)
        out << ", \"" << varName(sc.nodeId, sc.channel) << "_hist\"";
    out << ");\n"
        << "            t_hist = t_full;\n";
    for (const auto& sc : plan.bufferedChannels) {
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
        if (auto err = validateCustomDescriptor(*n)) {
            gs.error = std::move(*err);
            return gs;
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

    // 7a) Sumideros declarados — los que la UI itera para paneles de plot.
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
    // 7b) Buffered outputs (etapa 6J.8): superset — TODO output escalar.
    //     El backend guarda un ring buffer por entrada para que el walker
    //     3D y los consumidores de live-values lean valor en cualquier
    //     punto del cable sin depender de Sinks downstream.
    for (int id : order) {
        const NodeInstance* n = graph.findNode(id);
        if (!n) continue;
        const NodeDef& def = defOf(*n);
        int chans = std::max(1, (int)plans.at(id).outputExprs.size());
        for (int c = 0; c < chans; ++c) {
            if (!isScalarType(outputPortTypeOf(def, c))) continue;
            scinodes::BackendPrepareSpec::SinkChannel sc;
            sc.nodeId     = id;
            sc.channel    = c;
            sc.expression = varName(id, c);
            gs.spec.bufferedChannels.push_back(std::move(sc));
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
