# Sistema de compilación

SciNodes se compila con CMake 3.25+, en C++17. El raíz del
proyecto define tres *targets*: el editor (`SciNodes`) y dos
binarios de prueba (`test_grammar` y `test_integration`).

## Targets

| Target            | Origen                       | Scilab en *runtime* |
|-------------------|-------------------------------|---------------------|
| `SciNodes`        | `src/`                        | sí, al pulsar Run   |
| `test_grammar`    | `tests/test_grammar.cpp`      | no                  |
| `test_integration`| `tests/test_integration.cpp`  | sí                  |

`test_grammar` reúne 186 aserciones —`EXPECT_TRUE`,
`EXPECT_FALSE`, `EXPECT_VALID`, `EXPECT_INVALID`, `EXPECT_RULE`—
sobre la gramática y el `NodeGraph`. `test_integration` corre 17
aserciones en seis escenarios *end-to-end* que lanzan
`scilab-cli` y verifican la trayectoria con tolerancia.

## Configurar y compilar

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

`CMAKE_BUILD_TYPE=Debug` también funciona y habilita símbolos +
chequeos. La primera configuración tarda varios minutos porque
`FetchContent` clona las dependencias; las recompilaciones
incrementales son rápidas.

## FetchContent

Las cuatro dependencias se declaran al inicio del
`CMakeLists.txt`:

- **SDL2** `release-2.30.2` con `SDL_STATIC=ON`,
  `SDL_SHARED=OFF` y `SDL_TEST=OFF`. Esto da el target `SDL2-static`
  contra el que enlaza el editor.
- **Dear ImGui** rama `docking`. SciNodes arma una librería
  interna `imgui_lib` que combina los archivos `imgui_*.cpp` con
  los *backends* `imgui_impl_sdl2.cpp` e `imgui_impl_vulkan.cpp`.
- **imnodes** rama `master`. Se hace `FetchContent_Populate` —sin
  configurarlo— porque imnodes intenta hacer `find_package(imgui)`
  que no encaja con nuestra librería interna. Después se compila
  manualmente como `imnodes_lib` enlazando contra `imgui_lib`.
- **nlohmann/json** `v3.11.3`, *header-only*. Se enlaza con
  `target_link_libraries(... nlohmann_json::nlohmann_json)`.

Vulkan se resuelve con `find_package(Vulkan REQUIRED)`. En Linux
suele requerir el paquete `libvulkan-dev`; en otros sistemas
operativos, el SDK oficial.

## Estructura de includes

`src/` se añade como *include directory* del editor, así que
cualquier archivo dentro de `src/` puede incluir con paths cortos
relativos al raíz de `src/`:

```cpp
#include "core/NodeGraph.hpp"
#include "ui/NodeCanvas.hpp"
```

## Artefactos producidos

Después de `cmake --build build -j`, la carpeta `build/` contiene
los tres binarios listos: `build/SciNodes` (~4 MB), `build/test_grammar`
(~360 kB) y `build/test_integration` (~340 kB). El editor se
ejecuta con `./build/SciNodes` desde el raíz del repo.
