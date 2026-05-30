# Instalación

## Requisitos del sistema

SciNodes se compila con CMake 3.25 o superior y un compilador con
soporte para C++17. Necesitas también el SDK de Vulkan 1.3+
instalado en el sistema (la búsqueda se hace con
`find_package(Vulkan REQUIRED)`); en Linux esto suele venir del
paquete `libvulkan-dev` o equivalente.

Las demás dependencias se descargan automáticamente vía
`FetchContent` la primera vez que configures CMake:

- **SDL2** `release-2.30.2`, para la ventana y el manejo de
  eventos. Se compila estática (`SDL_STATIC=ON`, `SDL_SHARED=OFF`).
- **Dear ImGui** rama `docking`, para la UI inmediata. SciNodes
  arma su propia librería interna que combina los archivos
  `imgui_*.cpp` con los *backends* `imgui_impl_sdl2.cpp` e
  `imgui_impl_vulkan.cpp`.
- **imnodes**, rama `master`, para el canvas de nodos. Se descarga
  sin configurarlo y se envuelve en un target estático
  `imnodes_lib` que enlaza contra el ImGui interno.
- **nlohmann/json** `v3.11.3`, *header-only*, para la
  serialización del formato `.scn`.

Para correr el editor necesitas además **Scilab 2026** o más
nuevo, con su binario `scilab-cli` accesible. SciNodes lo busca
primero en rutas comunes (`/opt/scilab-2026.0.1/bin/`,
`/opt/scilab/bin/`, `/usr/bin/`, `/usr/local/bin/`) y, si no lo
encuentra ahí, cae al `PATH`.

## Compilar

```bash
git clone https://github.com/codecs-x/SciNodes.git
cd SciNodes
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

La primera configuración tarda varios minutos porque
`FetchContent` clona SDL2, ImGui, imnodes y nlohmann/json desde su
repositorio fuente. Las compilaciones incrementales son muchísimo
más rápidas.

El binario del editor queda en `build/SciNodes`. Junto a él se
construyen dos binarios de prueba:

- `build/test_grammar`, que cubre 1112 aserciones sobre la
  gramática (R0–R7), la alcanzabilidad, el ciclo undo/redo, los
  vectores `vec(3)`, el análisis dimensional y el catálogo de
  geometría. Corre en milisegundos y no requiere Scilab.
- `build/test_integration`, que lanza `scilab-cli` real y verifica
  comportamiento de 41 escenarios *end-to-end* (603 aserciones
  totales).

## Verificar la instalación

```bash
./build/test_grammar       # 1112/1112, sin Scilab
./build/test_integration   # 603/603, requiere scilab-cli
```

Si `test_integration` falla con un mensaje sobre `scilab-cli`,
revisa que esté en una de las rutas que SciNodes busca o
explícitamente en el `PATH`:

```bash
which scilab-cli
scilab-cli -nb -nwni -e "disp('hola')"
```

## Levantar el editor

```bash
./build/SciNodes
```

La ventana abre un canvas vacío. Pulsar **Shift + A** sobre el
canvas muestra el popup para insertar tu primer nodo.
