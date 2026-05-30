// =============================================================================
// smoke.cpp — Prueba de humo para call_scilab.
//
// Objetivo: validar que el entorno actual permite enlazar contra libscilab,
// inicializar Scilab desde C++, ejecutar una expresión trivial y recuperar
// el resultado en C++.
//
// Criterio de éxito:
//   - El ejecutable compila y enlaza.
//   - StartScilab() retorna sin error.
//   - SendScilabJob("c = 2 + 3;") retorna 0.
//   - El programa imprime "c = 5" y termina con código 0.
//
// Si cualquier paso de los anteriores falla en menos de 4 horas de trabajo,
// el experimento call_scilab se aborta y se pasa al plan B (API propia).
//
// Antes de compilar:
//   export SCI=/opt/scilab-2026.0.1/share/scilab        (ajusta a tu instalación)
//   export LD_LIBRARY_PATH=/opt/scilab-2026.0.1/lib/scilab:$LD_LIBRARY_PATH
//
// Compilar:
//   cd experiments/call_scilab && mkdir -p build && cd build
//   cmake .. && make
//
// Ejecutar:
//   ./smoke
// =============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---- Headers de Scilab ----
// La ruta exacta depende de tu instalación. Estos suelen estar en
// <prefix>/include/scilab/.  CMake ya añade -I al directorio correcto.
extern "C" {
    #include <call_scilab.h>
    #include <api_scilab.h>
}

// -----------------------------------------------------------------------------
// Paso 1 — Inicializar Scilab.
// StartScilab(SCIpath, ScilabStartup, StackSize):
//   - Si SCIpath es NULL, Scilab lee SCI del entorno.
//   - ScilabStartup puede ser NULL (no ejecutar nada al iniciar).
//   - StackSize en doubles; 0 usa el valor por defecto.
// Retorna TRUE (no-cero) si OK, FALSE si falla.
// -----------------------------------------------------------------------------
static bool initScilab() {
    const char* sci = std::getenv("SCI");
    if (!sci) {
        std::fprintf(stderr,
            "[ERROR] La variable de entorno SCI no está definida.\n"
            "Ejemplo: export SCI=/opt/scilab-2026.0.1/share/scilab\n");
        return false;
    }
    std::fprintf(stderr, "[INFO]  SCI = %s\n", sci);

    if (!StartScilab(const_cast<char*>(sci), nullptr, 0)) {
        std::fprintf(stderr, "[ERROR] StartScilab() retornó FALSE.\n");
        return false;
    }
    std::fprintf(stderr, "[OK]    StartScilab\n");
    return true;
}

// -----------------------------------------------------------------------------
// Paso 2 — Enviar un comando a Scilab.
// SendScilabJob(char* command) devuelve 0 en éxito, !=0 en fallo.
// -----------------------------------------------------------------------------
static bool runJob(const char* job) {
    if (SendScilabJob(const_cast<char*>(job)) != 0) {
        std::fprintf(stderr, "[ERROR] SendScilabJob falló: %s\n", job);
        return false;
    }
    std::fprintf(stderr, "[OK]    SendScilabJob: %s\n", job);
    return true;
}

// -----------------------------------------------------------------------------
// Paso 3 — Leer una variable escalar de vuelta a C++.
//
// Scilab 2026 expone dos APIs en paralelo:
//   - Moderno: scilab_getVar / scilab_getDouble (api_double.h). Pensado para
//     gateways (toolboxes llamadas desde Scilab); requiere un scilabEnv que
//     en contexto embedded no está accesible.
//   - Legacy "stack": getNamedScalarDouble (api_stack_double.h). Toma un
//     puntero void* opaco (pvApiCtx). En embedding (call_scilab) basta con
//     pasar NULL — la función opera sobre el estado global de la sesión.
//
// Usamos el legacy porque es el camino documentado para call_scilab.
// -----------------------------------------------------------------------------
extern "C" {
    #include <api_stack_double.h>
}

static bool readScalarDouble(const char* name, double* out) {
    int rc = getNamedScalarDouble(nullptr,
                                  const_cast<char*>(name),
                                  out);
    if (rc != 0) {
        std::fprintf(stderr,
            "[ERROR] getNamedScalarDouble(\"%s\") rc=%d\n", name, rc);
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Programa principal.
// -----------------------------------------------------------------------------
int main(int argc, char** argv) {
    (void)argc; (void)argv;

    std::fprintf(stderr,
        "================================================================\n"
        " SciNodes — call_scilab smoke test\n"
        " Esperado: c = 5  (entero), exit code 0\n"
        "================================================================\n");

    if (!initScilab()) return 1;

    if (!runJob("c = 2 + 3;")) {
        TerminateScilab(nullptr);
        return 1;
    }

    double c = 0.0;
    if (!readScalarDouble("c", &c)) {
        TerminateScilab(nullptr);
        return 1;
    }

    std::fprintf(stdout, "c = %g\n", c);
    std::fprintf(stderr, "[OK]    Lectura del escalar: c = %g\n", c);

    TerminateScilab(nullptr);
    std::fprintf(stderr, "[OK]    TerminateScilab\n");

    // Validación final: el resultado debe ser exactamente 5.
    if (c != 5.0) {
        std::fprintf(stderr, "[FAIL]  Se esperaba 5, se obtuvo %g\n", c);
        return 2;
    }

    std::fprintf(stderr, "[PASS]  Smoke test exitoso.\n");
    return 0;
}
