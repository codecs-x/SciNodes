#include "AssetMappingPanel.hpp"

// tinygltf — usado SOLO para listar los nombres de los nodos del .gltf.
// TINYGLTF_IMPLEMENTATION ya está definido en src/core/DeviceAsset.cpp, así
// que aquí lo dejamos como header puro.
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_USE_CPP14
#include <tiny_gltf.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr float kAxisEps = 1e-4f;

const char* axisLabel(AssetMappingPanel::AxisMode m) {
    switch (m) {
        case AssetMappingPanel::AxisMode::PX: return "+X";
        case AssetMappingPanel::AxisMode::NX: return "-X";
        case AssetMappingPanel::AxisMode::PY: return "+Y";
        case AssetMappingPanel::AxisMode::NY: return "-Y";
        case AssetMappingPanel::AxisMode::PZ: return "+Z";
        case AssetMappingPanel::AxisMode::NZ: return "-Z";
        case AssetMappingPanel::AxisMode::Custom: return "Custom";
    }
    return "?";
}

bool approxEq(float a, float b) { return std::fabs(a - b) < kAxisEps; }

// Texto a mostrar para "sin asignar" en los dropdowns.
constexpr const char* kUnassignedLabel = "(sin asignar)";

}  // namespace

// ===========================================================================
AssetMappingPanel::AssetMappingPanel() = default;

// ===========================================================================
AssetMappingPanel::AxisMode
AssetMappingPanel::axisModeFromVec(const std::array<float, 3>& v) {
    if (approxEq(v[0],  1.f) && approxEq(v[1], 0.f) && approxEq(v[2], 0.f))
        return AxisMode::PX;
    if (approxEq(v[0], -1.f) && approxEq(v[1], 0.f) && approxEq(v[2], 0.f))
        return AxisMode::NX;
    if (approxEq(v[0], 0.f) && approxEq(v[1],  1.f) && approxEq(v[2], 0.f))
        return AxisMode::PY;
    if (approxEq(v[0], 0.f) && approxEq(v[1], -1.f) && approxEq(v[2], 0.f))
        return AxisMode::NY;
    if (approxEq(v[0], 0.f) && approxEq(v[1], 0.f) && approxEq(v[2],  1.f))
        return AxisMode::PZ;
    if (approxEq(v[0], 0.f) && approxEq(v[1], 0.f) && approxEq(v[2], -1.f))
        return AxisMode::NZ;
    return AxisMode::Custom;
}

std::array<float, 3>
AssetMappingPanel::vecFromAxisMode(AxisMode m) {
    switch (m) {
        case AxisMode::PX: return {{  1.f,  0.f,  0.f }};
        case AxisMode::NX: return {{ -1.f,  0.f,  0.f }};
        case AxisMode::PY: return {{  0.f,  1.f,  0.f }};
        case AxisMode::NY: return {{  0.f, -1.f,  0.f }};
        case AxisMode::PZ: return {{  0.f,  0.f,  1.f }};
        case AxisMode::NZ: return {{  0.f,  0.f, -1.f }};
        case AxisMode::Custom: return {{ 0.f, 1.f, 0.f }};
    }
    return {{ 0.f, 1.f, 0.f }};
}

