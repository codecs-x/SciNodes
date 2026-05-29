#include "AboutGraphPanel.hpp"
#include "../core/I18n.hpp"
#include "../core/NodeGraph.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <imgui.h>
#include <sstream>

namespace {

// Trim de whitespace en ambos extremos.  No usamos boost ni std::ranges
// para mantener la base mínima (stdlib + ctype) — política del proyecto.
std::string trim(const std::string& s) {
    auto isSpace = [](unsigned char c) { return std::isspace(c); };
    auto b = std::find_if_not(s.begin(),  s.end(),  isSpace);
    auto e = std::find_if_not(s.rbegin(), s.rend(), isSpace).base();
    if (b >= e) return {};
    return std::string(b, e);
}

// Split de tags por comas, con trim por elemento, descartando vacíos.
// Eso permite al usuario escribir "control, pid,  ogata" sin pensar.
std::vector<std::string> splitTags(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token = trim(token);
        if (!token.empty()) out.push_back(std::move(token));
    }
    return out;
}

// Junta los tags como "a, b, c" para mostrarlos en el input single-line.
std::string joinTags(const std::vector<std::string>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) out += ", ";
        out += v[i];
    }
    return out;
}

// Copia segura a buffer ImGui-style con null terminator garantizado.
void copyToBuf(char* dst, std::size_t cap, const std::string& src) {
    if (cap == 0) return;
    const std::size_t n = std::min(src.size(), cap - 1);
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

}  // namespace

void AboutGraphPanel::open(const NodeCanvas& canvas) {
    const NodeGraph& g = canvas.graph();
    m_idView             = g.id();
    copyToBuf(m_titleBuf, sizeof(m_titleBuf), g.title());
    m_descriptionBuf     = g.description();
    copyToBuf(m_tagsBuf,  sizeof(m_tagsBuf),  joinTags(g.tags()));
    // Empezar en modo lectura: la descripción wrappeada cabe más texto
    // de un vistazo que un editor con scroll horizontal.  El usuario
    // entra explícitamente al modo edición con el botón.
    m_editingDescription = false;
    m_open               = true;
}

void AboutGraphPanel::draw(NodeCanvas& canvas) {
    if (!m_open) return;

    ImGui::SetNextWindowSize({560, 480}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(scinodes::trOr("about_graph.title",
                                     "About this graph").c_str(),
                      &m_open)) {
        ImGui::End();
        return;
    }

    // ID — sólo lectura.  Es la identidad estable del documento;
    // editarlo desde aquí rompería referencias de otros sitios.
    ImGui::TextDisabled("%s", scinodes::trOr("about_graph.id", "ID").c_str());
    ImGui::SameLine();
    if (m_idView.empty())
        ImGui::TextDisabled("%s",
            scinodes::trOr("about_graph.no_id", "(sin id)").c_str());
    else
        ImGui::TextUnformatted(m_idView.c_str());

    ImGui::Spacing();

    // Title
    ImGui::TextDisabled("%s",
        scinodes::trOr("about_graph.title_label", "Título").c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##about_title", m_titleBuf, sizeof(m_titleBuf));

    ImGui::Spacing();

    // Description — comentario libre a nivel grafo, equivalente al
    // `comment` por-nodo pero para el documento entero.  Dual-mode
    // porque ImGui no provee word-wrap en InputTextMultiline:
    //   - Vista (default): TextWrapped en un Child scrolleable.  Siempre
    //     wrappea al ancho disponible.  Lee perfecto incluso para los
    //     párrafos largos de los ejemplos migrados.
    //   - Edición: InputTextMultiline.  Toggleable con el botón a la
    //     derecha del label.
    ImGui::TextDisabled("%s",
        scinodes::trOr("about_graph.description_label", "Descripción").c_str());
    ImGui::SameLine();
    ImGui::Dummy({ImGui::GetContentRegionAvail().x - 90.f, 0.f});
    ImGui::SameLine();
    const char* toggleLabel = m_editingDescription
        ? scinodes::trOr("about_graph.done", "Listo").c_str()
        : scinodes::trOr("about_graph.edit", "Editar").c_str();
    if (ImGui::SmallButton(toggleLabel))
        m_editingDescription = !m_editingDescription;

    const ImVec2 descSize{ -FLT_MIN, ImGui::GetTextLineHeight() * 10.f };
    if (m_editingDescription) {
        // Cap a 4096 chars es generoso para una descripción de documento;
        // si alguien necesita más, mejor cambiar a un editor externo.
        constexpr std::size_t kDescCap = 4096;
        char descBuf[kDescCap];
        copyToBuf(descBuf, kDescCap, m_descriptionBuf);
        if (ImGui::InputTextMultiline("##about_desc", descBuf, kDescCap, descSize))
            m_descriptionBuf = descBuf;
    } else {
        // Vista wrappeada read-only en un Child con borde — refleja
        // visualmente que es un "campo" y no narrativa libre.  Scroll
        // vertical automático si supera la altura.
        ImGui::BeginChild("##about_desc_view", descSize, true);
        if (m_descriptionBuf.empty())
            ImGui::TextDisabled("%s",
                scinodes::trOr("about_graph.description_empty",
                               "(sin descripción)").c_str());
        else
            ImGui::TextWrapped("%s", m_descriptionBuf.c_str());
        ImGui::EndChild();
    }

    ImGui::Spacing();

    // Tags como single-line comma-separated.  La conversión a vector la
    // hace splitTags al aplicar.
    ImGui::TextDisabled("%s",
        scinodes::trOr("about_graph.tags_label", "Tags").c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s",
        scinodes::trOr("about_graph.tags_hint",
                       "(separados por coma)").c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##about_tags", m_tagsBuf, sizeof(m_tagsBuf));

    ImGui::Separator();

    if (ImGui::Button(scinodes::trOr("about_graph.apply", "Aplicar").c_str(),
                      {120, 0})) {
        canvas.setGraphTitle      (m_titleBuf);
        canvas.setGraphDescription(m_descriptionBuf);
        canvas.setGraphTags       (splitTags(m_tagsBuf));
        m_open = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(scinodes::trOr("about_graph.cancel", "Cancelar").c_str(),
                      {120, 0})) {
        m_open = false;
    }

    ImGui::End();
}
