#include "NodeInstance.hpp"

NodeInstance makeNode(int id, NodeType type) {
    NodeInstance inst;
    inst.id   = id;
    inst.type = type;

    const NodeDef& def = nodeRegistry().at(type);
    for (const auto& p : def.params)
        inst.params[p.name] = p.defaultValue;

    return inst;
}
