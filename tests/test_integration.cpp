// -----------------------------------------------------------------------
// SciNodes integration tests — drive a real scilab-cli subprocess through
// ScilabBridge and check the per-sink output against analytical predictions
// or convergence tolerances.
//
// Requires Scilab to be installed (auto-discovered by ScilabBridge, or
// pointed to via $SCN_SCILAB_PATH).
//
// Build: cmake --build build --target test_integration
// Run:   ./build/test_integration
// -----------------------------------------------------------------------
#include "../src/core/CustomNodeRegistry.hpp"
#include "../src/core/Fft.hpp"
#include "../src/core/NodeGraph.hpp"
#include "../src/core/ScilabBridge.hpp"
#include "../src/core/ScilabCodeGen.hpp"
#include "../src/core/ScnSerializer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

// ---- Tolerancias para `EXPECT_NEAR` --------------------------------------
// Cinco niveles, elegidos según qué clase de comparación se hace.  Antes
// estaban como literales `1e-3`/`1e-4`/`0.10` dispersos en los asserts;
// agruparlos hace explícito qué expectativa de precisión tiene cada test.
//   - kTolStrict      → integradores con dt fino, fórmulas analíticas con
//                       cancelación trivial.  ±1e-5.
//   - kTolTight       → soluciones cerradas con dinámica de orden bajo
//                       (LPF, exp(-t), 1-cos(t)).  ±1e-4.
//   - kTolMedium      → ODE no triviales, controles más rápidos que el
//                       solver.  ±1e-3.
//   - kTolLoose       → respuestas con transitorio comparado contra
//                       analítico aproximado.  ±1e-2.
//   - kTolQualitative → contraste cualitativo (steady-state esperado
//                       contra valor de orden de magnitud).  ±0.10.
constexpr double kTolStrict      = 1e-5;
constexpr double kTolTight       = 1e-4;
constexpr double kTolMedium      = 1e-3;
constexpr double kTolLoose       = 1e-2;
constexpr double kTolQualitative = 0.10;

// ---- Minimal test framework (mirrors test_grammar.cpp) ------------------
static int g_pass = 0, g_fail = 0;

#define EXPECT_TRUE(cond) do { \
    if (cond) { ++g_pass; } \
    else { ++g_fail; \
           std::cerr << "  FAIL  " << #cond \
                     << "  [" << __FILE__ << ":" << __LINE__ << "]\n"; } \
} while(0)

#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

#define EXPECT_NEAR(got, want, tol) do { \
    double _g = (got), _w = (want), _t = (tol); \
    if (std::fabs(_g - _w) <= _t) { ++g_pass; } \
    else { ++g_fail; \
           std::cerr << "  FAIL  " << #got << " ≈ " << #want \
                     << "  (got=" << _g << " want=" << _w \
                     << " diff=" << std::fabs(_g - _w) \
                     << " tol=" << _t << ")" \
                     << "  [" << __FILE__ << ":" << __LINE__ << "]\n"; } \
} while(0)

// Read the most-recently written sample for a sink.
static float lastSample(const ScilabBridge& br, int sinkId) {
    auto buf = br.buffer(sinkId);
    return buf.empty() ? 0.0f : buf.back();
}

// Step the bridge until simTime >= target − ½·dt, return the final sample.
// Using a half-step margin gives a clean exit even with float drift
// across many additions; callers should compute expected values from
// `br.time()` after the call rather than from the nominal `target`.
static float runUntil(ScilabBridge& br, int sinkId,
                      double target, float dt = 1.0f/60.0f) {
    while (br.time() < target - 0.5f * dt) {
        if (!br.step(dt)) {
            std::cerr << "  step failed: " << br.lastError() << "\n";
            return 0.0f;
        }
    }
    return lastSample(br, sinkId);
}

// ========================================================================
// Scenario 1 — Stateless: Sine(2 Hz) → Gain(K=3) → Scope
//   At t = 0.5 s, sin(2π·2·0.5) = sin(2π) = 0, so y = 0.
// ========================================================================
static void scenario_stateless_chain() {
    std::cout << "[1] Stateless  Sine(2 Hz) → Gain(K=3) → Scope\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int t = g.addNode(NodeType::Gain);
    int k = g.addNode(NodeType::Oscilloscope);
    g.setParam(s, "Frequency", 2.0);
    g.setParam(t, "K",         3.0);
    auto* ns = g.findNode(s); auto* nt = g.findNode(t); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0));
    g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    float got = runUntil(br, k, 0.5);
    EXPECT_NEAR(got, 0.0, kTolStrict);
}

// ========================================================================
// Scenario 2 — Stateful: Step(1) → Integrator(IC=0) → Scope
//   y(t) = t  (integral of unit step).
// ========================================================================
static void scenario_integrator() {
    std::cout << "[2] Stateful   Step(1) → Integrator → Scope\n";
    NodeGraph g;
    int s = g.addNode(NodeType::StepSignal);
    int i = g.addNode(NodeType::Integrator);
    int k = g.addNode(NodeType::Oscilloscope);
    auto* ns = g.findNode(s); auto* ni = g.findNode(i); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), ni->inputAttrId(0));
    g.tryAddEdge(ni->outputAttrId(), nk->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    EXPECT_NEAR(runUntil(br, k, 1.0), 1.0, kTolStrict);
}

// ========================================================================
// Scenario 3 — Coupled ODE: Voltage(12V) → DCMotor (default) → Scope
//   Closed-form slow eigenvalue λ ≈ -1.11, ω_ss = 109.09 rad/s.
//   At t = 1 s, ω ≈ ω_ss·(1 - e^(-1/0.9)) ≈ 72.78 rad/s.  (~0.5% tolerance.)
// ========================================================================
static void scenario_open_loop_motor() {
    std::cout << "[3] Coupled    Voltage(12V) → DCMotor → Scope\n";
    NodeGraph g;
    int v = g.addNode(NodeType::VoltageSource);
    int m = g.addNode(NodeType::DCMotorModel);
    int k = g.addNode(NodeType::Oscilloscope);
    auto* nv = g.findNode(v); auto* nm = g.findNode(m); auto* nk = g.findNode(k);
    g.tryAddEdge(nv->outputAttrId(), nm->inputAttrId(0));
    g.tryAddEdge(nm->outputAttrId(), nk->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    EXPECT_NEAR(runUntil(br, k, 1.0), 72.78, 1.0);   // ~1.4% tolerance
}

// ========================================================================
// Scenario 4 — Closed loop 1st-order:
//   Step(1) → Sum(+,-) → Integrator → ↺
//   Equation y' = 1 - y → y(t) = 1 - e^(-t).
// ========================================================================
static void scenario_closed_loop_first_order() {
    std::cout << "[4] Feedback   Step(1) → Sum(+,-) → Integrator → ↺\n";
    NodeGraph g;
    int step  = g.addNode(NodeType::StepSignal);
    int sum   = g.addNode(NodeType::Summation);
    int integ = g.addNode(NodeType::Integrator);
    int scope = g.addNode(NodeType::Oscilloscope);
    g.setParam(sum, "Sign2", -1.0);
    auto* ns  = g.findNode(step);
    auto* nsm = g.findNode(sum);
    auto* ni  = g.findNode(integ);
    auto* nk  = g.findNode(scope);
    g.tryAddEdge(ns->outputAttrId(),  nsm->inputAttrId(0));
    g.tryAddEdge(nsm->outputAttrId(), ni->inputAttrId(0));
    g.tryAddEdge(ni->outputAttrId(),  nk->inputAttrId(0));
    g.tryAddEdge(ni->outputAttrId(),  nsm->inputAttrId(1));   // feedback

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    runUntil(br, scope, 1.0);
    double t1 = br.time();
    EXPECT_NEAR(lastSample(br, scope), 1.0 - std::exp(-t1), kTolTight);
    runUntil(br, scope, 3.0);
    double t3 = br.time();
    EXPECT_NEAR(lastSample(br, scope), 1.0 - std::exp(-t3), kTolTight);
}

// ========================================================================
// Scenario 5 — Live tuning:
//   Sine(1 Hz) → Gain → Scope. Verify K=2 result, send K=5, see change.
// ========================================================================
static void scenario_live_tuning() {
    std::cout << "[5] Live tune  Sine → Gain → Scope, sendParameter K\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int t = g.addNode(NodeType::Gain);
    int k = g.addNode(NodeType::Oscilloscope);
    g.setParam(t, "K", 2.0);
    auto* ns = g.findNode(s); auto* nt = g.findNode(t); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0));
    g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    // Step to t = 0.25 s. sin(2π·1·0.25) = 1, K=2 → y = 2.
    float before = runUntil(br, k, 0.25);
    EXPECT_NEAR(before, 2.0, kTolLoose);

    // Live-tune K to 5.
    EXPECT_TRUE(br.sendParameter(t, /*paramIdx=*/0, 5.0));
    br.step(1.0f / 60.0f);
    float after = lastSample(br, k);
    double now  = br.time();
    EXPECT_NEAR(after, 5.0 * std::sin(2.0 * M_PI * now), kTolLoose);
}

// ========================================================================
// Scenario 6 — Canonical closed-loop PID + DC motor:
//   Step(50) → Sum(+,-) → PID(Kp=0.5, Ki=2.0) → DCMotor → Scope
//                ↑                                  │
//                └────────── (feedback) ────────────┘
//
//   With Ki driving the integral term, ω → 50 rad/s in steady state.
// ========================================================================
// ========================================================================
// SubGraph encapsulate → flatten → simulate test.
//
// Construye el lazo cerrado PID + DCMotor de [6], encapsula los nodos
// intermedios (Sum + PID + Motor) en un SubGraph, y vuelve a simular.
// El resultado en el Oscilloscope debe ser indistinguible del baseline:
// el flatten en `ScilabCodeGen::generate()` re-aplana el grafo, así que
// si la simulación coincide entonces toda la cadena
// (`encapsulateByIds` + flatten + codegen) está validada.
// ========================================================================
static void scenario_subgraph_flatten_pid_motor() {
    std::cout << "[32] SubGraph  encapsulate(Sum+PID+Motor) → flatten → run\n";
    NodeGraph g;
    int setpt = g.addNode(NodeType::StepSignal);
    int sum   = g.addNode(NodeType::Summation);
    int pid   = g.addNode(NodeType::PIDController);
    int motor = g.addNode(NodeType::DCMotorModel);
    int scope = g.addNode(NodeType::Oscilloscope);
    g.setParam(setpt, "Amplitude", 50.0);
    g.setParam(sum,   "Sign2",     -1.0);
    g.setParam(pid,   "Kp",        0.5);
    g.setParam(pid,   "Ki",        2.0);

    auto* nset = g.findNode(setpt);
    auto* nsm  = g.findNode(sum);
    auto* np   = g.findNode(pid);
    auto* nm   = g.findNode(motor);
    auto* nk   = g.findNode(scope);
    g.tryAddEdge(nset->outputAttrId(), nsm->inputAttrId(0));
    g.tryAddEdge(nsm->outputAttrId(),  np->inputAttrId(0));
    g.tryAddEdge(np->outputAttrId(),   nm->inputAttrId(0));
    g.tryAddEdge(nm->outputAttrId(),   nk->inputAttrId(0));
    g.tryAddEdge(nm->outputAttrId(),   nsm->inputAttrId(1));   // feedback

    // Encapsular Sum + PID + Motor en un SubGraph.  Las aristas externas
    // deberían ser:  Step → SG:in0,  SG:out0 → Scope:in0,  SG:out0 → Sum:in1
    // (feedback que vuelve al propio SubGraph).
    int sgId = g.encapsulateByIds({ sum, pid, motor }).sgId;
    EXPECT_TRUE(sgId > 0);
    const NodeInstance* sgInst = g.findNode(sgId);
    EXPECT_TRUE(sgInst != nullptr);
    EXPECT_TRUE(sgInst->subGraphInputCount  >= 1);
    EXPECT_TRUE(sgInst->subGraphOutputCount >= 1);

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    float final = runUntil(br, scope, 5.0);
    EXPECT_NEAR(final, 50.0, 2.5);   // mismo tolerance que el baseline
}

