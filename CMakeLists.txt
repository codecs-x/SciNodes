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

# SDL2, imgui, and nlohmann/json go through normal MakeAvailable
FetchContent_MakeAvailable(SDL2 imgui nlohmann_json)

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
    src/app/VulkanContext.cpp
    src/app/AppWindow.cpp
    src/app/FileDialog.cpp
    src/ui/NodeCanvas.cpp
    src/ui/NodePalette.cpp
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
)
target_include_directories(test_grammar PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_grammar PRIVATE nlohmann_json::nlohmann_json)

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
)

# ---------------------------------------------------------------------------
# Tooling: dump_catalog — introspecta nodeRegistry() en runtime y emite JSON.
# Sirve para verificar que doc/db/node_types.json coincida con lo que el
# binario expone realmente. Sólo depende de NodeType.cpp.
# ---------------------------------------------------------------------------
add_executable(dump_catalog
    tools/dump_catalog.cpp
    src/core/NodeType.cpp
)
target_include_directories(dump_catalog PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(SciNodes PRIVATE -Wall -Wextra -Wpedantic)
endif()
