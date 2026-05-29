#include "PanelInterface.hpp"
#include "../core/I18n.hpp"

#include <imgui_internal.h>

#include <cstdio>
#include <cstring>

namespace scinodes::ui {

// ===========================================================================
// PanelRegistry
// ===========================================================================
void PanelRegistry::add(std::unique_ptr<IPanel> panel) {
    if (!panel) return;
    // Reemplazar si ya existe uno con el mismo typeId.
    for (auto& p : m_panels) {
        if (std::strcmp(p->typeId(), panel->typeId()) == 0) {
            p = std::move(panel);
            return;
        }
    }
    m_panels.push_back(std::move(panel));
}

IPanel* PanelRegistry::find(const char* typeId) const {
    if (!typeId) return nullptr;
    for (const auto& p : m_panels) {
        if (std::strcmp(p->typeId(), typeId) == 0) return p.get();
    }
    return nullptr;
}

std::vector<IPanel*> PanelRegistry::list() const {
    std::vector<IPanel*> out;
    out.reserve(m_panels.size());
    for (const auto& p : m_panels) out.push_back(p.get());
    return out;
}

// ===========================================================================
// Area
// ===========================================================================
Area::Area(int id, IPanel* initial) : m_id(id), m_current(initial) {
    refreshWindowName();
}

void Area::setPanel(IPanel* p) {
    m_current = p;
    refreshWindowName();
}

IPanel* Area::takePendingSwap() {
    IPanel* p = m_pendingSwap;
    m_pendingSwap = nullptr;
    return p;
}

void Area::refreshWindowName() {
    // `###area_N` es el ID estable que ImGui usa para dock + ini state.
    // Antes del ### va el título visible, que cambia con el panel
    // actual.  Si no hay panel, mostramos "(empty)".
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s###area_%d",
                  m_current ? m_current->displayName() : scinodes::tr("panel.empty").c_str(),
                  m_id);
    m_windowName = buf;
}

void Area::drawTypeSelector(PanelRegistry& registry) {
    if (!ImGui::BeginMenuBar()) return;

    // Dropdown para cambiar el panel — etiqueta genérica ("..."), no
    // el nombre del panel.  El tab del Area ya muestra el nombre en el
    // título de la ventana ImGui; duplicarlo aquí abajo era ruido visual
    // (bug reportado: cada panel mostraba su nombre dos veces).
    char menuLabel[32];
    std::snprintf(menuLabel, sizeof(menuLabel), "...##area_%d_sel", m_id);

    // Resaltado del menú para que se distinga del fondo del menu bar
    // y se perciba como "clickeable".
    ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32(50, 60, 80, 220));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(80, 100, 140, 230));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  IM_COL32(100, 130, 180, 255));
    if (ImGui::BeginMenu(menuLabel)) {
        ImGui::TextDisabled("Show in this slot:");
        ImGui::Separator();
        for (IPanel* p : registry.list()) {
            const bool isCurrent = (p == m_current);
            if (ImGui::MenuItem(p->displayName(), nullptr, isCurrent)) {
                if (!isCurrent) m_pendingSwap = p;
            }
        }
        ImGui::EndMenu();
    }
    ImGui::PopStyleColor(3);

    ImGui::EndMenuBar();
}

void Area::draw(PanelRegistry& registry) {
    if (!m_current) return;  // area vacía no abre window

    // Begin con MenuBar flag para que el selector tenga dónde vivir.
    // Sin background propio — el panel contenido aporta su estilo si
    // lo necesita.
    if (!ImGui::Begin(m_windowName.c_str(), nullptr,
                      ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    drawTypeSelector(registry);
    m_current->drawContent();

    ImGui::End();
}

}  // namespace scinodes::ui
