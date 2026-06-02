#include "scene/SceneMeshSubmitter.hpp"

#include "kb/render/ShaderLoader.hpp"
#include "kb/render/scene/RenderInstanceBuffer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>

namespace kb::render {
namespace {

struct Basis {
    float zx = 0.0F;
    float zy = 0.0F;
    float zz = 1.0F;
};

struct PackedSceneLighting {
    std::array<float, kMaxSceneForwardLights * 4U> dirKind{};
    std::array<float, kMaxSceneForwardLights * 4U> positionRange{};
    std::array<float, kMaxSceneForwardLights * 4U> colorIntensity{};
    std::array<float, kMaxSceneForwardLights * 4U> spot{};
    std::array<float, 4U> params{};
    std::array<float, 4U> ambient{ 0.18F, 0.20F, 0.23F, 1.0F };
    std::array<float, 4U> environmentZenith{ 0.36F, 0.42F, 0.52F, 1.0F };
    std::array<float, 4U> environmentGround{ 0.08F, 0.075F, 0.065F, 1.0F };
    std::array<float, 4U> environmentParams{ 1.0F, 1.0F, 0.25F, 0.0F };
};

struct LightCandidate {
    const LightRenderProxyDesc* light = nullptr;
    std::uint64_t entityId = 0;
    float score = 0.0F;
};

[[nodiscard]] float DegreesToRadians(float degrees) noexcept {
    return degrees * 0.017453292519943295769F;
}

[[nodiscard]] std::array<float, 4> CameraPosition(const SceneRenderCamera* camera) noexcept {
    if (camera == nullptr) {
        return { 0.0F, 0.0F, 0.0F, 1.0F };
    }
    const std::array<float, 16>& view = camera->view;
    const float tx = view[12];
    const float ty = view[13];
    const float tz = view[14];
    return {
        -(view[0] * tx + view[1] * ty + view[2] * tz),
        -(view[4] * tx + view[5] * ty + view[6] * tz),
        -(view[8] * tx + view[9] * ty + view[10] * tz),
        1.0F,
    };
}

[[nodiscard]] Basis BasisFromQuat(const std::array<float, 4>& q) noexcept {
    const float x = q[0];
    const float y = q[1];
    const float z = q[2];
    const float w = q[3];
    const float x2 = x + x;
    const float y2 = y + y;
    const float z2 = z + z;
    const float xz = x * z2;
    const float yy = y * y2;
    const float yz = y * z2;
    const float wx = w * x2;
    const float wy = w * y2;
    const float xx = x * x2;

    return Basis{
        .zx = xz + wy,
        .zy = yz - wx,
        .zz = 1.0F - (xx + yy),
    };
}

void Normalize(float& x, float& y, float& z) noexcept {
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length <= 0.0001F) {
        x = 0.0F;
        y = 0.0F;
        z = 1.0F;
        return;
    }

