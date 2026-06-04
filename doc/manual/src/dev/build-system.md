# Sistema de compilación

SciNodes se compila con CMake 3.25+, en C++20. El raíz del
proyecto define el editor (`SciNodes`), cuatro librerías estáticas
internas, una batería de binarios de prueba y un par de
herramientas de línea de comandos.

## Targets

| Tipo | Targets | Scilab en *runtime* |
|---|---|---|
| Editor | `SciNodes` | sí, al pulsar Run |
| Librerías internas | `imgui_lib`, `scinodes_units`, `scinodes_graph`, `scinodes_plots` | — |
| Pruebas (headless) | `test_grammar`, `test_canvas`, `test_i18n`, `test_example_library`, `test_contracts` | no |
| Pruebas (con Scilab) | `test_integration`; `test_callapi_bridge` (opt-in) | sí |
| Herramientas | `dump_catalog`, `audit_examples` | no |

Cada binario de prueba enlaza sólo las librerías que necesita
—`test_grammar` y `test_integration` contra `scinodes_graph`,
`test_i18n` y `test_example_library` contra `nlohmann_json`,
`test_contracts` contra `tinygltf` + `scinodes_graph`—, de modo que
ninguno arrastra el *stack* gráfico.

- `test_grammar` reúne **1146 aserciones** —`EXPECT_TRUE`,
  `EXPECT_FALSE`, `EXPECT_VALID`, `EXPECT_INVALID`, `EXPECT_RULE`—
  sobre la gramática (R0–R7), el `NodeGraph`, undo/redo, per-param
  pins y el álgebra de unidades. No levanta ventana ni lanza Scilab.
- `test_integration` corre **603 aserciones en 41 escenarios**
  *end-to-end* que lanzan `scilab-cli` y verifican la trayectoria
  con tolerancia.
- `test_callapi_bridge` ejercita el *facade* del bridge contra el
  *backend* `call_scilab` (Scilab in-process); sólo se compila
  cuando ese backend está configurado, porque enlaza la API nativa
  de Scilab. Ver [Backends del solver](backends.md).
- `dump_catalog` y `audit_examples` son herramientas: el primero
  vuelca el catálogo de nodos (lo consume la auditoría doc↔código);
  el segundo deserializa los `.scn` de `examples/` con R7 en su
  default actual y reporta aristas que el nuevo default rechazaría.

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

Las dependencias se declaran al inicio del `CMakeLists.txt`:

- **SDL2** `release-2.30.2` con `SDL_STATIC=ON`,
  `SDL_SHARED=OFF` y `SDL_TEST=OFF`. Esto da el target `SDL2-static`
  contra el que enlaza el editor.
- **Dear ImGui** rama `docking`. SciNodes arma una librería
  interna `imgui_lib` que combina los archivos `imgui_*.cpp` con
  los *backends* `imgui_impl_sdl2.cpp` e `imgui_impl_vulkan.cpp`.
- **nlohmann/json** `v3.11.3`, *header-only*. Se enlaza con
  `target_link_libraries(... nlohmann_json::nlohmann_json)`.
- **glslang** `15.4.0`. No se enlaza como librería C++: sólo se usa
  `glslangValidator` como herramienta de *build* para compilar los
  shaders a SPIR-V (ver abajo).
- **tinygltf** `v2.9.3`. Carga los `.gltf` de los dispositivos.

Vulkan se resuelve con `find_package(Vulkan REQUIRED)`. En Linux
suele requerir el paquete `libvulkan-dev`; en otros sistemas
operativos, el SDK oficial. Desde v0.0.8 el editor dibuja su propio
lienzo de nodos, así que **ya no se descarga `imnodes`**.

## Shaders → SPIR-V → header embebido

Cada par `*.vert`/`*.frag` bajo `src/shaders/` se compila a SPIR-V
con `glslangValidator` en tiempo de *build* (un `add_custom_command`
por etapa) y luego se envuelve en un header que define el bytecode
como un arreglo `static unsigned int`. Así el `Vulkan3DRenderer`
embebe los shaders en el binario y no depende de archivos `.spv`
externos en *runtime*.

## Librerías internas

El código del modelo se reparte en librerías estáticas con
fronteras de dependencia explícitas (`scinodes_units` ← `scinodes_graph`,
y `scinodes_plots` sobre `imgui_lib`). El detalle de qué contiene
cada una está en [Estructura del repositorio](repo-structure.md).
El editor y las herramientas linkean contra ellas en lugar de
recompilar los `.cpp` inline.

## Estructura de includes

`src/` se añade como *include directory*, así que cualquier archivo
dentro de `src/` puede incluir con paths cortos relativos al raíz
de `src/`:

```cpp
#include "core/NodeGraph.hpp"
#include "ui/NodeCanvas.hpp"
```

## Artefactos producidos

Después de `cmake --build build -j`, la carpeta `build/` contiene el
editor `build/SciNodes` junto con los binarios de prueba y las
herramientas. El editor se ejecuta con `./build/SciNodes` desde el
raíz del repo.
