#pragma once
#include "VulkanContext.hpp"
#include <imgui.h>
#include <vector>
#include <vulkan/vulkan.h>

// -----------------------------------------------------------------------
// Vulkan3DRenderer — offscreen wireframe renderer for the 3D View panel.
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

private:
    bool createColorTarget();
    void destroyColorTarget();
    bool createRenderPass();
    bool createPipeline();
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

    // Offscreen target
    VkImage        m_image      = VK_NULL_HANDLE;
    VkDeviceMemory m_imageMem   = VK_NULL_HANDLE;
    VkImageView    m_imageView  = VK_NULL_HANDLE;
    VkSampler      m_sampler    = VK_NULL_HANDLE;
    VkFramebuffer  m_framebuffer= VK_NULL_HANDLE;

    VkRenderPass     m_renderPass = VK_NULL_HANDLE;
    VkPipelineLayout m_pipeLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline   = VK_NULL_HANDLE;

    // Vertex buffer (host-visible coherent)
    VkBuffer        m_vbo      = VK_NULL_HANDLE;
    VkDeviceMemory  m_vboMem   = VK_NULL_HANDLE;
    uint32_t        m_vertexCount = 0;
    uint32_t        m_indicatorVertexBase = 0;   // first vertex of the indicator pair

    // Per-frame command resources
    VkCommandPool   m_cmdPool  = VK_NULL_HANDLE;
    VkCommandBuffer m_cmdBuf   = VK_NULL_HANDLE;
    VkFence         m_fence    = VK_NULL_HANDLE;

    // ImGui texture binding
    ImTextureID     m_imguiTexture = 0;
};
