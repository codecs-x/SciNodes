# Capa gráfica: Vulkan + ImGui

SciNodes dibuja con **Vulkan** como API de bajo nivel y **Dear ImGui**
como capa de interfaz. ImGui produce la UI (paneles, menús, el canvas de
nodos vía `ImDrawList`) y un motor procedural propio dibuja el visor 3-D;
ambos terminan en el mismo *command buffer* de Vulkan por frame.

## El render pass

Un único *render pass* con dos *attachments* —color y *depth*— organiza
el frame. El motor procedural y los *assets* glTF dibujan al *attachment*
de color (con *depth* real para los *assets*), y luego el color pasa a
`SHADER_READ` para que el subpaso de ImGui lo componga junto al resto de
la interfaz antes del *present*.

![Render pass de Vulkan: attachments de color y depth, subpaso offscreen, transición del color a SHADER_READ y subpaso de ImGui hacia el framebuffer y el present.](../diagrams/vulkan_renderpass.svg)

## Los dos pipelines gráficos

Sobre ese render pass conviven **dos** *pipelines* distintos:

- **principal** — dibuja los paneles de ImGui y el motor procedural
  (líneas); usa `motor.vert` / `motor.frag`, sin *depth buffer*.
- **assets** — dibuja la geometría glTF con *depth buffer* real y
  *shading* Lambert; usa `asset.vert` / `asset.frag`, conmutable entre
  *Wire* / *Solid* / *Both*.

![Pipeline gráfico de Vulkan: los pipelines principal (ImGui + motor procedural) y assets (glTF con depth + Lambert) comparten el mismo render pass.](../diagrams/vulkan_pipeline.svg)

## Shaders: el toolchain SPIR-V

Los *shaders* se compilan **en tiempo de build**, no en runtime: el
binario nunca busca `.spv` en disco. `glslangValidator` compila cada
`src/shaders/*.vert/frag` a un `.spv` binario; `embed_spv.cmake` lo
convierte en un header C++ con un `const uint32_t[]` embebido; y `g++`
lo enlaza al binario final.

![Toolchain SPIR-V: glslangValidator compila los shaders a .spv, embed_spv.cmake los embebe como arrays C++, y g++ los enlaza al binario; todo en build time.](../diagrams/spirv_toolchain.svg)

## El puente ImGui ↔ Vulkan

ImGui no conoce Vulkan: produce una **lista de comandos de dibujo** por
frame. El backend `imgui_impl_vulkan` traduce esa lista a comandos Vulkan
dentro del *command buffer* del frame. El motor procedural del visor 3-D
(`View3DPanel`) dibuja a una **textura offscreen** que entra al *draw
list* como un `ImTextureID`, de modo que el 3-D se compone como un widget
más de la interfaz.

![Puente ImGui↔Vulkan: la draw list de ImGui pasa por imgui_impl_vulkan al command buffer; los recursos offscreen del motor procedural entran como ImTextureID antes del submit.](../diagrams/imgui_vulkan_bridge.svg)

El renderer del canvas de nodos —que también usa `ImDrawList`— se
describe aparte en [Renderer nativo del canvas](native-renderer.md);
esta página cubre la capa gráfica que lo sostiene.
