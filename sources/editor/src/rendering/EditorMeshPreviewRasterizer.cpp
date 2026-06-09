#include "rendering/EditorMeshPreviewRasterizer.hpp"

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace kb::editor {
namespace {

constexpr float kPi = 3.14159265358979323846F;

using Vec3 = EditorMeshPreviewVector3;

struct ProjectedVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    Vec3 world{};
    Vec3 normal{};
};

struct ProjectedBounds {
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
    int centerX = 0;
    int centerY = 0;
    int radius = 0;
};

struct LightRig {
    Vec3 keyLight{};
    Vec3 fillLight{};
    float ambient = 0.28F;
    float keyStrength = 0.62F;
    float fillStrength = 0.16F;
    float rimStrength = 0.22F;
};

[[nodiscard]] std::uint32_t PackBgra(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    return static_cast<std::uint32_t>(b)
        | (static_cast<std::uint32_t>(g) << 8U)
        | (static_cast<std::uint32_t>(r) << 16U)
        | (0xFFU << 24U);
}

[[nodiscard]] std::uint8_t ClampByte(float value) noexcept {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0F, 255.0F));
}

[[nodiscard]] Vec3 Subtract(Vec3 left, Vec3 right) noexcept {
    return Vec3{ left.x - right.x, left.y - right.y, left.z - right.z };
}

