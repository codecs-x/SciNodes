# Instalación

## Requisitos

- CMake 3.25+
- Compilador con C++17 (C++20 desde versiones recientes)
- Vulkan SDK 1.3+
- Scilab 2026+ en `PATH` o en `SCN_SCILAB_PATH`
- SDL2 + Dear ImGui + *imnodes* se descargan automáticamente
  vía `FetchContent` durante el *configure*.

## Construir

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/SciNodes
```

## Tests

```bash
./build/test_grammar         # headless, sin Scilab
./build/test_integration     # requiere scilab-cli en PATH
```
