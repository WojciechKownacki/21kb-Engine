#include "rendering/EditorMeshThumbnailService.hpp"

#include "engine/assets/IAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace kb::editor {
namespace {

constexpr int kThumbnailSize = 128;
constexpr float kPi = 3.14159265358979323846F;

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct ProjectedVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    Vec3 world{};
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

[[nodiscard]] Vec3 Add(Vec3 left, Vec3 right) noexcept {
    return Vec3{ left.x + right.x, left.y + right.y, left.z + right.z };
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

[[nodiscard]] bool HasKnownMeshExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension == ".21kb"
        || extension == ".obj"
        || extension == ".gltf"
        || extension == ".glb"
        || extension == ".fbx";
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

[[nodiscard]] Vec3 RotateForThumbnail(Vec3 position) noexcept {
    constexpr float yaw = -35.0F * kPi / 180.0F;
    constexpr float pitch = 24.0F * kPi / 180.0F;
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

[[nodiscard]] std::vector<ProjectedVertex> ProjectVertices(std::span<const Vec3> positions, Vec3 center, float radius) {
    std::vector<ProjectedVertex> projected;
    projected.reserve(positions.size());

    constexpr float fit = static_cast<float>(kThumbnailSize) * 0.40F;
    const float invRadius = 1.0F / std::max(radius, 0.0001F);
    for (Vec3 position : positions) {
        const Vec3 rotated = RotateForThumbnail(Subtract(position, center));
        projected.push_back(ProjectedVertex{
            .x = static_cast<float>(kThumbnailSize) * 0.5F + rotated.x * invRadius * fit,
            .y = static_cast<float>(kThumbnailSize) * 0.52F - rotated.y * invRadius * fit,
            .z = rotated.z * invRadius,
            .world = rotated,
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

void PaintBackground(EditorMeshThumbnailImage& image) {
    image.width = kThumbnailSize;
    image.height = kThumbnailSize;
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
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const float dx = (static_cast<float>(x) - centerX) / 44.0F;
            const float dy = (static_cast<float>(y) - centerY) / 13.0F;
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
    const ProjectedVertex& c) {
    const float area = Edge(a, b, c.x, c.y);
    if (std::abs(area) <= 0.0001F) {
        return;
    }

    Vec3 normal = Normalize(Cross(Subtract(b.world, a.world), Subtract(c.world, a.world)));
    if (normal.z < 0.0F) {
        normal = Vec3{ -normal.x, -normal.y, -normal.z };
    }

    constexpr Vec3 light = Vec3{ -0.28F, 0.48F, 0.83F };
    const float lambert = std::clamp(Dot(normal, Normalize(light)), 0.0F, 1.0F);
    const float shade = 0.34F + lambert * 0.66F;
    const std::uint8_t r = ClampByte(126.0F * shade + 38.0F);
    const std::uint8_t g = ClampByte(148.0F * shade + 40.0F);
    const std::uint8_t bColor = ClampByte(178.0F * shade + 42.0F);

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

            depth[offset] = z;
            image.bgra[offset] = PackBgra(r, g, bColor);
        }
    }
}

void RasterizeMesh(EditorMeshThumbnailImage& image, const kb::render::RenderMeshAssetData& mesh, std::span<const Vec3> positions) {
    const std::optional<std::pair<Vec3, float>> bounds = ResolveBounds(positions, mesh.bounds);
    if (!bounds.has_value()) {
        return;
    }

    const std::vector<ProjectedVertex> projected = ProjectVertices(positions, bounds->first, bounds->second);
    std::vector<float> depth(static_cast<std::size_t>(image.width * image.height), -std::numeric_limits<float>::max());
    const std::size_t indexCount = IndexCount(mesh, positions.size());
    if (indexCount < 3U) {
        return;
    }

    const std::size_t triangleCount = indexCount / 3U;
    constexpr std::size_t maxThumbnailTriangles = 200000U;
    const std::size_t step = std::max<std::size_t>(1U, triangleCount / maxThumbnailTriangles);
    for (std::size_t triangle = 0; triangle < triangleCount; triangle += step) {
        const std::size_t base = triangle * 3U;
        const std::uint32_t ia = IndexAt(mesh, base);
        const std::uint32_t ib = IndexAt(mesh, base + 1U);
        const std::uint32_t ic = IndexAt(mesh, base + 2U);
        if (ia >= projected.size() || ib >= projected.size() || ic >= projected.size()) {
            continue;
        }
        RasterizeTriangle(image, depth, projected[ia], projected[ib], projected[ic]);
    }
}

[[nodiscard]] std::optional<kb::render::RenderMeshAssetData> LoadMesh(const kb::assets::AssetMetadata& metadata) {
    if (metadata.physicalPath.empty()) {
        return std::nullopt;
    }

    kb::render::RenderMeshAssetLoader loader;
    kb::assets::AssetLoadRequest request{
        .metadata = metadata,
        .resolvedPath = metadata.physicalPath,
    };
    kb::assets::AssetLoadResult result = loader.Load(request);
    if (!result.Succeeded() || result.asset == nullptr) {
        return std::nullopt;
    }

    std::shared_ptr<kb::render::RenderMeshAssetData> mesh = std::static_pointer_cast<kb::render::RenderMeshAssetData>(result.asset);
    if (mesh == nullptr) {
        return std::nullopt;
    }
    return *mesh;
}

} // namespace

const EditorMeshThumbnailImage* EditorMeshThumbnailService::ThumbnailFor(const kb::assets::AssetMetadata& metadata) {
    if (!IsMeshAsset(metadata)) {
        return nullptr;
    }

    const auto existing = entries_.find(metadata.id.value);
    if (existing == entries_.end() || existing->second.contentHash != metadata.contentHash) {
        Entry entry;
        entry.contentHash = metadata.contentHash;
        if (std::optional<EditorMeshThumbnailImage> image = RenderThumbnail(metadata)) {
            entry.state = EntryState::Ready;
            entry.image = std::move(*image);
        } else {
            entry.state = EntryState::Failed;
            entry.image = {};
        }
        auto [inserted, _] = entries_.insert_or_assign(metadata.id.value, std::move(entry));
        static_cast<void>(inserted);
        ++revision_;
    }

    const Entry& entry = entries_.find(metadata.id.value)->second;
    return entry.state == EntryState::Ready ? &entry.image : nullptr;
}

std::uint64_t EditorMeshThumbnailService::Revision() const noexcept {
    return revision_;
}

void EditorMeshThumbnailService::Clear() noexcept {
    entries_.clear();
    ++revision_;
}

bool EditorMeshThumbnailService::IsMeshAsset(const kb::assets::AssetMetadata& metadata) {
    return metadata.type == "RenderMesh"
        || metadata.importCategory == "Mesh"
        || HasKnownMeshExtension(metadata.physicalPath)
        || HasKnownMeshExtension(metadata.virtualPath);
}

std::optional<EditorMeshThumbnailImage> EditorMeshThumbnailService::RenderThumbnail(const kb::assets::AssetMetadata& metadata) {
    std::optional<kb::render::RenderMeshAssetData> mesh = LoadMesh(metadata);
    if (!mesh.has_value()) {
        return std::nullopt;
    }

    std::vector<Vec3> positions = MeshPositions(*mesh);
    if (positions.size() < 3U) {
        return std::nullopt;
    }

    EditorMeshThumbnailImage image;
    PaintBackground(image);
    RasterizeMesh(image, *mesh, positions);
    return image;
}

} // namespace kb::editor
