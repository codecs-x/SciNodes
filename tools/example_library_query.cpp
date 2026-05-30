// =============================================================================
// example_library_query — CLI diagnóstico de LinearExampleLibrary.
//
// Apunta el backend al directorio `examples/graphs/` real y muestra:
//   - Cada entry descubierta (id, title, tags).
//   - Todos los tags únicos (allTags).
//   - Tres búsquedas representativas con scoring + snippet.
//
// Útil para "ver qué piensa la biblioteca" sin levantar la UI todavía
// (el panel ExamplesBrowser sigue usando index.json directamente — la
// migración a IExampleLibrary es un paso aparte).
//
// Compilar (manual, sin CMake):
//   g++ -std=c++17 -I src -I build/_deps/nlohmann_json-src/include \
//       tools/example_library_query.cpp \
//       src/core/LinearExampleLibrary.cpp \
//       -o build/example_library_query
// =============================================================================

#include "../src/core/LinearExampleLibrary.hpp"

#include <cstdio>
#include <iostream>
#include <string>

using scinodes::LinearExampleLibrary;
using scinodes::SearchQuery;

static void runQuery(const LinearExampleLibrary& lib,
                     const std::string& text,
                     const std::vector<std::string>& mustTags = {}) {
    SearchQuery q; q.text = text; q.mustTags = mustTags; q.limit = 5;
    auto hits = lib.search(q);

    std::printf("\n--- search(text=\"%s\"", text.c_str());
    if (!mustTags.empty()) {
        std::printf(", mustTags=[");
        for (size_t i = 0; i < mustTags.size(); ++i)
            std::printf("%s%s", i ? "," : "", mustTags[i].c_str());
        std::printf("]");
    }
    std::printf(") → %zu hit(s)\n", hits.size());

    int rank = 1;
    for (const auto& h : hits) {
        std::printf("  [%d] %-10s score=%.1f  matched=[", rank++,
                    h.entry.id.c_str(), h.score);
        for (size_t i = 0; i < h.matchedFields.size(); ++i)
            std::printf("%s%s", i ? "," : "", h.matchedFields[i].c_str());
        std::printf("]\n");
        std::printf("        %s\n", h.entry.title.c_str());
        if (!h.snippet.empty())
            std::printf("        snippet: %s\n", h.snippet.c_str());
    }
}

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : "examples/graphs";

    LinearExampleLibrary lib(dir);
    lib.refresh();

    std::printf("=== LinearExampleLibrary @ %s ===\n", dir.c_str());
    std::printf("Entries descubiertos: %zu\n\n", lib.entries().size());
    for (const auto& e : lib.entries()) {
        std::printf("  %-10s %s\n", e.id.c_str(), e.title.c_str());
        if (!e.tags.empty()) {
            std::printf("           tags:");
            for (const auto& t : e.tags) std::printf(" #%s", t.c_str());
            std::putchar('\n');
        }
    }

    std::printf("\nallTags (deduplicados, ordenados):\n  ");
    bool first = true;
    for (const auto& t : lib.allTags()) {
        if (!first) std::printf(", ");
        std::printf("%s", t.c_str());
        first = false;
    }
    std::putchar('\n');

    // Búsquedas representativas — exactamente el tipo de query que el
    // panel Obsidian-like va a hacer.
    runQuery(lib, "pid");
    runQuery(lib, "ogata");
    runQuery(lib, "motor windup");
    runQuery(lib, "",        {"robot"});         // sólo facetas, sin texto
    runQuery(lib, "motor",   {"control"});       // texto + faceta AND

    return 0;
}
