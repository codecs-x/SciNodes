// =============================================================================
// test_callapi_bridge.cpp — Verifica el ScilabBridge en modo "external
// backend" usando ScilabCallApiBackend.  Ejerce la integración completa de
// la facade (setBackend → reset → step → ring buffer) contra un grafo
// concreto del walkthrough D1.
//
// Solo se compila cuando SCINODES_WITH_CALLAPI=ON.
// =============================================================================

#include "core/NodeGraph.hpp"
#include "core/NodeType.hpp"
#include "core/ScilabBridge.hpp"
#include "core/backends/ScilabCallApiBackend.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
int g_pass = 0, g_fail = 0;

void expect_true(bool cond, const char* msg) {
    if (cond) { ++g_pass; }
    else {
        ++g_fail;
        std::fprintf(stderr, "[FAIL] %s\n", msg);
    }
}

void expect_near(double got, double want, double tol, const char* msg) {
    if (std::fabs(got - want) <= tol) { ++g_pass; }
    else {
        ++g_fail;
        std::fprintf(stderr, "[FAIL] %s — got %.6f, want %.6f (±%.4f)\n",
                     msg, got, want, tol);
    }
}

// Construye el grafo del walkthrough D1:
//   StepSignal(50) → Sum(+,-) → PID(0.5, 2.0) → DCMotor → Scope
//                       ↑                          │
//                       └────── realimentación ────┘
NodeGraph buildClosedLoopPid(int* outScopeId) {
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
    g.tryAddEdge(nm->outputAttrId(),   nsm->inputAttrId(1));  // feedback

    *outScopeId = scope;
    return g;
}

}  // namespace

int main() {
    std::fprintf(stderr,
        "================================================================\n"
        " test_callapi_bridge — ScilabBridge facade + call_scilab backend\n"
        "================================================================\n");

    // --- Construcción del bridge con backend in-process ------------------
    ScilabBridge br;
    br.setBackend(std::make_unique<scinodes::ScilabCallApiBackend>());
    expect_true(br.hasExternalBackend(), "backend externo inyectado");

    // --- Escenario 6 / 16 (variante call_scilab) -------------------------
    int scopeId = 0;
    NodeGraph g = buildClosedLoopPid(&scopeId);

    expect_true(br.reset(g), "bridge.reset(g) OK");
    expect_true(br.status() == ScilabBridge::Status::Ready,
                "bridge.status() == Ready después de reset");

    // Avanza la simulación hasta t = 10 s con dt = 1/60.
    const float dt = 1.0f / 60.0f;
    const int   N  = static_cast<int>(10.0f / dt) + 1;

    for (int k = 1; k < N; ++k) {
        if (!br.step(dt)) {
            std::fprintf(stderr, "[FAIL] step(%d) — %s\n",
                         k, br.lastError().c_str());
            ++g_fail;
            br.stop();
            std::fprintf(stderr,
                "\n=== %d passed, %d failed ===\n", g_pass, g_fail);
            return g_fail ? 1 : 0;
        }
    }

    // Última muestra del scope.
    auto buf = br.buffer(scopeId, 0);
    int  ix  = br.writeIndex(scopeId, 0);
    double wFinal = buf[(ix - 1 + (int)buf.size()) % (int)buf.size()];
    expect_near(wFinal, 50.0, 0.5,
                "omega final (t=10s) en banda ±1% del setpoint");

    // --- Cambio de parámetro en vivo -------------------------------------
    // Reset al grafo, simular hasta t=2s, cambiar Kp, terminar a t=4s.
    NodeGraph g2 = buildClosedLoopPid(&scopeId);
    expect_true(br.reset(g2), "bridge.reset(g2) OK (segunda corrida)");

    const int Nphase1 = static_cast<int>(2.0f / dt);
    for (int k = 1; k < Nphase1; ++k) (void)br.step(dt);

    // PIDController es el nodo con paramIdx 0 = Kp (orden de declaración
    // en NodeType.cpp).  Necesitamos su nodeId del grafo nuevo.
    int pidId = -1;
    for (const auto& n : g2.nodes()) {
        if (n.type == NodeType::PIDController) { pidId = n.id; break; }
    }
    expect_true(pidId > 0, "encontrado nodo PID en el grafo");
    expect_true(br.sendParameter(pidId, 0, 1.5), "sendParameter(Kp=1.5)");

    const int Nphase2 = static_cast<int>(4.0f / dt) + 1;
    for (int k = Nphase1; k < Nphase2; ++k) (void)br.step(dt);

    auto buf2 = br.buffer(scopeId, 0);
    int  ix2  = br.writeIndex(scopeId, 0);
    double wAfterChange = buf2[(ix2 - 1 + (int)buf2.size()) % (int)buf2.size()];
    expect_near(wAfterChange, 50.0, 0.5,
                "omega(t=4s) sigue en banda tras cambio de Kp en vivo");

    // --- Export .sod -----------------------------------------------------
    const char* sodPath = "/tmp/test_callapi_bridge.sod";
    std::remove(sodPath);
    expect_true(br.exportSod(sodPath), "exportSod() llamada aceptada");

    // En modo síncrono, el resultado queda inmediatamente disponible vía
    // takeLastExportResult().
    std::string sodResult = br.takeLastExportResult();
    expect_true(sodResult.rfind("SAVED", 0) == 0,
                "takeLastExportResult prefijo SAVED");

    // El archivo debe existir y tener tamaño no trivial (HDF5 mínimo es
    // varios kB).
    if (FILE* f = std::fopen(sodPath, "rb")) {
        std::fseek(f, 0, SEEK_END);
        long size = std::ftell(f);
        std::fclose(f);
        expect_true(size > 1024, ".sod existe y mide > 1 KB");
        std::fprintf(stderr, "[info] %s = %ld bytes\n", sodPath, size);
    } else {
        ++g_fail;
        std::fprintf(stderr, "[FAIL] no pude abrir %s\n", sodPath);
    }

    br.stop();

    std::fprintf(stderr, "\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