// ========================================================================
// Replica del walkthrough_E3b.scn: lazo Ogata Ex.8-1 Sys2 + saturación
// + perturbación + anti-windup, y encapsulamos **todo menos** StepSignal
// y Oscilloscope. Verifica que la simulación tras el flatten reproduzca
// el comportamiento esperado (sat + anti-windup → y∞ ≈ 1.17).
// ========================================================================
static void scenario_subgraph_e3b_full_loop() {
    std::cout << "[33] SubGraph  E3b full-loop encapsulado (todos menos Step+Scope)\n";
    NodeGraph g;
    int step    = g.addNode(NodeType::StepSignal);
    int sum     = g.addNode(NodeType::Summation);
    int pid     = g.addNode(NodeType::PIDController);
    int sat     = g.addNode(NodeType::Saturation);
    int sumDist = g.addNode(NodeType::Summation);
    int integ   = g.addNode(NodeType::Integrator);
    int tf2     = g.addNode(NodeType::TransferFunction2);
    int gain    = g.addNode(NodeType::Gain);
    int stepD   = g.addNode(NodeType::StepSignal);
    int scope   = g.addNode(NodeType::Oscilloscope);

    g.setParam(step,    "Amplitude",        1.0);
    g.setParam(sum,     "Sign2",           -1.0);
    g.setParam(pid,     "Kp",              39.42);
    g.setParam(pid,     "Ki",              12.8112);
    g.setParam(pid,     "Kd",              30.3219);
    g.setParam(pid,     "N (filter)",      100.0);
    g.setParam(pid,     "Kt (anti-windup)", 0.3250);
    g.setParam(sat,     "Min",             -5.0);
    g.setParam(sat,     "Max",              5.0);
    g.setParam(stepD,   "Amplitude",        5.0);
    g.setParam(stepD,   "Step Time",        6.0);
    g.setParam(tf2,     "num[0]",           1.0);
    g.setParam(tf2,     "num[1]",           0.0);
    g.setParam(tf2,     "den[0]",           5.0);
    g.setParam(tf2,     "den[1]",           6.0);

    auto out = [&](int id, int p = 0) { return g.findNode(id)->outputAttrId(p); };
    auto in  = [&](int id, int p = 0) { return g.findNode(id)->inputAttrId(p); };
    g.tryAddEdge(out(step),    in(sum,     0));
    g.tryAddEdge(out(sum),     in(pid,     0));
    g.tryAddEdge(out(pid),     in(sat,     0));
    g.tryAddEdge(out(sat),     in(sumDist, 0));
    g.tryAddEdge(out(sat),     in(pid,     1));   // anti-windup feedback
    g.tryAddEdge(out(stepD),   in(sumDist, 1));
    g.tryAddEdge(out(sumDist), in(integ,   0));
    g.tryAddEdge(out(integ),   in(tf2,     0));
    g.tryAddEdge(out(tf2),     in(scope,   0));
    g.tryAddEdge(out(tf2),     in(gain,    0));
    g.tryAddEdge(out(gain),    in(sum,     1));   // setpoint feedback

    // Selección = todos menos Step y Scope.
    int sgId = g.encapsulateByIds({ sum, pid, sat, sumDist, integ, tf2, gain, stepD }).sgId;
    EXPECT_TRUE(sgId > 0);
    const NodeInstance* sgInst = g.findNode(sgId);
    EXPECT_TRUE(sgInst != nullptr);
    EXPECT_TRUE(sgInst->subGraphInputCount  == 1);
    EXPECT_TRUE(sgInst->subGraphOutputCount == 1);

    // El grafo padre debe quedar con 3 nodos (Step, SubGraph, Scope) y 2 aristas.
    EXPECT_TRUE(g.nodeCount() == 3);
    EXPECT_TRUE(g.edgeCount() == 2);

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    // El sistema con sat + anti-windup converge cerca de 1.16-1.18 al final
    // del horizonte de 14 s (referencia stage_E3_compare.sce caso C).
    float final = runUntil(br, scope, 14.0);
    EXPECT_NEAR(final, 1.17, kTolQualitative);
}

// ========================================================================
// SubGraph con múltiples puertos externos (2 in, 1 out).  Demuestra que
// el conteo dinámico de inputPorts/outputPorts y el cableo externo
// funcionan cuando la selección atraviesa la frontera por varios sitios.
//
//   Step(2.0) ──┐
//                Σ(+,+) ── Gain(0.5) ── Scope
//   Step(3.0) ──┘
//
// Encapsulamos { Σ, Gain }.  El SubGraph queda con 2 in (de cada Step)
// y 1 out (al Scope).  Esperado: scope=(2+3)*0.5 = 2.5.
// ========================================================================
static void scenario_subgraph_multi_port() {
    std::cout << "[34] SubGraph  multi-port (2 in, 1 out)\n";
    NodeGraph g;
    int s1   = g.addNode(NodeType::StepSignal);
    int s2   = g.addNode(NodeType::StepSignal);
    int sum  = g.addNode(NodeType::Summation);
    int gain = g.addNode(NodeType::Gain);
    int scp  = g.addNode(NodeType::Oscilloscope);
    g.setParam(s1,   "Amplitude", 2.0);
    g.setParam(s2,   "Amplitude", 3.0);
    g.setParam(sum,  "Sign1",     1.0);
    g.setParam(sum,  "Sign2",     1.0);
    g.setParam(gain, "K",         0.5);
    auto out = [&](int id) { return g.findNode(id)->outputAttrId(0); };
    auto in  = [&](int id, int p = 0) { return g.findNode(id)->inputAttrId(p); };
    g.tryAddEdge(out(s1),  in(sum, 0));
    g.tryAddEdge(out(s2),  in(sum, 1));
    g.tryAddEdge(out(sum), in(gain));
    g.tryAddEdge(out(gain), in(scp));

    auto res = g.encapsulateByIds({ sum, gain });
    EXPECT_TRUE(res.sgId > 0);
    const NodeInstance* sgInst = g.findNode(res.sgId);
    EXPECT_TRUE(sgInst->subGraphInputCount  == 2);
    EXPECT_TRUE(sgInst->subGraphOutputCount == 1);
    EXPECT_TRUE(g.nodeCount() == 4);   // s1, s2, sg, scope
    EXPECT_TRUE(g.edgeCount() == 3);   // 2 in + 1 out al scope

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    float final = runUntil(br, scp, 0.5);
    EXPECT_NEAR(final, 2.5, kTolLoose);
}

// ========================================================================
// SubGraph save / load roundtrip: serializa un grafo con SubGraph anidado,
// lo recarga, y verifica que la simulación produce el mismo resultado.
// ========================================================================
static void scenario_subgraph_roundtrip_scn() {
    std::cout << "[35] SubGraph .scn roundtrip (save → load → run)\n";

    // Construir el mismo grafo que [32]: Step→Sum→PID→DCMotor→Scope con
    // {Sum,PID,Motor} encapsulado.
    NodeGraph g;
    int setpt = g.addNode(NodeType::StepSignal);
    int sum   = g.addNode(NodeType::Summation);
    int pid   = g.addNode(NodeType::PIDController);
    int motor = g.addNode(NodeType::DCMotorModel);
    int scope = g.addNode(NodeType::Oscilloscope);
    g.setParam(setpt, "Amplitude", 50.0);
    g.setParam(sum,   "Sign2",     -1.0);
    g.setParam(pid,   "Kp",         0.5);
    g.setParam(pid,   "Ki",         2.0);
    auto N = [&](int id) -> const NodeInstance* { return g.findNode(id); };
    g.tryAddEdge(N(setpt)->outputAttrId(), N(sum)->inputAttrId(0));
    g.tryAddEdge(N(sum)->outputAttrId(),   N(pid)->inputAttrId(0));
    g.tryAddEdge(N(pid)->outputAttrId(),   N(motor)->inputAttrId(0));
    g.tryAddEdge(N(motor)->outputAttrId(), N(scope)->inputAttrId(0));
    g.tryAddEdge(N(motor)->outputAttrId(), N(sum)->inputAttrId(1));

    int sgId = g.encapsulateByIds({ sum, pid, motor }).sgId;
    EXPECT_TRUE(sgId > 0);
    // Renombrar el SubGraph para verificar que stringParams también persiste.
    g.setStringParam(sgId, "Name", "Controlador PI");

    // Serializar.
    ScnPositions emptyPos;
    std::string jsonText = ScnSerializer::serialize(g, emptyPos);
    EXPECT_TRUE(jsonText.find("\"subgraph\"")    != std::string::npos);
    EXPECT_TRUE(jsonText.find("Controlador PI") != std::string::npos);

    // Deserializar a un grafo fresco.
    NodeGraph g2;
    ScnPositions pos2;
    auto rep = ScnSerializer::deserialize(jsonText, g2, pos2);
    EXPECT_TRUE(rep.ok);
    EXPECT_TRUE(rep.rejectedEdges.empty());
    EXPECT_TRUE(g2.nodeCount() == 3);    // Step, SubGraph, Scope
    EXPECT_TRUE(g2.edgeCount() == 2);    // Step→SG, SG→Scope (feedback interno)

    // El SubGraph debe tener el grafo hijo poblado y el Name restaurado.
    int sgId2 = 0;
    for (const auto& nn : g2.nodes())
        if (nn.type == NodeType::SubGraph) sgId2 = nn.id;
    EXPECT_TRUE(sgId2 > 0);
    const NodeInstance* sgInst2 = g2.findNode(sgId2);
    EXPECT_TRUE(sgInst2->stringParams.at("Name") == "Controlador PI");
    EXPECT_TRUE(g2.subGraphOf(sgId2) != nullptr);
    EXPECT_TRUE(g2.subGraphOf(sgId2)->nodeCount() >= 3);

    // Simulación debe seguir convergiendo a 50.
    int scopeId2 = 0;
    for (const auto& nn : g2.nodes())
        if (nn.type == NodeType::Oscilloscope) scopeId2 = nn.id;
    EXPECT_TRUE(scopeId2 > 0);

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g2));
    float final = runUntil(br, scopeId2, 5.0);
    EXPECT_NEAR(final, 50.0, 2.5);
}

