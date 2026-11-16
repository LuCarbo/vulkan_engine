#include "scene/Mesh.hpp"
#include "core/Context.hpp"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace {

[[nodiscard]] float hash2D(int ix, int iz) noexcept {
    // Unsigned arithmetic guarantees well-defined wraparound on negative coordinates.
    uint32_t h = static_cast<uint32_t>(ix) * 374761393u + static_cast<uint32_t>(iz) * 668265263u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h ^= (h >> 16u);
    return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
}

[[nodiscard]] float valueNoise2D(float x, float z) noexcept {
    const float fx = std::floor(x);
    const float fz = std::floor(z);
    const int ix = static_cast<int>(fx);
    const int iz = static_cast<int>(fz);

    const float tx = x - fx;
    const float tz = z - fz;
    const float ux = tx * tx * tx * (tx * (tx * 6.0f - 15.0f) + 10.0f);
    const float uz = tz * tz * tz * (tz * (tz * 6.0f - 15.0f) + 10.0f);

    const float v00 = hash2D(ix, iz);
    const float v10 = hash2D(ix + 1, iz);
    const float v01 = hash2D(ix, iz + 1);
    const float v11 = hash2D(ix + 1, iz + 1);

    const float a = v00 + (v10 - v00) * ux;
    const float b = v01 + (v11 - v01) * ux;
    return (a + (b - a) * uz) * 2.0f - 1.0f;
}

[[nodiscard]] glm::vec3 computeTerrainColor(float x, float y, float z) noexcept {
    // Subtle deterministic per-vertex jitter for crisp low-poly facet variety.
    const float variation = (valueNoise2D(x * 0.45f + 2.7f, z * 0.45f - 1.9f)) * 0.045f;
    glm::vec3 baseColor{0.5f};

    if (y < -0.8f) {
        const float depthT = std::clamp((-y - 0.8f) / 2.8f, 0.0f, 1.0f);
        baseColor = glm::mix(glm::vec3(0.14f, 0.25f, 0.23f), glm::vec3(0.07f, 0.17f, 0.21f), depthT);
    } else if (y < 0.45f) {
        const float shoreT = std::clamp((y + 0.8f) / 1.25f, 0.0f, 1.0f);
        baseColor = glm::mix(glm::vec3(0.66f, 0.58f, 0.40f), glm::vec3(0.81f, 0.73f, 0.51f), shoreT);
    } else if (y < 5.5f) {
        const float meadowT = std::clamp((y - 0.45f) / 5.05f, 0.0f, 1.0f);
        baseColor = glm::mix(glm::vec3(0.28f, 0.58f, 0.22f), glm::vec3(0.15f, 0.39f, 0.17f), meadowT);
    } else if (y < 10.5f) {
        const float rockT = std::clamp((y - 5.5f) / 5.0f, 0.0f, 1.0f);
        baseColor = glm::mix(glm::vec3(0.39f, 0.40f, 0.43f), glm::vec3(0.55f, 0.56f, 0.60f), rockT);
    } else {
        const float snowT = std::clamp((y - 10.5f) / 3.0f, 0.0f, 1.0f);
        baseColor = glm::mix(glm::vec3(0.87f, 0.90f, 0.95f), glm::vec3(0.97f, 0.98f, 1.00f), snowT);
    }

    return glm::clamp(baseColor + glm::vec3(variation, variation * 0.85f, variation * 0.65f), 0.0f, 1.0f);
}

/**
 * @brief Appends CCW (+Y normal) quad triangles for a (gridSize + 1) x (gridSize + 1) vertex grid.
 */
void buildGridIndices(uint32_t gridSize, std::vector<uint32_t>& outIndices) {
    const uint32_t stride = gridSize + 1u;
    outIndices.reserve(static_cast<size_t>(gridSize) * static_cast<size_t>(gridSize) * 6u);

    for (uint32_t row = 0; row < gridSize; ++row) {
        for (uint32_t col = 0; col < gridSize; ++col) {
            const uint32_t i00 = row * stride + col;
            const uint32_t i10 = i00 + 1u;
            const uint32_t i01 = (row + 1u) * stride + col;
            const uint32_t i11 = i01 + 1u;

            // Alternate quad diagonal to avoid directional facet striping while keeping (v1-v0)x(v2-v0) = +Y.
            if (((row + col) & 1u) == 0u) {
                outIndices.push_back(i00);
                outIndices.push_back(i01);
                outIndices.push_back(i10);

                outIndices.push_back(i10);
                outIndices.push_back(i01);
                outIndices.push_back(i11);
            } else {
                outIndices.push_back(i00);
                outIndices.push_back(i01);
                outIndices.push_back(i11);

                outIndices.push_back(i00);
                outIndices.push_back(i11);
                outIndices.push_back(i10);
            }
        }
    }
}

