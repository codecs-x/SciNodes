#include "AppWindow.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#ifdef SCINODES_WITH_CALLAPI
#include "core/backends/ScilabCallApiBackend.hpp"
#endif

// ---------------------------------------------------------------------------
// Blender-inspired dark theme
// ---------------------------------------------------------------------------
static void applyBlenderTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 0.0f;
    s.ChildRounding     = 4.0f;
    s.FrameRounding     = 3.0f;
    s.PopupRounding     = 6.0f;
    s.GrabRounding      = 3.0f;
    s.TabRounding       = 3.0f;
    s.ScrollbarRounding = 3.0f;
    s.WindowBorderSize  = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.FramePadding      = {6, 4};
    s.ItemSpacing       = {8, 5};
    s.ItemInnerSpacing  = {4, 4};
    s.IndentSpacing     = 18.0f;
    s.ScrollbarSize     = 12.0f;
    s.GrabMinSize       = 10.0f;
    s.WindowPadding     = {8, 8};

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                  = { 0.84f, 0.84f, 0.84f, 1.00f };
    c[ImGuiCol_TextDisabled]          = { 0.46f, 0.46f, 0.46f, 1.00f };
    c[ImGuiCol_WindowBg]              = { 0.118f,0.118f,0.118f,1.00f };
    c[ImGuiCol_ChildBg]               = { 0.145f,0.145f,0.145f,1.00f };
    c[ImGuiCol_PopupBg]               = { 0.112f,0.114f,0.122f,0.97f };
    c[ImGuiCol_Border]                = { 0.255f,0.255f,0.255f,0.70f };
    c[ImGuiCol_BorderShadow]          = { 0.00f, 0.00f, 0.00f, 0.00f };
    c[ImGuiCol_FrameBg]               = { 0.204f,0.204f,0.204f,1.00f };
    c[ImGuiCol_FrameBgHovered]        = { 0.278f,0.278f,0.278f,1.00f };
    c[ImGuiCol_FrameBgActive]         = { 0.337f,0.337f,0.337f,1.00f };
    c[ImGuiCol_TitleBg]               = { 0.082f,0.082f,0.082f,1.00f };
    c[ImGuiCol_TitleBgActive]         = { 0.176f,0.176f,0.176f,1.00f };
    c[ImGuiCol_TitleBgCollapsed]      = { 0.082f,0.082f,0.082f,0.75f };
    c[ImGuiCol_MenuBarBg]             = { 0.098f,0.098f,0.098f,1.00f };
    c[ImGuiCol_ScrollbarBg]           = { 0.098f,0.098f,0.098f,1.00f };
    c[ImGuiCol_ScrollbarGrab]         = { 0.302f,0.302f,0.302f,1.00f };
    c[ImGuiCol_ScrollbarGrabHovered]  = { 0.400f,0.400f,0.400f,1.00f };
    c[ImGuiCol_ScrollbarGrabActive]   = { 0.502f,0.502f,0.502f,1.00f };
    c[ImGuiCol_CheckMark]             = { 0.388f,0.643f,0.945f,1.00f };
    c[ImGuiCol_SliderGrab]            = { 0.388f,0.643f,0.945f,0.80f };
    c[ImGuiCol_SliderGrabActive]      = { 0.490f,0.718f,1.00f,1.00f };
    c[ImGuiCol_Button]                = { 0.216f,0.216f,0.216f,1.00f };
    c[ImGuiCol_ButtonHovered]         = { 0.290f,0.290f,0.290f,1.00f };
    c[ImGuiCol_ButtonActive]          = { 0.388f,0.643f,0.945f,1.00f };
    c[ImGuiCol_Header]                = { 0.169f,0.314f,0.545f,0.80f };
    c[ImGuiCol_HeaderHovered]         = { 0.216f,0.384f,0.647f,0.90f };
    c[ImGuiCol_HeaderActive]          = { 0.263f,0.459f,0.745f,1.00f };
    c[ImGuiCol_Separator]             = { 0.235f,0.235f,0.235f,1.00f };
    c[ImGuiCol_SeparatorHovered]      = { 0.388f,0.643f,0.945f,0.78f };
    c[ImGuiCol_SeparatorActive]       = { 0.388f,0.643f,0.945f,1.00f };
    c[ImGuiCol_ResizeGrip]            = { 0.388f,0.643f,0.945f,0.25f };
    c[ImGuiCol_ResizeGripHovered]     = { 0.388f,0.643f,0.945f,0.67f };
    c[ImGuiCol_ResizeGripActive]      = { 0.388f,0.643f,0.945f,0.95f };
    c[ImGuiCol_Tab]                   = { 0.137f,0.137f,0.137f,1.00f };
    c[ImGuiCol_TabHovered]            = { 0.290f,0.290f,0.290f,1.00f };
    c[ImGuiCol_TabActive]             = { 0.216f,0.216f,0.216f,1.00f };
    c[ImGuiCol_TabUnfocused]          = { 0.098f,0.098f,0.098f,1.00f };
    c[ImGuiCol_TabUnfocusedActive]    = { 0.157f,0.157f,0.157f,1.00f };
    c[ImGuiCol_DockingPreview]        = { 0.388f,0.643f,0.945f,0.70f };
    c[ImGuiCol_DockingEmptyBg]        = { 0.08f, 0.08f, 0.08f, 1.00f };
    c[ImGuiCol_PlotLines]             = { 0.353f,0.784f,0.392f,1.00f };
    c[ImGuiCol_PlotLinesHovered]      = { 0.490f,0.941f,0.529f,1.00f };
    c[ImGuiCol_PlotHistogram]         = { 0.388f,0.643f,0.945f,1.00f };
    c[ImGuiCol_PlotHistogramHovered]  = { 0.490f,0.718f,1.00f,1.00f };
    c[ImGuiCol_ModalWindowDimBg]      = { 0.00f, 0.00f, 0.00f, 0.55f };
}

