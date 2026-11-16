#include "core/Renderer.hpp"
#include "core/Context.hpp"
#include "core/Window.hpp"
#include "core/Swapchain.hpp"
#include "core/DebugUtils.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

Renderer::Renderer(const Context& context, Window& window, Swapchain& swapchain, const Pipeline& pipeline)
    : m_context(context), m_window(window), m_swapchain(swapchain), m_pipeline(pipeline) {
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
    createQueryPools();
    createShadowResources();
    createPsxResources({ 320, 240 });
    createSceneResources();

    m_imguiLayer = std::make_unique<ImGuiLayer>(
        m_context,
        m_window,
        m_swapchain.getImageFormat(),
        m_swapchain.getDepthFormat(),
        static_cast<uint32_t>(m_swapchain.getImageCount())
    );

    const auto& props = m_context.getDeviceProperties();
    m_inspectorState.deviceName = props.deviceName;
    m_inspectorState.apiVersion = props.apiVersion;
    m_inspectorState.pushDescriptorsEnabled = m_context.hasPushDescriptors();
    m_inspectorState.wireframeSupported = m_context.hasNonSolidFill();

    m_startTime = std::chrono::steady_clock::now();
    m_lastFrameTime = m_startTime;

    std::cout << "[OK] Renderer 3D Low-Poly inizializzato (Frames in Flight: " << MAX_FRAMES_IN_FLIGHT
              << ", Shadow Map: " << SHADOW_MAP_DIM << "x" << SHADOW_MAP_DIM
              << ", ImGui Dynamic Rendering)." << std::endl;
    std::cout << "[CONTROLLI] 1-4: Debug View | F: Wireframe | C: Orbit/Fly | P: PSX Mode | WASD+RMB: Muovi Camera" << std::endl;
}

Renderer::~Renderer() {
    m_context.waitIdle();

    if (m_imguiLayer) {
        m_imguiLayer->cleanup();
        m_imguiLayer.reset();
    }

    m_pillarMesh.cleanup();
    m_sanctuaryMesh.cleanup();
    m_bushMesh.cleanup();
    m_crimsonTreeMesh.cleanup();
    m_goldTreeMesh.cleanup();
    m_rockMesh.cleanup();
    m_treeMesh.cleanup();
    m_waterMesh.cleanup();
    m_terrainMesh.cleanup();
    m_skyDomeMesh.cleanup();

    for (auto& ubo : m_uboBuffers) {
        ubo.cleanup();
    }

    m_shadowPipeline.cleanup();
    if (m_shadowSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_context.getDevice(), m_shadowSampler, nullptr);
        m_shadowSampler = VK_NULL_HANDLE;
    }
    m_shadowMapImage.cleanup();
    m_psxDepthImage.cleanup();
    m_psxColorImage.cleanup();

    if (m_fallbackDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_context.getDevice(), m_fallbackDescriptorPool, nullptr);
        m_fallbackDescriptorPool = VK_NULL_HANDLE;
    }

    destroyQueryPools();
    cleanupSyncObjects();

    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_context.getDevice(), m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }

    std::cout << "[INFO] Renderer deallocato correttamente." << std::endl;
}

void Renderer::createCommandPool() {
    auto queueFamilies = m_context.getQueueFamilies();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilies.graphicsFamily.value();

    if (vkCreateCommandPool(m_context.getDevice(), &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Impossibile creare il Command Pool!");
    }

    DebugUtils::setObjectName(m_context.getDevice(), VK_OBJECT_TYPE_COMMAND_POOL, m_commandPool, "Renderer Command Pool");
}

void Renderer::createCommandBuffers() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    if (vkAllocateCommandBuffers(m_context.getDevice(), &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Impossibile allocare i Command Buffer!");
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        const std::string cmdName = "Frame Command Buffer " + std::to_string(i);
        DebugUtils::setObjectName(m_context.getDevice(), VK_OBJECT_TYPE_COMMAND_BUFFER, m_commandBuffers[i], cmdName.c_str());
    }
}

void Renderer::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(m_context.getDevice(), &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_context.getDevice(), &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Impossibile creare gli oggetti di sincronizzazione per i frame in volo!");
        }

        const std::string semName = "ImageAvailable Semaphore " + std::to_string(i);
        const std::string fenceName = "InFlight Fence " + std::to_string(i);
        DebugUtils::setObjectName(m_context.getDevice(), VK_OBJECT_TYPE_SEMAPHORE, m_imageAvailableSemaphores[i], semName.c_str());
        DebugUtils::setObjectName(m_context.getDevice(), VK_OBJECT_TYPE_FENCE, m_inFlightFences[i], fenceName.c_str());
    }

    m_renderFinishedSemaphores.resize(m_swapchain.getImageCount());
    for (size_t i = 0; i < m_swapchain.getImageCount(); ++i) {
        if (vkCreateSemaphore(m_context.getDevice(), &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("Impossibile creare i semafori renderFinished!");
        }
        const std::string semName = "RenderFinished Semaphore " + std::to_string(i);
        DebugUtils::setObjectName(m_context.getDevice(), VK_OBJECT_TYPE_SEMAPHORE, m_renderFinishedSemaphores[i], semName.c_str());
    }
}

void Renderer::createQueryPools() {
    if (m_context.getDeviceProperties().limits.timestampPeriod <= 0.0f) {
        return;
    }

    VkQueryPoolCreateInfo queryPoolInfo{};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = QUERY_COUNT_PER_FRAME;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateQueryPool(m_context.getDevice(), &queryPoolInfo, nullptr, &m_timestampQueryPools[i]) != VK_SUCCESS) {
            throw std::runtime_error("Impossibile creare il VkQueryPool per i timestamp GPU!");
        }
        const std::string poolName = "GPU Timestamp QueryPool " + std::to_string(i);
        DebugUtils::setObjectName(m_context.getDevice(), VK_OBJECT_TYPE_QUERY_POOL, m_timestampQueryPools[i], poolName.c_str());
        m_queryPoolHasData[i] = false;
    }
}

void Renderer::destroyQueryPools() noexcept {
    VkDevice device = m_context.getDevice();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (m_timestampQueryPools[i] != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, m_timestampQueryPools[i], nullptr);
            m_timestampQueryPools[i] = VK_NULL_HANDLE;
        }
        m_queryPoolHasData[i] = false;
    }
}