[[nodiscard]] std::filesystem::path resolveObjPath(std::string_view objPath) {
    const std::filesystem::path requested(objPath);
    const std::filesystem::path filename = requested.filename();

    const std::array<std::filesystem::path, 5> candidates = {
        requested,
        std::filesystem::path("assets") / filename,
        std::filesystem::path("..") / "assets" / filename,
        std::filesystem::path("..") / ".." / "assets" / filename,
        std::filesystem::path("..") / ".." / ".." / "assets" / filename
    };

    std::error_code ec;
    for (const auto& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
        ec.clear();
    }

    return {};
}

[[nodiscard]] Mesh createFallbackCrystal(const Context& context, const glm::vec3& defaultColor) {
    constexpr uint32_t sides = 6u;
    constexpr float radius = 0.65f;
    constexpr float midHeight = 0.85f;
    constexpr float topHeight = 2.20f;

    const glm::vec3 topColor = glm::clamp(defaultColor * 1.18f, 0.0f, 1.0f);
    const glm::vec3 midColor = glm::clamp(defaultColor, 0.0f, 1.0f);
    const glm::vec3 bottomColor = glm::clamp(defaultColor * 0.62f, 0.0f, 1.0f);

    std::vector<Vertex> vertices;
    vertices.reserve(sides + 2u);
    vertices.push_back({{0.0f, topHeight, 0.0f}, topColor, {0.5f, 1.0f}});    // 0: apex
    vertices.push_back({{0.0f, 0.0f, 0.0f}, bottomColor, {0.5f, 0.0f}});      // 1: base

    for (uint32_t i = 0; i < sides; ++i) {
        const float angle = (static_cast<float>(i) / static_cast<float>(sides)) * 2.0f * std::numbers::pi_v<float>;
        const float x = radius * std::cos(angle);
        const float z = radius * std::sin(angle);
        const float u = static_cast<float>(i) / static_cast<float>(sides);
        vertices.push_back({{x, midHeight, z}, midColor, {u, 0.45f}});
    }

    std::vector<uint32_t> indices;
    indices.reserve(sides * 6u);
    for (uint32_t i = 0; i < sides; ++i) {
        const uint32_t curr = 2u + i;
        const uint32_t next = 2u + ((i + 1u) % sides);

        // Outward CCW winding
        indices.push_back(next);
        indices.push_back(curr);
        indices.push_back(0u);

        indices.push_back(curr);
        indices.push_back(next);
        indices.push_back(1u);
    }

    return Mesh(context, vertices, indices, "Fallback Crystal Mesh");
}

struct ObjVertexKey {
    int vertexIndex{-1};
    int texcoordIndex{-1};

    bool operator==(const ObjVertexKey&) const noexcept = default;
};

struct ObjVertexKeyHash {
    size_t operator()(const ObjVertexKey& key) const noexcept {
        const uint64_t high = static_cast<uint64_t>(static_cast<uint32_t>(key.vertexIndex));
        const uint64_t low = static_cast<uint64_t>(static_cast<uint32_t>(key.texcoordIndex));
        return std::hash<uint64_t>{}((high << 32u) | low);
    }
};

} // namespace

Mesh::Mesh(
    const Context& context,
    std::span<const Vertex> vertices,
    std::span<const uint32_t> indices,
    const char* debugName) {
    if (vertices.empty() || indices.empty()) {
        return;
    }

    if (vertices.size() > (std::numeric_limits<uint32_t>::max)() ||
        indices.size() > (std::numeric_limits<uint32_t>::max)()) {
        throw std::runtime_error("Mesh vertex or index count exceeds uint32_t limit.");
    }

    const std::string vbName = (debugName != nullptr) ? (std::string(debugName) + " Vertex Buffer") : std::string{};
    const std::string ibName = (debugName != nullptr) ? (std::string(debugName) + " Index Buffer") : std::string{};

    m_vertexBuffer = Buffer::createDeviceLocal(
        context,
        vertices.data(),
        static_cast<VkDeviceSize>(vertices.size_bytes()),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        (debugName != nullptr) ? vbName.c_str() : nullptr);

    m_indexBuffer = Buffer::createDeviceLocal(
        context,
        indices.data(),
        static_cast<VkDeviceSize>(indices.size_bytes()),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        (debugName != nullptr) ? ibName.c_str() : nullptr);

    m_vertexCount = static_cast<uint32_t>(vertices.size());
    m_indexCount = static_cast<uint32_t>(indices.size());
}

