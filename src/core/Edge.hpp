#pragma once

// -----------------------------------------------------------------------
// Edge — a directed connection between two nodes.
//
// Attribute ID encoding (same scheme as NodeInstance):
//   input  attr  = nodeId * 1000 + 0
//   output attr  = nodeId * 1000 + 1
// -----------------------------------------------------------------------
struct Edge {
    int id;
    int fromNodeId;   // source node
    int toNodeId;     // destination node
    int fromAttrId;   // output attribute (nodeId*1000 + 1)
    int toAttrId;     // input  attribute (nodeId*1000 + 0)
};
