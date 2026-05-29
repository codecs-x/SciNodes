#pragma once
#include "Edge.hpp"
#include "NodeInstance.hpp"
#include <optional>
#include <string>
#include <vector>

class NodeGraph;   // fwd — el overload recursivo evita arrastrar el header

// -----------------------------------------------------------------------
// GrammarError — returned when a connection violates a production rule.
// -----------------------------------------------------------------------
struct GrammarError {
    std::string rule;       // symbolic rule name, e.g. "R1"
    std::string message;    // human-readable explanation
    int fromNodeId = -1;
    int toNodeId   = -1;
};

// -----------------------------------------------------------------------
// GrammarState — overall validity of the whole graph.
// -----------------------------------------------------------------------
enum class GrammarState {
    Empty,        // no nodes at all
    Incomplete,   // has nodes but no complete Source → ... → Sink path
    Valid         // at least one complete pipeline exists
};

// -----------------------------------------------------------------------
// GrammarParser — stateless validator.
//
// Connection rule table (from_category × to_category):
//
//             │ Source │ Transformer │ Sink
//  ───────────┼────────┼─────────────┼──────
//  Source     │   ✗    │      ✓      │  ✓
//  Transformer│   ✗    │      ✓      │  ✓
//  Sink       │   ✗    │      ✗      │  ✗
//
// Rules enforced per edge:
//   R1 — Sinks have no output port
//   R2 — Sources have no input port
//   R3 — Self-connections are not allowed
//   R4 — Duplicate connection already exists
//   R5 — Input port already has an incoming edge
//   R6 — Port-type compatibility: from.portType must equal to.portType.
//        Materializa la separación entre los dos sub-lenguajes del grafo
//        (Signal vs Geometry).  Ver `doc/3d_scene_graph_design.md` §2.
// -----------------------------------------------------------------------
class GrammarParser {
public:
    // Validate a single proposed edge.
    //
    // `toIsParam` = true cuando el destino del edge es un PARAM-PIN (no
    // un input port).  Las reglas R2, R5 y la tabla de categorías se
    // relajan en ese caso — un edge a param solo necesita un source
    // válido (no Sink); la unicidad por param-pin la garantiza la
    // chequeada exacta de attrId en NodeGraph::tryAddEdge.
    //
    // `fromPortIdx` / `toPortIdx` identifican el puerto de salida del
    // origen y el puerto de entrada del destino.  Se usan para resolver
    // el sub-lenguaje (Signal/Geometry) en R6.  Defaultean a 0 para
    // mantener compat con call sites legacy de un solo puerto.  Si
    // `toIsParam` es true, `toPortIdx` se interpreta como índice del
    // param (los params son siempre Signal).
    //
    // Returns nullopt if valid; a GrammarError describing the violation otherwise.
    std::optional<GrammarError>
    validateEdge(const NodeInstance& fromNode,
                 const NodeInstance& toNode,
                 const std::vector<Edge>& existingEdges,
                 bool toIsParam = false,
                 int  fromPortIdx = 0,
                 int  toPortIdx   = 0) const;

    // Validate the whole graph and return its overall state.  Esta
    // sobrecarga es PLANA — sólo mira los nodos/aristas dados, no recurre
    // en sub-grafos.  La usan los tests para inyectar grafos sintéticos
    // y benchmarks que no involucran SubGraphs.
    GrammarState
    validateGraph(const std::vector<NodeInstance>& nodes,
                  const std::vector<Edge>& edges) const;

    // Validate the whole graph RECURSIVAMENTE: valida el nivel propio
    // (igual que la sobrecarga plana) y, para cada SubGraph encontrado,
    // valida su grafo hijo del mismo modo.  El grafo completo es Valid
    // sólo si cada nivel (raíz + cada SubGraph anidado) lo es.
    //
    // Esto materializa el espíritu recursivo de la gramática: un
    // SubGraph es un símbolo no-terminal cuya expansión también debe
    // ser una derivación válida — análogo a `expr = term | expr + term`
    // donde cada `term` (sub-grafo) tiene que respetar la misma gramática.
    GrammarState validateGraph(const NodeGraph& g) const;

    // Human-readable label for the StatusBar.
    static const char* label(GrammarState s);

private:
    // BFS reachability: can we reach a Sink from any Source via edges?
    bool reachable(const std::vector<NodeInstance>& nodes,
                   const std::vector<Edge>& edges) const;
};
