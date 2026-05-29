#pragma once
#include "NodeType.hpp"
#include <unordered_map>

// -----------------------------------------------------------------------
// NodeInstance — a single placed node in the graph
// -----------------------------------------------------------------------
struct NodeInstance {
    int      id;        // unique within the graph (sequential)
    NodeType type;

    // Parameter values indexed by ParamDef::name
    std::unordered_map<std::string, double> params;

    // imnodes attribute IDs derived from node id (multiplier = 10000):
    //   input  port i → id * 10000 + i              (i = 0, 1, …)
    //   output port k → id * 10000 + 9000 + k       (k = 0, 1, …)
    //   param  attrs  → id * 10000 + 100 + j        (display-only, not linked)
    //
    // Detection helpers (given any attr id):
    //   node id  = attrId / 10000
    //   is output: attrId % 10000 >= 9000     (range 9000..9999)
    //   is input:  attrId % 10000 < 100
    int inputAttrId(int port = 0)  const { return id * 10000 + port; }
    int outputAttrId(int port = 0) const { return id * 10000 + 9000 + port; }
    int paramAttrId(int j)         const { return id * 10000 + 100 + j; }
};

// Construct a NodeInstance with default parameters from the registry.
NodeInstance makeNode(int id, NodeType type);
