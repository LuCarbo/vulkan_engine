#include "core/DebugUtils.hpp"

namespace {

PFN_vkSetDebugUtilsObjectNameEXT g_vkSetDebugUtilsObjectNameEXT{nullptr};
PFN_vkCmdBeginDebugUtilsLabelEXT g_vkCmdBeginDebugUtilsLabelEXT{nullptr};
PFN_vkCmdEndDebugUtilsLabelEXT g_vkCmdEndDebugUtilsLabelEXT{nullptr};

} // namespace

namespace DebugUtils {

void init(VkInstance instance) noexcept {
    if (instance == VK_NULL_HANDLE) {
        g_vkSetDebugUtilsObjectNameEXT = nullptr;
        g_vkCmdBeginDebugUtilsLabelEXT = nullptr;
        g_vkCmdEndDebugUtilsLabelEXT = nullptr;
        return;
    }

    g_vkSetDebugUtilsObjectNameEXT = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT"));
    g_vkCmdBeginDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
        vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT"));
    g_vkCmdEndDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
        vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT"));
}

void setObjectName(VkDevice device, VkObjectType type, uint64_t handle, const char* name) noexcept {
    if (g_vkSetDebugUtilsObjectNameEXT == nullptr ||
        device == VK_NULL_HANDLE ||
        handle == 0 ||
        name == nullptr) {
        return;
    }

    VkDebugUtilsObjectNameInfoEXT nameInfo{};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.pNext = nullptr;
    nameInfo.objectType = type;
    nameInfo.objectHandle = handle;
    nameInfo.pObjectName = name;

    (void)g_vkSetDebugUtilsObjectNameEXT(device, &nameInfo);
}

} // namespace DebugUtils

ScopedCmdLabel::ScopedCmdLabel(
    VkCommandBuffer cmd,
    const char* labelName,
    std::array<float, 4> color) noexcept
    : m_cmd(cmd) {
    if (m_cmd != VK_NULL_HANDLE &&
        labelName != nullptr &&
        g_vkCmdBeginDebugUtilsLabelEXT != nullptr &&
        g_vkCmdEndDebugUtilsLabelEXT != nullptr) {
        VkDebugUtilsLabelEXT labelInfo{};
        labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        labelInfo.pNext = nullptr;
        labelInfo.pLabelName = labelName;
        labelInfo.color[0] = color[0];
        labelInfo.color[1] = color[1];
        labelInfo.color[2] = color[2];
        labelInfo.color[3] = color[3];

        g_vkCmdBeginDebugUtilsLabelEXT(m_cmd, &labelInfo);
        m_active = true;
    }
}

ScopedCmdLabel::~ScopedCmdLabel() noexcept {
    if (m_active && m_cmd != VK_NULL_HANDLE && g_vkCmdEndDebugUtilsLabelEXT != nullptr) {
        g_vkCmdEndDebugUtilsLabelEXT(m_cmd);
    }
}
