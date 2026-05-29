#pragma once
#include "AssetService.hpp"
#include "FileActions.hpp"
#include "FileDialog.hpp"
#include "PanelAdapters.hpp"
#include "PanelContext.hpp"
#include "PanelInterface.hpp"
#include "ShortcutHandler.hpp"
#include "SimController.hpp"
#include "VulkanContext.hpp"
#include "WorkspaceManager.hpp"
#include "../core/ContractRegistry.hpp"
#include "../core/CustomNodeRegistry.hpp"
#include "../core/ScilabBridge.hpp"
#include "../core/ScnSerializer.hpp"
#include "../ui/NodeCanvas.hpp"
#include "../ui/OutlinerPanel.hpp"
#include "../ui/PlotPanel.hpp"
#include "../ui/StatusBar.hpp"
#include "../ui/View3DPanel.hpp"
#include <SDL2/SDL.h>
#include <array>
#include <memory>
#include <string>

// -----------------------------------------------------------------------
// AppWindow — owns the SDL2 window and the main frame loop.
//
// Layout (Blender-style, set up once via DockBuilder):
//
//   ┌──────────────────────────┬──────────────┐
//   │                          │  3D View     │
//   │      Node Editor         │              │
//   │      (canvas)            ├──────────────┤
//   │                          │  Plots       │
//   └──────────────────────────┴──────────────┘
//                           ── Status bar ──
//
// Shift+A inside the canvas opens the "Add Node" popup.
// -----------------------------------------------------------------------
class AppWindow {
public:
    AppWindow();
    ~AppWindow();

    void run();

    // Carga un .scn desde la línea de comandos antes del run loop.
    // Si el archivo no existe o no parsea, se imprime a stderr y se
    // arranca con grafo vacío.
    void openGraphFromCli(const std::string& path);

private:
    void initImGui();
    void shutdownImGui();
    void handleEvents(bool& running);
    void renderUI();

    SDL_Window*  m_window  = nullptr;
    VulkanContext m_vk;
    ScilabBridge m_bridge;
    scinodes::app::SimController m_sim{ m_bridge };
    // Catálogo de contratos device.  Antes era singleton; ahora se
    // carga en initImGui() y se inyecta a NodeCanvas + PanelContext.
    scinodes::ContractRegistry      m_contractRegistry;
    // Catálogo de tipos custom (cargados de JSON en runtime).  Antes
    // era singleton; ahora AppWindow es dueño y lo "instala" via
    // installCustomNodes() para que defOf() lo encuentre desde core.
    scinodes::CustomNodeRegistry    m_customNodes;
    // Facade que encapsula contract lookup + DeviceAssetLoader + cache
    // por nodeId.  NodeCanvas le delega vía m_canvas.setAssetService().
    scinodes::app::AssetService     m_assetService{ m_contractRegistry };
    NodeCanvas   m_canvas;
    scinodes::app::FileActions      m_files{ m_canvas, m_bridge, m_sim };
    scinodes::app::ShortcutHandler  m_shortcuts{ m_files };
    // PanelContext implementa IPanelContext sobre canvas+bridge+
    // contracts; lo pasan los adapters como única dependencia
    // compartida (DIP).
    scinodes::app::PanelContext     m_panelCtx{ m_canvas, m_bridge, m_contractRegistry };
    PlotPanel    m_plotPanel;
    StatusBar    m_statusBar;
    View3DPanel  m_view3D;
    OutlinerPanel m_outliner;

    bool m_swapchainDirty  = false;
    // --- Panels (Strategy pattern: IPanel + Area + Registry) -----------
    // El registry posee los IPanel concretos.  Las Areas mantienen un
    // puntero no-owning al IPanel actual.  WorkspaceManager configura
    // qué Area arranca con qué IPanel y dónde se dockea, según el
    // workspace activo.
    scinodes::ui::PanelRegistry           m_panelRegistry;
    std::array<scinodes::ui::Area, 4>     m_areas = {
        scinodes::ui::Area{ 1 },
        scinodes::ui::Area{ 2 },
        scinodes::ui::Area{ 3 },
        scinodes::ui::Area{ 4 },
    };
    scinodes::ui::WorkspaceManager        m_workspaces{ m_areas, m_panelRegistry };
    int  m_winW = 1280;
    int  m_winH = 720;
};
