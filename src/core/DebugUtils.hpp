#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <type_traits>

/**
 * @brief Utilities for Vulkan EXT_debug_utils object naming and command buffer debug labels.
 */
namespace DebugUtils {

/**
 * @brief Loads EXT_debug_utils function pointers for the given Vulkan instance.
 *        Resets function pointers to nullptr if instance == VK_NULL_HANDLE.
 */
void init(VkInstance instance) noexcept;

/**
 * @brief Assigns a human-readable debug name to a Vulkan object handle.
 *        Safe no-op if debug utils are unavailable or any argument is invalid.
 */
void setObjectName(VkDevice device, VkObjectType type, uint64_t handle, const char* name) noexcept;

/**
 * @brief Typed overload converting dispatchable or non-dispatchable Vulkan handles to uint64_t.
 */
template <typename T>
void setObjectName(VkDevice device, VkObjectType type, T handle, const char* name) noexcept {
    if constexpr (std::is_null_pointer_v<T>) {
        (void)handle;
        setObjectName(device, type, uint64_t{0}, name);
    } else if constexpr (std::is_pointer_v<T>) {
        setObjectName(device, type, static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(handle)), name);
    } else {
        setObjectName(device, type, static_cast<uint64_t>(handle), name);
    }
}

} // namespace DebugUtils

/**
 * @brief RAII scope guard that begins and ends a debug utils label on a VkCommandBuffer.
 */
class ScopedCmdLabel {
public:
    ScopedCmdLabel(
        VkCommandBuffer cmd,
        const char* labelName,
        std::array<float, 4> color = {0.2f, 0.6f, 1.0f, 1.0f}) noexcept;
    ~ScopedCmdLabel() noexcept;

    ScopedCmdLabel(const ScopedCmdLabel&) = delete;
    ScopedCmdLabel& operator=(const ScopedCmdLabel&) = delete;

    ScopedCmdLabel(ScopedCmdLabel&&) = delete;
    ScopedCmdLabel& operator=(ScopedCmdLabel&&) = delete;

private:
    VkCommandBuffer m_cmd{VK_NULL_HANDLE};
    bool m_active{false};
};
