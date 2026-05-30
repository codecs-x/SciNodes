#pragma once
#include "GrammarParser.hpp"
#include "NodeGraph.hpp"
#include <string>
#include <unordered_map>
#include <vector>

// -----------------------------------------------------------------------
// ScnSerializer — read/write SciNodes Canvas (.scn) JSON files.
//
// File format (scnodes_version "0.3"):
//
//   {
//     "scnodes_version": "0.3",
//     "next_node_id": 7,
//     "nodes": [
//       { "id": 1, "type": "VoltageSource",
//         "position": [120.0, 80.0],
//         "params": { "Voltage": 12.0, "Int. Resistance": 0.1 } }
//     ],
//     "edges": [
//       { "id": 1, "from_node": 1, "to_node": 2, "to_port": 0 }
//     ]
//   }
//
// All public methods are stateless. Positions are owned by the caller
// (NodeCanvas) as a separate map keyed by node id; the serializer
// reads and writes them but the core NodeInstance type stays pure.
// -----------------------------------------------------------------------

// Compatibility aliases: posiciones ahora viven en `NodeInstance::position`,
// pero los call-sites legacy todavía usan ScnVec2/ScnPositions para hablar
// del top-level antes/después del refactor.  NodeCanvas mantiene su
// `m_positions` para no romper el flujo de undo/redo de un solo nivel.
using ScnVec2 = NodePos;
using ScnPositions = std::unordered_map<int, ScnVec2>;

struct RejectedEdge {
    int fromNodeId = -1;
    int toNodeId   = -1;
    std::string rule;
    std::string message;
};

struct LoadReport {
    std::string version;                    // version field from the file
    bool        ok = false;                 // true iff file parsed without fatal errors
    std::string fatalError;                 // populated if ok == false

    std::vector<std::string> unknownTypes;  // node entries with unrecognised "type"
    std::vector<RejectedEdge> rejectedEdges;

    int nodesLoaded   = 0;
    int edgesLoaded   = 0;
    GrammarState finalState = GrammarState::Empty;

    bool hasViolations() const {
        return !unknownTypes.empty() || !rejectedEdges.empty();
    }
};

class ScnSerializer {
public:
    // 0.5 — adds root-level `objects` array para el catálogo de modelos
    // 3D importados (paso 3 del refactor de escena, ver
    // `doc/3d_scene_graph_design.md` §5).  0.3 y 0.4 siguen cargando:
    // un .scn sin "objects" se interpreta como catálogo vacío.
    static constexpr const char* FILE_VERSION = "0.5";

    // Serialize to a JSON string (indented for human readability + git diffs).
    static std::string
    serialize(const NodeGraph& graph,
              const ScnPositions& positions);

    // Convenience: write to a file. Returns false on I/O error.
    static bool
    saveToFile(const std::string& path,
               const NodeGraph& graph,
               const ScnPositions& positions);

    // Deserialize into the given graph (the graph is reset first).
    // Edges are re-validated through NodeGraph::tryAddEdge — any that fail
    // are dropped and recorded in the report.
    static LoadReport
    deserialize(const std::string& jsonText,
                NodeGraph& graph,
                ScnPositions& positions);

    // Convenience: read from a file. Returns a fatal-error report if the
    // file cannot be opened or parsed.
    static LoadReport
    loadFromFile(const std::string& path,
                 NodeGraph& graph,
                 ScnPositions& positions);
};
