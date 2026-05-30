#include "AssetMapping.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace scinodes {

namespace {

using nlohmann::json;

// Lee un slot {"node": "..."} (parts / anchors) de un objeto JSON, tolerando
// que la forma sea string suelto ("Cylinder.001") para compatibilidad con
// sidecars hechos a mano "rápido".  Devuelve cadena vacía si la entrada no
// existe o tiene un tipo inesperado.
std::string readNodeName(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_object() && v.contains("node") && v["node"].is_string())
        return v["node"].get<std::string>();
    return {};
}

// Lee el eje de un joint si está presente y bien formado (array de 3
// números).  Devuelve true si se consumió un eje válido.
bool readAxis(const json& v, std::array<float, 3>& out) {
    if (!v.is_object() || !v.contains("axis")) return false;
    const auto& a = v["axis"];
    if (!a.is_array() || a.size() != 3) return false;
    for (size_t i = 0; i < 3; ++i) {
        if (!a[i].is_number()) return false;
        out[i] = a[i].get<float>();
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
AssetMapping AssetMapping::loadFromString(const std::string& jsonText,
                                         std::string*       err) {
    AssetMapping m;
    json j;
    try {
        j = json::parse(jsonText);
    } catch (const std::exception& e) {
        if (err) *err = std::string("AssetMapping JSON inválido: ") + e.what();
        return m;
    }

    if (!j.is_object()) {
        if (err) *err = "AssetMapping: el documento raíz debe ser un objeto.";
        return m;
    }

    if (j.contains("version") && j["version"].is_string())
        m.version = j["version"].get<std::string>();

    // El loader actual solo entiende v0.1; si alguien escribe v0.2 desde
    // una versión más nueva de SciNodes y se intenta abrir aquí, mejor
    // avisar y devolver el mapping vacío que aceptar campos a ciegas.
    if (m.version != "0.1") {
        if (err) *err = "AssetMapping: versión desconocida '" + m.version +
                        "' (esperaba 0.1).";
        return AssetMapping{};
    }

    if (j.contains("asset_path") && j["asset_path"].is_string())
        m.asset_path = j["asset_path"].get<std::string>();
    if (j.contains("device_type") && j["device_type"].is_string())
        m.device_type = j["device_type"].get<std::string>();

    // ---- parts ----
    if (j.contains("parts") && j["parts"].is_object()) {
        for (auto it = j["parts"].begin(); it != j["parts"].end(); ++it) {
            const std::string name = it.key();
            PartSlot s;
            s.node_name = readNodeName(it.value());
            m.parts[name] = std::move(s);
        }
    }

    // ---- joints ----
    if (j.contains("joints") && j["joints"].is_object()) {
        for (auto it = j["joints"].begin(); it != j["joints"].end(); ++it) {
            const std::string name = it.key();
            JointSlot s;
            s.node_name = readNodeName(it.value());
            s.axis_explicit = readAxis(it.value(), s.axis);
            m.joints[name] = std::move(s);
        }
    }

    // ---- anchors ----
    if (j.contains("anchors") && j["anchors"].is_object()) {
        for (auto it = j["anchors"].begin(); it != j["anchors"].end(); ++it) {
            const std::string name = it.key();
            AnchorSlot s;
            s.node_name = readNodeName(it.value());
            m.anchors[name] = std::move(s);
        }
    }

    return m;
}

// ---------------------------------------------------------------------------
AssetMapping AssetMapping::loadFromFile(const std::string& path,
                                       std::string*       err) {
    std::ifstream f(path);
    if (!f) {
        if (err) *err = "AssetMapping: no se pudo abrir " + path;
        return {};
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return loadFromString(ss.str(), err);
}

// ---------------------------------------------------------------------------
std::string AssetMapping::toJsonString() const {
    json j;
    j["version"]     = version;
    j["asset_path"]  = asset_path;
    j["device_type"] = device_type;

    j["parts"]   = json::object();
    j["joints"]  = json::object();
    j["anchors"] = json::object();

    for (const auto& [name, slot] : parts) {
        json e = json::object();
        e["node"] = slot.node_name;
        j["parts"][name] = std::move(e);
    }
    for (const auto& [name, slot] : joints) {
        json e = json::object();
        e["node"] = slot.node_name;
        if (slot.axis_explicit) {
            e["axis"] = json::array({ slot.axis[0], slot.axis[1], slot.axis[2] });
        }
        j["joints"][name] = std::move(e);
    }
    for (const auto& [name, slot] : anchors) {
        json e = json::object();
        e["node"] = slot.node_name;
        j["anchors"][name] = std::move(e);
    }

    return j.dump(2);
}

// ---------------------------------------------------------------------------
bool AssetMapping::saveToFile(const std::string& path,
                              std::string*       err) const {
    std::ofstream f(path);
    if (!f) {
        if (err) *err = "AssetMapping: no se pudo escribir " + path;
        return false;
    }
    f << toJsonString();
    f << '\n';
    return f.good();
}

// ---------------------------------------------------------------------------
std::string AssetMapping::sidecarPathFor(const std::string& assetPath) {
    // Sustituye la extensión por ".mapping.json".  Si no hay extensión
    // (improbable pero posible), agrega el sufijo directamente.
    //
    // No usamos std::filesystem para mantener simetría con el resto del
    // core, que evita esa dependencia por compatibilidad con toolchains
    // antiguos en algunos entornos de prueba.
    if (assetPath.empty()) return ".mapping.json";

    // Buscar el último '.' después del último separador de directorio.
    const std::size_t slash = assetPath.find_last_of("/\\");
    const std::size_t dot   = assetPath.find_last_of('.');

    const bool extInBasename =
        dot != std::string::npos &&
        (slash == std::string::npos || dot > slash);

    if (extInBasename) {
        return assetPath.substr(0, dot) + ".mapping.json";
    }
    return assetPath + ".mapping.json";
}

}  // namespace scinodes
