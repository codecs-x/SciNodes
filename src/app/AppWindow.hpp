#pragma once
#include "FileDialog.hpp"
#include "VulkanContext.hpp"
#include "../core/ScilabBridge.hpp"
#include "../core/ScnSerializer.hpp"
#include "../ui/NodeCanvas.hpp"
#include "../ui/OutlinerPanel.hpp"
#include "../ui/PlotPanel.hpp"
#include "../ui/StatusBar.hpp"
#include "../ui/View3DPanel.hpp"
#include <SDL2/SDL.h>
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
    void buildDockLayout(ImGuiID dockId);
    void handleEvents(bool& running);
    void renderUI();

    // File-menu helpers
    void requestNew();
    void requestOpen();
    void requestSave();
    void requestSaveAs();
    void requestExportSod();            // File → Export Simulation Data (SOD)
    void pollFileDialog();              // poll picker, dispatch to load/save
    void renderLoadReportPopup();       // modal shown when a load has issues
    void renderLoadErrorPopup();        // modal shown on fatal load error
    void doLoad(const std::string& path);
    void doSave(const std::string& path);
    void doExportSod(const std::string& path);

    enum class PendingAction { None, OpenLoad, SaveAs, ExportSod };

    // Simulation state transitions
    void simRun();      // (re)start the bridge with the current graph
    void simPause();
    void simResume();
    void simStop();
    void simReset();

    SDL_Window*  m_window  = nullptr;
    VulkanContext m_vk;
    ScilabBridge m_bridge;
    NodeCanvas   m_canvas;
    PlotPanel    m_plotPanel;
    StatusBar    m_statusBar;
    View3DPanel  m_view3D;
    OutlinerPanel m_outliner;

    FileDialog    m_fileDialog;
    PendingAction m_pendingAction = PendingAction::None;
    std::string   m_currentPath;        // last opened/saved .scn path; "" = untitled

    LoadReport m_lastReport;            // populated after a load
    bool       m_showReportPopup = false;
    bool       m_showErrorPopup  = false;

    // Transient toast for .sod export results (fades after ~5 s).
    std::string m_exportStatus;
    float       m_exportStatusTimer = 0.0f;

    SimState   m_simState = SimState::Idle;

    bool m_swapchainDirty  = false;
    bool m_layoutBuilt     = false;
    int  m_winW = 1280;
    int  m_winH = 720;
};
