#pragma once
#include "NodeType.hpp"
#include <string>
#include <unordered_map>
#include <vector>

// -----------------------------------------------------------------------
// CustomNodeRegistry — addRule() hook for runtime-defined node types.
//
// The built-in grammar is keyed by the closed `NodeType` enum, which is
// fixed at compile time. Plugins / user extensions live in this parallel
// registry, keyed by a string `typeId`. Each entry carries everything
// the grammar/codegen would need to integrate it later (category, port
// counts, params, and — for transformers — a Scilab expression
// template).
//
// Expression template substitution (transformers only):
//   • `u1`, `u2`, ...   → the source expressions of the input ports
//                          (in port order, 1-based to match the planner's
//                          published grammar reference)
//   • `p_<name>`        → the live value of param "<name>"
//
// This loader is intentionally side-effect-free: registration only adds
// entries to the in-memory map. Wiring the descriptors into the live
// canvas, palette and codegen is a follow-up slice.
// -----------------------------------------------------------------------
namespace scinodes {

struct CustomNodeDef {
    std::string  typeId;        // unique key, used by future palette/codegen
    std::string  label;
    std::string  description;
    NodeCategory category = NodeCategory::Transformer;
    int          inputPorts  = 1;
    int          outputPorts = 1;
    std::vector<ParamDef> params;
    std::string  expression;    // Scilab expression; empty for sources/sinks
};

class CustomNodeRegistry {
public:
    static CustomNodeRegistry& instance();

    // Parse a JSON string and register the descriptor. Returns true on
    // success; on failure, *err (if non-null) is filled with a
    // human-readable reason and the registry is unchanged.
    bool loadFromJsonString(const std::string& json,
                            std::string* err = nullptr);

    // Same, but reads from disk first.
    bool loadFromFile(const std::string& path,
                      std::string* err = nullptr);

    // Lookup by typeId. Returns nullptr if absent.
    const CustomNodeDef* find(const std::string& typeId) const;

    // List all registered type ids in insertion order is not preserved
    // (unordered_map under the hood); callers should sort if they need
    // determinism.
    std::vector<std::string> typeIds() const;

    // Wipe the registry (mostly for tests).
    void clear();

private:
    CustomNodeRegistry() = default;
    std::unordered_map<std::string, CustomNodeDef> m_defs;
};

}  // namespace scinodes
