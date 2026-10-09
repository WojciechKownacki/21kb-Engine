#include "engine/navigation/NavGeometryCollector.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/CollisionMeshAsset.hpp"
#include "engine/assets/TerrainAsset.hpp"
#include "engine/assets/TerrainAssetIO.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/ScenePrefabNode.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace kb::navigation {
namespace {

// Column-major 3x3 linear part followed by the translation.
struct Affine {
    std::array<double, 9U> linear{ 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 };
    kb::math::DVec3 translation{};
};

[[nodiscard]] Affine LocalAffine(const kb::scene::ScenePrefabNodeDesc& node) {
    double x = node.transform.localRotation.x;
    double y = node.transform.localRotation.y;
    double z = node.transform.localRotation.z;
    double w = node.transform.localRotation.w;
    const double length = std::sqrt(x * x + y * y + z * z + w * w);
    if (length > 1e-12) {
        x /= length; y /= length; z /= length; w /= length;
    } else {
        x = 0.0; y = 0.0; z = 0.0; w = 1.0;
    }
    const double sx = node.transform.localScale.x;
    const double sy = node.transform.localScale.y;
    const double sz = node.transform.localScale.z;
    Affine affine;
    affine.linear = {
        (1.0 - 2.0 * (y * y + z * z)) * sx, (2.0 * (x * y + z * w)) * sx, (2.0 * (x * z - y * w)) * sx,
        (2.0 * (x * y - z * w)) * sy, (1.0 - 2.0 * (x * x + z * z)) * sy, (2.0 * (y * z + x * w)) * sy,
        (2.0 * (x * z + y * w)) * sz, (2.0 * (y * z - x * w)) * sz, (1.0 - 2.0 * (x * x + y * y)) * sz,
    };
    affine.translation = node.LocalTranslation();
    return affine;
}

[[nodiscard]] kb::math::DVec3 ApplyLinear(const std::array<double, 9U>& m, double x, double y, double z) noexcept {
    return { m[0] * x + m[3] * y + m[6] * z, m[1] * x + m[4] * y + m[7] * z, m[2] * x + m[5] * y + m[8] * z };
}

[[nodiscard]] Affine Compose(const Affine& parent, const Affine& local) {
    Affine out;
    for (int column = 0; column < 3; ++column) {
        const kb::math::DVec3 axis = ApplyLinear(parent.linear, local.linear[column * 3], local.linear[column * 3 + 1], local.linear[column * 3 + 2]);
        out.linear[column * 3] = axis.x;
        out.linear[column * 3 + 1] = axis.y;
        out.linear[column * 3 + 2] = axis.z;
    }
    out.translation = parent.translation + ApplyLinear(parent.linear, local.translation.x, local.translation.y, local.translation.z);
    return out;
}

// Mesh-space triangles placed by `world`: the chunk's origin is the node's precise position and its
// vertices the linear part applied in double precision.
void AddPlaced(const Affine& world, std::span<const float> vertices, std::span<const std::uint32_t> indices, NavGeometry& geometry) {
    NavGeometryChunk chunk;
    chunk.origin = world.translation;
    chunk.vertices.reserve(vertices.size());
    for (std::size_t vertex = 0U; vertex + 2U < vertices.size(); vertex += 3U) {
        const kb::math::DVec3 placed = ApplyLinear(world.linear, vertices[vertex], vertices[vertex + 1U], vertices[vertex + 2U]);
        chunk.vertices.push_back(static_cast<float>(placed.x));
        chunk.vertices.push_back(static_cast<float>(placed.y));
        chunk.vertices.push_back(static_cast<float>(placed.z));
    }
    chunk.indices.assign(indices.begin(), indices.end());
    geometry.Add(std::move(chunk));
}

void AppendBox(kb::math::Vec3 center, kb::math::Vec3 size, std::vector<float>& vertices, std::vector<std::uint32_t>& indices) {
    const float hx = std::fabs(size.x) * 0.5F;
    const float hy = std::fabs(size.y) * 0.5F;
    const float hz = std::fabs(size.z) * 0.5F;
    for (int corner = 0; corner < 8; ++corner) {
        vertices.push_back(center.x + ((corner & 1) != 0 ? hx : -hx));
        vertices.push_back(center.y + ((corner & 2) != 0 ? hy : -hy));
        vertices.push_back(center.z + ((corner & 4) != 0 ? hz : -hz));
    }
    constexpr std::array<std::uint32_t, 36U> kFaces{
        0, 1, 3, 0, 3, 2,  4, 6, 7, 4, 7, 5,  0, 4, 5, 0, 5, 1,
        2, 3, 7, 2, 7, 6,  0, 2, 6, 0, 6, 4,  1, 5, 7, 1, 7, 3,
    };
    indices.assign(kFaces.begin(), kFaces.end());
}

// A capsule along local Y (height includes both caps); a sphere is a capsule without a middle.
void AppendCapsule(kb::math::Vec3 center, float radius, float height, std::vector<float>& vertices, std::vector<std::uint32_t>& indices) {
    constexpr int kSegments = 16;
    constexpr int kCapRings = 4;
    const float r = std::fabs(radius);
    const float halfMiddle = std::max(0.0F, std::fabs(height) * 0.5F - r);
    constexpr float kPi = 3.14159265358979F;
    // Rings from the top pole down to the bottom pole.
    std::vector<std::pair<float, float>> rings;
    rings.emplace_back(halfMiddle + r, 0.0F);
    for (int ring = 1; ring <= kCapRings; ++ring) {
        const float angle = (kPi * 0.5F) * static_cast<float>(ring) / kCapRings;
        rings.emplace_back(halfMiddle + r * std::cos(angle), r * std::sin(angle));
    }
    for (int ring = kCapRings - 1; ring >= 1; --ring) {
        const float angle = (kPi * 0.5F) * static_cast<float>(ring) / kCapRings;
        rings.emplace_back(-halfMiddle - r * std::cos(angle), r * std::sin(angle));
    }
    if (halfMiddle > 0.0F) {
        rings.insert(rings.begin() + kCapRings + 1, std::pair{ -halfMiddle, r });
    }
    rings.emplace_back(-halfMiddle - r, 0.0F);
    for (const auto& [y, ringRadius] : rings) {
        for (int segment = 0; segment < kSegments; ++segment) {
            const float angle = 2.0F * kPi * static_cast<float>(segment) / kSegments;
            vertices.push_back(center.x + ringRadius * std::cos(angle));
            vertices.push_back(center.y + y);
            vertices.push_back(center.z + ringRadius * std::sin(angle));
        }
    }
    for (std::uint32_t ring = 0U; ring + 1U < rings.size(); ++ring) {
        for (std::uint32_t segment = 0U; segment < kSegments; ++segment) {
            const std::uint32_t next = (segment + 1U) % kSegments;
            const std::uint32_t a = ring * kSegments + segment;
            const std::uint32_t b = ring * kSegments + next;
            const std::uint32_t c = (ring + 1U) * kSegments + segment;
            const std::uint32_t d = (ring + 1U) * kSegments + next;
            indices.insert(indices.end(), { a, b, c, b, d, c });
        }
    }
}

[[nodiscard]] std::shared_ptr<const NavSourceMesh> FromCollisionMesh(const kb::assets::CollisionMeshAsset& mesh) {
    auto out = std::make_shared<NavSourceMesh>();
    out->vertices.reserve(mesh.positions.size() * 3U);
    for (const kb::math::Vec3& position : mesh.positions) {
        out->vertices.push_back(position.x);
        out->vertices.push_back(position.y);
        out->vertices.push_back(position.z);
    }
    out->indices = mesh.indices;
    return out;
}

} // namespace

