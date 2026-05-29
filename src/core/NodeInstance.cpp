#include "NodeInstance.hpp"
#include "NodeKind.hpp"
#include "CustomNodeRegistry.hpp"

// Etapa 6J — el dispatch sobre `NodeType` se centralizó en NodeKind.
// Este archivo queda como el puente entre las APIs públicas históricas
// (`makeNode`, `makeCustomNode`, `defOf`) y la operación libre del kind.

NodeInstance makeNode(int id, NodeType type) {
    NodeInstance inst;
    inst.id   = id;
    inst.type = type;
    if (type == NodeType::Custom) return inst;  // se completa con makeCustomNode

    const NodeDef& def = nodeRegistry().at(type);
    for (const auto& p : def.params)
        inst.params[p.name] = p.defaultValue;
    scinodes::seedFields(inst);

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
        scinodes::seedFields(inst);
    }
    return inst;
}

// La API histórica `defOf(NodeInstance)` se mantiene como wrapper sobre
// `scinodes::resolveDef` — los call-sites legacy quedan intactos pero
// el dispatch ahora pasa por `std::visit` sobre el sum-type.
const NodeDef& defOf(const NodeInstance& n) {
    return scinodes::resolveDef(n);
}

NodeCategory categoryOf(const NodeInstance& n) {
    return defOf(n).category;
}
