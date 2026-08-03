#include "scene/submit/SceneMeshDrawCommandSubmitter.hpp"

#include "kb/render/scene/RenderInstanceBuffer.hpp"
#include "renderer/RendererDebugLog.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <sstream>

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

void EmitProgramUnavailableDiagnostic(
    SceneRenderDiagnostics* diagnostics,
    const MeshDrawCommand& command,
    const SceneMeshPassProgramResolution& resolution) {
    if (diagnostics == nullptr) {
        return;
    }
    diagnostics->events.push_back(SceneRenderDiagnosticEvent{
        .severity = SceneRenderDiagnosticSeverity::Error,
        .kind = SceneRenderDiagnosticKind::GraphMaterialProgramUnavailable,
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

[[nodiscard]] const char* MeshPassName(MeshPassType pass) noexcept {
    switch (pass) {
    case MeshPassType::Depth:
        return "Depth";
    case MeshPassType::BaseOpaque:
        return "BaseOpaque";
    case MeshPassType::GBuffer:
        return "GBuffer";
    case MeshPassType::BaseTransparent:
        return "BaseTransparent";
    case MeshPassType::ShadowDepth:
        return "ShadowDepth";
    case MeshPassType::SelectionId:
        return "SelectionId";
    case MeshPassType::EditorSelection:
        return "EditorSelection";
    case MeshPassType::Gizmo:
        return "Gizmo";
    }
    return "Unknown";
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::ProgramHandle handle) noexcept {
    return handle.idx;
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::VertexBufferHandle handle) noexcept {
    return handle.idx;
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::DynamicVertexBufferHandle handle) noexcept {
    return handle.idx;
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::IndexBufferHandle handle) noexcept {
    return handle.idx;
}

[[nodiscard]] std::string_view VertexFormatName(RenderVertexFormat format) noexcept {
    switch (format) {
    case RenderVertexFormat::P3C3: return "P3C3";
    case RenderVertexFormat::P3N3UV2: return "P3N3UV2";
    case RenderVertexFormat::P3N3T4UV2: return "P3N3T4UV2";
    case RenderVertexFormat::SkinnedP3N3T4UV2J4W4: return "SkinnedP3N3T4UV2J4W4";
    }
    return "Unknown";
}

[[nodiscard]] bool VertexFormatHasTangent(RenderVertexFormat format) noexcept {
    return format == RenderVertexFormat::P3N3T4UV2 ||
        format == RenderVertexFormat::SkinnedP3N3T4UV2J4W4;
}

} // namespace

void SceneMeshDrawCommandSubmitter::Submit(const SceneMeshDrawCommandSubmitDesc& desc) {
    const bool meshDrawDebugLogEnabled = RendererDebugLogEnabled("mesh_draw");
    if (meshDrawDebugLogEnabled) {
        std::ostringstream message;
        message << "Submit begin pass=" << MeshPassName(desc.pass)
                << " viewId=" << desc.viewId
                << " commandCount=" << desc.commands.size();
        WriteRendererDebugLog("mesh_draw", message.str());
    }
    for (const MeshDrawCommand& command : desc.commands) {
        if (command.meshResource == nullptr ||
            (!bgfx::isValid(command.meshResource->vertexBuffer) &&
             !bgfx::isValid(command.meshResource->dynamicVertexBuffer)) ||
            !bgfx::isValid(command.meshResource->indexBuffer)) {
            if (meshDrawDebugLogEnabled) {
                std::ostringstream message;
                message << "Skip missing mesh resource pass=" << MeshPassName(desc.pass)
                        << " meshAsset=" << command.meshAssetId
                        << " materialAsset=" << command.materialAssetId
                        << " instances=" << command.instances.size()
                        << " meshResource=" << (command.meshResource != nullptr ? "true" : "false");
                WriteRendererDebugLog("mesh_draw", message.str());
            }
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
            if (meshDrawDebugLogEnabled) {
                std::ostringstream message;
                message << "Drop all instances pass=" << MeshPassName(desc.pass)
                        << " meshAsset=" << command.meshAssetId
                        << " materialAsset=" << command.materialAssetId
                        << " requested=" << instanceCount
                        << " stride=" << RenderInstanceBuffer::Stride();
                WriteRendererDebugLog("mesh_draw", message.str());
            }
            desc.stats.droppedInstanceCount += instanceCount;
            EmitGroupDiagnostics(desc.diagnostics, SceneRenderDiagnosticKind::DroppedInstances, SceneRenderDiagnosticSeverity::Warning, command, 0U, instanceCount);
            continue;
        }
        if (availableInstances < instanceCount) {
            if (meshDrawDebugLogEnabled) {
                std::ostringstream message;
                message << "Drop partial instances pass=" << MeshPassName(desc.pass)
                        << " meshAsset=" << command.meshAssetId
                        << " materialAsset=" << command.materialAssetId
                        << " requested=" << instanceCount
                        << " available=" << availableInstances;
                WriteRendererDebugLog("mesh_draw", message.str());
            }
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
        if (bgfx::isValid(command.meshResource->dynamicVertexBuffer)) {
            bgfx::setVertexBuffer(0, command.meshResource->dynamicVertexBuffer);
        } else {
            bgfx::setVertexBuffer(0, command.meshResource->vertexBuffer);
        }
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
            .skinningPaletteAllocator = desc.skinningPaletteAllocator,
        });
        const SceneMeshPassProgramResolution resolution = desc.passResources.LastProgramResolution();
        if (command.materialResource != nullptr && command.materialResource->graphProgram.active) {
            std::ostringstream row;
            row << "draw-graph pass=" << MeshPassName(desc.pass)
                << " viewId=" << desc.viewId
                << " meshAsset=" << command.meshAssetId
                << " materialAsset=" << command.materialAssetId
                << " instances=" << availableInstances << '/' << instanceCount
                << " meshVertexFormat=" << VertexFormatName(command.meshResource->vertexFormat)
                << " meshHasTangent=" << (VertexFormatHasTangent(command.meshResource->vertexFormat) ? "true" : "false")
                << " meshStride=" << RenderStaticMeshVertexStride(command.meshResource->vertexFormat)
                << " vb=" << (bgfx::isValid(command.meshResource->dynamicVertexBuffer)
                    ? HandleValue(command.meshResource->dynamicVertexBuffer)
                    : HandleValue(command.meshResource->vertexBuffer))
                << " ib=" << HandleValue(command.meshResource->indexBuffer)
                << " graphProgram=" << (resolution.graphProgram ? "true" : "false")
                << " fallback=" << (resolution.fellBackToBuiltin ? "true" : "false")
                << " status=" << static_cast<int>(resolution.status)
                << " program=" << HandleValue(program)
                << " graphHash=" << command.materialResource->graphProgram.graphSourceHash
                << " textures=" << command.materialResource->graphProgram.textures.size()
                << " normalTextureAssetId=" << command.materialResource->normalTextureAssetId
                << " normalScale=" << command.materialResource->normalScale
                << " state=0x" << std::hex << command.state << std::dec;
            WriteRendererMaterialGraphDebugLog("draw", row.str());
        }
        EmitGraphProgramDiagnostic(desc.diagnostics, command, resolution);
        if (!bgfx::isValid(program)) {
            if (meshDrawDebugLogEnabled) {
                std::ostringstream message;
                message << "Skip invalid program pass=" << MeshPassName(desc.pass)
                        << " meshAsset=" << command.meshAssetId
                        << " materialAsset=" << command.materialAssetId
                        << " instances=" << availableInstances
                        << " graphProgram=" << (resolution.graphProgram ? "true" : "false")
                        << " fallback=" << (resolution.fellBackToBuiltin ? "true" : "false")
                        << " status=" << static_cast<int>(resolution.status)
                        << " keyHash=" << resolution.materialProgramIdentity
                        << " backend=" << resolution.key.backend
                        << " graphHash=" << resolution.key.graphSourceHash
                        << " variant=" << resolution.key.variantKey
                        << " pipeline=" << resolution.key.pipelineStateKey;
                WriteRendererDebugLog("mesh_draw", message.str());
            }
            EmitProgramUnavailableDiagnostic(desc.diagnostics, command, resolution);
            desc.stats.missingMaterialResourceCount += availableInstances;
            continue;
        }
        if (meshDrawDebugLogEnabled) {
            std::ostringstream message;
            message << "Draw submit pass=" << MeshPassName(desc.pass)
                    << " viewId=" << desc.viewId
                    << " meshAsset=" << command.meshAssetId
                    << " materialAsset=" << command.materialAssetId
                    << " instances=" << availableInstances << '/' << instanceCount
                    << " indexStart=" << command.indexStart
                    << " indexCount=" << command.indexCount
                    << " vb=" << (bgfx::isValid(command.meshResource->dynamicVertexBuffer)
                        ? HandleValue(command.meshResource->dynamicVertexBuffer)
                        : HandleValue(command.meshResource->vertexBuffer))
                    << " ib=" << HandleValue(command.meshResource->indexBuffer)
                    << " program=" << HandleValue(program)
                    << " graphProgram=" << (resolution.graphProgram ? "true" : "false")
                    << " fallback=" << (resolution.fellBackToBuiltin ? "true" : "false")
                    << " status=" << static_cast<int>(resolution.status)
                    << " alphaMode=" << (command.materialResource != nullptr ? static_cast<int>(command.materialResource->alphaMode) : -1)
                    << " doubleSidedMesh=" << (command.meshResource->doubleSided ? "true" : "false")
                    << " doubleSidedMaterial=" << (command.materialResource != nullptr && command.materialResource->doubleSided ? "true" : "false")
                    << " state=0x" << std::hex << command.state << std::dec
                    << " keyHash=" << resolution.materialProgramIdentity
                    << " graphHash=" << resolution.key.graphSourceHash
                    << " backend=" << resolution.key.backend;
            WriteRendererDebugLog("mesh_draw", message.str());
        }
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
    if (meshDrawDebugLogEnabled) {
        std::ostringstream message;
        message << "Submit end pass=" << MeshPassName(desc.pass)
                << " submittedMeshes=" << desc.stats.submittedMeshCount
                << " submittedDrawCalls=" << desc.stats.submittedDrawCallCount
                << " missingMeshResource=" << desc.stats.missingMeshResourceCount
                << " missingMaterialResource=" << desc.stats.missingMaterialResourceCount
                << " dropped=" << desc.stats.droppedInstanceCount
                << " uploadBytes=" << desc.stats.instanceUploadBytes;
        WriteRendererDebugLog("mesh_draw", message.str());
    }
}

} // namespace kb::render