// ========================================================================
// Deep-clone semantics of NodeGraph: copiar un grafo con SubGraph debe
// duplicar el contenido del child.  Es lo que sostiene el copy/paste
// del NodeCanvas y el undo/redo cuando hay subgrafos involucrados.
// ========================================================================
static void scenario_subgraph_clone_deep() {
    std::cout << "[36] SubGraph deep-clone (copy con contenido del hijo)\n";

    // Construir el mismo lazo de [32] y encapsular {Sum, PID, Motor}.
    NodeGraph g;
    int setpt = g.addNode(NodeType::StepSignal);
    int sum   = g.addNode(NodeType::Summation);
    int pid   = g.addNode(NodeType::PIDController);
    int motor = g.addNode(NodeType::DCMotorModel);
    int scope = g.addNode(NodeType::Oscilloscope);
    g.setParam(setpt, "Amplitude", 50.0);
    g.setParam(sum,   "Sign2",     -1.0);
    g.setParam(pid,   "Kp",         0.5);
    g.setParam(pid,   "Ki",         2.0);
    auto N = [&](int id) -> const NodeInstance* { return g.findNode(id); };
    g.tryAddEdge(N(setpt)->outputAttrId(), N(sum)->inputAttrId(0));
    g.tryAddEdge(N(sum)->outputAttrId(),   N(pid)->inputAttrId(0));
    g.tryAddEdge(N(pid)->outputAttrId(),   N(motor)->inputAttrId(0));
    g.tryAddEdge(N(motor)->outputAttrId(), N(scope)->inputAttrId(0));
    g.tryAddEdge(N(motor)->outputAttrId(), N(sum)->inputAttrId(1));
    int sgId = g.encapsulateByIds({ sum, pid, motor }).sgId;
    EXPECT_TRUE(sgId > 0);

    // Capturar contenido del child antes de clonar.
    const NodeGraph* childOriginal = g.subGraphOf(sgId);
    EXPECT_TRUE(childOriginal != nullptr);
    const int origNodeCount = childOriginal->nodeCount();

    // Clonar el grafo completo (simula copy/paste vía operator=).
    NodeGraph g2 = g;

    // El clon debe tener su propio child con el mismo contenido.
    const NodeGraph* childClone = g2.subGraphOf(sgId);
    EXPECT_TRUE(childClone != nullptr);
    EXPECT_TRUE(childClone->nodeCount() == origNodeCount);
    EXPECT_TRUE(childClone != childOriginal);  // pointer distinto = deep copy

    // Mutar el child del clon NO debe afectar al original.
    auto* mutClone = g2.subGraphOf(sgId);
    mutClone->addNode(NodeType::Gain);
    EXPECT_TRUE(g2.subGraphOf(sgId)->nodeCount() == origNodeCount + 1);
    EXPECT_TRUE(g.subGraphOf(sgId)->nodeCount()  == origNodeCount);

    // Ambos siguen simulando independientemente.
    int scopeOrig = 0, scopeClone = 0;
    for (const auto& nn : g.nodes())
        if (nn.type == NodeType::Oscilloscope) scopeOrig = nn.id;
    for (const auto& nn : g2.nodes())
        if (nn.type == NodeType::Oscilloscope) scopeClone = nn.id;
    EXPECT_TRUE(scopeOrig > 0);
    EXPECT_TRUE(scopeClone > 0);

    ScilabBridge br1, br2;
    EXPECT_TRUE(br1.reset(g));
    EXPECT_TRUE(br2.reset(g2));
    float finalOrig  = runUntil(br1, scopeOrig,  5.0);
    float finalClone = runUntil(br2, scopeClone, 5.0);
    EXPECT_NEAR(finalOrig,  50.0, 2.5);
    EXPECT_NEAR(finalClone, 50.0, 2.5);
}

// ========================================================================
// idForPath validation: tras encapsular Sum+PID+Motor, el `GeneratedPlan`
// debe exponer el mapeo (path) → flatId para que el live-tuning de
// parámetros dentro del SubGraph llegue a la variable correcta del
// script Scilab.
// ========================================================================
static void scenario_subgraph_id_for_path() {
    std::cout << "[37] SubGraph idForPath (live-tuning path -> flatId)\n";

    NodeGraph g;
    int setpt = g.addNode(NodeType::StepSignal);
    int sum   = g.addNode(NodeType::Summation);
    int pid   = g.addNode(NodeType::PIDController);
    int motor = g.addNode(NodeType::DCMotorModel);
    int scope = g.addNode(NodeType::Oscilloscope);
    auto N = [&](int id) -> const NodeInstance* { return g.findNode(id); };
    g.tryAddEdge(N(setpt)->outputAttrId(), N(sum)->inputAttrId(0));
    g.tryAddEdge(N(sum)->outputAttrId(),   N(pid)->inputAttrId(0));
    g.tryAddEdge(N(pid)->outputAttrId(),   N(motor)->inputAttrId(0));
    g.tryAddEdge(N(motor)->outputAttrId(), N(scope)->inputAttrId(0));
    g.tryAddEdge(N(motor)->outputAttrId(), N(sum)->inputAttrId(1));

    int sgId = g.encapsulateByIds({ sum, pid, motor }).sgId;
    EXPECT_TRUE(sgId > 0);

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());

    // Top-level node (StepSignal): path = {setpt} ⇒ flatId = setpt
    auto it1 = plan.idForPath.find({ setpt });
    EXPECT_TRUE(it1 != plan.idForPath.end());
    EXPECT_TRUE(it1->second == setpt);

    // Nodos dentro del SubGraph: cada nodo del grafo hijo aparece como
    // path = {sgId, child_id}.  Verificamos que para CADA nodo del child
    // (excluyendo port stubs) exista una entrada con esa forma.
    const NodeGraph* child = g.subGraphOf(sgId);
    EXPECT_TRUE(child != nullptr);
    int found = 0, expected = 0;
    for (const NodeInstance& cn : child->nodes()) {
        if (cn.type == NodeType::SubGraphInput ||
            cn.type == NodeType::SubGraphOutput) continue;
        ++expected;
        auto it = plan.idForPath.find({ sgId, cn.id });
        if (it != plan.idForPath.end()) ++found;
    }
    EXPECT_TRUE(expected == 3);     // Sum + PID + Motor
    EXPECT_TRUE(found    == expected);

    // No queda ninguna entrada con path = {sgId} (los SubGraphs no se
    // ven a sí mismos como destinos de live-tuning — sus parámetros son
    // de los nodos internos).
    EXPECT_TRUE(plan.idForPath.find({ sgId }) == plan.idForPath.end());
}

static void scenario_closed_loop_pid_motor() {
    std::cout << "[6] CLOSED LOOP  Step(50) → Sum → PID → DCMotor → Scope ↺\n";
    NodeGraph g;
    int setpt = g.addNode(NodeType::StepSignal);
    int sum   = g.addNode(NodeType::Summation);
    int pid   = g.addNode(NodeType::PIDController);
    int motor = g.addNode(NodeType::DCMotorModel);
    int scope = g.addNode(NodeType::Oscilloscope);
    g.setParam(setpt, "Amplitude", 50.0);
    g.setParam(sum,   "Sign2",     -1.0);
    g.setParam(pid,   "Kp",        0.5);
    g.setParam(pid,   "Ki",        2.0);

    auto* nset = g.findNode(setpt);
    auto* nsm  = g.findNode(sum);
    auto* np   = g.findNode(pid);
    auto* nm   = g.findNode(motor);
    auto* nk   = g.findNode(scope);
    g.tryAddEdge(nset->outputAttrId(), nsm->inputAttrId(0));
    g.tryAddEdge(nsm->outputAttrId(),  np->inputAttrId(0));
    g.tryAddEdge(np->outputAttrId(),   nm->inputAttrId(0));
    g.tryAddEdge(nm->outputAttrId(),   nk->inputAttrId(0));
    g.tryAddEdge(nm->outputAttrId(),   nsm->inputAttrId(1));   // feedback

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    // After 5 s the PI controller has driven the motor to the setpoint.
    float final = runUntil(br, scope, 5.0);
    EXPECT_NEAR(final, 50.0, 2.5);   // 5% tolerance
}

// ========================================================================
// Scenario 9 — Real 2-link planar Inverse Kinematics (multi-output):
//   CurrentSource(x), CurrentSource(y) → IK(L1=0.3, L2=0.2)
//                                         → port 0 (θ₁) → Scope1
//                                         → port 1 (θ₂) → Scope2
//
// Verifies the multi-output architecture end-to-end through Scilab.
//
//   target (0.5, 0)    →  θ₁ = 0,    θ₂ = 0     (arm extended)
//   target (0.3, 0.2)  →  θ₁ = 0,    θ₂ = π/2   (elbow at right angle)
// ========================================================================
static void scenario_inverse_kinematics() {
    std::cout << "[9] Real 2-link IK  (x, y) → IK → (θ₁, θ₂) → 2× Scope\n";

    auto runCase = [](double tx, double ty,
                      double expectedT1, double expectedT2) {
        NodeGraph g;
        int sx = g.addNode(NodeType::CurrentSource);
        int sy = g.addNode(NodeType::CurrentSource);
        int ik = g.addNode(NodeType::InverseKinematics);
        int k1 = g.addNode(NodeType::Oscilloscope);
        int k2 = g.addNode(NodeType::Oscilloscope);
        g.setParam(sx, "Current", tx);
        g.setParam(sy, "Current", ty);
        auto* nx = g.findNode(sx);  auto* ny = g.findNode(sy);
        auto* ni = g.findNode(ik);
        auto* nk1 = g.findNode(k1); auto* nk2 = g.findNode(k2);
        g.tryAddEdge(nx->outputAttrId(),  ni->inputAttrId(0));
        g.tryAddEdge(ny->outputAttrId(),  ni->inputAttrId(1));
        g.tryAddEdge(ni->outputAttrId(0), nk1->inputAttrId(0));
        g.tryAddEdge(ni->outputAttrId(1), nk2->inputAttrId(0));

        ScilabBridge br;
        EXPECT_TRUE(br.reset(g));
        runUntil(br, k1, 0.1);
        EXPECT_NEAR(lastSample(br, k1), expectedT1, kTolTight);
        EXPECT_NEAR(lastSample(br, k2), expectedT2, kTolTight);
    };

    runCase(0.5, 0.0,      0.0,     0.0);             // arm fully extended
    runCase(0.3, 0.2,      0.0,     M_PI / 2.0);      // elbow at 90°
}

// ========================================================================
// Scenario 10 — TransferFunction2 (undamped oscillator):
//   Step(1) → TF2(b1=0, b0=1, a1=0, a0=1) → Scope
//   H(s) = 1 / (s² + 1)  →  y(t) = 1 − cos(t)
//
//   y(π/2) = 1.0       (cos = 0)
//   y(π)   = 2.0       (cos = −1, overshoot peak)
// ========================================================================
static void scenario_transfer_function_2nd_order() {
    std::cout << "[10] TF2  Step(1) → 1/(s²+1) → Scope (undamped osc.)\n";
    NodeGraph g;
    int s  = g.addNode(NodeType::StepSignal);
    int tf = g.addNode(NodeType::TransferFunction2);
    int k  = g.addNode(NodeType::Oscilloscope);
    // H(s) = 1 / (s² + 1): num = [1, 0], den (monic) = [1, 0]
    g.setParam(tf, "num[0]", 1.0); g.setParam(tf, "num[1]", 0.0);
    g.setParam(tf, "den[0]", 1.0); g.setParam(tf, "den[1]", 0.0);
    auto* ns = g.findNode(s); auto* nt = g.findNode(tf); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0));
    g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    runUntil(br, k, M_PI / 2.0);
    double t = br.time();
    EXPECT_NEAR(lastSample(br, k), 1.0 - std::cos(t), kTolMedium);
    runUntil(br, k, M_PI);
    t = br.time();
    EXPECT_NEAR(lastSample(br, k), 1.0 - std::cos(t), kTolMedium);
}

// ========================================================================
// Scenario 8 — TransferFunction:
//   Step(1) → TF(b=1, a0=1, a1=1) → Scope
//   H(s) = 1/(s+1) → y(t) = 1 − e^(−t)
// ========================================================================
static void scenario_transfer_function() {
    std::cout << "[8] TransferFunction  Step(1) → 1/(s+1) → Scope\n";
    NodeGraph g;
    int s = g.addNode(NodeType::StepSignal);
    int tf = g.addNode(NodeType::TransferFunction);
    int k = g.addNode(NodeType::Oscilloscope);
    g.setParam(tf, "num[0]", 1.0);
    g.setParam(tf, "den[0]", 1.0);
    g.setParam(tf, "den[1]", 1.0);
    auto* ns = g.findNode(s); auto* nt = g.findNode(tf); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0));
    g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    runUntil(br, k, 1.0);
    double t = br.time();
    EXPECT_NEAR(lastSample(br, k), 1.0 - std::exp(-t), kTolTight);
    runUntil(br, k, 3.0);
    t = br.time();
    EXPECT_NEAR(lastSample(br, k), 1.0 - std::exp(-t), kTolTight);
}