    x /= length;
    y /= length;
    z /= length;
}

[[nodiscard]] float LightKindValue(RenderLightKind kind) noexcept {
    switch (kind) {
    case RenderLightKind::Directional:
        return 0.0F;
    case RenderLightKind::Point:
        return 1.0F;
    case RenderLightKind::Spot:
        return 2.0F;
    }
    return 1.0F;
}

[[nodiscard]] float MaxColorChannel(const LightRenderProxyDesc& light) noexcept {
    return std::max(std::max(light.color[0], light.color[1]), light.color[2]);
}

[[nodiscard]] bool IsValidForwardLight(const LightRenderProxyDesc& light) noexcept {
    if (!light.visible || light.intensity <= 0.0F || MaxColorChannel(light) <= 0.0F) {
        return false;
    }
    if (light.kind != RenderLightKind::Directional && light.range <= 0.0F) {
        return false;
    }
    return true;
}

[[nodiscard]] float LightSelectionScore(const LightRenderProxyDesc& light, const std::array<float, 4>& cameraPosition) noexcept {
    const float radiance = std::max(light.intensity, 0.0F) * std::max(MaxColorChannel(light), 0.0F);
    if (light.kind == RenderLightKind::Directional) {
        return 1'000'000.0F + radiance;
    }

    const float dx = light.position[0] - cameraPosition[0];
    const float dy = light.position[1] - cameraPosition[1];
    const float dz = light.position[2] - cameraPosition[2];
    const float distanceToCamera = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float range = std::max(light.range, 0.0001F);
    const float rangeWeight = std::clamp(1.0F - distanceToCamera / range, 0.0F, 1.0F);
    float score = radiance * (0.05F + rangeWeight * rangeWeight);
    if (light.kind == RenderLightKind::Spot) {
        Basis basis = BasisFromQuat(light.rotation);
        Normalize(basis.zx, basis.zy, basis.zz);
        const float invDistance = distanceToCamera > 0.0001F ? 1.0F / distanceToCamera : 0.0F;
        const float toCameraX = -dx * invDistance;
        const float toCameraY = -dy * invDistance;
        const float toCameraZ = -dz * invDistance;
        const float innerCos = std::cos(DegreesToRadians(light.innerConeDegrees));
        const float outerCos = std::cos(DegreesToRadians(light.outerConeDegrees));
        const float highCone = std::max(innerCos, outerCos);
        const float lowCone = std::min(innerCos, outerCos);
        const float coneCos = basis.zx * toCameraX + basis.zy * toCameraY + basis.zz * toCameraZ;
        const float coneWeight = std::clamp((coneCos - lowCone) / std::max(highCone - lowCone, 0.001F), 0.0F, 1.0F);
        score *= 0.1F + coneWeight * coneWeight;
    }
    return score;
}

[[nodiscard]] bool CandidateIsBetter(const LightCandidate& lhs, const LightCandidate& rhs) noexcept {
    if (lhs.light == nullptr) {
        return false;
    }
    if (rhs.light == nullptr) {
        return true;
    }
    if (lhs.score != rhs.score) {
        return lhs.score > rhs.score;
    }
    return lhs.entityId < rhs.entityId;
}

void InsertSelectedLight(std::array<LightCandidate, kMaxSceneForwardLights>& selected, std::uint32_t& selectedCount, std::uint32_t capacity, LightCandidate candidate) noexcept {
    if (capacity == 0U) {
        return;
    }

    if (selectedCount < capacity) {
        selected[selectedCount] = candidate;
        ++selectedCount;
    } else if (CandidateIsBetter(candidate, selected[capacity - 1U])) {
        selected[capacity - 1U] = candidate;
    } else {
        return;
    }

    for (std::uint32_t index = selectedCount; index > 1U; --index) {
        if (!CandidateIsBetter(selected[index - 1U], selected[index - 2U])) {
            break;
        }
        std::swap(selected[index - 1U], selected[index - 2U]);
    }
}

bool PackLight(const LightRenderProxyDesc& light, std::uint32_t slot, PackedSceneLighting& lighting) noexcept {
    if (slot >= kMaxSceneForwardLights) {
        return false;
    }

    const std::uint32_t offset = slot * 4U;
    Basis basis = BasisFromQuat(light.rotation);
    Normalize(basis.zx, basis.zy, basis.zz);

    lighting.dirKind[offset + 0U] = basis.zx;
    lighting.dirKind[offset + 1U] = basis.zy;
    lighting.dirKind[offset + 2U] = basis.zz;
    lighting.dirKind[offset + 3U] = LightKindValue(light.kind);
    lighting.positionRange[offset + 0U] = light.position[0];
    lighting.positionRange[offset + 1U] = light.position[1];
    lighting.positionRange[offset + 2U] = light.position[2];
    lighting.positionRange[offset + 3U] = std::max(light.range, 0.0F);
    lighting.colorIntensity[offset + 0U] = std::max(light.color[0], 0.0F);
    lighting.colorIntensity[offset + 1U] = std::max(light.color[1], 0.0F);
    lighting.colorIntensity[offset + 2U] = std::max(light.color[2], 0.0F);
    lighting.colorIntensity[offset + 3U] = light.intensity;

    const float innerCos = std::cos(DegreesToRadians(light.innerConeDegrees));
    const float outerCos = std::cos(DegreesToRadians(light.outerConeDegrees));
    lighting.spot[offset + 0U] = std::max(innerCos, outerCos);
    lighting.spot[offset + 1U] = std::min(innerCos, outerCos);
    lighting.spot[offset + 2U] = 0.0F;
    lighting.spot[offset + 3U] = 0.0F;
    return true;
}

[[nodiscard]] std::uint32_t ClampedForwardLightBudget(SceneRenderLightingConfig config) noexcept {
    if (config.maxForwardLights == 0U) {
        return 0U;
    }
    return std::min<std::uint32_t>(config.maxForwardLights, kMaxSceneForwardLights);
}

[[nodiscard]] std::uint32_t ShadowFilterSampleCount(SceneRenderShadowFilter filter) noexcept {
    switch (filter) {
    case SceneRenderShadowFilter::Hard:
        return 1U;
    case SceneRenderShadowFilter::Pcf3x3:
        return 9U;
    }
    return 9U;
}

[[nodiscard]] float EnvironmentModeValue(SceneRenderEnvironmentMode mode) noexcept {
    switch (mode) {
    case SceneRenderEnvironmentMode::Disabled:
        return 0.0F;
    case SceneRenderEnvironmentMode::Constant:
        return 1.0F;
    case SceneRenderEnvironmentMode::Hemisphere:
        return 2.0F;
    }
    return 1.0F;
}

[[nodiscard]] std::uint32_t EnvironmentSampleCount(SceneRenderEnvironmentMode mode) noexcept {
    switch (mode) {
    case SceneRenderEnvironmentMode::Disabled:
        return 0U;
    case SceneRenderEnvironmentMode::Constant:
        return 1U;
    case SceneRenderEnvironmentMode::Hemisphere:
        return 2U;
    }
    return 1U;
}

[[nodiscard]] PackedSceneLighting BuildSceneLighting(const RenderScene& renderScene, SceneRenderSubmitStats& stats, SceneRenderLightingConfig config, const SceneRenderCamera* camera) noexcept {
    PackedSceneLighting lighting{};
    const std::uint32_t capacity = ClampedForwardLightBudget(config);
    lighting.params[1] = static_cast<float>(capacity);
    const float ambientIntensity = std::max(config.ambientIntensity, 0.0F);
    lighting.ambient = {
        std::max(config.ambientColor[0], 0.0F) * ambientIntensity,
        std::max(config.ambientColor[1], 0.0F) * ambientIntensity,
        std::max(config.ambientColor[2], 0.0F) * ambientIntensity,
        1.0F,
    };
    lighting.environmentZenith = {
        std::max(config.environmentZenithColor[0], 0.0F) * ambientIntensity,
        std::max(config.environmentZenithColor[1], 0.0F) * ambientIntensity,
        std::max(config.environmentZenithColor[2], 0.0F) * ambientIntensity,
        1.0F,
    };
    lighting.environmentGround = {
        std::max(config.environmentGroundColor[0], 0.0F) * ambientIntensity,
        std::max(config.environmentGroundColor[1], 0.0F) * ambientIntensity,
        std::max(config.environmentGroundColor[2], 0.0F) * ambientIntensity,
        1.0F,
    };
    lighting.environmentParams = {
        EnvironmentModeValue(config.environmentMode),
        std::max(config.environmentDiffuseIntensity, 0.0F),
        std::max(config.environmentSpecularIntensity, 0.0F),
        0.0F,
    };
    stats.submittedEnvironmentLightingCount = config.environmentMode == SceneRenderEnvironmentMode::Disabled ? 0U : 1U;
    stats.environmentLightingMode = static_cast<std::uint32_t>(config.environmentMode) + 1U;
    stats.environmentLightingSampleCount = EnvironmentSampleCount(config.environmentMode);
    stats.sceneLightCount = static_cast<std::uint32_t>(renderScene.LightProxies().size());
    stats.forwardLightCapacity = capacity;
    std::array<LightCandidate, kMaxSceneForwardLights> selected{};
    std::uint32_t selectedCount = 0U;
    std::uint32_t validLightCount = 0U;
    const std::array<float, 4> cameraPosition = CameraPosition(camera);
    for (const auto& [entityId, proxy] : renderScene.LightProxies()) {
        if (!IsValidForwardLight(proxy.desc)) {
            ++stats.invalidLightCount;
            continue;
        }
        ++validLightCount;
        InsertSelectedLight(selected, selectedCount, capacity, LightCandidate{
            .light = &proxy.desc,
            .entityId = entityId,
            .score = LightSelectionScore(proxy.desc, cameraPosition),
        });
    }
    for (std::uint32_t slot = 0U; slot < selectedCount; ++slot) {
        if (selected[slot].light != nullptr && PackLight(*selected[slot].light, slot, lighting)) {
            ++stats.submittedForwardLightCount;
        }
    }
    stats.skippedForwardLightCount = validLightCount - stats.submittedForwardLightCount;
    lighting.params[0] = static_cast<float>(stats.submittedForwardLightCount);
    return lighting;
}

[[nodiscard]] bgfx::TextureHandle CreateFallbackWhiteTexture() {
    const std::uint32_t white = 0xFFFF'FFFFU;
    const bgfx::Memory* memory = bgfx::copy(&white, sizeof(white));
    return bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, memory);
}

