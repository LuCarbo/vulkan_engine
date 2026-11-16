#include "core/Pipeline.hpp"
#include "core/Context.hpp"
#include "core/DebugUtils.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

VkVertexInputBindingDescription Vertex::getBindingDescription() noexcept {
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

std::array<VkVertexInputAttributeDescription, 3> Vertex::getAttributeDescriptions() noexcept {
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, color);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, uv);

    return attributeDescriptions;
}

Pipeline::Pipeline(
    const Context& context,
    VkFormat colorFormat,
    VkFormat depthFormat,
    std::string_view vertSpvPath,
    std::string_view fragSpvPath,
    const PipelineConfig& config)
    : m_device(context.getDevice()) {

    // 1. Caricamento bytecode SPIR-V
    auto vertShaderCode = readFile(vertSpvPath);
    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = VK_NULL_HANDLE;

    auto destroyShaderModules = [&]() noexcept {
        if (fragShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
            fragShaderModule = VK_NULL_HANDLE;
        }
        if (vertShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
            vertShaderModule = VK_NULL_HANDLE;
        }
    };

    const bool hasFragmentShader = !fragSpvPath.empty();
    if (hasFragmentShader) {
        try {
            auto fragShaderCode = readFile(fragSpvPath);
            fragShaderModule = createShaderModule(fragShaderCode);
        } catch (...) {
            destroyShaderModules();
            throw;
        }
    }

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    if (hasFragmentShader) {
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";
    }

    const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
        vertShaderStageInfo,
        fragShaderStageInfo
    };

    // 2. Stati dinamici (Viewport e Scissor dinamici: nessuna ricreazione pipeline al resize)
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    if (config.enableDepthBias) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
    }

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // 3. Vertex Input
    const auto bindingDescription = Vertex::getBindingDescription();
    const auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (config.enableVertexInput) {
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
    } else {
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;
    }

    // 4. Input Assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 5. Viewport State (Dinamico: count = 1, puntatori null)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // 6. Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = config.cullMode;
    rasterizer.frontFace = config.frontFace;
    if (config.enableDepthBias) {
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = config.depthBiasConstantFactor;
        rasterizer.depthBiasSlopeFactor = config.depthBiasSlopeFactor;
    } else {
        rasterizer.depthBiasEnable = VK_FALSE;
    }

    // 7. Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 8. Depth Stencil State
    const bool hasDepth = (depthFormat != VK_FORMAT_UNDEFINED);
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = (hasDepth && config.enableDepthTest) ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = (hasDepth && config.enableDepthWrite) ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = config.depthCompareOp;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 9. Color Blending (1 target o 0 per passate depth-only)
    const bool hasColorAttachment = (colorFormat != VK_FORMAT_UNDEFINED);

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = hasColorAttachment ? 1u : 0u;
    colorBlending.pAttachments = hasColorAttachment ? &colorBlendAttachment : nullptr;

    // 10. Descriptor Set Layout (Push Descriptors su Vulkan 1.4)
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(config.includeShadowSamplerBinding ? 2u : 1u);

    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.push_back(uboLayoutBinding);

    if (config.includeShadowSamplerBinding) {
        VkDescriptorSetLayoutBinding shadowSamplerBinding{};
        shadowSamplerBinding.binding = 1;
        shadowSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        shadowSamplerBinding.descriptorCount = 1;
        shadowSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(shadowSamplerBinding);
    }

    VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
    setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setLayoutInfo.flags = context.hasPushDescriptors()
        ? static_cast<VkDescriptorSetLayoutCreateFlags>(VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT)
        : VkDescriptorSetLayoutCreateFlags{0};
    setLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    setLayoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_device, &setLayoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        destroyShaderModules();
        cleanup();
        throw std::runtime_error("Impossibile creare il Descriptor Set Layout!");
    }

    const std::string baseName = (config.debugName != nullptr) ? config.debugName : "Graphics Pipeline";
    const std::string setLayoutName = baseName + " SetLayout";
    DebugUtils::setObjectName(m_device, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, m_descriptorSetLayout, setLayoutName.c_str());

    // 11. Pipeline Layout (DescriptorSetLayout + PushConstants)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = static_cast<uint32_t>(sizeof(PushConstants));

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        destroyShaderModules();
        cleanup();
        throw std::runtime_error("Impossibile creare il Pipeline Layout!");
    }

    const std::string layoutName = baseName + " Layout";
    DebugUtils::setObjectName(m_device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, m_pipelineLayout, layoutName.c_str());

    // 12. Configurazione Dynamic Rendering (Vulkan 1.4: NO RenderPass, NO Framebuffer)
    VkPipelineRenderingCreateInfo renderingCreateInfo{};
    renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingCreateInfo.colorAttachmentCount = hasColorAttachment ? 1u : 0u;
    renderingCreateInfo.pColorAttachmentFormats = hasColorAttachment ? &colorFormat : nullptr;
    renderingCreateInfo.depthAttachmentFormat = depthFormat;
    renderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    // 13. Creazione effettiva della Graphics Pipeline (Fill)
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingCreateInfo;
    pipelineInfo.stageCount = hasFragmentShader ? 2u : 1u;
    pipelineInfo.pStages = hasFragmentShader ? shaderStages.data() : &vertShaderStageInfo;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        destroyShaderModules();
        cleanup();
        throw std::runtime_error("Impossibile creare la Graphics Pipeline Vulkan!");
    }

    DebugUtils::setObjectName(m_device, VK_OBJECT_TYPE_PIPELINE, m_pipeline, baseName.c_str());

    // 14. Variante Wireframe opzionale (se fillModeNonSolid è supportato dall'hardware)
    if (config.enableWireframeVariant && context.hasNonSolidFill()) {
        VkPipelineRasterizationStateCreateInfo wireRasterizer = rasterizer;
        wireRasterizer.polygonMode = VK_POLYGON_MODE_LINE;
        wireRasterizer.cullMode = VK_CULL_MODE_NONE;

        VkGraphicsPipelineCreateInfo wirePipelineInfo = pipelineInfo;
        wirePipelineInfo.pRasterizationState = &wireRasterizer;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &wirePipelineInfo, nullptr, &m_wireframePipeline) != VK_SUCCESS) {
            destroyShaderModules();
            cleanup();
            throw std::runtime_error("Impossibile creare la Graphics Pipeline Wireframe Vulkan!");
        }

        const std::string wireName = baseName + " (Wireframe)";
        DebugUtils::setObjectName(m_device, VK_OBJECT_TYPE_PIPELINE, m_wireframePipeline, wireName.c_str());
    }

    destroyShaderModules();

    std::cout << "[OK] Graphics Pipeline creata con Dynamic Rendering nativo"
              << (m_wireframePipeline != VK_NULL_HANDLE ? " (+ Wireframe)." : ".") << std::endl;
}

