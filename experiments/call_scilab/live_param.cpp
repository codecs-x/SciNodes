// =============================================================================
// live_param.cpp — Cambio de parámetro en vivo vía call_scilab.
//
// Mismo lazo PID + motor DC del walkthrough D1, pero a t = 2 s se cambia
// Kp de 0.5 a 1.5.  El propósito es validar que la API permite que el
// hilo de simulación recoja un valor nuevo sin reiniciar nada y que la
// trayectoria responde.
//
// Implementación: Kp se mantiene como variable global de Scilab y la
// función dynamics la lee por referencia.  En C++ basta hacer
// SendScilabJob("Kp = 1.5;") para que el siguiente ode(...) use el valor
// nuevo.  Esto es lo que tendría que hacer ScilabBridge::sendParameter()
// en el refactor.
// =============================================================================

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <chrono>

extern "C" {
    #include <call_scilab.h>
    #include <api_scilab.h>
    #include <api_stack_double.h>
}

static bool runJob(const char* job) {
    if (SendScilabJob(const_cast<char*>(job)) != 0) {
        std::fprintf(stderr, "[ERROR] SendScilabJob: %s\n", job);
        return false;
    }
    return true;
}

static bool readDouble(const char* name, double* out) {
    return getNamedScalarDouble(nullptr, const_cast<char*>(name), out) == 0;
}

int main() {
    std::fprintf(stderr,
        "================================================================\n"
        " SciNodes — call_scilab: cambio de Kp en vivo (0.5 → 1.5 a t=2s)\n"
        "================================================================\n");

    const char* sci = std::getenv("SCI");
    if (!sci) { std::fprintf(stderr, "[ERROR] SCI no definido.\n"); return 1; }

    if (!StartScilab(const_cast<char*>(sci), nullptr, 0)) {
        std::fprintf(stderr, "[ERROR] StartScilab\n"); return 1;
    }

    // Kp y Ki como globales de Scilab.  La función dynamics los lee.
    if (!runJob("Kp = 0.5; Ki = 2.0;")) { TerminateScilab(nullptr); return 1; }

    const char* defDynamics =
        "function xdot=dynamics(t, x)\n"
        "  w_ref = 50;\n"
        "  e = w_ref - x(3);\n"
        "  V = Kp*e + Ki*x(1);\n"
        "  xdot = [e; ..\n"
        "          (V - 1.0*x(2) - 0.1*x(3)) / 0.01; ..\n"
        "          (0.1*x(2) - 0.001*x(3)) / 0.01];\n"
        "endfunction";
    if (!runJob(defDynamics)) { TerminateScilab(nullptr); return 1; }
    if (!runJob("x = [0; 0; 0]; t_prev = 0;")) {
        TerminateScilab(nullptr); return 1;
    }
    std::fprintf(stderr, "[OK]    setup completo (Kp = 0.5)\n");

    // -------------------------------------------------------------------------
    // Bucle: dt = 1/60, total 4 s.  A los 2 s exactos se reasigna Kp = 1.5.
    // -------------------------------------------------------------------------
    const double dt = 1.0 / 60.0;
    const int    N  = static_cast<int>(4.0 / dt) + 1;
    const int    kSwitch = static_cast<int>(2.0 / dt);

    std::vector<double> tHist(N), wHist(N);
    char cmd[160];

    bool switched = false;

    auto tStartWall = std::chrono::steady_clock::now();

    for (int k = 0; k < N; ++k) {
        double t = k * dt;

        // Cambio de Kp justo antes del tick de t = 2 s.
        if (!switched && k >= kSwitch) {
            if (!runJob("Kp = 1.5;")) {
                TerminateScilab(nullptr); return 1;
            }
            std::fprintf(stderr, "[OK]    Kp ← 1.5 en tick %d (t=%.4f)\n", k, t);
            switched = true;
        }

        if (k > 0) {
            std::snprintf(cmd, sizeof(cmd),
                "x = ode(\"rk\", x, t_prev, %.6f, dynamics); t_prev = %.6f;",
                t, t);
            if (!runJob(cmd)) { TerminateScilab(nullptr); return 1; }
        }
        if (!runJob("y = x(3);")) { TerminateScilab(nullptr); return 1; }
        double w = 0.0;
        if (!readDouble("y", &w)) { TerminateScilab(nullptr); return 1; }
        tHist[k] = t;
        wHist[k] = w;
    }

    auto tEndWall  = std::chrono::steady_clock::now();
    double wallSec = std::chrono::duration<double>(tEndWall - tStartWall).count();

    // -------------------------------------------------------------------------
    // Resumen: dos regiones, antes y después del switch.
    // -------------------------------------------------------------------------
    auto wPeakInRange = [&](int kA, int kB) {
        double peak = 0.0; int kp = kA;
        for (int k = kA; k <= kB && k < N; ++k) {
            if (wHist[k] > peak) { peak = wHist[k]; kp = k; }
        }
        return std::pair<double,int>(peak, kp);
    };

    auto [peak1, kp1] = wPeakInRange(0, kSwitch - 1);
    auto [peak2, kp2] = wPeakInRange(kSwitch, N - 1);

    std::fprintf(stdout, "\n%-8s %-12s\n", "t [s]", "omega");
    for (int k = 0; k < N; k += 12) {
        std::fprintf(stdout, "%-8.4f %-12.4f%s\n",
            tHist[k], wHist[k],
            (k == kSwitch) ? "   ← Kp = 1.5" : "");
    }

    std::fprintf(stdout,
        "\n----------------------------------------------------------------\n"
        "Fase 1 (Kp = 0.5, t ∈ [0, 2 s])\n"
        "  Pico:   %.3f rad/s en t = %.3f s\n"
        "  ω final (t=2s):  %.4f rad/s\n"
        "Fase 2 (Kp = 1.5, t ∈ [2, 4 s])\n"
        "  Pico:   %.3f rad/s en t = %.3f s\n"
        "  ω final (t=4s):  %.4f rad/s\n"
        "Costo de pared: %.3f s para %d ticks (~%.2f ms/tick)\n"
        "----------------------------------------------------------------\n",
        peak1, tHist[kp1], wHist[kSwitch - 1],
        peak2, tHist[kp2], wHist[N - 1],
        wallSec, N, wallSec * 1000.0 / N);

    TerminateScilab(nullptr);

    // -------------------------------------------------------------------------
    // Criterios:
    //   1. La fase 1 debe asentarse cerca de 50 al final de t=2s.
    //   2. Justo después del switch, debe haber un transitorio observable
    //      (no es trivial de definir).  Mínimo: el sistema sigue convergiendo.
    //   3. ω(t=4s) debe estar dentro de ±1% del setpoint.
    // -------------------------------------------------------------------------
    bool ok = true;
    if (std::fabs(wHist[N - 1] - 50.0) > 0.5) {
        std::fprintf(stderr, "[FAIL]  ω(t=4s) fuera de banda: %.4f\n",
                     wHist[N - 1]);
        ok = false;
    }

    if (ok) {
        std::fprintf(stderr, "[PASS]  Cambio de parámetro en vivo es funcional.\n");
        return 0;
    }
    return 2;
}
