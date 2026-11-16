#pragma once

#include "scene/Camera.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

class Context;
class Window;

/**
 * @brief Telemetry snapshot and interactive parameters exposed by the Vulkan Backend Inspector UI.
 */
struct InspectorState {
    // Hardware & Swapchain info
    const char* deviceName{"Unknown GPU"};
    uint32_t apiVersion{VK_API_VERSION_1_4};
    const char* presentModeName{"FIFO"};
    uint32_t swapchainWidth{0};
    uint32_t swapchainHeight{0};
    bool pushDescriptorsEnabled{false};
    bool wireframeSupported{false};

    // Frame & GPU Pass Timings
    float fps{0.0f};
    float cpuFrameMs{0.0f};
    float gpuFrameMs{0.0f};
    float shadowPassMs{0.0f};
    float mainPassMs{0.0f};

    // Geometry Counters
    uint32_t drawCalls{0};
    uint32_t renderedTriangles{0};

    // VMA Memory Stats
    float deviceLocalMB{0.0f};
    uint32_t deviceLocalAllocations{0};
    float hostVisibleMB{0.0f};
    uint32_t hostVisibleAllocations{0};
    float totalReservedBlockMB{0.0f};
    uint32_t totalBlocks{0};

    // Interactive Controls
    float sunAzimuthDeg{42.0f};
    float sunElevationDeg{48.0f};
    float sunIntensity{1.85f};
    float ambientStrength{0.55f};
    float waterWaveSpeed{1.60f};
    float waterWaveHeight{0.14f};
    CameraMode cameraMode{CameraMode::Orbit};
    bool wireframe{false};
    int32_t debugView{0};

    // PSX Retro Aesthetic Controls
    bool psxEnabled{true};
    int32_t psxResolutionPreset{1}; // 0 = 256x224, 1 = 320x240, 2 = 512x384, 3 = Native
    float psxVertexSnap{1.0f};
    bool psxAffineWarping{true};
    bool psx15BitColor{true};
    float psxDitherStrength{1.0f};
    float psxFogFar{95.0f};
};

/**
 * @brief RAII wrapper for Dear ImGui (GLFW + Vulkan Dynamic Rendering backend).
 *
 * Non-copyable and non-movable so the address of m_colorFormat passed to
 * VkPipelineRenderingCreateInfo remains stable for the lifetime of the backend.
 */
class ImGuiLayer {
public:
    ImGuiLayer() = default;
    ImGuiLayer(const Context& context, const Window& window, VkFormat colorFormat, VkFormat depthFormat, uint32_t imageCount);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    ImGuiLayer(ImGuiLayer&&) = delete;
    ImGuiLayer& operator=(ImGuiLayer&&) = delete;

    void cleanup() noexcept;
    void beginFrame();
    void drawBackendInspector(InspectorState& state);
    void render(VkCommandBuffer cmd);

    [[nodiscard]] bool wantsCaptureMouse() const noexcept;
    [[nodiscard]] bool wantsCaptureKeyboard() const noexcept;

private:
    VkDevice m_device{VK_NULL_HANDLE};
    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
    VkFormat m_colorFormat{VK_FORMAT_UNDEFINED};
    bool m_initialized{false};
};