Mesh::Mesh(Mesh&& other) noexcept
    : m_vertexBuffer(std::move(other.m_vertexBuffer)),
      m_indexBuffer(std::move(other.m_indexBuffer)),
      m_vertexCount(other.m_vertexCount),
      m_indexCount(other.m_indexCount) {
    other.m_vertexCount = 0;
    other.m_indexCount = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        cleanup();

        m_vertexBuffer = std::move(other.m_vertexBuffer);
        m_indexBuffer = std::move(other.m_indexBuffer);
        m_vertexCount = other.m_vertexCount;
        m_indexCount = other.m_indexCount;

        other.m_vertexCount = 0;
        other.m_indexCount = 0;
    }
    return *this;
}

void Mesh::cleanup() noexcept {
    m_vertexBuffer.cleanup();
    m_indexBuffer.cleanup();
    m_vertexCount = 0;
    m_indexCount = 0;
}

void Mesh::bindAndDraw(VkCommandBuffer cmd) const noexcept {
    const VkBuffer vertexBuffer = m_vertexBuffer.getHandle();
    const VkBuffer indexBuffer = m_indexBuffer.getHandle();
    if (cmd == VK_NULL_HANDLE ||
        m_indexCount == 0 ||
        vertexBuffer == VK_NULL_HANDLE ||
        indexBuffer == VK_NULL_HANDLE) {
        return;
    }

    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
}

float Mesh::sampleTerrainHeight(float x, float z) noexcept {
    float fbm = 0.0f;
    float amplitude = 1.0f;
    float frequency = 0.055f;
    for (int octave = 0; octave < 4; ++octave) {
        const float offset = static_cast<float>(octave);
        fbm += amplitude * valueNoise2D(x * frequency + offset * 17.3f, z * frequency - offset * 11.7f);
        frequency *= 2.05f;
        amplitude *= 0.5f;
    }

    const float r1 = 1.0f - std::abs(valueNoise2D(x * 0.055f + 13.7f, z * 0.055f - 9.2f));
    const float r2 = 1.0f - std::abs(valueNoise2D(x * 0.110f - 4.3f, z * 0.110f + 7.8f));
    const float ridgeBase = r1 * 0.7f + r2 * 0.3f;
    const float ridge = ridgeBase * ridgeBase;

    const float radialDist = std::sqrt(x * x + z * z);
    const float lakeDist = std::sqrt((x * 0.88f) * (x * 0.88f) + (z * 1.12f) * (z * 1.12f)) +
                           2.4f * valueNoise2D(x * 0.085f + 5.2f, z * 0.085f - 3.4f);
    const float lakeDip = 3.90f * std::exp(-(lakeDist * lakeDist) / (14.5f * 14.5f));

    const float base = 1.50f + 1.60f * fbm + 0.45f * std::sin(x * 0.12f) * std::cos(z * 0.10f);

    const float tRim = std::clamp((radialDist - 23.0f) / 22.0f, 0.0f, 1.0f);
    const float rimMask = tRim * tRim * (3.0f - 2.0f * tRim);
    const float mountainShape = 0.55f + 0.45f * std::cos(x * 0.068f - 0.6f) * std::sin(z * 0.072f + 1.1f);
    const float mountain = rimMask * (3.2f + 10.2f * ridge * (0.42f + 0.88f * mountainShape));

    return base - lakeDip + mountain;
}

