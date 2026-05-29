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
#include "../src/core/NodeGraph.hpp"
#include "../src/core/ScilabBridge.hpp"
#include "../src/core/ScilabCodeGen.hpp"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

// ---- Minimal test framework (mirrors test_grammar.cpp) ------------------
static int g_pass = 0, g_fail = 0;

#define EXPECT_TRUE(cond) do { \
    if (cond) { ++g_pass; } \
    else { ++g_fail; \
           std::cerr << "  FAIL  " << #cond \
                     << "  [" << __FILE__ << ":" << __LINE__ << "]\n"; } \
} while(0)

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
    int wi = br.writeIndex(sinkId);
    return br.buffer(sinkId)[(wi - 1) % ScilabBridge::BUFFER_SIZE];
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
    EXPECT_NEAR(got, 0.0, 1e-5);
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
    EXPECT_NEAR(runUntil(br, k, 1.0), 1.0, 1e-5);
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
    EXPECT_NEAR(lastSample(br, scope), 1.0 - std::exp(-t1), 1e-4);
    runUntil(br, scope, 3.0);
    double t3 = br.time();
    EXPECT_NEAR(lastSample(br, scope), 1.0 - std::exp(-t3), 1e-4);
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
    EXPECT_NEAR(before, 2.0, 1e-2);

    // Live-tune K to 5.
    EXPECT_TRUE(br.sendParameter(t, /*paramIdx=*/0, 5.0));
    br.step(1.0f / 60.0f);
    float after = lastSample(br, k);
    double now  = br.time();
    EXPECT_NEAR(after, 5.0 * std::sin(2.0 * M_PI * now), 1e-2);
}

// ========================================================================
// Scenario 6 — Canonical closed-loop PID + DC motor:
//   Step(50) → Sum(+,-) → PID(Kp=0.5, Ki=2.0) → DCMotor → Scope
//                ↑                                  │
//                └────────── (feedback) ────────────┘
//
//   With Ki driving the integral term, ω → 50 rad/s in steady state.
// ========================================================================
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
int main() {
    std::cout << "=== SciNodes Scilab integration tests ===\n\n";

    scenario_stateless_chain();
    scenario_integrator();
    scenario_open_loop_motor();
    scenario_closed_loop_first_order();
    scenario_live_tuning();
    scenario_closed_loop_pid_motor();

    std::cout << "\n=== " << g_pass << " passed, " << g_fail << " failed ===\n";
    return g_fail > 0 ? 1 : 0;
}
