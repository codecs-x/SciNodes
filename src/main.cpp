#include "app/AppWindow.hpp"
#include <cstdio>
#include <stdexcept>

int main() {
    try {
        AppWindow app;
        app.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[SciNodes] Fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