// ========================================================================
// Scenario 7 — Differentiator on a ramp:
//   Ramp(slope=2) → Differentiator(fc=100 Hz) → Scope
//   For slow inputs the filtered derivative tracks du/dt = 2.
//   Filter τ = 1/(2π·100) ≈ 1.6 ms; after 0.5 s we are well past steady state.
// ========================================================================
static void scenario_differentiator() {
    std::cout << "[7] Differentiator  Ramp(slope=2) → Differentiator → Scope\n";
    NodeGraph g;
    int r = g.addNode(NodeType::RampSignal);
    int d = g.addNode(NodeType::Differentiator);
    int k = g.addNode(NodeType::Oscilloscope);
    g.setParam(r, "Slope", 2.0);
    auto* nr = g.findNode(r); auto* nd = g.findNode(d); auto* nk = g.findNode(k);
    g.tryAddEdge(nr->outputAttrId(), nd->inputAttrId(0));
    g.tryAddEdge(nd->outputAttrId(), nk->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    EXPECT_NEAR(runUntil(br, k, 0.5), 2.0, kTolLoose);
}

// ========================================================================
// Scenario 12 — NaN detection identifies the offending node:
//   Step(1) → TF(num=1, den[0]=-1000, den[1]=1) → Scope
//   The TF has a right-half-plane pole at s=+1000, so x(t) grows as
//   (e^(1000·t) − 1)/1000 and crosses the double-precision overflow
//   threshold in well under a second of simulated time. The script's
//   nanid guard catches v_TF = Inf and the bridge reports the TF id.
// ========================================================================
static void scenario_nan_detection() {
    std::cout << "[12] NaN highlight  unstable TF diverges → bridge reports TF id\n";
    NodeGraph g;
    int s  = g.addNode(NodeType::StepSignal);
    int tf = g.addNode(NodeType::TransferFunction);
    int k  = g.addNode(NodeType::Oscilloscope);
    g.setParam(tf, "num[0]",  1.0);
    g.setParam(tf, "den[0]", -1000.0);    // right-half-plane pole at +1000
    g.setParam(tf, "den[1]",  1.0);
    auto* ns = g.findNode(s); auto* nt = g.findNode(tf); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0));
    g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    // Step until divergence; should happen well within a few hundred ticks
    // (e^(1000·1) ≈ 10^434 ≫ 1.8e308).
    bool ok = true;
    for (int i = 0; i < 300 && ok; ++i)
        ok = br.step(1.0f / 60.0f);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(br.status() == ScilabBridge::Status::Error);
    EXPECT_TRUE(br.offendingNodeId() == tf);
}

// ========================================================================
// Scenario 13 — FFT through the full pipeline:
//   Sine(f) → FFTAnalyzer(bin=64) → Scope
//   The bridge captures the sine via Scilab at dt = 1/60 s. We run for
//   exactly N = 64 steps so the most-recent window in the ring buffer is
//   a contiguous N-sample slice. Choosing f = 60·k/N with k=4 yields a
//   bin-aligned sinusoid and the FFT peak lands cleanly at bin k.
// ========================================================================
static void scenario_fft_pipeline() {
    std::cout << "[13] FFT pipeline   Sine -> FFTAnalyzer (peak bin matches)\n";
    constexpr int kN = 64;
    constexpr int kBin = 4;
    constexpr float kFs = 60.0f;
    constexpr float kFreq = kFs * kBin / kN;        // = 3.75 Hz

    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int f = g.addNode(NodeType::FFTAnalyzer);
    g.setParam(s, "Frequency", kFreq);
    g.setParam(f, "Bin Count", (double)kN);
    auto* ns = g.findNode(s); auto* nf = g.findNode(f);
    g.tryAddEdge(ns->outputAttrId(), nf->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    for (int i = 0; i < kN; ++i)
        EXPECT_TRUE(br.step(1.0f / kFs));

    // Pull the last N samples directly and FFT them — same window the
    // PlotPanel would render.
    auto buf = br.buffer(f);
    std::vector<float> win(kN);
    const int srcStart = static_cast<int>(buf.size()) - kN;
    for (int i = 0; i < kN; ++i)
        win[i] = buf[srcStart + i];

    auto mag = scinodes::magnitudeSpectrum(win.data(), kN);
    int peak = (int)(std::max_element(mag.begin() + 1, mag.end()) - mag.begin());
    EXPECT_TRUE(peak == kBin);
}

// ========================================================================
// Scenario 14 — PhasePortrait captures both inputs as separate channels:
//   Sine(1 Hz)        → PhasePortrait.in1 (x-axis)
//   Sine(1 Hz, phase=π/2) = cos → PhasePortrait.in2 (y-axis)
//   After many ticks the channels store x(t) and y(t) independently and
//   the (x, y) trajectory traces a circle.
// ========================================================================
static void scenario_phase_portrait() {
    std::cout << "[14] Phase portrait  2 sines → PhasePortrait (channels 0+1)\n";
    NodeGraph g;
    int sx = g.addNode(NodeType::SineSignal);
    int sy = g.addNode(NodeType::SineSignal);
    int pp = g.addNode(NodeType::PhasePortrait);
    g.setParam(sx, "Frequency", 1.0);  g.setParam(sx, "Phase", 0.0);
    g.setParam(sy, "Frequency", 1.0);  g.setParam(sy, "Phase", M_PI / 2.0);
    auto* nx = g.findNode(sx); auto* ny = g.findNode(sy); auto* np = g.findNode(pp);
    g.tryAddEdge(nx->outputAttrId(), np->inputAttrId(0));
    g.tryAddEdge(ny->outputAttrId(), np->inputAttrId(1));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    EXPECT_TRUE(br.channelCount(pp) == 2);
    for (int i = 0; i < 60; ++i) EXPECT_TRUE(br.step(1.0f / 60.0f));

    // After 1 second t=1 → sin(2π)=0, cos(2π)=1.
    int wi0 = br.writeIndex(pp, 0);
    int wi1 = br.writeIndex(pp, 1);
    float x = br.buffer(pp, 0).back();
    float y = br.buffer(pp, 1).back();
    EXPECT_NEAR(x, 0.0, kTolMedium);
    EXPECT_NEAR(y, 1.0, kTolMedium);
}

// ========================================================================
// Scenario 15 — View3DSink populates its ring buffer:
//   Sine(1 Hz) → View3DSink. Confirms the new sink type behaves like a
//   single-channel oscilloscope for the View3DPanel to read.
// ========================================================================
static void scenario_view3d_sink() {
    std::cout << "[15] View3DSink     Sine -> View3DSink populates 1 channel\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int v = g.addNode(NodeType::View3DSink);
    g.setParam(s, "Frequency", 1.0);
    auto* ns = g.findNode(s); auto* nv = g.findNode(v);
    g.tryAddEdge(ns->outputAttrId(), nv->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    EXPECT_TRUE(br.channelCount(v) == 1);
    for (int i = 0; i < 60; ++i) EXPECT_TRUE(br.step(1.0f / 60.0f));

    int wi = br.writeIndex(v);
    float last = br.buffer(v).back();
    // After 1 simulated second the sine should be ≈ sin(2π) = 0.
    EXPECT_NEAR(last, 0.0, kTolMedium);
}

// ========================================================================
// Scenario 11 — Dedicated solver thread:
//   Sine(1 Hz) → Gain(K=2) → Scope, run by ScilabBridge's background
//   thread for ~0.5 s wall time. Verify the buffer is populated and the
//   solver thread cleanly stops on signal.
// ========================================================================
static void scenario_solver_thread() {
    std::cout << "[11] Solver thread  ScilabBridge background loop\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int t = g.addNode(NodeType::Gain);
    int k = g.addNode(NodeType::Oscilloscope);
    g.setParam(t, "K", 2.0);
    auto* ns = g.findNode(s); auto* nt = g.findNode(t); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0));
    g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    EXPECT_TRUE(br.startSolverThread(1.0f / 60.0f));
    EXPECT_TRUE(br.isThreadRunning());

    // Let the thread run real-time for ~0.5 s, then stop.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_TRUE(br.writeIndex(k) > 5);                // many samples written
    EXPECT_TRUE(br.time() > 0.3f);                    // sim time advanced

    // Live-tune the gain mid-flight — must not deadlock or race.
    EXPECT_TRUE(br.sendParameter(t, /*paramIdx=*/0, 5.0));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    br.stopSolverThread();
    EXPECT_TRUE(!br.isThreadRunning());

    // Buffer accessor still works after thread stop.
    auto snap = br.buffer(k);
    EXPECT_TRUE(!snap.empty());
}

// ========================================================================
// ========================================================================
// Scenario 16 — Stage v0.7 acceptance test:
// Canonical closed-loop PID + DC motor model, run for 10 s, ω(t) must
// match the setpoint within 1% in steady state.
//
// The planner promises "ω(t) matches Scilab reference within 1% tolerance".
// Since the integration tests already drive a real scilab-cli subprocess,
// the bridge result *is* the Scilab reference — what we have to verify is
// that the canonical model converges and stays converged. We sample at
// t ≈ 8, 9, 10 s and require all three to be within 1% of the 50 rad/s
// setpoint. Sampling several late points catches both undershoot and
// limit-cycle oscillation that a single end-of-run check would miss.
// ========================================================================
static void scenario_closed_loop_pid_motor_10s() {
    std::cout << "[16] STAGE v0.7  10 s closed-loop PID + DC motor (1% tol)\n";
    NodeGraph g;
    int setpt = g.addNode(NodeType::StepSignal);
    int sum   = g.addNode(NodeType::Summation);
    int pid   = g.addNode(NodeType::PIDController);
    int motor = g.addNode(NodeType::DCMotorModel);
    int scope = g.addNode(NodeType::Oscilloscope);
    g.setParam(setpt, "Amplitude", 50.0);
    g.setParam(sum,   "Sign2",     -1.0);
    g.setParam(pid,   "Kp",        0.5);
    g.setParam(pid,   "Ki",        2.0);

    auto* nset = g.findNode(setpt);
    auto* nsm  = g.findNode(sum);
    auto* np   = g.findNode(pid);
    auto* nm   = g.findNode(motor);
    auto* nk   = g.findNode(scope);
    g.tryAddEdge(nset->outputAttrId(), nsm->inputAttrId(0));
    g.tryAddEdge(nsm->outputAttrId(),  np->inputAttrId(0));
    g.tryAddEdge(np->outputAttrId(),   nm->inputAttrId(0));
    g.tryAddEdge(nm->outputAttrId(),   nk->inputAttrId(0));
    g.tryAddEdge(nm->outputAttrId(),   nsm->inputAttrId(1));   // feedback

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));

    const double setpoint = 50.0;
    const double tol_1pct = 0.5;        // 1% of 50
    const float  dt       = 1.0f / 60.0f;

    // Settle phase — drive to ≈ 8 s.
    (void)runUntil(br, scope, 8.0, dt);
    float w_at_8 = lastSample(br, scope);
    EXPECT_NEAR(w_at_8, setpoint, tol_1pct);

    // Continue to ≈ 9 s and re-check.
    (void)runUntil(br, scope, 9.0, dt);
    float w_at_9 = lastSample(br, scope);
    EXPECT_NEAR(w_at_9, setpoint, tol_1pct);

    // Final check at ≈ 10 s.
    (void)runUntil(br, scope, 10.0, dt);
    float w_at_10 = lastSample(br, scope);
    EXPECT_NEAR(w_at_10, setpoint, tol_1pct);

    // No solver divergence anywhere along the trajectory.
    EXPECT_TRUE(br.offendingNodeId() == 0);
    EXPECT_TRUE(br.status() == ScilabBridge::Status::Ready);
}