// ---------------------------------------------------------------------------
AppWindow::AppWindow() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());

    m_window = SDL_CreateWindow(
        "SciNodes v0.1",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        m_winW, m_winH,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!m_window)
        throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());

    m_vk.init(m_window);
    initImGui();

    // -----------------------------------------------------------------------
    // Selección de backend de cómputo.
    // SCINODES_BACKEND=callapi → ScilabCallApiBackend in-process (requiere
    //                            que el binario se haya compilado con
    //                            -DSCINODES_WITH_CALLAPI=ON).
    // cualquier otro valor o no definido → subproceso scilab-cli (default).
    // -----------------------------------------------------------------------
    const char* backendEnv = std::getenv("SCINODES_BACKEND");
    const bool  wantCallApi =
        backendEnv != nullptr && std::strcmp(backendEnv, "callapi") == 0;

    if (wantCallApi) {
#ifdef SCINODES_WITH_CALLAPI
        std::fprintf(stderr,
            "[SciNodes] Backend de cómputo: call_scilab (in-process).\n");
        m_bridge.setBackend(std::make_unique<scinodes::ScilabCallApiBackend>());
#else
        std::fprintf(stderr,
            "[SciNodes] SCINODES_BACKEND=callapi pero el binario no se "
            "compiló con -DSCINODES_WITH_CALLAPI=ON.\n"
            "           Cayendo al subproceso scilab-cli.\n");
#endif
    } else {
        std::fprintf(stderr,
            "[SciNodes] Backend de cómputo: subproceso scilab-cli.\n");
    }
}

AppWindow::~AppWindow() {
    // Tear down our offscreen Vulkan renderer BEFORE the device dies,
    // so its descriptor sets / images can be destroyed cleanly.
    m_view3D.releaseVulkan();
    shutdownImGui();
    m_vk.shutdown();
    SDL_DestroyWindow(m_window);
    SDL_Quit();
}

