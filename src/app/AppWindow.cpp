#include "AppWindow.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "../core/ContractRegistry.hpp"

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

    // Service Locator install: defOf() (free function en core) y los
    // sitios de NodeCanvas / ScilabCodeGen consultan customNodes() para
    // resolver tipos JSON.  AppWindow es dueño del storage y lo instala
    // antes de cualquier llamada que pueda lookupear un tipo custom.
    scinodes::installCustomNodes(m_customNodes);

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

    // -----------------------------------------------------------------------
    // Contratos de dispositivos físicos: cargados desde la carpeta
    // `contracts/` relativa al cwd.  Si no existe (instalación incompleta,
    // ejecutado desde otra ruta), el panel del nodo Device muestra
    // "(sin contrato registrado)" en vez de fallar; el resto del binario
    // sigue funcionando.
    // -----------------------------------------------------------------------
    {
        // Probar dos rutas comunes: cwd directo y subiendo un nivel (para
        // ejecuciones desde build/).  Sólo reportamos errores cuando la
        // segunda ruta también falla.
        std::string err1, err2;
        int loaded = m_contractRegistry.loadFromDirectory("contracts", &err1);
        if (loaded == 0) {
            loaded = m_contractRegistry.loadFromDirectory("../contracts", &err2);
        }
        std::fprintf(stderr,
            "[SciNodes] Contratos cargados: %d\n", loaded);
        if (loaded == 0 && !err2.empty())
            std::fprintf(stderr,
                "[SciNodes] (sin carpeta contracts/ accesible: %s)\n",
                err2.c_str());
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
    // El registry global apunta a un miembro que está a punto de morir.
    scinodes::uninstallCustomNodes();
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
    m_canvas.setContractRegistry(m_contractRegistry);
    m_canvas.setAssetService(m_assetService);
    m_view3D.initVulkan(m_vk);
    m_canvas.setParamCallback(
        [this](int nodeId, int paramIdx, double value) {
            m_sim.onParamEdit(nodeId, paramIdx, value);
        });

    // Registramos los 4 paneles concretos en el registry.  Cada uno
    // queda disponible para que cualquier Area lo hospede.  El owner
    // de las instancias es m_panelRegistry; aquí solo entregamos
    // referencias a los panel singletons.
    using namespace scinodes::ui;
    m_panelRegistry.add(std::make_unique<NodeEditorPanelAdapter>(m_canvas));
    m_panelRegistry.add(std::make_unique<View3DPanelAdapter>(m_view3D, m_panelCtx));
    m_panelRegistry.add(std::make_unique<PlotsPanelAdapter>(m_plotPanel, m_panelCtx));
    m_panelRegistry.add(std::make_unique<OutlinerPanelAdapter>(m_outliner, m_panelCtx));
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
// buildDockLayout / drawWorkspaceTabs movidos a WorkspaceManager.
// AppWindow ahora solo delega via m_workspaces.

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
        // SimController detecta divergencias del bridge y promueve a
        // Error.  Devuelve el id del nodo culpable para que el canvas
        // lo pinte en rojo (0 = sin error).
        m_canvas.setHighlightedNode(m_sim.detectErrors());

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
            if (ImGui::MenuItem("New",      "Ctrl+N"))            m_files.requestNew();
            if (ImGui::MenuItem("Open…",    "Ctrl+O"))            m_files.requestOpen();
            if (ImGui::MenuItem("Save",     "Ctrl+S"))            m_files.requestSave();
            if (ImGui::MenuItem("Save As…", "Ctrl+Shift+S"))      m_files.requestSaveAs();
            ImGui::Separator();
            const bool simReady =
                m_bridge.status() == ScilabBridge::Status::Ready ||
                m_bridge.status() == ScilabBridge::Status::Running;
            ImGui::BeginDisabled(!simReady);
            if (ImGui::MenuItem("Export Simulation Data (SOD)…"))
                m_files.requestExportSod();
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
            if (ImGui::MenuItem("Reset Layout"))   m_workspaces.resetCurrentLayout();
            if (ImGui::MenuItem("Reset Simulation")) m_sim.reset();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About SciNodes")) { /* TODO */ }
            ImGui::EndMenu();
        }

        // Drain + toast del .sod export — FileActions maneja el ciclo.
        m_files.drawExportToast();

        // Right-aligned hint
        const char* hint = "Shift+A  Add node   |   Del  Delete selected";
        float hw = ImGui::CalcTextSize(hint).x + 16.f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             ImGui::GetContentRegionAvail().x - hw);
        ImGui::TextDisabled("%s", hint);

        ImGui::EndMenuBar();
    }

    // --- Workspace tabs (Blender-style) ------------------------------------
    // Una barra fina debajo del menú con tres botones que conmutan el
    // layout dock de la aplicación.  Cambiar de pestaña reconstruye el
    // dock layout para esa workspace; el contenido (estado del grafo,
    // simulación, plots) persiste.
    m_workspaces.drawTabs();
    ImGui::Separator();

    // --- Dockspace ----------------------------------------------------------
    ImGuiID dockId = ImGui::GetID("MainDock");
    m_workspaces.buildIfNeeded(dockId);

    ImGui::DockSpace(dockId, {0, 0}, ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End(); // DockHost

    // --- Status bar ---------------------------------------------------------
    bool grammarValid = (std::string(m_canvas.grammarLabel()) == "Valid")
                        && !m_canvas.isReadOnly();
    const char* errMsg = (m_sim.state() == SimState::Error)
                           ? m_bridge.lastError().c_str()
                           : nullptr;
    SimAction action = m_statusBar.draw(
        m_canvas.nodeCount(), m_canvas.edgeCount(),
        m_canvas.grammarLabel(),
        m_sim.state(),
        grammarValid,
        m_bridge.time(),
        errMsg);
    m_sim.dispatch(action, m_canvas.graph());

    // Loaded-with-violations: never advance la simulación si la
    // gramática es inválida, incluso si el solver thread sigue
    // corriendo.
    if (m_sim.isActive() && !grammarValid) {
        m_sim.stop();
    }

    // --- Panels: dispatch via Areas (Strategy pattern) ---------------------
    // Cada Area abre su window, dibuja el menu bar con el selector "≡",
    // delega contenido al IPanel actual.  Las Areas vacías (panel ==
    // nullptr) no abren window.
    for (auto& area : m_areas) area.draw(m_panelRegistry);

    // Procesar swaps pedidos por el selector "≡" durante este frame.
    // Lo hacemos DESPUÉS de todas las draw() para evitar inconsistencias.
    for (auto& area : m_areas) {
        if (auto* newPanel = area.takePendingSwap()) {
            area.setPanel(newPanel);
            // Forzar foco para que la pestaña recién montada quede al frente.
            ImGui::SetWindowFocus(area.windowName().c_str());
        }
    }

    // --- Persistence: keyboard shortcuts, dialog polling, modal popups -----
    m_shortcuts.poll();
    m_files.update();

    // Tras cambiar de workspace, forzamos foco al panel "primario" del
    // nuevo layout — el WorkspaceManager nos dice cuál es.
    if (const char* w = m_workspaces.takePendingFocus()) {
        ImGui::SetWindowFocus(w);
    }
}

// File menu / popups / .sod export — movidos a FileActions.
// SimController hospeda la máquina de estados de simulación.

void AppWindow::openGraphFromCli(const std::string& path) {
    m_files.openFromCli(path);
}