// ===========================================================================
bool AssetMappingPanel::openFor(const std::string& assetPath,
                                const scinodes::DeviceContract& contract) {
    if (assetPath.empty()) return false;
    if (contract.parts.empty() && contract.joints.empty() &&
        contract.anchors.empty()) {
        return false;
    }

    // Re-parsea el glTF para listar nombres de nodos.  No conservamos el
    // modelo; el panel solo necesita los nombres.
    tinygltf::TinyGLTF loader;
    tinygltf::Model    model;
    std::string        warn, err;
    const bool isBinary =
        assetPath.size() >= 4 &&
        (assetPath.substr(assetPath.size() - 4) == ".glb" ||
         assetPath.substr(assetPath.size() - 4) == ".GLB");
    const bool ok = isBinary
        ? loader.LoadBinaryFromFile(&model, &err, &warn, assetPath)
        : loader.LoadASCIIFromFile (&model, &err, &warn, assetPath);
    if (!ok) {
        std::fprintf(stderr,
            "[AssetMappingPanel] no se pudo abrir %s: %s\n",
            assetPath.c_str(), err.c_str());
        return false;
    }

    m_assetPath   = assetPath;
    m_sidecarPath = scinodes::AssetMapping::sidecarPathFor(assetPath);
    m_contract    = contract;

    m_nodeNames.clear();
    m_nodeNames.reserve(model.nodes.size());
    for (const auto& n : model.nodes) {
        // Solo listamos nodos con nombre — los anónimos son inservibles
        // como referencia desde un mapping por nombre.
        if (!n.name.empty()) m_nodeNames.push_back(n.name);
    }
    std::sort(m_nodeNames.begin(), m_nodeNames.end());

    // Si ya hay sidecar en disco, lo cargamos como punto de partida.
    // Si está corrupto, arrancamos con un mapping vacío (el usuario
    // tendrá que llenarlo desde cero).
    std::string sidecarErr;
    m_mapping = scinodes::AssetMapping::loadFromFile(m_sidecarPath, &sidecarErr);
    if (!sidecarErr.empty() || m_mapping.empty()) {
        m_mapping = scinodes::AssetMapping{};
        m_mapping.asset_path  = m_assetPath;
        m_mapping.device_type = contract.device_type;
    }

    // Crear entradas para CADA slot del contrato (aunque el sidecar no
    // las tuviera) — así el UI siempre lista todas las opciones.
    for (const auto& p : contract.parts)   m_mapping.parts.emplace(p.name, scinodes::AssetMapping::PartSlot{});
    for (const auto& j : contract.joints)  m_mapping.joints.emplace(j.name, scinodes::AssetMapping::JointSlot{});
    for (const auto& a : contract.anchors) m_mapping.anchors.emplace(a.name, scinodes::AssetMapping::AnchorSlot{});

    inferAxisModeFromMapping();

    m_open       = true;
    m_justOpened = true;
    m_applied    = false;
    return true;
}

// ===========================================================================
void AssetMappingPanel::inferAxisModeFromMapping() {
    m_jointAxisMode.clear();
    for (const auto& [name, slot] : m_mapping.joints) {
        if (slot.axis_explicit) {
            m_jointAxisMode[name] = axisModeFromVec(slot.axis);
        } else {
            // Sin axis explícito → el loader cae al derivado de
            // node.rotation.  Visualmente lo presentamos como "auto"
            // (Custom con valores actuales del slot, que es Y por
            // default).
            m_jointAxisMode[name] = AxisMode::PY;
        }
    }
}

// ===========================================================================
void AssetMappingPanel::applyAxisModeToSlot(const std::string& jointName) {
    auto it = m_mapping.joints.find(jointName);
    if (it == m_mapping.joints.end()) return;
    auto mode = m_jointAxisMode[jointName];
    if (mode == AxisMode::Custom) {
        it->second.axis_explicit = true;
        // Mantén lo que el usuario haya escrito.
    } else {
        it->second.axis = vecFromAxisMode(mode);
        it->second.axis_explicit = true;
    }
}