[[nodiscard]] bgfx::TextureHandle CreateFallbackTexture(std::uint32_t rgba) {
    const bgfx::Memory* memory = bgfx::copy(&rgba, sizeof(rgba));
    return bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, memory);
}

[[nodiscard]] bgfx::TextureHandle ResolveMaterialTexture(
    RenderTextureHandle directTexture,
    std::uint64_t textureAssetId,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    RenderTextureHandle textureHandle = directTexture;
    if (!textureHandle.IsValid() && textureAssetId != 0U) {
        textureHandle = resourceMap.ResolveTexture(textureAssetId);
    }

    const RenderTextureResource* texture = resources.FindTexture(textureHandle);
    return texture == nullptr || !bgfx::isValid(texture->texture) ? fallback : texture->texture;
}

[[nodiscard]] bgfx::TextureHandle ResolveAlbedoTexture(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    return material == nullptr
        ? fallback
        : ResolveMaterialTexture(material->albedoTexture, material->albedoTextureAssetId, resources, resourceMap, fallback);
}

[[nodiscard]] bgfx::TextureHandle ResolveNormalTexture(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    return material == nullptr
        ? fallback
        : ResolveMaterialTexture(material->normalTexture, material->normalTextureAssetId, resources, resourceMap, fallback);
}

[[nodiscard]] bgfx::TextureHandle ResolveMetallicRoughnessTexture(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    return material == nullptr
        ? fallback
        : ResolveMaterialTexture(material->metallicRoughnessTexture, material->metallicRoughnessTextureAssetId, resources, resourceMap, fallback);
}

