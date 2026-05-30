#include "ExamplesBrowser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Devuelve la primera carpeta candidata que contiene index.json.  Permite
// correr el binario desde repo root, desde build/, o desde build/Debug, sin
// hardcodear nada.  El env var SCINODES_EXAMPLES_DIR gana siempre.
std::string resolveExamplesDir() {
    if (const char* override_ = std::getenv("SCINODES_EXAMPLES_DIR")) {
        if (fs::exists(fs::path(override_) / "index.json")) return override_;
    }
    const char* candidates[] = {
        "examples/graphs",
        "../examples/graphs",
        "../../examples/graphs",
        "../../../examples/graphs",
    };
    for (const char* rel : candidates) {
        if (fs::exists(fs::path(rel) / "index.json"))
            return fs::absolute(rel).string();
    }
    return {};
}

}  // namespace

void ExamplesBrowser::loadManifest() {
    m_entries.clear();
    m_loadError.clear();
    m_selected = -1;

    m_examplesDir = resolveExamplesDir();
    if (m_examplesDir.empty()) {
        m_loadError =
            "No encontré examples/graphs/index.json.  Lanza SciNodes desde "
            "la raíz del repo, o ajusta SCINODES_EXAMPLES_DIR.";
        return;
    }

    const fs::path indexPath = fs::path(m_examplesDir) / "index.json";
    std::ifstream in(indexPath);
    if (!in) {
        m_loadError = "No pude abrir " + indexPath.string();
        return;
    }
    std::stringstream ss; ss << in.rdbuf();
    json j;
    try { j = json::parse(ss.str()); }
    catch (const std::exception& e) {
        m_loadError = std::string("index.json inválido: ") + e.what();
        return;
    }

    if (!j.is_object() || !j.contains("examples") || !j["examples"].is_array()) {
        m_loadError = "index.json: se esperaba { examples: [ ... ] }.";
        return;
    }

    for (const auto& je : j["examples"]) {
        Entry e;
        if (je.contains("id")          && je["id"].is_string())          e.id          = je["id"].get<std::string>();
        if (je.contains("title")       && je["title"].is_string())       e.title       = je["title"].get<std::string>();
        if (je.contains("file")        && je["file"].is_string())        e.file        = je["file"].get<std::string>();
        if (je.contains("description") && je["description"].is_string()) e.description = je["description"].get<std::string>();
        if (je.contains("tags") && je["tags"].is_array()) {
            for (const auto& t : je["tags"])
                if (t.is_string()) e.tags.push_back(t.get<std::string>());
        }
        if (!e.file.empty()) m_entries.push_back(std::move(e));
    }

    if (!m_entries.empty()) m_selected = 0;
}

bool ExamplesBrowser::matchesSearch(const Entry& e) const {
    if (m_searchBuf[0] == '\0') return true;
    std::string q = toLower(m_searchBuf);
    if (toLower(e.title).find(q) != std::string::npos) return true;
    if (toLower(e.id).find(q) != std::string::npos)    return true;
    for (const auto& t : e.tags)
        if (toLower(t).find(q) != std::string::npos)    return true;
    return false;
}

bool ExamplesBrowser::draw() {
    if (!m_open) return false;
    if (m_needsLoad) { loadManifest(); m_needsLoad = false; }

    bool loadRequested = false;
    m_pickedPath.clear();

    ImGui::SetNextWindowSize({860, 520}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Examples", &m_open)) { ImGui::End(); return false; }

    if (!m_loadError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 120, 100, 255));
        ImGui::TextWrapped("%s", m_loadError.c_str());
        ImGui::PopStyleColor();
        if (ImGui::Button("Reintentar")) m_needsLoad = true;
        ImGui::End();
        return false;
    }

    // Search arriba a todo el ancho.
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##search", "Buscar por título, id o tag...",
                             m_searchBuf, sizeof(m_searchBuf));
    ImGui::Separator();

    // Layout: lista (izquierda) | descripción (derecha).
    const float leftWidth = ImGui::GetContentRegionAvail().x * 0.32f;
    ImGui::BeginChild("##list", {leftWidth, 0}, true);
    int visibleCount = 0;
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        const Entry& e = m_entries[i];
        if (!matchesSearch(e)) continue;
        ++visibleCount;
        const bool sel = (i == m_selected);
        if (ImGui::Selectable(e.title.c_str(), sel)) m_selected = i;
    }
    if (visibleCount == 0) {
        ImGui::TextDisabled("Sin resultados.");
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##detail", {0, 0}, false);
    if (m_selected >= 0 && m_selected < static_cast<int>(m_entries.size())) {
        const Entry& e = m_entries[m_selected];

        ImGui::PushFont(nullptr);  // mantiene el font default pero permite scale
        ImGui::TextWrapped("%s", e.title.c_str());
        ImGui::PopFont();
        ImGui::Separator();

        if (!e.tags.empty()) {
            ImGui::TextDisabled("Tags:");
            ImGui::SameLine();
            for (size_t i = 0; i < e.tags.size(); ++i) {
                if (i) { ImGui::SameLine(); ImGui::TextUnformatted("·"); ImGui::SameLine(); }
                ImGui::TextUnformatted(e.tags[i].c_str());
            }
        }

        ImGui::Spacing();

        // Descripción scrolleable para que no empuje los botones fuera de
        // pantalla en descripciones largas.
        const float footerH = ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("##desc",
                          {0, ImGui::GetContentRegionAvail().y - footerH},
                          true);
        ImGui::TextWrapped("%s", e.description.c_str());
        ImGui::EndChild();

        const fs::path full = fs::path(m_examplesDir) / e.file;
        if (ImGui::Button("Load")) {
            m_pickedPath = full.string();
            loadRequested = true;
            m_open = false;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", full.string().c_str());
    } else {
        ImGui::TextDisabled("Selecciona un ejemplo en la lista.");
    }
    ImGui::EndChild();

    ImGui::End();
    return loadRequested;
}