void AppWindow::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    applyBlenderTheme();

    ImGui_ImplSDL2_InitForVulkan(m_window);

    ImGui_ImplVulkan_InitInfo vkInfo{};
    vkInfo.ApiVersion      = VK_API_VERSION_1_2;
    vkInfo.Instance        = m_vk.instance();
    vkInfo.PhysicalDevice  = m_vk.physicalDevice();
    vkInfo.Device          = m_vk.device();
    vkInfo.QueueFamily     = m_vk.graphicsFamily();
    vkInfo.Queue           = m_vk.graphicsQueue();
    vkInfo.DescriptorPool  = m_vk.descriptorPool();
    vkInfo.MinImageCount   = m_vk.minImageCount();
    vkInfo.ImageCount      = m_vk.imageCount();
    vkInfo.PipelineInfoMain.RenderPass  = m_vk.renderPass();
    vkInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&vkInfo);

    m_canvas.init();
    m_view3D.initVulkan(m_vk);
    m_canvas.setParamCallback(
        [this](int nodeId, int paramIdx, double value) {
            // Only forward while the bridge is alive; Idle/Error/Stopped skip.
            if (m_simState == SimState::Simulating ||
                m_simState == SimState::Paused)
                m_bridge.sendParameter(nodeId, paramIdx, value);
        });
}

void AppWindow::shutdownImGui() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

// ---------------------------------------------------------------------------
// Blender-style dock layout — called once on the first rendered frame.
//
//   ┌──────────────────────────┬──────────────┐
//   │                          │  3D View     │
//   │      Node Editor         ├──────────────┤
//   │                          │  Plots       │
//   └──────────────────────────┴──────────────┘
// ---------------------------------------------------------------------------
void AppWindow::buildDockLayout(ImGuiID dockId) {
    ImGuiViewport* vp = ImGui::GetMainViewport();

    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId,
        ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_NoWindowMenuButton);
    ImGui::DockBuilderSetNodeSize(dockId, vp->Size);

    ImGuiID rightId, centerId;
    ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Right, 0.28f,
                                &rightId, &centerId);

    ImGuiID rightTopId, rightBotId;
    ImGui::DockBuilderSplitNode(rightId, ImGuiDir_Down, 0.50f,
                                &rightBotId, &rightTopId);

    ImGui::DockBuilderDockWindow("Node Editor", centerId);
    ImGui::DockBuilderDockWindow("3D View",     rightTopId);
    ImGui::DockBuilderDockWindow("Plots",        rightBotId);

    ImGui::DockBuilderFinish(dockId);
    m_layoutBuilt = true;
}

// ---------------------------------------------------------------------------
void AppWindow::run() {
    bool running = true;
    while (running) {
        handleEvents(running);

        if (m_swapchainDirty) {
            SDL_GetWindowSize(m_window, &m_winW, &m_winH);
            if (m_winW > 0 && m_winH > 0) {
                m_vk.rebuildSwapchain(m_winW, m_winH);
                ImGui_ImplVulkan_SetMinImageCount(m_vk.minImageCount());
                m_swapchainDirty = false;
            }
            continue;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Scilab steps now happen on a background solver thread inside
        // ScilabBridge. Here we only check whether the thread died.
        if ((m_simState == SimState::Simulating ||
             m_simState == SimState::Paused) &&
            m_bridge.status() == ScilabBridge::Status::Error) {
            m_simState = SimState::Error;
        }

        // Forward the offending-node id so NodeCanvas can paint that
        // node's title bar red. Zero means "no divergence".
        m_canvas.setHighlightedNode(
            (m_simState == SimState::Error) ? m_bridge.offendingNodeId() : 0);

        renderUI();

        ImGui::Render();

        if (!m_vk.beginFrame()) {
            m_swapchainDirty = true;
            continue;
        }
        m_vk.endFrame();
    }
}

void AppWindow::handleEvents(bool& running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        ImGui_ImplSDL2_ProcessEvent(&e);
        if (e.type == SDL_QUIT) running = false;
        if (e.type == SDL_WINDOWEVENT &&
            e.window.event == SDL_WINDOWEVENT_RESIZED)
            m_swapchainDirty = true;
    }
}

