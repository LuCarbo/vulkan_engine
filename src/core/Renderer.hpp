#pragma once

#include "core/Buffer.hpp"
#include "core/Image.hpp"
#include "core/ImGuiLayer.hpp"
#include "core/Pipeline.hpp"
#include "scene/Camera.hpp"
#include "scene/Mesh.hpp"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

class Context;
class Window;
class Swapchain;

/**
 * @brief Categoria geometrica per le istanze sceniche nel mondo 3D.
 */
enum class PropType : uint8_t {
    PineTree = 0,
    GoldTree,
    CrimsonTree,
    ShoreBush,
    Rock,
    StoneSanctuary,
    RuinedPillar
};

/**
 * @brief Istanza di un oggetto scenico posizionato sul terreno o nel lago.
 */
struct PropInstance {
    glm::mat4 model{1.0f};
    PropType type{PropType::PineTree};
};

/**
 * @brief Orchestratore del rendering 3D: gestisce i command buffer,
 * Frames-in-Flight con Synchronization2, Shadow Mapping multi-pass,
 * Push Descriptors / Push Constants, Timestamp Queries, VMA Telemetry e Dear ImGui.
 */
class Renderer {
public:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    static constexpr uint32_t SHADOW_MAP_DIM = 2048;
    static constexpr uint32_t QUERY_COUNT_PER_FRAME = 4;

    Renderer(const Context& context, Window& window, Swapchain& swapchain, const Pipeline& pipeline);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    void renderFrame();

private:
    const Context& m_context;
    Window& m_window;
    Swapchain& m_swapchain;
    const Pipeline& m_pipeline;

    VkCommandPool m_commandPool{VK_NULL_HANDLE};
    std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> m_commandBuffers{};

    // Sincronizzazione GPU-CPU e GPU-GPU
    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_imageAvailableSemaphores{};
    std::array<VkFence, MAX_FRAMES_IN_FLIGHT> m_inFlightFences{};
    std::vector<VkSemaphore> m_renderFinishedSemaphores{};

    // Timestamp Query Pools per misurare Shadow Pass & Main Pass ms sulla GPU
    std::array<VkQueryPool, MAX_FRAMES_IN_FLIGHT> m_timestampQueryPools{};
    std::array<bool, MAX_FRAMES_IN_FLIGHT> m_queryPoolHasData{false, false};

    // Risorse Shadow Mapping (Pass 1)
    Image m_shadowMapImage;
    VkSampler m_shadowSampler{VK_NULL_HANDLE};
    Pipeline m_shadowPipeline;

    // Risorse Offscreen PSX Low-Resolution Pass
    Image m_psxColorImage;
    Image m_psxDepthImage;
    VkExtent2D m_psxExtent{320, 240};

    // Risorse per frame & fallback descriptor (se pushDescriptor non supportato)
    std::array<Buffer, MAX_FRAMES_IN_FLIGHT> m_uboBuffers{};
    VkDescriptorPool m_fallbackDescriptorPool{VK_NULL_HANDLE};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_fallbackDescriptorSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_fallbackShadowDescriptorSets{};

    // Scena 3D Low-Poly
    Camera m_camera{55.0f, 0.2f, 600.0f};
    Mesh m_skyDomeMesh;
    Mesh m_terrainMesh;
    Mesh m_waterMesh;
    Mesh m_treeMesh;
    Mesh m_rockMesh;
    Mesh m_goldTreeMesh;
    Mesh m_crimsonTreeMesh;
    Mesh m_bushMesh;
    Mesh m_sanctuaryMesh;
    Mesh m_pillarMesh;
    std::vector<PropInstance> m_propInstances;

    // Dear ImGui Telemetry & Inspector
    std::unique_ptr<ImGuiLayer> m_imguiLayer;
    InspectorState m_inspectorState{};

    std::chrono::steady_clock::time_point m_startTime{};
    std::chrono::steady_clock::time_point m_lastFrameTime{};

    float m_fpsSmoothed{0.0f};
    float m_cpuFrameMsSmoothed{0.0f};
    float m_gpuFrameMs{0.0f};
    float m_shadowPassMs{0.0f};
    float m_mainPassMs{0.0f};
    uint32_t m_lastDrawCalls{0};
    uint32_t m_lastRenderedTriangles{0};

    bool m_keyFPressed{false};
    bool m_keyCPressed{false};
    bool m_keyPPressed{false};

    uint32_t m_currentFrame{0};

    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void createQueryPools();
    void destroyQueryPools() noexcept;
    void createShadowResources();
    void createPsxResources(VkExtent2D extent);
    [[nodiscard]] VkExtent2D resolvePsxExtent() const noexcept;
    void createSceneResources();
    void populateProps();
    [[nodiscard]] uint32_t drawPropMesh(VkCommandBuffer cmd, PropType type) const noexcept;
    void processInput();
    void readTimestampQueries(uint32_t frameIndex);
    void updateVmaStats();
    void updateInspectorState(float deltaTime);
    void updateFrameUBO(float elapsedTime);
    void recreateSwapchain();
    void cleanupSyncObjects();
};
