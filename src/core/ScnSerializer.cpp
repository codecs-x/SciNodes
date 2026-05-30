#include "ScnSerializer.hpp"
#include "NodeInstance.hpp"
#include "NodeType.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

using json = nlohmann::json;

// Forward — declared as `static` later in this file.  These helpers
// recurse the serializer through nested SubGraph contents.
static json serializeGraphBody(const NodeGraph& graph,
                               const ScnPositions* topLevelPositions);
static void deserializeGraphBody(const json& j, NodeGraph& graph,
                                 ScnPositions* topLevelPositions,
                                 LoadReport& report);

// ===========================================================================
// serialize — escribe nodes/edges/positions recursivamente para SubGraphs.
// El esquema lleva ahora versión "0.4": cada nodo `SubGraph` lleva un
// campo extra `subgraph` con la misma estructura (nodes/edges/...) del
// grafo padre.  La versión 0.3 sin `subgraph` se sigue cargando.
// ===========================================================================
std::string ScnSerializer::serialize(const NodeGraph& graph,
                                     const ScnPositions& positions) {
    json j = serializeGraphBody(graph, &positions);
    j["scnodes_version"] = FILE_VERSION;
    return j.dump(2) + "\n";
}

static json serializeGraphBody(const NodeGraph& graph,
                               const ScnPositions* topLevelPositions) {
    json j;
    int maxNodeId = 0;
    for (const auto& n : graph.nodes())
        if (n.id > maxNodeId) maxNodeId = n.id;
    j["next_node_id"] = maxNodeId + 1;

    json jnodes = json::array();
    for (const auto& n : graph.nodes()) {
        json jn;
        jn["id"]   = n.id;
        jn["type"] = typeName(n.type);

        // Position priority: explicit ScnPositions override (used by the
        // top-level call, where NodeCanvas has the live imnodes positions)
        // > n.position (model storage, populated for nested levels).
        NodePos p = n.position;
        if (topLevelPositions) {
            auto it = topLevelPositions->find(n.id);
            if (it != topLevelPositions->end()) p = it->second;
        }
        jn["position"] = json::array({ p.x, p.y });

        json jp = json::object();
        for (const auto& [name, value] : n.params)
            jp[name] = value;
        jn["params"] = jp;

        if (!n.assetPath.empty())
            jn["asset_path"] = n.assetPath;

        if (!n.stringParams.empty()) {
            json js = json::object();
            for (const auto& [k, v] : n.stringParams) js[k] = v;
            jn["string_params"] = js;
        }

        // Recursión: si es SubGraph y tiene grafo hijo, emitir su contenido
        // bajo "subgraph".  Positions del hijo viven en n.position de sus
        // propios nodos (NodeCanvas las sincroniza antes de save).
        if (isSubGraphContainer(n.type)) {
            if (const NodeGraph* child = graph.subGraphOf(n.id)) {
                jn["subgraph"] = serializeGraphBody(*child, nullptr);
            }
        }

        jnodes.push_back(jn);
    }
    j["nodes"] = jnodes;

    json jedges = json::array();
    for (const auto& e : graph.edges()) {
        json je;
        je["id"]        = e.id;
        je["from_node"] = e.fromNodeId;
        je["to_node"]   = e.toNodeId;
        je["from_port"] = attrOutputPort(e.fromAttrId);
        je["to_port"]   = attrInputPort(e.toAttrId);
        jedges.push_back(je);
    }
    j["edges"] = jedges;

    return j;
}

bool ScnSerializer::saveToFile(const std::string& path,
                               const NodeGraph& graph,
                               const ScnPositions& positions) {
    std::ofstream f(path);
    if (!f) return false;
    f << serialize(graph, positions);
    return static_cast<bool>(f);
}

// ===========================================================================
// deserialize
// ===========================================================================
LoadReport ScnSerializer::deserialize(const std::string& jsonText,
                                      NodeGraph& graph,
                                      ScnPositions& positions) {
    LoadReport report;
    positions.clear();
    graph.restoreSnapshot(GraphSnapshot{});

    json j;
    try {
        j = json::parse(jsonText);
    } catch (const std::exception& e) {
        report.fatalError = std::string("JSON parse error: ") + e.what();
        return report;
    }

    if (!j.is_object()) {
        report.fatalError = "Top-level JSON value is not an object.";
        return report;
    }

    report.version = j.value("scnodes_version", "");
    if (!report.version.empty() &&
        report.version != FILE_VERSION &&
        report.version != "0.3") {
        // Reconocemos 0.3 (sin SubGraphs) y 0.4 (con SubGraphs).  Otras
        // versiones se cargan en best-effort y se reporta el mismatch.
        report.unknownTypes.push_back("(version mismatch: file is " +
                                      report.version + ", expected " +
                                      FILE_VERSION + ")");
    }

    deserializeGraphBody(j, graph, &positions, report);
    report.finalState = graph.grammarState();
    report.ok = true;
    return report;
}

