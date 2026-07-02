#include "scene/submit/SceneMeshDrawCommandSubmitter.hpp"

#include "kb/render/scene/RenderInstanceBuffer.hpp"

#include <algorithm>
#include <cstdint>
#include <span>

namespace kb::render {
namespace {

void EmitGroupDiagnostics(
    SceneRenderDiagnostics* diagnostics,
    SceneRenderDiagnosticKind kind,
    SceneRenderDiagnosticSeverity severity,
    const MeshDrawCommand& command,
    std::uint32_t firstInstance,
    std::uint32_t instanceCount) {
    if (diagnostics == nullptr || instanceCount == 0U || firstInstance >= command.instances.size()) {
        return;
    }

    const std::uint32_t end = std::min<std::uint32_t>(
        static_cast<std::uint32_t>(command.instances.size()),
        firstInstance + instanceCount);
    for (std::uint32_t index = firstInstance; index < end; ++index) {
        const SceneRenderMeshInstance& instance = command.instances[index];
        diagnostics->events.push_back(SceneRenderDiagnosticEvent{
            .severity = severity,
            .kind = kind,
            .entityId = instance.entityId,
            .meshAssetId = command.meshAssetId,
            .materialAssetId = command.materialAssetId,
            .instanceCount = 1U,
        });
    }
}

void EmitGraphProgramDiagnostic(
    SceneRenderDiagnostics* diagnostics,
    const MeshDrawCommand& command,
    const SceneMeshPassProgramResolution& resolution) {
    if (diagnostics == nullptr || !resolution.fellBackToBuiltin) {
        return;
    }
    diagnostics->events.push_back(SceneRenderDiagnosticEvent{
        .severity = SceneRenderDiagnosticSeverity::Warning,
        .kind = SceneRenderDiagnosticKind::GraphMaterialProgramFallback,
        .entityId = command.instances.empty() ? 0U : command.instances.front().entityId,
        .meshAssetId = command.meshAssetId,
        .materialAssetId = command.materialAssetId,
        .instanceCount = static_cast<std::uint32_t>(command.instances.size()),
        .materialTypeId = resolution.key.materialTypeId,
        .materialTypeVersion = resolution.key.materialTypeVersion,
        .graphSourceHash = resolution.key.graphSourceHash,
        .graphVariantKey = resolution.key.variantKey,
        .pipelineStateKey = resolution.key.pipelineStateKey,
        .materialProgramIdentity = resolution.materialProgramIdentity,
        .materialProgramBackend = resolution.key.backend,
        .materialProgramHandle = resolution.program.idx,
        .materialProgramStatus = resolution.status,
    });
}

[[nodiscard]] bool IsSelectionPass(MeshPassType pass) noexcept {
    return pass == MeshPassType::SelectionId || pass == MeshPassType::EditorSelection;
}

} // namespace

void SceneMeshDrawCommandSubmitter::Submit(const SceneMeshDrawCommandSubmitDesc& desc) {
    for (const MeshDrawCommand& command : desc.commands) {
        if (command.meshResource == nullptr ||
            !bgfx::isValid(command.meshResource->vertexBuffer) ||
            !bgfx::isValid(command.meshResource->indexBuffer)) {
            desc.stats.missingMeshResourceCount += static_cast<std::uint32_t>(command.instances.size());
            EmitGroupDiagnostics(
                desc.diagnostics,
                SceneRenderDiagnosticKind::MissingMeshResource,
                SceneRenderDiagnosticSeverity::Error,
                command,
                0U,
                static_cast<std::uint32_t>(command.instances.size()));
            continue;
        }
        const std::uint32_t instanceCount = static_cast<std::uint32_t>(command.instances.size());
        const std::uint32_t availableInstances = bgfx::getAvailInstanceDataBuffer(instanceCount, RenderInstanceBuffer::Stride());
        if (availableInstances == 0U) {
            desc.stats.droppedInstanceCount += instanceCount;
            EmitGroupDiagnostics(desc.diagnostics, SceneRenderDiagnosticKind::DroppedInstances, SceneRenderDiagnosticSeverity::Warning, command, 0U, instanceCount);
            continue;
        }
        if (availableInstances < instanceCount) {
            desc.stats.droppedInstanceCount += instanceCount - availableInstances;
            EmitGroupDiagnostics(
                desc.diagnostics,
                SceneRenderDiagnosticKind::DroppedInstances,
                SceneRenderDiagnosticSeverity::Warning,
                command,
                availableInstances,
                instanceCount - availableInstances);
        }

        bgfx::InstanceDataBuffer instanceBuffer{};
        bgfx::allocInstanceDataBuffer(&instanceBuffer, availableInstances, RenderInstanceBuffer::Stride());
        auto* instanceData = reinterpret_cast<RenderInstanceData*>(instanceBuffer.data);
        const bool selectionPass = IsSelectionPass(desc.pass);
        RenderInstanceBuffer::Copy(
            std::span<RenderInstanceData>(instanceData, availableInstances),
            std::span<const SceneRenderMeshInstance>(command.instances.data(), availableInstances),
            selectionPass ? nullptr : command.materialResource,
            desc.pass != MeshPassType::ShadowDepth && !selectionPass);

        bgfx::setInstanceDataBuffer(&instanceBuffer, 0U, static_cast<std::uint32_t>(availableInstances));
        bgfx::setVertexBuffer(0, command.meshResource->vertexBuffer);
        bgfx::setIndexBuffer(command.meshResource->indexBuffer, command.indexStart, command.indexCount);
        const bgfx::ProgramHandle program = desc.passResources.Bind(SceneMeshPassBindDesc{
            .command = command,
            .resources = desc.resources,
            .resourceMap = desc.resourceMap,
            .pass = desc.pass,
            .lighting = desc.lighting,
            .cameraPosition = desc.cameraPosition,
            .frameTime = desc.frameTime,
            .dynamicParameter = desc.dynamicParameter,
            .shadowMap = desc.shadowMap,
            .sceneDepthTexture = desc.sceneDepthTexture,
            .sceneColorTexture = desc.sceneColorTexture,
        });
        EmitGraphProgramDiagnostic(desc.diagnostics, command, desc.passResources.LastProgramResolution());
        bgfx::setState(command.state);
        bgfx::submit(desc.viewId, program);

        desc.stats.submittedMeshCount += availableInstances;
        ++desc.stats.submittedDrawCallCount;
        ++desc.stats.submittedDrawGroupCount;
        if (desc.pass == MeshPassType::ShadowDepth) {
            desc.stats.submittedShadowCasterCount += availableInstances;
            ++desc.stats.submittedShadowDrawCallCount;
        }
        desc.stats.instanceUploadBytes += static_cast<std::uint64_t>(availableInstances) * RenderInstanceBuffer::Stride();
    }
}

} // namespace kb::render
