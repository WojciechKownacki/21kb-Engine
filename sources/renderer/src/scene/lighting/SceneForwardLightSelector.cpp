#include "scene/lighting/SceneForwardLightSelector.hpp"

#include "engine/math/EngineMath.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace kb::render {
namespace {

struct Basis {
    float zx = 0.0F;
    float zy = 0.0F;
    float zz = 1.0F;
};

// LIB-044: delegates to the single canonical kb::math::ToRadians instead
// of an independently-rederived degrees-to-radians constant.
[[nodiscard]] float DegreesToRadians(float degrees) noexcept {
    return kb::math::ToRadians(kb::math::Degrees{ degrees }).Value();
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

[[nodiscard]] bool CandidateIsBetter(const SceneForwardLightCandidate& lhs, const SceneForwardLightCandidate& rhs) noexcept {
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

void InsertSelectedLight(SceneForwardLightSelection& selection, std::uint32_t capacity, SceneForwardLightCandidate candidate) noexcept {
    if (capacity == 0U) {
        return;
    }

    if (selection.selectedCount < capacity) {
        selection.selected[selection.selectedCount] = candidate;
        ++selection.selectedCount;
    } else if (CandidateIsBetter(candidate, selection.selected[capacity - 1U])) {
        selection.selected[capacity - 1U] = candidate;
    } else {
        return;
    }

    for (std::uint32_t index = selection.selectedCount; index > 1U; --index) {
        if (!CandidateIsBetter(selection.selected[index - 1U], selection.selected[index - 2U])) {
            break;
        }
        std::swap(selection.selected[index - 1U], selection.selected[index - 2U]);
    }
}

void AccumulateAdvancedLightStats(const LightRenderProxyDesc& light, SceneRenderSubmitStats& stats, SceneRenderLightingConfig config) noexcept {
    if (light.kind == RenderLightKind::AreaRect || light.kind == RenderLightKind::AreaDisk || light.kind == RenderLightKind::Tube) {
        ++stats.submittedAreaLightCount;
    }
    if (config.volumetricLightingEnabled && light.volumetricScattering > 0.0F) {
        ++stats.submittedVolumetricLightCount;
    }
    if (config.contactShadowsEnabled && light.contactShadowLength > 0.0F) {
        ++stats.contactShadowLightCount;
    }
}

} // namespace

SceneForwardLightSelection SceneForwardLightSelector::Select(
    const RenderScene::LightProxyMap& lights,
    std::uint32_t capacity,
    const std::array<float, 4>& cameraPosition,
    SceneRenderSubmitStats& stats,
    SceneRenderLightingConfig config,
    std::uint32_t cameraCullingMask) noexcept {
    SceneForwardLightSelection selection{};
    for (const auto& [entityId, proxy] : lights) {
        if (!IsValidForwardLight(proxy.desc)) {
            ++stats.invalidLightCount;
            continue;
        }
        // LIB-141: a light whose layer bitmask has no overlap with the current camera's
        // cullingMask does not contribute to that camera's forward lighting - the light-side
        // mirror of MeshPipelinePassPolicy::PassesCullingMask's exact test. Not counted as
        // "invalid" (the light is perfectly valid, just filtered for this camera) or toward
        // validLightCount (which drives skippedForwardLightCount - a masked-out light was
        // never a forward-light candidate for this camera in the first place).
        if ((proxy.desc.layer & cameraCullingMask) == 0U) {
            continue;
        }
        ++selection.validLightCount;
        AccumulateAdvancedLightStats(proxy.desc, stats, config);
        InsertSelectedLight(selection, capacity, SceneForwardLightCandidate{
            .light = &proxy.desc,
            .entityId = entityId,
            .score = LightSelectionScore(proxy.desc, cameraPosition),
        });
    }
    return selection;
}

} // namespace kb::render
