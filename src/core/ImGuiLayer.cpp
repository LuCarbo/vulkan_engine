#include "core/ImGuiLayer.hpp"
#include "core/Context.hpp"
#include "core/Window.hpp"
#include "core/DebugUtils.hpp"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <array>
#include <stdexcept>

namespace {

void applyInspectorStyle() {
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);

    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.12f, 0.86f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.12f, 0.16f, 0.94f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.22f, 0.34f, 0.96f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.18f, 0.28f, 0.42f, 0.55f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.38f, 0.58f, 0.75f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.45f, 0.68f, 0.90f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.17f, 0.22f, 0.78f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.27f, 0.36f, 0.88f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.35f, 0.48f, 0.95f);
}

} // namespace

ImGuiLayer::ImGuiLayer(
    const Context& context,
    const Window& window,
    VkFormat colorFormat,
    VkFormat depthFormat,
    uint32_t imageCount)
    : m_device(context.getDevice()),
      m_colorFormat(colorFormat) {
    const auto& queueFamilies = context.getQueueFamilies();
    if (m_device == VK_NULL_HANDLE ||
        !queueFamilies.graphicsFamily.has_value() ||
        context.getGraphicsQueue() == VK_NULL_HANDLE ||
        window.getHandle() == nullptr ||
        m_colorFormat == VK_FORMAT_UNDEFINED) {
        m_device = VK_NULL_HANDLE;
        m_colorFormat = VK_FORMAT_UNDEFINED;
        throw std::runtime_error("ImGuiLayer received invalid Vulkan or GLFW handles!");
    }

    auto rollbackInit = [&](bool shutdownVulkan, bool shutdownGlfw, bool destroyImGuiContext) noexcept {
        if (shutdownVulkan) {
            ImGui_ImplVulkan_Shutdown();
        }
        if (shutdownGlfw) {
            ImGui_ImplGlfw_Shutdown();
        }
        if (destroyImGuiContext && ImGui::GetCurrentContext() != nullptr) {
            ImGui::DestroyContext();
        }
        if (m_device != VK_NULL_HANDLE && m_descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
            m_descriptorPool = VK_NULL_HANDLE;
        }
        m_device = VK_NULL_HANDLE;
        m_colorFormat = VK_FORMAT_UNDEFINED;
        m_initialized = false;
    };

    const std::array<VkDescriptorPoolSize, 1> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16}
    }};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 16;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        rollbackInit(false, false, false);
        throw std::runtime_error("Failed to create ImGui descriptor pool!");
    }

    DebugUtils::setObjectName(m_device, VK_OBJECT_TYPE_DESCRIPTOR_POOL, m_descriptorPool, "ImGui Descriptor Pool");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    applyInspectorStyle();

    if (!ImGui_ImplGlfw_InitForVulkan(window.getHandle(), true)) {
        rollbackInit(false, false, true);
        throw std::runtime_error("Failed to initialize ImGui GLFW backend!");
    }

    const uint32_t effectiveImageCount = (imageCount >= 2u) ? imageCount : 2u;

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &m_colorFormat;
    renderingInfo.depthAttachmentFormat = depthFormat;
    renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = context.getInstance();
    initInfo.PhysicalDevice = context.getPhysicalDevice();
    initInfo.Device = m_device;
    initInfo.QueueFamily = queueFamilies.graphicsFamily.value();
    initInfo.Queue = context.getGraphicsQueue();
    initInfo.DescriptorPool = m_descriptorPool;
    initInfo.RenderPass = VK_NULL_HANDLE;
    initInfo.MinImageCount = effectiveImageCount;
    initInfo.ImageCount = effectiveImageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineRenderingCreateInfo = renderingInfo;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        rollbackInit(false, true, true);
        throw std::runtime_error("Failed to initialize ImGui Vulkan backend!");
    }

    if (!ImGui_ImplVulkan_CreateFontsTexture()) {
        rollbackInit(true, true, true);
        throw std::runtime_error("Failed to create ImGui Vulkan font texture!");
    }

    m_initialized = true;
}

ImGuiLayer::~ImGuiLayer() {
    cleanup();
}

