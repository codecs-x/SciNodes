# SciNodes

Editor visual de nodos para diseñar, simular y observar sistemas
dinámicos en tiempo real, respaldado por Scilab como motor
numérico delegado.

El usuario construye un diagrama de bloques en un canvas de estilo
Blender (popup `Shift+A` para insertar, drag para mover, cables
para conectar). SciNodes traduce el grafo a un *script* Scilab y
lo ejecuta paso a paso vía el subproceso `scilab-cli`, dejando una
ventana de tiempo entre cada paso en la que el usuario puede
ajustar parámetros sin reiniciar la simulación.

## Requisitos

- CMake 3.25+ y un compilador con C++17.
- Vulkan SDK 1.3+ (`find_package(Vulkan REQUIRED)`).
- Scilab 2026+. El editor busca `scilab-cli` primero en
  `/opt/scilab-2026.0.1/bin/`, `/opt/scilab/bin/`, `/usr/bin/`,
  `/usr/local/bin/`, y si no lo encuentra ahí cae al `PATH`.

SDL2 (`release-2.30.2`), Dear ImGui (rama `docking`), imnodes y
nlohmann/json (`v3.11.3`) se descargan automáticamente vía
`FetchContent` la primera vez que configures CMake.

## Compilar

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/SciNodes
```

La primera compilación tarda varios minutos porque
`FetchContent` clona y compila las dependencias.

## Pruebas

```bash
./build/test_grammar       # 117 aserciones, headless, sin Scilab
./build/test_integration   # 17 aserciones en 6 escenarios, con scilab-cli real
```

## Documentación

- [Manual de usuario y de desarrollador](https://codecs-x.github.io/SciNodes/) —
  generado con [mdBook](https://rust-lang.github.io/mdBook/) desde
  `doc/manual/`.
- [`doc/db/`](doc/db) — tablas JSON que describen las entidades
  reales del código en este *tag* (catálogo de nodos, reglas de
  gramática, items de menú, atajos, módulos, dependencias,
  escenarios de prueba, formato `.scn`). Son la fuente de verdad
  contra la que se redacta la documentación.
- [CHANGELOG.md](CHANGELOG.md).

## Licencia

[MIT](LICENSE) — © 2026 Nelson Jiménez.