// ========================================================================
// Scenario 17 — Stage v0.7 custom-node end-to-end:
// Load a JSON descriptor for a "Tripler" transformer (output = 3 * p_k * u1),
// build  Step(amp=5) → Tripler(k=2) → Scope, and verify the bridge reports
// 30.0 once the step has fired. Exercises:
//   • CustomNodeRegistry parsing + registration
//   • NodeGraph::addCustomNode
//   • GrammarParser accepting Custom as a Transformer
//   • ScilabCodeGen substituting u1 and p_k in the expression template
//   • Live param tuning of a custom node via sendParameter
// ========================================================================
static void scenario_custom_node_via_json() {
    std::cout << "[17] CUSTOM NODE Step → Custom(\"Tripler\",k=2) → Scope ⇒ 30\n";

    scinodes::CustomNodeRegistry reg;
    scinodes::ScopedCustomNodes installer(reg);   // route customNodes() to this fresh reg
    std::string err;
    const char* descriptor = R"({
        "type_id": "Tripler",
        "label":   "Tripler",
        "description": "Output = 3 * p_k * u1",
        "category": "transformer",
        "input_ports": 1,
        "output_ports": 1,
        "params":  [ { "name": "k", "default": 1.0 } ],
        "expression": "3 * p_k * u1"
    })";
    EXPECT_TRUE(reg.loadFromJsonString(descriptor, &err));
    EXPECT_TRUE(err.empty());

    NodeGraph g;
    int step  = g.addNode(NodeType::StepSignal);
    int tri   = g.addCustomNode("Tripler");
    int scope = g.addNode(NodeType::Oscilloscope);
    g.setParam(step, "Amplitude", 5.0);
    g.setParam(tri,  "k",         2.0);

    auto* ns = g.findNode(step);
    auto* nt = g.findNode(tri);
    auto* nk = g.findNode(scope);
    EXPECT_TRUE(ns && nt && nk);

    // Grammar must accept Source → Custom → Sink.
    auto e1 = g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0));
    auto e2 = g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0));
    EXPECT_FALSE(e1.has_value());
    EXPECT_FALSE(e2.has_value());

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));

    // After the step has fired, output should be 3*k*amp = 3*2*5 = 30.
    float v = runUntil(br, scope, 0.5);
    EXPECT_NEAR(v, 30.0, kTolMedium);

    // Live-tune k: 3*5*5 = 75 once Scilab applies the param.
    EXPECT_TRUE(br.sendParameter(tri, /*paramIdx*/ 0, /*value*/ 5.0));
    v = runUntil(br, scope, 1.0);
    EXPECT_NEAR(v, 75.0, kTolMedium);

    reg.clear();
}

// ========================================================================
// Scenario 18 — Stage v0.7 .sod export:
// Run a brief simulation (Sine → Gain → Scope), tell Scilab to write its
// accumulated history to a temp .sod file, verify the file exists and is
// non-empty. We also check the magic bytes loosely — a valid .sod is
// HDF5, which starts with the 8-byte signature 0x89 'H' 'D' 'F' \r \n
// 0x1a \n.
// ========================================================================
static void scenario_sod_export() {
    std::cout << "[18] .sod EXPORT  Sine → Gain → Scope, save to /tmp/*.sod\n";
    NodeGraph g;
    int src   = g.addNode(NodeType::SineSignal);
    int gain  = g.addNode(NodeType::Gain);
    int scope = g.addNode(NodeType::Oscilloscope);
    g.setParam(src,  "Frequency", 2.0);
    g.setParam(gain, "K",         1.5);

    auto* ns = g.findNode(src);
    auto* nx = g.findNode(gain);
    auto* nk = g.findNode(scope);
    g.tryAddEdge(ns->outputAttrId(), nx->inputAttrId(0));
    g.tryAddEdge(nx->outputAttrId(), nk->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));

    // Run synchronously for ~0.5 s of simulated time so the driver has
    // history to write.
    (void)runUntil(br, scope, 0.5);

    char tmpl[] = "/tmp/scinodes_sod_XXXXXX";
    int fd = ::mkstemp(tmpl);
    EXPECT_TRUE(fd >= 0);
    if (fd >= 0) ::close(fd);
    std::string path = std::string(tmpl) + ".sod";

    // exportSod is synchronous when no solver thread is running.
    EXPECT_TRUE(br.exportSod(path));
    std::string result = br.takeLastExportResult();
    EXPECT_TRUE(result.find("Exported to") == 0);

    // Verify file exists + non-empty + HDF5 magic.
    std::ifstream f(path, std::ios::binary);
    EXPECT_TRUE(f.is_open());
    if (f.is_open()) {
        char hdr[8] = {0};
        f.read(hdr, 8);
        EXPECT_TRUE(f.gcount() == 8);
        EXPECT_TRUE(static_cast<unsigned char>(hdr[0]) == 0x89);
        EXPECT_TRUE(hdr[1] == 'H' && hdr[2] == 'D' && hdr[3] == 'F');
    }
    std::remove(path.c_str());
    std::remove(tmpl);

    // Spaces in the path are explicitly rejected before any pipe write.
    EXPECT_FALSE(br.exportSod("/tmp/has spaces.sod"));
    std::string err = br.takeLastExportResult();
    EXPECT_TRUE(err.find("must not contain spaces") != std::string::npos);
}

// ========================================================================
// Scenario 19 — Stage v0.8 Phase 1 analytical sizing:
// DesignTemplate → PMSMSizing → 3× Scope. Verify the bore diameter, stack
// length and rated power that come back from Scilab match the closed-form
// classical sizing equation to high precision.
//
//   D = ((2 * T) / (pi * B * A * alpha))^(1/3)
//   L = alpha * D
//   P = T * omega
// ========================================================================
static void scenario_pmsm_sizing() {
    std::cout << "[19] STAGE v0.8  DesignTemplate → PMSMSizing → 3× Scope\n";
    NodeGraph g;
    int dt   = g.addNode(NodeType::DesignTemplate);
    int sz   = g.addNode(NodeType::PMSMSizing);
    int sk_D = g.addNode(NodeType::Oscilloscope);
    int sk_L = g.addNode(NodeType::Oscilloscope);
    int sk_P = g.addNode(NodeType::Oscilloscope);

    // Pick nice round numbers for an obvious analytical answer.
    const double T_target  = 100.0;          // Nm
    const double w_target  = 200.0;          // rad/s
    const double B         = 0.85;           // T   — default in PMSMSizing
    const double A         = 40000.0;        // A/m — default
    const double alpha     = 1.2;            // L/D — default
    g.setParam(dt, "Target Torque", T_target);
    g.setParam(dt, "Target Speed",  w_target);

    auto* nd = g.findNode(dt);
    auto* ns = g.findNode(sz);
    g.tryAddEdge(nd->outputAttrId(0), ns->inputAttrId(0));
    g.tryAddEdge(nd->outputAttrId(1), ns->inputAttrId(1));
    g.tryAddEdge(ns->outputAttrId(0), g.findNode(sk_D)->inputAttrId(0));
    g.tryAddEdge(ns->outputAttrId(1), g.findNode(sk_L)->inputAttrId(0));
    g.tryAddEdge(ns->outputAttrId(2), g.findNode(sk_P)->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));

    // The graph is fully stateless — one step is enough to capture the
    // converged answer.
    EXPECT_TRUE(br.step(1.0f / 60.0f));

    float D = lastSample(br, sk_D);
    float L = lastSample(br, sk_L);
    float P = lastSample(br, sk_P);

    double D_expected = std::cbrt(2.0 * T_target /
                                  (M_PI * B * A * alpha));
    double L_expected = alpha * D_expected;
    double P_expected = T_target * w_target;

    EXPECT_NEAR(D, D_expected, kTolTight);   // metres
    EXPECT_NEAR(L, L_expected, kTolTight);
    EXPECT_NEAR(P, P_expected, 1e-1);   // watts
}

// ========================================================================
// Scenario 20 — Stage v0.8 Phase 3 lumped-parameter EM:
// Drive D, L, omega through Step sources into PMSMElectromagnetic.
// Verify Ke, L_phase, V_rms, T_cog all match closed-form predictions.
// ========================================================================
static void scenario_pmsm_electromagnetic() {
    std::cout << "[20] STAGE v0.8  PMSMElectromagnetic Ke / L / Vrms / Tcog\n";

    const double D_val      = 0.10;       // m
    const double L_val      = 0.12;       // m
    const double omega_val  = 100.0;      // rad/s
    const double Nph        = 100.0;
    const double kw         = 0.95;
    const double p          = 4.0;
    const double Bg         = 0.85;
    const double g          = 0.001;
    const double hm         = 0.003;
    const double mu_r       = 1.05;
    const double Nslots     = 24.0;
    const double mu0        = 4.0 * M_PI * 1e-7;

    NodeGraph gph;
    int sD  = gph.addNode(NodeType::StepSignal);
    int sL  = gph.addNode(NodeType::StepSignal);
    int sW  = gph.addNode(NodeType::StepSignal);
    int em  = gph.addNode(NodeType::PMSMElectromagnetic);
    int kKe = gph.addNode(NodeType::Oscilloscope);
    int kLp = gph.addNode(NodeType::Oscilloscope);
    int kV  = gph.addNode(NodeType::Oscilloscope);
    int kTc = gph.addNode(NodeType::Oscilloscope);

    gph.setParam(sD, "Amplitude", D_val);
    gph.setParam(sL, "Amplitude", L_val);
    gph.setParam(sW, "Amplitude", omega_val);

    auto* ne = gph.findNode(em);
    gph.tryAddEdge(gph.findNode(sD)->outputAttrId(), ne->inputAttrId(0));
    gph.tryAddEdge(gph.findNode(sL)->outputAttrId(), ne->inputAttrId(1));
    gph.tryAddEdge(gph.findNode(sW)->outputAttrId(), ne->inputAttrId(2));
    gph.tryAddEdge(ne->outputAttrId(0), gph.findNode(kKe)->inputAttrId(0));
    gph.tryAddEdge(ne->outputAttrId(1), gph.findNode(kLp)->inputAttrId(0));
    gph.tryAddEdge(ne->outputAttrId(2), gph.findNode(kV)->inputAttrId(0));
    gph.tryAddEdge(ne->outputAttrId(3), gph.findNode(kTc)->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(gph));
    EXPECT_TRUE(br.step(1.0f / 60.0f));

    float Ke   = lastSample(br, kKe);
    float Lph  = lastSample(br, kLp);
    float Vrms = lastSample(br, kV);
    float Tcog = lastSample(br, kTc);

    const double Ke_exp   = (kw * Nph * p * Bg * L_val * D_val) / 2.0;
    const double g_eff    = g + hm / mu_r;
    const double Lph_exp  = (mu0 * Nph * Nph * kw * kw * M_PI * D_val * L_val)
                          / (8.0 * p * p * g_eff);
    const double Vrms_exp = Ke_exp * omega_val / std::sqrt(2.0);
    const double Tcog_exp = (Bg * Bg * D_val * D_val * L_val)
                          / (8.0 * mu0 * Nslots);

    EXPECT_NEAR(Ke,   Ke_exp,   1e-4);
    EXPECT_NEAR(Lph,  Lph_exp,  1e-7);
    EXPECT_NEAR(Vrms, Vrms_exp, kTolLoose);
    EXPECT_NEAR(Tcog, Tcog_exp, 1e-1);
}

// ========================================================================
// Scenario 21 — Stage v0.8 Phase 4 air-gap flux density:
// Step(omega=1) → AirgapFluxDensity → Scope. Integrate to t=1.0 so
// theta = 1.0 rad (exact since dθ/dt = ω = const). With default params
// (B_peak=0.85, p=4, a3=0.10, a_slot=0.05, N_s=24), the analytical
// answer is:
//
//   B_g = 0.85 · (sin(4) + 0.1·sin(12) + 0.05·sin(24))
//       ≈ 0.85 · (-0.7568 + 0.1·(-0.5366) + 0.05·(-0.9056))
//       ≈ -0.7274
// ========================================================================
static void scenario_airgap_flux_density() {
    std::cout << "[21] STAGE v0.8  Step(ω=1) → AirgapFluxDensity → Scope ⇒ -0.727\n";
    NodeGraph g;
    int src = g.addNode(NodeType::StepSignal);
    int agf = g.addNode(NodeType::AirgapFluxDensity);
    int sk  = g.addNode(NodeType::Oscilloscope);
    g.setParam(src, "Amplitude", 1.0);
    g.setParam(src, "Step Time", 0.0);

    auto* ns = g.findNode(src);
    auto* na = g.findNode(agf);
    auto* nk = g.findNode(sk);
    g.tryAddEdge(ns->outputAttrId(), na->inputAttrId(0));
    g.tryAddEdge(na->outputAttrId(), nk->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));

    float v = runUntil(br, sk, 1.0);

    // Analytical value with the registry defaults.
    const double Bpeak = 0.85;
    const double p     = 4.0;
    const double a3    = 0.10;
    const double as    = 0.05;
    const double Ns    = 24.0;
    const double theta = 1.0;
    const double expected = Bpeak * ( std::sin(p * theta)
                                    + a3 * std::sin(3.0 * p * theta)
                                    + as * std::sin(Ns * theta) );

    EXPECT_NEAR(v, expected, 5e-3);   // Scilab "rk" drift over 60 steps
}

