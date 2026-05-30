// =============================================================================
// inverse_kinematics.cpp — Cinemática inversa de dos eslabones vía call_scilab.
//
// Replica el escenario 9 de test_integration.cpp.  Aquí no hay ODE: cada
// "tick" es solo aritmética con atan/sqrt/clamp.  Sirve para validar dos
// cosas:
//
//   1. La API maneja correctamente cómputos sin estado integrado
//      (no se usa ode(), solo expresiones).
//   2. Se pueden leer múltiples variables de vuelta a C++ en una sola
//      pasada por la sesión Scilab.
//
// Casos:
//   (x, y) = (0.5, 0)    → θ₁ = 0,     θ₂ = 0      (brazo extendido)
//   (x, y) = (0.3, 0.2)  → θ₁ = 0,     θ₂ = π/2    (codo en ángulo recto)
// =============================================================================

#include <cstdio>
#include <cstdlib>
#include <cmath>

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

struct TestCase {
    const char* label;
    double x, y, L1, L2;
    double expectedTheta1, expectedTheta2;
};

static bool runCase(const TestCase& tc) {
    char job[512];
    std::snprintf(job, sizeof(job),
        "x = %.6f; y = %.6f; L1 = %.6f; L2 = %.6f;"
        "c2 = max(min((x^2 + y^2 - L1^2 - L2^2) / (2*L1*L2), 1), -1);"
        "s2 = sqrt(1 - c2^2);"
        "theta2 = atan(s2, c2);"
        "theta1 = atan(y, x) - atan(L2*s2, L1 + L2*c2);",
        tc.x, tc.y, tc.L1, tc.L2);
    if (!runJob(job)) return false;

    double th1 = 0.0, th2 = 0.0;
    if (!readDouble("theta1", &th1)) return false;
    if (!readDouble("theta2", &th2)) return false;

    const double tol = 1e-4;
    bool ok1 = std::fabs(th1 - tc.expectedTheta1) <= tol;
    bool ok2 = std::fabs(th2 - tc.expectedTheta2) <= tol;

    std::fprintf(stdout,
        "%-25s  θ₁ = %+8.5f (esp %+8.5f) %s   θ₂ = %+8.5f (esp %+8.5f) %s\n",
        tc.label,
        th1, tc.expectedTheta1, ok1 ? "OK" : "FAIL",
        th2, tc.expectedTheta2, ok2 ? "OK" : "FAIL");

    return ok1 && ok2;
}

int main() {
    std::fprintf(stderr,
        "================================================================\n"
        " SciNodes — call_scilab: cinemática inversa de dos eslabones\n"
        " Escenario 9 de test_integration.cpp\n"
        "================================================================\n");

    const char* sci = std::getenv("SCI");
    if (!sci) { std::fprintf(stderr, "[ERROR] SCI no definido.\n"); return 1; }

    if (!StartScilab(const_cast<char*>(sci), nullptr, 0)) {
        std::fprintf(stderr, "[ERROR] StartScilab\n"); return 1;
    }
    std::fprintf(stderr, "[OK]    StartScilab\n\n");

    TestCase cases[] = {
        { "brazo extendido (0.5, 0)", 0.5, 0.0, 0.3, 0.2, 0.0,        0.0     },
        { "codo recto (0.3, 0.2)",    0.3, 0.2, 0.3, 0.2, 0.0,        M_PI/2  },
    };

    bool ok = true;
    for (const auto& tc : cases) {
        if (!runCase(tc)) ok = false;
    }

    TerminateScilab(nullptr);

    std::fprintf(stderr, "\n%s\n",
        ok ? "[PASS]  Inverse kinematics reproducible vía call_scilab."
           : "[FAIL]  Al menos un caso no coincide.");
    return ok ? 0 : 2;
}
