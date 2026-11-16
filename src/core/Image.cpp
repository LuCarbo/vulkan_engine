#include "core/Image.hpp"
#include "core/Context.hpp"
#include "core/DebugUtils.hpp"

#include <stdexcept>

Image::Image(
    const Context& context,
    VkExtent2D extent,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspectFlags,
    const char* debugName)
    : m_device(context.getDevice()),
      m_allocator(context.getAllocator()),
      m_format(format),
      m_extent(extent) {
    if (extent.width == 0 || extent.height == 0) {
        throw std::runtime_error("Image extent width and height must be greater than zero.");
    }
    if (format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("Image format cannot be VK_FORMAT_UNDEFINED.");
    }
    if (m_device == VK_NULL_HANDLE || m_allocator == VK_NULL_HANDLE) {
        throw std::runtime_error("Cannot create Image with an uninitialized Context.");
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {extent.width, extent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    if (vmaCreateImage(m_allocator, &imageInfo, &allocCreateInfo, &m_image, &m_allocation, nullptr) != VK_SUCCESS) {
        m_image = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
        m_format = VK_FORMAT_UNDEFINED;
        m_extent = {0, 0};
        throw std::runtime_error("Failed to create Vulkan image via VMA.");
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
        // Destroy the allocated image here because the constructor throws before ~Image() runs.
        vmaDestroyImage(m_allocator, m_image, m_allocation);
        m_image = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
        m_imageView = VK_NULL_HANDLE;
        m_format = VK_FORMAT_UNDEFINED;
        m_extent = {0, 0};
        throw std::runtime_error("Failed to create Vulkan image view.");
    }

    if (debugName != nullptr) {
        DebugUtils::setObjectName(m_device, VK_OBJECT_TYPE_IMAGE, m_image, debugName);
        DebugUtils::setObjectName(m_device, VK_OBJECT_TYPE_IMAGE_VIEW, m_imageView, debugName);
        vmaSetAllocationName(m_allocator, m_allocation, debugName);
    }
}

Image::~Image() {
    cleanup();
}

Image::Image(Image&& other) noexcept
    : m_device(other.m_device),
      m_allocator(other.m_allocator),
      m_image(other.m_image),
      m_imageView(other.m_imageView),
      m_allocation(other.m_allocation),
      m_format(other.m_format),
      m_extent(other.m_extent) {
    other.m_device = VK_NULL_HANDLE;
    other.m_allocator = VK_NULL_HANDLE;
    other.m_image = VK_NULL_HANDLE;
    other.m_imageView = VK_NULL_HANDLE;
    other.m_allocation = VK_NULL_HANDLE;
    other.m_format = VK_FORMAT_UNDEFINED;
    other.m_extent = {0, 0};
}

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        cleanup();

        m_device = other.m_device;
        m_allocator = other.m_allocator;
        m_image = other.m_image;
        m_imageView = other.m_imageView;
        m_allocation = other.m_allocation;
        m_format = other.m_format;
        m_extent = other.m_extent;

        other.m_device = VK_NULL_HANDLE;
        other.m_allocator = VK_NULL_HANDLE;
        other.m_image = VK_NULL_HANDLE;
        other.m_imageView = VK_NULL_HANDLE;
        other.m_allocation = VK_NULL_HANDLE;
        other.m_format = VK_FORMAT_UNDEFINED;
        other.m_extent = {0, 0};
    }
    return *this;
}

void Image::cleanup() noexcept {
    if (m_device != VK_NULL_HANDLE && m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_imageView, nullptr);
    }
    m_imageView = VK_NULL_HANDLE;

    if (m_allocator != VK_NULL_HANDLE && (m_image != VK_NULL_HANDLE || m_allocation != VK_NULL_HANDLE)) {
        vmaDestroyImage(m_allocator, m_image, m_allocation);
    }
    m_image = VK_NULL_HANDLE;
    m_allocation = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_allocator = VK_NULL_HANDLE;
    m_format = VK_FORMAT_UNDEFINED;
    m_extent = {0, 0};
}

void Image::transitionLayout(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags2 srcStageMask,
    VkAccessFlags2 srcAccessMask,
    VkPipelineStageFlags2 dstStageMask,
    VkAccessFlags2 dstAccessMask,
    VkImageAspectFlags aspectMask) noexcept {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStageMask;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstStageMask = dstStageMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}
