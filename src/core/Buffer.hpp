#pragma once

#include <vulkan/vulkan.h>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <vma/vk_mem_alloc.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <cstddef>

class Context;

/**
 * @brief RAII wrapper for a VMA-managed VkBuffer and its memory allocation.
 */
class Buffer {
public:
    Buffer() = default;

    /**
     * @brief Allocates a Vulkan buffer via VMA and optionally applies debug names.
     */
    Buffer(
        const Context& context,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO,
        VmaAllocationCreateFlags flags = 0,
        const char* debugName = nullptr);

    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    /**
     * @brief Destroys the underlying buffer and resets all handles and metadata.
     */
    void cleanup() noexcept;

    /**
     * @brief Copies host memory into the buffer allocation at the specified byte offset.
     */
    void upload(const void* data, size_t size, size_t offset = 0);

    /**
     * @brief Creates a device-local buffer and populates it via a transient staging buffer.
     */
    [[nodiscard]] static Buffer createDeviceLocal(
        const Context& context,
        const void* data,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        const char* debugName = nullptr);

    [[nodiscard]] VkBuffer getHandle() const noexcept { return m_buffer; }
    [[nodiscard]] VkDeviceSize getSize() const noexcept { return m_size; }
    [[nodiscard]] void* getMappedData() const noexcept { return m_mappedData; }
    [[nodiscard]] VmaAllocation getAllocation() const noexcept { return m_allocation; }

private:
    VkDevice m_device{VK_NULL_HANDLE};
    VmaAllocator m_allocator{VK_NULL_HANDLE};
    VkBuffer m_buffer{VK_NULL_HANDLE};
    VmaAllocation m_allocation{VK_NULL_HANDLE};
    VkDeviceSize m_size{0};
    void* m_mappedData{nullptr};
};
