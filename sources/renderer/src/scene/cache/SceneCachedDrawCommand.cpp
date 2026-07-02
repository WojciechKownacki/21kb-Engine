#include "kb/render/scene/cache/SceneCachedDrawCommand.hpp"

#include "kb/render/MaterialProgramRegistry.hpp"
#include "kb/render/scene/MeshPipeline.hpp"

#include <cstddef>
#include <cstdint>

namespace kb::render {
namespace {

void HashCombine(std::size_t& seed, std::uint64_t value) noexcept {
    seed ^= static_cast<std::size_t>(value + 0x9e3779b97f4a7c15ULL + (static_cast<std::uint64_t>(seed) << 6U) + (static_cast<std::uint64_t>(seed) >> 2U));
}

[[nodiscard]] SceneCachedDrawCommandKey BuildKey(const SceneCachedDrawCommandDesc& desc) noexcept {
    MaterialProgramKey programKey{};
    if (desc.materialResource != nullptr && desc.materialResource->graphProgram.active) {
        programKey = MaterialProgramKey{
            .materialTypeId = desc.materialResource->graphProgram.materialTypeId,
            .materialTypeVersion = desc.materialResource->graphProgram.materialTypeVersion,
            .graphSourceHash = desc.materialResource->graphProgram.graphSourceHash,
            .variantKey = desc.materialResource->graphProgram.variantKey,
            .pass = MeshPassTypeName(desc.pass),
            .backend = 0U,
            .pipelineStateKey = desc.materialResource->graphProgram.pipelineStateKey,
            .requiresGeneratedVertexShader = desc.materialResource->graphProgram.requiresGeneratedVertexShader,
            .graphProgram = true,
        };
    }

    return SceneCachedDrawCommandKey{
        .pass = desc.pass,
        .meshAssetId = desc.meshAssetId,
        .materialAssetId = desc.materialAssetId,
        .meshHandleValue = desc.mesh.value,
        .materialHandleValue = desc.material.value,
        .meshResourceVersion = desc.meshResourceVersion,
        .materialResourceVersion = desc.materialResourceVersion,
        .materialProgramKey = programKey.graphProgram ? MaterialProgramKeyIdentityHash(programKey) : 0U,
        .materialProgramTypeId = programKey.materialTypeId,
        .materialProgramTypeVersion = programKey.materialTypeVersion,
        .materialProgramGraphSourceHash = programKey.graphSourceHash,
        .materialProgramVariantKey = programKey.variantKey,
        .materialProgramPipelineStateKey = programKey.pipelineStateKey,
        .materialGraphProgram = programKey.graphProgram,
        .materialTextureDependencySignature = desc.materialTextureDependencySignature,
        .sectionIndex = desc.sectionIndex,
        .materialSlot = desc.materialSlot,
        .firstMeshlet = desc.firstMeshlet,
        .meshletCount = desc.meshletCount,
        .indexStart = desc.indexStart,
        .indexCount = desc.indexCount,
        .lodLevel = desc.lodLevel,
        .state = desc.state,
    };
}

void UpdateStats(const SceneCachedDrawCommandStore& store, SceneRenderSubmitStats& stats) noexcept {
    stats.meshCachedDrawCommandCount = static_cast<std::uint32_t>(store.commands.size());
    stats.meshCachedDrawCommandCapacity = static_cast<std::uint32_t>(store.commands.capacity());
    stats.meshDrawCommandCacheLookupCapacity = static_cast<std::uint32_t>(store.lookup.bucket_count());
}

} // namespace

std::size_t SceneCachedDrawCommandKeyHash::operator()(const SceneCachedDrawCommandKey& key) const noexcept {
    std::size_t seed = 0U;
    HashCombine(seed, static_cast<std::uint64_t>(static_cast<std::uint8_t>(key.pass)));
    HashCombine(seed, key.meshAssetId);
    HashCombine(seed, key.materialAssetId);
    HashCombine(seed, key.meshHandleValue);
    HashCombine(seed, key.materialHandleValue);
    HashCombine(seed, key.meshResourceVersion);
    HashCombine(seed, key.materialResourceVersion);
    HashCombine(seed, key.materialProgramKey);
    HashCombine(seed, key.materialProgramTypeId);
    HashCombine(seed, key.materialProgramTypeVersion);
    HashCombine(seed, key.materialProgramGraphSourceHash);
    HashCombine(seed, key.materialProgramVariantKey);
    HashCombine(seed, key.materialProgramPipelineStateKey);
    HashCombine(seed, key.materialGraphProgram ? 1U : 0U);
    HashCombine(seed, key.materialTextureDependencySignature);
    HashCombine(seed, key.sectionIndex);
    HashCombine(seed, key.materialSlot);
    HashCombine(seed, key.firstMeshlet);
    HashCombine(seed, key.meshletCount);
    HashCombine(seed, key.indexStart);
    HashCombine(seed, key.indexCount);
    HashCombine(seed, key.lodLevel);
    HashCombine(seed, key.state);
    return seed;
}

void SceneDrawCommandCache::BeginBuild(SceneCachedDrawCommandStore& store, MeshPassType) noexcept {
    ++store.currentBuildId;
}

const SceneCachedDrawCommand& SceneDrawCommandCache::Resolve(
    SceneCachedDrawCommandStore& store,
    const SceneCachedDrawCommandDesc& desc,
    SceneRenderSubmitStats& stats) {
    const SceneCachedDrawCommandKey key = BuildKey(desc);
    const auto cacheIt = store.lookup.find(key);
    if (cacheIt != store.lookup.end()) {
        ++stats.meshDrawCommandCacheHitCount;
        SceneCachedDrawCommand& cachedCommand = store.commands[cacheIt->second];
        cachedCommand.mesh = desc.mesh;
        cachedCommand.material = desc.material;
        cachedCommand.meshResource = desc.meshResource;
        cachedCommand.materialResource = desc.materialResource;
        cachedCommand.lastUsedBuildId = store.currentBuildId;
        UpdateStats(store, stats);
        return cachedCommand;
    }

    const std::size_t commandIndex = store.commands.size();
    store.commands.push_back(SceneCachedDrawCommand{
        .key = key,
        .mesh = desc.mesh,
        .material = desc.material,
        .meshResource = desc.meshResource,
        .materialResource = desc.materialResource,
        .lastUsedBuildId = store.currentBuildId,
    });
    store.lookup.emplace(key, commandIndex);
    ++stats.meshDrawCommandCacheMissCount;
    ++stats.meshDrawCommandCacheBuildCount;
    UpdateStats(store, stats);
    return store.commands[commandIndex];
}

void SceneDrawCommandCache::EndBuild(SceneCachedDrawCommandStore& store, MeshPassType pass, SceneRenderSubmitStats& stats) {
    std::size_t writeIndex = 0U;
    for (std::size_t readIndex = 0U; readIndex < store.commands.size(); ++readIndex) {
        if (store.commands[readIndex].key.pass == pass && store.commands[readIndex].lastUsedBuildId != store.currentBuildId) {
            ++stats.meshDrawCommandCachePruneCount;
            continue;
        }
        if (writeIndex != readIndex) {
            store.commands[writeIndex] = store.commands[readIndex];
        }
        ++writeIndex;
    }
    store.commands.resize(writeIndex);

    store.lookup.clear();
    store.lookup.reserve(store.commands.size());
    for (std::size_t index = 0U; index < store.commands.size(); ++index) {
        store.lookup.emplace(store.commands[index].key, index);
    }
    UpdateStats(store, stats);
}

} // namespace kb::render
