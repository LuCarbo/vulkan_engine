#include "core/Buffer.hpp"
#include "core/Context.hpp"
#include "core/DebugUtils.hpp"

#include <stdexcept>

Buffer::Buffer(
    const Context& context,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VmaMemoryUsage memoryUsage,
    VmaAllocationCreateFlags flags,
    const char* debugName)
    : m_device(context.getDevice()),
      m_allocator(context.getAllocator()),
      m_size(size) {
    if (size == 0) {
        throw std::runtime_error("Buffer size must be greater than zero.");
    }
    if (m_device == VK_NULL_HANDLE || m_allocator == VK_NULL_HANDLE) {
        throw std::runtime_error("Cannot create Buffer with an uninitialized Context.");
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = memoryUsage;
    allocCreateInfo.flags = flags;

    VmaAllocationInfo allocInfo{};
    if (vmaCreateBuffer(m_allocator, &bufferInfo, &allocCreateInfo, &m_buffer, &m_allocation, &allocInfo) != VK_SUCCESS) {
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
        m_size = 0;
        m_mappedData = nullptr;
        throw std::runtime_error("Failed to create Vulkan buffer via VMA.");
    }

    m_mappedData = allocInfo.pMappedData;

    if (debugName != nullptr) {
        DebugUtils::setObjectName(m_device, VK_OBJECT_TYPE_BUFFER, m_buffer, debugName);
        vmaSetAllocationName(m_allocator, m_allocation, debugName);
    }
}

Buffer::~Buffer() {
    cleanup();
}

Buffer::Buffer(Buffer&& other) noexcept
    : m_device(other.m_device),
      m_allocator(other.m_allocator),
      m_buffer(other.m_buffer),
      m_allocation(other.m_allocation),
      m_size(other.m_size),
      m_mappedData(other.m_mappedData) {
    other.m_device = VK_NULL_HANDLE;
    other.m_allocator = VK_NULL_HANDLE;
    other.m_buffer = VK_NULL_HANDLE;
    other.m_allocation = VK_NULL_HANDLE;
    other.m_size = 0;
    other.m_mappedData = nullptr;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        cleanup();

        m_device = other.m_device;
        m_allocator = other.m_allocator;
        m_buffer = other.m_buffer;
        m_allocation = other.m_allocation;
        m_size = other.m_size;
        m_mappedData = other.m_mappedData;

        other.m_device = VK_NULL_HANDLE;
        other.m_allocator = VK_NULL_HANDLE;
        other.m_buffer = VK_NULL_HANDLE;
        other.m_allocation = VK_NULL_HANDLE;
        other.m_size = 0;
        other.m_mappedData = nullptr;
    }
    return *this;
}

void Buffer::cleanup() noexcept {
    if (m_allocator != VK_NULL_HANDLE && (m_buffer != VK_NULL_HANDLE || m_allocation != VK_NULL_HANDLE)) {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
    }
    m_buffer = VK_NULL_HANDLE;
    m_allocation = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_allocator = VK_NULL_HANDLE;
    m_size = 0;
    m_mappedData = nullptr;
}

void Buffer::upload(const void* data, size_t size, size_t offset) {
    if (size == 0) {
        return;
    }
    if (m_allocator == VK_NULL_HANDLE || m_allocation == VK_NULL_HANDLE) {
        throw std::runtime_error("Cannot upload data to an uninitialized Buffer.");
    }
    if (data == nullptr) {
        throw std::runtime_error("Cannot upload null data pointer to Buffer.");
    }

    const VkDeviceSize uploadOffset = static_cast<VkDeviceSize>(offset);
    const VkDeviceSize uploadSize = static_cast<VkDeviceSize>(size);
    if (uploadOffset > m_size || uploadSize > (m_size - uploadOffset)) {
        throw std::runtime_error("Buffer upload range (offset + size) exceeds buffer size.");
    }

    if (vmaCopyMemoryToAllocation(m_allocator, data, m_allocation, uploadOffset, uploadSize) != VK_SUCCESS) {
        throw std::runtime_error("vmaCopyMemoryToAllocation failed during Buffer::upload.");
    }
}

Buffer Buffer::createDeviceLocal(
    const Context& context,
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    const char* debugName) {
    if (size == 0) {
        throw std::runtime_error("Cannot create device-local Buffer with size 0.");
    }
    if (data == nullptr) {
        throw std::runtime_error("Cannot create device-local Buffer from null data pointer.");
    }

    Buffer staging(
        context,
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

    staging.upload(data, static_cast<size_t>(size), 0);

    Buffer dst(
        context,
        size,
        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        0,
        debugName);

    VkCommandBuffer cmd = context.beginSingleTimeCommands();
    VkBufferCopy copyRegion{0, 0, size};
    vkCmdCopyBuffer(cmd, staging.getHandle(), dst.getHandle(), 1, &copyRegion);
    context.endSingleTimeCommands(cmd);

    return dst;
}
