#include "ExamplesBrowser.hpp"
#include "../core/I18n.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <imgui.h>

namespace fs = std::filesystem;
using scinodes::ExampleEntry;
using scinodes::LinearExampleLibrary;
using scinodes::SearchHit;
using scinodes::SearchQuery;

namespace {

// Devuelve la primera carpeta candidata que contenga al menos un `.scn`.
// Permite correr el binario desde repo root, desde build/, o desde
// build/Debug, sin hardcodear nada.  El env var SCINODES_EXAMPLES_DIR
// gana siempre.  No miramos `index.json` — la migración a metadata
// embebida ya pasó, los ejemplos se autodescriben.
std::string resolveExamplesDir() {
    auto hasScn = [](const fs::path& dir) {
        if (!fs::exists(dir) || !fs::is_directory(dir)) return false;
        for (const auto& de : fs::directory_iterator(dir))
            if (de.is_regular_file() && de.path().extension() == ".scn")
                return true;
        return false;
    };

    if (const char* override_ = std::getenv("SCINODES_EXAMPLES_DIR")) {
        if (hasScn(override_)) return override_;
    }
    const char* candidates[] = {
        "examples/graphs",
        "../examples/graphs",
        "../../examples/graphs",
        "../../../examples/graphs",
    };
    for (const char* rel : candidates) {
        if (hasScn(rel)) return fs::absolute(rel).string();
    }
    return {};
}

}  // namespace

// ---------------------------------------------------------------------------
void ExamplesBrowser::rebuildLibrary() {
    m_library.reset();
    m_hits.clear();
    m_selected = -1;
    m_loadError.clear();

    m_examplesDir = resolveExamplesDir();
    if (m_examplesDir.empty()) {
        m_loadError =
            "No encontré un directorio examples/graphs/ con archivos .scn.  "
            "Lanza SciNodes desde la raíz del repo, o ajusta "
            "SCINODES_EXAMPLES_DIR.";
        return;
    }

    m_library = std::make_unique<LinearExampleLibrary>(m_examplesDir);
    m_library->refresh();
    recomputeHits();
}

void ExamplesBrowser::recomputeHits() {
    if (!m_library) { m_hits.clear(); m_selected = -1; return; }

    SearchQuery q;
    q.text = m_searchBuf;
    m_hits = m_library->search(q);

    // Re-seleccionar la entry previa si sigue presente — la búsqueda
    // pudo cambiar el orden o la cantidad de hits.
    m_selected = -1;
    if (!m_selectedPath.empty()) {
        for (int i = 0; i < static_cast<int>(m_hits.size()); ++i) {
            if (m_hits[i].entry.file.string() == m_selectedPath) {
                m_selected = i; break;
            }
        }
    }
    if (m_selected < 0 && !m_hits.empty()) {
        m_selected     = 0;
        m_selectedPath = m_hits[0].entry.file.string();
    }
}

// ---------------------------------------------------------------------------
ExamplesBrowser::Action ExamplesBrowser::draw() {
    if (!m_open) return Action::None;
    if (m_needsLoad) { rebuildLibrary(); m_needsLoad = false; }

    Action requested = Action::None;
    m_pickedPath.clear();

    ImGui::SetNextWindowSize({860, 520}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(scinodes::tr("examples.title").c_str(), &m_open)) {
        ImGui::End(); return Action::None;
    }

    if (!m_loadError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 120, 100, 255));
        ImGui::TextWrapped("%s", m_loadError.c_str());
        ImGui::PopStyleColor();
        if (ImGui::Button("Reintentar")) m_needsLoad = true;
        ImGui::End();
        return Action::None;
    }

    // Search arriba a todo el ancho.  Cualquier cambio en el buffer
    // dispara una re-búsqueda — barata mientras la biblioteca sea
    // del orden de docenas; cuando crezca, mover a un debouncer.
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##search",
            scinodes::tr("examples.search").c_str(),
            m_searchBuf, sizeof(m_searchBuf))) {
        recomputeHits();
    }
    ImGui::Separator();

    // Layout: lista (izquierda) | descripción (derecha).
    const float leftWidth = ImGui::GetContentRegionAvail().x * 0.32f;
    ImGui::BeginChild("##list", {leftWidth, 0}, true);
    if (m_hits.empty()) {
        ImGui::TextDisabled("%s", scinodes::tr("examples.no_results").c_str());
    } else {
        for (int i = 0; i < static_cast<int>(m_hits.size()); ++i) {
            const auto& hit = m_hits[i];
            const bool sel = (i == m_selected);
            // El label incluye el id como prefijo discreto — útil cuando
            // el título es largo o ambiguo.  ImGui necesita un id único
            // por Selectable, lo conseguimos con "##" + path.
            std::string label = hit.entry.id + "  " + hit.entry.title;
            label += "##" + hit.entry.file.string();
            if (ImGui::Selectable(label.c_str(), sel)) {
                m_selected     = i;
                m_selectedPath = hit.entry.file.string();
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##detail", {0, 0}, false);
    if (m_selected >= 0 && m_selected < static_cast<int>(m_hits.size())) {
        const SearchHit& hit = m_hits[m_selected];
        const ExampleEntry& e = hit.entry;

        ImGui::TextWrapped("%s", e.title.c_str());
        ImGui::Separator();

        if (!e.tags.empty()) {
            ImGui::TextDisabled("%s", scinodes::tr("examples.tags").c_str());
            ImGui::SameLine();
            for (size_t i = 0; i < e.tags.size(); ++i) {
                if (i) { ImGui::SameLine(); ImGui::TextUnformatted("·"); ImGui::SameLine(); }
                ImGui::TextUnformatted(e.tags[i].c_str());
            }
        }

        // Si hubo búsqueda y matcheó en algún campo, mostrar dónde — da
        // contexto al usuario sobre por qué este hit aparece.  Sin
        // matchedFields (query vacía o sólo facetas), no se renderiza.
        if (!hit.matchedFields.empty()) {
            ImGui::TextDisabled("matched: ");
            ImGui::SameLine();
            for (size_t i = 0; i < hit.matchedFields.size(); ++i) {
                if (i) { ImGui::SameLine(); ImGui::TextUnformatted(","); ImGui::SameLine(); }
                ImGui::TextUnformatted(hit.matchedFields[i].c_str());
            }
        }

        ImGui::Spacing();

        // Descripción scrolleable.  No intentamos resaltar el snippet
        // dentro de la descripción completa — eso requiere RichText
        // que ImGui no provee out-of-the-box.  Si el match fue en
        // description, el snippet ya lo da la librería separado.
        const float footerH = ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("##desc",
                          {0, ImGui::GetContentRegionAvail().y - footerH},
                          true);
        ImGui::TextWrapped("%s", e.description.c_str());
        ImGui::EndChild();

        if (ImGui::Button(scinodes::tr("examples.load").c_str())) {
            m_pickedPath = e.file.string();
            requested    = Action::Load;
            m_open       = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(scinodes::tr("examples.import").c_str())) {
            m_pickedPath = e.file.string();
            requested    = Action::Import;
            m_open       = false;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", e.file.string().c_str());
    } else {
        ImGui::TextDisabled("%s", scinodes::tr("examples.select_one").c_str());
    }
    ImGui::EndChild();

    ImGui::End();
    return requested;
}
