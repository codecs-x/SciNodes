#include "VulkanContext.hpp"

#include <SDL2/SDL_vulkan.h>
#include <imgui_impl_vulkan.h>

#include <stdexcept>
#include <vector>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
namespace {

void vkCheck(VkResult r, const char* msg) {
    if (r != VK_SUCCESS)
        throw std::runtime_error(std::string(msg) + " (VkResult=" + std::to_string(r) + ")");
}

uint32_t findGraphicsFamily(VkPhysicalDevice pd, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, props.data());

    for (uint32_t i = 0; i < count; ++i) {
        VkBool32 presentOk = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &presentOk);
        if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentOk)
            return i;
    }
    throw std::runtime_error("No suitable graphics+present queue family");
}

VkSurfaceFormatKHR chooseSurfaceFormat(VkPhysicalDevice pd, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &count, formats.data());

    for (const auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    return formats[0];
}

VkPresentModeKHR choosePresentMode(VkPhysicalDevice /*pd*/, VkSurfaceKHR /*surface*/) {
    // FIFO = v-sync clásico, capa a la tasa de refresh del monitor
    // (~60 Hz en pantallas estándar).  Antes preferíamos MAILBOX, que
    // no bloquea: el resultado eran 300-500 FPS quemando CPU/GPU sin
    // ganancia visible (la UI no cambia entre frames si el grafo no
    // se mueve).  FIFO está garantizado por la spec en todas las
    // GPUs Vulkan, así que no necesitamos enumerar disponibles.
    return VK_PRESENT_MODE_FIFO_KHR;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void VulkanContext::init(SDL_Window* window) {
    createInstance(window);
    createSurface(window);
    pickPhysicalDevice();
    createDevice();
    createDescriptorPool();

    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    createSwapchain(w, h);
    createRenderPass();
    createFramebuffers();
    createCommandPoolAndBuffers();
    createSyncObjects();
}

void VulkanContext::shutdown() {
    // Defensa contra double-shutdown — el destructor también llama a
    // shutdown() si m_device sigue no-nulo, así que el cuerpo tiene que
    // ser idempotente.
    if (m_device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(m_device);

    for (auto s : m_imageAvailSem)  vkDestroySemaphore(m_device, s, nullptr);
    for (auto s : m_renderDoneSem)  vkDestroySemaphore(m_device, s, nullptr);
    for (auto f : m_inFlightFences) vkDestroyFence(m_device, f, nullptr);
    m_imageAvailSem.clear();
    m_renderDoneSem.clear();
    m_inFlightFences.clear();

    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    destroySwapchainResources();

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    vkDestroyDevice(m_device, nullptr);
    m_device = VK_NULL_HANDLE;
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

VulkanContext::~VulkanContext() {
    if (m_device != VK_NULL_HANDLE) shutdown();
}

bool VulkanContext::beginFrame() {
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    VkResult r = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                        m_imageAvailSem[m_currentFrame],
                                        VK_NULL_HANDLE, &m_imageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) return false;
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("vkAcquireNextImageKHR failed");

    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(cmd, &beginInfo), "vkBeginCommandBuffer");

    VkClearValue clearColor = {{{0.12f, 0.12f, 0.12f, 1.0f}}};
    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass        = m_renderPass;
    rpInfo.framebuffer       = m_framebuffers[m_imageIndex];
    rpInfo.renderArea.extent = m_swapExtent;
    rpInfo.clearValueCount   = 1;
    rpInfo.pClearValues      = &clearColor;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    return true;
}

void VulkanContext::endFrame() {
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    // ImGui records into the active command buffer via imgui_impl_vulkan
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRenderPass(cmd);
    vkCheck(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = &m_imageAvailSem[m_currentFrame];
    submitInfo.pWaitDstStageMask    = &waitStage;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &m_renderDoneSem[m_currentFrame];
    vkCheck(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo,
                          m_inFlightFences[m_currentFrame]), "vkQueueSubmit");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &m_renderDoneSem[m_currentFrame];
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &m_swapchain;
    presentInfo.pImageIndices      = &m_imageIndex;
    vkQueuePresentKHR(m_graphicsQueue, &presentInfo);

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanContext::rebuildSwapchain(int w, int h) {
    vkDeviceWaitIdle(m_device);
    destroySwapchainResources();
    createSwapchain(w, h);
    // createRenderPass es indispensable aquí: destroySwapchainResources
    // destruyó m_renderPass y lo dejó en VK_NULL_HANDLE.  createFramebuffers
    // referencia m_renderPass; sin recrearlo primero, vkCreateFramebuffer
    // recibe handle nulo, vkCheck lanza, el stack unwind dispara double-
    // shutdown del VkDevice y al final el loader aborta con
    // VUID-vkDeviceWaitIdle-device-parameter.  Reproducido al maximizar
    // la ventana de SciNodes.
    createRenderPass();
    createFramebuffers();
}

// ---------------------------------------------------------------------------
// Private creation helpers
// ---------------------------------------------------------------------------
void VulkanContext::createInstance(SDL_Window* window) {
    uint32_t extCount = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &extCount, nullptr);
    std::vector<const char*> exts(extCount);
    SDL_Vulkan_GetInstanceExtensions(window, &extCount, exts.data());

    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "SciNodes";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName        = "SciNodes Engine";
    appInfo.apiVersion         = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &appInfo;
    ci.enabledExtensionCount   = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();

    vkCheck(vkCreateInstance(&ci, nullptr, &m_instance), "vkCreateInstance");
}

void VulkanContext::createSurface(SDL_Window* window) {
    if (!SDL_Vulkan_CreateSurface(window, m_instance, &m_surface))
        throw std::runtime_error("SDL_Vulkan_CreateSurface failed");
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) throw std::runtime_error("No Vulkan physical devices");
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    // Prefer discrete GPU; fall back to first device
    for (auto pd : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pd, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            m_physicalDevice = pd;
            return;
        }
    }
    m_physicalDevice = devices[0];
}