[[nodiscard]] bgfx::TextureHandle ResolveEmissiveTexture(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    return material == nullptr
        ? fallback
        : ResolveMaterialTexture(material->emissiveTexture, material->emissiveTextureAssetId, resources, resourceMap, fallback);
}

[[nodiscard]] bgfx::TextureHandle ResolveOcclusionTexture(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    return material == nullptr
        ? fallback
        : ResolveMaterialTexture(material->occlusionTexture, material->occlusionTextureAssetId, resources, resourceMap, fallback);
}

[[nodiscard]] std::array<float, 4> MaterialParams(const RenderMaterialResource* material) noexcept {
    if (material == nullptr) {
        return { 0.0F, 1.0F, 1.0F, 0.5F };
    }
    return {
        std::clamp(material->metallicFactor, 0.0F, 1.0F),
        std::clamp(material->roughnessFactor, 0.04F, 1.0F),
        material->normalScale,
        material->alphaCutoff,
    };
}

[[nodiscard]] std::array<float, 4> MaterialEmissive(const RenderMaterialResource* material) noexcept {
    if (material == nullptr) {
        return { 0.0F, 0.0F, 0.0F, 1.0F };
    }
    return {
        std::max(material->emissiveColor[0], 0.0F),
        std::max(material->emissiveColor[1], 0.0F),
        std::max(material->emissiveColor[2], 0.0F),
        std::max(material->emissiveStrength, 0.0F),
    };
}

[[nodiscard]] float MaterialAlphaModeValue(RenderMaterialAlphaMode mode) noexcept {
    switch (mode) {
    case RenderMaterialAlphaMode::Opaque:
        return 0.0F;
    case RenderMaterialAlphaMode::Mask:
        return 1.0F;
    case RenderMaterialAlphaMode::Blend:
        return 2.0F;
    }
    return 0.0F;
}

[[nodiscard]] std::array<float, 4> MaterialFlags(const RenderMaterialResource* material) noexcept {
    return {
        MaterialAlphaModeValue(material == nullptr ? RenderMaterialAlphaMode::Opaque : material->alphaMode),
        std::clamp(material == nullptr ? 1.0F : material->occlusionStrength, 0.0F, 1.0F),
        0.0F,
        0.0F,
    };
}

[[nodiscard]] std::uint32_t DrawGroupInstanceCapacity(std::span<const SceneRenderDrawGroup> drawGroups) noexcept {
    std::uint32_t capacity = 0U;
    for (const SceneRenderDrawGroup& group : drawGroups) {
        capacity += static_cast<std::uint32_t>(group.instances.capacity());
    }
    return capacity;
}

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

} // namespace