[[nodiscard]] Vec3 Cross(Vec3 left, Vec3 right) noexcept {
    return Vec3{
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] float Dot(Vec3 left, Vec3 right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] Vec3 Normalize(Vec3 value) noexcept {
    const float length = std::sqrt(Dot(value, value));
    if (length <= std::numeric_limits<float>::epsilon()) {
        return Vec3{};
    }
    return Vec3{ value.x / length, value.y / length, value.z / length };
}

[[nodiscard]] LightRig ResolveLightRig(EditorMeshPreviewLightPreset preset) noexcept {
    switch (preset) {
    case EditorMeshPreviewLightPreset::Front:
        return LightRig{
            .keyLight = Normalize(Vec3{ 0.0F, 0.08F, 1.0F }),
            .fillLight = Normalize(Vec3{ -0.34F, -0.18F, 0.72F }),
            .ambient = 0.34F,
            .keyStrength = 0.70F,
            .fillStrength = 0.08F,
            .rimStrength = 0.08F,
        };
    case EditorMeshPreviewLightPreset::Rim:
        return LightRig{
            .keyLight = Normalize(Vec3{ 0.58F, 0.26F, 0.58F }),
            .fillLight = Normalize(Vec3{ -0.45F, -0.15F, 0.45F }),
            .ambient = 0.20F,
            .keyStrength = 0.42F,
            .fillStrength = 0.10F,
            .rimStrength = 0.56F,
        };
    case EditorMeshPreviewLightPreset::Studio:
    default:
        return LightRig{
            .keyLight = Normalize(Vec3{ -0.36F, 0.48F, 0.80F }),
            .fillLight = Normalize(Vec3{ 0.54F, -0.18F, 0.58F }),
            .ambient = 0.28F,
            .keyStrength = 0.62F,
            .fillStrength = 0.16F,
            .rimStrength = 0.22F,
        };
    }
}

[[nodiscard]] std::vector<Vec3> MeshPositions(const kb::render::RenderMeshAssetData& mesh) {
    std::vector<Vec3> positions;
    if (!mesh.tangentVertices.empty()) {
        positions.reserve(mesh.tangentVertices.size());
        for (const kb::render::RenderStaticMeshVertexP3N3T4UV2& vertex : mesh.tangentVertices) {
            positions.push_back(Vec3{ vertex.x, vertex.y, vertex.z });
        }
        return positions;
    }

    positions.reserve(mesh.vertices.size());
    for (const kb::render::RenderStaticMeshVertexP3N3UV2& vertex : mesh.vertices) {
        positions.push_back(Vec3{ vertex.x, vertex.y, vertex.z });
    }
    return positions;
}

[[nodiscard]] std::vector<Vec3> MeshNormals(const kb::render::RenderMeshAssetData& mesh) {
    std::vector<Vec3> normals;
    if (!mesh.tangentVertices.empty()) {
        normals.reserve(mesh.tangentVertices.size());
        for (const kb::render::RenderStaticMeshVertexP3N3T4UV2& vertex : mesh.tangentVertices) {
            normals.push_back(Normalize(Vec3{ vertex.nx, vertex.ny, vertex.nz }));
        }
        return normals;
    }

    normals.reserve(mesh.vertices.size());
    for (const kb::render::RenderStaticMeshVertexP3N3UV2& vertex : mesh.vertices) {
        normals.push_back(Normalize(Vec3{ vertex.nx, vertex.ny, vertex.nz }));
    }
    return normals;
}

[[nodiscard]] std::uint32_t IndexAt(const kb::render::RenderMeshAssetData& mesh, std::size_t offset) noexcept {
    if (!mesh.indices16.empty()) {
        return mesh.indices16[offset];
    }
    if (!mesh.indices32.empty()) {
        return mesh.indices32[offset];
    }
    return static_cast<std::uint32_t>(offset);
}

[[nodiscard]] std::size_t IndexCount(const kb::render::RenderMeshAssetData& mesh, std::size_t vertexCount) noexcept {
    if (!mesh.indices16.empty()) {
        return mesh.indices16.size();
    }
    if (!mesh.indices32.empty()) {
        return mesh.indices32.size();
    }
    return vertexCount;
}

[[nodiscard]] std::vector<std::uint32_t> MeshIndices(const kb::render::RenderMeshAssetData& mesh) {
    std::vector<std::uint32_t> indices;
    if (!mesh.indices16.empty()) {
        indices.reserve(mesh.indices16.size());
        for (std::uint16_t index : mesh.indices16) {
            indices.push_back(index);
        }
        return indices;
    }
    if (!mesh.indices32.empty()) {
        return mesh.indices32;
    }
    return indices;
}

[[nodiscard]] std::optional<std::pair<Vec3, float>> ResolveBounds(std::span<const Vec3> positions, const kb::render::RenderBoundsSphere& bounds) {
    if (bounds.IsValid()) {
        return std::pair<Vec3, float>{
            Vec3{ bounds.center[0], bounds.center[1], bounds.center[2] },
            bounds.radius,
        };
    }

    if (positions.empty()) {
        return std::nullopt;
    }

    Vec3 minimum = positions.front();
    Vec3 maximum = positions.front();
    for (Vec3 position : positions) {
        minimum.x = std::min(minimum.x, position.x);
        minimum.y = std::min(minimum.y, position.y);
        minimum.z = std::min(minimum.z, position.z);
        maximum.x = std::max(maximum.x, position.x);
        maximum.y = std::max(maximum.y, position.y);
        maximum.z = std::max(maximum.z, position.z);
    }

    const Vec3 center{
        (minimum.x + maximum.x) * 0.5F,
        (minimum.y + maximum.y) * 0.5F,
        (minimum.z + maximum.z) * 0.5F,
    };

    float radius = 0.0F;
    for (Vec3 position : positions) {
        const Vec3 delta = Subtract(position, center);
        radius = std::max(radius, std::sqrt(Dot(delta, delta)));
    }
    if (radius <= std::numeric_limits<float>::epsilon()) {
        return std::nullopt;
    }
    return std::pair<Vec3, float>{ center, radius };
}

[[nodiscard]] EditorMeshThumbnailStats BuildStats(const kb::render::RenderMeshAssetData& mesh, std::span<const Vec3> positions) {
    EditorMeshThumbnailStats stats;
    stats.vertexCount = static_cast<std::uint32_t>(positions.size());
    stats.indexCount = static_cast<std::uint32_t>(IndexCount(mesh, positions.size()));
    stats.triangleCount = stats.indexCount / 3U;
    stats.materialSlotCount = static_cast<std::uint32_t>(mesh.materialSlots.size());

    if (const std::optional<std::pair<Vec3, float>> bounds = ResolveBounds(positions, mesh.bounds)) {
        stats.boundsCenter[0] = bounds->first.x;
        stats.boundsCenter[1] = bounds->first.y;
        stats.boundsCenter[2] = bounds->first.z;
        stats.boundsRadius = bounds->second;
    }
    return stats;
}

[[nodiscard]] Vec3 RotateForPreview(Vec3 position, const EditorMeshPreviewSettings& settings) noexcept {
    const float yaw = settings.yawDegrees * kPi / 180.0F;
    const float pitch = settings.pitchDegrees * kPi / 180.0F;
    const float cosYaw = std::cos(yaw);
    const float sinYaw = std::sin(yaw);
    const float cosPitch = std::cos(pitch);
    const float sinPitch = std::sin(pitch);

    const float x = position.x * cosYaw + position.z * sinYaw;
    const float z = -position.x * sinYaw + position.z * cosYaw;
    const float y = position.y * cosPitch - z * sinPitch;
    const float depth = position.y * sinPitch + z * cosPitch;
    return Vec3{ x, y, depth };
}

[[nodiscard]] std::vector<ProjectedVertex> ProjectVertices(
    std::span<const Vec3> positions,
    std::span<const Vec3> normals,
    Vec3 center,
    float radius,
    int size,
    const EditorMeshPreviewSettings& settings) {
    std::vector<ProjectedVertex> projected;
    projected.reserve(positions.size());

    const float fit = static_cast<float>(size) * 0.42F * std::clamp(settings.zoom, 0.45F, 3.0F);
    const float invRadius = 1.0F / std::max(radius, 0.0001F);
    for (std::size_t index = 0; index < positions.size(); ++index) {
        const Vec3 position = positions[index];
        const Vec3 rotated = RotateForPreview(Subtract(position, center), settings);
        Vec3 normal = index < normals.size() ? RotateForPreview(normals[index], settings) : Normalize(rotated);
        normal = Normalize(normal);
        projected.push_back(ProjectedVertex{
            .x = static_cast<float>(size) * 0.5F + rotated.x * invRadius * fit,
            .y = static_cast<float>(size) * 0.52F - rotated.y * invRadius * fit,
            .z = rotated.z * invRadius,
            .world = rotated,
            .normal = normal,
        });
    }
    return projected;
}

void BlendPixel(EditorMeshThumbnailImage& image, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b, float alpha) {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) {
        return;
    }
    const std::size_t offset = static_cast<std::size_t>(y * image.width + x);
    const std::uint32_t original = image.bgra[offset];
    const float invAlpha = 1.0F - alpha;
    const std::uint8_t originalB = static_cast<std::uint8_t>(original & 0xFFU);
    const std::uint8_t originalG = static_cast<std::uint8_t>((original >> 8U) & 0xFFU);
    const std::uint8_t originalR = static_cast<std::uint8_t>((original >> 16U) & 0xFFU);
    image.bgra[offset] = PackBgra(
        ClampByte(static_cast<float>(originalR) * invAlpha + static_cast<float>(r) * alpha),
        ClampByte(static_cast<float>(originalG) * invAlpha + static_cast<float>(g) * alpha),
        ClampByte(static_cast<float>(originalB) * invAlpha + static_cast<float>(b) * alpha));
}