void VulkanContext::createDevice() {
    m_graphicsFamily = findGraphicsFamily(m_physicalDevice, m_surface);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueCI{};
    queueCI.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCI.queueFamilyIndex = m_graphicsFamily;
    queueCI.queueCount       = 1;
    queueCI.pQueuePriorities = &priority;

    const char* deviceExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    // Habilitamos fillModeNonSolid para soportar VK_POLYGON_MODE_LINE en
    // el wireframe pipeline (Vulkan3DRenderer).  Es feature opcional pero
    // soportada por todos los GPUs desktop modernos (Intel HD 4xxx+,
    // NVIDIA Fermi+, AMD GCN+).  Si en el futuro hace falta correr en
    // hardware sin esta feature, consultar VkPhysicalDeviceFeatures
    // antes de habilitarla y caer al solid-only.
    VkPhysicalDeviceFeatures enabledFeatures{};
    enabledFeatures.fillModeNonSolid = VK_TRUE;

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = 1;
    ci.pQueueCreateInfos       = &queueCI;
    ci.enabledExtensionCount   = 1;
    ci.ppEnabledExtensionNames = deviceExts;
    ci.pEnabledFeatures        = &enabledFeatures;

    vkCheck(vkCreateDevice(m_physicalDevice, &ci, nullptr, &m_device), "vkCreateDevice");
    vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
}

void VulkanContext::createDescriptorPool() {
    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 };

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    ci.maxSets       = 16;
    ci.poolSizeCount = 1;
    ci.pPoolSizes    = &poolSize;

    vkCheck(vkCreateDescriptorPool(m_device, &ci, nullptr, &m_descriptorPool),
            "vkCreateDescriptorPool");
}

