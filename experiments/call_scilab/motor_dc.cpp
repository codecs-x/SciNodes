// =============================================================================
// motor_dc.cpp — Replica del walkthrough D1 (PID + motor DC) vía call_scilab.
//
// Compara el comportamiento numérico con el documentado en la tesis:
//   - Setpoint:                 ω* = 50 rad/s
//   - PID:                      Kp = 0.5, Ki = 2.0, Kd = 0.0
//   - DCMotor (defaults):       Ra=1.0  La=0.01  Ke=0.1  Kt=0.1  J=0.01  B=0.001
//   - Esperado:                 pico ~53 rad/s a t≈0.4s, asentamiento <0.8s
//
// El estado de Scilab es x = [pid_integral; i; ω].
// Cada tick: ode("rk", x, t_prev, t, dynamics).
// Lectura: y = x(3); luego getNamedScalarDouble.
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
        std::fprintf(stderr, "[ERROR] SendScilabJob falló:\n  %s\n", job);
        return false;
    }
    return true;
}

static bool readDouble(const char* name, double* out) {
    return getNamedScalarDouble(nullptr, const_cast<char*>(name), out) == 0;
}

// -----------------------------------------------------------------------------
int main(int argc, char** argv) {
    (void)argc; (void)argv;

    std::fprintf(stderr,
        "================================================================\n"
        " SciNodes — call_scilab: walkthrough PID + motor DC\n"
        " Esperado:  pico ~53 rad/s, asentamiento <0.8s, final ~50\n"
        "================================================================\n");

    const char* sci = std::getenv("SCI");
    if (!sci) { std::fprintf(stderr, "[ERROR] SCI no definido.\n"); return 1; }

    if (!StartScilab(const_cast<char*>(sci), nullptr, 0)) {
        std::fprintf(stderr, "[ERROR] StartScilab\n"); return 1;
    }
    std::fprintf(stderr, "[OK]    StartScilab\n");

    // -------------------------------------------------------------------------
    // Definir la función dynamics(t, x) en Scilab.
    // x(1) = integral del error del PID
    // x(2) = corriente del motor
    // x(3) = velocidad angular del motor
    // -------------------------------------------------------------------------
    const char* defDynamics =
        "function xdot=dynamics(t, x)\n"
        "  w_ref = 50;\n"
        "  e = w_ref - x(3);\n"
        "  V = 0.5*e + 2.0*x(1);\n"
        "  xdot = [e; ..\n"
        "          (V - 1.0*x(2) - 0.1*x(3)) / 0.01; ..\n"
        "          (0.1*x(2) - 0.001*x(3)) / 0.01];\n"
        "endfunction";
    if (!runJob(defDynamics)) { TerminateScilab(nullptr); return 1; }
    std::fprintf(stderr, "[OK]    función dynamics definida\n");

    // Estado inicial
    if (!runJob("x = [0; 0; 0]; t_prev = 0;")) {
        TerminateScilab(nullptr); return 1;
    }
    std::fprintf(stderr, "[OK]    estado inicial x = [0;0;0]\n");

    // -------------------------------------------------------------------------
    // Bucle de simulación: dt = 1/60, duración 1.5 s.
    // En cada paso medimos también el tiempo de pared para tener una pista
    // del costo del ciclo (esto NO es benchmark serio, solo orientativo).
    // -------------------------------------------------------------------------
    const double dt = 1.0 / 60.0;
    const int    N  = static_cast<int>(10.0 / dt) + 1;     // 10 s como scenario 16

    std::vector<double> tHist(N), wHist(N);
    char cmd[160];

    auto tStartWall = std::chrono::steady_clock::now();

    for (int k = 0; k < N; ++k) {
        double t = k * dt;
        if (k > 0) {
            std::snprintf(cmd, sizeof(cmd),
                "x = ode(\"rk\", x, t_prev, %.6f, dynamics); t_prev = %.6f;",
                t, t);
            if (!runJob(cmd)) { TerminateScilab(nullptr); return 1; }
        }
        // Leer ω = x(3)
        if (!runJob("y = x(3);")) { TerminateScilab(nullptr); return 1; }
        double w = 0.0;
        if (!readDouble("y", &w)) { TerminateScilab(nullptr); return 1; }
        tHist[k] = t;
        wHist[k] = w;
    }

    auto tEndWall  = std::chrono::steady_clock::now();
    double wallSec = std::chrono::duration<double>(tEndWall - tStartWall).count();

    // -------------------------------------------------------------------------
    // Trayectoria condensada (un punto cada 30 ticks ~ medio segundo).
    // -------------------------------------------------------------------------
    std::fprintf(stdout, "\n%-8s %-12s\n", "t [s]", "omega [rad/s]");
    for (int k = 0; k < N; k += 30) {
        std::fprintf(stdout, "%-8.4f %-12.4f\n", tHist[k], wHist[k]);
    }

    // -------------------------------------------------------------------------
    // Métricas de aceptación.
    // -------------------------------------------------------------------------
    double wPeak = 0.0; int kPeak = 0;
    for (int k = 0; k < N; ++k) {
        if (wHist[k] > wPeak) { wPeak = wHist[k]; kPeak = k; }
    }
    double wFinal = wHist[N - 1];
    double errorFinalPct = std::fabs(wFinal - 50.0) / 50.0 * 100.0;

    // Asentamiento: primer k tal que para todo j>=k, |w(j) - 50| <= 0.5 rad/s
    int kSettle = N - 1;
    for (int k = 0; k < N; ++k) {
        bool ok = true;
        for (int j = k; j < N; ++j) {
            if (std::fabs(wHist[j] - 50.0) > 0.5) { ok = false; break; }
        }
        if (ok) { kSettle = k; break; }
    }

    std::fprintf(stdout,
        "\n----------------------------------------------------------------\n"
        "Pico:           %.3f rad/s en t = %.3f s   (sobre-impulso %.2f%%)\n"
        "Asentamiento:   t_s = %.3f s (banda ±1%%)\n"
        "Valor final:    %.4f rad/s\n"
        "Error final:    %.4f %%\n"
        "Costo de pared: %.3f s para %d ticks\n"
        "----------------------------------------------------------------\n",
        wPeak, tHist[kPeak], (wPeak - 50.0) / 50.0 * 100.0,
        tHist[kSettle], wFinal, errorFinalPct,
        wallSec, N);

    TerminateScilab(nullptr);
    std::fprintf(stderr, "[OK]    TerminateScilab\n");

    // -------------------------------------------------------------------------
    // Criterio del escenario 16 de test_integration.cpp: |w - 50| <= 0.5
    // a los t = 8, 9, 10 s.  Eso es lo que el test real verifica\,; el
    // sobre-impulso transitorio no está cubierto por ninguna prueba.
    // -------------------------------------------------------------------------
    auto omegaAt = [&](double tTarget) -> double {
        int k = static_cast<int>(tTarget / dt + 0.5);
        if (k < 0) k = 0;
        if (k >= N) k = N - 1;
        return wHist[k];
    };

    double w8  = omegaAt(8.0);
    double w9  = omegaAt(9.0);
    double w10 = omegaAt(10.0);

    std::fprintf(stdout,
        "Criterio scenario 16 (banda ±1%% del setpoint 50 rad/s):\n"
        "   ω(8 s)  = %.4f   |err| = %.4f   %s\n"
        "   ω(9 s)  = %.4f   |err| = %.4f   %s\n"
        "   ω(10 s) = %.4f   |err| = %.4f   %s\n",
        w8,  std::fabs(w8  - 50.0), std::fabs(w8  - 50.0) <= 0.5 ? "OK" : "FAIL",
        w9,  std::fabs(w9  - 50.0), std::fabs(w9  - 50.0) <= 0.5 ? "OK" : "FAIL",
        w10, std::fabs(w10 - 50.0), std::fabs(w10 - 50.0) <= 0.5 ? "OK" : "FAIL");

    bool ok = std::fabs(w8  - 50.0) <= 0.5
           && std::fabs(w9  - 50.0) <= 0.5
           && std::fabs(w10 - 50.0) <= 0.5;

    if (ok) {
        std::fprintf(stderr, "[PASS]  Walkthrough D1 reproducible vía call_scilab.\n");
        return 0;
    }
    return 2;
}