// ========================================================================
// Scenario 22 — Stage v0.8 Phase 5 operating-point sweep:
//   T → ┐
//   ω → │── PMSMEfficiency → η → ┐
//   Ke → ┘                       │
//   T → ────────────────────────►│
//   ω → ────────────────────────►│── HeatmapSink (x=T, y=ω, c=η)
//                                 │
//   Verify (a) η matches the closed-form for the chosen operating point
//   and (b) the HeatmapSink's three channels recorded the right values.
// ========================================================================
static void scenario_operating_point_sweep() {
    std::cout << "[22] STAGE v0.8  PMSMEfficiency + HeatmapSink (T,ω → η)\n";

    const double T_val   = 5.0;       // Nm
    const double w_val   = 100.0;     // rad/s
    const double Ke_val  = 0.5;       // V*s/rad

    NodeGraph g;
    int sT  = g.addNode(NodeType::StepSignal);
    int sW  = g.addNode(NodeType::StepSignal);
    int sK  = g.addNode(NodeType::StepSignal);
    int eff = g.addNode(NodeType::PMSMEfficiency);
    int sk_eta = g.addNode(NodeType::Oscilloscope);
    int hm  = g.addNode(NodeType::HeatmapSink);

    g.setParam(sT, "Amplitude", T_val);
    g.setParam(sW, "Amplitude", w_val);
    g.setParam(sK, "Amplitude", Ke_val);

    auto* ne = g.findNode(eff);
    auto* nh = g.findNode(hm);
    g.tryAddEdge(g.findNode(sT)->outputAttrId(), ne->inputAttrId(0));
    g.tryAddEdge(g.findNode(sW)->outputAttrId(), ne->inputAttrId(1));
    g.tryAddEdge(g.findNode(sK)->outputAttrId(), ne->inputAttrId(2));
    g.tryAddEdge(ne->outputAttrId(),
                 g.findNode(sk_eta)->inputAttrId(0));
    // Heatmap inputs: (T, ω, η)
    g.tryAddEdge(g.findNode(sT)->outputAttrId(), nh->inputAttrId(0));
    g.tryAddEdge(g.findNode(sW)->outputAttrId(), nh->inputAttrId(1));
    g.tryAddEdge(ne->outputAttrId(),             nh->inputAttrId(2));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    EXPECT_TRUE(br.step(1.0f / 60.0f));

    // Analytical answer.
    const double Iq    = T_val / Ke_val;
    const double P_out = T_val * w_val;
    const double P_cu  = 1.5 * 0.5 * Iq * Iq;            // default R=0.5
    const double P_fe  = 1e-4 * w_val * w_val;           // default K_iron
    const double P_mech= 1e-3 * std::fabs(w_val);        // default K_mech
    const double eta_exp = P_out / (P_out + P_cu + P_fe + P_mech);

    float eta = lastSample(br, sk_eta);
    EXPECT_NEAR(eta, eta_exp, kTolTight);

    // The HeatmapSink stored 3 channels. Read each one's latest sample.
    EXPECT_TRUE(br.channelCount(hm) == 3);
    auto bufX  = br.buffer(hm, 0);
    auto bufY  = br.buffer(hm, 1);
    auto bufC  = br.buffer(hm, 2);
    int wX = br.writeIndex(hm, 0);
    int wY = br.writeIndex(hm, 1);
    int wC = br.writeIndex(hm, 2);
    EXPECT_TRUE(wX > 0 && wY > 0 && wC > 0);
    float x_latest = bufX.back();
    float y_latest = bufY.back();
    float c_latest = bufC.back();
    EXPECT_NEAR(x_latest, T_val, kTolTight);
    EXPECT_NEAR(y_latest, w_val, kTolTight);
    EXPECT_NEAR(c_latest, eta_exp, kTolTight);
}

// ========================================================================
// Scenario 23 — Stage v0.8 extras: topology sizing variants.
// IPMSizing's saliency factor and BLDCSizing's trapezoidal factor both
// boost achievable torque density relative to the surface PMSM baseline,
// so the bore diameter for the same target T comes out smaller. Verify
// both variants against their closed-form predictions.
// ========================================================================
static void scenario_topology_variants() {
    std::cout << "[23] STAGE v0.8 IPMSizing + BLDCSizing closed-form D check\n";

    const double T_val = 50.0;
    const double w_val = 200.0;

    auto buildAndStep = [&](NodeType type) {
        NodeGraph g;
        int dt = g.addNode(NodeType::DesignTemplate);
        int sz = g.addNode(type);
        int sk = g.addNode(NodeType::Oscilloscope);
        g.setParam(dt, "Target Torque", T_val);
        g.setParam(dt, "Target Speed",  w_val);
        auto* nd = g.findNode(dt);
        auto* ns = g.findNode(sz);
        g.tryAddEdge(nd->outputAttrId(0), ns->inputAttrId(0));
        g.tryAddEdge(nd->outputAttrId(1), ns->inputAttrId(1));
        g.tryAddEdge(ns->outputAttrId(0), g.findNode(sk)->inputAttrId(0));
        ScilabBridge br;
        EXPECT_TRUE(br.reset(g));
        EXPECT_TRUE(br.step(1.0f / 60.0f));
        return std::pair<float, ScilabBridge::Status>{
            lastSample(br, sk), br.status() };
    };

    auto [D_pmsm, _s1] = buildAndStep(NodeType::PMSMSizing);
    auto [D_ipm,  _s2] = buildAndStep(NodeType::IPMSizing);
    auto [D_bldc, _s3] = buildAndStep(NodeType::BLDCSizing);

    // Closed-form references (from each node's defaults).
    // PMSM:  D = (2T / (π·0.85·40000·1.2))^(1/3)
    // IPM:   adds /1.2 saliency under the cube root.
    // BLDC:  B=0.9, A=35000, α=1.0, k=1.15.
    const double D_pmsm_exp = std::cbrt(2.0 * T_val /
        (M_PI * 0.85 * 40000.0 * 1.2));
    const double D_ipm_exp = std::cbrt(2.0 * T_val /
        (M_PI * 0.85 * 40000.0 * 1.2 * 1.2));
    const double D_bldc_exp = std::cbrt(2.0 * T_val /
        (M_PI * 0.90 * 35000.0 * 1.0 * 1.15));

    EXPECT_NEAR(D_pmsm, D_pmsm_exp, kTolTight);
    EXPECT_NEAR(D_ipm,  D_ipm_exp,  1e-4);
    EXPECT_NEAR(D_bldc, D_bldc_exp, kTolTight);
    // Reluctance torque shrinks the IPM bore vs surface PMSM at the same
    // (B, A, α).
    EXPECT_TRUE(D_ipm < D_pmsm);
}

// ========================================================================
// Scenario 24 — Stage v0.9 Phase 1 thermal mass:
//   Step(P=100W) → ThermalMass(C=2, R=0.5, T_amb=298) → Scope
//   tau = R*C = 1 s; steady state = 298 + 100*0.5 = 348 K.
//   At t = 8 s the closed form gives 298 + 50*(1 - exp(-8)) ≈ 347.983 K.
// ========================================================================
static void scenario_thermal_mass_step_response() {
    std::cout << "[24] STAGE v0.9  Step(100W) → ThermalMass(τ=1s) → Scope\n";

    NodeGraph g;
    int sP  = g.addNode(NodeType::StepSignal);
    int tm  = g.addNode(NodeType::ThermalMass);
    int sk  = g.addNode(NodeType::Oscilloscope);
    g.setParam(sP, "Amplitude", 100.0);
    g.setParam(tm, "Thermal Capacitance",   2.0);
    g.setParam(tm, "Thermal Resistance",    0.5);
    g.setParam(tm, "Ambient Temperature", 298.0);

    g.tryAddEdge(g.findNode(sP)->outputAttrId(),
                 g.findNode(tm)->inputAttrId(0));
    g.tryAddEdge(g.findNode(tm)->outputAttrId(),
                 g.findNode(sk)->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));

    // T(t) = T_amb + P*R * (1 - exp(-t/(R*C)))
    const double T_amb = 298.0, P = 100.0, R = 0.5, RC = 1.0;
    auto expected = [&](double t) {
        return T_amb + P * R * (1.0 - std::exp(-t / RC));
    };

    // Check three points along the curve.
    float v1 = runUntil(br, sk, 1.0);   // ~316.4 K
    EXPECT_NEAR(v1, expected(1.0), 0.2);

    float v5 = runUntil(br, sk, 5.0);   // ~347.66 K
    EXPECT_NEAR(v5, expected(5.0), 0.05);

    float v8 = runUntil(br, sk, 8.0);   // ~347.98 K
    EXPECT_NEAR(v8, expected(8.0), 0.05);

    // No NaN/Inf along the trajectory.
    EXPECT_TRUE(br.offendingNodeId() == 0);
}

// ========================================================================
// Scenario 25 — Joule loss closed-form: P_cu = (3/2) R (T/Ke)^2.
//   T=5 Nm, Ke=0.5 → Iq=10 → P_cu = 1.5 * 0.5 * 100 = 75 W.
// ========================================================================
static void scenario_joule_loss() {
    std::cout << "[25] STAGE v0.9  Step(T,Ke) → JouleLoss → Scope ⇒ 75 W\n";
    NodeGraph g;
    int sT = g.addNode(NodeType::StepSignal);
    int sK = g.addNode(NodeType::StepSignal);
    int jl = g.addNode(NodeType::JouleLoss);
    int sk = g.addNode(NodeType::Oscilloscope);
    g.setParam(sT, "Amplitude", 5.0);
    g.setParam(sK, "Amplitude", 0.5);
    auto* nj = g.findNode(jl);
    g.tryAddEdge(g.findNode(sT)->outputAttrId(), nj->inputAttrId(0));
    g.tryAddEdge(g.findNode(sK)->outputAttrId(), nj->inputAttrId(1));
    g.tryAddEdge(nj->outputAttrId(), g.findNode(sk)->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    EXPECT_TRUE(br.step(1.0f / 60.0f));

    float v = lastSample(br, sk);
    EXPECT_NEAR(v, 75.0, kTolMedium);
}

// ========================================================================
// Scenario 26 — Stage v0.9 mesh-colormap pipeline end-to-end.
//   Step(P=50) → ThermalMass(τ=1s) → View3DThermalSink
// Run to t=5s; the bridge's record of the View3DThermalSink channel
// must contain the rising winding temperature (analytical closed-form
// match at the final sample). View3DPanel reads this same channel
// every frame to drive the procedural-mesh tint.
// ========================================================================
static void scenario_view3d_thermal_chain() {
    std::cout << "[26] STAGE v0.9 Step(50W) → ThermalMass → View3DThermalSink\n";
    NodeGraph g;
    int sP = g.addNode(NodeType::StepSignal);
    int tm = g.addNode(NodeType::ThermalMass);
    int sk = g.addNode(NodeType::View3DThermalSink);
    g.setParam(sP, "Amplitude",            50.0);
    g.setParam(tm, "Thermal Capacitance",   2.0);
    g.setParam(tm, "Thermal Resistance",    0.5);
    g.setParam(tm, "Ambient Temperature", 298.0);

    g.tryAddEdge(g.findNode(sP)->outputAttrId(),
                 g.findNode(tm)->inputAttrId(0));
    g.tryAddEdge(g.findNode(tm)->outputAttrId(),
                 g.findNode(sk)->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));

    float v = runUntil(br, sk, 5.0);

    // Closed form: T = T_amb + P*R*(1 - exp(-t/(R*C)))
    //            = 298 + 25*(1 - exp(-5)) ≈ 322.83 K
    const double expected = 298.0 + 50.0 * 0.5 * (1.0 - std::exp(-5.0));
    EXPECT_NEAR(v, expected, 0.05);

    EXPECT_TRUE(br.channelCount(sk) == 1);
}