bool SceneMeshSubmitter::Initialize() {
    if (IsInitialized()) {
        return true;
    }

    meshProgram_ = ShaderLoader::LoadProgram("vs_mesh_instanced.sc", "fs_mesh_instanced.sc");
    if (!bgfx::isValid(meshProgram_)) {
        return false;
    }
    shadowProgram_ = ShaderLoader::LoadProgram("vs_mesh_shadow_instanced.sc", "fs_mesh_shadow_instanced.sc");
    if (!bgfx::isValid(shadowProgram_)) {
        Shutdown();
        return false;
    }

    albedoSampler_ = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
    shadowSampler_ = bgfx::createUniform("s_shadowMap", bgfx::UniformType::Sampler);
    normalSampler_ = bgfx::createUniform("s_normal", bgfx::UniformType::Sampler);
    metallicRoughnessSampler_ = bgfx::createUniform("s_metallicRoughness", bgfx::UniformType::Sampler);
    occlusionSampler_ = bgfx::createUniform("s_occlusion", bgfx::UniformType::Sampler);
    emissiveSampler_ = bgfx::createUniform("s_emissive", bgfx::UniformType::Sampler);
    materialParamsUniform_ = bgfx::createUniform("u_materialParams", bgfx::UniformType::Vec4);
    materialEmissiveUniform_ = bgfx::createUniform("u_materialEmissive", bgfx::UniformType::Vec4);
    materialFlagsUniform_ = bgfx::createUniform("u_materialFlags", bgfx::UniformType::Vec4);
    cameraPositionUniform_ = bgfx::createUniform("u_cameraPosition", bgfx::UniformType::Vec4);
    lightDirKindUniform_ = bgfx::createUniform("u_lightDirKind", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightPositionRangeUniform_ = bgfx::createUniform("u_lightPositionRange", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightColorIntensityUniform_ = bgfx::createUniform("u_lightColorIntensity", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightSpotUniform_ = bgfx::createUniform("u_lightSpot", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightParamsUniform_ = bgfx::createUniform("u_lightParams", bgfx::UniformType::Vec4);
    ambientColorUniform_ = bgfx::createUniform("u_ambientColor", bgfx::UniformType::Vec4);
    environmentZenithUniform_ = bgfx::createUniform("u_environmentZenith", bgfx::UniformType::Vec4);
    environmentGroundUniform_ = bgfx::createUniform("u_environmentGround", bgfx::UniformType::Vec4);
    environmentParamsUniform_ = bgfx::createUniform("u_environmentParams", bgfx::UniformType::Vec4);
    shadowViewProjUniform_ = bgfx::createUniform("u_shadowViewProj", bgfx::UniformType::Mat4);
    shadowParamsUniform_ = bgfx::createUniform("u_shadowParams", bgfx::UniformType::Vec4);
    fallbackWhiteTexture_ = CreateFallbackWhiteTexture();
    fallbackNormalTexture_ = CreateFallbackTexture(0xFFFF'8080U);
    fallbackBlackTexture_ = CreateFallbackTexture(0xFF00'0000U);
    if (!bgfx::isValid(albedoSampler_) ||
        !bgfx::isValid(shadowSampler_) ||
        !bgfx::isValid(normalSampler_) ||
        !bgfx::isValid(metallicRoughnessSampler_) ||
        !bgfx::isValid(occlusionSampler_) ||
        !bgfx::isValid(emissiveSampler_) ||
        !bgfx::isValid(materialParamsUniform_) ||
        !bgfx::isValid(materialEmissiveUniform_) ||
        !bgfx::isValid(materialFlagsUniform_) ||
        !bgfx::isValid(cameraPositionUniform_) ||
        !bgfx::isValid(lightDirKindUniform_) ||
        !bgfx::isValid(lightPositionRangeUniform_) ||
        !bgfx::isValid(lightColorIntensityUniform_) ||
        !bgfx::isValid(lightSpotUniform_) ||
        !bgfx::isValid(lightParamsUniform_) ||
        !bgfx::isValid(ambientColorUniform_) ||
        !bgfx::isValid(environmentZenithUniform_) ||
        !bgfx::isValid(environmentGroundUniform_) ||
        !bgfx::isValid(environmentParamsUniform_) ||
        !bgfx::isValid(shadowViewProjUniform_) ||
        !bgfx::isValid(shadowParamsUniform_) ||
        !bgfx::isValid(fallbackWhiteTexture_) ||
        !bgfx::isValid(fallbackNormalTexture_) ||
        !bgfx::isValid(fallbackBlackTexture_)) {
        Shutdown();
        return false;
    }

    return true;
}

void SceneMeshSubmitter::Shutdown() {
    if (bgfx::isValid(fallbackBlackTexture_)) {
        bgfx::destroy(fallbackBlackTexture_);
        fallbackBlackTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(fallbackNormalTexture_)) {
        bgfx::destroy(fallbackNormalTexture_);
        fallbackNormalTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(fallbackWhiteTexture_)) {
        bgfx::destroy(fallbackWhiteTexture_);
        fallbackWhiteTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(shadowParamsUniform_)) {
        bgfx::destroy(shadowParamsUniform_);
        shadowParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(shadowViewProjUniform_)) {
        bgfx::destroy(shadowViewProjUniform_);
        shadowViewProjUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(environmentParamsUniform_)) {
        bgfx::destroy(environmentParamsUniform_);
        environmentParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(environmentGroundUniform_)) {
        bgfx::destroy(environmentGroundUniform_);
        environmentGroundUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(environmentZenithUniform_)) {
        bgfx::destroy(environmentZenithUniform_);
        environmentZenithUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(ambientColorUniform_)) {
        bgfx::destroy(ambientColorUniform_);
        ambientColorUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightParamsUniform_)) {
        bgfx::destroy(lightParamsUniform_);
        lightParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightSpotUniform_)) {
        bgfx::destroy(lightSpotUniform_);
        lightSpotUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightColorIntensityUniform_)) {
        bgfx::destroy(lightColorIntensityUniform_);
        lightColorIntensityUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightPositionRangeUniform_)) {
        bgfx::destroy(lightPositionRangeUniform_);
        lightPositionRangeUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightDirKindUniform_)) {
        bgfx::destroy(lightDirKindUniform_);
        lightDirKindUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(albedoSampler_)) {
        bgfx::destroy(albedoSampler_);
        albedoSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(shadowSampler_)) {
        bgfx::destroy(shadowSampler_);
        shadowSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(cameraPositionUniform_)) {
        bgfx::destroy(cameraPositionUniform_);
        cameraPositionUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(materialFlagsUniform_)) {
        bgfx::destroy(materialFlagsUniform_);
        materialFlagsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(materialEmissiveUniform_)) {
        bgfx::destroy(materialEmissiveUniform_);
        materialEmissiveUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(materialParamsUniform_)) {
        bgfx::destroy(materialParamsUniform_);
        materialParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(emissiveSampler_)) {
        bgfx::destroy(emissiveSampler_);
        emissiveSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(occlusionSampler_)) {
        bgfx::destroy(occlusionSampler_);
        occlusionSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(metallicRoughnessSampler_)) {
        bgfx::destroy(metallicRoughnessSampler_);
        metallicRoughnessSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(normalSampler_)) {
        bgfx::destroy(normalSampler_);
        normalSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(meshProgram_)) {
        bgfx::destroy(meshProgram_);
        meshProgram_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(shadowProgram_)) {
        bgfx::destroy(shadowProgram_);
        shadowProgram_ = BGFX_INVALID_HANDLE;
    }
}

SceneRenderSubmitStats SceneMeshSubmitter::ValidateResourcesInto(
    const RenderScene& renderScene,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    std::vector<SceneRenderDrawGroup>& drawGroupsScratch,
    MeshPipelineBuildResult& pipelineScratch,
    MeshPassType pass,
    const SceneRenderCamera* camera,
    SceneRenderDiagnostics* diagnostics,
    SceneRenderDrawBudget drawBudget,
    SceneRenderLightingConfig lightingConfig) noexcept {
    drawGroupsScratch.reserve(renderScene.MeshProxyCount());
    renderScene.BuildDrawGroups(drawGroupsScratch);
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = pass,
        .drawGroups = &drawGroupsScratch,
        .resources = &resources,
        .resourceMap = &resourceMap,
        .camera = camera,
        .diagnostics = diagnostics,
        .maxDrawCommands = drawBudget.maxDrawCommands,
        .maxVisibleInstances = drawBudget.maxVisibleInstances,
    }, pipelineScratch);
    pipelineScratch.stats.meshDrawGroupScratchCapacity = static_cast<std::uint32_t>(drawGroupsScratch.capacity());
    pipelineScratch.stats.meshDrawGroupInstanceScratchCapacity = DrawGroupInstanceCapacity(drawGroupsScratch);
    pipelineScratch.stats.meshDrawGroupLookupCapacity = static_cast<std::uint32_t>(renderScene.DrawGroupLookupScratchCapacity());
    SceneRenderSubmitStats lightingStats{};
    static_cast<void>(BuildSceneLighting(renderScene, lightingStats, lightingConfig, camera));
    pipelineScratch.stats.sceneLightCount = lightingStats.sceneLightCount;
    pipelineScratch.stats.submittedForwardLightCount = lightingStats.submittedForwardLightCount;
    pipelineScratch.stats.skippedForwardLightCount = lightingStats.skippedForwardLightCount;
    pipelineScratch.stats.invalidLightCount = lightingStats.invalidLightCount;
    pipelineScratch.stats.forwardLightCapacity = lightingStats.forwardLightCapacity;
    pipelineScratch.stats.submittedEnvironmentLightingCount = lightingStats.submittedEnvironmentLightingCount;
    pipelineScratch.stats.environmentLightingMode = lightingStats.environmentLightingMode;
    pipelineScratch.stats.environmentLightingSampleCount = lightingStats.environmentLightingSampleCount;
    if (pass == MeshPassType::ShadowDepth) {
        pipelineScratch.stats.shadowCasterCount = pipelineScratch.stats.visibleMeshCount;
    }
    MeshPipelineProcessor::CountCommandsAsSubmitted(pipelineScratch.stats, pipelineScratch.commands);
    if (pass == MeshPassType::ShadowDepth) {
        pipelineScratch.stats.submittedShadowCasterCount = pipelineScratch.stats.submittedMeshCount;
        pipelineScratch.stats.submittedShadowDrawCallCount = pipelineScratch.stats.submittedDrawCallCount;
        pipelineScratch.stats.shadowMapSize = lightingConfig.shadowMapSize;
        pipelineScratch.stats.shadowFilterSampleCount = ShadowFilterSampleCount(lightingConfig.shadowFilter);
    }
    return pipelineScratch.stats;
}

