#include "kb/render/scene/RenderScene.hpp"

#include "engine/assets/TerrainFoliage.hpp"
#include "scene/GeometrySwarmVisibilityClusters.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace kb::render {
namespace {

constexpr float kChunkMeters = kb::assets::kTerrainFoliageChunkMeters;

void HashBytes(std::uint64_t& hash, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0U; index < size; ++index) {
        hash = (hash ^ bytes[index]) * 1099511628211ULL;
    }
}

template <typename T>
void HashValue(std::uint64_t& hash, const T& value) noexcept {
    HashBytes(hash, &value, sizeof(T));
}

[[nodiscard]] std::uint64_t Mix(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

// The samples of a map of `size` samples across `world` metres that cover metres [low, high] from the terrain corner.
[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> SampleRange(float low, float high, float world, std::uint32_t size) noexcept {
    const float scale = static_cast<float>(size - 1U) / world;
    const std::uint32_t first = std::min(static_cast<std::uint32_t>(std::floor(std::max(low, 0.0F) * scale)), size - 1U);
    const std::uint32_t last = std::min(static_cast<std::uint32_t>(std::ceil(std::clamp(high, 0.0F, world) * scale)), size - 1U);
    return { first, std::max(first, last) };
}

// Everything the plants of one chunk are made from: the layer, the density, heights and holes around the chunk (a plant
// stands up to a cell outside it, and its slope reads the heights beside it) and the terrain's model.
[[nodiscard]] std::uint64_t ChunkInputHash(
    const TerrainFoliageRenderProxyDesc& desc, std::uint32_t layerIndex, std::uint32_t chunkX, std::uint32_t chunkZ) noexcept {
    const kb::assets::TerrainAsset& terrain = *desc.terrain;
    const kb::assets::TerrainFoliageLayer& layer = terrain.foliageLayers[layerIndex];
    std::uint64_t hash = 14695981039346656037ULL;
    HashValue(hash, layerIndex);
    HashValue(hash, chunkX);
    HashValue(hash, chunkZ);
    HashValue(hash, desc.model);
    HashValue(hash, desc.layer);
    // Field by field: the layer has padding.
    HashValue(hash, layer.meshAssetId);
    HashValue(hash, layer.materialAssetId);
    HashValue(hash, layer.density);
    HashValue(hash, layer.minScale);
    HashValue(hash, layer.maxScale);
    HashValue(hash, layer.rotationRangeDegrees);
    HashValue(hash, layer.alignToNormal);
    HashValue(hash, layer.fadeStartDistance);
    HashValue(hash, layer.cullDistance);
    HashValue(hash, layer.seed);
    HashValue(hash, layer.castsShadow);
    HashValue(hash, terrain.width);
    HashValue(hash, terrain.height);
    HashValue(hash, terrain.worldSizeX);
    HashValue(hash, terrain.worldSizeZ);
    HashValue(hash, terrain.foliageMapWidth);
    HashValue(hash, terrain.foliageMapHeight);
    const float cell = 1.0F / std::sqrt(layer.density);
    const float marginX = cell + 2.0F * std::max(
        terrain.worldSizeX / static_cast<float>(terrain.width - 1U), terrain.worldSizeX / static_cast<float>(terrain.foliageMapWidth - 1U));
    const float marginZ = cell + 2.0F * std::max(
        terrain.worldSizeZ / static_cast<float>(terrain.height - 1U), terrain.worldSizeZ / static_cast<float>(terrain.foliageMapHeight - 1U));
    const float x0 = static_cast<float>(chunkX) * kChunkMeters - marginX;
    const float x1 = static_cast<float>(chunkX + 1U) * kChunkMeters + marginX;
    const float z0 = static_cast<float>(chunkZ) * kChunkMeters - marginZ;
    const float z1 = static_cast<float>(chunkZ + 1U) * kChunkMeters + marginZ;

    const std::size_t planeSize = static_cast<std::size_t>(terrain.foliageMapWidth) * terrain.foliageMapHeight;
    const auto [densityX0, densityX1] = SampleRange(x0, x1, terrain.worldSizeX, terrain.foliageMapWidth);
    const auto [densityZ0, densityZ1] = SampleRange(z0, z1, terrain.worldSizeZ, terrain.foliageMapHeight);
    const std::uint8_t* plane = terrain.foliageDensity.data() + planeSize * layerIndex;
    for (std::uint32_t row = densityZ0; row <= densityZ1; ++row) {
        HashBytes(hash, plane + static_cast<std::size_t>(row) * terrain.foliageMapWidth + densityX0, densityX1 - densityX0 + 1U);
    }
    const auto [heightX0, heightX1] = SampleRange(x0, x1, terrain.worldSizeX, terrain.width);
    const auto [heightZ0, heightZ1] = SampleRange(z0, z1, terrain.worldSizeZ, terrain.height);
    for (std::uint32_t row = heightZ0; row <= heightZ1; ++row) {
        HashBytes(hash, terrain.heights.data() + static_cast<std::size_t>(row) * terrain.width + heightX0,
            static_cast<std::size_t>(heightX1 - heightX0 + 1U) * sizeof(float));
    }
    // A hole is a quad: there is one fewer of them than vertices along each side.
    const std::uint32_t holeX0 = std::min(heightX0, terrain.width - 2U);
    const std::uint32_t holeX1 = std::min(heightX1, terrain.width - 2U);
    for (std::uint32_t row = std::min(heightZ0, terrain.height - 2U); row <= std::min(heightZ1, terrain.height - 2U); ++row) {
        HashBytes(hash, terrain.holes.data() + static_cast<std::size_t>(row) * (terrain.width - 1U) + holeX0, holeX1 - holeX0 + 1U);
    }
    return hash;
}

[[nodiscard]] std::array<float, 16> Multiply(const std::array<float, 16>& left, const std::array<float, 16>& right) noexcept {
    std::array<float, 16> result{};
    for (std::size_t column = 0U; column < 4U; ++column) {
        for (std::size_t row = 0U; row < 4U; ++row) {
            float sum = 0.0F;
            for (std::size_t inner = 0U; inner < 4U; ++inner) sum += left[inner * 4U + row] * right[column * 4U + inner];
            result[column * 4U + row] = sum;
        }
    }
    return result;
}

// The plants of one chunk as render instances, with the clusters the scan culls them by.
[[nodiscard]] TerrainFoliageChunkCache MakeChunk(
    const TerrainFoliageRenderProxyDesc& desc, std::uint32_t layerIndex, std::uint32_t chunkX, std::uint32_t chunkZ, std::uint64_t inputHash) {
    TerrainFoliageChunkCache cache{ .inputHash = inputHash };
    const kb::assets::TerrainFoliageLayer& layer = desc.terrain->foliageLayers[layerIndex];
    const kb::assets::TerrainFoliageChunk chunk = kb::assets::BuildTerrainFoliageChunk(*desc.terrain, layerIndex, { chunkX, chunkZ });
    if (chunk.instances.empty()) return cache;
    const std::uint64_t owner = Mix(desc.entityId ^ Mix((static_cast<std::uint64_t>(layerIndex) << 56U) ^
        (static_cast<std::uint64_t>(chunkX) << 28U) ^ chunkZ));
    auto instances = std::make_shared<std::vector<SceneRenderMeshInstance>>();
    instances->reserve(chunk.instances.size());
    for (std::size_t index = 0U; index < chunk.instances.size(); ++index) {
        instances->push_back(SceneRenderMeshInstance{
            .entityId = Mix(owner ^ (index + 1U)),
            .meshAssetId = layer.meshAssetId,
            .materialAssetId = layer.materialAssetId,
            .model = Multiply(desc.model, chunk.instances[index].model),
            .castsShadow = layer.castsShadow,
            .receivesShadow = true,
            .layer = desc.layer,
            .isStatic = true,
        });
    }
    GeometrySwarmVisibilityClusters::Append(*instances, cache.clusters, 0U);
    cache.instances = std::move(instances);
    return cache;
}

} // namespace

void RenderScene::SyncTerrainFoliage(
    std::span<const TerrainFoliageRenderProxyDesc> terrains, std::span<const std::uint64_t> keptEntityIds) {
    std::unordered_set<std::uint64_t> alive(keptEntityIds.begin(), keptEntityIds.end());
    bool changed = false;
    for (const TerrainFoliageRenderProxyDesc& desc : terrains) {
        alive.insert(desc.entityId);
        const auto [it, inserted] = terrainFoliage_.try_emplace(desc.entityId);
        TerrainFoliageRenderProxy& proxy = it->second;
        if (!inserted && proxy.desc.terrain == desc.terrain && proxy.desc.model == desc.model && proxy.desc.layer == desc.layer &&
            proxy.desc.revision == desc.revision) {
            continue;
        }
        if (inserted) proxy.id = AllocateProxyId();
        const kb::assets::TerrainFoliageChunkCoord count = kb::assets::TerrainFoliageChunkCount(*desc.terrain);
        const std::size_t layerCount = desc.terrain->foliageLayers.size();
        // Chunks keep their instances while their inputs are the same, which the index tells only for the same layout.
        const bool sameLayout = proxy.chunkCountX == count.x && proxy.chunkCountZ == count.z &&
            proxy.chunks.size() == layerCount * count.x * count.z;
        std::vector<TerrainFoliageChunkCache> chunks(layerCount * count.x * count.z);
        for (std::uint32_t layer = 0U; layer < layerCount; ++layer) {
            for (std::uint32_t z = 0U; z < count.z; ++z) {
                for (std::uint32_t x = 0U; x < count.x; ++x) {
                    const std::size_t index = (static_cast<std::size_t>(layer) * count.z + z) * count.x + x;
                    const std::uint64_t hash = ChunkInputHash(desc, layer, x, z);
                    if (sameLayout && proxy.chunks[index].inputHash == hash) {
                        chunks[index] = std::move(proxy.chunks[index]);
                        continue;
                    }
                    chunks[index] = MakeChunk(desc, layer, x, z, hash);
                    if (chunks[index].instances != nullptr) chunks[index].cacheId = NextContentRevision();
                    ++terrainFoliageChunkBuilds_;
                }
            }
        }
        proxy.desc = desc;
        proxy.chunkCountX = count.x;
        proxy.chunkCountZ = count.z;
        proxy.chunks = std::move(chunks);
        changed = true;
    }
    for (auto it = terrainFoliage_.begin(); it != terrainFoliage_.end();) {
        if (alive.contains(it->first)) {
            ++it;
        } else {
            it = terrainFoliage_.erase(it);
            changed = true;
        }
    }
    if (changed) InvalidateDrawGroupsOfNonMeshProxy();
}

void RenderScene::AppendTerrainFoliageGroups(std::size_t& writeGroupCount) const {
    for (const auto& [entityId, proxy] : terrainFoliage_) {
        const std::vector<kb::assets::TerrainFoliageLayer>& layers = proxy.desc.terrain->foliageLayers;
        for (std::size_t index = 0U; index < proxy.chunks.size(); ++index) {
            const TerrainFoliageChunkCache& chunk = proxy.chunks[index];
            if (chunk.instances == nullptr) continue;
            const kb::assets::TerrainFoliageLayer& layer = layers[index / (static_cast<std::size_t>(proxy.chunkCountX) * proxy.chunkCountZ)];
            if (writeGroupCount == drawGroups_.size()) drawGroups_.push_back(SceneRenderDrawGroup{});
            SceneRenderDrawGroup& group = drawGroups_[writeGroupCount++];
            group.cacheId = chunk.cacheId;
            group.contentRevision = chunk.cacheId;
            group.partitionOwner = entityId;
            group.partitionKind = 3U;
            group.meshAssetId = layer.meshAssetId;
            group.materialAssetId = layer.materialAssetId;
            group.hasMaterialSlotOverrides = false;
            group.hasMorphDeformation = false;
            group.sharedInstances = chunk.instances;
            group.visibilityClusters = chunk.clusters;
            group.fadeStartDistance = layer.fadeStartDistance;
            group.cullDistance = layer.cullDistance;
        }
    }
}

} // namespace kb::render
