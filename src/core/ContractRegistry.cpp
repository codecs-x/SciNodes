#include "ContractRegistry.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace scinodes {

using json = nlohmann::json;

ContractRegistry& ContractRegistry::instance() {
    static ContractRegistry inst;
    return inst;
}

namespace {

// Helpers que rellenan err con un mensaje útil sin abortar — el patrón
// del proyecto es que el registry queda intacto en error y el caller
// muestra el mensaje en la UI.
bool failBad(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

bool parseParts(const json& j, std::vector<ContractPart>& out, std::string* err) {
    if (!j.is_array()) return failBad(err, "`parts` must be an array");
    for (size_t i = 0; i < j.size(); ++i) {
        const auto& e = j[i];
        ContractPart p;
        try {
            p.name     = e.at("name").get<std::string>();
            p.kind     = e.value("kind", "mesh");
            p.required = e.value("required", true);
            p.doc      = e.value("doc", "");
        } catch (const std::exception& ex) {
            return failBad(err, "parts[" + std::to_string(i) + "]: "
                                + ex.what());
        }
        if (p.name.empty())
            return failBad(err, "parts[" + std::to_string(i) +
                                "].name is empty");
        out.push_back(std::move(p));
    }
    return true;
}

bool parseJoints(const json& j, std::vector<ContractJoint>& out, std::string* err) {
    if (!j.is_array()) return failBad(err, "`joints` must be an array");
    static const std::vector<std::string> validTypes = {
        "revolute", "prismatic", "fixed", "cylindrical", "ball", "planar"
    };
    for (size_t i = 0; i < j.size(); ++i) {
        const auto& e = j[i];
        ContractJoint jo;
        try {
            jo.name      = e.at("name").get<std::string>();
            jo.type      = e.at("type").get<std::string>();
            jo.parent    = e.at("parent").get<std::string>();
            jo.child     = e.at("child").get<std::string>();
            jo.driven_by = e.value("driven_by", "");
            jo.required  = e.value("required", true);
            jo.doc       = e.value("doc", "");
        } catch (const std::exception& ex) {
            return failBad(err, "joints[" + std::to_string(i) + "]: "
                                + ex.what());
        }
        bool ok = false;
        for (const auto& t : validTypes) if (jo.type == t) { ok = true; break; }
        if (!ok) return failBad(err, "joints[" + std::to_string(i) +
                                "].type='" + jo.type + "' not recognized");
        out.push_back(std::move(jo));
    }
    return true;
}

bool parseAnchors(const json& j, std::vector<ContractAnchor>& out, std::string* err) {
    if (!j.is_array()) return failBad(err, "`anchors` must be an array");
    for (size_t i = 0; i < j.size(); ++i) {
        const auto& e = j[i];
        ContractAnchor a;
        try {
            a.name     = e.at("name").get<std::string>();
            a.kind     = e.value("kind", "");
            a.required = e.value("required", true);
            a.doc      = e.value("doc", "");
        } catch (const std::exception& ex) {
            return failBad(err, "anchors[" + std::to_string(i) + "]: "
                                + ex.what());
        }
        out.push_back(std::move(a));
    }
    return true;
}

}  // namespace

bool ContractRegistry::loadFromJsonString(const std::string& text,
                                          std::string*       err) {
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& ex) {
        return failBad(err, std::string("JSON parse error: ") + ex.what());
    }

    DeviceContract c;
    try {
        c.device_type = j.at("device_type").get<std::string>();
    } catch (const std::exception& ex) {
        return failBad(err, std::string("missing device_type: ") + ex.what());
    }
    if (c.device_type.empty())
        return failBad(err, "device_type is empty");

    c.version     = j.value("version", "0.0");
    c.description = j.value("description", "");

    if (j.contains("parts")   && !parseParts  (j["parts"],   c.parts,   err))
        return false;
    if (j.contains("joints")  && !parseJoints (j["joints"],  c.joints,  err))
        return false;
    if (j.contains("anchors") && !parseAnchors(j["anchors"], c.anchors, err))
        return false;

    // Coherencia: joints deben referenciar parts existentes.
    auto findPart = [&](const std::string& name) {
        for (const auto& p : c.parts) if (p.name == name) return true;
        return false;
    };
    for (size_t i = 0; i < c.joints.size(); ++i) {
        const auto& jo = c.joints[i];
        if (!findPart(jo.parent))
            return failBad(err, "joint '" + jo.name + "' references unknown "
                                "parent part '" + jo.parent + "'");
        if (!findPart(jo.child))
            return failBad(err, "joint '" + jo.name + "' references unknown "
                                "child part '" + jo.child + "'");
    }

    m_contracts[c.device_type] = std::move(c);
    return true;
}

bool ContractRegistry::loadFromFile(const std::string& path,
                                    std::string*       err) {
    std::ifstream f(path);
    if (!f) return failBad(err, "cannot open " + path);
    std::ostringstream os; os << f.rdbuf();
    return loadFromJsonString(os.str(), err);
}

int ContractRegistry::loadFromDirectory(const std::string& dir,
                                        std::string*       err) {
    namespace fs = std::filesystem;
    int loaded = 0;
    std::string accumulated;
    if (!fs::is_directory(dir)) {
        if (err) *err = "contracts directory does not exist: " + dir;
        return 0;
    }
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        std::string fileErr;
        if (!loadFromFile(entry.path().string(), &fileErr)) {
            accumulated += entry.path().filename().string() + ": "
                         + fileErr + "\n";
        } else {
            ++loaded;
        }
    }
    if (err && !accumulated.empty()) *err = accumulated;
    return loaded;
}

const DeviceContract*
ContractRegistry::find(const std::string& deviceType) const {
    auto it = m_contracts.find(deviceType);
    return it == m_contracts.end() ? nullptr : &it->second;
}

std::vector<std::string> ContractRegistry::deviceTypes() const {
    std::vector<std::string> out;
    out.reserve(m_contracts.size());
    for (const auto& kv : m_contracts) out.push_back(kv.first);
    return out;
}

void ContractRegistry::clear() {
    m_contracts.clear();
}

}  // namespace scinodes
