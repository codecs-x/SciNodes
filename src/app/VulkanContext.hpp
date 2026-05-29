#pragma once
#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>
#include <vector>

// -----------------------------------------------------------------------
// VulkanContext — owns the Vulkan instance, device, swapchain, and the
// ImGui Vulkan backend resources.  One instance per application lifetime.
// -----------------------------------------------------------------------
class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext();

    // No copy / no move (owns raw Vulkan handles)
    VulkanContext(const VulkanContext&)            = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    void init(SDL_Window* window);
    void shutdown();

    // Called at the start of every frame: acquires the next swapchain image.
    // Returns false if the swapchain must be rebuilt (window resized).
    bool beginFrame();

    // Submits the ImGui draw data and presents the frame.
    void endFrame();

    // Called when the SDL window is resized.
    void rebuildSwapchain(int w, int h);

    // Expose handles needed by imgui_impl_vulkan
    VkInstance       instance()       const { return m_instance; }
    VkDevice         device()         const { return m_device; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkQueue          graphicsQueue()  const { return m_graphicsQueue; }
    uint32_t         graphicsFamily() const { return m_graphicsFamily; }
    VkRenderPass     renderPass()     const { return m_renderPass; }
    VkDescriptorPool descriptorPool() const { return m_descriptorPool; }
    uint32_t         imageCount()     const { return static_cast<uint32_t>(m_swapImages.size()); }
    uint32_t         minImageCount()  const { return m_minImageCount; }

private:
    void createInstance(SDL_Window* window);
    void createSurface(SDL_Window* window);
    void pickPhysicalDevice();
    void createDevice();
    void createDescriptorPool();
    void createSwapchain(int w, int h);
    void createRenderPass();
    void createFramebuffers();
    void createCommandPoolAndBuffers();
    void createSyncObjects();
    void destroySwapchainResources();

    VkInstance       m_instance       = VK_NULL_HANDLE;
    VkSurfaceKHR     m_surface        = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue  = VK_NULL_HANDLE;
    uint32_t         m_graphicsFamily = 0;

    VkSwapchainKHR           m_swapchain     = VK_NULL_HANDLE;
    VkFormat                 m_swapFormat    = VK_FORMAT_UNDEFINED;
    VkExtent2D               m_swapExtent    = {};
    uint32_t                 m_minImageCount = 2;
    std::vector<VkImage>     m_swapImages;
    std::vector<VkImageView> m_swapImageViews;
    std::vector<VkFramebuffer> m_framebuffers;

    VkRenderPass     m_renderPass     = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkCommandPool    m_commandPool    = VK_NULL_HANDLE;

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore>     m_imageAvailSem;
    std::vector<VkSemaphore>     m_renderDoneSem;
    std::vector<VkFence>         m_inFlightFences;

    uint32_t m_currentFrame = 0;
    uint32_t m_imageIndex   = 0;
};
