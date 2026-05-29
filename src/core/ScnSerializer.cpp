#include "ScnSerializer.hpp"
#include "NodeInstance.hpp"
#include "NodeType.hpp"
#include "Unit.hpp"

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

    // Root-level metadata — sólo se emite en la llamada outermost para
    // que viajen con el .scn como descriptores del experimento.  Los
    // SubGraphs anidados no llevan título/tags propios: su identidad
    // viene del `Name` del nodo padre y de su `comment` en el padre.
    if (!graph.id().empty())          j["id"]          = graph.id();
    if (!graph.title().empty())       j["title"]       = graph.title();
    if (!graph.description().empty()) j["description"] = graph.description();
    if (!graph.tags().empty())        j["tags"]        = graph.tags();

    // Catálogo de objetos 3D importados (paso 3 del refactor de escena).
    // Sólo se emite si está poblado para no ensuciar los .scn legacy.
    if (!graph.importedObjects().empty()) {
        json jobjs = json::array();
        for (const auto& o : graph.importedObjects()) {
            json jo;
            jo["name"] = o.name;
            jo["path"] = o.path;
            if (!o.parts.empty()) jo["parts"] = o.parts;
            jobjs.push_back(jo);
        }
        j["objects"] = jobjs;
    }

    // Domain unit (etapa 6I.O).  Default "s" (time-domain) no se
    // serializa para .scn limpios; sólo emitimos si el grafo es de
    // un dominio distinto.
    {
        scinodes::Unit u = graph.domainUnit();
        if (!(u == scinodes::unitSecond())) {
            j["domain_unit"] = u.toCanonicalString();
        }
    }

    // Display units del proyecto (etapa 6I.C).  Lista de unit strings;
    // cada uno representa la unidad PREFERIDA del usuario para esa
    // dimensión SI.  El orden no importa (el load reindexa por exp).
    // Se emite sólo si no está vacío para mantener .scn limpios.
    if (!graph.displayUnits().empty()) {
        json jdu = json::array();
        for (const auto& [_, u] : graph.displayUnits())
            jdu.push_back(u.toCanonicalString());
        j["display_units"] = jdu;
    }

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

        if (!n.comment.empty())
            jn["comment"] = n.comment;

        if (!n.stringParams.empty()) {
            json js = json::object();
            for (const auto& [k, v] : n.stringParams) js[k] = v;
            jn["string_params"] = js;
        }

        // Per-instance unit overrides (etapa 6G).  Emitimos el TEXTO
        // verbatim del override — no canonicalizamos por
        // `toCanonicalString()` porque para unidades adimensionales
        // (rad, dB, etc.) la canonicalización pierde el nombre que el
        // usuario eligió (toCanonicalString de rad = "" porque rad es
        // dimensionless × 1 en SI puro).  El round-trip es lossless.
        if (!n.portUnitOverrides.empty()) {
            json ju = json::object();
            for (const auto& [key, text] : n.portUnitOverrides) {
                std::string slot;
                if (key >= kAttrIdOutputBase) {
                    slot = "out" + std::to_string(key - kAttrIdOutputBase);
                } else {
                    slot = "in"  + std::to_string(key);
                }
                ju[slot] = text;
            }
            jn["port_units"] = ju;
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
        report.version != "0.4" &&
        report.version != "0.3") {
        // Reconocemos 0.3 (sin SubGraphs), 0.4 (con SubGraphs) y 0.5
        // (con catálogo de objetos 3D).  Otras versiones se cargan en
        // best-effort y se reporta el mismatch.
        report.unknownTypes.push_back("(version mismatch: file is " +
                                      report.version + ", expected " +
                                      FILE_VERSION + ")");
    }

    deserializeGraphBody(j, graph, &positions, report);

    // Root-level metadata: sólo en la llamada outermost.  Si el archivo
    // no la trae (formato 0.3 ó 0.4 pre-metadata), los campos quedan
    // vacíos — el cliente puede caer a un fallback (filename como título).
    if (j.contains("id") && j["id"].is_string())
        graph.setId(j["id"].get<std::string>());
    if (j.contains("title") && j["title"].is_string())
        graph.setTitle(j["title"].get<std::string>());
    if (j.contains("description") && j["description"].is_string())
        graph.setDescription(j["description"].get<std::string>());
    if (j.contains("tags") && j["tags"].is_array()) {
        std::vector<std::string> tags;
        for (const auto& t : j["tags"])
            if (t.is_string()) tags.push_back(t.get<std::string>());
        graph.setTags(std::move(tags));
    }

    // Domain unit (etapa 6I.O).  Sin la llave: queda en default `s`.
    if (j.contains("domain_unit") && j["domain_unit"].is_string()) {
        auto pr = scinodes::parseUnit(j["domain_unit"].get<std::string>());
        if (pr.ok()) graph.setDomainUnit(pr.unit);
    }

    // Display units del proyecto (etapa 6I.C).  Lista de strings;
    // cada uno se parsea via parseUnit → setDisplayUnit indexa por
    // exp.  Entradas que no parsean se ignoran silenciosamente — el
    // .scn corrupto no bloquea la carga, simplemente pierde esa
    // preferencia.  Sin la llave, el grafo queda con map vacío.
    if (j.contains("display_units") && j["display_units"].is_array()) {
        for (const auto& jdu : j["display_units"]) {
            if (!jdu.is_string()) continue;
            auto pr = scinodes::parseUnit(jdu.get<std::string>());
            if (pr.ok()) graph.setDisplayUnit(pr.unit);
        }
    }

    // Catálogo de objetos 3D (esquema 0.5).  Si falta, el catálogo
    // queda vacío — los .scn legacy 0.3/0.4 se cargan sin error.
    if (j.contains("objects") && j["objects"].is_array()) {
        std::vector<ImportedObject> objs;
        for (const auto& jo : j["objects"]) {
            if (!jo.is_object()) continue;
            ImportedObject o;
            o.name = jo.value("name", std::string{});
            o.path = jo.value("path", std::string{});
            if (jo.contains("parts") && jo["parts"].is_array())
                for (const auto& jp : jo["parts"])
                    if (jp.is_string()) o.parts.push_back(jp.get<std::string>());
            // Entradas sin name caen; un name vacío no es referenciable.
            if (!o.name.empty()) objs.push_back(std::move(o));
        }
        graph.setImportedObjects(std::move(objs));
    }

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
                    if (it.value().is_number()) {
                        const double v = it.value().get<double>();
                        n.params[it.key()] = v;
                        // Etapa 6I.E: el serializer .scn 0.6+ guarda el
                        // double crudo (SI canónico — el codegen hace toSI
                        // antes de emitir).  Al cargar, sincronizamos
                        // fields[name].value para que el codegen leyendo
                        // toSI() lo recupere idéntico.  La unidad se
                        // preserva del sembrado en makeNode (FieldDef
                        // default), porque el .scn legacy NO guarda
                        // unidad explícita por param (eso lo agrega una
                        // etapa futura que extienda el schema params a
                        // objeto {value, unit}).
                        auto fit = n.fields.find(it.key());
                        if (fit != n.fields.end()) {
                            // Convertimos value SI → field.value en la
                            // unidad del field, para que toSI() lo
                            // devuelva idéntico.  Esto preserva las
                            // unidades canónicas que makeNode sembró
                            // desde FieldDef.defaultQuantity.
                            if (fit->second.unit.magnitude != 0.0)
                                fit->second.value =
                                    v / fit->second.unit.magnitude;
                            else
                                fit->second.value = v;
                        }
                    }

            if (jn.contains("asset_path") && jn["asset_path"].is_string())
                n.assetPath = jn["asset_path"].get<std::string>();

            if (jn.contains("comment") && jn["comment"].is_string())
                n.comment = jn["comment"].get<std::string>();

            if (jn.contains("string_params") && jn["string_params"].is_object())
                for (auto it = jn["string_params"].begin();
                     it != jn["string_params"].end(); ++it)
                    if (it.value().is_string())
                        n.stringParams[it.key()] = it.value().get<std::string>();

            // Per-instance unit overrides (etapa 6G).  Lee
            // "port_units": { "in0": "V", "out0": "rad", ... } y
            // almacena el texto verbatim (lossless — ver comentario
            // en NodeInstance.hpp::portUnitOverrides).  Las entradas
            // con key malformada o texto que no parsea se descartan
            // silenciosamente; el analyzer las re-parsea en runtime y
            // si fallan caen al modo polimórfico normal.
            if (jn.contains("port_units") && jn["port_units"].is_object()) {
                for (auto it = jn["port_units"].begin();
                     it != jn["port_units"].end(); ++it) {
                    if (!it.value().is_string()) continue;
                    const std::string& slot = it.key();
                    int key = -1;
                    if (slot.size() > 2 && slot.rfind("in", 0) == 0) {
                        try { key = portKeyForInput(std::stoi(slot.substr(2))); }
                        catch (...) { continue; }
                    } else if (slot.size() > 3 && slot.rfind("out", 0) == 0) {
                        try { key = portKeyForOutput(std::stoi(slot.substr(3))); }
                        catch (...) { continue; }
                    } else continue;
                    std::string text = it.value().get<std::string>();
                    if (text.empty()) continue;
                    auto pr = scinodes::parseUnit(text);
                    if (pr.ok()) n.portUnitOverrides[key] = std::move(text);
                }
            }

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
