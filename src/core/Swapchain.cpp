#include "core/Swapchain.hpp"
#include "core/Context.hpp"
#include "core/Window.hpp"
#include "core/DebugUtils.hpp"

#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <limits>
#include <string>

Swapchain::Swapchain(const Context& context, const Window& window)
    : m_context(context) {
    init(window);
}

Swapchain::~Swapchain() {
    cleanup();
}

Swapchain::Swapchain(Swapchain&& other) noexcept
    : m_context(other.m_context),
      m_swapchain(other.m_swapchain),
      m_images(std::move(other.m_images)),
      m_imageViews(std::move(other.m_imageViews)),
      m_imageFormat(other.m_imageFormat),
      m_depthFormat(other.m_depthFormat),
      m_extent(other.m_extent),
      m_presentMode(other.m_presentMode),
      m_depthImage(std::move(other.m_depthImage)) {
    other.m_swapchain = VK_NULL_HANDLE;
    other.m_imageFormat = VK_FORMAT_UNDEFINED;
    other.m_extent = {0, 0};
}

Swapchain& Swapchain::operator=(Swapchain&& other) noexcept {
    if (this != &other) {
        cleanup();

        m_swapchain = other.m_swapchain;
        m_images = std::move(other.m_images);
        m_imageViews = std::move(other.m_imageViews);
        m_imageFormat = other.m_imageFormat;
        m_depthFormat = other.m_depthFormat;
        m_extent = other.m_extent;
        m_presentMode = other.m_presentMode;
        m_depthImage = std::move(other.m_depthImage);

        other.m_swapchain = VK_NULL_HANDLE;
        other.m_imageFormat = VK_FORMAT_UNDEFINED;
        other.m_extent = {0, 0};
    }
    return *this;
}

void Swapchain::cleanup() {
    m_depthImage.cleanup();

    for (auto imageView : m_imageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_context.getDevice(), imageView, nullptr);
        }
    }
    m_imageViews.clear();
    m_images.clear();

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_context.getDevice(), m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void Swapchain::recreate(const Window& window) {
    // Gestione finestra minimizzata
    auto [width, height] = window.getFramebufferSize();
    while (width == 0 || height == 0) {
        window.pollEvents();
        auto size = window.getFramebufferSize();
        width = size.first;
        height = size.second;
    }

    m_context.waitIdle();

    VkSwapchainKHR oldSwapchain = m_swapchain;
    m_swapchain = VK_NULL_HANDLE; // Evita doppia distruzione in cleanup
    cleanup();

    init(window, oldSwapchain);

    if (oldSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_context.getDevice(), oldSwapchain, nullptr);
    }

    std::cout << "[OK] Swapchain ricreata con successo (" << m_extent.width << "x" << m_extent.height << ")." << std::endl;
}

void Swapchain::init(const Window& window, VkSwapchainKHR oldSwapchain) {
    SwapchainSupportDetails swapchainSupport = querySupport(m_context.getPhysicalDevice(), m_context.getSurface());

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapchainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapchainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapchainSupport.capabilities, window);

    // Richiediamo almeno 1 immagine in più rispetto al minimo per supportare il triple buffering
    uint32_t imageCount = swapchainSupport.capabilities.minImageCount + 1;
    if (swapchainSupport.capabilities.maxImageCount > 0 && imageCount > swapchainSupport.capabilities.maxImageCount) {
        imageCount = swapchainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_context.getSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    const auto& queueFamilies = m_context.getQueueFamilies();
    uint32_t queueFamilyIndices[] = {
        queueFamilies.graphicsFamily.value(),
        queueFamilies.presentFamily.value()
    };

    if (queueFamilies.graphicsFamily != queueFamilies.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = swapchainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = oldSwapchain;

    if (vkCreateSwapchainKHR(m_context.getDevice(), &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        throw std::runtime_error("Impossibile creare la Swapchain!");
    }

    DebugUtils::setObjectName(m_context.getDevice(), VK_OBJECT_TYPE_SWAPCHAIN_KHR, m_swapchain, "Main Swapchain");

    m_imageFormat = surfaceFormat.format;
    m_extent = extent;
    m_presentMode = presentMode;

    // Recupero immagini della swapchain
    uint32_t actualImageCount = 0;
    vkGetSwapchainImagesKHR(m_context.getDevice(), m_swapchain, &actualImageCount, nullptr);
    m_images.resize(actualImageCount);
    vkGetSwapchainImagesKHR(m_context.getDevice(), m_swapchain, &actualImageCount, m_images.data());

    // Creazione delle VkImageView per ciascuna immagine
    m_imageViews.resize(m_images.size());
    for (size_t i = 0; i < m_images.size(); ++i) {
        const std::string imgName = "Swapchain Image " + std::to_string(i);
        DebugUtils::setObjectName(m_context.getDevice(), VK_OBJECT_TYPE_IMAGE, m_images[i], imgName.c_str());

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_imageFormat;

        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_context.getDevice(), &viewInfo, nullptr, &m_imageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Impossibile creare una Image View per la Swapchain!");
        }

        const std::string viewName = "Swapchain ImageView " + std::to_string(i);
        DebugUtils::setObjectName(m_context.getDevice(), VK_OBJECT_TYPE_IMAGE_VIEW, m_imageViews[i], viewName.c_str());
    }

    // Creazione del Depth Buffer associato alla Swapchain
    m_depthImage = Image(
        m_context,
        m_extent,
        m_depthFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        "Swapchain Depth Buffer"
    );

    std::cout << "[OK] Swapchain creata: " << m_extent.width << "x" << m_extent.height
              << " (" << m_images.size() << " immagini a schermo, Depth D32_SFLOAT)" << std::endl;
}

VkResult Swapchain::acquireNextImage(VkSemaphore semaphore, uint32_t& imageIndex, uint64_t timeout) {
    return vkAcquireNextImageKHR(m_context.getDevice(), m_swapchain, timeout, semaphore, VK_NULL_HANDLE, &imageIndex);
}

VkResult Swapchain::present(VkSemaphore waitSemaphore, uint32_t imageIndex) {
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &waitSemaphore;

    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &imageIndex;

    return vkQueuePresentKHR(m_context.getPresentQueue(), &presentInfo);
}

SwapchainSupportDetails Swapchain::querySupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapchainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR Swapchain::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    // Preferiamo il formato standard SRGB a 32-bit (B8G8R8A8)
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR Swapchain::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    // Preferiamo Mailbox (Triple Buffering a bassissima latenza senza tearing)
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            std::cout << "[INFO] Present Mode selezionato: VK_PRESENT_MODE_MAILBOX_KHR (Triple Buffering)" << std::endl;
            return availablePresentMode;
        }
    }

    // Fallback garantito dallo standard Vulkan
    std::cout << "[INFO] Present Mode selezionato: VK_PRESENT_MODE_FIFO_KHR (V-Sync)" << std::endl;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Swapchain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, const Window& window) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    auto [width, height] = window.getFramebufferSize();

    VkExtent2D actualExtent = { width, height };
    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
}

const char* Swapchain::getPresentModeName() const noexcept {
    switch (m_presentMode) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:
            return "IMMEDIATE";
        case VK_PRESENT_MODE_MAILBOX_KHR:
            return "MAILBOX (Triple Buffering)";
        case VK_PRESENT_MODE_FIFO_KHR:
            return "FIFO (V-Sync)";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
            return "FIFO_RELAXED";
        default:
            return "UNKNOWN";
    }
}
