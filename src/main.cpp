#include "app/AppWindow.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

// Uso:
//   ./SciNodes                 Arranca con grafo vacío.
//   ./SciNodes archivo.scn     Abre el grafo al iniciar.
//   ./SciNodes -h | --help     Imprime esta ayuda.
//
// Similar a `code archivo.cpp` o `scilab script.sce`.
int main(int argc, char** argv) {
    // Mitigación parcial de env contaminada por snaps.
    //
    // Cuando SciNodes se lanza desde la terminal de un editor instalado
    // como snap (VS Code snap, etc.), el entorno hereda LD_LIBRARY_PATH
    // y rutas GTK/GIO apuntando al sandbox del snap.  Algunos procesos
    // hijos (scilab-cli, ffmpeg) se recuperan si vaciamos esas variables
    // aquí.  zenity sigue fallando porque el mount namespace del snap
    // redirige incluso sin LD_LIBRARY_PATH; ese caso queda como trabajo
    // futuro (file picker propio basado en ImGui).
    ::unsetenv("LD_LIBRARY_PATH");
    ::unsetenv("LD_PRELOAD");
    ::unsetenv("GIO_MODULE_DIR");
    ::unsetenv("GSETTINGS_SCHEMA_DIR");
    ::unsetenv("GTK_PATH");
    ::unsetenv("GTK_EXE_PREFIX");
    ::unsetenv("GTK_IM_MODULE_FILE");

    std::string fileToOpen;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-h") == 0 ||
            std::strcmp(argv[i], "--help") == 0) {
            std::printf(
                "Uso: %s [archivo.scn]\n"
                "  archivo.scn   Grafo a abrir al iniciar (opcional).\n"
                "  -h, --help    Mostrar esta ayuda.\n",
                argv[0]);
            return 0;
        }
        if (argv[i][0] != '-' && fileToOpen.empty()) {
            fileToOpen = argv[i];
        } else {
            std::fprintf(stderr, "[SciNodes] Argumento ignorado: %s\n", argv[i]);
        }
    }

    try {
        AppWindow app;
        if (!fileToOpen.empty()) app.openGraphFromCli(fileToOpen);
        app.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[SciNodes] Fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