Mesh Mesh::createTerrain(const Context& context, uint32_t gridSize, float worldSize) {
    const uint32_t cells = (std::max)(gridSize, 1u);
    const uint32_t vertsPerSide = cells + 1u;
    const float halfSize = worldSize * 0.5f;
    const float step = worldSize / static_cast<float>(cells);
    const float invCells = 1.0f / static_cast<float>(cells);

    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<size_t>(vertsPerSide) * static_cast<size_t>(vertsPerSide));

    for (uint32_t row = 0; row < vertsPerSide; ++row) {
        const float z = -halfSize + static_cast<float>(row) * step;
        const float v = static_cast<float>(row) * invCells;

        for (uint32_t col = 0; col < vertsPerSide; ++col) {
            const float x = -halfSize + static_cast<float>(col) * step;
            const float u = static_cast<float>(col) * invCells;
            const float y = sampleTerrainHeight(x, z);

            vertices.push_back({
                {x, y, z},
                computeTerrainColor(x, y, z),
                {u, v}
            });
        }
    }

    std::vector<uint32_t> indices;
    buildGridIndices(cells, indices);

    return Mesh(context, vertices, indices, "Terrain Mesh");
}

Mesh Mesh::createWaterPlane(
    const Context& context,
    uint32_t gridSize,
    float worldSize,
    float waterLevel) {
    const uint32_t cells = (std::max)(gridSize, 1u);
    const uint32_t vertsPerSide = cells + 1u;
    const float halfSize = worldSize * 0.5f;
    const float step = worldSize / static_cast<float>(cells);
    const float invCells = 1.0f / static_cast<float>(cells);

    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<size_t>(vertsPerSide) * static_cast<size_t>(vertsPerSide));

    for (uint32_t row = 0; row < vertsPerSide; ++row) {
        const float z = -halfSize + static_cast<float>(row) * step;
        const float v = static_cast<float>(row) * invCells;

        for (uint32_t col = 0; col < vertsPerSide; ++col) {
            const float x = -halfSize + static_cast<float>(col) * step;
            const float u = static_cast<float>(col) * invCells;

            const float distNorm = std::clamp(std::sqrt(x * x + z * z) / (halfSize * 0.65f), 0.0f, 1.0f);
            const float shimmer = valueNoise2D(x * 0.18f - 3.1f, z * 0.18f + 4.7f) * 0.03f;
            const glm::vec3 color = glm::clamp(
                glm::mix(glm::vec3(0.08f, 0.42f, 0.62f), glm::vec3(0.18f, 0.62f, 0.74f), distNorm) +
                    glm::vec3(shimmer * 0.4f, shimmer * 0.8f, shimmer),
                0.0f,
                1.0f);

            vertices.push_back({
                {x, waterLevel, z},
                color,
                {u, v}
            });
        }
    }

    std::vector<uint32_t> indices;
    buildGridIndices(cells, indices);

    return Mesh(context, vertices, indices, "Water Plane Mesh");
}

Mesh Mesh::createSkyDome(const Context& context, float radius) {
    constexpr uint32_t rings = 16u;
    constexpr uint32_t segments = 32u;
    constexpr float pi = std::numbers::pi_v<float>;
    constexpr float twoPi = 2.0f * pi;

    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<size_t>(rings + 1u) * static_cast<size_t>(segments + 1u));

    for (uint32_t ring = 0; ring <= rings; ++ring) {
        const float v = static_cast<float>(ring) / static_cast<float>(rings);
        const float phi = v * pi;
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);

        glm::vec3 skyColor{};
        if (cosPhi >= 0.0f) {
            const float skyT = std::pow(std::clamp(cosPhi, 0.0f, 1.0f), 0.65f);
            skyColor = glm::mix(glm::vec3(0.68f, 0.83f, 0.96f), glm::vec3(0.14f, 0.35f, 0.72f), skyT);
        } else {
            const float nadirT = std::clamp(-cosPhi, 0.0f, 1.0f);
            skyColor = glm::mix(glm::vec3(0.68f, 0.83f, 0.96f), glm::vec3(0.22f, 0.34f, 0.48f), nadirT);
        }

        for (uint32_t seg = 0; seg <= segments; ++seg) {
            const float u = static_cast<float>(seg) / static_cast<float>(segments);
            const float theta = u * twoPi;

            const float x = radius * sinPhi * std::cos(theta);
            const float y = radius * cosPhi;
            const float z = radius * sinPhi * std::sin(theta);

            vertices.push_back({{x, y, z}, skyColor, {u, v}});
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(rings) * static_cast<size_t>(segments) * 6u);
    const uint32_t stride = segments + 1u;

    for (uint32_t ring = 0; ring < rings; ++ring) {
        for (uint32_t seg = 0; seg < segments; ++seg) {
            const uint32_t i00 = ring * stride + seg;
            const uint32_t i10 = i00 + 1u;
            const uint32_t i01 = (ring + 1u) * stride + seg;
            const uint32_t i11 = i01 + 1u;

            // Inward-facing CCW winding so the dome interior is visible under VK_CULL_MODE_BACK_BIT.
            if (ring != 0u) {
                indices.push_back(i00);
                indices.push_back(i01);
                indices.push_back(i10);
            }
            if (ring != rings - 1u) {
                indices.push_back(i10);
                indices.push_back(i01);
                indices.push_back(i11);
            }
        }
    }

    return Mesh(context, vertices, indices, "Sky Dome Mesh");
}