// ===========================================================================
bool AssetMappingPanel::drawNodeDropdown(const char* label,
                                        std::string& out,
                                        bool required) {
    bool changed = false;
    const char* preview = out.empty() ? kUnassignedLabel : out.c_str();
    if (ImGui::BeginCombo(label, preview)) {
        // Entrada "(sin asignar)" — solo útil cuando el slot es opcional;
        // para required, lo dejamos visible pero seleccionarlo deja el
        // botón Aplicar deshabilitado.
        bool isUnassigned = out.empty();
        if (ImGui::Selectable(kUnassignedLabel, isUnassigned)) {
            out.clear();
            changed = true;
        }
        ImGui::Separator();
        for (const auto& name : m_nodeNames) {
            bool selected = (name == out);
            if (ImGui::Selectable(name.c_str(), selected)) {
                out = name;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (required && out.empty()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 110, 110, 255));
        ImGui::TextUnformatted("(requerido)");
        ImGui::PopStyleColor();
    }
    return changed;
}

// ===========================================================================
bool AssetMappingPanel::drawFrame() {
    if (!m_open) return false;

    if (m_justOpened) {
        ImGui::OpenPopup("##AssetMappingModal");
        m_justOpened = false;
    }

    // Centrar la modal.
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({560.f, 0.f}, ImGuiCond_Appearing);

    bool applyThisFrame = false;

    if (ImGui::BeginPopupModal("##AssetMappingModal", &m_open,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored({0.85f, 0.92f, 1.f, 1.f},
            "Mapping de dispositivo: %s", m_contract.device_type.c_str());
        ImGui::TextDisabled("Asset: %s", m_assetPath.c_str());
        ImGui::Separator();

        // ---- Parts ----
        if (!m_contract.parts.empty()) {
            ImGui::TextColored({0.7f, 0.85f, 1.f, 1.f}, "Partes");
            ImGui::Indent();
            for (const auto& p : m_contract.parts) {
                ImGui::PushID(("part_" + p.name).c_str());
                ImGui::Text("%s%s", p.name.c_str(),
                            p.required ? "" : "  (opcional)");
                if (!p.doc.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("?");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.doc.c_str());
                }
                auto& slot = m_mapping.parts[p.name];
                drawNodeDropdown("##part_dd", slot.node_name, p.required);
                ImGui::PopID();
            }
            ImGui::Unindent();
            ImGui::Spacing();
        }

        // ---- Joints ----
        if (!m_contract.joints.empty()) {
            ImGui::TextColored({0.7f, 0.85f, 1.f, 1.f}, "Joints");
            ImGui::Indent();
            for (const auto& j : m_contract.joints) {
                ImGui::PushID(("joint_" + j.name).c_str());
                ImGui::Text("%s  (%s, driven_by=%s)%s",
                            j.name.c_str(), j.type.c_str(),
                            j.driven_by.c_str(),
                            j.required ? "" : "  (opcional)");
                auto& slot = m_mapping.joints[j.name];
                ImGui::Text("Nodo origen:");
                ImGui::SameLine();
                drawNodeDropdown("##joint_dd", slot.node_name, j.required);

                // Selector de eje — radio botones para los 6 presets y
                // un séptimo "Custom" que desbloquea 3 inputs.
                ImGui::Text("Eje:");
                ImGui::SameLine();
                AxisMode mode = m_jointAxisMode[j.name];
                bool axisChanged = false;
                auto axisRadio = [&](AxisMode m) {
                    if (ImGui::RadioButton(axisLabel(m), mode == m)) {
                        m_jointAxisMode[j.name] = m;
                        axisChanged = true;
                    }
                    ImGui::SameLine();
                };
                axisRadio(AxisMode::PX);
                axisRadio(AxisMode::NX);
                axisRadio(AxisMode::PY);
                axisRadio(AxisMode::NY);
                axisRadio(AxisMode::PZ);
                axisRadio(AxisMode::NZ);
                if (ImGui::RadioButton("Custom", mode == AxisMode::Custom)) {
                    m_jointAxisMode[j.name] = AxisMode::Custom;
                    axisChanged = true;
                }
                if (axisChanged) applyAxisModeToSlot(j.name);

                if (m_jointAxisMode[j.name] == AxisMode::Custom) {
                    float a[3] = { slot.axis[0], slot.axis[1], slot.axis[2] };
                    ImGui::SetNextItemWidth(220.f);
                    if (ImGui::InputFloat3("##custom_axis", a, "%.4f")) {
                        slot.axis = {{ a[0], a[1], a[2] }};
                        slot.axis_explicit = true;
                    }
                }
                ImGui::Spacing();
                ImGui::PopID();
            }
            ImGui::Unindent();
            ImGui::Spacing();
        }

        // ---- Anchors ----
        if (!m_contract.anchors.empty()) {
            ImGui::TextColored({0.7f, 0.85f, 1.f, 1.f}, "Anclas");
            ImGui::Indent();
            for (const auto& a : m_contract.anchors) {
                ImGui::PushID(("anchor_" + a.name).c_str());
                ImGui::Text("%s  (%s)%s",
                            a.name.c_str(), a.kind.c_str(),
                            a.required ? "" : "  (opcional)");
                if (!a.doc.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("?");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", a.doc.c_str());
                }
                auto& slot = m_mapping.anchors[a.name];
                drawNodeDropdown("##anchor_dd", slot.node_name, a.required);
                ImGui::PopID();
            }
            ImGui::Unindent();
            ImGui::Spacing();
        }

        ImGui::Separator();

        // ---- Footer: validez + botones ----
        bool allRequiredSet = true;
        for (const auto& p : m_contract.parts)
            if (p.required && m_mapping.parts[p.name].node_name.empty())
                allRequiredSet = false;
        for (const auto& j : m_contract.joints)
            if (j.required && m_mapping.joints[j.name].node_name.empty())
                allRequiredSet = false;
        for (const auto& a : m_contract.anchors)
            if (a.required && m_mapping.anchors[a.name].node_name.empty())
                allRequiredSet = false;

        if (!allRequiredSet) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 110, 110, 255));
            ImGui::TextUnformatted(
                "Faltan ranuras requeridas por asignar.");
            ImGui::PopStyleColor();
        }

        if (ImGui::Button("Cancelar", {120, 0})) {
            ImGui::CloseCurrentPopup();
            m_open = false;
        }
        ImGui::SameLine();

        // En vez de envolver el botón en BeginDisabled/EndDisabled
        // (que en este versión de ImGui dispara una aserción de
        // error-recovery por la forma en que se anidan los
        // ItemFlags dentro del popup modal), aplicamos el bloqueo
        // de forma manual: si faltan ranuras required, atenuamos
        // visualmente el botón y rechazamos su click.
        if (!allRequiredSet) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                IM_COL32( 75,  75,  80, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                IM_COL32( 75,  75,  80, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                IM_COL32( 75,  75,  80, 255));
            ImGui::PushStyleColor(ImGuiCol_Text,
                IM_COL32(170, 170, 175, 255));
        }
        const bool applyClicked =
            ImGui::Button("Aplicar y guardar", {180, 0});
        if (!allRequiredSet) ImGui::PopStyleColor(4);

        if (applyClicked && allRequiredSet) {
            // Limpia los slots vacíos antes de exponer el mapping al
            // consumidor: parts/anchors/joints sin node_name no aportan
            // (los opcionales simplemente no se persisten).
            for (auto it = m_mapping.parts.begin(); it != m_mapping.parts.end(); ) {
                if (it->second.node_name.empty()) it = m_mapping.parts.erase(it);
                else                              ++it;
            }
            for (auto it = m_mapping.joints.begin(); it != m_mapping.joints.end(); ) {
                if (it->second.node_name.empty()) it = m_mapping.joints.erase(it);
                else                              ++it;
            }
            for (auto it = m_mapping.anchors.begin(); it != m_mapping.anchors.end(); ) {
                if (it->second.node_name.empty()) it = m_mapping.anchors.erase(it);
                else                              ++it;
            }
            m_applied = true;
            applyThisFrame = true;
            ImGui::CloseCurrentPopup();
            m_open = false;
        }

        ImGui::EndPopup();
    }

    return applyThisFrame;
}