SceneRenderSubmitStats SceneMeshSubmitter::Submit(
    bgfx::ViewId viewId,
    const RenderScene& renderScene,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    MeshPassType pass,
    const SceneRenderCamera* camera,
    SceneRenderDiagnostics* diagnostics,
    SceneRenderDrawBudget drawBudget,
    SceneRenderLightingConfig lightingConfig,
    const SceneRenderShadowMapBinding* shadowMap) const {
    SceneRenderSubmitStats stats{};
    if (!IsInitialized()) {
        return stats;
    }

    drawGroupsScratch_.reserve(renderScene.MeshProxyCount());
    renderScene.BuildDrawGroups(drawGroupsScratch_);
    SceneRenderSubmitStats lightingStats{};
    const PackedSceneLighting lighting = BuildSceneLighting(renderScene, lightingStats, lightingConfig, camera);
    const std::array<float, 4> cameraPosition = CameraPosition(camera);
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = pass,
        .drawGroups = &drawGroupsScratch_,
        .resources = &resources,
        .resourceMap = &resourceMap,
        .camera = camera,
        .diagnostics = diagnostics,
        .maxDrawCommands = drawBudget.maxDrawCommands,
        .maxVisibleInstances = drawBudget.maxVisibleInstances,
    }, pipelineScratch_);
    stats = pipelineScratch_.stats;
    stats.sceneLightCount = lightingStats.sceneLightCount;
    stats.submittedForwardLightCount = lightingStats.submittedForwardLightCount;
    stats.skippedForwardLightCount = lightingStats.skippedForwardLightCount;
    stats.invalidLightCount = lightingStats.invalidLightCount;
    stats.forwardLightCapacity = lightingStats.forwardLightCapacity;
    stats.submittedEnvironmentLightingCount = lightingStats.submittedEnvironmentLightingCount;
    stats.environmentLightingMode = lightingStats.environmentLightingMode;
    stats.environmentLightingSampleCount = lightingStats.environmentLightingSampleCount;
    if (pass == MeshPassType::ShadowDepth) {
        stats.shadowCasterCount = stats.visibleMeshCount;
        stats.shadowMapSize = lightingConfig.shadowMapSize;
        stats.shadowFilterSampleCount = ShadowFilterSampleCount(lightingConfig.shadowFilter);
    }
    stats.meshDrawGroupScratchCapacity = static_cast<std::uint32_t>(drawGroupsScratch_.capacity());
    stats.meshDrawGroupInstanceScratchCapacity = DrawGroupInstanceCapacity(drawGroupsScratch_);
    stats.meshDrawGroupLookupCapacity = static_cast<std::uint32_t>(renderScene.DrawGroupLookupScratchCapacity());
    for (const MeshDrawCommand& command : pipelineScratch_.commands) {
        const std::uint32_t instanceCount = static_cast<std::uint32_t>(command.instances.size());
        const std::uint32_t availableInstances = bgfx::getAvailInstanceDataBuffer(instanceCount, RenderInstanceBuffer::Stride());
        if (availableInstances == 0U) {
            stats.droppedInstanceCount += instanceCount;
            EmitGroupDiagnostics(diagnostics, SceneRenderDiagnosticKind::DroppedInstances, SceneRenderDiagnosticSeverity::Warning, command, 0U, instanceCount);
            continue;
        }
        if (availableInstances < instanceCount) {
            stats.droppedInstanceCount += instanceCount - availableInstances;
            EmitGroupDiagnostics(
                diagnostics,
                SceneRenderDiagnosticKind::DroppedInstances,
                SceneRenderDiagnosticSeverity::Warning,
                command,
                availableInstances,
                instanceCount - availableInstances);
        }

        bgfx::InstanceDataBuffer instanceBuffer{};
        bgfx::allocInstanceDataBuffer(&instanceBuffer, availableInstances, RenderInstanceBuffer::Stride());
        auto* instanceData = reinterpret_cast<RenderInstanceData*>(instanceBuffer.data);
        RenderInstanceBuffer::Copy(
            std::span<RenderInstanceData>(instanceData, availableInstances),
            std::span<const SceneRenderMeshInstance>(command.instances.data(), availableInstances),
            command.materialResource,
            pass != MeshPassType::ShadowDepth);

        bgfx::setInstanceDataBuffer(&instanceBuffer, 0U, static_cast<std::uint32_t>(availableInstances));
        bgfx::setVertexBuffer(0, command.meshResource->vertexBuffer);
        bgfx::setIndexBuffer(command.meshResource->indexBuffer, command.indexStart, command.indexCount);
        const bgfx::TextureHandle albedoTexture = ResolveAlbedoTexture(command.materialResource, resources, resourceMap, fallbackWhiteTexture_);
        const std::array<float, 4> materialParams = MaterialParams(command.materialResource);
        const std::array<float, 4> materialFlags = MaterialFlags(command.materialResource);
        bgfx::setTexture(0U, albedoSampler_, albedoTexture);
        bgfx::setUniform(materialParamsUniform_, materialParams.data());
        bgfx::setUniform(materialFlagsUniform_, materialFlags.data());

        bgfx::ProgramHandle program = meshProgram_;
        if (pass == MeshPassType::ShadowDepth) {
            program = shadowProgram_;
        } else {
            const bgfx::TextureHandle normalTexture = ResolveNormalTexture(command.materialResource, resources, resourceMap, fallbackNormalTexture_);
            const bgfx::TextureHandle metallicRoughnessTexture = ResolveMetallicRoughnessTexture(command.materialResource, resources, resourceMap, fallbackWhiteTexture_);
            const bgfx::TextureHandle occlusionTexture = ResolveOcclusionTexture(command.materialResource, resources, resourceMap, fallbackWhiteTexture_);
            const bgfx::TextureHandle emissiveTexture = ResolveEmissiveTexture(command.materialResource, resources, resourceMap, fallbackBlackTexture_);
            const std::array<float, 4> materialEmissive = MaterialEmissive(command.materialResource);
            const std::array<float, 4> disabledShadowParams{};
            bgfx::setTexture(1U, normalSampler_, normalTexture);
            bgfx::setTexture(2U, metallicRoughnessSampler_, metallicRoughnessTexture);
            bgfx::setTexture(3U, occlusionSampler_, occlusionTexture);
            bgfx::setTexture(4U, emissiveSampler_, emissiveTexture);
            bgfx::setTexture(5U, shadowSampler_, shadowMap != nullptr && shadowMap->IsValid() ? shadowMap->depthTexture : fallbackWhiteTexture_);
            bgfx::setUniform(materialEmissiveUniform_, materialEmissive.data());
            bgfx::setUniform(cameraPositionUniform_, cameraPosition.data());
            bgfx::setUniform(lightDirKindUniform_, lighting.dirKind.data(), kMaxSceneForwardLights);
            bgfx::setUniform(lightPositionRangeUniform_, lighting.positionRange.data(), kMaxSceneForwardLights);
            bgfx::setUniform(lightColorIntensityUniform_, lighting.colorIntensity.data(), kMaxSceneForwardLights);
            bgfx::setUniform(lightSpotUniform_, lighting.spot.data(), kMaxSceneForwardLights);
            bgfx::setUniform(lightParamsUniform_, lighting.params.data());
            bgfx::setUniform(ambientColorUniform_, lighting.ambient.data());
            bgfx::setUniform(environmentZenithUniform_, lighting.environmentZenith.data());
            bgfx::setUniform(environmentGroundUniform_, lighting.environmentGround.data());
            bgfx::setUniform(environmentParamsUniform_, lighting.environmentParams.data());
            bgfx::setUniform(shadowViewProjUniform_, shadowMap != nullptr && shadowMap->IsValid() ? shadowMap->lightViewProjection.data() : disabledShadowParams.data());
            bgfx::setUniform(shadowParamsUniform_, shadowMap != nullptr && shadowMap->IsValid() ? shadowMap->params.data() : disabledShadowParams.data());
        }
        bgfx::setState(command.state);
        bgfx::submit(viewId, program);

        stats.submittedMeshCount += availableInstances;
        ++stats.submittedDrawCallCount;
        ++stats.submittedDrawGroupCount;
        if (pass == MeshPassType::ShadowDepth) {
            stats.submittedShadowCasterCount += availableInstances;
            ++stats.submittedShadowDrawCallCount;
        }
        stats.instanceUploadBytes += static_cast<std::uint64_t>(availableInstances) * RenderInstanceBuffer::Stride();
    }

    return stats;
}

