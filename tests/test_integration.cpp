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
#include "../src/core/Fft.hpp"
#include "../src/core/NodeGraph.hpp"
#include "../src/core/ScilabBridge.hpp"
#include "../src/core/ScilabCodeGen.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
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

    std::cout << "\n=== " << g_pass << " passed, " << g_fail << " failed ===\n";
    return g_fail > 0 ? 1 : 0;
}
