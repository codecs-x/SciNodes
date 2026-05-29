#include "OutlinerPanel.hpp"

#include "../core/ContractRegistry.hpp"
#include "../core/I18n.hpp"
#include "../core/NodeType.hpp"

#include <imgui.h>

#include <cstdio>

namespace {

const ImVec4 kOK    = { 0.30f, 0.90f, 0.50f, 1.00f };
const ImVec4 kBad   = { 0.95f, 0.50f, 0.30f, 1.00f };
const ImVec4 kDim   = { 0.60f, 0.62f, 0.68f, 1.00f };

// Recorta una ruta larga a "…/<últimas N partes>" para que quepa en la
// fila sin desbordar el panel.  Si ya es corta, la devuelve tal cual.
std::string shortPath(const std::string& p) {
    constexpr size_t kLimit = 40;
    if (p.size() <= kLimit) return p;
    return std::string("…/") + p.substr(p.size() - kLimit + 2);
}

}  // namespace

void OutlinerPanel::drawContent(NodeCanvas& canvas) {
    // Focus-follows-mouse estilo Blender (ver NodeCanvas para racional).
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::IsAnyItemActive()) {
        ImGui::SetWindowFocus();
    }

    const NodeGraph& graph = canvas.graph();
    const auto&      assets = canvas.loadedAssets();

    // ---- Catálogo de objetos 3D del proyecto -----------------------------
    // Sección superior — los modelos importados vía Menú Archivo →
    // Importar modelo 3D viven aquí.  Los nodos Object3D del grafo los
    // referencian por nombre.  Separado de los nodos Device (sección de
    // abajo) porque son conceptualmente proyecto-level, no nodo-level.
    if (!graph.importedObjects().empty()) {
        const bool catOpen = ImGui::TreeNodeEx(
            scinodes::tr("outliner.catalog_3d").c_str(),
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
        if (catOpen) {
            for (const auto& obj : graph.importedObjects()) {
                ImGui::PushID(obj.name.c_str());
                char header[160];
                // Glifo "▣" (U+25A3, BMP) — la fuente default de ImGui
                // sí lo renderea; "📦" (U+1F4E6, supplementary plane)
                // aparece como "�" porque la fuente no carga ese rango.
                std::snprintf(header, sizeof(header),
                    "▣ %s  (%zu partes)",
                    obj.name.c_str(), obj.parts.size());
                const bool objOpen = ImGui::TreeNodeEx(
                    header, ImGuiTreeNodeFlags_DefaultOpen);
                if (objOpen) {
                    ImGui::TextDisabled("%s: %s",
                        scinodes::tr("outliner.asset_prefix").c_str(),
                        shortPath(obj.path).c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", obj.path.c_str());

                    if (ImGui::SmallButton(scinodes::tr("outliner.btn.remove").c_str()))
                        canvas.removeImportedObject(obj.name);

                    for (const auto& part : obj.parts) {
                        ImGui::BulletText("%s", part.c_str());
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::Separator();
    }

    // Recolectamos los nodos Device primero — el resto del grafo no
    // aporta al Outliner por ahora (en una versión futura se puede
    // mostrar también la jerarquía completa).
    bool found = false;
    for (const auto& n : graph.nodes()) {
        const NodeDef& def = defOf(n);
        if (def.category != NodeCategory::Device) continue;
        found = true;

        // ---- Cabecera del nodo ----------------------------------------
        ImGui::PushID(n.id);

        const auto* contract =
            canvas.contractRegistry()
                ? canvas.contractRegistry()->find(typeName(n.type))
                : nullptr;

        char header[128];
        if (n.assetPath.empty()) {
            std::snprintf(header, sizeof(header),
                "%s #%d  (sin asset)", def.label.c_str(), n.id);
        } else {
            std::snprintf(header, sizeof(header),
                "%s #%d", def.label.c_str(), n.id);
        }

        const bool open = ImGui::TreeNodeEx(header,
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);

        // ---- Línea de estado del contrato (mismo nivel que la cabecera) ---
        ImGui::SameLine();
        if (!contract) {
            ImGui::TextColored(kDim, "%s",
                scinodes::tr("outliner.no_contract").c_str());
        } else if (n.assetPath.empty()) {
            ImGui::TextColored(kDim, "%s",
                scinodes::tr("outliner.no_asset_hint").c_str());
        } else {
            auto it = assets.find(n.id);
            if (it == assets.end()) {
                ImGui::TextColored(kDim, "(cargando…)");
            } else if (it->second.valid()) {
                ImGui::TextColored(kOK, "  ✓ %s", contract->device_type.c_str());
            } else {
                ImGui::TextColored(kBad, "  ✗ faltan %zu",
                                   it->second.missing.size());
            }
        }

        if (open) {
            // ---- Path del asset ---------------------------------------
            if (!n.assetPath.empty()) {
                ImGui::TextDisabled("%s: %s",
                                    scinodes::tr("outliner.asset_prefix").c_str(),
                                    shortPath(n.assetPath).c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", n.assetPath.c_str());

                // Botones de acción sobre el asset.
                if (ImGui::SmallButton(scinodes::tr("outliner.btn.reload").c_str()))
                    canvas.reloadAsset(n.id);
                ImGui::SameLine();
                if (ImGui::SmallButton(scinodes::tr("outliner.btn.remove").c_str()))
                    canvas.detachAsset(n.id);
            }

            // ---- Contenido del asset (si está cargado y válido) -------
            auto it = assets.find(n.id);
            if (contract && it != assets.end()) {
                const auto& asset = it->second;

                if (!asset.missing.empty()) {
                    ImGui::TextColored(kBad, "%s",
                        scinodes::tr("outliner.contract_missing").c_str());
                    for (const auto& m : asset.missing)
                        ImGui::BulletText("%s", m.c_str());
                }

                if (!asset.parts.empty()) {
                    if (ImGui::TreeNodeEx(scinodes::tr("outliner.parts").c_str(),
                            ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (const auto& [name, mesh] : asset.parts) {
                            ImGui::BulletText("%s  (%zu vértices)",
                                name.c_str(), mesh.positions.size() / 3);
                        }
                        ImGui::TreePop();
                    }
                }

                if (!asset.joints.empty()) {
                    if (ImGui::TreeNodeEx(scinodes::tr("outliner.joints").c_str(),
                            ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (const auto& [name, jf] : asset.joints) {
                            ImGui::BulletText(
                                "%s  (%s, %s→%s, driven by %s)",
                                name.c_str(),
                                jf.type.c_str(),
                                jf.parent.c_str(),
                                jf.child.c_str(),
                                jf.driven_by.empty() ? "—" : jf.driven_by.c_str());
                        }
                        ImGui::TreePop();
                    }
                }

                if (!asset.anchors.empty()) {
                    if (ImGui::TreeNodeEx(scinodes::tr("outliner.anchors").c_str(),
                            ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (const auto& [name, a] : asset.anchors) {
                            ImGui::BulletText(
                                "%s  [%s]  @ (%.3f, %.3f, %.3f)",
                                name.c_str(),
                                a.kind.empty() ? "?" : a.kind.c_str(),
                                a.position[0], a.position[1], a.position[2]);
                        }
                        ImGui::TreePop();
                    }
                }

                if (!asset.warnings.empty()) {
                    if (ImGui::TreeNodeEx(scinodes::tr("outliner.warnings").c_str())) {
                        for (const auto& w : asset.warnings)
                            ImGui::BulletText("%s", w.c_str());
                        ImGui::TreePop();
                    }
                }
            }

            ImGui::TreePop();
        }

        ImGui::PopID();
        ImGui::Separator();
    }

    if (!found) {
        ImGui::TextDisabled(
            "No hay nodos físicos (NodeCategory::Device) en el grafo.\n"
            "Añade un nodo como DCMotorModel para verlo aquí.");
    }
}
