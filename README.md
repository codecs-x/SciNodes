# SciNodes

Editor visual de nodos en tiempo real para Scilab.  El
usuario construye diagramas de bloques en un canvas tipo
Blender; el editor traduce el grafo a código Scilab y lo
ejecuta a 60 Hz, dejando una ventana entre paso y paso para
ajustar parámetros sin reiniciar la simulación.

## Requisitos

- CMake 3.25+
- Compilador con soporte C++17
- Vulkan SDK 1.3+
- SDL2 + Dear ImGui via FetchContent (automático)
- Scilab 2026+ en `PATH` o en `SCN_SCILAB_PATH`

## Compilar y correr

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/SciNodes
```

Tests:

```bash
./build/test_grammar         # headless, sin Scilab
./build/test_integration     # requiere scilab-cli en PATH
```

## Documentación

- [Manual de usuario y de desarrollador](https://codecs-x.github.io/SciNodes/)
- [CHANGELOG.md](CHANGELOG.md)

## Licencia

[MIT](LICENSE) — © 2026 Nelson Jiménez.
