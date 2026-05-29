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
    CustomNodeRegistry() = default;

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
    std::unordered_map<std::string, CustomNodeDef> m_defs;
};

// ---------------------------------------------------------------------------
// Service Locator — custom node lookup is global *by design* because
// `defOf()` is the type-system query used pervasively across core
// (NodeGraph, ScilabCodeGen, GrammarParser, NodeCanvas, ...).  Threading a
// registry reference through every site would ripple to ~30 signatures for
// a registry that is conceptually a process-wide singleton (it answers
// "what does type 'X' mean here?", same role as RTTI).
//
// We accept the global access pattern but make ownership explicit:
//   • AppWindow constructs a CustomNodeRegistry and calls installCustomNodes().
//   • Tests use ScopedCustomNodes for isolation (RAII; restores previous on
//     scope exit).
//   • customNodes() asserts an instance is installed — no lazy singleton.
//
// This is the Service Locator pattern; Ostrowski (Cap. 11) discusses it as
// the pragmatic compromise when full DI would be more invasive than the
// benefit warrants.
// ---------------------------------------------------------------------------
void                installCustomNodes(CustomNodeRegistry& reg);
void                uninstallCustomNodes();
CustomNodeRegistry& customNodes();
CustomNodeRegistry* customNodesOpt();   // nullptr if none installed (use in pre-init paths)

// RAII helper for tests: install on construction, restore previous on
// destruction.  Lets each test own a fresh registry without leaking state.
class ScopedCustomNodes {
public:
    explicit ScopedCustomNodes(CustomNodeRegistry& reg);
    ~ScopedCustomNodes();
    ScopedCustomNodes(const ScopedCustomNodes&)            = delete;
    ScopedCustomNodes& operator=(const ScopedCustomNodes&) = delete;

private:
    CustomNodeRegistry* m_prev;
};

}  // namespace scinodes