void ImGuiLayer::cleanup() noexcept {
    if (!m_initialized) {
        return;
    }

    if (m_device != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(m_device);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui::DestroyContext();
    }

    if (m_device != VK_NULL_HANDLE && m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    m_device = VK_NULL_HANDLE;
    m_colorFormat = VK_FORMAT_UNDEFINED;
    m_initialized = false;
}

void ImGuiLayer::beginFrame() {
    if (!m_initialized) {
        return;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::drawBackendInspector(InspectorState& state) {
    if (!m_initialized) {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(370.0f, 0.0f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Vulkan Backend Inspector")) {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Hardware & Backend", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* gpuName = (state.deviceName != nullptr) ? state.deviceName : "Unknown GPU";
        const char* presentMode = (state.presentModeName != nullptr) ? state.presentModeName : "FIFO";
        const uint32_t apiMajor = VK_API_VERSION_MAJOR(state.apiVersion);
        const uint32_t apiMinor = VK_API_VERSION_MINOR(state.apiVersion);
        const uint32_t apiPatch = VK_API_VERSION_PATCH(state.apiVersion);

        ImGui::Text("GPU: %s", gpuName);
        ImGui::Text("Vulkan API: %u.%u.%u", apiMajor, apiMinor, apiPatch);
        ImGui::Text("Present Mode: %s", presentMode);
        ImGui::Text("Swapchain Extent: %u x %u", state.swapchainWidth, state.swapchainHeight);

        const ImVec4 activeColor(0.32f, 0.85f, 0.48f, 1.0f);
        const ImVec4 fallbackColor(0.92f, 0.60f, 0.25f, 1.0f);

        ImGui::TextUnformatted("Push Descriptors:");
        ImGui::SameLine();
        ImGui::TextColored(
            state.pushDescriptorsEnabled ? activeColor : fallbackColor,
            "%s",
            state.pushDescriptorsEnabled ? "ENABLED" : "FALLBACK");

        ImGui::TextUnformatted("Wireframe Support:");
        ImGui::SameLine();
        ImGui::TextColored(
            state.wireframeSupported ? activeColor : fallbackColor,
            "%s",
            state.wireframeSupported ? "SUPPORTED" : "UNSUPPORTED");
    }

    if (ImGui::CollapsingHeader("Performance & GPU Timings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("FPS: %.1f", static_cast<double>(state.fps));
        ImGui::Text("CPU Frame: %.3f ms", static_cast<double>(state.cpuFrameMs));
        ImGui::Text("GPU Frame: %.3f ms", static_cast<double>(state.gpuFrameMs));
        ImGui::Text("Shadow Pass (ms): %.3f", static_cast<double>(state.shadowPassMs));
        ImGui::Text("Main Pass (ms): %.3f", static_cast<double>(state.mainPassMs));
        ImGui::Text("Draw Calls: %u", state.drawCalls);
        ImGui::Text("Rendered Triangles: %u", state.renderedTriangles);
    }

    if (ImGui::CollapsingHeader("VMA Memory Telemetry", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text(
            "Device-Local: %.2f MB (%u allocs)",
            static_cast<double>(state.deviceLocalMB),
            state.deviceLocalAllocations);
        ImGui::Text(
            "Host-Visible: %.2f MB (%u allocs)",
            static_cast<double>(state.hostVisibleMB),
            state.hostVisibleAllocations);
        ImGui::Text(
            "Total Reserved: %.2f MB (%u blocks)",
            static_cast<double>(state.totalReservedBlockMB),
            state.totalBlocks);
    }

    if (ImGui::CollapsingHeader("Lighting & Water", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Sun Azimuth", &state.sunAzimuthDeg, -180.0f, 180.0f, "%.1f deg");
        ImGui::SliderFloat("Sun Elevation", &state.sunElevationDeg, 8.0f, 85.0f, "%.1f deg");
        ImGui::SliderFloat("Sun Intensity", &state.sunIntensity, 0.2f, 4.0f, "%.2f");
        ImGui::SliderFloat("Ambient Strength", &state.ambientStrength, 0.05f, 1.5f, "%.2f");
        ImGui::SliderFloat("Wave Speed", &state.waterWaveSpeed, 0.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("Wave Height", &state.waterWaveHeight, 0.0f, 0.45f, "%.2f m");
    }

    if (ImGui::CollapsingHeader("Camera & Debug Views", ImGuiTreeNodeFlags_DefaultOpen)) {
        int cameraMode = (state.cameraMode == CameraMode::Fly) ? 1 : 0;
        ImGui::TextUnformatted("Camera Mode:");
        ImGui::SameLine();
        if (ImGui::RadioButton("Orbit", &cameraMode, 0)) {
            state.cameraMode = CameraMode::Orbit;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Fly", &cameraMode, 1)) {
            state.cameraMode = CameraMode::Fly;
        }
        ImGui::TextDisabled("Hints: WASD+RMB look, C toggle Orbit/Fly");

        ImGui::BeginDisabled(!state.wireframeSupported);
        ImGui::Checkbox("Wireframe", &state.wireframe);
        ImGui::EndDisabled();

        static constexpr std::array<const char*, 4> kDebugViews = {
            "0: Final Lit",
            "1: Flat Normals RGB",
            "2: Shadow Factor",
            "3: Depth / Elevation"
        };

        int debugView = std::clamp(
            static_cast<int>(state.debugView),
            0,
            static_cast<int>(kDebugViews.size()) - 1);
        if (ImGui::Combo("Debug View", &debugView, kDebugViews.data(), static_cast<int>(kDebugViews.size()))) {
            state.debugView = static_cast<int32_t>(debugView);
        }
    }

    ImGui::End();
}

void ImGuiLayer::render(VkCommandBuffer cmd) {
    if (!m_initialized) {
        return;
    }

    ImGui::Render();
    if (cmd != VK_NULL_HANDLE) {
        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData != nullptr) {
            ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
        }
    }
}

bool ImGuiLayer::wantsCaptureMouse() const noexcept {
    if (!m_initialized || ImGui::GetCurrentContext() == nullptr) {
        return false;
    }

    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::wantsCaptureKeyboard() const noexcept {
    if (!m_initialized || ImGui::GetCurrentContext() == nullptr) {
        return false;
    }

    return ImGui::GetIO().WantCaptureKeyboard;
}
