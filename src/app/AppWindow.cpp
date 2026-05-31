#include "AppWindow.hpp"
#include "../core/I18n.hpp"

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

    // Arranque con ventana grande: si el display tiene un área usable
    // razonable, dimensionamos la ventana al 92 % de ella; si no,
    // dejamos los defaults conservadores.  Centrada y con flag
    // MAXIMIZED como respaldo para WMs que ignoran el tamaño inicial.
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0 &&
        usable.w > 0 && usable.h > 0) {
        m_winW = static_cast<int>(usable.w * 0.92f);
        m_winH = static_cast<int>(usable.h * 0.92f);
    }

    m_window = SDL_CreateWindow(
        "SciNodes v" SCINODES_VERSION,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        m_winW, m_winH,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
        | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_MAXIMIZED
    );
    if (!m_window)
        throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());

    // Algunos WMs ignoran SDL_WINDOW_MAXIMIZED al crear; forzamos
    // maximizado vía API post-creación.  Re-leemos el tamaño efectivo
    // para que el swapchain Vulkan se inicialice acorde.
    SDL_MaximizeWindow(m_window);
    SDL_GetWindowSize(m_window, &m_winW, &m_winH);

    m_vk.init(m_window);
    initImGui();

    // Service Locator install: defOf() (free function en core) y los
    // sitios de NodeCanvas / ScilabCodeGen consultan customNodes() para
    // resolver tipos JSON.  AppWindow es dueño del storage y lo instala
    // antes de cualquier llamada que pueda lookupear un tipo custom.
    scinodes::installCustomNodes(m_customNodes);

    // -----------------------------------------------------------------------
    // Selección de backend de cómputo.
    //
    // subproceso scilab-cli es el backend PRIMARIO — más rápido para el
    // solver real-time (ver ADR Cap. 2 sobre el trade-off medido contra
    // call_scilab).  call_scilab queda disponible bajo
    // SCINODES_BACKEND=callapi cuando el binario se compiló con
    // -DSCINODES_WITH_CALLAPI=ON, útil para usos one-shot del motor
    // Scilab; no recomendado como solver del grafo.
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

    // -----------------------------------------------------------------------
    // i18n — idioma del UI.  Default `es` (audiencia colombiana, tesis
    // en español); override vía SCINODES_LANG=en.  Si el archivo no se
    // encuentra, tr() devuelve la clave misma — sin crash, con
    // feedback visual de qué falta por traducir.
    // -----------------------------------------------------------------------
    {
        const char* envLang = std::getenv("SCINODES_LANG");
        const std::string lang = envLang ? envLang : "es";
        scinodes::I18n::instance().load(lang);
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

// Carga DejaVu Sans (o un fallback razonable) con un rango Unicode
// extendido: Latin-1 + griego completo (para π, σ, θ, ω, Σ, Δ, …) +
// flechas (→, ←, ↑, ↓, ↔) + operadores matemáticos + algunos símbolos
// puntuales (em-dash, checkmark, cross, bullet, ellipsis).  Si ningún
// archivo TTF está disponible, ImGui cae al ProggyClean por defecto
// y los caracteres no-ASCII se renderizan como '?' — situación previa.
static void loadExtendedFont(ImGuiIO& io) {
    static const char* kCandidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    };
    const char* path = nullptr;
    for (const char* p : kCandidates) {
        std::FILE* f = std::fopen(p, "rb");
        if (f) { std::fclose(f); path = p; break; }
    }
    if (!path) return;

    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    static const ImWchar kGreek[]  = { 0x0370, 0x03FF, 0 };
    static const ImWchar kMath[]   = { 0x2200, 0x22FF, 0 };
    static const ImWchar kArrows[] = { 0x2190, 0x21FF, 0 };
    static const ImWchar kPunct[]  = { 0x2010, 0x205E, 0 };  // em-dash, ellipsis, bullet, etc.
    static const ImWchar kShapes[] = { 0x25A0, 0x25FF, 0 };  // ▲ ■ ● □ ◯ …
    static const ImWchar kSymbols[] = { 0x2700, 0x27BF, 0 }; // ✓ ✗ ✱ …
    builder.AddRanges(kGreek);
    builder.AddRanges(kMath);
    builder.AddRanges(kArrows);
    builder.AddRanges(kPunct);
    builder.AddRanges(kShapes);
    builder.AddRanges(kSymbols);

    ImVector<ImWchar> ranges;
    builder.BuildRanges(&ranges);
    io.Fonts->AddFontFromFileTTF(path, 16.0f, nullptr, ranges.Data);
}