void PaintBackground(EditorMeshThumbnailImage& image, int size) {
    image.width = size;
    image.height = size;
    image.bgra.resize(static_cast<std::size_t>(image.width * image.height));
    for (int y = 0; y < image.height; ++y) {
        const float vertical = static_cast<float>(y) / static_cast<float>(std::max(1, image.height - 1));
        const std::uint8_t r = ClampByte(18.0F + vertical * 7.0F);
        const std::uint8_t g = ClampByte(21.0F + vertical * 8.0F);
        const std::uint8_t b = ClampByte(25.0F + vertical * 10.0F);
        for (int x = 0; x < image.width; ++x) {
            image.bgra[static_cast<std::size_t>(y * image.width + x)] = PackBgra(r, g, b);
        }
    }

    const float centerX = static_cast<float>(image.width) * 0.5F;
    const float centerY = static_cast<float>(image.height) * 0.74F;
    const float shadowWidth = static_cast<float>(image.width) * 0.34F;
    const float shadowHeight = static_cast<float>(image.height) * 0.10F;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const float dx = (static_cast<float>(x) - centerX) / std::max(1.0F, shadowWidth);
            const float dy = (static_cast<float>(y) - centerY) / std::max(1.0F, shadowHeight);
            const float falloff = dx * dx + dy * dy;
            if (falloff < 1.0F) {
                BlendPixel(image, x, y, 4, 6, 9, (1.0F - falloff) * 0.42F);
            }
        }
    }
}

[[nodiscard]] float Edge(const ProjectedVertex& a, const ProjectedVertex& b, float x, float y) noexcept {
    return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}

