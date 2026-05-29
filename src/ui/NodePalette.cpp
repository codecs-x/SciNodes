#include "NodePalette.hpp"
#include "../core/I18n.hpp"

#include <imgui.h>
#include <cstring>
#include <cctype>
#include <optional>

// ---------------------------------------------------------------------------
// Node lists per category (must match NodeType.hpp / nodeRegistry())
// ---------------------------------------------------------------------------
const NodeType NodePalette::s_sources[] = {
    NodeType::VoltageSource,
    NodeType::CurrentSource,
    NodeType::StepSignal,
    NodeType::SineSignal,
    NodeType::RampSignal,
};
const NodeType NodePalette::s_transformers[] = {
    NodeType::Gain,
    NodeType::Summation,
    NodeType::Integrator,
    NodeType::Differentiator,
    NodeType::LowPassFilter,
    NodeType::PIDController,
    NodeType::TransferFunction,
    NodeType::TransferFunction2,
    NodeType::Saturation,
    NodeType::GearTransmission,
    NodeType::InverseKinematics,
};
// Dispositivos físicos — cuarta categoría gramatical (junto a
// Source/Transformer/Sink).  Los Device se comportan como Transformers
// para las reglas R1-R5 pero llevan modelo 3-D asociado vía contrato.
const NodeType NodePalette::s_devices[] = {
    NodeType::DCMotorModel,
};
const NodeType NodePalette::s_sinks[] = {
    NodeType::Oscilloscope,
    NodeType::FFTAnalyzer,
    NodeType::PhasePortrait,
    NodeType::DataLogger,
    NodeType::TerminalDisplay,
};

// Case-insensitive substring match
static bool matchesFilter(const char* label, const char* filter) {
    if (filter[0] == '\0') return true;
    // simple case-insensitive search
    const char* l = label;
    while (*l) {
        const char* p = l;
        const char* f = filter;
        while (*p && *f && (std::tolower((unsigned char)*p) ==
                            std::tolower((unsigned char)*f))) {
            ++p; ++f;
        }
        if (*f == '\0') return true;
        ++l;
    }
    return false;
}

template<int N>
static std::optional<NodeType> drawCategory(
        const char* label, const NodeType (&types)[N],
        const char* filter, ImU32 headerColor)
{
    std::optional<NodeType> clicked;

    ImGui::PushStyleColor(ImGuiCol_Header,        headerColor);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::ColorConvertFloat4ToU32(
        {ImGui::GetStyle().Colors[ImGuiCol_HeaderHovered].x,
         ImGui::GetStyle().Colors[ImGuiCol_HeaderHovered].y,
         ImGui::GetStyle().Colors[ImGuiCol_HeaderHovered].z, 1.0f}));

    bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(2);

    if (open) {
        ImGui::Indent(8.0f);
        for (const auto& t : types) {
            // Display label traducido si hay clave i18n; fallback al
            // labelOf() del registry (mantiene búsqueda funcionando
            // con cualquier idioma activo).
            const std::string lblStr = scinodes::trOr(
                std::string("node.") + typeName(t) + ".label",
                labelOf(t));
            const char* lbl = lblStr.c_str();
            if (!matchesFilter(lbl, filter)) continue;

            ImGui::Bullet();
            ImGui::SameLine();
            if (ImGui::Selectable(lbl, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                clicked = t;
            }
            if (ImGui::IsItemHovered()) {
                const auto& def = nodeRegistry().at(t);
                const std::string desc = scinodes::trOr(
                    std::string("node.") + typeName(t) + ".description",
                    def.description);
                // Wrap a ~25 caracteres de fuente para que las
                // descripciones largas (SubGraph, PIDController…) no
                // se muestren como una sola línea kilométrica.
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.f);
                ImGui::TextUnformatted(desc.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        ImGui::Unindent(8.0f);
    }
    return clicked;
}

std::optional<NodeType> NodePalette::draw() {
    ImGui::Begin("Node Palette");

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##search",
                             scinodes::tr("nodepalette.search").c_str(),
                             m_search, sizeof(m_search));
    ImGui::Separator();

    std::optional<NodeType> result;

    auto r = drawCategory("  Sources",      s_sources,      m_search, IM_COL32(30, 100, 40, 200));
    if (r) result = r;

    r = drawCategory("  Transformers", s_transformers, m_search, IM_COL32(30, 60, 140, 200));
    if (r) result = r;

    r = drawCategory("  Devices",      s_devices,      m_search, IM_COL32(110, 60, 160, 200));
    if (r) result = r;

    r = drawCategory("  Sinks",        s_sinks,        m_search, IM_COL32(140, 30, 30, 200));
    if (r) result = r;

    ImGui::Separator();
    ImGui::TextDisabled("%s", scinodes::tr("nodepalette.hint").c_str());

    ImGui::End();
    return result;
}
