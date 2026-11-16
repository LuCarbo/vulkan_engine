#pragma once

#include <vulkan/vulkan.h>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <vma/vk_mem_alloc.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

class Context;

/**
 * @brief RAII wrapper for a VMA-managed 2D VkImage and its VkImageView.
 */
class Image {
public:
    Image() = default;

    /**
     * @brief Allocates a device-local 2D Vulkan image and creates a matching VkImageView.
     */
    Image(
        const Context& context,
        VkExtent2D extent,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageAspectFlags aspectFlags,
        const char* debugName = nullptr);

    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;

    /**
     * @brief Destroys the image view and VMA image allocation, resetting all handles and metadata.
     */
    void cleanup() noexcept;

    /**
     * @brief Records a Synchronization2 image memory barrier for a single mip/layer layout transition.
     */
    static void transitionLayout(
        VkCommandBuffer cmd,
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkPipelineStageFlags2 srcStageMask,
        VkAccessFlags2 srcAccessMask,
        VkPipelineStageFlags2 dstStageMask,
        VkAccessFlags2 dstAccessMask,
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT) noexcept;

    [[nodiscard]] VkImage getHandle() const noexcept { return m_image; }
    [[nodiscard]] VkImageView getView() const noexcept { return m_imageView; }
    [[nodiscard]] VkFormat getFormat() const noexcept { return m_format; }
    [[nodiscard]] VkExtent2D getExtent() const noexcept { return m_extent; }

private:
    VkDevice m_device{VK_NULL_HANDLE};
    VmaAllocator m_allocator{VK_NULL_HANDLE};
    VkImage m_image{VK_NULL_HANDLE};
    VkImageView m_imageView{VK_NULL_HANDLE};
    VmaAllocation m_allocation{VK_NULL_HANDLE};
    VkFormat m_format{VK_FORMAT_UNDEFINED};
    VkExtent2D m_extent{0, 0};
};
