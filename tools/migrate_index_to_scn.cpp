// =============================================================================
// migrate_index_to_scn — migración one-shot del manifest legacy
// `examples/graphs/index.json` hacia metadata embebida en cada `.scn`.
//
// Motivación: el principio de documentación descentralizada exige que
// la descripción de un experimento viva CON el experimento, no en un
// índice central que puede desincronizarse.  Mientras existió el
// `index.json`, los `.scn` no se describían a sí mismos; tras esta
// migración cada `.scn` carga su propio título/descripción/tags y el
// manifest queda obsoleto.
//
// Comportamiento:
//   - Lee `examples/graphs/index.json` (o el directorio pasado por argv).
//   - Para cada entry: carga el `.scn` con ScnSerializer, copia los
//     campos legacy a la metadata root del grafo SI los campos están
//     vacíos (no pisa metadata ya embebida), y reescribe el archivo.
//   - Falla ruidosamente si un `.scn` no se puede cargar o tiene
//     edges rechazadas — esos casos requieren intervención manual.
//   - NO borra `index.json` automáticamente — el usuario lo hace tras
//     revisar el diff (`git diff examples/graphs/*.scn`).
//
// Compilar (manual, sin CMake):
//   g++ -std=c++17 -I src -I build/_deps/nlohmann_json-src/include \
//       tools/migrate_index_to_scn.cpp \
//       src/core/NodeGraph.cpp src/core/NodeInstance.cpp \
//       src/core/NodeType.cpp src/core/GrammarParser.cpp \
//       src/core/UndoRedoStack.cpp src/core/ScnSerializer.cpp \
//       src/core/CustomNodeRegistry.cpp \
//       -o build/migrate_index_to_scn
// =============================================================================

#include "../src/core/NodeGraph.hpp"
#include "../src/core/ScnSerializer.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

struct LegacyEntry {
    std::string              file;
    std::string              id;
    std::string              title;
    std::string              description;
    std::vector<std::string> tags;
};

static std::vector<LegacyEntry> readIndex(const fs::path& indexFile) {
    std::vector<LegacyEntry> out;
    std::ifstream in(indexFile);
    if (!in) {
        std::fprintf(stderr, "ERROR: no pude abrir %s\n",
                     indexFile.string().c_str());
        return out;
    }
    std::stringstream ss; ss << in.rdbuf();
    json j;
    try { j = json::parse(ss.str()); }
    catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR: index.json inválido: %s\n", e.what());
        return out;
    }
    if (!j.is_object() || !j.contains("examples")) return out;
    for (const auto& je : j["examples"]) {
        if (!je.is_object()) continue;
        LegacyEntry e;
        if (je.contains("file")        && je["file"].is_string())        e.file        = je["file"].get<std::string>();
        if (je.contains("id")          && je["id"].is_string())          e.id          = je["id"].get<std::string>();
        if (je.contains("title")       && je["title"].is_string())       e.title       = je["title"].get<std::string>();
        if (je.contains("description") && je["description"].is_string()) e.description = je["description"].get<std::string>();
        if (je.contains("tags") && je["tags"].is_array())
            for (const auto& t : je["tags"])
                if (t.is_string()) e.tags.push_back(t.get<std::string>());
        if (!e.file.empty()) out.push_back(std::move(e));
    }
    return out;
}

int main(int argc, char** argv) {
    const fs::path dir = (argc > 1) ? fs::path(argv[1])
                                    : fs::path("examples/graphs");
    const fs::path indexFile = dir / "index.json";

    if (!fs::exists(indexFile)) {
        std::fprintf(stderr,
            "ERROR: %s no existe.  Migración no requerida o ejecutas "
            "el tool fuera del repo root.\n", indexFile.string().c_str());
        return 1;
    }

    const auto entries = readIndex(indexFile);
    std::printf("Leídas %zu entries de %s\n", entries.size(),
                indexFile.string().c_str());

    int migrated = 0, skipped = 0, failed = 0, alreadyEmbedded = 0;
    for (const auto& e : entries) {
        const fs::path scn = dir / e.file;
        if (!fs::exists(scn)) {
            std::fprintf(stderr, "  SKIP %s — no existe en disco\n",
                         e.file.c_str());
            ++skipped; continue;
        }

        NodeGraph    g;
        ScnPositions pos;
        LoadReport rep = ScnSerializer::loadFromFile(scn.string(), g, pos);
        if (!rep.ok) {
            std::fprintf(stderr, "  FAIL %s — load error: %s\n",
                         e.file.c_str(), rep.fatalError.c_str());
            ++failed; continue;
        }
        if (!rep.rejectedEdges.empty()) {
            std::fprintf(stderr,
                "  FAIL %s — %zu edges rechazadas en load; reparar antes "
                "de migrar.\n", e.file.c_str(), rep.rejectedEdges.size());
            ++failed; continue;
        }

        const bool hadId    = !g.id().empty();
        const bool hadTitle = !g.title().empty();
        const bool hadDesc  = !g.description().empty();
        const bool hadTags  = !g.tags().empty();

        // Sólo llenamos lo vacío — re-correr la migración es idempotente
        // y no destruye metadata escrita a mano después del primer pase.
        if (!hadId)    g.setId(e.id);
        if (!hadTitle) g.setTitle(e.title);
        if (!hadDesc)  g.setDescription(e.description);
        if (!hadTags)  g.setTags(e.tags);

        if (hadId && hadTitle && hadDesc && hadTags) {
            std::printf("  SKIP %s — ya tiene metadata embebida completa\n",
                        e.file.c_str());
            ++alreadyEmbedded; continue;
        }

        if (!ScnSerializer::saveToFile(scn.string(), g, pos)) {
            std::fprintf(stderr, "  FAIL %s — no pude escribir\n",
                         e.file.c_str());
            ++failed; continue;
        }
        std::printf("  OK   %s — id=\"%s\"  title=\"%s\"  tags=%zu  desc=%zuc\n",
                    e.file.c_str(),
                    g.id().c_str(),
                    g.title().c_str(),
                    g.tags().size(),
                    g.description().size());
        ++migrated;
    }

    std::printf("\nResumen: %d migrados, %d ya embebidos, %d saltados, %d fallidos.\n",
                migrated, alreadyEmbedded, skipped, failed);
    if (failed == 0 && skipped == 0) {
        std::printf("Listo.  Verifica `git diff examples/graphs/*.scn` y luego:\n"
                    "  git rm %s\n", indexFile.string().c_str());
    }
    return failed == 0 ? 0 : 1;
}