// ---------------------------------------------------------------------------
void AppWindow::renderUI() {
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // Full-screen invisible host window for the dockspace
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoCollapse  |
        ImGuiWindowFlags_NoResize    | ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(18, 18, 18, 255));
    ImGui::Begin("##DockHost", nullptr, hostFlags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // --- Menu bar -----------------------------------------------------------
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New",      "Ctrl+N"))            requestNew();
            if (ImGui::MenuItem("Open…",    "Ctrl+O"))            requestOpen();
            if (ImGui::MenuItem("Save",     "Ctrl+S"))            requestSave();
            if (ImGui::MenuItem("Save As…", "Ctrl+Shift+S"))      requestSaveAs();
            ImGui::Separator();
            const bool simReady =
                m_bridge.status() == ScilabBridge::Status::Ready ||
                m_bridge.status() == ScilabBridge::Status::Running;
            ImGui::BeginDisabled(!simReady);
            if (ImGui::MenuItem("Export Simulation Data (SOD)…"))
                requestExportSod();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered() && !simReady)
                ImGui::SetTooltip("Run the simulation first — SOD export "
                                  "needs a live Scilab session.");
            ImGui::Separator();
            if (ImGui::MenuItem("Quit",  "Alt+F4")) {
                SDL_Event q; q.type = SDL_QUIT; SDL_PushEvent(&q);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset Canvas"))   m_canvas.resetView();
            if (ImGui::MenuItem("Reset Layout"))   m_layoutBuilt = false;
            if (ImGui::MenuItem("Reset Simulation")) simReset();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About SciNodes")) { /* TODO */ }
            ImGui::EndMenu();
        }

        // Drain any .sod export result from the bridge (queued exports
        // complete asynchronously inside the solver thread).
        if (std::string r = m_bridge.takeLastExportResult(); !r.empty()) {
            m_exportStatus = std::move(r);
            m_exportStatusTimer = 5.0f;
        }

        // Toast (left of the right-aligned hint).
        if (m_exportStatusTimer > 0.0f) {
            float dt = ImGui::GetIO().DeltaTime;
            m_exportStatusTimer -= dt;
            float alpha = std::min(1.0f, m_exportStatusTimer / 1.0f);
            bool isError = m_exportStatus.find("failed") != std::string::npos ||
                           m_exportStatus.find("refused") != std::string::npos;
            ImU32 col = isError
                ? IM_COL32(230, 110,  90, static_cast<int>(255 * alpha))
                : IM_COL32(140, 220, 140, static_cast<int>(255 * alpha));
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted("   ");
            ImGui::SameLine();
            ImGui::TextUnformatted(m_exportStatus.c_str());
            ImGui::PopStyleColor();
        }

        // Right-aligned hint
        const char* hint = "Shift+A  Add node   |   Del  Delete selected";
        float hw = ImGui::CalcTextSize(hint).x + 16.f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             ImGui::GetContentRegionAvail().x - hw);
        ImGui::TextDisabled("%s", hint);

        ImGui::EndMenuBar();
    }

    // --- Dockspace ----------------------------------------------------------
    ImGuiID dockId = ImGui::GetID("MainDock");

    if (!m_layoutBuilt)
        buildDockLayout(dockId);

    ImGui::DockSpace(dockId, {0, 0}, ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End(); // DockHost

    // --- Status bar ---------------------------------------------------------
    bool grammarValid = (std::string(m_canvas.grammarLabel()) == "Valid")
                        && !m_canvas.isReadOnly();
    const char* errMsg = (m_simState == SimState::Error)
                           ? m_bridge.lastError().c_str()
                           : nullptr;
    SimAction action = m_statusBar.draw(
        m_canvas.nodeCount(), m_canvas.edgeCount(),
        m_canvas.grammarLabel(),
        m_simState,
        grammarValid,
        m_bridge.time(),
        errMsg);

    switch (action) {
        case SimAction::Run:    simRun();    break;
        case SimAction::Pause:  simPause();  break;
        case SimAction::Resume: simResume(); break;
        case SimAction::Stop:   simStop();   break;
        case SimAction::Reset:  simReset();  break;
        case SimAction::None:                break;
    }

    // If grammar was broken while running, fall back to Idle on the next frame.
    if ((m_simState == SimState::Simulating || m_simState == SimState::Paused)
        && !grammarValid) {
        simStop();
    }

    // --- Panels -------------------------------------------------------------
    m_canvas.draw();
    m_view3D.draw(m_canvas.graph(), m_bridge);
    m_plotPanel.draw(m_canvas.graph(), m_bridge);

    // --- Persistence: keyboard shortcuts, dialog polling, modal popups -----
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            if (io.KeyShift) requestSaveAs(); else requestSave();
        }
        if (io.KeyCtrl && !io.KeyAlt && !io.KeyShift &&
            ImGui::IsKeyPressed(ImGuiKey_O, false))
            requestOpen();
        if (io.KeyCtrl && !io.KeyAlt && !io.KeyShift &&
            ImGui::IsKeyPressed(ImGuiKey_N, false))
            requestNew();
    }
    pollFileDialog();
    renderLoadReportPopup();
    renderLoadErrorPopup();
}

