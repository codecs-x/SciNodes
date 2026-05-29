#include "WorkspaceManager.hpp"

#include <imgui_internal.h>

namespace scinodes::ui {

// ===========================================================================
// drawTabs — barra superior con un botón por workspace.  Cambiar de
// workspace marca el layout para reconstrucción en el próximo frame.
// ===========================================================================
void WorkspaceManager::drawTabs() {
    struct WSInfo { Workspace ws; const char* label; };
    static const WSInfo kTabs[] = {
        { Workspace::Design,        "Design"        },
        { Workspace::Simulation2D,  "2D Simulation" },
        { Workspace::Simulation3D,  "3D Simulation" },
    };

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));
    for (const auto& tab : kTabs) {
        const bool active = (m_current == tab.ws);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32( 70, 110, 180, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32( 80, 130, 200, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32( 70, 110, 180, 255));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32( 40,  44,  52, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32( 55,  60,  72, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32( 60,  80, 120, 255));
        }
        if (ImGui::Button(tab.label) && !active) {
            m_current      = tab.ws;
            m_needsRebuild = true;
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
    }
    ImGui::PopStyleVar();
}

// ===========================================================================
// buildIfNeeded — punto de entrada del lado de AppWindow.  No reentrante:
// solo construye el layout cuando hay rebuild pendiente.
// ===========================================================================
bool WorkspaceManager::buildIfNeeded(ImGuiID dockId) {
    if (!m_needsRebuild) return false;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId,
        ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_NoWindowMenuButton);
    ImGui::DockBuilderSetNodeSize(dockId, vp->Size);

    // Reset previo: todas las Areas vacías; el builder específico
    // del workspace las llena.
    for (auto& a : m_areas) a.setPanel(nullptr);

    switch (m_current) {
        case Workspace::Design:       buildDesign       (dockId); break;
        case Workspace::Simulation2D: buildSimulation2D (dockId); break;
        case Workspace::Simulation3D: buildSimulation3D (dockId); break;
    }

    ImGui::DockBuilderFinish(dockId);
    m_needsRebuild = false;
    return true;
}

const char* WorkspaceManager::takePendingFocus() {
    const char* p = m_pendingFocus;
    m_pendingFocus = nullptr;
    return p;
}

// ===========================================================================
// Layouts concretos.
// ===========================================================================
void WorkspaceManager::assignArea(int areaIndex, const char* panelTypeId) {
    if (areaIndex < 0 || areaIndex >= (int)m_areas.size()) return;
    m_areas[areaIndex].setPanel(m_registry.find(panelTypeId));
}

void WorkspaceManager::buildDesign(ImGuiID dockId) {
    // ┌──────────────────────┬───────────┐
    // │     Node Editor      │ Outliner  │
    // └──────────────────────┴───────────┘
    assignArea(0, "node_editor");
    assignArea(1, "outliner");
    ImGuiID rightId, centerId;
    ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Right, 0.28f,
                                &rightId, &centerId);
    ImGui::DockBuilderDockWindow(m_areas[0].windowName().c_str(), centerId);
    ImGui::DockBuilderDockWindow(m_areas[1].windowName().c_str(), rightId);
    m_pendingFocus = m_areas[0].windowName().c_str();
}

void WorkspaceManager::buildSimulation2D(ImGuiID dockId) {
    // ┌──────────────────────────────┐
    // │      Node Editor             │
    // ├──────────────────────────────┤
    // │      Plots                   │
    // └──────────────────────────────┘
    assignArea(0, "node_editor");
    assignArea(1, "plots");
    ImGuiID topId, botId;
    ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Down, 0.40f,
                                &botId, &topId);
    ImGui::DockBuilderDockWindow(m_areas[0].windowName().c_str(), topId);
    ImGui::DockBuilderDockWindow(m_areas[1].windowName().c_str(), botId);
    m_pendingFocus = m_areas[1].windowName().c_str();
}

void WorkspaceManager::buildSimulation3D(ImGuiID dockId) {
    // ┌────────────────────────┬──────────┐
    // │                        │  Plots   │
    // │       3D View          ├──────────┤
    // │                        │ Outliner │
    // ├────────────────────────┤          │
    // │       Node Editor      │          │
    // └────────────────────────┴──────────┘
    assignArea(0, "view3d");
    assignArea(1, "plots");
    assignArea(2, "outliner");
    assignArea(3, "node_editor");

    ImGuiID rightId, leftColId;
    ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Right, 0.28f,
                                &rightId, &leftColId);
    ImGuiID nodeEditorId, view3dId;
    ImGui::DockBuilderSplitNode(leftColId, ImGuiDir_Down, 0.35f,
                                &nodeEditorId, &view3dId);
    ImGuiID rightTopId, rightBotId;
    ImGui::DockBuilderSplitNode(rightId, ImGuiDir_Down, 0.45f,
                                &rightBotId, &rightTopId);
    ImGui::DockBuilderDockWindow(m_areas[0].windowName().c_str(), view3dId);
    ImGui::DockBuilderDockWindow(m_areas[3].windowName().c_str(), nodeEditorId);
    ImGui::DockBuilderDockWindow(m_areas[1].windowName().c_str(), rightTopId);
    ImGui::DockBuilderDockWindow(m_areas[2].windowName().c_str(), rightBotId);
    m_pendingFocus = m_areas[0].windowName().c_str();
}

}  // namespace scinodes::ui
