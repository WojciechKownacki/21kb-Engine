#pragma once

#include "kb/render/resources/RenderHandles.hpp"
#include "kb/render/resources/RenderResources.hpp"
#include "kb/render/scene/MeshPassType.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace kb::render {

struct SceneCachedDrawCommandKey {
    MeshPassType pass = MeshPassType::BaseOpaque;
    std::uint64_t meshAssetId = 0;
    std::uint64_t materialAssetId = 0;
    std::uint64_t meshHandleValue = 0;
    std::uint64_t materialHandleValue = 0;
    std::uint64_t meshResourceVersion = 0;
    std::uint64_t materialResourceVersion = 0;
    std::uint64_t materialProgramKey = 0;
    std::uint64_t materialProgramTypeId = 0;
    std::uint32_t materialProgramTypeVersion = 0;
    std::uint64_t materialProgramGraphSourceHash = 0;
    std::uint64_t materialProgramVariantKey = 0;
    std::uint64_t materialProgramPipelineStateKey = 0;
    bool materialGraphProgram = false;
    std::uint64_t materialTextureDependencySignature = 0;
    std::uint32_t sectionIndex = 0;
    std::uint32_t materialSlot = 0;
    std::uint32_t firstMeshlet = 0;
    std::uint32_t meshletCount = 0;
    std::uint32_t indexStart = 0;
    std::uint32_t indexCount = 0;
    std::uint8_t lodLevel = 0;
    std::uint64_t state = 0;

    [[nodiscard]] friend constexpr bool operator==(SceneCachedDrawCommandKey lhs, SceneCachedDrawCommandKey rhs) noexcept = default;
};

struct SceneCachedDrawCommandKeyHash {
    [[nodiscard]] std::size_t operator()(const SceneCachedDrawCommandKey& key) const noexcept;
};

struct SceneCachedDrawCommand {
    SceneCachedDrawCommandKey key{};
    RenderMeshHandle mesh{};
    RenderMaterialHandle material{};
    const RenderMeshResource* meshResource = nullptr;
    const RenderMaterialResource* materialResource = nullptr;
    std::uint64_t lastUsedBuildId = 0;
};

struct SceneCachedDrawCommandStore {
    std::vector<SceneCachedDrawCommand> commands;
    std::unordered_map<SceneCachedDrawCommandKey, std::size_t, SceneCachedDrawCommandKeyHash> lookup;
    std::uint64_t currentBuildId = 0;
};

struct SceneCachedDrawCommandDesc {
    MeshPassType pass = MeshPassType::BaseOpaque;
    std::uint64_t meshAssetId = 0;
    std::uint64_t materialAssetId = 0;
    std::uint32_t sectionIndex = 0;
    std::uint32_t materialSlot = 0;
    std::uint32_t firstMeshlet = 0;
    std::uint32_t meshletCount = 0;
    std::uint32_t indexStart = 0;
    std::uint32_t indexCount = 0;
    std::uint8_t lodLevel = 0;
    RenderMeshHandle mesh{};
    RenderMaterialHandle material{};
    const RenderMeshResource* meshResource = nullptr;
    const RenderMaterialResource* materialResource = nullptr;
    std::uint64_t meshResourceVersion = 0;
    std::uint64_t materialResourceVersion = 0;
    std::uint64_t materialTextureDependencySignature = 0;
    std::uint64_t state = 0;
};

class SceneDrawCommandCache {
public:
    SceneDrawCommandCache() = delete;

    static void BeginBuild(SceneCachedDrawCommandStore& store, MeshPassType pass) noexcept;
    [[nodiscard]] static const SceneCachedDrawCommand& Resolve(
        SceneCachedDrawCommandStore& store,
        const SceneCachedDrawCommandDesc& desc,
        SceneRenderSubmitStats& stats);
    static void EndBuild(SceneCachedDrawCommandStore& store, MeshPassType pass, SceneRenderSubmitStats& stats);
};

} // namespace kb::render
