#pragma once
#include "VulkanContext.hpp"
#include <array>
#include <imgui.h>           // by-design: ver nota arriba sobre dependencias
#include <vector>
#include <vulkan/vulkan.h>

// -----------------------------------------------------------------------
// Vulkan3DRenderer — offscreen wireframe renderer for the 3D View panel.
//
// Por qué este header (en `src/app/`) incluye ImGui y Vulkan: es un
// IMPLEMENTADOR concreto de un servicio de rendering, no una abstracción
// del modelo.  El modelo (`src/core/`) sigue siendo puro y nunca
// depende de ImGui ni Vulkan; el catálogo de nodos, gramática, codegen
// e ISimSession no saben que existe este renderer.  Vulkan3DRenderer
// vive justo donde tiene sentido — la capa `app/` que orquesta
// servicios concretos (composition root).  Las dependencias en este
// header son intencionales y NO una violación de capas.
//
// Owns:
//   • A small offscreen color attachment (R8G8B8A8_UNORM) sized to the
//     panel.
//   • A render pass with one color subpass and the EXTERNAL→0 / 0→EXTERNAL
//     subpass dependencies that handle the cross-submission memory
//     barrier with the ImGui main pass.
//   • A graphics pipeline (line topology + push constants).
//   • A host-visible vertex buffer holding the motor wireframe; the
//     last two vertices are the rotating shaft indicator and are
//     overwritten each frame.
//   • Its own command pool + command buffer + fence; render() submits
//     to VulkanContext's graphics queue (same queue as ImGui's main
//     pass, so memory dependencies through the subpass dependency
//     hold without an explicit semaphore).
//   • A persistent ImGui texture binding (via ImGui_ImplVulkan_AddTexture)
//     so the panel can call ImGui::Image((ImTextureID)set, size).
//
// The class is intentionally one big translation unit — Vulkan
// boilerplate stays in one place rather than spreading across utility
// headers.
// -----------------------------------------------------------------------
class Vulkan3DRenderer {
public:
    Vulkan3DRenderer() = default;
    ~Vulkan3DRenderer();

    Vulkan3DRenderer(const Vulkan3DRenderer&)            = delete;
    Vulkan3DRenderer& operator=(const Vulkan3DRenderer&) = delete;

    // Allocate all Vulkan resources. Returns false on failure — caller
    // should fall back to CPU rendering.
    bool init(VulkanContext& ctx);

    // Free all owned resources. Safe to call multiple times.
    void shutdown();

    // Resize the offscreen color target. No-op if already sized.
    void resize(uint32_t width, uint32_t height);

    // Record + submit one frame's worth of commands.
    // The shaft indicator vertex is rotated to `shaftAngle` (radians).
    // azimuth / elevation / zoom drive the orbit camera.
    void render(float shaftAngle, float azimuthDeg,
                float elevationDeg, float zoom);

    // ImGui texture handle suitable for ImGui::Image. Valid for the
    // lifetime of the renderer (recreated on resize, but the value
    // passed to ImGui::Image is reloaded from imguiTextureId()).
    ImTextureID imguiTextureId() const { return m_imguiTexture; }

    // Read-only — useful for the panel to skip rendering when not init'd.
    bool ready() const { return m_ready && m_extent.width > 0 && m_extent.height > 0; }

    // -- Procedural mesh upload ----------------------------------------
    // Replace the wireframe with a user-supplied one. `verts3` is a flat
    // (x, y, z) array of size 3 * vertexCount; `edges` indexes pairs of
    // those vertices to form the line list. The given (r, g, b) is applied
    // uniformly. The indicator pair is appended at the tail so the existing
    // updateIndicatorVertex path keeps working unchanged.
    //
    // The VBO is reallocated when the new mesh would exceed the existing
    // capacity (vkQueueWaitIdle first). Returns true on success.
    bool uploadProceduralWireframe(const std::vector<float>& verts3,
                                   const std::vector<std::array<int, 2>>& edges,
                                   float r, float g, float b);

    // Restore the original hard-coded DC-motor wireframe. View3DPanel
    // calls this when the user removes a PMSMSizing node and the panel
    // reverts to its default scene.
    void rebuildLegacyMotor();

    // Modo de render del asset (solid / wire / both).  Cambiar en
    // cualquier momento; el siguiente render() respeta el nuevo valor.
    enum class AssetRenderMode { Solid = 0, Wire = 1, Both = 2 };
    void setAssetRenderMode(AssetRenderMode m) { m_assetMode = m; }

    // -- Asset 3D (DeviceAsset → triángulos sólidos con depth test) ----
    // Sube una malla indexada al GPU.  positions y normals son arrays
    // flat de 3*N floats; indices son tripletas de tri-list (size%3==0).
    // tint{R,G,B} se aplica uniformemente al difuso.  El View3DPanel
    // convierte el DeviceAsset (parts + joints) en estos arrays antes
    // de llamar — Vulkan3DRenderer no conoce el DeviceAsset por SRP.
    // Mientras hay una malla de asset cargada (m_assetActive=true),
    // render() bindea el solid pipeline en vez del line pipeline.
    bool uploadAssetMesh(const std::vector<float>&    positions,
                         const std::vector<float>&    normals,
                         const std::vector<uint32_t>& indices,
                         float tintR, float tintG, float tintB);