void RasterizeTriangle(
    EditorMeshThumbnailImage& image,
    std::span<float> depth,
    const ProjectedVertex& a,
    const ProjectedVertex& b,
    const ProjectedVertex& c,
    const LightRig& lightRig,
    bool normalDebug) {
    const float area = Edge(a, b, c.x, c.y);
    if (std::abs(area) <= 0.0001F) {
        return;
    }

    Vec3 faceNormal = Normalize(Cross(Subtract(b.world, a.world), Subtract(c.world, a.world)));
    if (faceNormal.z < 0.0F) {
        faceNormal = Vec3{ -faceNormal.x, -faceNormal.y, -faceNormal.z };
    }

    const Vec3 viewDir = Vec3{ 0.0F, 0.0F, 1.0F };

    const int minX = std::clamp(static_cast<int>(std::floor(std::min({ a.x, b.x, c.x }))), 0, image.width - 1);
    const int maxX = std::clamp(static_cast<int>(std::ceil(std::max({ a.x, b.x, c.x }))), 0, image.width - 1);
    const int minY = std::clamp(static_cast<int>(std::floor(std::min({ a.y, b.y, c.y }))), 0, image.height - 1);
    const int maxY = std::clamp(static_cast<int>(std::ceil(std::max({ a.y, b.y, c.y }))), 0, image.height - 1);

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float px = static_cast<float>(x) + 0.5F;
            const float py = static_cast<float>(y) + 0.5F;
            const float w0 = Edge(b, c, px, py) / area;
            const float w1 = Edge(c, a, px, py) / area;
            const float w2 = Edge(a, b, px, py) / area;
            if (w0 < -0.0002F || w1 < -0.0002F || w2 < -0.0002F) {
                continue;
            }

            const float z = w0 * a.z + w1 * b.z + w2 * c.z;
            const std::size_t offset = static_cast<std::size_t>(y * image.width + x);
            if (z <= depth[offset]) {
                continue;
            }

            Vec3 normal = Normalize(Vec3{
                w0 * a.normal.x + w1 * b.normal.x + w2 * c.normal.x,
                w0 * a.normal.y + w1 * b.normal.y + w2 * c.normal.y,
                w0 * a.normal.z + w1 * b.normal.z + w2 * c.normal.z,
            });
            if (Dot(normal, normal) <= 0.0001F) {
                normal = faceNormal;
            }
            if (normal.z < 0.0F) {
                normal = Vec3{ -normal.x, -normal.y, -normal.z };
            }

            std::uint8_t r = 0;
            std::uint8_t g = 0;
            std::uint8_t bColor = 0;
            if (normalDebug) {
                r = ClampByte((normal.x * 0.5F + 0.5F) * 255.0F);
                g = ClampByte((normal.y * 0.5F + 0.5F) * 255.0F);
                bColor = ClampByte((normal.z * 0.5F + 0.5F) * 255.0F);
            } else {
                const float key = std::max(0.0F, Dot(normal, lightRig.keyLight));
                const float fill = std::max(0.0F, Dot(normal, lightRig.fillLight));
                const float rim = std::pow(std::max(0.0F, 1.0F - std::max(0.0F, Dot(normal, viewDir))), 2.2F);
                const float shade = std::clamp(lightRig.ambient + key * lightRig.keyStrength + fill * lightRig.fillStrength + rim * lightRig.rimStrength, 0.0F, 1.25F);
                r = ClampByte(118.0F * shade + 42.0F);
                g = ClampByte(142.0F * shade + 44.0F);
                bColor = ClampByte(174.0F * shade + 50.0F);
            }

            depth[offset] = z;
            image.bgra[offset] = PackBgra(r, g, bColor);
        }
    }
}

