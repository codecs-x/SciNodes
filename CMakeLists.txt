cmake_minimum_required(VERSION 3.25)
project(SciNodes VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# System packages
# ---------------------------------------------------------------------------
find_package(Vulkan REQUIRED)

# ---------------------------------------------------------------------------
# call_scilab in-process backend (opcional — ver tesis Cap. 2 ADR)
#
# Por defecto OFF: el binario usa el bridge subprocess (scilab-cli) que
# es más rápido para el solver real-time (ver el ADR del trade-off
# medido: SendScilabJob tiene ~5ms de overhead fijo por llamada,
# inviable a 60Hz; el subprocess mantiene Scilab en un único bucle
# REPL caliente).
#
# Para construir CON call_scilab — útil para futuros usos one-shot del
# motor Scilab (bode, fft, optim invocados desde paneles dedicados) —
# pasar -DSCINODES_WITH_CALLAPI=ON.  Requiere libs de desarrollo Scilab
# en SCILAB_PREFIX, y activar en runtime con SCINODES_BACKEND=callapi.
# ---------------------------------------------------------------------------
option(SCINODES_WITH_CALLAPI "Build the in-process call_scilab backend" OFF)
set(SCILAB_PREFIX "/opt/scilab-2026.0.1" CACHE PATH
    "Raíz de la instalación de Scilab usada por SCINODES_WITH_CALLAPI")

if(SCINODES_WITH_CALLAPI)
    find_path(SCILAB_INCLUDE_DIR
        NAMES call_scilab.h
        PATHS ${SCILAB_PREFIX}/include/scilab
              /opt/scilab/include/scilab
              /usr/include/scilab
        NO_DEFAULT_PATH)
    find_library(SCILAB_LIBRARY
        NAMES scilab
        PATHS ${SCILAB_PREFIX}/lib/scilab
              ${SCILAB_PREFIX}/lib
              /opt/scilab/lib/scilab
        NO_DEFAULT_PATH)
    find_library(SCILAB_CALL_LIBRARY
        NAMES scicall_scilab
        PATHS ${SCILAB_PREFIX}/lib/scilab
              ${SCILAB_PREFIX}/lib
              /opt/scilab/lib/scilab
        NO_DEFAULT_PATH)
    if(NOT SCILAB_INCLUDE_DIR OR NOT SCILAB_LIBRARY OR NOT SCILAB_CALL_LIBRARY)
        message(FATAL_ERROR
            "SCINODES_WITH_CALLAPI=ON pero no se encontró Scilab. "
            "Ajusta -DSCILAB_PREFIX=<ruta> o vuelve a OFF.")
    endif()
    get_filename_component(SCILAB_LIBDIR "${SCILAB_LIBRARY}" DIRECTORY)
    message(STATUS "SciNodes con call_scilab — headers: ${SCILAB_INCLUDE_DIR}")
    message(STATUS "SciNodes con call_scilab — libs   : ${SCILAB_LIBDIR}")
endif()

# ---------------------------------------------------------------------------
# FetchContent dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

# SDL2
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.2
    GIT_SHALLOW    TRUE
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)

# Dear ImGui (docking branch)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
)

# nlohmann/json — header-only JSON library used by ScnSerializer (.scn format)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")

# glslang — used at configure time to turn shader sources into SPIR-V.
# We only need glslangValidator as a build tool; we don't link the C++
# library into SciNodes. The "ENABLE_*" caches keep the build slim.
FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG        15.4.0
    GIT_SHALLOW    TRUE
)
set(ENABLE_GLSLANG_BINARIES  ON  CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_INSTALL   OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER       OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL              OFF CACHE BOOL "" FORCE)
set(ENABLE_CTEST             OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT               OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING            OFF CACHE BOOL "" FORCE)