void AppWindow::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    loadExtendedFont(io);
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

    // NativeNodeRenderer es el único renderer del proyecto tras retirar
    // imnodes (\S~\ref{sec:adr-canvas-renderer}).  La inyección vía
    // INodeRenderer queda intacta para soportar tests sin GUI y futuras
    // alternativas (p. ej. una variante web sobre WebGPU).
    m_nodeRenderer = std::make_unique<scinodes::ui::NativeNodeRenderer>();
    m_nodeRenderer->init();
    m_canvas.setRenderer(*m_nodeRenderer);
    std::printf("[SciNodes] Renderer: NativeNodeRenderer\n");
    m_canvas.init();
    m_canvas.setContractRegistry(m_contractRegistry);
    m_canvas.setAssetService(m_assetService);
    m_canvas.setBridge(&m_bridge);
    m_view3D.initVulkan(m_vk);
    m_canvas.setParamCallback(
        [this](const std::vector<int>& path, int paramIdx, double value) {
            m_sim.onParamEdit(path, paramIdx, value);
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
    if (m_nodeRenderer) m_nodeRenderer->shutdown();
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
// ---------------------------------------------------------------------------
// Frame loop arquitectónico (Gregory, Cap.~8).
//
// Cuatro etapas explícitas, cada una medida por separado vía measureMs().
// El delta-time del FrameClock es informativo: la simulación corre en su
// propio hilo a paso fijo, así que el dt aquí sólo se usaría para
// animaciones de UI (ninguna por ahora).  Lo guardamos igual para que
// quede listo cuando se necesite.
// ---------------------------------------------------------------------------
void AppWindow::run() {
    while (m_running) {
        const double dt = m_frameClock.tick();

        bool inputAskedSkip = false;
        m_frameStats.inputMs = scinodes::app::measureMs([&]() {
            inputAskedSkip = processInput();
        });
        if (inputAskedSkip) continue;

        m_frameStats.updateMs  = scinodes::app::measureMs([&]() { update(dt); });
        m_frameStats.renderMs  = scinodes::app::measureMs([&]() { buildFrame(); });
        m_frameStats.presentMs = scinodes::app::measureMs([&]() { present(); });

        // Feed the frame stats back to the StatusBar; el siguiente
        // buildFrame() los pintará en la barra inferior derecha.
        m_statusBar.setFrameStats(m_frameStats);

        // Consumir un quit ya autorizado por FileActions (sea por grafo
        // limpio o porque el usuario resolvió el modal "Cambios sin
        // guardar" con Descartar o con Save+Quit).  Hacerlo al final
        // del frame garantiza que el modal alcanzó a renderizarse.
        if (m_files.shouldQuit()) m_running = false;
    }
}

// Drena eventos SDL/ImGui; reconstruye el swapchain si la ventana cambió
// de tamaño.  Retorna true cuando la reconstrucción ocurre — el caller
// debe saltar el resto del frame para no dibujar con dimensiones
// obsoletas.
bool AppWindow::processInput() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        ImGui_ImplSDL2_ProcessEvent(&e);
        // El cierre lo delegamos a FileActions: si hay cambios sin
        // guardar, dispara el modal y NO baja m_running todavía.
        // shouldQuit() se consume al final del frame (ver run loop).
        if (e.type == SDL_QUIT) m_files.requestQuit();
        if (e.type == SDL_WINDOWEVENT &&
            e.window.event == SDL_WINDOWEVENT_RESIZED)
            m_swapchainDirty = true;
    }

    if (m_swapchainDirty) {
        SDL_GetWindowSize(m_window, &m_winW, &m_winH);
        if (m_winW > 0 && m_winH > 0) {
            m_vk.rebuildSwapchain(m_winW, m_winH);
            ImGui_ImplVulkan_SetMinImageCount(m_vk.minImageCount());
            m_swapchainDirty = false;
        }
        return true;
    }
    return false;
}

