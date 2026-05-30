# =============================================================================
# experiments/call_scilab/CMakeLists.txt
#
# Build aislado del smoke test de call_scilab.
#
# Este CMakeLists NO se invoca desde el CMakeLists.txt raíz: el experimento es
# independiente para no contaminar el build principal con dependencias
# opcionales.
#
# Uso típico:
#   cd experiments/call_scilab
#   mkdir -p build && cd build
#   cmake -DSCILAB_PREFIX=/opt/scilab-2026.0.1 ..
#   make
#   export SCI=/opt/scilab-2026.0.1/share/scilab
#   export LD_LIBRARY_PATH=/opt/scilab-2026.0.1/lib/scilab:$LD_LIBRARY_PATH
#   ./smoke
# =============================================================================
cmake_minimum_required(VERSION 3.16)
project(scinodes_call_scilab_smoke CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# El usuario debería pasar -DSCILAB_PREFIX=... apuntando a la raíz de Scilab.
# Si no, se intentan varias rutas estándar.
if(NOT DEFINED SCILAB_PREFIX)
    set(SCILAB_PREFIX "/opt/scilab-2026.0.1" CACHE PATH
        "Ruta raíz de la instalación de Scilab")
endif()

message(STATUS "Buscando Scilab en: ${SCILAB_PREFIX}")

find_path(SCILAB_INCLUDE_DIR
    NAMES call_scilab.h
    PATHS
        ${SCILAB_PREFIX}/include/scilab
        ${SCILAB_PREFIX}/include
        /opt/scilab/include/scilab
        /usr/include/scilab
    NO_DEFAULT_PATH
)

find_library(SCILAB_LIBRARY
    NAMES scilab
    PATHS
        ${SCILAB_PREFIX}/lib/scilab
        ${SCILAB_PREFIX}/lib
        /opt/scilab/lib/scilab
    NO_DEFAULT_PATH
)

# StartScilab / TerminateScilab / SendScilabJob viven en una biblioteca aparte
# (libscicall_scilab), no en libscilab.  Necesitamos enlazar las dos.
find_library(SCILAB_CALL_LIBRARY
    NAMES scicall_scilab
    PATHS
        ${SCILAB_PREFIX}/lib/scilab
        ${SCILAB_PREFIX}/lib
        /opt/scilab/lib/scilab
    NO_DEFAULT_PATH
)

# Diagnóstico claro si algo falta — la primera barrera del experimento.
if(NOT SCILAB_INCLUDE_DIR)
    message(FATAL_ERROR
        "No se encontró call_scilab.h. Ajusta -DSCILAB_PREFIX=<ruta> "
        "o instala los headers de Scilab.")
endif()
if(NOT SCILAB_LIBRARY)
    message(FATAL_ERROR
        "No se encontró libscilab. Ajusta -DSCILAB_PREFIX=<ruta> "
        "o instala los archivos de desarrollo de Scilab.")
endif()
if(NOT SCILAB_CALL_LIBRARY)
    message(FATAL_ERROR
        "No se encontró libscicall_scilab (StartScilab vive aquí). "
        "Verifica la instalación de Scilab.")
endif()

message(STATUS "Scilab headers      : ${SCILAB_INCLUDE_DIR}")
message(STATUS "Scilab library      : ${SCILAB_LIBRARY}")
message(STATUS "Scilab call library : ${SCILAB_CALL_LIBRARY}")

get_filename_component(SCILAB_LIBDIR "${SCILAB_LIBRARY}" DIRECTORY)

function(scinodes_add_experiment name)
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name} PRIVATE ${SCILAB_INCLUDE_DIR})
    target_link_libraries(${name} PRIVATE
        ${SCILAB_CALL_LIBRARY}
        ${SCILAB_LIBRARY}
    )
    set_target_properties(${name} PROPERTIES
        INSTALL_RPATH "${SCILAB_LIBDIR}"
        BUILD_RPATH   "${SCILAB_LIBDIR}"
    )
endfunction()

scinodes_add_experiment(smoke)
scinodes_add_experiment(motor_dc)
scinodes_add_experiment(inverse_kinematics)
scinodes_add_experiment(live_param)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/core)

# -----------------------------------------------------------------------------
# make_pid_scn — Helper que escribe /tmp/walkthrough_pid.scn con el grafo
# del walkthrough D1.  No usa Scilab: linkea contra src/core/ y nlohmann_json.
# -----------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    nlohmann_json_local
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json_local)

add_executable(make_pid_scn
    make_pid_scn.cpp
    ${CORE_DIR}/NodeType.cpp
    ${CORE_DIR}/NodeInstance.cpp
    ${CORE_DIR}/GrammarParser.cpp
    ${CORE_DIR}/NodeGraph.cpp
    ${CORE_DIR}/ScnSerializer.cpp
    ${CORE_DIR}/CustomNodeRegistry.cpp
)
target_include_directories(make_pid_scn PRIVATE
    ${CORE_DIR}/..       # para que los includes "core/..." resuelvan
)
target_link_libraries(make_pid_scn PRIVATE nlohmann_json::nlohmann_json)

# -----------------------------------------------------------------------------
# backend_motor_dc — ejercita IComputeBackend (src/core/) usando
# ScilabCallApiBackend.  Las fuentes del backend ahora viven en
# src/core/backends/; este experimento las compila directamente para no
# depender del build principal.
# -----------------------------------------------------------------------------
add_executable(backend_motor_dc
    backend_motor_dc.cpp
    ${CORE_DIR}/backends/ScilabCallApiBackend.cpp
)
target_include_directories(backend_motor_dc PRIVATE
    ${SCILAB_INCLUDE_DIR}
    ${CORE_DIR}                       # para IComputeBackend.hpp
    ${CORE_DIR}/backends              # para ScilabCallApiBackend.hpp
)
target_link_libraries(backend_motor_dc PRIVATE
    ${SCILAB_CALL_LIBRARY}
    ${SCILAB_LIBRARY}
)
set_target_properties(backend_motor_dc PROPERTIES
    INSTALL_RPATH "${SCILAB_LIBDIR}"
    BUILD_RPATH   "${SCILAB_LIBDIR}"
)