AssetNavGeometrySource::AssetNavGeometrySource(kb::assets::AssetManager& assets) noexcept
    : assets_(assets) {}

std::shared_ptr<const NavSourceMesh> AssetNavGeometrySource::TerrainMesh(std::uint64_t assetId) {
    const kb::assets::AssetMetadata* metadata = assets_.Registry().Find(kb::assets::AssetId{ assetId });
    if (metadata == nullptr) {
        return nullptr;
    }
    const std::string extension = metadata->sourceExtension.empty() ? metadata->virtualPath.extension().string() : metadata->sourceExtension;
    if (extension != kb::assets::kTerrainAssetExtension || metadata->physicalPath.empty()) {
        return nullptr;
    }
    const std::optional<kb::assets::TerrainAsset> terrain = kb::assets::TerrainAssetIO::Load(metadata->physicalPath);
    if (!terrain.has_value()) {
        return nullptr;
    }
    std::string error;
    const std::optional<kb::assets::CollisionMeshAsset> heightfield = kb::assets::BuildTerrainCollisionMesh(*terrain, error);
    return heightfield.has_value() ? FromCollisionMesh(*heightfield) : nullptr;
}

std::shared_ptr<const NavSourceMesh> AssetNavGeometrySource::RenderMesh(std::uint64_t assetId) {
    const auto cached = renderMeshes_.find(assetId);
    if (cached != renderMeshes_.end()) {
        return cached->second;
    }
    std::shared_ptr<const NavSourceMesh> mesh = TerrainMesh(assetId);
    renderMeshes_.emplace(assetId, mesh);
    return mesh;
}

std::shared_ptr<const NavSourceMesh> AssetNavGeometrySource::CollisionMesh(std::uint64_t assetId) {
    const auto cached = collisionMeshes_.find(assetId);
    if (cached != collisionMeshes_.end()) {
        return cached->second;
    }
    std::shared_ptr<const NavSourceMesh> mesh;
    const kb::assets::AssetHandle<kb::assets::CollisionMeshAsset> handle =
        assets_.Load<kb::assets::CollisionMeshAsset>(kb::assets::AssetId{ assetId });
    if (handle.IsLoaded()) {
        mesh = FromCollisionMesh(*handle.Shared());
    }
    collisionMeshes_.emplace(assetId, mesh);
    return mesh;
}