// ===========================================================================
// File menu — Save / Open / New
// ===========================================================================
void AppWindow::requestNew() {
    m_canvas.clear();
    simStop();
    m_currentPath.clear();
}

void AppWindow::requestOpen() {
    if (m_fileDialog.isOpen()) return;
    m_pendingAction = PendingAction::OpenLoad;
    m_fileDialog.open(FileDialog::Mode::Open,
                      "Open SciNodes Graph",
                      {"SciNodes graph (*.scn)", "*.scn"});
}

void AppWindow::requestSave() {
    if (m_currentPath.empty()) { requestSaveAs(); return; }
    doSave(m_currentPath);
}

void AppWindow::requestSaveAs() {
    if (m_fileDialog.isOpen()) return;
    m_pendingAction = PendingAction::SaveAs;
    std::string suggested = m_currentPath.empty() ? "graph.scn" : m_currentPath;
    m_fileDialog.open(FileDialog::Mode::Save,
                      "Save SciNodes Graph",
                      {"SciNodes graph (*.scn)", "*.scn"},
                      suggested);
}

void AppWindow::requestExportSod() {
    if (m_fileDialog.isOpen()) return;
    m_pendingAction = PendingAction::ExportSod;
    m_fileDialog.open(FileDialog::Mode::Save,
                      "Export Simulation Data (SOD)",
                      {"Scilab data (*.sod)", "*.sod"},
                      "simulation.sod");
}

void AppWindow::pollFileDialog() {
    if (m_fileDialog.isOpen()) return;
    std::string picked = m_fileDialog.take();
    if (picked.empty()) return;

    PendingAction act = m_pendingAction;
    m_pendingAction = PendingAction::None;

    auto appendIfMissing = [&](const char* ext) {
        auto dot = picked.rfind('.');
        auto sep = picked.find_last_of("/\\");
        if (dot == std::string::npos || (sep != std::string::npos && dot < sep))
            picked += ext;
    };

    if (act == PendingAction::OpenLoad) {
        doLoad(picked);
    } else if (act == PendingAction::SaveAs) {
        appendIfMissing(".scn");
        doSave(picked);
    } else if (act == PendingAction::ExportSod) {
        appendIfMissing(".sod");
        doExportSod(picked);
    }
}

void AppWindow::doLoad(const std::string& path) {
    LoadReport r = m_canvas.loadFromFile(path);
    m_lastReport = r;
    simStop();
    if (!r.ok) {
        m_showErrorPopup  = true;
        m_currentPath.clear();
    } else {
        m_currentPath = path;
        if (r.hasViolations()) m_showReportPopup = true;
    }
}

