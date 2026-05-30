#include "GrammarParser.hpp"
#include "NodeGraph.hpp"   // for Edge definition
#include <algorithm>
#include <queue>
#include <unordered_map>

// ---------------------------------------------------------------------------
// validateEdge
// ---------------------------------------------------------------------------
std::optional<GrammarError>
GrammarParser::validateEdge(const NodeInstance& fromNode,
                             const NodeInstance& toNode,
                             const std::vector<Edge>& existingEdges,
                             bool toIsParam) const {
    const NodeDef& fromDef = defOf(fromNode);
    const NodeDef& toDef   = defOf(toNode);

    // R3 — self-loop
    if (fromNode.id == toNode.id)
        return GrammarError{
            "R3", "Self-connections are not allowed.",
            fromNode.id, toNode.id
        };

    // R1 — Sink has no output
    if (fromDef.outputPorts == 0)
        return GrammarError{
            "R1",
            std::string("\"") + fromDef.label + "\" is a Sink — it has no output port.",
            fromNode.id, toNode.id
        };

    // R2 — Source has no input port.  Si el edge va a un PARAM (no a un
    // input port), R2 no aplica: un Source puede tener parámetros
    // editables y un param-pin acepta una señal modulando ese valor
    // (el edge pisa al widget).  Sin esta excepción, "Sine → param
    // de StepSignal" sería rechazado erróneamente.
    if (!toIsParam && toDef.inputPorts == 0)
        return GrammarError{
            "R2",
            std::string("\"") + toDef.label + "\" is a Source — it has no input port.",
            fromNode.id, toNode.id
        };

    // R4 (full-attribute duplicate) is enforced at NodeGraph::tryAddEdge,
    // which is the only level with access to the proposed edge's port
    // attrs. validateEdge keeps the node-pair duplicate check below as a
    // safety net for single-port nodes — multi-output sources fanning
    // out to multi-input destinations need port-aware comparison, which
    // the NodeGraph layer does explicitly.

    // R5 — the specific input port is already occupied.
    // Count how many existing edges already land on toNode's input
    // ports (ignorando los que van a param-pins, que se cuentan via
    // R6 a nivel de NodeGraph::tryAddEdge), then check whether all
    // input ports are taken (supports multi-input nodes like Summation).
    // Para edges a PARAM, R5 no aplica (la unicidad la garantiza la
    // chequeada exacta del attrId en tryAddEdge).
    if (!toIsParam) {
        int usedPorts = 0;
        for (const auto& e : existingEdges)
            if (e.toNodeId == toNode.id && !attrIsParam(e.toAttrId))
                ++usedPorts;
        if (usedPorts >= toDef.inputPorts)
            return GrammarError{
                "R5",
                std::string("All input ports of \"") + toDef.label
                    + "\" are already connected.",
                fromNode.id, toNode.id
            };
    }

    // Grammar table check (redundant with R1/R2 but explicit).  Si el
    // edge va a un PARAM, la categoría del destino no impone restricción:
    // un Source, un Transformer y un Sink pueden tener params modulados
    // por igual.  El único requisito es que el source no sea Sink (R1
    // ya lo capturó).
    if (toIsParam) return std::nullopt;

    NodeCategory fc = fromDef.category;
    NodeCategory tc = toDef.category;

    // Sink → anything is already caught by R1.
    // anything → Source is already caught by R2.
    // Device se trata como Transformer en las reglas de conexión (tiene
    // inputs y outputs, vive en el medio de la cadena).  La distinción
    // es para el resto del sistema (UI, asset binding), no para la
    // gramática.
    auto isMid = [](NodeCategory c) {
        return c == NodeCategory::Transformer || c == NodeCategory::Device;
    };
    bool valid = (fc == NodeCategory::Source && isMid(tc))                  ||
                 (fc == NodeCategory::Source && tc == NodeCategory::Sink)   ||
                 (isMid(fc)                  && isMid(tc))                  ||
                 (isMid(fc)                  && tc == NodeCategory::Sink);

    if (!valid)
        return GrammarError{
            "R0",
            std::string("Cannot connect ") + fromDef.label + " → " + toDef.label
                + "  (violates S|>T|>Sk rule).",
            fromNode.id, toNode.id
        };

    return std::nullopt;   // valid
}

