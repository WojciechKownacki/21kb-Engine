#pragma once

#include "kb/render/resources/RenderResources.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace kb::render {

enum class MeshPassType : std::uint8_t;

inline constexpr std::uint32_t kMaxSceneMaterialSlotOverrides = 8U;
inline constexpr std::uint32_t kMaxSceneForwardLights = 4U;

enum class RenderLightKind : std::uint8_t {
    Directional,
    Point,
    Spot,
};

enum class SceneRenderShadowFilter : std::uint8_t {
    Hard,
    Pcf3x3,
};

enum class SceneRenderEnvironmentMode : std::uint8_t {
    Disabled,
    Constant,
    Hemisphere,
};

struct SceneRenderCamera {
    std::array<float, 16> view{};
    std::array<float, 16> projection{};
};

struct SceneRenderMeshInstance {
    std::uint64_t entityId = 0;
    std::uint64_t meshAssetId = 0;
    std::uint64_t materialAssetId = 0;
    std::array<std::uint64_t, kMaxSceneMaterialSlotOverrides> materialSlotAssetIds{};
    std::uint32_t materialSlotOverrideCount = 0;
    std::array<float, 16> model{};
    std::array<float, 4> color{ 0.76F, 0.80F, 0.86F, 1.0F };
    RenderBoundsSphere worldBounds{};
    std::uint16_t depthBucket = 0;
    bool castsShadow = true;
    bool receivesShadow = true;
};

struct SceneRenderDrawGroup {
    std::uint64_t meshAssetId = 0;
    std::uint64_t materialAssetId = 0;
    std::vector<SceneRenderMeshInstance> instances;
};

struct SceneRenderLight {
    std::uint64_t entityId = 0;
    RenderLightKind kind = RenderLightKind::Point;
    float position[3]{};
    float direction[3]{ 0.0F, 0.0F, 1.0F };
    float color[3]{ 1.0F, 1.0F, 1.0F };
    float intensity = 1.0F;
    float range = 10.0F;
    float innerConeCos = 0.0F;
    float outerConeCos = 0.0F;
};

struct SceneRenderSnapshot {
    std::optional<SceneRenderCamera> camera;
    std::vector<SceneRenderMeshInstance> meshes;
    std::vector<SceneRenderLight> lights;
};

struct SceneRenderDrawBudget {
    std::uint32_t maxDrawCommands = 0;
    std::uint32_t maxVisibleInstances = 0;
};

struct SceneRenderLightingConfig {
    std::uint32_t maxForwardLights = kMaxSceneForwardLights;
    std::array<float, 3> ambientColor{ 0.18F, 0.20F, 0.23F };
    float ambientIntensity = 1.0F;
    SceneRenderEnvironmentMode environmentMode = SceneRenderEnvironmentMode::Constant;
    std::array<float, 3> environmentZenithColor{ 0.36F, 0.42F, 0.52F };
    std::array<float, 3> environmentGroundColor{ 0.08F, 0.075F, 0.065F };
    float environmentDiffuseIntensity = 1.0F;
    float environmentSpecularIntensity = 0.25F;
    std::uint32_t shadowMapSize = 1024U;
    float shadowDistance = 50.0F;
    float shadowDepthBias = 0.002F;
    float shadowStrength = 0.65F;
    SceneRenderShadowFilter shadowFilter = SceneRenderShadowFilter::Pcf3x3;
    bool shadowsEnabled = true;
};

struct SceneRenderShadowMapBinding {
    bgfx::TextureHandle depthTexture = BGFX_INVALID_HANDLE;
    std::array<float, 16> lightViewProjection{};
    std::array<float, 4> params{};

    [[nodiscard]] bool IsValid() const noexcept {
        return bgfx::isValid(depthTexture) && params[3] > 0.0F;
    }
};

enum class SceneRenderMeshPassMode : std::uint8_t {
    OpaqueOnly,
    OpaqueAndTransparent,
};

