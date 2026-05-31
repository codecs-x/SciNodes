#pragma once

#include "PanelInterface.hpp"

#include <imgui.h>

#include <array>

// -----------------------------------------------------------------------------
// WorkspaceManager — administra los presets de layout (Blender-style)
// y la barra de tabs que conmuta entre ellos.
//
// Patrón Strategy: cada Workspace es una "estrategia" de asignación de
// IPanel→Area + posiciones del DockBuilder.  Se separa de AppWindow
// (SRP) — AppWindow ya no tiene 80 líneas de switch de layouts
// hardcodeados.
//
// Dependencias inyectadas (refs no-owning):
//   - std::array<Area, N> de la app (para asignarles IPanels)
//   - PanelRegistry (para resolver typeId → IPanel*)
// -----------------------------------------------------------------------------
namespace scinodes::ui {

class WorkspaceManager {
public:
    enum class Workspace {
        Design,           // foco en el grafo
        Simulation2D,     // grafo arriba + plots abajo
        Simulation3D,     // 3D arriba + grafo abajo + plots/outliner derecha
    };

    WorkspaceManager(std::array<Area, 4>& areas, PanelRegistry& registry)
        : m_areas(areas), m_registry(registry) {}

    Workspace current() const { return m_current; }

    // Marca el layout actual para reconstrucción.  Usado por
    // "View → Reset Layout" para forzar restauración del preset.
    void resetCurrentLayout() { m_needsRebuild = true; }

    // Renderiza la barra superior de tabs.  Si el usuario clickea un
    // tab distinto del actual, el layout se marca para reconstrucción.
    void drawTabs();

    // Construye el dock layout para el workspace activo si está
    // marcado para reconstruir.  Idempotente: skip-out cuando ya está
    // construido.  Devuelve true si reconstruyó (AppWindow puede usar
    // esto para tomar foco en la ventana "primaria").
    bool buildIfNeeded(ImGuiID dockId);

    // Nombre del window que debe tomar foco tras un cambio de
    // workspace.  Solo es válido tras buildIfNeeded; se consume una
    // sola vez (devuelve "" después).
    const char* takePendingFocus();

    // Pantalla completa estilo Blender (Ctrl+Space).
    //   isMaximized()        — true si alguna Area está maximizada.
    //   maximizedAreaId()    — id de la Area maximizada (o -1).
    //   toggleMaximize(id)   — entra/sale del modo full-viewport para
    //                          la Area indicada.  Si ya hay otra
    //                          maximizada, la sustituye.
    //   clearMaximize()      — sale del modo full-viewport (no-op si
    //                          no estaba maximizado).
    bool isMaximized()     const { return m_maximizedAreaId >= 0; }
    int  maximizedAreaId() const { return m_maximizedAreaId; }
    void toggleMaximize(int areaId) {
        m_maximizedAreaId = (m_maximizedAreaId == areaId) ? -1 : areaId;
    }
    void clearMaximize() { m_maximizedAreaId = -1; }

private:
    Workspace             m_current          = Workspace::Simulation3D;
    bool                  m_needsRebuild     = true;
    std::array<Area, 4>&  m_areas;
    PanelRegistry&        m_registry;
    const char*           m_pendingFocus     = nullptr;
    int                   m_maximizedAreaId  = -1;  // -1 = sin maximize

    // Cada uno de estos arma el layout para su workspace.
    void buildDesign       (ImGuiID dockId);
    void buildSimulation2D (ImGuiID dockId);
    void buildSimulation3D (ImGuiID dockId);

    // Helper: resuelve typeId en el registry y asigna a una Area.
    void assignArea(int areaIndex, const char* panelTypeId);
};

}  // namespace scinodes::ui