void DrawWireLine(EditorMeshThumbnailImage& image, const ProjectedVertex& a, const ProjectedVertex& b) {
    int x0 = static_cast<int>(std::round(a.x));
    int y0 = static_cast<int>(std::round(a.y));
    const int x1 = static_cast<int>(std::round(b.x));
    const int y1 = static_cast<int>(std::round(b.y));
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    for (;;) {
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                BlendPixel(image, x0 + ox, y0 + oy, 5, 8, 12, 0.70F);
            }
        }
        BlendPixel(image, x0, y0, 230, 244, 255, 1.0F);
        BlendPixel(image, x0 + 1, y0, 132, 200, 255, 0.95F);
        BlendPixel(image, x0, y0 + 1, 132, 200, 255, 0.95F);
        BlendPixel(image, x0 + 1, y0 + 1, 90, 150, 220, 0.75F);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twiceError = 2 * error;
        if (twiceError >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twiceError <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void RasterizeWireTriangle(EditorMeshThumbnailImage& image, const ProjectedVertex& a, const ProjectedVertex& b, const ProjectedVertex& c) {
    DrawWireLine(image, a, b);
    DrawWireLine(image, b, c);
    DrawWireLine(image, c, a);
}

[[nodiscard]] ProjectedBounds ResolveProjectedBounds(std::span<const ProjectedVertex> projected) noexcept {
    ProjectedBounds bounds;
    if (projected.empty()) {
        return bounds;
    }

    float minX = projected.front().x;
    float minY = projected.front().y;
    float maxX = projected.front().x;
    float maxY = projected.front().y;
    for (const ProjectedVertex& vertex : projected) {
        minX = std::min(minX, vertex.x);
        minY = std::min(minY, vertex.y);
        maxX = std::max(maxX, vertex.x);
        maxY = std::max(maxY, vertex.y);
    }

    bounds.minX = static_cast<int>(std::floor(minX));
    bounds.minY = static_cast<int>(std::floor(minY));
    bounds.maxX = static_cast<int>(std::ceil(maxX));
    bounds.maxY = static_cast<int>(std::ceil(maxY));
    bounds.centerX = (bounds.minX + bounds.maxX) / 2;
    bounds.centerY = (bounds.minY + bounds.maxY) / 2;
    bounds.radius = std::max(bounds.maxX - bounds.minX, bounds.maxY - bounds.minY) / 2;
    return bounds;
}

void DrawClippedLine(EditorMeshThumbnailImage& image, int x0, int y0, int x1, int y1, std::uint8_t r, std::uint8_t g, std::uint8_t b, float alpha) {
    ProjectedVertex a{ .x = static_cast<float>(x0), .y = static_cast<float>(y0) };
    ProjectedVertex c{ .x = static_cast<float>(x1), .y = static_cast<float>(y1) };
    int px0 = static_cast<int>(std::round(a.x));
    int py0 = static_cast<int>(std::round(a.y));
    const int px1 = static_cast<int>(std::round(c.x));
    const int py1 = static_cast<int>(std::round(c.y));
    const int dx = std::abs(px1 - px0);
    const int sx = px0 < px1 ? 1 : -1;
    const int dy = -std::abs(py1 - py0);
    const int sy = py0 < py1 ? 1 : -1;
    int error = dx + dy;

    for (;;) {
        BlendPixel(image, px0, py0, r, g, b, alpha);
        if (px0 == px1 && py0 == py1) {
            break;
        }
        const int twiceError = 2 * error;
        if (twiceError >= dy) {
            error += dy;
            px0 += sx;
        }
        if (twiceError <= dx) {
            error += dx;
            py0 += sy;
        }
    }
}

void DrawBoundsOverlay(EditorMeshThumbnailImage& image, std::span<const ProjectedVertex> projected) {
    const ProjectedBounds bounds = ResolveProjectedBounds(projected);
    DrawClippedLine(image, bounds.minX, bounds.minY, bounds.maxX, bounds.minY, 244, 196, 72, 0.95F);
    DrawClippedLine(image, bounds.maxX, bounds.minY, bounds.maxX, bounds.maxY, 244, 196, 72, 0.95F);
    DrawClippedLine(image, bounds.maxX, bounds.maxY, bounds.minX, bounds.maxY, 244, 196, 72, 0.95F);
    DrawClippedLine(image, bounds.minX, bounds.maxY, bounds.minX, bounds.minY, 244, 196, 72, 0.95F);

    const int segments = 96;
    int previousX = bounds.centerX + bounds.radius;
    int previousY = bounds.centerY;
    for (int segment = 1; segment <= segments; ++segment) {
        const float angle = static_cast<float>(segment) * 2.0F * kPi / static_cast<float>(segments);
        const int x = bounds.centerX + static_cast<int>(std::round(std::cos(angle) * static_cast<float>(bounds.radius)));
        const int y = bounds.centerY + static_cast<int>(std::round(std::sin(angle) * static_cast<float>(bounds.radius)));
        DrawClippedLine(image, previousX, previousY, x, y, 244, 196, 72, 0.58F);
        previousX = x;
        previousY = y;
    }
}

void RasterizeGeometry(
    EditorMeshThumbnailImage& image,
    std::span<const Vec3> positions,
    std::span<const Vec3> normals,
    std::span<const std::uint32_t> indices,
    const EditorMeshThumbnailStats& stats,
    const EditorMeshPreviewSettings& settings) {
    if (positions.empty() || stats.boundsRadius <= 0.0F) {
        return;
    }

    const Vec3 center{ stats.boundsCenter[0], stats.boundsCenter[1], stats.boundsCenter[2] };
    const std::vector<ProjectedVertex> projected = ProjectVertices(positions, normals, center, stats.boundsRadius, image.width, settings);
    std::vector<float> depth(static_cast<std::size_t>(image.width * image.height), -std::numeric_limits<float>::max());
    const LightRig lightRig = ResolveLightRig(settings.lightPreset);
    const std::size_t indexCount = indices.empty() ? positions.size() : indices.size();
    const std::size_t triangleCount = indexCount / 3U;
    const bool wireOnly = settings.renderMode == EditorMeshPreviewRenderMode::WireframeOnly;
    const bool drawWire = wireOnly || settings.renderMode == EditorMeshPreviewRenderMode::WireframeOverlay;
    const bool normalDebug = settings.renderMode == EditorMeshPreviewRenderMode::Normals;
    const bool boundsDebug = settings.renderMode == EditorMeshPreviewRenderMode::Bounds;
    const std::size_t maxTriangles = image.width >= kEditorMeshPreviewSize ? 600000U : 180000U;
    const std::size_t step = std::max<std::size_t>(1U, triangleCount / maxTriangles);
    for (std::size_t triangle = 0; triangle < triangleCount; triangle += step) {
        const std::size_t base = triangle * 3U;
        const std::uint32_t ia = indices.empty() ? static_cast<std::uint32_t>(base) : indices[base];
        const std::uint32_t ib = indices.empty() ? static_cast<std::uint32_t>(base + 1U) : indices[base + 1U];
        const std::uint32_t ic = indices.empty() ? static_cast<std::uint32_t>(base + 2U) : indices[base + 2U];
        if (ia >= projected.size() || ib >= projected.size() || ic >= projected.size()) {
            continue;
        }
        if (!wireOnly) {
            RasterizeTriangle(image, depth, projected[ia], projected[ib], projected[ic], lightRig, normalDebug);
        }
    }

    if (drawWire) {
        const std::size_t wireMaxTriangles = image.width >= kEditorMeshPreviewSize ? 140000U : 60000U;
        const std::size_t wireStep = std::max<std::size_t>(1U, triangleCount / wireMaxTriangles);
        for (std::size_t triangle = 0; triangle < triangleCount; triangle += wireStep) {
            const std::size_t base = triangle * 3U;
            const std::uint32_t ia = indices.empty() ? static_cast<std::uint32_t>(base) : indices[base];
            const std::uint32_t ib = indices.empty() ? static_cast<std::uint32_t>(base + 1U) : indices[base + 1U];
            const std::uint32_t ic = indices.empty() ? static_cast<std::uint32_t>(base + 2U) : indices[base + 2U];
            if (ia < projected.size() && ib < projected.size() && ic < projected.size()) {
                RasterizeWireTriangle(image, projected[ia], projected[ib], projected[ic]);
            }
        }
    }

    if (boundsDebug) {
        DrawBoundsOverlay(image, projected);
    }
}

} // namespace

EditorMeshPreviewGeometry EditorMeshPreviewRasterizer::ExtractGeometry(const kb::render::RenderMeshAssetData& mesh) {
    EditorMeshPreviewGeometry geometry;
    geometry.positions = MeshPositions(mesh);
    geometry.normals = MeshNormals(mesh);
    geometry.indices = MeshIndices(mesh);
    geometry.stats = BuildStats(mesh, geometry.positions);
    return geometry;
}

EditorMeshThumbnailImage EditorMeshPreviewRasterizer::Render(
    const kb::render::RenderMeshAssetData& mesh,
    int size,
    const EditorMeshPreviewSettings& settings) {
    return Render(ExtractGeometry(mesh), size, settings);
}

EditorMeshThumbnailImage EditorMeshPreviewRasterizer::Render(
    const EditorMeshPreviewGeometry& geometry,
    int size,
    const EditorMeshPreviewSettings& settings) {
    EditorMeshThumbnailImage image;
    PaintBackground(image, size);
    RasterizeGeometry(image, geometry.positions, geometry.normals, geometry.indices, geometry.stats, settings);
    return image;
}

} // namespace kb::editor