struct SceneRenderSubmitStats {
    std::uint32_t visibleMeshCount = 0;
    std::uint32_t visibleDrawGroupCount = 0;
    std::uint32_t culledInstanceCount = 0;
    std::uint32_t submittedMeshCount = 0;
    std::uint32_t submittedDrawGroupCount = 0;
    std::uint32_t submittedDrawCallCount = 0;
    std::uint32_t droppedInstanceCount = 0;
    std::uint32_t meshPipelineCommandCount = 0;
    std::uint32_t meshPipelineCommandCapacity = 0;
    std::uint32_t meshPipelineSortKeyCount = 0;
    std::uint32_t meshDrawGroupScratchCapacity = 0;
    std::uint32_t meshDrawGroupInstanceScratchCapacity = 0;
    std::uint32_t meshDrawGroupLookupCapacity = 0;
    std::uint32_t meshCommandLookupCapacity = 0;
    std::uint32_t meshPipelineScratchInstanceCapacity = 0;
    std::uint64_t instanceUploadBytes = 0;
    std::uint32_t sceneLightCount = 0;
    std::uint32_t submittedForwardLightCount = 0;
    std::uint32_t skippedForwardLightCount = 0;
    std::uint32_t invalidLightCount = 0;
    std::uint32_t forwardLightCapacity = 0;
    std::uint32_t submittedEnvironmentLightingCount = 0;
    std::uint32_t environmentLightingMode = 0;
    std::uint32_t environmentLightingSampleCount = 0;
    std::uint32_t shadowCasterCount = 0;
    std::uint32_t submittedShadowCasterCount = 0;
    std::uint32_t submittedShadowDrawCallCount = 0;
    std::uint32_t shadowMapSize = 0;
    std::uint32_t shadowFilterSampleCount = 0;
    std::uint64_t shadowLightEntityId = 0;
    std::uint64_t shadowMapAllocationBytes = 0;
    std::uint32_t missingMeshBindingCount = 0;
    std::uint32_t missingMeshResourceCount = 0;
    std::uint32_t unsupportedMeshVertexFormatCount = 0;
    std::uint32_t missingMaterialBindingCount = 0;
    std::uint32_t missingMaterialResourceCount = 0;
    std::uint32_t missingTextureBindingCount = 0;
    std::uint32_t missingTextureResourceCount = 0;

    [[nodiscard]] constexpr bool HasMissingResources() const noexcept {
        return missingMeshBindingCount != 0U ||
            missingMeshResourceCount != 0U ||
            unsupportedMeshVertexFormatCount != 0U ||
            missingMaterialBindingCount != 0U ||
            missingMaterialResourceCount != 0U ||
            missingTextureBindingCount != 0U ||
            missingTextureResourceCount != 0U;
    }
};

struct SceneRenderPassSubmitStats {
    std::uint32_t viewportId = 0;
    std::uint32_t viewportIndex = 0;
    MeshPassType pass{};
    SceneRenderSubmitStats stats{};
};

enum class SceneRenderDiagnosticSeverity : std::uint8_t {
    Info,
    Warning,
    Error,
};

enum class SceneRenderDiagnosticKind : std::uint8_t {
    MissingMeshBinding,
    MissingMeshResource,
    UnsupportedMeshVertexFormat,
    MissingMaterialBinding,
    MissingMaterialResource,
    MissingTextureBinding,
    MissingTextureResource,
    UnresolvedMaterialTexturePath,
    DroppedInstances,
};

struct SceneRenderDiagnosticEvent {
    SceneRenderDiagnosticSeverity severity = SceneRenderDiagnosticSeverity::Info;
    SceneRenderDiagnosticKind kind = SceneRenderDiagnosticKind::MissingMeshBinding;
    std::uint64_t entityId = 0;
    std::uint64_t meshAssetId = 0;
    std::uint64_t materialAssetId = 0;
    std::uint32_t instanceCount = 0;
};

struct SceneRenderDiagnostics {
    std::vector<SceneRenderDiagnosticEvent> events;

    [[nodiscard]] bool HasErrors() const noexcept {
        for (const SceneRenderDiagnosticEvent& event : events) {
            if (event.severity == SceneRenderDiagnosticSeverity::Error) {
                return true;
            }
        }
        return false;
    }

    void Clear() noexcept {
        events.clear();
    }
};

