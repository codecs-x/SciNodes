#include "I18n.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace scinodes {

I18n& I18n::instance() {
    static I18n inst;
    return inst;
}

bool I18n::load(const std::string& langCode) {
    namespace fs = std::filesystem;
    // "en" es un caso especial: el inglés es la fuente de verdad en el
    // código C++ (NodeDef::label, NodeDef::description, etc).  Cargar
    // "en" simplemente limpia la tabla i18n para que tr() caiga al
    // fallback en todas las llamadas via trOr() — eso ES inglés.
    // Resultado: no hay i18n/en.json para mantener; las cadenas inglesas
    // tienen una única fuente de verdad (el código C++).
    if (langCode == "en") {
        m_strings.clear();
        m_lang = "en";
        std::fprintf(stderr,
            "[I18n] Idioma: en (sin tabla; fallback al texto C++).\n");
        return true;
    }

    const std::vector<fs::path> candidates = {
        fs::path("i18n") / (langCode + ".json"),
        fs::path("..")   / "i18n" / (langCode + ".json"),
    };
    fs::path picked;
    for (const auto& c : candidates) {
        if (fs::exists(c)) { picked = c; break; }
    }
    if (picked.empty()) {
        std::fprintf(stderr,
            "[I18n] No se encontró i18n/%s.json (probé %zu rutas).\n",
            langCode.c_str(), candidates.size());
        return false;
    }

    std::ifstream f(picked);
    if (!f) {
        std::fprintf(stderr, "[I18n] No pude abrir %s\n", picked.c_str());
        return false;
    }

    nlohmann::json j;
    try { f >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr, "[I18n] JSON parse error en %s: %s\n",
                     picked.c_str(), e.what());
        return false;
    }
    if (!j.is_object()) {
        std::fprintf(stderr, "[I18n] %s no es un objeto JSON.\n",
                     picked.c_str());
        return false;
    }

    m_strings.clear();
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.value().is_string())
            m_strings[it.key()] = it.value().get<std::string>();
    }
    m_lang = langCode;
    std::fprintf(stderr, "[I18n] %zu strings cargadas (%s).\n",
                 m_strings.size(), langCode.c_str());
    return !m_strings.empty();
}

const std::string& I18n::tr(const std::string& key) const {
    auto it = m_strings.find(key);
    if (it != m_strings.end()) return it->second;
    return key;
}

const std::string& I18n::trOr(const std::string& key,
                              const std::string& fallback) const {
    auto it = m_strings.find(key);
    if (it != m_strings.end()) return it->second;
    return fallback;
}

std::vector<std::string> I18n::availableLanguages() const {
    namespace fs = std::filesystem;
    // "en" siempre está disponible aunque no haya i18n/en.json — el
    // inglés vive en el código C++ (load("en") limpia la tabla y
    // tr() cae al fallback def.label/def.description).
    std::vector<std::string> out = { "en" };
    const std::vector<fs::path> roots = {
        fs::path("i18n"),
        fs::path("..") / "i18n",
    };
    for (const auto& root : roots) {
        if (!fs::is_directory(root)) continue;
        for (const auto& ent : fs::directory_iterator(root)) {
            if (!ent.is_regular_file()) continue;
            const auto p = ent.path();
            if (p.extension() != ".json") continue;
            const std::string lang = p.stem().string();
            if (lang == "en") continue;  // ya está en la lista
            out.push_back(lang);
        }
        if (out.size() > 1) break;  // primera carpeta válida gana
    }
    return out;
}

}  // namespace scinodes