void VulkanContext::createSwapchain(int w, int h) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);

    auto surfFmt  = chooseSurfaceFormat(m_physicalDevice, m_surface);
    auto pMode    = choosePresentMode(m_physicalDevice, m_surface);
    m_swapFormat  = surfFmt.format;
    m_minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0)
        m_minImageCount = std::min(m_minImageCount, caps.maxImageCount);

    m_swapExtent = (caps.currentExtent.width != UINT32_MAX)
                   ? caps.currentExtent
                   : VkExtent2D{ static_cast<uint32_t>(w), static_cast<uint32_t>(h) };

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = m_surface;
    ci.minImageCount    = m_minImageCount;
    ci.imageFormat      = m_swapFormat;
    ci.imageColorSpace  = surfFmt.colorSpace;
    ci.imageExtent      = m_swapExtent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform     = caps.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = pMode;
    ci.clipped          = VK_TRUE;

    vkCheck(vkCreateSwapchainKHR(m_device, &ci, nullptr, &m_swapchain), "vkCreateSwapchainKHR");

    uint32_t imgCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imgCount, nullptr);
    m_swapImages.resize(imgCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imgCount, m_swapImages.data());

    m_swapImageViews.resize(imgCount);
    for (uint32_t i = 0; i < imgCount; ++i) {
        VkImageViewCreateInfo ivCI{};
        ivCI.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivCI.image                           = m_swapImages[i];
        ivCI.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        ivCI.format                          = m_swapFormat;
        ivCI.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ivCI.subresourceRange.levelCount     = 1;
        ivCI.subresourceRange.layerCount     = 1;
        vkCheck(vkCreateImageView(m_device, &ivCI, nullptr, &m_swapImageViews[i]),
                "vkCreateImageView");
    }
}

void VulkanContext::createRenderPass() {
    VkAttachmentDescription colorAtt{};
    colorAtt.format         = m_swapFormat;
    colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments    = &colorAtt;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies   = &dep;

    vkCheck(vkCreateRenderPass(m_device, &ci, nullptr, &m_renderPass), "vkCreateRenderPass");
}

void VulkanContext::createFramebuffers() {
    m_framebuffers.resize(m_swapImageViews.size());
    for (size_t i = 0; i < m_swapImageViews.size(); ++i) {
        VkFramebufferCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = m_renderPass;
        ci.attachmentCount = 1;
        ci.pAttachments    = &m_swapImageViews[i];
        ci.width           = m_swapExtent.width;
        ci.height          = m_swapExtent.height;
        ci.layers          = 1;
        vkCheck(vkCreateFramebuffer(m_device, &ci, nullptr, &m_framebuffers[i]),
                "vkCreateFramebuffer");
    }
}

void VulkanContext::createCommandPoolAndBuffers() {
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = m_graphicsFamily;
    vkCheck(vkCreateCommandPool(m_device, &ci, nullptr, &m_commandPool), "vkCreateCommandPool");

    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    vkCheck(vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()),
            "vkAllocateCommandBuffers");
}

void VulkanContext::createSyncObjects() {
    m_imageAvailSem.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderDoneSem.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fenCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                 nullptr, VK_FENCE_CREATE_SIGNALED_BIT };

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkCheck(vkCreateSemaphore(m_device, &semCI, nullptr, &m_imageAvailSem[i]),  "semaphore");
        vkCheck(vkCreateSemaphore(m_device, &semCI, nullptr, &m_renderDoneSem[i]),  "semaphore");
        vkCheck(vkCreateFence    (m_device, &fenCI, nullptr, &m_inFlightFences[i]), "fence");
    }
}

void VulkanContext::destroySwapchainResources() {
    for (auto fb : m_framebuffers)   vkDestroyFramebuffer(m_device, fb, nullptr);
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
    for (auto iv : m_swapImageViews) vkDestroyImageView(m_device, iv, nullptr);
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
    m_framebuffers.clear();
    m_swapImageViews.clear();
    m_swapImages.clear();
}
