cmake_minimum_required(VERSION 3.25)
project(SciNodes VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# System packages
# ---------------------------------------------------------------------------
find_package(Vulkan REQUIRED)

# ---------------------------------------------------------------------------
# Optional: call_scilab in-process backend
# Cuando se activa con -DSCINODES_WITH_CALLAPI=ON el binario incluye el
# backend ScilabCallApiBackend que embebe Scilab en el mismo proceso.  Por
# defecto OFF: el binario solo usa el subprocess scilab-cli y no necesita
# headers ni librerías de desarrollo de Scilab.
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

# imnodes
FetchContent_Declare(
    imnodes
    GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
    GIT_TAG        master
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

# imnodes: only download — we compile it ourselves to avoid its find_package(imgui)
cmake_policy(SET CMP0169 OLD)
FetchContent_Populate(imnodes)

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
# imnodes static library
# ---------------------------------------------------------------------------
add_library(imnodes_lib STATIC
    ${imnodes_SOURCE_DIR}/imnodes.cpp
)
target_include_directories(imnodes_lib PUBLIC ${imnodes_SOURCE_DIR})
target_link_libraries(imnodes_lib PUBLIC imgui_lib)

# ---------------------------------------------------------------------------
# SciNodes executable
# ---------------------------------------------------------------------------
add_executable(SciNodes
    src/main.cpp
    src/core/NodeType.cpp
    src/core/NodeInstance.cpp
    src/core/GrammarParser.cpp
    src/core/NodeGraph.cpp
    src/core/UndoRedoStack.cpp
    src/core/ScnSerializer.cpp
    src/core/ScilabBridge.cpp
    src/core/ScilabCodeGen.cpp
    src/core/Fft.cpp
    src/core/CsvExport.cpp
    src/core/CsvParamIO.cpp
    src/core/CustomNodeRegistry.cpp
    src/core/ContractRegistry.cpp
    src/core/DeviceAsset.cpp
    src/app/VulkanContext.cpp
    src/app/Vulkan3DRenderer.cpp
    src/app/AppWindow.cpp
    src/app/FileDialog.cpp
    src/ui/NodeCanvas.cpp
    src/ui/NodePalette.cpp
    src/ui/OutlinerPanel.cpp
    src/ui/PlotPanel.cpp
    src/ui/StatusBar.cpp
    src/ui/View3DPanel.cpp
)

# ---------------------------------------------------------------------------
# Unit-test executable (headless — no Vulkan / ImGui)
# ---------------------------------------------------------------------------
add_executable(test_grammar
    tests/test_grammar.cpp
    src/core/NodeType.cpp
    src/core/NodeInstance.cpp
    src/core/GrammarParser.cpp
    src/core/NodeGraph.cpp
    src/core/UndoRedoStack.cpp
    src/core/ScnSerializer.cpp
    src/core/ScilabCodeGen.cpp
    src/core/Fft.cpp
    src/core/CsvExport.cpp
    src/core/CsvParamIO.cpp
    src/core/CustomNodeRegistry.cpp
)
target_include_directories(test_grammar PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_grammar PRIVATE nlohmann_json::nlohmann_json)

# Test específico del ContractRegistry + DeviceAssetLoader.
add_executable(test_contracts
    tests/test_contracts.cpp
    src/core/ContractRegistry.cpp
    src/core/DeviceAsset.cpp
)
target_include_directories(test_contracts PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_contracts PRIVATE nlohmann_json::nlohmann_json tinygltf)

# ---------------------------------------------------------------------------
# Integration tests — spawn a real scilab-cli subprocess.
# Requires Scilab on PATH or $SCN_SCILAB_PATH.
# ---------------------------------------------------------------------------
add_executable(test_integration
    tests/test_integration.cpp
    src/core/NodeType.cpp
    src/core/NodeInstance.cpp
    src/core/GrammarParser.cpp
    src/core/NodeGraph.cpp
    src/core/UndoRedoStack.cpp
    src/core/ScnSerializer.cpp
    src/core/ScilabCodeGen.cpp
    src/core/ScilabBridge.cpp
    src/core/Fft.cpp
    src/core/CustomNodeRegistry.cpp
)
target_include_directories(test_integration PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_integration PRIVATE nlohmann_json::nlohmann_json)

target_include_directories(SciNodes PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imnodes_SOURCE_DIR}
)

target_link_libraries(SciNodes PRIVATE
    imgui_lib
    imnodes_lib
    SDL2::SDL2-static
    Vulkan::Vulkan
    nlohmann_json::nlohmann_json
    tinygltf
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
    set_target_properties(SciNodes PROPERTIES
        BUILD_RPATH "${SCILAB_LIBDIR}")

    # Test específico del bridge facade + call_scilab backend.
    add_executable(test_callapi_bridge
        tests/test_callapi_bridge.cpp
        src/core/NodeType.cpp
        src/core/NodeInstance.cpp
        src/core/GrammarParser.cpp
        src/core/NodeGraph.cpp
        src/core/UndoRedoStack.cpp
        src/core/ScnSerializer.cpp
        src/core/ScilabCodeGen.cpp
        src/core/ScilabBridge.cpp
        src/core/Fft.cpp
        src/core/CustomNodeRegistry.cpp
        src/core/backends/ScilabCallApiBackend.cpp
    )
    target_include_directories(test_callapi_bridge PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
        ${SCILAB_INCLUDE_DIR})
    target_link_libraries(test_callapi_bridge PRIVATE
        nlohmann_json::nlohmann_json
        ${SCILAB_CALL_LIBRARY}
        ${SCILAB_LIBRARY})
    set_target_properties(test_callapi_bridge PROPERTIES
        BUILD_RPATH "${SCILAB_LIBDIR}")
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

add_custom_target(shaders DEPENDS ${MOTOR_VERT_HDR} ${MOTOR_FRAG_HDR})
add_dependencies(SciNodes shaders)
target_include_directories(SciNodes PRIVATE ${SHADER_OUT})

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(SciNodes PRIVATE -Wall -Wextra -Wpedantic)
endif()