// ========================================================================
// Scenario 27 — Stage v0.9 Phase 3 multi-node thermal chain:
//
//                       ┌─── q_HtoC ────────┐
//   Step(P=50) ──► tn1 ─┤                   │
//                       │   ThermalRes R1   │
//                       └─── q_CtoH ───┐    │
//                                      │    ▼
//                                  ┌── tn2 ◄── feeds q_HtoC into tn2.in(0)
//                                  │
//                            ThermalRes R2
//                                  │
//                           Step(T_amb=298)
//
// Wire:
//   step_P → tn1.in(0)             (heat in)
//   ir1.in(0) ← tn1.out            (T_hot)
//   ir1.in(1) ← tn2.out            (T_cold)
//   ir1.out(0) (q_HtoC) → tn2.in(0)
//   ir1.out(1) (q_CtoH) → tn1.in(1)
//   ir2.in(0) ← tn2.out
//   ir2.in(1) ← step_amb.out
//   ir2.out(1) (q from ambient to tn2 — negative when tn2 hotter) → tn2.in(1)
//
// Steady state: q_in = P balances heat through R1 and R2 in series.
//   T2 = T_amb + P * R2
//   T1 = T_amb + P * (R1 + R2)
// For P=50, R1=0.5, R2=1.0, T_amb=298:
//   T2_ss = 348 K, T1_ss = 373 K.
// ========================================================================
static void scenario_thermal_chain_two_nodes() {
    std::cout << "[27] STAGE v0.9 Phase 3 — 2-node thermal chain → steady state\n";
    NodeGraph g;
    int step_P   = g.addNode(NodeType::StepSignal);
    int step_amb = g.addNode(NodeType::StepSignal);
    int tn1      = g.addNode(NodeType::ThermalNode);
    int tn2      = g.addNode(NodeType::ThermalNode);
    int ir1      = g.addNode(NodeType::ThermalResistance);
    int ir2      = g.addNode(NodeType::ThermalResistance);
    int k1       = g.addNode(NodeType::Oscilloscope);
    int k2       = g.addNode(NodeType::Oscilloscope);

    g.setParam(step_P,   "Amplitude",  50.0);
    g.setParam(step_amb, "Amplitude", 298.0);
    g.setParam(tn1, "Thermal Capacitance", 0.5);
    g.setParam(tn1, "Initial Temperature", 298.0);
    g.setParam(tn2, "Thermal Capacitance", 0.5);
    g.setParam(tn2, "Initial Temperature", 298.0);
    g.setParam(ir1, "Thermal Resistance",   0.5);
    g.setParam(ir2, "Thermal Resistance",   1.0);

    auto out = [&](int id, int port = 0) { return g.findNode(id)->outputAttrId(port); };
    auto in  = [&](int id, int port = 0) { return g.findNode(id)->inputAttrId(port);  };

    // Heat in
    g.tryAddEdge(out(step_P),         in(tn1, 0));
    // Resistance ir1 between tn1 (hot) and tn2 (cold)
    g.tryAddEdge(out(tn1),            in(ir1, 0));
    g.tryAddEdge(out(tn2),            in(ir1, 1));
    g.tryAddEdge(out(ir1, 0),         in(tn2, 0));        // q_HtoC → tn2
    g.tryAddEdge(out(ir1, 1),         in(tn1, 1));        // q_CtoH → tn1
    // Resistance ir2 between tn2 and ambient
    g.tryAddEdge(out(tn2),            in(ir2, 0));
    g.tryAddEdge(out(step_amb),       in(ir2, 1));
    g.tryAddEdge(out(ir2, 1),         in(tn2, 1));        // q from ambient to tn2
    // Scope sinks
    g.tryAddEdge(out(tn1),            in(k1));
    g.tryAddEdge(out(tn2),            in(k2));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));

    // Run long enough to be at steady state. Slowest time constant is
    // roughly C * R_eff ≈ 0.5 s, so 20 s is way past convergence.
    (void)runUntil(br, k1, 20.0);

    float T1 = lastSample(br, k1);
    float T2 = lastSample(br, k2);

    EXPECT_NEAR(T2, 348.0, 0.2);
    EXPECT_NEAR(T1, 373.0, 0.3);
    // T1 must be hotter than T2 (heat flows from tn1 toward ambient).
    EXPECT_TRUE(T1 > T2);
    EXPECT_TRUE(br.offendingNodeId() == 0);
}

// ========================================================================
// Scenario 28 — Stage v0.9 energy-balance acceptance test.
//   Step(100 W) → ThermalMass(C=10, R=0.5, T_amb=298) → Scope.
//
// τ = R·C = 5 s, so 60 s of integration is ≈12·τ — essentially steady
// state. We compare the bridge-reported T(60s) against energy
// conservation:
//
//   E_in       = P · t_run
//   E_internal = C · (T_bridge − T_amb)
//   E_rejected = ∫_0^t_run (T_analytical(t) − T_amb)/R dt
//              = P · (t − τ·(1 − exp(−t/τ)))     [closed form]
//
// Pass criterion (per planner v0.9 definition-of-done):
//   |E_in − E_internal − E_rejected| / E_in ≤ 1 %.
//
// This is mathematically the same as checking the bridge's final
// temperature matches the analytical curve, scaled by C/E_in (~0.17%
// per K of error). A failure here would indicate a deeper drift in
// the Scilab RK integration.
// ========================================================================
static void scenario_energy_balance_60s() {
    std::cout << "[28] STAGE v0.9 — energy balance over 60 s (≤1 % drift)\n";

    const double P     = 100.0;
    const double C     = 10.0;
    const double R     = 0.5;
    const double T_amb = 298.0;
    const double tau   = R * C;
    const double t_run = 60.0;

    NodeGraph g;
    int sP = g.addNode(NodeType::StepSignal);
    int tm = g.addNode(NodeType::ThermalMass);
    int sk = g.addNode(NodeType::Oscilloscope);
    g.setParam(sP, "Amplitude",            P);
    g.setParam(tm, "Thermal Capacitance",  C);
    g.setParam(tm, "Thermal Resistance",   R);
    g.setParam(tm, "Ambient Temperature", T_amb);
    g.tryAddEdge(g.findNode(sP)->outputAttrId(),
                 g.findNode(tm)->inputAttrId(0));
    g.tryAddEdge(g.findNode(tm)->outputAttrId(),
                 g.findNode(sk)->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));

    float T_final = runUntil(br, sk, t_run);

    const double E_in       = P * t_run;
    const double E_internal = C * (static_cast<double>(T_final) - T_amb);
    const double E_rejected = P * (t_run - tau *
                                   (1.0 - std::exp(-t_run / tau)));
    const double imbalance  = std::fabs(E_in - E_internal - E_rejected);
    const double rel_err    = imbalance / E_in;

    std::cout << "      E_in=" << E_in
              << " J, E_internal=" << E_internal
              << " J, E_rejected=" << E_rejected
              << " J, rel err=" << (rel_err * 100.0) << " %\n";
    EXPECT_TRUE(rel_err < 0.01);     // planner's "≤ 1%" criterion
    EXPECT_TRUE(br.offendingNodeId() == 0);
}

// ========================================================================
// Scenario 29 — Stage v1.0 Phase 1 structural nodes.
//   Step(B=1.0) → MaxwellForce → Scope            ⇒ σ_r ≈ 397 887 Pa
//   Step(R=0.10) → ModalFrequency(m=2) → Scope    ⇒ f₂ matches the
//     thin-ring closed form (~1245 Hz with defaults).
//   ModalFrequency at m=1 must return 0 (rigid-body guard).
// ========================================================================
static void scenario_maxwell_and_modal() {
    std::cout << "[29] STAGE v1.0  MaxwellForce + ModalFrequency closed forms\n";

    // (a) Maxwell pressure.
    {
        NodeGraph g;
        int sB = g.addNode(NodeType::StepSignal);
        int mf = g.addNode(NodeType::MaxwellForce);
        int sk = g.addNode(NodeType::Oscilloscope);
        g.setParam(sB, "Amplitude", 1.0);   // 1 T
        g.tryAddEdge(g.findNode(sB)->outputAttrId(),
                     g.findNode(mf)->inputAttrId(0));
        g.tryAddEdge(g.findNode(mf)->outputAttrId(),
                     g.findNode(sk)->inputAttrId(0));
        ScilabBridge br;
        EXPECT_TRUE(br.reset(g));
        EXPECT_TRUE(br.step(1.0f / 60.0f));
        const double expected = 1.0 * 1.0 / (2.0 * 4.0 * M_PI * 1e-7);
        EXPECT_NEAR(lastSample(br, sk), expected, 1.0);
    }

    // (b) Modal frequency, mode 2, defaults.
    {
        NodeGraph g;
        int sR = g.addNode(NodeType::StepSignal);
        int mf = g.addNode(NodeType::ModalFrequency);
        int sk = g.addNode(NodeType::Oscilloscope);
        g.setParam(sR, "Amplitude", kTolQualitative);  // R = 100 mm
        g.tryAddEdge(g.findNode(sR)->outputAttrId(),
                     g.findNode(mf)->inputAttrId(0));
        g.tryAddEdge(g.findNode(mf)->outputAttrId(),
                     g.findNode(sk)->inputAttrId(0));
        ScilabBridge br;
        EXPECT_TRUE(br.reset(g));
        EXPECT_TRUE(br.step(1.0f / 60.0f));
        // Defaults: E=200e9, ρ=7850, t=0.02, m=2.
        const double E = 200.0e9, rho = 7850.0, t = 0.02, m = 2.0;
        const double R = 0.10;
        const double shape = m * (m*m - 1.0) / std::sqrt(m*m + 1.0);
        const double expected =
            (t / (2.0 * M_PI * R * R)) *
            std::sqrt(E / (12.0 * rho)) * shape;
        EXPECT_NEAR(lastSample(br, sk), expected, 0.5);
    }

    // (c) Mode 1 must be silenced by the bool2s guard.
    {
        NodeGraph g;
        int sR = g.addNode(NodeType::StepSignal);
        int mf = g.addNode(NodeType::ModalFrequency);
        int sk = g.addNode(NodeType::Oscilloscope);
        g.setParam(sR, "Amplitude", kTolQualitative);
        g.setParam(mf, "Mode Order", 1.0);
        g.tryAddEdge(g.findNode(sR)->outputAttrId(),
                     g.findNode(mf)->inputAttrId(0));
        g.tryAddEdge(g.findNode(mf)->outputAttrId(),
                     g.findNode(sk)->inputAttrId(0));
        ScilabBridge br;
        EXPECT_TRUE(br.reset(g));
        EXPECT_TRUE(br.step(1.0f / 60.0f));
        EXPECT_NEAR(lastSample(br, sk), 0.0, 1e-6);
    }
}