// ---------------------------------------------------------------------------
// reachable — BFS from every Source; returns true if any Sink is reached.
// ---------------------------------------------------------------------------
bool GrammarParser::reachable(const std::vector<NodeInstance>& nodes,
                               const std::vector<Edge>& edges) const {
    // Direct-addressed lookups keyed by node id. unordered_map allocates
    // per insertion which dominates the runtime at ~256 nodes — flat
    // vectors keep validateGraph well under the 1 ms budget.
    int maxId = 0;
    for (const auto& n : nodes) if (n.id > maxId) maxId = n.id;

    constexpr int8_t kAbsent     = -1;
    constexpr int8_t kSource     = static_cast<int8_t>(NodeCategory::Source);
    constexpr int8_t kSink       = static_cast<int8_t>(NodeCategory::Sink);

    std::vector<int8_t>            cat    (maxId + 1, kAbsent);
    std::vector<std::vector<int>>  adj    (maxId + 1);
    std::vector<bool>              visited(maxId + 1, false);

    for (const auto& n : nodes)
        if (n.id >= 0 && n.id <= maxId)
            cat[n.id] = static_cast<int8_t>(categoryOf(n));

    for (const auto& e : edges)
        if (e.fromNodeId >= 0 && e.fromNodeId <= maxId)
            adj[e.fromNodeId].push_back(e.toNodeId);

    std::queue<int> q;
    for (const auto& n : nodes)
        if (n.id >= 0 && n.id <= maxId && cat[n.id] == kSource) {
            q.push(n.id);
            visited[n.id] = true;
        }

    while (!q.empty()) {
        int cur = q.front(); q.pop();
        if (cur >= 0 && cur <= maxId && cat[cur] == kSink)
            return true;
        if (cur < 0 || cur > maxId) continue;
        for (int next : adj[cur]) {
            if (next < 0 || next > maxId) continue;
            if (!visited[next]) {
                visited[next] = true;
                q.push(next);
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// validateGraph
// ---------------------------------------------------------------------------
GrammarState
GrammarParser::validateGraph(const std::vector<NodeInstance>& nodes,
                              const std::vector<Edge>& edges) const {
    if (nodes.empty()) return GrammarState::Empty;
    if (reachable(nodes, edges)) return GrammarState::Valid;
    return GrammarState::Incomplete;
}

GrammarState GrammarParser::validateGraph(const NodeGraph& g) const {
    // Estado del nivel actual (plano).
    const auto& nodes = g.nodes();
    const auto& edges = g.edges();
    if (nodes.empty()) return GrammarState::Empty;
    const bool selfReachable = reachable(nodes, edges);

    // Recursión: cada SubGraph hijo también debe ser válido.  Si algún
    // hijo es Incomplete (o internamente no llega de su source a su
    // sink), todo el padre se reporta Incomplete — la derivación
    // gramatical no cierra.
    for (const auto& n : nodes) {
        if (!isSubGraphContainer(n.type)) continue;
        const NodeGraph* child = g.subGraphOf(n.id);
        if (!child) continue;   // SubGraph sin contenido es válido vacío
        const GrammarState childState = validateGraph(*child);
        if (childState == GrammarState::Incomplete) return GrammarState::Incomplete;
        // Empty está bien: un SubGraph recién creado sin contenido no
        // invalida al padre (lo más que puede ser es un placeholder).
    }

    return selfReachable ? GrammarState::Valid : GrammarState::Incomplete;
}

// ---------------------------------------------------------------------------
// label
// ---------------------------------------------------------------------------
const char* GrammarParser::label(GrammarState s) {
    switch (s) {
        case GrammarState::Empty:      return "Empty";
        case GrammarState::Incomplete: return "Incomplete";
        case GrammarState::Valid:      return "Valid";
    }
    return "Unknown";
}