void Renderer::createShadowResources() {
    m_shadowMapImage = Image(
        m_context,
        { SHADOW_MAP_DIM, SHADOW_MAP_DIM },
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        "Directional Shadow Map"
    );

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    if (vkCreateSampler(m_context.getDevice(), &samplerInfo, nullptr, &m_shadowSampler) != VK_SUCCESS) {
        throw std::runtime_error("Impossibile creare il Shadow Map Comparison Sampler!");
    }

    DebugUtils::setObjectName(
        m_context.getDevice(),
        VK_OBJECT_TYPE_SAMPLER,
        m_shadowSampler,
        "Shadow Map Comparison Sampler"
    );

    PipelineConfig shadowConfig{};
    shadowConfig.enableVertexInput = true;
    shadowConfig.enableDepthTest = true;
    shadowConfig.enableDepthWrite = true;
    shadowConfig.enableWireframeVariant = false;
    shadowConfig.enableDepthBias = true;
    shadowConfig.depthBiasConstantFactor = 1.25f;
    shadowConfig.depthBiasSlopeFactor = 1.75f;
    shadowConfig.includeShadowSamplerBinding = false;
    shadowConfig.cullMode = VK_CULL_MODE_BACK_BIT;
    shadowConfig.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    shadowConfig.depthCompareOp = VK_COMPARE_OP_LESS;
    shadowConfig.debugName = "Shadow Depth Pipeline";

    m_shadowPipeline = Pipeline(
        m_context,
        VK_FORMAT_UNDEFINED,
        VK_FORMAT_D32_SFLOAT,
        "shaders/shadow.vert.spv",
        "",
        shadowConfig
    );
}

VkExtent2D Renderer::resolvePsxExtent() const noexcept {
    if (!m_inspectorState.psxEnabled) {
        return m_swapchain.getExtent();
    }

    switch (m_inspectorState.psxResolutionPreset) {
        case 0:
            return { 256u, 224u };
        case 1:
            return { 320u, 240u };
        case 2:
            return { 512u, 384u };
        case 3:
        default:
            return m_swapchain.getExtent();
    }
}

void Renderer::createPsxResources(VkExtent2D extent) {
    extent.width = (std::max)(extent.width, 1u);
    extent.height = (std::max)(extent.height, 1u);

    m_psxDepthImage.cleanup();
    m_psxColorImage.cleanup();

    m_psxColorImage = Image(
        m_context,
        extent,
        m_swapchain.getImageFormat(),
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        "PSX Offscreen Color"
    );

    m_psxDepthImage = Image(
        m_context,
        extent,
        m_swapchain.getDepthFormat(),
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        "PSX Offscreen Depth"
    );

    m_psxExtent = extent;
}