Mesh Mesh::loadFromObj(
    const Context& context,
    std::string_view objPath,
    const glm::vec3& defaultColor) {
    const std::filesystem::path resolvedPath = resolveObjPath(objPath);
    if (resolvedPath.empty()) {
        std::cerr << "[WARN] OBJ file not found ('" << objPath
                  << "'); generating fallback low-poly crystal mesh." << std::endl;
        return createFallbackCrystal(context, defaultColor);
    }

    tinyobj::ObjReaderConfig readerConfig;
    readerConfig.triangulate = true;
    readerConfig.vertex_color = true;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(resolvedPath.string(), readerConfig)) {
        if (!reader.Error().empty()) {
            std::cerr << "[WARN] Failed to parse OBJ '" << resolvedPath.string()
                      << "': " << reader.Error();
        }
        return createFallbackCrystal(context, defaultColor);
    }

    if (!reader.Warning().empty()) {
        std::cerr << "[WARN] OBJ warning ('" << resolvedPath.string()
                  << "'): " << reader.Warning();
    }

    const tinyobj::attrib_t& attrib = reader.GetAttrib();
    const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::unordered_map<ObjVertexKey, uint32_t, ObjVertexKeyHash> uniqueVertices;

    for (const auto& shape : shapes) {
        indices.reserve(indices.size() + shape.mesh.indices.size());

        for (const auto& index : shape.mesh.indices) {
            if (index.vertex_index < 0) {
                continue;
            }

            const size_t posBase = static_cast<size_t>(index.vertex_index) * 3u;
            if (posBase + 2u >= attrib.vertices.size()) {
                continue;
            }

            const int normalizedUvIndex = (index.texcoord_index >= 0) ? index.texcoord_index : -1;
            const ObjVertexKey key{index.vertex_index, normalizedUvIndex};

            auto [it, inserted] = uniqueVertices.emplace(key, static_cast<uint32_t>(vertices.size()));
            if (inserted) {
                Vertex vertex{};
                vertex.pos = {
                    static_cast<float>(attrib.vertices[posBase + 0u]),
                    static_cast<float>(attrib.vertices[posBase + 1u]),
                    static_cast<float>(attrib.vertices[posBase + 2u])
                };

                if (posBase + 2u < attrib.colors.size()) {
                    vertex.color = {
                        static_cast<float>(attrib.colors[posBase + 0u]),
                        static_cast<float>(attrib.colors[posBase + 1u]),
                        static_cast<float>(attrib.colors[posBase + 2u])
                    };
                } else {
                    vertex.color = defaultColor;
                }

                if (normalizedUvIndex >= 0) {
                    const size_t uvBase = static_cast<size_t>(normalizedUvIndex) * 2u;
                    if (uvBase + 1u < attrib.texcoords.size()) {
                        vertex.uv = {
                            static_cast<float>(attrib.texcoords[uvBase + 0u]),
                            1.0f - static_cast<float>(attrib.texcoords[uvBase + 1u])
                        };
                    }
                }

                vertices.push_back(vertex);
            }

            indices.push_back(it->second);
        }
    }

    if (vertices.empty() || indices.empty()) {
        std::cerr << "[WARN] OBJ '" << resolvedPath.string()
                  << "' contained no valid triangles; generating fallback crystal mesh." << std::endl;
        return createFallbackCrystal(context, defaultColor);
    }

    const std::string debugName = "OBJ Mesh (" + resolvedPath.filename().string() + ")";
    return Mesh(context, vertices, indices, debugName.c_str());
}
