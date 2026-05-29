# `Vulkan3DRenderer` — segundo pipeline *offscreen*

`src/app/Vulkan3DRenderer.{cpp,hpp}` encapsula el render
3-D del `View3DPanel`.  Pipeline paralelo al de ImGui:
comparte `VkDevice` / cola / *command pool* con
`VulkanContext`, pero posee sus propios *attachments*,
*framebuffer* y *render pass*.

## Configuración del *render pass*

| Attachment        | Formato              | Layout final              |
|-------------------|----------------------|---------------------------|
| 0 — color         | `R8G8B8A8_UNORM`     | `SHADER_READ_ONLY_OPTIMAL` |
| 1 — profundidad   | `D32_SFLOAT`         | `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` |

Dos dependencias entre `EXTERNAL` y *subpass* 0 cubren los
dos sentidos: la lectura del cuadro anterior por ImGui
debe terminar antes de la nueva escritura; la nueva
escritura debe ser visible antes del próximo muestreo.

## Push constants

```glsl
layout(push_constant) uniform PC {
    mat4 viewProj;   // cámara × modelo, multiplicado en CPU
} pc;
```

`model` se multiplica en CPU porque el ángulo del eje
cambia cada cuadro y queríamos evitar un *uniform buffer*
por *frame*.

## Integración con ImGui

`ImGui_ImplVulkan_AddTexture()` devuelve una
`VkDescriptorSet` que ImGui acepta como `ImTextureID`.  El
`Vulkan3DRenderer` la publica con `imguiTextureId()` y el
panel la pasa a `ImGui::Image`.

## Modelo procedural del motor

`buildMotor()` ensambla la malla del motor procedural:
estator hexagonal, rotor cilíndrico, eje, bobinas.  Los
vértices se generan en CPU una sola vez y se suben a un VBO.
El rotor gira aplicando una matriz de rotación al `model`
de los vértices del rotor antes del *push constant*.
