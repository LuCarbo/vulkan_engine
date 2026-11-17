#pragma once

#include "core/Buffer.hpp"
#include "core/Pipeline.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

class Context;

/**
 * @brief GPU-resident indexed triangle mesh backed by device-local VMA vertex and index buffers.
 */
class Mesh {
public:
    Mesh() = default;

    /**
     * @brief Uploads vertex and index data to device-local GPU buffers.
     */
    Mesh(
        const Context& context,
        std::span<const Vertex> vertices,
        std::span<const uint32_t> indices,
        const char* debugName = nullptr);

    ~Mesh() = default;

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    /**
     * @brief Releases the underlying GPU buffers and resets vertex/index counts.
     */
    void cleanup() noexcept;

    /**
     * @brief Binds vertex and index buffers and issues an indexed draw call if valid.
     */
    void bindAndDraw(VkCommandBuffer cmd) const noexcept;

    [[nodiscard]] uint32_t getIndexCount() const noexcept { return m_indexCount; }
    [[nodiscard]] uint32_t getVertexCount() const noexcept { return m_vertexCount; }
    [[nodiscard]] uint32_t getTriangleCount() const noexcept { return m_indexCount / 3; }

    /**
     * @brief Evaluates the deterministic multi-octave terrain heightfield at world coordinates (x, z).
     */
    [[nodiscard]] static float sampleTerrainHeight(float x, float z) noexcept;

    /**
     * @brief Generates a subdivided low-poly terrain mesh with biome vertex colors by elevation.
     */
    [[nodiscard]] static Mesh createTerrain(
        const Context& context,
        uint32_t gridSize = 150,
        float worldSize = 120.0f);

    /**
     * @brief Generates a subdivided horizontal water surface mesh at the given water level.
     */
    [[nodiscard]] static Mesh createWaterPlane(
        const Context& context,
        uint32_t gridSize = 110,
        float worldSize = 120.0f,
        float waterLevel = -0.25f);

    /**
     * @brief Generates an inward-facing UV sphere sky dome with atmospheric vertex gradient.
     */
    [[nodiscard]] static Mesh createSkyDome(
        const Context& context,
        float radius = 350.0f);

    /**
     * @brief Loads an indexed mesh from a Wavefront .obj file, falling back to a low-poly crystal if unavailable.
     */
    [[nodiscard]] static Mesh loadFromObj(
        const Context& context,
        std::string_view objPath,
        const glm::vec3& defaultColor = {0.8f, 0.8f, 0.8f});

    /**
     * @brief Generates a multi-part low-poly stone sanctuary ruin (plinth, columns, lintel archway, and rune obelisk).
     */
    [[nodiscard]] static Mesh createStoneRuins(const Context& context);

    /**
     * @brief Generates a weathered/mossy stone column & rubble fragment for submerged lake ruins and hillside markers.
     */
    [[nodiscard]] static Mesh createRuinedPillar(const Context& context);

    /**
     * @brief Generates a stylized low-poly broadleaf/autumn tree with a multi-cluster faceted canopy.
     */
    [[nodiscard]] static Mesh createBroadleafTree(
        const Context& context,
        const glm::vec3& canopyColor,
        const char* debugName = "Broadleaf Tree Mesh");

    /**
     * @brief Generates a low-poly shoreline bush and cattail reed cluster.
     */
    [[nodiscard]] static Mesh createBushAndReeds(const Context& context);

private:
    Buffer m_vertexBuffer;
    Buffer m_indexBuffer;
    uint32_t m_vertexCount{0};
    uint32_t m_indexCount{0};
};
