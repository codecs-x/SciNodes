# Sistema de build

CMake 3.25+ con `FetchContent` para SDL2, Dear ImGui (rama
*docking*), *imnodes* y `nlohmann/json`.  Vulkan SDK del
sistema.  Scilab CLI a *runtime* solo (no hace falta para
compilar).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Targets:

- `SciNodes` — el binario principal.
- `test_grammar` — tests *headless*.
- `test_integration` — requieren `scilab-cli` en `PATH` para
  correr.
