#include "NodeInstance.hpp"
#include "CustomNodeRegistry.hpp"

#include <map>
#include <mutex>
#include <unordered_map>
#include <utility>

NodeInstance makeNode(int id, NodeType type) {
    NodeInstance inst;
    inst.id   = id;
    inst.type = type;
    if (type == NodeType::Custom) return inst;  // populate via makeCustomNode

    const NodeDef& def = nodeRegistry().at(type);
    for (const auto& p : def.params)
        inst.params[p.name] = p.defaultValue;

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
static const NodeDef& synthesizeSubGraphDef(int inCount, int outCount) {
    static std::mutex cacheMtx;
    static std::map<std::pair<int,int>, NodeDef> cache;
    std::lock_guard<std::mutex> lock(cacheMtx);
    auto key = std::make_pair(inCount, outCount);
    auto it  = cache.find(key);
    if (it != cache.end()) return it->second;
    NodeDef def {
        NodeType::SubGraph, NodeCategory::Transformer,
        "SubGraph",
        "Sub-grafo recursivo (paréntesis).  Doble-click para entrar.",
        inCount, outCount, {}
    };
    auto ins = cache.emplace(key, std::move(def));
    return ins.first->second;
}

const NodeDef& defOf(const NodeInstance& n) {
    if (n.type == NodeType::SubGraph)
        return synthesizeSubGraphDef(n.subGraphInputCount,
                                     n.subGraphOutputCount);
    if (n.type != NodeType::Custom)
        return nodeRegistry().at(n.type);
    return synthesizeCustomDef(n.customType);
}

NodeCategory categoryOf(const NodeInstance& n) {
    return defOf(n).category;
}