static void deserializeGraphBody(const json& j, NodeGraph& graph,
                                 ScnPositions* topLevelPositions,
                                 LoadReport& report) {
    GraphSnapshot snap;
    snap.nextNodeId = j.value("next_node_id", 1);
    snap.nextEdgeId = 1;

    // Mapping from declared id → child subgraph JSON, processed after the
    // restoreSnapshot so the parent already has the SubGraph instance.
    std::unordered_map<int, json> deferredChildren;

    if (j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& jn : j["nodes"]) {
            std::string tname = jn.value("type", "");
            auto t = typeFromName(tname);
            if (!t) {
                report.unknownTypes.push_back(tname);
                continue;
            }
            int id = jn.value("id", 0);
            if (id <= 0) {
                report.unknownTypes.push_back(tname + " (invalid id)");
                continue;
            }

            NodeInstance n = makeNode(id, *t);

            if (jn.contains("params") && jn["params"].is_object())
                for (auto it = jn["params"].begin(); it != jn["params"].end(); ++it)
                    if (it.value().is_number())
                        n.params[it.key()] = it.value().get<double>();

            if (jn.contains("asset_path") && jn["asset_path"].is_string())
                n.assetPath = jn["asset_path"].get<std::string>();

            if (jn.contains("string_params") && jn["string_params"].is_object())
                for (auto it = jn["string_params"].begin();
                     it != jn["string_params"].end(); ++it)
                    if (it.value().is_string())
                        n.stringParams[it.key()] = it.value().get<std::string>();

            if (jn.contains("position") && jn["position"].is_array() &&
                jn["position"].size() >= 2 &&
                jn["position"][0].is_number() &&
                jn["position"][1].is_number()) {
                n.position = NodePos{
                    jn["position"][0].get<float>(),
                    jn["position"][1].get<float>()
                };
                if (topLevelPositions)
                    (*topLevelPositions)[id] = n.position;
            }

            // SubGraph: capturar el bloque `subgraph` para procesarlo tras
            // el restoreSnapshot (necesitamos que el NodeGraph ya tenga
            // este nodo registrado para llamar addSubGraphNode/subGraphOf).
            if (isSubGraphContainer(n.type) &&
                jn.contains("subgraph") && jn["subgraph"].is_object()) {
                deferredChildren[id] = jn["subgraph"];
            }

            snap.nodes.push_back(n);
            ++report.nodesLoaded;
            if (id >= snap.nextNodeId) snap.nextNodeId = id + 1;
        }
    }

    graph.restoreSnapshot(snap);

    // Reconstruir grafos hijos *después* del restoreSnapshot.  Como
    // addSubGraphNode crea sus propios stubs default que el child
    // serializado puede no tener, lo populamos directamente desde el
    // JSON con un NodeGraph fresco y lo cargamos en el side-table del
    // padre vía operator=.
    for (auto& [parentId, childJson] : deferredChildren) {
        NodeGraph childGraph;
        // Cargar el contenido del child en un NodeGraph fresco.  No nos
        // interesan sus positions externas — todas las posiciones del
        // child viven en n.position de cada NodeInstance.
        LoadReport childReport;
        deserializeGraphBody(childJson, childGraph, nullptr, childReport);

        // Necesitamos que el side-table del padre tenga este child.  La
        // forma más limpia: crear el SubGraph con addSubGraphNode habría
        // sido si no tuviéramos ya el nodo en el snapshot — pero ya lo
        // tenemos.  Atajamos: usar `addSubGraphNode` para crear el slot
        // y luego sobreescribir el grafo hijo y eliminar la duplicación.
        // Pero crearía un nodo extra.  Mejor: insertar el child al
        // side-table directamente vía un setter público.
        graph.installSubGraph(parentId, std::move(childGraph));
        graph.recomputeSubGraphPorts(parentId);

        // Propagar reports del child al padre (acumulativo).
        for (auto& u : childReport.unknownTypes) report.unknownTypes.push_back(u);
        for (auto& re : childReport.rejectedEdges) report.rejectedEdges.push_back(re);
        report.nodesLoaded += childReport.nodesLoaded;
        report.edgesLoaded += childReport.edgesLoaded;
    }

    // Edges (re-validated through tryAddEdge).
    if (j.contains("edges") && j["edges"].is_array()) {
        for (const auto& je : j["edges"]) {
            int from   = je.value("from_node", 0);
            int to     = je.value("to_node",   0);
            int fromP  = je.value("from_port", 0);
            int toPort = je.value("to_port",   0);

            const NodeInstance* fromN = graph.findNode(from);
            const NodeInstance* toN   = graph.findNode(to);
            if (!fromN || !toN) {
                report.rejectedEdges.push_back({
                    from, to, "R0", "Unknown node reference"
                });
                continue;
            }

            auto err = graph.tryAddEdge(fromN->outputAttrId(fromP),
                                        toN->inputAttrId(toPort));
            if (err) {
                report.rejectedEdges.push_back({
                    from, to, err->rule, err->message
                });
            } else {
                ++report.edgesLoaded;
            }
        }
    }
}

LoadReport ScnSerializer::loadFromFile(const std::string& path,
                                       NodeGraph& graph,
                                       ScnPositions& positions) {
    std::ifstream f(path);
    if (!f) {
        LoadReport r;
        r.fatalError = "Cannot open file: " + path;
        return r;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return deserialize(ss.str(), graph, positions);
}
