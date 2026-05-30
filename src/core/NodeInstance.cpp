#include "NodeInstance.hpp"
#include "CustomNodeRegistry.hpp"
#include "Field.hpp"

#include <map>
#include <mutex>
#include <unordered_map>
#include <utility>

// Etapa 6I.D.1: sembrar `inst.fields` desde synthesizeFields(def).  Crea
// una entrada Quantity por cada Field declarado (inputs, params,
// outputs).  Los puertos arrancan value=0; los params arrancan
// value=defaultValue del registry.  La unidad sale de
// FieldDef.defaultQuantity.unit.
static void seedFieldsFromDef(NodeInstance& inst, const NodeDef& def) {
    for (const auto& fd : scinodes::synthesizeFields(def))
        inst.fields[fd.name] = fd.defaultQuantity;
}

NodeInstance makeNode(int id, NodeType type) {
    NodeInstance inst;
    inst.id   = id;
    inst.type = type;
    if (type == NodeType::Custom) return inst;  // populate via makeCustomNode

    const NodeDef& def = nodeRegistry().at(type);
    for (const auto& p : def.params)
        inst.params[p.name] = p.defaultValue;
    seedFieldsFromDef(inst, def);

    return inst;
}

NodeInstance makeCustomNode(int id, const std::string& typeId) {
    NodeInstance inst;
    inst.id         = id;
    inst.type       = NodeType::Custom;
    inst.customType = typeId;

    const auto* cd = scinodes::customNodes().find(typeId);
    if (cd) {
        for (const auto& p : cd->params)
            inst.params[p.name] = p.defaultValue;
        // Para custom nodes, defOf() sintetiza el NodeDef con los
        // mismos params.  Sembramos fields contra ese def para que
        // ports + params estén ambos cubiertos.
        seedFieldsFromDef(inst, defOf(inst));
    }
    return inst;
}

// ---------------------------------------------------------------------------
// defOf / categoryOf — instance-aware metadata lookup.
//
// For builtins, defOf simply returns the static entry from `nodeRegistry()`.
// For custom nodes the def is synthesized once from the matching
// CustomNodeDef and cached in a static map so the returned reference is
// stable across calls (callers like the grammar parser and codegen rely
// on being able to hold a const NodeDef& briefly).
// ---------------------------------------------------------------------------
namespace {

// Fallback def returned when a Custom instance points at an unknown
// type id — keeps grammar/codegen from crashing on a typo while still
// being clearly identifiable.
const NodeDef& unknownCustomDef() {
    static const NodeDef stub = {
        NodeType::Custom, NodeCategory::Transformer,
        "<unknown custom>", "Custom node referencing a missing descriptor.",
        1, 1, {}
    };
    return stub;
}

const NodeDef& synthesizeCustomDef(const std::string& typeId) {
    static std::mutex                            cacheMtx;
    static std::unordered_map<std::string, NodeDef> cache;

    std::lock_guard<std::mutex> lock(cacheMtx);
    auto it = cache.find(typeId);
    if (it != cache.end()) return it->second;

    const auto* cd = scinodes::customNodes().find(typeId);
    if (!cd) return unknownCustomDef();

    NodeDef def{
        NodeType::Custom, cd->category,
        cd->label, cd->description,
        cd->inputPorts, cd->outputPorts,
        cd->params
    };
    auto [ins, _] = cache.emplace(typeId, std::move(def));
    return ins->second;
}

} // namespace

// SubGraph: el conteo de puertos depende del contenido del grafo hijo.
// Sintetizamos un NodeDef por (inputCount, outputCount) y lo cacheamos.
//
// Categoría derivada del contenido — sin esto, el BFS de la gramática
// no arrancaba desde SubGraphs source-like (sin inputs externos, solo
// outputs):
//
//   inCount == 0  → Source       (el subgrafo genera señal internamente,
//                                 actúa como fuente desde el padre)
//   outCount == 0 → Sink         (consume y no devuelve nada)
//   otherwise     → Transformer  (pipeline intermedio)
static const NodeDef& synthesizeSubGraphDef(int inCount, int outCount) {
    static std::mutex cacheMtx;
    static std::map<std::pair<int,int>, NodeDef> cache;
    std::lock_guard<std::mutex> lock(cacheMtx);
    auto key = std::make_pair(inCount, outCount);
    auto it  = cache.find(key);
    if (it != cache.end()) return it->second;
    NodeCategory cat = NodeCategory::Transformer;
    if      (inCount  == 0 && outCount > 0) cat = NodeCategory::Source;
    else if (outCount == 0 && inCount  > 0) cat = NodeCategory::Sink;
    NodeDef def {
        NodeType::SubGraph, cat,
        "SubGraph",
        "Sub-grafo recursivo (paréntesis).  Doble-click para entrar.",
        inCount, outCount, {}
    };
    auto ins = cache.emplace(key, std::move(def));
    return ins.first->second;
}

const NodeDef& defOf(const NodeInstance& n) {
    if (isSubGraphContainer(n.type))
        return synthesizeSubGraphDef(n.subGraphInputCount,
                                     n.subGraphOutputCount);
    if (n.type != NodeType::Custom)
        return nodeRegistry().at(n.type);
    return synthesizeCustomDef(n.customType);
}

NodeCategory categoryOf(const NodeInstance& n) {
    return defOf(n).category;
}
