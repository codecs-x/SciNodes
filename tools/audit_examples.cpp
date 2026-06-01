// audit_examples — itera examples/graphs/*.scn, los deserializa con
// R7 ON (default desde v0.1.1), y reporta cuáles tienen aristas
// rechazadas por la gramática.  Sirve para detectar drift entre los
// .scn pre-existentes y el nuevo default de R7.
//
// Uso:  ./audit_examples [examples_dir]
//       examples_dir defaults a ./examples/graphs
#include "core/NodeGraph.hpp"
#include "core/ScnSerializer.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    fs::path root = (argc >= 2) ? fs::path(argv[1])
                                : fs::path("examples/graphs");
    if (!fs::is_directory(root)) {
        std::fprintf(stderr, "audit_examples: no es un directorio: %s\n",
                     root.string().c_str());
        return 2;
    }

    int totalFiles = 0, filesWithRejects = 0, totalRejects = 0;
    for (const auto& ent : fs::directory_iterator(root)) {
        if (!ent.is_regular_file()) continue;
        if (ent.path().extension() != ".scn") continue;
        ++totalFiles;

        std::ifstream f(ent.path());
        std::stringstream buf; buf << f.rdbuf();
        const std::string jsonText = buf.str();

        NodeGraph g;
        ScnPositions pos;
        auto rep = ScnSerializer::deserialize(jsonText, g, pos);

        if (!rep.rejectedEdges.empty() || !rep.unknownTypes.empty()) {
            ++filesWithRejects;
            std::printf("\n%s\n", ent.path().filename().c_str());
            std::printf("  ok=%d  nodes=%d  edges=%d  rejected=%zu\n",
                        (int)rep.ok, g.nodeCount(), g.edgeCount(),
                        rep.rejectedEdges.size());
            for (const auto& re : rep.rejectedEdges) {
                ++totalRejects;
                std::printf("  [%s] node %d -> node %d   %s\n",
                            re.rule.c_str(), re.fromNodeId, re.toNodeId,
                            re.message.c_str());
            }
            for (const auto& ut : rep.unknownTypes)
                std::printf("  unknown type: %s\n", ut.c_str());
        }
    }

    std::printf("\n=== resumen ===\n");
    std::printf("  archivos examinados: %d\n", totalFiles);
    std::printf("  con rechazos:        %d\n", filesWithRejects);
    std::printf("  aristas rechazadas:  %d\n", totalRejects);
    return filesWithRejects > 0 ? 1 : 0;
}