# tinygltf — parser glTF 2.0 header-only.  Lo usamos para cargar los
# assets 3D con sus extras.scinodes (parts/joints/anchors).  Mantenerlo
# header-only evita una librería extra: el .cpp que define
# TINYGLTF_IMPLEMENTATION es DeviceAsset.cpp.
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        v2.9.3
    GIT_SHALLOW    TRUE
)
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_GL_EXAMPLES    OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_VALIDATOR_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_BUILDER_EXAMPLE   OFF CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL              OFF CACHE BOOL "" FORCE)
set(TINYGLTF_HEADER_ONLY          ON  CACHE BOOL "" FORCE)

# SDL2, imgui, nlohmann/json, glslang, tinygltf go through normal MakeAvailable
FetchContent_MakeAvailable(SDL2 imgui nlohmann_json glslang tinygltf)

# ---------------------------------------------------------------------------
# ImGui static library (SDL2+Vulkan backend)
# ---------------------------------------------------------------------------
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC SDL2::SDL2-static Vulkan::Vulkan)

# ---------------------------------------------------------------------------
# scinodes_units — librería interna de análisis dimensional (etapa 6K.A).
#
# Capa pura: parser gramatical de unidades textuales, álgebra SI, struct
# Quantity (valor + unit), display con búsqueda algebraica de compound.
# CERO dependencias del resto del proyecto — sólo stdlib.  Pensada para
# eventual extracción a repo independiente cuando estabilice; por ahora
# vive en este árbol como `add_library(scinodes_units STATIC ...)`.
#
# Su API pública es `Unit.hpp` + `Quantity.hpp`.  Cualquier target que
# antes listaba Unit.cpp + Quantity.cpp inline ahora linkea contra ella.
# ---------------------------------------------------------------------------
add_library(scinodes_units STATIC
    src/core/Unit.cpp
    src/core/Quantity.cpp
)
target_include_directories(scinodes_units PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(scinodes_units PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# scinodes_graph — librería interna del DAG tipado con gramática
# (etapa 6K.B).
#
# Contiene la maquinaria entera del grafo de nodos:
#   - NodeType (registry de NodeDef con units / types declarados)
#   - NodeInstance (instancia + params + fields + overrides)
#   - NodeKind (sum-type variant + dispatch polimórfico)
#   - NodeGraph (DAG + tryAddEdge con R1..R7)
#   - GrammarParser (validación de aristas)
#   - Field (synthesis FieldDef desde NodeDef)
#   - DimensionalAnalyzer (propagación + R7 enforcement)
#   - UndoRedoStack (snapshot stack)
#   - CustomNodeRegistry (descriptores JSON-loaded de nodos custom)
#
# Pensada como capa reutilizable: cualquier editor de nodos
# (visual programming, shader graph, dataflow tool) podría usar esta
# librería sin tocar el código.  Su API pública son los headers de los
# 9 módulos arriba.  Depende sólo de `scinodes_units` y nlohmann_json.
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# scinodes_plots — librería interna de widgets de plot ImGui (etapa 6K.D).
#
# Cinco renderers en `src/ui/plots/`: WaveRenderer (oscilloscope multi-canal
# con autoscale + pan + zoom), PhaseRenderer (x-vs-y), HeatmapRenderer
# (x/y/c en grilla), HistogramRenderer (distribución binned), SpectrumRenderer
# (magnitud FFT).
#
# Pensada como capa reutilizable: cualquier tool basado en ImGui que necesite
# plots tipo osciloscopio puede consumirla.  Sus dependencias mínimas son
# imgui_lib (para los primitivos de dibujo + estado de input), I18n (un único
# string "plots.no_data_yet") y los headers de `Fft` (sólo el .hpp; la
# implementación la provee el target final via Fft.cpp).
#
# Antes (pre-6K.D) los renderers acoplaban contra `ScilabBridge.hpp` por una
# sola constante y contra `PlotPanel.hpp` por el struct ZoomState — esos
# dos puentes se rompieron al introducir `PlotDefaults.hpp` y `ZoomState.hpp`
# dentro de la misma carpeta del lib.
# ---------------------------------------------------------------------------
add_library(scinodes_plots STATIC
    src/ui/plots/WaveRenderer.cpp
    src/ui/plots/PhaseRenderer.cpp
    src/ui/plots/HeatmapRenderer.cpp
    src/ui/plots/HistogramRenderer.cpp
    src/ui/plots/SpectrumRenderer.cpp
)
target_include_directories(scinodes_plots PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(scinodes_plots PUBLIC cxx_std_20)
target_link_libraries(scinodes_plots PUBLIC imgui_lib)

# Notas para futuro spin-off: scinodes_plots referencia símbolos de
# `scinodes::tr` (I18n.cpp) y `scinodes::ui::plots::computeMagnitudeSpectrum`
# (Fft.cpp) que NO viven dentro de esta lib.  El target final que linkee
# scinodes_plots debe proveer ambos .cpp (hoy SciNodes los compila como
# parte del ejecutable, lo que satisface los undefined references).

add_library(scinodes_graph STATIC
    src/core/NodeType.cpp
    src/core/NodeInstance.cpp
    src/core/NodeKind.cpp
    src/core/NodeGraph.cpp
    src/core/GrammarParser.cpp
    src/core/Field.cpp
    src/core/DimensionalAnalyzer.cpp
    src/core/UndoRedoStack.cpp
    src/core/CustomNodeRegistry.cpp
)
target_include_directories(scinodes_graph PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(scinodes_graph PUBLIC cxx_std_20)
target_link_libraries(scinodes_graph PUBLIC
    scinodes_units
    nlohmann_json::nlohmann_json
)

# ---------------------------------------------------------------------------
# SciNodes executable
# ---------------------------------------------------------------------------
add_executable(SciNodes
    src/main.cpp
    src/core/SceneCollector.cpp
    src/core/ScnSerializer.cpp
    src/core/ScilabBridge.cpp
    src/core/ScilabCodeGen.cpp
    src/core/Fft.cpp
    src/core/CsvExport.cpp
    src/core/I18n.cpp
    src/core/CsvParamIO.cpp
    src/core/ContractRegistry.cpp
    src/core/DeviceAsset.cpp
    src/core/AssetMapping.cpp
    src/core/LinearExampleLibrary.cpp
    src/app/VulkanContext.cpp
    src/app/Vulkan3DRenderer.cpp
    src/app/AppWindow.cpp
    src/app/AssetService.cpp
    src/app/FileActions.cpp
    src/app/FrameClock.cpp
    src/app/PanelContext.cpp
    src/app/PanelInterface.cpp
    src/app/ShortcutHandler.cpp
    src/app/SimController.cpp
    src/app/WorkspaceManager.cpp
    src/app/FileDialog.cpp
    src/ui/AboutGraphPanel.cpp
    src/ui/AssetMappingPanel.cpp
    src/ui/ExamplesBrowser.cpp
    src/ui/NodeCanvas.cpp
    src/ui/NodeCanvasRender.cpp
    src/ui/NodeCanvasPanels.cpp
    src/ui/NodeCanvasPopups.cpp
    src/ui/canvas/Canvas.cpp
    src/ui/canvas/NativeNodeRenderer.cpp
    src/ui/canvas/NativeNodeRendererNode.cpp
    src/ui/canvas/NativeNodeRendererLink.cpp
    src/ui/canvas/NativeNodeRendererInteraction.cpp
    src/ui/NodePalette.cpp
    src/ui/OutlinerPanel.cpp
    src/ui/PlotPanel.cpp
    src/ui/StatusBar.cpp
    src/ui/View3DPanel.cpp
    src/ui/View3DPanelMesh.cpp
    src/ui/View3DPanelAsset.cpp
)

# ---------------------------------------------------------------------------
# Unit-test executable (headless — no Vulkan / ImGui)
# ---------------------------------------------------------------------------
add_executable(test_grammar
    tests/test_grammar.cpp
    src/core/SceneCollector.cpp
    src/core/ScnSerializer.cpp
    src/core/ScilabCodeGen.cpp
    src/core/Fft.cpp
    src/core/CsvExport.cpp
    src/core/CsvParamIO.cpp
)
target_include_directories(test_grammar PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_grammar PRIVATE scinodes_graph)

# Test headless de Canvas::autoLayout (refactor own-abstractions).
add_executable(test_canvas
    tests/test_canvas.cpp
    src/core/I18n.cpp
    src/ui/canvas/Canvas.cpp
)
target_include_directories(test_canvas PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_canvas PRIVATE scinodes_graph)

# Test de coherencia de i18n — verifica que en.json y es.json comparten
# exactamente las mismas claves (sin orphans entre idiomas) y que la
# clase I18n hace lookup correcto.
add_executable(test_i18n
    tests/test_i18n.cpp
    src/core/I18n.cpp
)
target_include_directories(test_i18n PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_i18n PRIVATE nlohmann_json::nlohmann_json)

# Test del backend lineal de IExampleLibrary — discover/search/snippet.
add_executable(test_example_library
    tests/test_example_library.cpp
    src/core/LinearExampleLibrary.cpp
)
target_include_directories(test_example_library PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_example_library PRIVATE nlohmann_json::nlohmann_json)

# Test específico del ContractRegistry + DeviceAssetLoader.
add_executable(test_contracts
    tests/test_contracts.cpp
    src/core/ContractRegistry.cpp
    src/core/DeviceAsset.cpp
    src/core/AssetMapping.cpp
)
target_include_directories(test_contracts PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_contracts PRIVATE tinygltf scinodes_graph)

# ---------------------------------------------------------------------------
# Integration tests — spawn a real scilab-cli subprocess.
# Requires Scilab on PATH or $SCN_SCILAB_PATH.
# ---------------------------------------------------------------------------
add_executable(test_integration
    tests/test_integration.cpp
    src/core/ScnSerializer.cpp
    src/core/ScilabCodeGen.cpp
    src/core/ScilabBridge.cpp
    src/core/Fft.cpp
)
target_include_directories(test_integration PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_integration PRIVATE scinodes_graph)

target_include_directories(SciNodes PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# Versión accesible desde C++ como SCINODES_VERSION (string literal).
# Fuente única: PROJECT_VERSION declarado en project() arriba.  Pasar
# como define evita hardcodear "v0.1.0" en strings de UI; bumpear la
# versión en un solo lugar (project()) actualiza título y splash.
target_compile_definitions(SciNodes PRIVATE
    SCINODES_VERSION="${PROJECT_VERSION}")

target_link_libraries(SciNodes PRIVATE
    imgui_lib
    SDL2::SDL2-static
    Vulkan::Vulkan
    tinygltf
    scinodes_graph
    scinodes_plots
)

# ---------------------------------------------------------------------------
# Optional call_scilab backend wired into SciNodes
# ---------------------------------------------------------------------------
if(SCINODES_WITH_CALLAPI)
    target_sources(SciNodes PRIVATE src/core/backends/ScilabCallApiBackend.cpp)
    target_include_directories(SciNodes PRIVATE
        ${SCILAB_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/src/core)     # para que el backend encuentre IComputeBackend.hpp
    target_link_libraries(SciNodes PRIVATE
        ${SCILAB_CALL_LIBRARY}
        ${SCILAB_LIBRARY})
    target_compile_definitions(SciNodes PRIVATE SCINODES_WITH_CALLAPI=1)
    # RPATH cubre libscilab + JVM bundleada de Scilab (libjava/libjvm
    # están en thirdparty/java/lib y thirdparty/java/lib/server, y libscilab
    # depende de ellas).  --disable-new-dtags fuerza DT_RPATH (recursivo
    # a libs transitivas) en lugar del DT_RUNPATH moderno.
    set_target_properties(SciNodes PROPERTIES
        BUILD_RPATH "${SCILAB_LIBDIR};${SCILAB_PREFIX}/thirdparty/java/lib;${SCILAB_PREFIX}/thirdparty/java/lib/server")
    target_link_options(SciNodes PRIVATE "-Wl,--disable-new-dtags")

    # Launcher shell: el binario solo no basta porque Scilab necesita
    # SCI + TCL_LIBRARY + LD_LIBRARY_PATH al runtime para que tclsci
    # y la JVM arranquen correctamente.  El script exporta lo mismo
    # que el launcher oficial /opt/scilab-…/bin/scilab y luego exec()
    # al binario real.  El usuario corre ./build/scinodes y todo
    # funciona sin tener que saber dónde vive Scilab.
    file(WRITE ${CMAKE_BINARY_DIR}/scinodes
"#!/usr/bin/env bash
# Auto-generado por CMake.  Lanza SciNodes con el entorno que la
# instalación de Scilab requiere para que call_scilab arranque:
#   - LD_LIBRARY_PATH para libscilab y la JVM bundleada.
#   - SCI raíz de la instalación (share/scilab).
#   - TCL_LIBRARY / TK_LIBRARY para que tclsci encuentre init.tcl.
# Editar este archivo es inútil — regenerar vía cmake.
SCILAB_PREFIX=\"${SCILAB_PREFIX}\"
export SCI=\"\${SCILAB_PREFIX}/share/scilab\"
export TCL_LIBRARY=\"\${SCI}/modules/tclsci/tcl/tcl8.5\"
export TK_LIBRARY=\"\${SCI}/modules/tclsci/tcl/tk8.5\"
export LD_LIBRARY_PATH=\"\${SCILAB_PREFIX}/lib/scilab:\${SCILAB_PREFIX}/thirdparty/java/lib:\${SCILAB_PREFIX}/thirdparty/java/lib/server:\${SCILAB_PREFIX}/lib/thirdparty:\${SCILAB_PREFIX}/lib/thirdparty/redist\${LD_LIBRARY_PATH:+:\${LD_LIBRARY_PATH}}\"
exec \"$(dirname \"\$0\")/SciNodes\" \"\$@\"
")
    execute_process(COMMAND chmod +x ${CMAKE_BINARY_DIR}/scinodes)

    # Test específico del bridge facade + call_scilab backend.
    add_executable(test_callapi_bridge
        tests/test_callapi_bridge.cpp
        src/core/ScnSerializer.cpp
        src/core/ScilabCodeGen.cpp
        src/core/ScilabBridge.cpp
        src/core/Fft.cpp
        src/core/backends/ScilabCallApiBackend.cpp
    )
    target_include_directories(test_callapi_bridge PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
        ${SCILAB_INCLUDE_DIR})
    target_link_libraries(test_callapi_bridge PRIVATE
        ${SCILAB_CALL_LIBRARY}
        ${SCILAB_LIBRARY}
        scinodes_graph)
    set_target_properties(test_callapi_bridge PROPERTIES
        BUILD_RPATH "${SCILAB_LIBDIR};${SCILAB_PREFIX}/thirdparty/java/lib;${SCILAB_PREFIX}/thirdparty/java/lib/server")
    target_link_options(test_callapi_bridge PRIVATE "-Wl,--disable-new-dtags")
endif()

# ---------------------------------------------------------------------------
# Shader compilation
# ---------------------------------------------------------------------------
# Each pair of *.vert/*.frag files under src/shaders/ is compiled to SPIR-V
# at build time. The bytes are wrapped in a small C++ header so the
# renderer can embed them without depending on shipped .spv files.
set(SHADER_DIR ${CMAKE_SOURCE_DIR}/src/shaders)
set(SHADER_OUT ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT})

function(scn_compile_shader STAGE INPUT OUTPUT)
    add_custom_command(
        OUTPUT  ${OUTPUT}
        COMMAND $<TARGET_FILE:glslang-standalone>
                -V --target-env vulkan1.2
                -S ${STAGE}
                -o ${OUTPUT}
                ${INPUT}
        DEPENDS ${INPUT} glslang-standalone
        COMMENT "Compiling shader ${STAGE}: ${INPUT}"
        VERBATIM)
endfunction()

# Wrap each .spv as a header that defines a static unsigned-int array
# named after the shader stem. The header is force-included via
# target_sources/HEADER_FILE_ONLY for IDE indexing.
function(scn_embed_spv SPV_FILE HEADER_FILE SYMBOL)
    add_custom_command(
        OUTPUT  ${HEADER_FILE}
        COMMAND ${CMAKE_COMMAND} -E echo "#pragma once" > ${HEADER_FILE}
        COMMAND ${CMAKE_COMMAND} -E echo "// Auto-generated from ${SPV_FILE}" >> ${HEADER_FILE}
        COMMAND ${CMAKE_COMMAND} -DIN=${SPV_FILE} -DOUT=${HEADER_FILE}
                -DSYM=${SYMBOL}
                -P ${CMAKE_SOURCE_DIR}/cmake/EmbedSpv.cmake
        DEPENDS ${SPV_FILE}
        COMMENT "Embedding SPIR-V ${SPV_FILE} → ${HEADER_FILE}"
        VERBATIM)
endfunction()

# motor.vert → motor.vert.spv → motor_vert_spv.h
set(MOTOR_VERT_SPV ${SHADER_OUT}/motor.vert.spv)
set(MOTOR_FRAG_SPV ${SHADER_OUT}/motor.frag.spv)
set(MOTOR_VERT_HDR ${SHADER_OUT}/motor_vert_spv.h)
set(MOTOR_FRAG_HDR ${SHADER_OUT}/motor_frag_spv.h)
scn_compile_shader(vert ${SHADER_DIR}/motor.vert ${MOTOR_VERT_SPV})
scn_compile_shader(frag ${SHADER_DIR}/motor.frag ${MOTOR_FRAG_SPV})
scn_embed_spv(${MOTOR_VERT_SPV} ${MOTOR_VERT_HDR} kMotorVertSpv)
scn_embed_spv(${MOTOR_FRAG_SPV} ${MOTOR_FRAG_HDR} kMotorFragSpv)

# asset.vert / asset.frag — solid triangle pipeline (DeviceAsset)
set(ASSET_VERT_SPV ${SHADER_OUT}/asset.vert.spv)
set(ASSET_FRAG_SPV ${SHADER_OUT}/asset.frag.spv)
set(ASSET_VERT_HDR ${SHADER_OUT}/asset_vert_spv.h)
set(ASSET_FRAG_HDR ${SHADER_OUT}/asset_frag_spv.h)
scn_compile_shader(vert ${SHADER_DIR}/asset.vert ${ASSET_VERT_SPV})
scn_compile_shader(frag ${SHADER_DIR}/asset.frag ${ASSET_FRAG_SPV})
scn_embed_spv(${ASSET_VERT_SPV} ${ASSET_VERT_HDR} kAssetVertSpv)
scn_embed_spv(${ASSET_FRAG_SPV} ${ASSET_FRAG_HDR} kAssetFragSpv)

add_custom_target(shaders DEPENDS
    ${MOTOR_VERT_HDR} ${MOTOR_FRAG_HDR}
    ${ASSET_VERT_HDR} ${ASSET_FRAG_HDR})
add_dependencies(SciNodes shaders)
target_include_directories(SciNodes PRIVATE ${SHADER_OUT})

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(SciNodes PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Tooling: dump_catalog — introspecta nodeRegistry() en runtime y emite JSON.
# ---------------------------------------------------------------------------
add_executable(dump_catalog
    tools/dump_catalog.cpp
    src/core/NodeType.cpp
)
target_include_directories(dump_catalog PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dump_catalog PRIVATE scinodes_units scinodes_graph)
