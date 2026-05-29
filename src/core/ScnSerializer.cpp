#include "ScnSerializer.hpp"
#include "NodeInstance.hpp"
#include "NodeType.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

using json = nlohmann::json;

// ===========================================================================
// serialize
// ===========================================================================
std::string ScnSerializer::serialize(const NodeGraph& graph,
                                     const ScnPositions& positions) {
    json j;
    j["scnodes_version"] = FILE_VERSION;

    // Highest id + 1, so a reload-then-add never collides with a saved id.
    int maxNodeId = 0;
    for (const auto& n : graph.nodes())
        if (n.id > maxNodeId) maxNodeId = n.id;
    j["next_node_id"] = maxNodeId + 1;

    json jnodes = json::array();
    for (const auto& n : graph.nodes()) {
        json jn;
        jn["id"]   = n.id;
        jn["type"] = typeName(n.type);

        auto it = positions.find(n.id);
        ScnVec2 p = (it != positions.end()) ? it->second : ScnVec2{};
        jn["position"] = json::array({ p.x, p.y });

        json jp = json::object();
        for (const auto& [name, value] : n.params)
            jp[name] = value;
        jn["params"] = jp;

        // Asset path solo se persiste si está set, para no inflar .scn de
        // nodos puramente numéricos con un campo vacío.
        if (!n.assetPath.empty())
            jn["asset_path"] = n.assetPath;

        jnodes.push_back(jn);
    }
    j["nodes"] = jnodes;

    json jedges = json::array();
    for (const auto& e : graph.edges()) {
        json je;
        je["id"]        = e.id;
        je["from_node"] = e.fromNodeId;
        je["to_node"]   = e.toNodeId;
        je["to_port"]   = e.toAttrId % 10000;
        jedges.push_back(je);
    }
    j["edges"] = jedges;

    return j.dump(2) + "\n";
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
    graph.restoreSnapshot(GraphSnapshot{});   // reset to empty

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
    if (report.version != FILE_VERSION && !report.version.empty()) {
        // Soft-incompatible: still try to load, but note it.
        report.unknownTypes.push_back("(version mismatch: file is " +
                                      report.version + ", expected " +
                                      FILE_VERSION + ")");
    }

    // ---- Nodes -----------------------------------------------------------
    GraphSnapshot snap;
    snap.nextNodeId = j.value("next_node_id", 1);
    snap.nextEdgeId = 1;

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

            snap.nodes.push_back(n);
            ++report.nodesLoaded;

            if (jn.contains("position") && jn["position"].is_array() &&
                jn["position"].size() >= 2 &&
                jn["position"][0].is_number() &&
                jn["position"][1].is_number())
            {
                positions[id] = ScnVec2{
                    jn["position"][0].get<float>(),
                    jn["position"][1].get<float>()
                };
            }

            if (id >= snap.nextNodeId) snap.nextNodeId = id + 1;
        }
    }

    graph.restoreSnapshot(snap);

    // ---- Edges (re-validated through tryAddEdge) --------------------------
    if (j.contains("edges") && j["edges"].is_array()) {
        for (const auto& je : j["edges"]) {
            int from   = je.value("from_node", 0);
            int to     = je.value("to_node",   0);
            int toPort = je.value("to_port",   0);

            const NodeInstance* fromN = graph.findNode(from);
            const NodeInstance* toN   = graph.findNode(to);
            if (!fromN || !toN) {
                report.rejectedEdges.push_back({
                    from, to, "R0", "Unknown node reference"
                });
                continue;
            }

            auto err = graph.tryAddEdge(fromN->outputAttrId(),
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

    report.finalState = graph.grammarState();
    report.ok = true;
    return report;
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
