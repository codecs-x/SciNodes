// =============================================================================
// backend_motor_dc.cpp — Replica del walkthrough D1 pero a través de la
// interfaz IComputeBackend.
//
// Punto de la prueba: ejercitar IComputeBackend con un caso real (lazo PID +
// motor DC con cambio de parámetro en vivo) usando una implementación
// concreta (ScilabCallApiBackend), sin mencionar call_scilab fuera de la
// construcción del backend.
//
// Esto demuestra que:
//   1. La interfaz es suficiente para expresar el comportamiento que el
//      bridge necesita.
//   2. Un backend concreto puede implementarla limpiamente.
//   3. El consumidor de la interfaz no se entera del mecanismo subyacente
//      (subprocess vs in-process).
// =============================================================================

#include "ScilabCallApiBackend.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <vector>

using scinodes::IComputeBackend;
using scinodes::BackendPrepareSpec;
using scinodes::SinkSample;
using scinodes::ScilabCallApiBackend;

// Construye la spec del walkthrough D1 a mano. En el bridge real,
// ScilabCodeGen produciría esto desde un NodeGraph.
static BackendPrepareSpec buildMotorDcSpec() {
    BackendPrepareSpec spec;

    // x(1) = pid_integral, x(2) = i (corriente), x(3) = omega
    spec.stateSize    = 3;
    spec.initialState = { 0.0, 0.0, 0.0 };

    // Parámetros vivos: Kp y Ki del PID. setpoint también, por gusto.
    spec.params = {
        // nodeId, paramIdx, scilabName, initialValue
        { 1, 0, "Kp",      0.5  },
        { 1, 1, "Ki",      2.0  },
        { 0, 0, "w_ref",   50.0 },
    };

    // Dinámica: misma que motor_dc.cpp pero usando los globales declarados.
    spec.dynamicsFunction =
        "function xdot=dynamics(t, x)\n"
        "  e = w_ref - x(3);\n"
        "  V = Kp*e + Ki*x(1);\n"
        "  xdot = [e; ..\n"
        "          (V - 1.0*x(2) - 0.1*x(3)) / 0.01; ..\n"
        "          (0.1*x(2) - 0.001*x(3)) / 0.01];\n"
        "endfunction";

    // Único sumidero: Oscilloscope leyendo omega.
    spec.sinkChannels = {
        // nodeId del Scope, canal 0, expresión = x(3)
        { 2, 0, "x(3)" },
    };

    return spec;
}

int main() {
    std::fprintf(stderr,
        "================================================================\n"
        " SciNodes — backend_motor_dc: walkthrough D1 vía IComputeBackend\n"
        "================================================================\n");

    // Construir el backend concreto. A partir de aquí, todo el código habla
    // contra la interfaz, no contra el tipo concreto.
    ScilabCallApiBackend impl;
    IComputeBackend& backend = impl;

    BackendPrepareSpec spec = buildMotorDcSpec();
    if (!backend.prepare(spec)) {
        std::fprintf(stderr, "[ERROR] prepare: %s\n",
                     backend.lastError().c_str());
        return 1;
    }
    std::fprintf(stderr, "[OK]    prepare\n");

    const double dt      = 1.0 / 60.0;
    const int    N       = static_cast<int>(10.0 / dt) + 1;
    const int    kSwitch = static_cast<int>(2.0 / dt);

    std::vector<double> wHist(N), tHist(N);
    bool kpSwitched = false;

    auto wallStart = std::chrono::steady_clock::now();

    for (int k = 0; k < N; ++k) {
        // Cambio de parámetro en vivo: a t = 2 s, Kp 0.5 → 1.5.
        if (!kpSwitched && k >= kSwitch) {
            if (!backend.setParameter(1, 0, 1.5)) {
                std::fprintf(stderr, "[ERROR] setParameter: %s\n",
                             backend.lastError().c_str());
                return 1;
            }
            std::fprintf(stderr, "[OK]    Kp ← 1.5 en t = %.4f\n", k * dt);
            kpSwitched = true;
        }

        std::vector<SinkSample> out;
        int offending = 0;
        if (!backend.step(dt, out, &offending)) {
            std::fprintf(stderr, "[ERROR] step: %s\n",
                         backend.lastError().c_str());
            return 1;
        }
        if (offending != 0) {
            std::fprintf(stderr, "[WARN]  nodo %d produjo NaN/Inf\n", offending);
        }

        tHist[k] = k * dt;
        wHist[k] = out.empty() ? 0.0 : out[0].value;
    }

    auto wallEnd = std::chrono::steady_clock::now();
    double wallSec = std::chrono::duration<double>(wallEnd - wallStart).count();

    backend.shutdown();

    // Trayectoria condensada.
    std::fprintf(stdout, "\n%-8s %-12s\n", "t [s]", "omega [rad/s]");
    for (int k = 0; k < N; k += 30) {
        std::fprintf(stdout, "%-8.4f %-12.4f%s\n",
            tHist[k], wHist[k],
            (k == kSwitch) ? "   ← Kp = 1.5" : "");
    }

    // Criterios.
    auto omegaAt = [&](double tTarget) {
        int k = static_cast<int>(tTarget / dt + 0.5);
        if (k < 0) k = 0;
        if (k >= N) k = N - 1;
        return wHist[k];
    };

    double w8  = omegaAt(8.0);
    double w9  = omegaAt(9.0);
    double w10 = omegaAt(10.0);

    std::fprintf(stdout,
        "\n----------------------------------------------------------------\n"
        "Criterio scenario 16 (banda ±1%% del setpoint 50 rad/s):\n"
        "   ω(8 s)  = %.4f   %s\n"
        "   ω(9 s)  = %.4f   %s\n"
        "   ω(10 s) = %.4f   %s\n"
        "Costo de pared: %.3f s para %d ticks (~%.2f ms/tick)\n"
        "----------------------------------------------------------------\n",
        w8,  std::fabs(w8  - 50.0) <= 0.5 ? "OK" : "FAIL",
        w9,  std::fabs(w9  - 50.0) <= 0.5 ? "OK" : "FAIL",
        w10, std::fabs(w10 - 50.0) <= 0.5 ? "OK" : "FAIL",
        wallSec, N, wallSec * 1000.0 / N);

    bool ok = std::fabs(w8  - 50.0) <= 0.5
           && std::fabs(w9  - 50.0) <= 0.5
           && std::fabs(w10 - 50.0) <= 0.5;

    std::fprintf(stderr, "\n%s\n",
        ok ? "[PASS]  IComputeBackend ejerce correctamente el walkthrough D1."
           : "[FAIL]  Trayectoria fuera de banda.");
    return ok ? 0 : 2;
}