void CollectNavGeometry(std::span<const kb::scene::ScenePrefabNodeDesc> nodes, const NavMeshBuildSettings& settings,
    INavGeometrySource* source, NavGeometry& geometry, NavGeometryCollectStats& stats) {
    std::vector<Affine> world(nodes.size());
    std::vector<bool> moving(nodes.size(), false);
    std::vector<bool> visible(nodes.size(), true);
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        const kb::scene::ScenePrefabNodeDesc& node = nodes[index];
        const kb::scene::ScenePrefabNodeComponents& components = node.components;
        const Affine local = LocalAffine(node);
        const bool hidden = node.visibility.mode == kb::scene::VisibilityMode::Hidden || !node.visibility.visible;
        const bool ownMotion = components.rigidbody.has_value() && components.rigidbody->bodyType != kb::scene::RigidbodyBodyType::Static;
        if (node.parentNode == kb::scene::ScenePrefabNodeDesc::NoParent || node.parentNode >= index) {
            world[index] = local;
            moving[index] = ownMotion;
            visible[index] = !hidden;
        } else {
            world[index] = Compose(world[node.parentNode], local);
            moving[index] = moving[node.parentNode] || ownMotion;
            visible[index] = visible[node.parentNode] && !hidden;
        }
        if (moving[index] || components.navAgent.has_value() || components.navObstacle.has_value() || components.characterController.has_value()) {
            continue;
        }
        if (settings.colliders && components.collider.has_value() && !components.collider->trigger) {
            const kb::scene::ColliderComponent& collider = *components.collider;
            vertices.clear();
            indices.clear();
            switch (collider.shape) {
            case kb::scene::ColliderShape::Box:
                AppendBox(collider.center, collider.boxSize, vertices, indices);
                break;
            case kb::scene::ColliderShape::Sphere:
                AppendCapsule(collider.center, collider.radius, 0.0F, vertices, indices);
                break;
            case kb::scene::ColliderShape::Capsule:
                AppendCapsule(collider.center, collider.radius, collider.height, vertices, indices);
                break;
            case kb::scene::ColliderShape::Mesh:
                if (const std::shared_ptr<const NavSourceMesh> mesh = source != nullptr && collider.meshAssetId != 0U
                        ? source->CollisionMesh(collider.meshAssetId) : nullptr) {
                    vertices = mesh->vertices;
                    indices = mesh->indices;
                } else {
                    ++stats.unresolved;
                }
                break;
            }
            if (!indices.empty()) {
                AddPlaced(world[index], vertices, indices, geometry);
                ++stats.colliders;
            }
        }
        if (settings.renderMeshes && visible[index] && components.meshRenderer.has_value() && components.meshRenderer->meshAssetId != 0U) {
            const std::shared_ptr<const NavSourceMesh> mesh = source != nullptr ? source->RenderMesh(components.meshRenderer->meshAssetId) : nullptr;
            if (mesh != nullptr && !mesh->indices.empty()) {
                AddPlaced(world[index], mesh->vertices, mesh->indices, geometry);
                ++stats.meshes;
            } else {
                ++stats.unresolved;
            }
        }
    }
}

NavSceneBakeResult BakeSceneNavMesh(const std::filesystem::path& scenePath, const NavMeshBuildSettings& settings, INavGeometrySource* source) {
    NavSceneBakeResult result;
    const kb::scene::SceneDocumentLoadResult loaded = kb::scene::SceneDocumentService::Load(scenePath);
    if (!loaded.succeeded) {
        result.error = "the scene " + scenePath.generic_string() + " could not be read: " + loaded.error;
        return result;
    }
    NavGeometry geometry;
    CollectNavGeometry(loaded.document.worldPrefab.Nodes(), settings, source, geometry, result.geometry);
    // Tiles are built on the engine's workers (at most 8: each holds a tile's voxels while it runs).
    kb::ecs::WorkerPool workers{ kb::ecs::WorkerPoolConfig{ .workerCount = std::min<std::size_t>(8U, kb::ecs::WorkerPool::DefaultWorkerCount()) } };
    NavMeshBakeResult baked = NavMeshBuilder::Bake(settings, geometry, &workers);
    if (!baked.succeeded) {
        result.error = std::move(baked.error);
        return result;
    }
    result.asset.settings = settings;
    result.asset.tiles = std::move(baked.tiles);
    result.stats = baked.stats;
    result.succeeded = true;
    return result;
}

std::filesystem::path SceneNavMeshPath(const std::filesystem::path& scenePath) {
    std::filesystem::path path = scenePath;
    path.replace_extension(NavMeshAsset::Extension);
    return path;
}

NavMeshBuildSettings SceneNavMeshSettings(const std::filesystem::path& scenePath) {
    std::error_code code;
    const std::filesystem::path navMesh = SceneNavMeshPath(scenePath);
    if (!std::filesystem::is_regular_file(navMesh, code)) {
        return {};
    }
    NavMeshAssetReadResult read = NavMeshAssetIO::Read(navMesh);
    return read.succeeded ? std::move(read.asset.settings) : NavMeshBuildSettings{};
}

} // namespace kb::navigation