bool SceneMeshSubmitter::IsInitialized() const noexcept {
    return bgfx::isValid(meshProgram_) &&
        bgfx::isValid(shadowProgram_) &&
        bgfx::isValid(albedoSampler_) &&
        bgfx::isValid(shadowSampler_) &&
        bgfx::isValid(normalSampler_) &&
        bgfx::isValid(metallicRoughnessSampler_) &&
        bgfx::isValid(occlusionSampler_) &&
        bgfx::isValid(emissiveSampler_) &&
        bgfx::isValid(materialParamsUniform_) &&
        bgfx::isValid(materialEmissiveUniform_) &&
        bgfx::isValid(materialFlagsUniform_) &&
        bgfx::isValid(cameraPositionUniform_) &&
        bgfx::isValid(lightDirKindUniform_) &&
        bgfx::isValid(lightPositionRangeUniform_) &&
        bgfx::isValid(lightColorIntensityUniform_) &&
        bgfx::isValid(lightSpotUniform_) &&
        bgfx::isValid(lightParamsUniform_) &&
        bgfx::isValid(ambientColorUniform_) &&
        bgfx::isValid(environmentZenithUniform_) &&
        bgfx::isValid(environmentGroundUniform_) &&
        bgfx::isValid(environmentParamsUniform_) &&
        bgfx::isValid(shadowViewProjUniform_) &&
        bgfx::isValid(shadowParamsUniform_) &&
        bgfx::isValid(fallbackWhiteTexture_) &&
        bgfx::isValid(fallbackNormalTexture_) &&
        bgfx::isValid(fallbackBlackTexture_);
}

} // namespace kb::render
