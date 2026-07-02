#include "scene/pipeline/MeshPipelineCommandBuilder.hpp"

#include <algorithm>
#include <cstdint>

namespace kb::render {
namespace {

[[nodiscard]] std::uint32_t ResourceKey20(std::uint64_t value) noexcept {
    return static_cast<std::uint32_t>(value & 0xFFFFFU);
}

void MixU64(std::uint64_t& seed, std::uint64_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

[[nodiscard]] std::uint64_t GraphProgramSortIdentity(const RenderMaterialGraphProgramBinding& graphProgram) noexcept {
    std::uint64_t identity = graphProgram.materialTypeId;
    MixU64(identity, graphProgram.materialTypeVersion);
    MixU64(identity, graphProgram.graphSourceHash);
    MixU64(identity, graphProgram.variantKey);
    MixU64(identity, graphProgram.pipelineStateKey);
    return identity;
}

[[nodiscard]] std::uint16_t SortDepthBucket(MeshPassType pass, std::uint16_t depthBucket) noexcept {
    return pass == MeshPassType::BaseTransparent
        ? static_cast<std::uint16_t>(UINT16_MAX - depthBucket)
        : depthBucket;
}

[[nodiscard]] std::uint64_t BuildSortKey(
    MeshPassType pass,
    RenderMaterialHandle material,
    std::uint64_t materialAssetId,
    const RenderMaterialResource* materialResource,
    RenderMeshHandle mesh,
    std::uint64_t meshAssetId,
    std::uint16_t depthBucket) noexcept {
    const std::uint64_t passKey = static_cast<std::uint64_t>(static_cast<std::uint8_t>(pass) & 0x0FU);
    // Group graph-material draws by their full GPU program identity so the sort minimizes program
    // switches across graph hash, static variant, material type/version, and pipeline state.
    const std::uint64_t programIdentity = (materialResource != nullptr && materialResource->graphProgram.active)
        ? GraphProgramSortIdentity(materialResource->graphProgram)
        : (material.IsValid() ? material.value : materialAssetId);
    const std::uint64_t materialKey = ResourceKey20(programIdentity);
    const std::uint64_t meshKey = ResourceKey20(mesh.IsValid() ? mesh.value : meshAssetId);
    return (passKey << 60U) | (materialKey << 40U) | (meshKey << 20U) | static_cast<std::uint64_t>(SortDepthBucket(pass, depthBucket));
}

void ResetCommandKeepingInstanceStorage(MeshDrawCommand& command) noexcept {
    command.pass = MeshPassType::BaseOpaque;
    command.meshAssetId = 0U;
    command.materialAssetId = 0U;
    command.sectionIndex = 0U;
    command.materialSlot = 0U;
    command.firstMeshlet = 0U;
    command.meshletCount = 0U;
    command.indexStart = 0U;
    command.indexCount = 0U;
    command.lodLevel = 0U;
    command.depthBucket = 0U;
    command.mesh = {};
    command.material = {};
    command.meshResource = nullptr;
    command.materialResource = nullptr;
    command.state = 0U;
    command.sortKey = 0U;
    command.instances.clear();
}

} // namespace

MeshDrawCommand& MeshPipelineCommandBuilder::WritableCommand(MeshPipelineBuildResult& result, std::size_t index) {
    if (index == result.commands.size()) {
        result.commands.push_back(MeshDrawCommand{});
    }
    MeshDrawCommand& command = result.commands[index];
    ResetCommandKeepingInstanceStorage(command);
    return command;
}

void MeshPipelineCommandBuilder::FinalizeCommands(MeshPipelineBuildResult& result, MeshPassType pass, std::size_t commandCount) noexcept {
    result.commands.resize(commandCount);
    for (MeshDrawCommand& command : result.commands) {
        command.pass = pass;
        command.depthBucket = command.instances.empty()
            ? 0U
            : static_cast<std::uint16_t>(command.sortKey / static_cast<std::uint64_t>(command.instances.size()));
        command.sortKey = BuildSortKey(pass, command.material, command.materialAssetId, command.materialResource, command.mesh, command.meshAssetId, command.depthBucket);
        result.stats.visibleMeshCount += static_cast<std::uint32_t>(command.instances.size());
        ++result.stats.visibleDrawGroupCount;
    }
    std::ranges::sort(result.commands, [](const MeshDrawCommand& lhs, const MeshDrawCommand& rhs) {
        return lhs.sortKey < rhs.sortKey;
    });
    result.stats.meshPipelineCommandCount = static_cast<std::uint32_t>(result.commands.size());
    result.stats.meshPipelineCommandCapacity = static_cast<std::uint32_t>(result.commands.capacity());
    result.stats.meshPipelineSortKeyCount = result.stats.meshPipelineCommandCount;
}

void MeshPipelineCommandBuilder::CountCommandsAsSubmitted(SceneRenderSubmitStats& stats, const std::vector<MeshDrawCommand>& commands) noexcept {
    stats.meshPipelineCommandCount = static_cast<std::uint32_t>(commands.size());
    stats.meshPipelineCommandCapacity = static_cast<std::uint32_t>(commands.capacity());
    stats.meshPipelineSortKeyCount = stats.meshPipelineCommandCount;
    for (const MeshDrawCommand& command : commands) {
        stats.submittedMeshCount += static_cast<std::uint32_t>(command.instances.size());
        ++stats.submittedDrawGroupCount;
        ++stats.submittedDrawCallCount;
    }
}

} // namespace kb::render
