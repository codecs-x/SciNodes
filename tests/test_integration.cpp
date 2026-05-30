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
        EXPECT_NEAR(lastSample(br, k1), expectedT1, 1e-4);
        EXPECT_NEAR(lastSample(br, k2), expectedT2, 1e-4);
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
    EXPECT_NEAR(lastSample(br, k), 1.0 - std::cos(t), 1e-3);
    runUntil(br, k, M_PI);
    t = br.time();
    EXPECT_NEAR(lastSample(br, k), 1.0 - std::cos(t), 1e-3);
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
    EXPECT_NEAR(lastSample(br, k), 1.0 - std::exp(-t), 1e-4);
    runUntil(br, k, 3.0);
    t = br.time();
    EXPECT_NEAR(lastSample(br, k), 1.0 - std::exp(-t), 1e-4);
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
    EXPECT_NEAR(runUntil(br, k, 0.5), 2.0, 1e-2);
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
    int wi    = br.writeIndex(f);
    int start = (wi - kN) & (ScilabBridge::BUFFER_SIZE - 1);
    std::vector<float> win(kN);
    for (int i = 0; i < kN; ++i)
        win[i] = buf[(start + i) % ScilabBridge::BUFFER_SIZE];

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
    float x = br.buffer(pp, 0)[(wi0 - 1) % ScilabBridge::BUFFER_SIZE];
    float y = br.buffer(pp, 1)[(wi1 - 1) % ScilabBridge::BUFFER_SIZE];
    EXPECT_NEAR(x, 0.0, 1e-3);
    EXPECT_NEAR(y, 1.0, 1e-3);
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
    float last = br.buffer(v)[(wi - 1) % ScilabBridge::BUFFER_SIZE];
    // After 1 simulated second the sine should be ≈ sin(2π) = 0.
    EXPECT_NEAR(last, 0.0, 1e-3);
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

    auto& reg = scinodes::CustomNodeRegistry::instance();
    reg.clear();
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
    EXPECT_NEAR(v, 30.0, 1e-3);

    // Live-tune k: 3*5*5 = 75 once Scilab applies the param.
    EXPECT_TRUE(br.sendParameter(tri, /*paramIdx*/ 0, /*value*/ 5.0));
    v = runUntil(br, scope, 1.0);
    EXPECT_NEAR(v, 75.0, 1e-3);

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

    EXPECT_NEAR(D, D_expected, 1e-4);   // metres
    EXPECT_NEAR(L, L_expected, 1e-4);
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
    EXPECT_NEAR(Vrms, Vrms_exp, 1e-2);
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
    EXPECT_NEAR(eta, eta_exp, 1e-4);

    // The HeatmapSink stored 3 channels. Read each one's latest sample.
    EXPECT_TRUE(br.channelCount(hm) == 3);
    auto bufX  = br.buffer(hm, 0);
    auto bufY  = br.buffer(hm, 1);
    auto bufC  = br.buffer(hm, 2);
    int wX = br.writeIndex(hm, 0);
    int wY = br.writeIndex(hm, 1);
    int wC = br.writeIndex(hm, 2);
    EXPECT_TRUE(wX > 0 && wY > 0 && wC > 0);
    float x_latest = bufX[(wX - 1) % ScilabBridge::BUFFER_SIZE];
    float y_latest = bufY[(wY - 1) % ScilabBridge::BUFFER_SIZE];
    float c_latest = bufC[(wC - 1) % ScilabBridge::BUFFER_SIZE];
    EXPECT_NEAR(x_latest, T_val, 1e-4);
    EXPECT_NEAR(y_latest, w_val, 1e-4);
    EXPECT_NEAR(c_latest, eta_exp, 1e-4);
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

    EXPECT_NEAR(D_pmsm, D_pmsm_exp, 1e-4);
    EXPECT_NEAR(D_ipm,  D_ipm_exp,  1e-4);
    EXPECT_NEAR(D_bldc, D_bldc_exp, 1e-4);
    // Reluctance torque shrinks the IPM bore vs surface PMSM at the same
    // (B, A, α).
    EXPECT_TRUE(D_ipm < D_pmsm);
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

    std::cout << "\n=== " << g_pass << " passed, " << g_fail << " failed ===\n";
    return g_fail > 0 ? 1 : 0;
}