[[nodiscard]] constexpr SceneRenderSubmitStats operator+(SceneRenderSubmitStats lhs, const SceneRenderSubmitStats& rhs) noexcept {
    lhs.visibleMeshCount += rhs.visibleMeshCount;
    lhs.visibleDrawGroupCount += rhs.visibleDrawGroupCount;
    lhs.culledInstanceCount += rhs.culledInstanceCount;
    lhs.submittedMeshCount += rhs.submittedMeshCount;
    lhs.submittedDrawGroupCount += rhs.submittedDrawGroupCount;
    lhs.submittedDrawCallCount += rhs.submittedDrawCallCount;
    lhs.droppedInstanceCount += rhs.droppedInstanceCount;
    lhs.meshPipelineCommandCount += rhs.meshPipelineCommandCount;
    lhs.meshPipelineCommandCapacity += rhs.meshPipelineCommandCapacity;
    lhs.meshPipelineSortKeyCount += rhs.meshPipelineSortKeyCount;
    lhs.meshDrawGroupScratchCapacity += rhs.meshDrawGroupScratchCapacity;
    lhs.meshDrawGroupInstanceScratchCapacity += rhs.meshDrawGroupInstanceScratchCapacity;
    lhs.meshDrawGroupLookupCapacity += rhs.meshDrawGroupLookupCapacity;
    lhs.meshCommandLookupCapacity += rhs.meshCommandLookupCapacity;
    lhs.meshPipelineScratchInstanceCapacity += rhs.meshPipelineScratchInstanceCapacity;
    lhs.instanceUploadBytes += rhs.instanceUploadBytes;
    lhs.sceneLightCount += rhs.sceneLightCount;
    lhs.submittedForwardLightCount += rhs.submittedForwardLightCount;
    lhs.skippedForwardLightCount += rhs.skippedForwardLightCount;
    lhs.invalidLightCount += rhs.invalidLightCount;
    lhs.forwardLightCapacity += rhs.forwardLightCapacity;
    lhs.submittedEnvironmentLightingCount += rhs.submittedEnvironmentLightingCount;
    if (rhs.environmentLightingMode != 0U) {
        lhs.environmentLightingMode = rhs.environmentLightingMode;
        lhs.environmentLightingSampleCount = rhs.environmentLightingSampleCount;
    }
    lhs.shadowCasterCount += rhs.shadowCasterCount;
    lhs.submittedShadowCasterCount += rhs.submittedShadowCasterCount;
    lhs.submittedShadowDrawCallCount += rhs.submittedShadowDrawCallCount;
    lhs.shadowMapSize = rhs.shadowMapSize != 0U ? rhs.shadowMapSize : lhs.shadowMapSize;
    lhs.shadowFilterSampleCount = rhs.shadowFilterSampleCount != 0U ? rhs.shadowFilterSampleCount : lhs.shadowFilterSampleCount;
    lhs.shadowLightEntityId = rhs.shadowLightEntityId != 0U ? rhs.shadowLightEntityId : lhs.shadowLightEntityId;
    lhs.shadowMapAllocationBytes = rhs.shadowMapAllocationBytes != 0U ? rhs.shadowMapAllocationBytes : lhs.shadowMapAllocationBytes;
    lhs.missingMeshBindingCount += rhs.missingMeshBindingCount;
    lhs.missingMeshResourceCount += rhs.missingMeshResourceCount;
    lhs.unsupportedMeshVertexFormatCount += rhs.unsupportedMeshVertexFormatCount;
    lhs.missingMaterialBindingCount += rhs.missingMaterialBindingCount;
    lhs.missingMaterialResourceCount += rhs.missingMaterialResourceCount;
    lhs.missingTextureBindingCount += rhs.missingTextureBindingCount;
    lhs.missingTextureResourceCount += rhs.missingTextureResourceCount;
    return lhs;
}

constexpr SceneRenderSubmitStats& operator+=(SceneRenderSubmitStats& lhs, const SceneRenderSubmitStats& rhs) noexcept {
    lhs = lhs + rhs;
    return lhs;
}

inline SceneRenderDiagnostics& operator+=(SceneRenderDiagnostics& lhs, const SceneRenderDiagnostics& rhs) {
    lhs.events.insert(lhs.events.end(), rhs.events.begin(), rhs.events.end());
    return lhs;
}

} // namespace kb::render
