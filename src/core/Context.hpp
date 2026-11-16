#pragma once

#include <vulkan/vulkan.h>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <vma/vk_mem_alloc.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <optional>
#include <vector>
#include <string>

class Window;

/**
 * @brief Indici delle famiglie di code hardware necessarie.
 */
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    [[nodiscard]] bool isComplete() const noexcept {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

/**
 * @brief Incapsula il contesto hardware Vulkan 1.4:
 * Istanza, Validation Layers, Debug Utils, Surface, Physical Device, Logical Device,
 * Code, VmaAllocator, Push Descriptors e Command Pool per trasferimenti sincroni.
 */
class Context {
public:
    explicit Context(const Window& window);
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;

    [[nodiscard]] VkInstance getInstance() const noexcept { return m_instance; }
    [[nodiscard]] VkSurfaceKHR getSurface() const noexcept { return m_surface; }
    [[nodiscard]] VkPhysicalDevice getPhysicalDevice() const noexcept { return m_physicalDevice; }
    [[nodiscard]] VkDevice getDevice() const noexcept { return m_device; }
    [[nodiscard]] VkQueue getGraphicsQueue() const noexcept { return m_graphicsQueue; }
    [[nodiscard]] VkQueue getPresentQueue() const noexcept { return m_presentQueue; }
    [[nodiscard]] const QueueFamilyIndices& getQueueFamilies() const noexcept { return m_queueIndices; }
    [[nodiscard]] VmaAllocator getAllocator() const noexcept { return m_allocator; }
    [[nodiscard]] VkCommandPool getCommandPool() const noexcept { return m_commandPool; }
    [[nodiscard]] const VkPhysicalDeviceProperties& getDeviceProperties() const noexcept { return m_deviceProperties; }

    [[nodiscard]] bool hasPushDescriptors() const noexcept { return m_pushDescriptorSupported; }
    [[nodiscard]] bool hasNonSolidFill() const noexcept { return m_nonSolidFillSupported; }
    [[nodiscard]] bool hasSamplerAnisotropy() const noexcept { return m_samplerAnisotropySupported; }
    [[nodiscard]] bool hasDebugUtils() const noexcept { return m_debugUtilsSupported; }

    void cmdPushDescriptorSet(
        VkCommandBuffer cmd,
        VkPipelineBindPoint bindPoint,
        VkPipelineLayout layout,
        uint32_t set,
        uint32_t descriptorWriteCount,
        const VkWriteDescriptorSet* pDescriptorWrites) const noexcept;

    [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(VkCommandBuffer cmd) const;

    void waitIdle() const;

private:
    VkInstance m_instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT m_debugMessenger{VK_NULL_HANDLE};
    VkSurfaceKHR m_surface{VK_NULL_HANDLE};
    VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    VkQueue m_graphicsQueue{VK_NULL_HANDLE};
    VkQueue m_presentQueue{VK_NULL_HANDLE};
    QueueFamilyIndices m_queueIndices;
    VkPhysicalDeviceProperties m_deviceProperties{};

    VmaAllocator m_allocator{VK_NULL_HANDLE};
    VkCommandPool m_commandPool{VK_NULL_HANDLE};
    PFN_vkCmdPushDescriptorSetKHR m_pfnCmdPushDescriptorSet{nullptr};

    bool m_pushDescriptorSupported{false};
    bool m_nonSolidFillSupported{false};
    bool m_samplerAnisotropySupported{false};
    bool m_debugUtilsSupported{false};

    void createInstance();
    void setupDebugMessenger();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createAllocator();
    void createCommandPool();

    [[nodiscard]] QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;
    [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice device);
    [[nodiscard]] bool checkDeviceExtensionSupport(VkPhysicalDevice device) const;
    [[nodiscard]] bool checkValidationLayerSupport() const;
    [[nodiscard]] bool checkInstanceExtensionSupport(const char* extensionName) const;
    [[nodiscard]] std::vector<const char*> getRequiredExtensions();
};