// Avanza el modelo en respuesta a tiempo y eventos.  Por ahora el
// solucionador corre en su propio hilo (\\texttt{ScilabBridge::m\\_solver})
// y la única lectura de \"estado vivo\" que hace el frame loop es
// detectar si el bridge entró en Error tras un NaN/Inf, y propagar el
// id del nodo culpable al canvas para pintarlo en rojo.  \\texttt{dt}
// queda disponible para futuras animaciones de UI.
void AppWindow::update(double /*dt*/) {
    m_canvas.setHighlightedNode(m_sim.detectErrors());
    // Nota: NO se llama ensureUpToDate aquí — mutar el grafo en vivo
    // (Ctrl+G, paste, undo) no debe reiniciar el solver.  El usuario
    // controla cuándo regenerar el plan vía el botón Run/Reset del
    // StatusBar.  Mantener t corriendo es parte del flujo exploratorio.
}

// Construye la escena ImGui del frame.  Esta etapa es \"CPU only\":
// genera las DrawList y las deja en el contexto de ImGui.  El envío
// efectivo a la GPU ocurre en present().
void AppWindow::buildFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    renderUI();

    ImGui::Render();
}

// Entrega las DrawList al swapchain Vulkan.  Si beginFrame() falla
// (out-of-date swapchain), marca dirty para rebuild en la próxima
// iteración\\,---\\,no consideramos esto un error fatal, sólo un frame
// perdido.
void AppWindow::present() {
    if (!m_vk.beginFrame()) {
        m_swapchainDirty = true;
        return;
    }
    m_vk.endFrame();
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
    using scinodes::tr;
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu(tr("menu.file").c_str())) {
            if (ImGui::MenuItem(tr("menu.file.new").c_str(),    "Ctrl+N"))       m_files.requestNew();
            if (ImGui::MenuItem(tr("menu.file.open").c_str(),   "Ctrl+O"))       m_files.requestOpen();
            if (ImGui::MenuItem(tr("menu.file.import").c_str()))                 m_files.requestImport();
            if (ImGui::MenuItem(tr("menu.file.import_model_3d").c_str()))        m_files.requestImportModel3D();
            if (ImGui::MenuItem(tr("menu.file.save").c_str(),   "Ctrl+S"))       m_files.requestSave();
            if (ImGui::MenuItem(tr("menu.file.save_as").c_str(),"Ctrl+Shift+S")) m_files.requestSaveAs();
            if (ImGui::MenuItem(tr("menu.file.save_as_example").c_str()))        m_files.requestSaveAsExample();
            ImGui::Separator();
            const bool simReady =
                m_bridge.status() == ScilabBridge::Status::Ready ||
                m_bridge.status() == ScilabBridge::Status::Running;
            if (ImGui::BeginMenu(tr("menu.file.export").c_str(), simReady)) {
                if (ImGui::MenuItem(tr("menu.file.export.csv_wide").c_str()))
                    m_files.requestExportCsvWide();
                if (ImGui::MenuItem(tr("menu.file.export.csv_folder").c_str()))
                    m_files.requestExportCsvFolder();
                ImGui::Separator();
                if (ImGui::MenuItem(tr("menu.file.export.sod").c_str()))
                    m_files.requestExportSod();
                ImGui::EndMenu();
            }
            if (!simReady && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s",
                    tr("menu.file.export.tooltip_disabled").c_str());
            ImGui::Separator();
            if (ImGui::MenuItem(tr("menu.file.quit").c_str(), "Alt+F4")) {
                SDL_Event q; q.type = SDL_QUIT; SDL_PushEvent(&q);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr("menu.view").c_str())) {
            if (ImGui::MenuItem(tr("menu.view.reset_canvas").c_str()))     m_canvas.resetView();
            if (ImGui::MenuItem(tr("menu.view.reset_layout").c_str()))     m_workspaces.resetCurrentLayout();
            if (ImGui::MenuItem(tr("menu.view.reset_simulation").c_str())) m_sim.reset();
            ImGui::Separator();
            // Submenú Idioma — lista los .json disponibles en i18n/.
            // El switch en runtime recarga la tabla; ImGui re-llama tr()
            // cada frame, así que los textos cambian al siguiente render.
            if (ImGui::BeginMenu(tr("menu.view.language").c_str())) {
                const auto& cur = scinodes::I18n::instance().currentLanguage();
                for (const std::string& l :
                     scinodes::I18n::instance().availableLanguages()) {
                    const bool active = (l == cur);
                    if (ImGui::MenuItem(l.c_str(), nullptr, active))
                        scinodes::I18n::instance().load(l);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr("menu.help").c_str())) {
            if (ImGui::MenuItem(tr("menu.help.examples").c_str()))    m_examples.open();
            if (ImGui::MenuItem(tr("menu.help.about_graph").c_str())) m_aboutGraph.open(m_canvas);
            if (ImGui::MenuItem(tr("menu.help.about").c_str())) { /* TODO */ }
            ImGui::EndMenu();
        }

        // Drain + toast del .sod export — FileActions maneja el ciclo.
        m_files.drawExportToast();

        // Right-aligned hint
        const std::string& hintStr = tr("menubar.hint");
        const char* hint = hintStr.c_str();
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
    const bool stale = m_sim.isStale(m_canvas.dirtyRevision());
    SimAction action = m_statusBar.draw(
        m_canvas.nodeCount(), m_canvas.edgeCount(),
        m_canvas.grammarLabel(),
        m_sim.state(),
        grammarValid,
        m_bridge.time(),
        errMsg,
        stale,
        m_sim.realTimeFactor());
    // Interceptar Resume si el cambio fue destructivo (quitar nodos
    // o cables): el sistema topológicamente cambió de identidad, así
    // que pedir confirmación antes de descartar el estado acumulado.
    // El usuario fundamenta esta regla: una desconexión NO es "el
    // mismo sistema con menos input", es un sistema diferente.
    if (action == SimAction::Resume &&
        m_sim.wouldBeDestructiveResume(m_canvas.graph())) {
        m_pendingDestructiveResume = true;
    } else {
        m_sim.dispatch(action, m_canvas.graph(), m_canvas.dirtyRevision());
    }

    if (m_pendingDestructiveResume) {
        ImGui::OpenPopup("##destructive_resume");
        m_pendingDestructiveResume = false;  // OpenPopup persiste por sí solo
        m_destructiveResumeOpen    = true;
    }
    if (m_destructiveResumeOpen &&
        ImGui::BeginPopupModal("##destructive_resume", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize |
                               ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted(
            "El cambio quita conexiones — el sistema simulado dejó");
        ImGui::TextUnformatted(
            "de ser el mismo.  Los estados acumulados pertenecen al");
        ImGui::TextUnformatted(
            "sistema anterior y no tienen sentido en el nuevo.");
        ImGui::Spacing();
        ImGui::TextDisabled(
            "Conectar a un puerto antes libre = aditivo (preserva t).");
        ImGui::TextDisabled(
            "Quitar o sustituir nodos/cables = nuevo sistema (reinicia).");
        ImGui::Spacing();
        if (ImGui::Button("Reiniciar desde t=0", ImVec2(180, 0))) {
            m_sim.run(m_canvas.graph(), m_canvas.dirtyRevision());
            m_destructiveResumeOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(100, 0))) {
            m_destructiveResumeOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Loaded-with-violations: never advance la simulación si la
    // gramática es inválida, incluso si el solver thread sigue
    // corriendo.
    if (m_sim.isActive() && !grammarValid) {
        m_sim.stop();
    }

    // Propagar el estado de sim al canvas: éste lo usa para bloquear
    // operaciones destructivas (Delete sobre cables/nodos) y mostrar
    // el cursor "prohibido" al hover.
    m_canvas.setSimActive(m_sim.isActive());

    // Tras un refactor estructural (encapsular, desempacar), refrescar
    // la baseline del SimController.  El refactor cambia la jerarquía
    // visible pero NO las dinámicas; sin esto, Resume mostraría el
    // modal destructivo erróneamente.
    if (m_canvas.consumeRefactorFlag()) {
        m_sim.rebaselineForRefactor(m_canvas.graph());
    }

    // --- Panels: dispatch via Areas (Strategy pattern) ---------------------
    // Cada Area abre su window, dibuja el menu bar con el selector "≡",
    // delega contenido al IPanel actual.  Las Areas vacías (panel ==
    // nullptr) no abren window.
    //
    // Modo maximize (Blender Ctrl+Space): si una Area está marcada como
    // maximizada en el WorkspaceManager, sólo esa se dibuja, y cubre el
    // viewport completo.  El dock state queda intacto.
    if (m_workspaces.isMaximized()) {
        const int maxId = m_workspaces.maximizedAreaId();
        for (auto& area : m_areas) {
            if (area.id() == maxId) {
                area.draw(m_panelRegistry, /*fullViewport=*/true);
                break;
            }
        }
    } else {
        for (auto& area : m_areas) area.draw(m_panelRegistry);
    }

    // Atajo Ctrl+Space: toggle del modo maximize de la Area bajo el
    // cursor.  Si ya hay otra Area maximizada, se sale del modo (porque
    // sólo esa Area se está renderizando y por definición es la hovered).
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Space)) {
        if (m_workspaces.isMaximized()) {
            m_workspaces.clearMaximize();
        } else {
            for (auto& area : m_areas) {
                ImGuiWindow* w = ImGui::FindWindowByName(area.windowName().c_str());
                if (!w) continue;
                const ImVec2 mn = w->Pos;
                const ImVec2 mx{ w->Pos.x + w->Size.x, w->Pos.y + w->Size.y };
                if (ImGui::IsMouseHoveringRect(mn, mx, /*clip=*/false)) {
                    m_workspaces.toggleMaximize(area.id());
                    break;
                }
            }
        }
    }

    // Procesar swaps pedidos por el selector "≡" durante este frame.
    // Lo hacemos DESPUÉS de todas las draw() para evitar inconsistencias.
    for (auto& area : m_areas) {
        if (auto* newPanel = area.takePendingSwap()) {
            area.setPanel(newPanel);
            // Forzar foco para que la pestaña recién montada quede al frente.
            ImGui::SetWindowFocus(area.windowName().c_str());
        }
    }

    // --- Examples browser (Help → Examples...) ----------------------------
    // El browser corre fuera del dockspace porque es una ventana modal-like
    // que el usuario abre puntualmente.  draw() devuelve true en el frame
    // en que el usuario presionó Load.
    switch (m_examples.draw()) {
        case ExamplesBrowser::Action::None:                                         break;
        // Examples → Cargar usa openExample (no openFromPath): el grafo
        // queda marcado como template para que un Save subsecuente caiga
        // a Save As y no sobreescriba el archivo del ejemplo.
        case ExamplesBrowser::Action::Load:   m_files.openExample   (m_examples.pickedPath()); break;
        case ExamplesBrowser::Action::Import: m_files.importFromPath(m_examples.pickedPath()); break;
    }

    // Ventana "Sobre este grafo" — leer/editar la metadata root del
    // documento (id, title, description, tags) sin tocar JSON a mano.
    m_aboutGraph.draw(m_canvas);

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
