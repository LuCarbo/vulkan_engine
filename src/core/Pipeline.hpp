#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class Context;

/**
 * @brief Vertice 3D compatto per il motore Low-Poly (posizione, colore per-vertice, UV).
 */
struct Vertex {
    glm::vec3 pos{0.0f};
    glm::vec3 color{1.0f};
    glm::vec2 uv{0.0f};

    [[nodiscard]] static VkVertexInputBindingDescription getBindingDescription() noexcept;
    [[nodiscard]] static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() noexcept;
};

/**
 * @brief Push constants per singola draw call (matrice model + debug/material mode).
 */
struct alignas(16) PushConstants {
    glm::mat4 model{1.0f};
    int32_t debugView{0};
    int32_t materialType{0}; // 0 = Solid Terrain/Prop, 1 = Animated Water, 2 = Skybox Dome
    float pad0{0.0f};
    float pad1{0.0f};
};

/**
 * @brief Uniform Buffer globale per frame (camere, luci, parametri acqua).
 */
struct alignas(16) FrameUBO {
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::mat4 viewProj{1.0f};
    glm::mat4 lightSpaceMatrix{1.0f};
    glm::vec4 sunDirectionIntensity{0.45f, 0.72f, 0.52f, 1.85f};
    glm::vec4 sunColorAmbient{1.0f, 0.95f, 0.86f, 0.55f};
    glm::vec4 cameraPosTime{0.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 waterParams{1.6f, 0.14f, -0.25f, 0.0f};
};

/**
 * @brief Parametri di configurazione per la creazione modulare delle Graphics Pipeline.
 */
struct PipelineConfig {
    bool enableVertexInput{true};
    bool enableDepthTest{true};
    bool enableDepthWrite{true};
    bool enableWireframeVariant{true};
    VkCullModeFlags cullMode{VK_CULL_MODE_BACK_BIT};
    VkFrontFace frontFace{VK_FRONT_FACE_COUNTER_CLOCKWISE};
    VkCompareOp depthCompareOp{VK_COMPARE_OP_LESS};
    const char* debugName{"Main Graphics Pipeline"};
    bool enableDepthBias{false};
    float depthBiasConstantFactor{1.25f};
    float depthBiasSlopeFactor{1.75f};
    bool includeShadowSamplerBinding{true};
};

/**
 * @brief Gestisce la Pipeline grafica Vulkan configurata per il Dynamic Rendering nativo,
 * con supporto Push Descriptors (Vulkan 1.4), Push Constants e variante Wireframe.
 */
class Pipeline {
public:
    Pipeline() = default;
    Pipeline(
        const Context& context,
        VkFormat colorFormat,
        VkFormat depthFormat,
        std::string_view vertSpvPath,
        std::string_view fragSpvPath,
        const PipelineConfig& config = {});
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    Pipeline(Pipeline&& other) noexcept;
    Pipeline& operator=(Pipeline&& other) noexcept;

    void cleanup() noexcept;

    void bind(VkCommandBuffer cmd, bool wireframe = false) const noexcept;

    [[nodiscard]] VkPipeline getHandle() const noexcept { return m_pipeline; }
    [[nodiscard]] VkPipeline getWireframeHandle() const noexcept { return m_wireframePipeline; }
    [[nodiscard]] VkPipelineLayout getLayout() const noexcept { return m_pipelineLayout; }
    [[nodiscard]] VkDescriptorSetLayout getDescriptorSetLayout() const noexcept { return m_descriptorSetLayout; }

private:
    VkDevice m_device{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_descriptorSetLayout{VK_NULL_HANDLE};
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};
    VkPipeline m_wireframePipeline{VK_NULL_HANDLE};

    static std::vector<char> readFile(std::string_view filePath);
    VkShaderModule createShaderModule(const std::vector<char>& code) const;
};