Pipeline::~Pipeline() {
    cleanup();
}

Pipeline::Pipeline(Pipeline&& other) noexcept
    : m_device(other.m_device),
      m_descriptorSetLayout(other.m_descriptorSetLayout),
      m_pipelineLayout(other.m_pipelineLayout),
      m_pipeline(other.m_pipeline),
      m_wireframePipeline(other.m_wireframePipeline) {
    other.m_device = VK_NULL_HANDLE;
    other.m_descriptorSetLayout = VK_NULL_HANDLE;
    other.m_pipelineLayout = VK_NULL_HANDLE;
    other.m_pipeline = VK_NULL_HANDLE;
    other.m_wireframePipeline = VK_NULL_HANDLE;
}

Pipeline& Pipeline::operator=(Pipeline&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_device = other.m_device;
        m_descriptorSetLayout = other.m_descriptorSetLayout;
        m_pipelineLayout = other.m_pipelineLayout;
        m_pipeline = other.m_pipeline;
        m_wireframePipeline = other.m_wireframePipeline;

        other.m_device = VK_NULL_HANDLE;
        other.m_descriptorSetLayout = VK_NULL_HANDLE;
        other.m_pipelineLayout = VK_NULL_HANDLE;
        other.m_pipeline = VK_NULL_HANDLE;
        other.m_wireframePipeline = VK_NULL_HANDLE;
    }
    return *this;
}

void Pipeline::cleanup() noexcept {
    if (m_device == VK_NULL_HANDLE) {
        return;
    }

    if (m_wireframePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_wireframePipeline, nullptr);
        m_wireframePipeline = VK_NULL_HANDLE;
    }
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }

    m_device = VK_NULL_HANDLE;
}

void Pipeline::bind(VkCommandBuffer cmd, bool wireframe) const noexcept {
    const VkPipeline targetPipeline = (wireframe && m_wireframePipeline != VK_NULL_HANDLE)
        ? m_wireframePipeline
        : m_pipeline;
    if (targetPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, targetPipeline);
    }
}

std::vector<char> Pipeline::readFile(std::string_view filePath) {
    std::filesystem::path path(filePath);
    if (!std::filesystem::exists(path)) {
        if (std::filesystem::exists("shaders" / path.filename())) {
            path = "shaders" / path.filename();
        } else if (std::filesystem::exists(path.filename())) {
            path = path.filename();
        }
    }

    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Impossibile aprire il file shader SPIR-V: " + std::string(filePath));
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
    file.close();

    return buffer;
}

VkShaderModule Pipeline::createShaderModule(const std::vector<char>& code) const {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Impossibile creare il modulo shader Vulkan!");
    }

    return shaderModule;
}
