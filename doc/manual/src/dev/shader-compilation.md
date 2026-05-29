# Compilación y embebido de shaders

Los shaders `motor.vert` / `motor.frag` se compilan a SPIR-V
en tiempo de *configure* con `glslangValidator` y se
embeben en el binario como cabeceras C++ generadas
automáticamente.

## Cadena

```
src/shaders/motor.vert ──► motor.vert.spv ──► motor_vert_spv.h ──► binario
src/shaders/motor.frag ──► motor.frag.spv ──► motor_frag_spv.h ──► binario
```

Tres pasos orquestados por CMake:

1. **`scn_compile_shader`** invoca `glslangValidator -V
   --target-env vulkan1.2 -S <stage>` y produce el `.spv`.
2. **`scn_embed_spv`** corre `cmake/EmbedSpv.cmake` que lee
   el `.spv` y escribe un `static const uint32_t SYMBOL[] =
   {...};` en una cabecera.
3. **`add_dependencies(SciNodes shaders)`** encadena la
   *target* `shaders` con los headers generados.

## Por qué `FetchContent` para `glslang`

Para no depender del Vulkan SDK del sistema durante el
*build*.  Versión fijada a `15.4.0`.  CI sin SDK funciona
porque solo `libvulkan-dev` hace falta.