    // Volver al render del motor wireframe legacy.
    void clearAsset();

    // Per-frame deformation overlay — applies an exaggerated
    // mode-shape radial displacement to the cached base mesh and
    // rewrites the VBO before the next render() submit.
    //   Δr(θ, t) = amplitude * cos(modeOrder * θ) * sin(2π · freq · t)
    // Pass `active = false` to disable (the base mesh is restored on
    // the next render).
    void setDeformation(bool active, float freq, float modeOrder,
                        float amplitude);

    // Vertex layout used by the line pipeline (motor wireframe).
    struct Vertex { float pos[3]; float color[3]; };

    // Vertex layout del solid pipeline (asset glTF).  Posición + normal
    // por vértice; tint + iluminación van por push constant.
    struct SolidVertex { float pos[3]; float normal[3]; };

private:
    bool createColorTarget();
    void destroyColorTarget();
    bool createDepthTarget();      // separado de color para SRP — vida ligada al framebuffer
    void destroyDepthTarget();
    bool createRenderPass();
    bool createPipeline();
    bool createSolidPipeline();    // pipeline de triángulos con depth test + Lambert
    bool createWirePipeline();     // mismo vertex layout que solid, polygonMode=LINE
    bool createVertexBuffer();
    bool createCommandResources();

    void updateIndicatorVertex(float shaftAngle);
    void buildBaseVertices();

    // Helpers
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
    void     uploadToBuffer(VkDeviceMemory mem, VkDeviceSize size, const void* data);

    VulkanContext* m_ctx        = nullptr;
    bool           m_ready      = false;
    VkExtent2D     m_extent     = { 0, 0 };

    // Offscreen color target
    VkImage        m_image      = VK_NULL_HANDLE;
    VkDeviceMemory m_imageMem   = VK_NULL_HANDLE;
    VkImageView    m_imageView  = VK_NULL_HANDLE;
    VkSampler      m_sampler    = VK_NULL_HANDLE;
    VkFramebuffer  m_framebuffer= VK_NULL_HANDLE;

    // Offscreen depth target — necesario para depth-testing del solid
    // pipeline (asset glTF).  El line pipeline lo ignora (depthTest=FALSE).
    VkImage        m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMem   = VK_NULL_HANDLE;
    VkImageView    m_depthView  = VK_NULL_HANDLE;
    VkFormat       m_depthFormat = VK_FORMAT_D32_SFLOAT;

    VkRenderPass     m_renderPass     = VK_NULL_HANDLE;
    VkPipelineLayout m_pipeLayout     = VK_NULL_HANDLE;
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;  // motor wireframe (lines)
    VkPipelineLayout m_solidPipeLayout = VK_NULL_HANDLE; // asset solid (triangles)
    VkPipeline       m_solidPipeline   = VK_NULL_HANDLE;
    VkPipeline       m_wirePipeline    = VK_NULL_HANDLE; // asset wireframe (LINE polygon mode)

    // Vertex buffer (host-visible coherent)
    VkBuffer        m_vbo      = VK_NULL_HANDLE;
    VkDeviceMemory  m_vboMem   = VK_NULL_HANDLE;
    uint32_t        m_vertexCount = 0;
    uint32_t        m_indicatorVertexBase = 0;   // first vertex of the indicator pair
    VkDeviceSize    m_vboCapacityBytes = 0;      // current VBO allocation

    // Per-frame command resources
    VkCommandPool   m_cmdPool  = VK_NULL_HANDLE;
    VkCommandBuffer m_cmdBuf   = VK_NULL_HANDLE;
    VkFence         m_fence    = VK_NULL_HANDLE;

    // Cached undeformed vertex array — kept in sync with the VBO
    // whenever the mesh changes. The deformation update reads from
    // this and writes displaced copies into the VBO.
    std::vector<Vertex> m_baseMesh;

    // -- Asset mesh state (subido vía uploadAssetMesh) -----------------
    VkBuffer       m_assetVbo         = VK_NULL_HANDLE;
    VkDeviceMemory m_assetVboMem      = VK_NULL_HANDLE;
    VkDeviceSize   m_assetVboCapBytes = 0;
    VkBuffer       m_assetIbo         = VK_NULL_HANDLE;
    VkDeviceMemory m_assetIboMem      = VK_NULL_HANDLE;
    VkDeviceSize   m_assetIboCapBytes = 0;
    uint32_t       m_assetIndexCount  = 0;
    bool           m_assetActive      = false;
    float          m_assetTint[3]     = { 0.45f, 0.73f, 1.00f };
    AssetRenderMode m_assetMode       = AssetRenderMode::Solid;

    // Deformation parameters — set by setDeformation, consumed each
    // frame in render(). When `active` is false the VBO is just
    // refreshed from m_baseMesh once and then left alone.
    bool  m_deformActive    = false;
    float m_deformFreq      = 0.0f;
    float m_deformMode      = 2.0f;
    float m_deformAmplitude = 0.0f;
    bool  m_deformDirty     = false;   // true when we need to restore base mesh

    // ImGui texture binding
    ImTextureID     m_imguiTexture = 0;
};
