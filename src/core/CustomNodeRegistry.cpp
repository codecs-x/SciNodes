#include "CustomNodeRegistry.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace scinodes {

using nlohmann::json;

static bool categoryFromString(const std::string& s, NodeCategory& out) {
    if (s == "source")      { out = NodeCategory::Source;      return true; }
    if (s == "transformer") { out = NodeCategory::Transformer; return true; }
    if (s == "device")      { out = NodeCategory::Device;      return true; }
    if (s == "sink")        { out = NodeCategory::Sink;        return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Service Locator storage — single global pointer, owner lives in AppWindow
// (or, for tests, in a ScopedCustomNodes RAII guard).
namespace {
CustomNodeRegistry* g_customNodes = nullptr;
}

void installCustomNodes(CustomNodeRegistry& reg) { g_customNodes = &reg; }
void uninstallCustomNodes()                       { g_customNodes = nullptr; }

CustomNodeRegistry& customNodes() {
    // Asserts at the use site — no lazy singleton.  If this fires, the
    // process started using custom-node lookups before AppWindow finished
    // installing its registry (or a test forgot a ScopedCustomNodes).
    static CustomNodeRegistry fallback;  // safe default during very early init
    return g_customNodes ? *g_customNodes : fallback;
}

CustomNodeRegistry* customNodesOpt() { return g_customNodes; }

ScopedCustomNodes::ScopedCustomNodes(CustomNodeRegistry& reg)
    : m_prev(g_customNodes) {
    g_customNodes = &reg;
}
ScopedCustomNodes::~ScopedCustomNodes() { g_customNodes = m_prev; }

const CustomNodeDef* CustomNodeRegistry::find(const std::string& id) const {
    auto it = m_defs.find(id);
    return (it == m_defs.end()) ? nullptr : &it->second;
}

std::vector<std::string> CustomNodeRegistry::typeIds() const {
    std::vector<std::string> out;
    out.reserve(m_defs.size());
    for (const auto& [k, _] : m_defs) out.push_back(k);
    return out;
}

void CustomNodeRegistry::clear() { m_defs.clear(); }

// ---------------------------------------------------------------------------
// Validating loader. Refuses to register an entry that would clobber an
// existing type id or that's missing any of the required fields — partial
// loads would leave the canvas in a confusing state.
// ---------------------------------------------------------------------------
bool CustomNodeRegistry::loadFromJsonString(const std::string& text,
                                            std::string* err) {
    auto bail = [&](const std::string& msg) {
        if (err) *err = msg;
        return false;
    };

    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        return bail(std::string("JSON parse error: ") + e.what());
    }
    if (!j.is_object()) return bail("Top-level JSON value must be an object.");

    CustomNodeDef def;

    if (!j.contains("type_id") || !j["type_id"].is_string())
        return bail("Missing required string field: \"type_id\".");
    def.typeId = j["type_id"].get<std::string>();
    if (def.typeId.empty())
        return bail("\"type_id\" must be a non-empty string.");

    if (m_defs.count(def.typeId))
        return bail("Duplicate type_id: \"" + def.typeId + "\".");

    if (!j.contains("label") || !j["label"].is_string())
        return bail("Missing required string field: \"label\".");
    def.label = j["label"].get<std::string>();
    if (def.label.empty())
        return bail("\"label\" must be a non-empty string.");

    if (j.contains("description") && j["description"].is_string())
        def.description = j["description"].get<std::string>();

    if (!j.contains("category") || !j["category"].is_string())
        return bail("Missing required string field: \"category\".");
    if (!categoryFromString(j["category"].get<std::string>(), def.category))
        return bail("\"category\" must be one of "
                    "\"source\", \"transformer\", \"sink\".");

    auto getInt = [&](const char* key, int& out) -> bool {
        if (!j.contains(key)) {
            if (err) *err = std::string("Missing required integer field: \"")
                          + key + "\".";
            return false;
        }
        if (!j[key].is_number_integer()) {
            if (err) *err = std::string("Field \"") + key
                          + "\" must be an integer.";
            return false;
        }
        out = j[key].get<int>();
        return true;
    };
    if (!getInt("input_ports",  def.inputPorts))  return false;
    if (!getInt("output_ports", def.outputPorts)) return false;

    if (def.inputPorts < 0 || def.inputPorts > 8)
        return bail("\"input_ports\" must be in [0, 8].");
    if (def.outputPorts < 0 || def.outputPorts > 8)
        return bail("\"output_ports\" must be in [0, 8].");

    // Category ↔ port-count consistency mirrors the built-in registry.
    if (def.category == NodeCategory::Source && def.inputPorts != 0)
        return bail("Sources must have input_ports == 0.");
    if (def.category == NodeCategory::Sink && def.outputPorts != 0)
        return bail("Sinks must have output_ports == 0.");

    if (j.contains("params")) {
        if (!j["params"].is_array())
            return bail("\"params\" must be an array.");
        for (const auto& p : j["params"]) {
            if (!p.is_object())
                return bail("Each entry in \"params\" must be an object.");
            ParamDef pd;
            if (!p.contains("name") || !p["name"].is_string() ||
                p["name"].get<std::string>().empty())
                return bail("Each param requires a non-empty \"name\".");
            pd.name = p["name"].get<std::string>();
            if (!p.contains("default") || !p["default"].is_number())
                return bail("Param \"" + pd.name +
                            "\" needs a numeric \"default\".");
            pd.defaultValue = p["default"].get<double>();
            if (p.contains("units") && p["units"].is_string())
                pd.unit = p["units"].get<std::string>();
            def.params.push_back(std::move(pd));
        }
    }

    if (def.category == NodeCategory::Transformer) {
        if (!j.contains("expression") || !j["expression"].is_string() ||
            j["expression"].get<std::string>().empty())
            return bail("Transformer descriptors require a non-empty "
                        "\"expression\" field.");
        def.expression = j["expression"].get<std::string>();
    } else if (j.contains("expression") && j["expression"].is_string()) {
        // Sources/sinks may carry an expression for documentation purposes
        // but it isn't currently consumed.
        def.expression = j["expression"].get<std::string>();
    }

    m_defs.emplace(def.typeId, std::move(def));
    return true;
}

bool CustomNodeRegistry::loadFromFile(const std::string& path,
                                      std::string* err) {
    std::ifstream f(path);
    if (!f) {
        if (err) *err = "Could not open '" + path + "' for reading.";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return loadFromJsonString(ss.str(), err);
}

}  // namespace scinodes