void Renderer::createSceneResources() {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        const std::string uboName = "Frame UBO Buffer " + std::to_string(i);
        m_uboBuffers[i] = Buffer(
            m_context,
            sizeof(FrameUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            uboName.c_str()
        );
    }

    if (!m_context.hasPushDescriptors()) {
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT * 2;

        if (vkCreateDescriptorPool(m_context.getDevice(), &poolInfo, nullptr, &m_fallbackDescriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("Impossibile creare il Descriptor Pool di fallback!");
        }

        std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> mainLayouts{};
        mainLayouts.fill(m_pipeline.getDescriptorSetLayout());

        VkDescriptorSetAllocateInfo mainAllocInfo{};
        mainAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        mainAllocInfo.descriptorPool = m_fallbackDescriptorPool;
        mainAllocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
        mainAllocInfo.pSetLayouts = mainLayouts.data();

        if (vkAllocateDescriptorSets(m_context.getDevice(), &mainAllocInfo, m_fallbackDescriptorSets.data()) != VK_SUCCESS) {
            throw std::runtime_error("Impossibile allocare i Descriptor Set principali di fallback!");
        }

        std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> shadowLayouts{};
        shadowLayouts.fill(m_shadowPipeline.getDescriptorSetLayout());

        VkDescriptorSetAllocateInfo shadowAllocInfo{};
        shadowAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        shadowAllocInfo.descriptorPool = m_fallbackDescriptorPool;
        shadowAllocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
        shadowAllocInfo.pSetLayouts = shadowLayouts.data();

        if (vkAllocateDescriptorSets(m_context.getDevice(), &shadowAllocInfo, m_fallbackShadowDescriptorSets.data()) != VK_SUCCESS) {
            throw std::runtime_error("Impossibile allocare i Descriptor Set Shadow di fallback!");
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = m_uboBuffers[i].getHandle();
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(FrameUBO);

            VkDescriptorImageInfo imageInfo{};
            imageInfo.sampler = m_shadowSampler;
            imageInfo.imageView = m_shadowMapImage.getView();
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            std::array<VkWriteDescriptorSet, 3> writes{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = m_fallbackDescriptorSets[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &bufferInfo;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = m_fallbackDescriptorSets[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &imageInfo;

            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = m_fallbackShadowDescriptorSets[i];
            writes[2].dstBinding = 0;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[2].descriptorCount = 1;
            writes[2].pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(m_context.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    m_skyDomeMesh = Mesh::createSkyDome(m_context, 350.0f);
    m_terrainMesh = Mesh::createTerrain(m_context, 120, 90.0f);
    m_waterMesh = Mesh::createWaterPlane(m_context, 90, 90.0f, -0.25f);
    m_treeMesh = Mesh::loadFromObj(m_context, "assets/pine_tree.obj", { 0.14f, 0.45f, 0.20f });
    m_rockMesh = Mesh::loadFromObj(m_context, "assets/rock.obj", { 0.50f, 0.49f, 0.46f });
    m_goldTreeMesh = Mesh::createBroadleafTree(m_context, { 0.88f, 0.62f, 0.14f }, "Golden Autumn Tree Mesh");
    m_crimsonTreeMesh = Mesh::createBroadleafTree(m_context, { 0.78f, 0.25f, 0.16f }, "Crimson Maple Tree Mesh");
    m_bushMesh = Mesh::createBushAndReeds(m_context);
    m_sanctuaryMesh = Mesh::createStoneRuins(m_context);
    m_pillarMesh = Mesh::createRuinedPillar(m_context);

    populateProps();
}

uint32_t Renderer::drawPropMesh(VkCommandBuffer cmd, PropType type) const noexcept {
    switch (type) {
        case PropType::GoldTree:
            m_goldTreeMesh.bindAndDraw(cmd);
            return m_goldTreeMesh.getTriangleCount();
        case PropType::CrimsonTree:
            m_crimsonTreeMesh.bindAndDraw(cmd);
            return m_crimsonTreeMesh.getTriangleCount();
        case PropType::ShoreBush:
            m_bushMesh.bindAndDraw(cmd);
            return m_bushMesh.getTriangleCount();
        case PropType::Rock:
            m_rockMesh.bindAndDraw(cmd);
            return m_rockMesh.getTriangleCount();
        case PropType::StoneSanctuary:
            m_sanctuaryMesh.bindAndDraw(cmd);
            return m_sanctuaryMesh.getTriangleCount();
        case PropType::RuinedPillar:
            m_pillarMesh.bindAndDraw(cmd);
            return m_pillarMesh.getTriangleCount();
        case PropType::PineTree:
        default:
            m_treeMesh.bindAndDraw(cmd);
            return m_treeMesh.getTriangleCount();
    }
}

void Renderer::populateProps() {
    m_propInstances.clear();

    constexpr float waterLevel = -0.25f;
    constexpr float islandX = 1.5f;
    constexpr float islandZ = -1.0f;
    constexpr float overlookX = 16.5f;
    constexpr float overlookZ = 13.5f;

    // 1. Landmark: Central Lake Island Sanctuary
    {
        const float islandY = Mesh::sampleTerrainHeight(islandX, islandZ);
        glm::mat4 model(1.0f);
        model = glm::translate(model, glm::vec3(islandX, islandY - 0.12f, islandZ));
        model = glm::rotate(model, glm::radians(25.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        m_propInstances.push_back({ model, PropType::StoneSanctuary });

        const float guardX = islandX - 2.3f;
        const float guardZ = islandZ + 1.6f;
        glm::mat4 treeModel(1.0f);
        treeModel = glm::translate(treeModel, glm::vec3(guardX, Mesh::sampleTerrainHeight(guardX, guardZ) - 0.12f, guardZ));
        treeModel = glm::rotate(treeModel, glm::radians(40.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        treeModel = glm::scale(treeModel, glm::vec3(0.95f));
        m_propInstances.push_back({ treeModel, PropType::GoldTree });

        const std::array<glm::vec2, 3> islandReeds = {{
            { -0.9f, -2.8f },
            {  4.1f, -0.4f },
            {  2.6f,  1.4f }
        }};
        for (size_t i = 0; i < islandReeds.size(); ++i) {
            const float rx = islandReeds[i].x;
            const float rz = islandReeds[i].y;
            glm::mat4 reedModel(1.0f);
            reedModel = glm::translate(reedModel, glm::vec3(rx, Mesh::sampleTerrainHeight(rx, rz) - 0.08f, rz));
            reedModel = glm::rotate(reedModel, glm::radians(static_cast<float>(i) * 75.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            m_propInstances.push_back({ reedModel, PropType::ShoreBush });
        }
    }

    // 2. Landmark: Sunken Processional Pillars & Submerged Ruins in the Lake
    struct SunkenPillarSpec {
        float x;
        float z;
        float yawDeg;
        float pitchDeg;
        float rollDeg;
        float scale;
    };
    const std::array<SunkenPillarSpec, 10> lakePillars = {{
        // Causeway approach from outer shore (-12, 8) across shoal (-8.5, 5.5) toward island (1.5, -1.0)
        { -11.8f,  7.8f,  18.0f,  5.0f, -4.0f, 1.22f },
        {  -9.6f,  6.2f,  62.0f, -7.0f,  6.0f, 1.28f },
        {  -7.1f,  4.4f, 115.0f,  4.0f,  9.0f, 1.32f },
        {  -4.6f,  2.4f, 205.0f, -9.0f, -5.0f, 1.38f },
        // Sunken colonnade ring around the sanctuary island (~7.8m radius)
        {   8.6f, -3.5f,  35.0f,  8.0f, -6.0f, 1.34f },
        {   5.2f, -8.2f, 145.0f, -6.0f,  7.0f, 1.40f },
        {  -3.8f, -7.2f, 240.0f, 11.0f,  4.0f, 1.36f },
        {  -6.4f, -1.4f, 305.0f, -5.0f, -8.0f, 1.30f },
        {   3.8f,  6.5f,  85.0f,  6.0f, 10.0f, 1.42f },
        {   8.8f,  3.2f, 190.0f, -8.0f,  5.0f, 1.26f }
    }};

    for (const auto& spec : lakePillars) {
        const float terrainY = Mesh::sampleTerrainHeight(spec.x, spec.z);
        const float baseY = (std::max)(terrainY - 0.10f, -1.35f);
        glm::mat4 model(1.0f);
        model = glm::translate(model, glm::vec3(spec.x, baseY, spec.z));
        model = glm::rotate(model, glm::radians(spec.yawDeg), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(spec.pitchDeg), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(spec.rollDeg), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, glm::vec3(spec.scale));
        m_propInstances.push_back({ model, PropType::RuinedPillar });
    }

    // Half-submerged mossy boulders on the shallow ruin shoal
    const std::array<glm::vec3, 3> shoalRocks = {{
        { -8.5f, 5.5f, 1.35f },
        { -5.2f, 2.8f, 1.15f },
        {  6.2f, 4.9f, 1.25f }
    }};
    for (size_t i = 0; i < shoalRocks.size(); ++i) {
        const float rx = shoalRocks[i].x;
        const float rz = shoalRocks[i].y;
        const float rScale = shoalRocks[i].z;
        const float ry = (std::max)(Mesh::sampleTerrainHeight(rx, rz) - 0.12f, waterLevel - 0.55f);
        glm::mat4 model(1.0f);
        model = glm::translate(model, glm::vec3(rx, ry, rz));
        model = glm::rotate(model, glm::radians(static_cast<float>(i) * 110.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, glm::vec3(rScale, rScale * 0.85f, rScale));
        m_propInstances.push_back({ model, PropType::Rock });
    }

    // Secondary lakeside overlook sanctuary ruin
    {
        const float overlookY = Mesh::sampleTerrainHeight(overlookX, overlookZ);
        glm::mat4 model(1.0f);
        model = glm::translate(model, glm::vec3(overlookX, overlookY - 0.14f, overlookZ));
        model = glm::rotate(model, glm::radians(-130.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, glm::vec3(0.78f));
        m_propInstances.push_back({ model, PropType::StoneSanctuary });
    }

    // 3. Biome-driven scattering across Shoreline, Forest/Meadow & Alpine zones
    std::mt19937 rng(2026);
    std::uniform_real_distribution<float> jitterDist(-1.15f, 1.15f);
    std::uniform_real_distribution<float> yawDist(0.0f, 360.0f);
    std::uniform_real_distribution<float> tiltDist(-7.0f, 7.0f);
    std::uniform_real_distribution<float> treeScaleDist(0.85f, 1.45f);
    std::uniform_real_distribution<float> bushScaleDist(0.65f, 1.20f);
    std::uniform_real_distribution<float> rockScaleDist(0.65f, 1.65f);
    std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> speciesDist(0.0f, 1.0f);

    for (float gx = -38.0f; gx <= 38.0f; gx += 3.6f) {
        for (float gz = -38.0f; gz <= 38.0f; gz += 3.6f) {
            const float x = gx + jitterDist(rng);
            const float z = gz + jitterDist(rng);

            const float dxIso = x - islandX;
            const float dzIso = z - islandZ;
            if ((dxIso * dxIso + dzIso * dzIso) < (4.5f * 4.5f)) {
                continue;
            }

            const float dxOver = x - overlookX;
            const float dzOver = z - overlookZ;
            if ((dxOver * dxOver + dzOver * dzOver) < (3.5f * 3.5f)) {
                continue;
            }

            const float y = Mesh::sampleTerrainHeight(x, z);
            const float roll = chanceDist(rng);

            if (y >= -0.20f && y < 0.85f) {
                // Shoreline & Marsh Zone
                if (roll < 0.45f) {
                    const float scale = bushScaleDist(rng);
                    glm::mat4 model(1.0f);
                    model = glm::translate(model, glm::vec3(x, y - 0.06f, z));
                    model = glm::rotate(model, glm::radians(yawDist(rng)), glm::vec3(0.0f, 1.0f, 0.0f));
                    model = glm::scale(model, glm::vec3(scale));
                    m_propInstances.push_back({ model, PropType::ShoreBush });
                } else if (roll < 0.62f) {
                    const float scale = rockScaleDist(rng) * 0.72f;
                    glm::mat4 model(1.0f);
                    model = glm::translate(model, glm::vec3(x, y - 0.06f, z));
                    model = glm::rotate(model, glm::radians(yawDist(rng)), glm::vec3(0.0f, 1.0f, 0.0f));
                    model = glm::scale(model, glm::vec3(scale, scale * 0.8f, scale));
                    m_propInstances.push_back({ model, PropType::Rock });
                } else if (roll < 0.70f && y > 0.35f) {
                    const float scale = treeScaleDist(rng) * 0.85f;
                    glm::mat4 model(1.0f);
                    model = glm::translate(model, glm::vec3(x, y - 0.10f, z));
                    model = glm::rotate(model, glm::radians(yawDist(rng)), glm::vec3(0.0f, 1.0f, 0.0f));
                    model = glm::scale(model, glm::vec3(scale));
                    m_propInstances.push_back({ model, PropType::GoldTree });
                }
            } else if (y >= 0.85f && y <= 5.8f) {
                // Forest & Meadow Zone
                const float biomeWave = std::sin(x * 0.08f) + std::cos(z * 0.08f);
                if (roll < 0.48f) {
                    const float speciesRoll = speciesDist(rng);
                    PropType treeType = PropType::PineTree;
                    if (biomeWave > 0.45f) {
                        if (speciesRoll < 0.55f) {
                            treeType = PropType::GoldTree;
                        } else if (speciesRoll < 0.85f) {
                            treeType = PropType::CrimsonTree;
                        } else {
                            treeType = PropType::PineTree;
                        }
                    } else if (biomeWave < -0.35f) {
                        treeType = (speciesRoll < 0.80f) ? PropType::PineTree : PropType::GoldTree;
                    } else {
                        if (speciesRoll < 0.50f) {
                            treeType = PropType::PineTree;
                        } else if (speciesRoll < 0.78f) {
                            treeType = PropType::GoldTree;
                        } else {
                            treeType = PropType::CrimsonTree;
                        }
                    }

                    const float scale = treeScaleDist(rng);
                    glm::mat4 model(1.0f);
                    model = glm::translate(model, glm::vec3(x, y - 0.10f, z));
                    model = glm::rotate(model, glm::radians(yawDist(rng)), glm::vec3(0.0f, 1.0f, 0.0f));
                    model = glm::scale(model, glm::vec3(scale));
                    m_propInstances.push_back({ model, treeType });
                } else if (roll < 0.64f) {
                    const float scale = bushScaleDist(rng) * 0.90f;
                    glm::mat4 model(1.0f);
                    model = glm::translate(model, glm::vec3(x, y - 0.06f, z));
                    model = glm::rotate(model, glm::radians(yawDist(rng)), glm::vec3(0.0f, 1.0f, 0.0f));
                    model = glm::scale(model, glm::vec3(scale));
                    m_propInstances.push_back({ model, PropType::ShoreBush });
                } else if (roll < 0.76f) {
                    const float scale = rockScaleDist(rng);
                    glm::mat4 model(1.0f);
                    model = glm::translate(model, glm::vec3(x, y - 0.06f, z));
                    model = glm::rotate(model, glm::radians(yawDist(rng)), glm::vec3(0.0f, 1.0f, 0.0f));
                    model = glm::scale(model, glm::vec3(scale, scale * 0.8f, scale));
                    m_propInstances.push_back({ model, PropType::Rock });
                }
            } else if (y > 5.8f && y <= 9.5f) {
                // Alpine Crags & Ridges
                if (roll < 0.30f) {
                    const float scale = rockScaleDist(rng) * 1.15f;
                    glm::mat4 model(1.0f);
                    model = glm::translate(model, glm::vec3(x, y - 0.08f, z));
                    model = glm::rotate(model, glm::radians(yawDist(rng)), glm::vec3(0.0f, 1.0f, 0.0f));
                    model = glm::scale(model, glm::vec3(scale, scale * 0.85f, scale));
                    m_propInstances.push_back({ model, PropType::Rock });
                } else if (roll < 0.46f) {
                    const float scale = treeScaleDist(rng) * 0.80f;
                    glm::mat4 model(1.0f);
                    model = glm::translate(model, glm::vec3(x, y - 0.10f, z));
                    model = glm::rotate(model, glm::radians(yawDist(rng)), glm::vec3(0.0f, 1.0f, 0.0f));
                    model = glm::scale(model, glm::vec3(scale));
                    m_propInstances.push_back({ model, PropType::PineTree });
                } else if (roll < 0.50f) {
                    const float scale = 0.90f + chanceDist(rng) * 0.35f;
                    glm::mat4 model(1.0f);
                    model = glm::translate(model, glm::vec3(x, y - 0.14f, z));
                    model = glm::rotate(model, glm::radians(yawDist(rng)), glm::vec3(0.0f, 1.0f, 0.0f));
                    model = glm::rotate(model, glm::radians(tiltDist(rng)), glm::vec3(1.0f, 0.0f, 0.0f));
                    model = glm::scale(model, glm::vec3(scale));
                    m_propInstances.push_back({ model, PropType::RuinedPillar });
                }
            }
        }
    }
}

void Renderer::processInput() {
    GLFWwindow* handle = m_window.getHandle();
    if (handle == nullptr) {
        return;
    }

    if (m_imguiLayer && m_imguiLayer->wantsCaptureKeyboard()) {
        m_keyFPressed = (glfwGetKey(handle, GLFW_KEY_F) == GLFW_PRESS);
        m_keyCPressed = (glfwGetKey(handle, GLFW_KEY_C) == GLFW_PRESS);
        m_keyPPressed = (glfwGetKey(handle, GLFW_KEY_P) == GLFW_PRESS);
        return;
    }

    if (glfwGetKey(handle, GLFW_KEY_1) == GLFW_PRESS) m_inspectorState.debugView = 0;
    if (glfwGetKey(handle, GLFW_KEY_2) == GLFW_PRESS) m_inspectorState.debugView = 1;
    if (glfwGetKey(handle, GLFW_KEY_3) == GLFW_PRESS) m_inspectorState.debugView = 2;
    if (glfwGetKey(handle, GLFW_KEY_4) == GLFW_PRESS) m_inspectorState.debugView = 3;

    const bool fPressed = (glfwGetKey(handle, GLFW_KEY_F) == GLFW_PRESS);
    if (fPressed && !m_keyFPressed && m_context.hasNonSolidFill()) {
        m_inspectorState.wireframe = !m_inspectorState.wireframe;
    }
    m_keyFPressed = fPressed;

    const bool cPressed = (glfwGetKey(handle, GLFW_KEY_C) == GLFW_PRESS);
    if (cPressed && !m_keyCPressed) {
        m_camera.toggleMode();
        m_inspectorState.cameraMode = m_camera.getMode();
    }
    m_keyCPressed = cPressed;

    const bool pPressed = (glfwGetKey(handle, GLFW_KEY_P) == GLFW_PRESS);
    if (pPressed && !m_keyPPressed) {
        m_inspectorState.psxEnabled = !m_inspectorState.psxEnabled;
    }
    m_keyPPressed = pPressed;
}

void Renderer::readTimestampQueries(uint32_t frameIndex) {
    if (m_timestampQueryPools[frameIndex] == VK_NULL_HANDLE || !m_queryPoolHasData[frameIndex]) {
        return;
    }

    std::array<uint64_t, QUERY_COUNT_PER_FRAME> timestamps{};
    const VkResult res = vkGetQueryPoolResults(
        m_context.getDevice(),
        m_timestampQueryPools[frameIndex],
        0,
        QUERY_COUNT_PER_FRAME,
        sizeof(timestamps),
        timestamps.data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
    );

    if (res == VK_SUCCESS) {
        const float periodNs = m_context.getDeviceProperties().limits.timestampPeriod;
        if (periodNs > 0.0f) {
            if (timestamps[1] >= timestamps[0]) {
                m_shadowPassMs = static_cast<float>(timestamps[1] - timestamps[0]) * periodNs * 1e-6f;
            }
            if (timestamps[3] >= timestamps[2]) {
                m_mainPassMs = static_cast<float>(timestamps[3] - timestamps[2]) * periodNs * 1e-6f;
            }
            if (timestamps[3] >= timestamps[0]) {
                m_gpuFrameMs = static_cast<float>(timestamps[3] - timestamps[0]) * periodNs * 1e-6f;
            }
        }
    }
}

void Renderer::updateVmaStats() {
    VmaTotalStatistics stats{};
    vmaCalculateStatistics(m_context.getAllocator(), &stats);

    const VkPhysicalDeviceMemoryProperties* memProps = nullptr;
    vmaGetMemoryProperties(m_context.getAllocator(), &memProps);

    VkDeviceSize deviceLocalBytes = 0;
    uint32_t deviceLocalAllocs = 0;
    VkDeviceSize hostVisibleBytes = 0;
    uint32_t hostVisibleAllocs = 0;

    if (memProps != nullptr) {
        for (uint32_t i = 0; i < memProps->memoryTypeCount; ++i) {
            const VkMemoryPropertyFlags flags = memProps->memoryTypes[i].propertyFlags;
            const auto& typeStats = stats.memoryType[i].statistics;

            if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
                deviceLocalBytes += typeStats.allocationBytes;
                deviceLocalAllocs += typeStats.allocationCount;
            } else if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
                hostVisibleBytes += typeStats.allocationBytes;
                hostVisibleAllocs += typeStats.allocationCount;
            }
        }
    }

    constexpr float bytesToMB = 1.0f / (1024.0f * 1024.0f);
    m_inspectorState.deviceLocalMB = static_cast<float>(deviceLocalBytes) * bytesToMB;
    m_inspectorState.deviceLocalAllocations = deviceLocalAllocs;
    m_inspectorState.hostVisibleMB = static_cast<float>(hostVisibleBytes) * bytesToMB;
    m_inspectorState.hostVisibleAllocations = hostVisibleAllocs;
    m_inspectorState.totalReservedBlockMB = static_cast<float>(stats.total.statistics.blockBytes) * bytesToMB;
    m_inspectorState.totalBlocks = stats.total.statistics.blockCount;
}

void Renderer::updateInspectorState(float deltaTime) {
    const float clampedDt = std::max(deltaTime, 0.0001f);
    const float instantFps = 1.0f / clampedDt;
    const float instantCpuMs = clampedDt * 1000.0f;

    if (m_fpsSmoothed <= 0.0f) {
        m_fpsSmoothed = instantFps;
        m_cpuFrameMsSmoothed = instantCpuMs;
    } else {
        m_fpsSmoothed = m_fpsSmoothed * 0.92f + instantFps * 0.08f;
        m_cpuFrameMsSmoothed = m_cpuFrameMsSmoothed * 0.92f + instantCpuMs * 0.08f;
    }

    const VkExtent2D extent = m_swapchain.getExtent();
    m_inspectorState.presentModeName = m_swapchain.getPresentModeName();
    m_inspectorState.swapchainWidth = extent.width;
    m_inspectorState.swapchainHeight = extent.height;
    m_inspectorState.fps = m_fpsSmoothed;
    m_inspectorState.cpuFrameMs = m_cpuFrameMsSmoothed;
    m_inspectorState.gpuFrameMs = m_gpuFrameMs;
    m_inspectorState.shadowPassMs = m_shadowPassMs;
    m_inspectorState.mainPassMs = m_mainPassMs;
    m_inspectorState.drawCalls = m_lastDrawCalls;
    m_inspectorState.renderedTriangles = m_lastRenderedTriangles;
    m_inspectorState.cameraMode = m_camera.getMode();

    updateVmaStats();
}

void Renderer::updateFrameUBO(float elapsedTime) {
    const VkExtent2D extent = m_swapchain.getExtent();
    const float aspectRatio = static_cast<float>(extent.width) /
                              static_cast<float>(std::max(extent.height, 1u));

    const float elevRad = glm::radians(m_inspectorState.sunElevationDeg);
    const float azimRad = glm::radians(m_inspectorState.sunAzimuthDeg);
    const glm::vec3 sunDir = glm::normalize(glm::vec3(
        std::cos(elevRad) * std::cos(azimRad),
        std::sin(elevRad),
        std::cos(elevRad) * std::sin(azimRad)
    ));

    const glm::vec3 lightPos = sunDir * 85.0f;
    const glm::mat4 lightView = glm::lookAt(
        lightPos,
        glm::vec3(0.0f, 1.5f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    glm::mat4 lightProj = glm::ortho(-55.0f, 55.0f, -55.0f, 55.0f, 10.0f, 175.0f);
    lightProj[1][1] *= -1.0f;

    const VkExtent2D renderExtent = resolvePsxExtent();

    FrameUBO ubo{};
    ubo.view = m_camera.getViewMatrix();
    ubo.proj = m_camera.getProjectionMatrix(aspectRatio);
    ubo.viewProj = ubo.proj * ubo.view;
    ubo.lightSpaceMatrix = lightProj * lightView;
    ubo.sunDirectionIntensity = glm::vec4(sunDir, m_inspectorState.sunIntensity);
    ubo.sunColorAmbient = glm::vec4(1.0f, 0.95f, 0.86f, m_inspectorState.ambientStrength);
    ubo.cameraPosTime = glm::vec4(m_camera.getPosition(), elapsedTime);
    ubo.waterParams = glm::vec4(m_inspectorState.waterWaveSpeed, m_inspectorState.waterWaveHeight, -0.25f, 0.0f);
    ubo.psxParams = glm::vec4(
        m_inspectorState.psxEnabled ? 1.0f : 0.0f,
        static_cast<float>((std::max)(renderExtent.width, 1u)),
        static_cast<float>((std::max)(renderExtent.height, 1u)),
        m_inspectorState.psxDitherStrength
    );
    ubo.psxConfig = glm::vec4(
        m_inspectorState.psxVertexSnap,
        m_inspectorState.psxAffineWarping ? 1.0f : 0.0f,
        m_inspectorState.psx15BitColor ? 1.0f : 0.0f,
        m_inspectorState.psxFogFar
    );

    m_uboBuffers[m_currentFrame].upload(&ubo, sizeof(ubo), 0);
}

void Renderer::cleanupSyncObjects() {
    VkDevice device = m_context.getDevice();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (m_inFlightFences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(device, m_inFlightFences[i], nullptr);
            m_inFlightFences[i] = VK_NULL_HANDLE;
        }
        if (m_imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, m_imageAvailableSemaphores[i], nullptr);
            m_imageAvailableSemaphores[i] = VK_NULL_HANDLE;
        }
    }

    for (auto& semaphore : m_renderFinishedSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
    }
    m_renderFinishedSemaphores.clear();
}

void Renderer::recreateSwapchain() {
    auto [width, height] = m_window.getFramebufferSize();
    while (width == 0 || height == 0) {
        m_window.pollEvents();
        auto size = m_window.getFramebufferSize();
        width = size.first;
        height = size.second;
    }

    m_context.waitIdle();
    m_swapchain.recreate(m_window);

    if (m_renderFinishedSemaphores.size() != m_swapchain.getImageCount()) {
        for (auto& semaphore : m_renderFinishedSemaphores) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(m_context.getDevice(), semaphore, nullptr);
            }
        }
        m_renderFinishedSemaphores.clear();

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        m_renderFinishedSemaphores.resize(m_swapchain.getImageCount());
        for (size_t i = 0; i < m_swapchain.getImageCount(); ++i) {
            if (vkCreateSemaphore(m_context.getDevice(), &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS) {
                throw std::runtime_error("Impossibile ricreare i semafori renderFinished!");
            }
            const std::string semName = "RenderFinished Semaphore " + std::to_string(i);
            DebugUtils::setObjectName(m_context.getDevice(), VK_OBJECT_TYPE_SEMAPHORE, m_renderFinishedSemaphores[i], semName.c_str());
        }
    }
}

void Renderer::renderFrame() {
    const auto now = std::chrono::steady_clock::now();
    const float deltaTime = std::chrono::duration<float>(now - m_lastFrameTime).count();
    const float elapsedTime = std::chrono::duration<float>(now - m_startTime).count();
    m_lastFrameTime = now;

    processInput();
    const bool uiCapturingInput = m_imguiLayer && (m_imguiLayer->wantsCaptureMouse() || m_imguiLayer->wantsCaptureKeyboard());
    m_camera.update(uiCapturingInput ? nullptr : m_window.getHandle(), deltaTime);

    VkDevice device = m_context.getDevice();

    // 1. Attesa che il frame in volo precedente abbia terminato
    vkWaitForFences(device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    readTimestampQueries(m_currentFrame);

    // 2. Acquisizione dell'immagine successiva dalla Swapchain
    uint32_t imageIndex = 0;
    VkResult result = m_swapchain.acquireNextImage(m_imageAvailableSemaphores[m_currentFrame], imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Impossibile acquisire un'immagine dalla Swapchain!");
    }

    vkResetFences(device, 1, &m_inFlightFences[m_currentFrame]);

    // Aggiorna telemetria + ImGui prima della registrazione del command buffer
    updateInspectorState(deltaTime);
    if (m_imguiLayer) {
        m_imguiLayer->beginFrame();
        m_imguiLayer->drawBackendInspector(m_inspectorState);
        m_camera.setMode(m_inspectorState.cameraMode);
        if (!m_context.hasNonSolidFill()) {
            m_inspectorState.wireframe = false;
        }
    }

    if (m_inspectorState.psxEnabled && m_inspectorState.psxResolutionPreset != 3) {
        const VkExtent2D desiredExtent = resolvePsxExtent();
        if (desiredExtent.width != m_psxExtent.width || desiredExtent.height != m_psxExtent.height) {
            m_context.waitIdle();
            createPsxResources(desiredExtent);
        }
    }

    // Aggiorna UBO dopo gli input dell'UI (luce/acqua/camera/PSX)
    updateFrameUBO(elapsedTime);

    // 3. Registrazione dei comandi nel Command Buffer
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Impossibile iniziare la registrazione del Command Buffer!");
    }

    const VkQueryPool queryPool = m_timestampQueryPools[m_currentFrame];
    if (queryPool != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(cmd, queryPool, 0, QUERY_COUNT_PER_FRAME);
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, queryPool, 0);
    }

    uint32_t frameDrawCalls = 0;
    uint32_t frameTriangles = 0;

    // =========================================================================
    // PASS 1: Directional Shadow Depth Pass (2048x2048 Depth-Only)
    // =========================================================================
    {
        ScopedCmdLabel shadowLabel(cmd, "Shadow Depth Pass", { 0.95f, 0.65f, 0.18f, 1.0f });

        Image::transitionLayout(
            cmd,
            m_shadowMapImage.getHandle(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            0,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT
        );

        VkRenderingAttachmentInfo shadowDepthAttachment{};
        shadowDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        shadowDepthAttachment.imageView = m_shadowMapImage.getView();
        shadowDepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        shadowDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        shadowDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        shadowDepthAttachment.clearValue.depthStencil = { 1.0f, 0 };

        VkRenderingInfo shadowRenderingInfo{};
        shadowRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        shadowRenderingInfo.renderArea.offset = { 0, 0 };
        shadowRenderingInfo.renderArea.extent = { SHADOW_MAP_DIM, SHADOW_MAP_DIM };
        shadowRenderingInfo.layerCount = 1;
        shadowRenderingInfo.colorAttachmentCount = 0;
        shadowRenderingInfo.pColorAttachments = nullptr;
        shadowRenderingInfo.pDepthAttachment = &shadowDepthAttachment;

        vkCmdBeginRendering(cmd, &shadowRenderingInfo);

        VkViewport shadowViewport{};
        shadowViewport.x = 0.0f;
        shadowViewport.y = 0.0f;
        shadowViewport.width = static_cast<float>(SHADOW_MAP_DIM);
        shadowViewport.height = static_cast<float>(SHADOW_MAP_DIM);
        shadowViewport.minDepth = 0.0f;
        shadowViewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &shadowViewport);

        VkRect2D shadowScissor{};
        shadowScissor.offset = { 0, 0 };
        shadowScissor.extent = { SHADOW_MAP_DIM, SHADOW_MAP_DIM };
        vkCmdSetScissor(cmd, 0, 1, &shadowScissor);

        vkCmdSetDepthBias(cmd, 1.25f, 0.0f, 1.75f);

        m_shadowPipeline.bind(cmd, false);

        if (m_context.hasPushDescriptors()) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = m_uboBuffers[m_currentFrame].getHandle();
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(FrameUBO);

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = VK_NULL_HANDLE;
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pBufferInfo = &bufferInfo;

            m_context.cmdPushDescriptorSet(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_shadowPipeline.getLayout(),
                0,
                1,
                &descriptorWrite
            );
        } else {
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_shadowPipeline.getLayout(),
                0,
                1,
                &m_fallbackShadowDescriptorSets[m_currentFrame],
                0,
                nullptr
            );
        }

        PushConstants pc{};
        pc.model = glm::mat4(1.0f);
        pc.debugView = m_inspectorState.debugView;
        pc.materialType = 0;
        vkCmdPushConstants(
            cmd,
            m_shadowPipeline.getLayout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(PushConstants),
            &pc
        );
        m_terrainMesh.bindAndDraw(cmd);
        ++frameDrawCalls;
        frameTriangles += m_terrainMesh.getTriangleCount();

        for (const auto& prop : m_propInstances) {
            pc.model = prop.model;
            vkCmdPushConstants(
                cmd,
                m_shadowPipeline.getLayout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(PushConstants),
                &pc
            );
            frameTriangles += drawPropMesh(cmd, prop.type);
            ++frameDrawCalls;
        }

        vkCmdEndRendering(cmd);

        if (queryPool != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, queryPool, 1);
        }

        Image::transitionLayout(
            cmd,
            m_shadowMapImage.getHandle(),
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT
        );
    }

    // =========================================================================
    // PASS 2: Main Low-Poly / PSX Pass (+ Nearest-Neighbor Blit & Dear ImGui)
    // =========================================================================
    {
        ScopedCmdLabel passLabel(cmd, "Main Low-Poly Pass", { 0.18f, 0.55f, 0.95f, 1.0f });

        if (queryPool != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, queryPool, 2);
        }

        const bool useOffscreenPsx = m_inspectorState.psxEnabled && (m_inspectorState.psxResolutionPreset != 3);
        const VkExtent2D swapExtent = m_swapchain.getExtent();
        const VkExtent2D targetExtent = useOffscreenPsx ? m_psxExtent : swapExtent;
        const VkImage targetColorImage = useOffscreenPsx ? m_psxColorImage.getHandle() : m_swapchain.getImage(imageIndex);
        const VkImageView targetColorView = useOffscreenPsx ? m_psxColorImage.getView() : m_swapchain.getImageView(imageIndex);
        const VkImage targetDepthImage = useOffscreenPsx ? m_psxDepthImage.getHandle() : m_swapchain.getDepthImage();
        const VkImageView targetDepthView = useOffscreenPsx ? m_psxDepthImage.getView() : m_swapchain.getDepthImageView();

        Image::transitionLayout(
            cmd,
            targetColorImage,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        Image::transitionLayout(
            cmd,
            targetDepthImage,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            0,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT
        );

        VkClearValue clearColor = {{{ 0.08f, 0.12f, 0.20f, 1.0f }}};

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = targetColorView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue = clearColor;

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = targetDepthView;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.offset = { 0, 0 };
        renderingInfo.renderArea.extent = targetExtent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        renderingInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(cmd, &renderingInfo);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(targetExtent.width);
        viewport.height = static_cast<float>(targetExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = targetExtent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        if (m_context.hasPushDescriptors()) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = m_uboBuffers[m_currentFrame].getHandle();
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(FrameUBO);

            VkDescriptorImageInfo imageInfo{};
            imageInfo.sampler = m_shadowSampler;
            imageInfo.imageView = m_shadowMapImage.getView();
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = VK_NULL_HANDLE;
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].pBufferInfo = &bufferInfo;

            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = VK_NULL_HANDLE;
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pImageInfo = &imageInfo;

            m_context.cmdPushDescriptorSet(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_pipeline.getLayout(),
                0,
                static_cast<uint32_t>(descriptorWrites.size()),
                descriptorWrites.data()
            );
        } else {
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_pipeline.getLayout(),
                0,
                1,
                &m_fallbackDescriptorSets[m_currentFrame],
                0,
                nullptr
            );
        }

        // Sky Dome (sempre solid)
        m_pipeline.bind(cmd, false);
        PushConstants pc{};
        pc.model = glm::translate(glm::mat4(1.0f), m_camera.getPosition());
        pc.debugView = m_inspectorState.debugView;
        pc.materialType = 2;
        vkCmdPushConstants(
            cmd,
            m_pipeline.getLayout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(PushConstants),
            &pc
        );
        m_skyDomeMesh.bindAndDraw(cmd);
        ++frameDrawCalls;
        frameTriangles += m_skyDomeMesh.getTriangleCount();

        // Scena Low-Poly (Fill o Wireframe)
        m_pipeline.bind(cmd, m_inspectorState.wireframe);

        pc.model = glm::mat4(1.0f);
        pc.materialType = 0;
        vkCmdPushConstants(
            cmd,
            m_pipeline.getLayout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(PushConstants),
            &pc
        );
        m_terrainMesh.bindAndDraw(cmd);
        ++frameDrawCalls;
        frameTriangles += m_terrainMesh.getTriangleCount();

        for (const auto& prop : m_propInstances) {
            pc.model = prop.model;
            pc.materialType = 0;
            vkCmdPushConstants(
                cmd,
                m_pipeline.getLayout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(PushConstants),
                &pc
            );
            frameTriangles += drawPropMesh(cmd, prop.type);
            ++frameDrawCalls;
        }

        pc.model = glm::mat4(1.0f);
        pc.materialType = 1;
        vkCmdPushConstants(
            cmd,
            m_pipeline.getLayout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(PushConstants),
            &pc
        );
        m_waterMesh.bindAndDraw(cmd);
        ++frameDrawCalls;
        frameTriangles += m_waterMesh.getTriangleCount();

        if (!useOffscreenPsx) {
            // Renderizzazione Dear ImGui Overlay dentro la Dynamic Rendering pass principale
            if (m_imguiLayer) {
                ScopedCmdLabel uiLabel(cmd, "ImGui Telemetry Overlay", { 0.45f, 0.85f, 0.45f, 1.0f });
                m_imguiLayer->render(cmd);
            }

            vkCmdEndRendering(cmd);
        } else {
            vkCmdEndRendering(cmd);

            // Nearest-Neighbor Blit dal target PSX a bassa risoluzione alla Swapchain nativa
            {
                ScopedCmdLabel blitLabel(cmd, "PSX Nearest Blit", { 0.90f, 0.45f, 0.85f, 1.0f });

                Image::transitionLayout(
                    cmd,
                    m_psxColorImage.getHandle(),
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_BLIT_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT
                );

                Image::transitionLayout(
                    cmd,
                    m_swapchain.getImage(imageIndex),
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    0,
                    VK_PIPELINE_STAGE_2_BLIT_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT
                );

                VkImageBlit2 blitRegion{};
                blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
                blitRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                blitRegion.srcOffsets[0] = { 0, 0, 0 };
                blitRegion.srcOffsets[1] = {
                    static_cast<int32_t>(m_psxExtent.width),
                    static_cast<int32_t>(m_psxExtent.height),
                    1
                };
                blitRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                blitRegion.dstOffsets[0] = { 0, 0, 0 };
                blitRegion.dstOffsets[1] = {
                    static_cast<int32_t>(swapExtent.width),
                    static_cast<int32_t>(swapExtent.height),
                    1
                };

                VkBlitImageInfo2 blitInfo{};
                blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
                blitInfo.srcImage = m_psxColorImage.getHandle();
                blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                blitInfo.dstImage = m_swapchain.getImage(imageIndex);
                blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                blitInfo.regionCount = 1;
                blitInfo.pRegions = &blitRegion;
                blitInfo.filter = VK_FILTER_NEAREST;

                vkCmdBlitImage2(cmd, &blitInfo);
            }

            // Passata UI nativa a risoluzione piena sopra l'immagine upscalata
            Image::transitionLayout(
                cmd,
                m_swapchain.getImage(imageIndex),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_BLIT_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT
            );

            Image::transitionLayout(
                cmd,
                m_swapchain.getDepthImage(),
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                0,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT
            );

            if (m_imguiLayer) {
                ScopedCmdLabel uiLabel(cmd, "ImGui Telemetry Overlay", { 0.45f, 0.85f, 0.45f, 1.0f });

                VkRenderingAttachmentInfo uiColorAttachment{};
                uiColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                uiColorAttachment.imageView = m_swapchain.getImageView(imageIndex);
                uiColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                uiColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                uiColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                VkRenderingAttachmentInfo uiDepthAttachment{};
                uiDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                uiDepthAttachment.imageView = m_swapchain.getDepthImageView();
                uiDepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                uiDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                uiDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                uiDepthAttachment.clearValue.depthStencil = { 1.0f, 0 };

                VkRenderingInfo uiRenderingInfo{};
                uiRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                uiRenderingInfo.renderArea.offset = { 0, 0 };
                uiRenderingInfo.renderArea.extent = swapExtent;
                uiRenderingInfo.layerCount = 1;
                uiRenderingInfo.colorAttachmentCount = 1;
                uiRenderingInfo.pColorAttachments = &uiColorAttachment;
                uiRenderingInfo.pDepthAttachment = &uiDepthAttachment;

                vkCmdBeginRendering(cmd, &uiRenderingInfo);

                VkViewport uiViewport{};
                uiViewport.x = 0.0f;
                uiViewport.y = 0.0f;
                uiViewport.width = static_cast<float>(swapExtent.width);
                uiViewport.height = static_cast<float>(swapExtent.height);
                uiViewport.minDepth = 0.0f;
                uiViewport.maxDepth = 1.0f;
                vkCmdSetViewport(cmd, 0, 1, &uiViewport);

                VkRect2D uiScissor{};
                uiScissor.offset = { 0, 0 };
                uiScissor.extent = swapExtent;
                vkCmdSetScissor(cmd, 0, 1, &uiScissor);

                m_imguiLayer->render(cmd);

                vkCmdEndRendering(cmd);
            }
        }

        Image::transitionLayout(
            cmd,
            m_swapchain.getImage(imageIndex),
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            0,
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        if (queryPool != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, queryPool, 3);
            m_queryPoolHasData[m_currentFrame] = true;
        }
    }

    m_lastDrawCalls = frameDrawCalls;
    m_lastRenderedTriangles = frameTriangles;

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("Impossibile registrare il Command Buffer!");
    }

    // 4. Invia comandi alla coda grafica con vkQueueSubmit2
    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = cmd;

    VkSemaphoreSubmitInfo waitSemaphoreInfo{};
    waitSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitSemaphoreInfo.semaphore = m_imageAvailableSemaphores[m_currentFrame];
    waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signalSemaphoreInfo{};
    signalSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalSemaphoreInfo.semaphore = m_renderFinishedSemaphores[imageIndex];
    signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitSemaphoreInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalSemaphoreInfo;

    if (vkQueueSubmit2(m_context.getGraphicsQueue(), 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("Impossibile inviare i comandi alla coda grafica!");
    }

    // 5. Presentazione a schermo
    result = m_swapchain.present(m_renderFinishedSemaphores[imageIndex], imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_window.wasResized()) {
        m_window.resetResizedFlag();
        recreateSwapchain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Impossibile presentare l'immagine della Swapchain!");
    }

    // 6. Avanza al prossimo frame in volo
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}