// ========================================================================
// Scenario 30 — Stage v1.0 Phase 2 deformation-sink pipeline.
//   Step(R=0.10) → ModalFrequency(m=2) → View3DDeformationSink.in(0)
//   Step(mode=2) ─────────────────────► View3DDeformationSink.in(1)
//   Step(amp=0.08) ───────────────────► View3DDeformationSink.in(2)
// Verify the sink records all three channels and the frequency channel
// matches the closed-form modal frequency from scenario [29].
// ========================================================================
static void scenario_deformation_pipeline() {
    std::cout << "[30] STAGE v1.0  ModalFrequency → View3DDeformationSink\n";

    NodeGraph g;
    int sR  = g.addNode(NodeType::StepSignal);
    int mf  = g.addNode(NodeType::ModalFrequency);
    int sM  = g.addNode(NodeType::StepSignal);
    int sA  = g.addNode(NodeType::StepSignal);
    int sk  = g.addNode(NodeType::View3DDeformationSink);

    g.setParam(sR, "Amplitude", kTolQualitative);
    g.setParam(sM, "Amplitude", 2.0);
    g.setParam(sA, "Amplitude", 0.08);

    auto* ns = g.findNode(sk);
    g.tryAddEdge(g.findNode(sR)->outputAttrId(),
                 g.findNode(mf)->inputAttrId(0));
    g.tryAddEdge(g.findNode(mf)->outputAttrId(), ns->inputAttrId(0));
    g.tryAddEdge(g.findNode(sM)->outputAttrId(), ns->inputAttrId(1));
    g.tryAddEdge(g.findNode(sA)->outputAttrId(), ns->inputAttrId(2));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    EXPECT_TRUE(br.step(1.0f / 60.0f));

    EXPECT_TRUE(br.channelCount(sk) == 3);
    auto bF = br.buffer(sk, 0);
    auto bM = br.buffer(sk, 1);
    auto bA = br.buffer(sk, 2);
    int wF = br.writeIndex(sk, 0);
    int wM = br.writeIndex(sk, 1);
    int wA = br.writeIndex(sk, 2);

    float fq = bF.back();
    float mo = bM.back();
    float am = bA.back();
    (void)wM; (void)wA;

    // Closed-form modal frequency (defaults: E=200e9, ρ=7850,
    // t=0.02, m=2, R=0.1).
    const double E_ym = 200.0e9, rho = 7850.0, tring = 0.02, m = 2.0, R = 0.1;
    const double shape = m * (m*m - 1.0) / std::sqrt(m*m + 1.0);
    const double fq_exp =
        (tring / (2.0 * M_PI * R * R)) * std::sqrt(E_ym / (12.0 * rho))
        * shape;
    EXPECT_NEAR(fq, fq_exp, 0.5);
    EXPECT_NEAR(mo, 2.0,    1e-3);
    EXPECT_NEAR(am, 0.08,   1e-4);
}

// ========================================================================
// Scenario 31 — Stage v1.0 Phase 3 tolerance Monte-Carlo.
//   Step(1.0) → TolerancePerturbator(h=0.1) → DistributionSink
// Run 200 steps (each step = one MC trial). Verify the channel records
// every sample inside [1.0 - h, 1.0 + h] and that the sample mean is
// within a few sigmas of 1.0 (uniform distribution has σ = h/√3,
// stdev_of_mean = σ/√N ≈ 0.0041 for h=0.1, N=200; tolerance 0.05 K
// gives ~12σ margin, robust against the LCG seed).
// ========================================================================
static void scenario_tolerance_monte_carlo() {
    std::cout << "[31] STAGE v1.0  TolerancePerturbator → DistributionSink\n";

    NodeGraph g;
    int sN = g.addNode(NodeType::StepSignal);
    int tp = g.addNode(NodeType::TolerancePerturbator);
    int ds = g.addNode(NodeType::DistributionSink);
    g.setParam(sN, "Amplitude", 1.0);
    g.setParam(tp, "Half Tolerance", 0.1);

    auto* nt = g.findNode(tp);
    g.tryAddEdge(g.findNode(sN)->outputAttrId(), nt->inputAttrId(0));
    g.tryAddEdge(nt->outputAttrId(), g.findNode(ds)->inputAttrId(0));

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));

    const int    N_steps = 200;
    const float  dt      = 1.0f / 60.0f;
    for (int i = 0; i < N_steps; ++i) EXPECT_TRUE(br.step(dt));

    auto buf = br.buffer(ds, 0);
    int  w   = br.writeIndex(ds, 0);
    EXPECT_TRUE(w == N_steps);

    // Buffer acumulativo: total = buf.size() = w.
    int count = static_cast<int>(buf.size());
    double sum = 0;
    float lo = std::numeric_limits<float>::infinity();
    float hi = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < count; ++i) {
        float v = buf[i];
        sum += v;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    double mean = sum / count;

    // Every sample must land strictly inside [nominal − h, nominal + h].
    EXPECT_TRUE(lo >= 0.9f - 1e-5f);
    EXPECT_TRUE(hi <= 1.1f + 1e-5f);
    // Sample mean within ±5·stdev_of_mean of nominal.
    EXPECT_NEAR(mean, 1.0, 0.05);
    // Sanity: we actually see spread, not a degenerate constant.
    EXPECT_TRUE((hi - lo) > 0.02f);
}

// ========================================================================
// Scenario 32 — Per-param input pin (PR2 v1.1):
//   Step(amp=1) → Gain[K=0.5 widget] → Scope          (chain principal)
//   Step(amp=5) → Gain.param_pin(K)                    (drive de K)
//
// Expectativa: el widget K=0.5 queda IGNORADO; el valor efectivo de K
// es la señal del segundo Step (= 5).  Output del Gain = 1 * 5 = 5.
// Si la regla "edge pisa widget" funciona, EXPECT_NEAR(out, 5.0).  Si
// el codegen aún usaba la constante, out = 1 * 0.5 = 0.5.
// ========================================================================
static void scenario_param_pin_drives_gain() {
    std::cout << "[32] Per-param pin  Step(5) → Gain.K(widget=0.5) → Scope\n";
    NodeGraph g;
    int sChain  = g.addNode(NodeType::StepSignal);
    int gain    = g.addNode(NodeType::Gain);
    int sDriver = g.addNode(NodeType::StepSignal);
    int scope   = g.addNode(NodeType::Oscilloscope);
    g.setParam(sChain,  "Amplitude", 1.0);
    g.setParam(sDriver, "Amplitude", 5.0);
    g.setParam(gain,    "K",         0.5);   // widget — debe ser ignorado

    auto* nChain  = g.findNode(sChain);
    auto* nGain   = g.findNode(gain);
    auto* nDriver = g.findNode(sDriver);
    auto* nScope  = g.findNode(scope);

    // Chain principal: signal → Gain → Scope.
    EXPECT_FALSE(g.tryAddEdge(nChain->outputAttrId(),  nGain->inputAttrId(0)).has_value());
    EXPECT_FALSE(g.tryAddEdge(nGain->outputAttrId(),   nScope->inputAttrId(0)).has_value());
    // Param-pin: driver → Gain.param[K] (index 0).
    EXPECT_FALSE(g.tryAddEdge(nDriver->outputAttrId(), nGain->paramAttrId(0)).has_value());

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g));
    float got = runUntil(br, scope, 0.5);
    EXPECT_NEAR(got, 5.0, kTolMedium);
}

// ========================================================================
// Scenario 33 — Param-pin round-trip via .scn:
//   El grafo Step(5) → Gain.K + Step(1) → Gain → Scope se guarda en
//   disco, se vuelve a cargar, y se simula.  La edge a param-pin
//   (to_port = 100 = kAttrIdParamBase) debe preservarse: la salida
//   sigue siendo 1·5 = 5.  Sin esto, la persistencia rompería la
//   diferenciación entre input port y param-pin.
// ========================================================================
static void scenario_param_pin_roundtrip_scn() {
    std::cout << "[33] Per-param pin  round-trip via .scn preserva el edge\n";
    const std::string path = "/tmp/scn_param_pin_roundtrip.scn";

    // Build, save.
    {
        NodeGraph g;
        int sChain  = g.addNode(NodeType::StepSignal);
        int gain    = g.addNode(NodeType::Gain);
        int sDriver = g.addNode(NodeType::StepSignal);
        int scope   = g.addNode(NodeType::Oscilloscope);
        g.setParam(sChain,  "Amplitude", 1.0);
        g.setParam(sDriver, "Amplitude", 5.0);
        g.setParam(gain,    "K",         0.5);
        auto* nC = g.findNode(sChain);
        auto* nG = g.findNode(gain);
        auto* nD = g.findNode(sDriver);
        auto* nK = g.findNode(scope);
        g.tryAddEdge(nC->outputAttrId(), nG->inputAttrId(0));
        g.tryAddEdge(nG->outputAttrId(), nK->inputAttrId(0));
        g.tryAddEdge(nD->outputAttrId(), nG->paramAttrId(0));
        std::unordered_map<int, ScnVec2> pos;
        EXPECT_TRUE(ScnSerializer::saveToFile(path, g, pos));
    }
    // Reload, run.
    NodeGraph g2; std::unordered_map<int, ScnVec2> pos2;
    auto rep = ScnSerializer::loadFromFile(path, g2, pos2);
    EXPECT_TRUE(rep.ok);
    EXPECT_TRUE(g2.edges().size() == 3);
    // Encontrar el scope para runUntil.
    int scopeId = -1;
    for (const auto& n : g2.nodes())
        if (n.type == NodeType::Oscilloscope) { scopeId = n.id; break; }
    EXPECT_TRUE(scopeId >= 0);

    ScilabBridge br;
    EXPECT_TRUE(br.reset(g2));
    float got = runUntil(br, scopeId, 0.5);
    EXPECT_NEAR(got, 5.0, kTolMedium);
}

// ========================================================================
// Scenario 34 — Grammar rejects self-loop to own param:
//   Gain.output → Gain.paramAttrId(0)  debe ser rechazado con R3,
//   igual que cualquier self-loop.  Sin este guard, el codegen
//   produciría una recursión instantánea en la expresión del param.
// ========================================================================
static void scenario_param_pin_selfloop_rejected() {
    std::cout << "[34] Per-param pin  self-loop a propio param rechazado (R3)\n";
    NodeGraph g;
    int gid = g.addNode(NodeType::Gain);
    auto* n = g.findNode(gid);
    auto err = g.tryAddEdge(n->outputAttrId(), n->paramAttrId(0));
    EXPECT_TRUE(err.has_value());
    if (err) EXPECT_TRUE(err->rule == "R3");
}

int main() {
    std::cout << "=== SciNodes Scilab integration tests ===\n\n";

    scenario_stateless_chain();
    scenario_integrator();
    scenario_open_loop_motor();
    scenario_closed_loop_first_order();
    scenario_live_tuning();
    scenario_closed_loop_pid_motor();
    scenario_differentiator();
    scenario_transfer_function();
    scenario_transfer_function_2nd_order();
    scenario_inverse_kinematics();
    scenario_solver_thread();
    scenario_nan_detection();
    scenario_fft_pipeline();
    scenario_phase_portrait();
    scenario_view3d_sink();
    scenario_closed_loop_pid_motor_10s();
    scenario_custom_node_via_json();
    scenario_sod_export();
    scenario_pmsm_sizing();
    scenario_pmsm_electromagnetic();
    scenario_airgap_flux_density();
    scenario_operating_point_sweep();
    scenario_topology_variants();
    scenario_thermal_mass_step_response();
    scenario_joule_loss();
    scenario_view3d_thermal_chain();
    scenario_thermal_chain_two_nodes();
    scenario_energy_balance_60s();
    scenario_maxwell_and_modal();
    scenario_deformation_pipeline();
    scenario_tolerance_monte_carlo();
    scenario_subgraph_flatten_pid_motor();
    scenario_subgraph_e3b_full_loop();
    scenario_subgraph_multi_port();
    scenario_subgraph_roundtrip_scn();
    scenario_subgraph_clone_deep();
    scenario_subgraph_id_for_path();
    scenario_param_pin_drives_gain();
    scenario_param_pin_roundtrip_scn();
    scenario_param_pin_selfloop_rejected();

    std::cout << "\n=== " << g_pass << " passed, " << g_fail << " failed ===\n";
    return g_fail > 0 ? 1 : 0;
}