void AppWindow::doExportSod(const std::string& path) {
    bool accepted = m_bridge.exportSod(path);
    if (!accepted) {
        // Show whatever the bridge wrote (e.g. "path must not contain spaces"
        // or "save failed: pipe write error"), or a generic fallback.
        std::string r = m_bridge.takeLastExportResult();
        m_exportStatus = r.empty() ? "SOD export refused (no Scilab session)."
                                   : r;
        m_exportStatusTimer = 5.0f;
    }
    // Queued exports surface their result in the per-frame poll inside
    // renderUI; nothing else to do here on success.
}

void AppWindow::doSave(const std::string& path) {
    if (m_canvas.saveToFile(path))
        m_currentPath = path;
    // Failure is silent for now — the only realistic case is filesystem
    // permission, which the native dialog already gates.
}

// ===========================================================================
// Load-result popups
// ===========================================================================
void AppWindow::renderLoadReportPopup() {
    if (m_showReportPopup) {
        ImGui::OpenPopup("##LoadReport");
        m_showReportPopup = false;
    }

    ImGui::SetNextWindowSize({520, 0}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("##LoadReport", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 170, 60, 255));
        ImGui::TextUnformatted(" Loaded with grammar violations");
        ImGui::PopStyleColor();
        ImGui::Separator();

        ImGui::Text("File:  %s", m_currentPath.c_str());
        ImGui::Text("Nodes loaded:  %d", m_lastReport.nodesLoaded);
        ImGui::Text("Edges loaded:  %d", m_lastReport.edgesLoaded);
        ImGui::Text("Edges dropped: %d", (int)m_lastReport.rejectedEdges.size());

        if (!m_lastReport.unknownTypes.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Unknown node types or notes:");
            for (const auto& s : m_lastReport.unknownTypes)
                ImGui::BulletText("%s", s.c_str());
        }

        if (!m_lastReport.rejectedEdges.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Rejected edges:");
            for (const auto& e : m_lastReport.rejectedEdges)
                ImGui::BulletText("[%s] node %d → node %d   %s",
                                  e.rule.c_str(),
                                  e.fromNodeId, e.toNodeId,
                                  e.message.c_str());
        }

        ImGui::Spacing();
        ImGui::TextDisabled("The graph is open in read-only mode.\n"
                            "Use File → New to start a fresh canvas.");
        ImGui::Separator();
        if (ImGui::Button("OK", {120, 0})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ===========================================================================
// Simulation state transitions
//
// The Scilab bridge runs its own solver thread at a fixed dt. AppWindow
// just opens/pauses/closes the thread via state-machine events; the per-
// frame loop never calls step() directly.
// ===========================================================================
namespace { constexpr float kSolverDt = 1.0f / 60.0f; }

void AppWindow::simRun() {
    if (m_simState == SimState::Paused) { simResume(); return; }
    if (!m_bridge.reset(m_canvas.graph())) {
        m_simState = SimState::Error;
        return;
    }
    if (!m_bridge.startSolverThread(kSolverDt)) {
        m_simState = SimState::Error;
        return;
    }
    m_simState = SimState::Simulating;
}
void AppWindow::simPause()  {
    if (m_simState == SimState::Simulating) {
        m_bridge.setPaused(true);
        m_simState = SimState::Paused;
    }
}
void AppWindow::simResume() {
    if (m_simState == SimState::Paused) {
        m_bridge.setPaused(false);
        m_simState = SimState::Simulating;
    }
}
void AppWindow::simStop()   { m_bridge.stopSolverThread(); m_bridge.stop(); m_simState = SimState::Idle; }
void AppWindow::simReset()  { m_bridge.stopSolverThread(); m_bridge.stop(); m_simState = SimState::Idle; }

// ===========================================================================
void AppWindow::renderLoadErrorPopup() {
    if (m_showErrorPopup) {
        ImGui::OpenPopup("##LoadError");
        m_showErrorPopup = false;
    }

    ImGui::SetNextWindowSize({480, 0}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("##LoadError", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 90, 90, 255));
        ImGui::TextUnformatted(" Could not load file");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_lastReport.fatalError.c_str());
        ImGui::Separator();
        if (ImGui::Button("OK", {120, 0})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
