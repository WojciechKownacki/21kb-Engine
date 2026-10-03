#include "PointShadowPassPlanner.hpp"

#include "kb/render/SceneDepthPolicy.hpp"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace kb::render {
namespace {

struct FaceBasis {
    bx::Vec3 direction;
    bx::Vec3 up;
};

// Must match KbPointShadowFace in point_shadow.sh.
constexpr std::array<FaceBasis, ScenePointShadowBinding::kFaceCount> kFaces{{
    {{ 1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }},
    {{ -1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }},
    {{ 0.0F, 1.0F, 0.0F }, { 0.0F, 0.0F, -1.0F }},
    {{ 0.0F, -1.0F, 0.0F }, { 0.0F, 0.0F, 1.0F }},
    {{ 0.0F, 0.0F, 1.0F }, { 0.0F, 1.0F, 0.0F }},
    {{ 0.0F, 0.0F, -1.0F }, { 0.0F, 1.0F, 0.0F }},
}};

[[nodiscard]] bool CastsPointShadow(const LightRenderProxyDesc& light) noexcept {
    return light.visible && light.castsShadow && light.kind == RenderLightKind::Point &&
        light.intensity > 0.0F && light.range > 0.0F && std::isfinite(light.range);
}

[[nodiscard]] float DistanceSquared(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept {
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

} // namespace

PointShadowLightSelection PointShadowPassPlanner::Select(
    const RenderScene& renderScene,
    const std::array<float, 3>* cameraPosition,
    std::uint32_t cameraCullingMask) noexcept {
    struct Candidate {
        float distanceSquared = 0.0F;
        std::uint64_t entityId = 0U;
        const LightRenderProxyDesc* light = nullptr;
    };
    std::array<Candidate, ScenePointShadowBinding::kMaxLights> best{};
    std::uint32_t count = 0U;
    const std::array<float, 3> origin{};
    const std::array<float, 3>& reference = cameraPosition != nullptr ? *cameraPosition : origin;
    for (const auto& [entityId, proxy] : renderScene.LightProxies()) {
        const LightRenderProxyDesc& light = proxy.desc;
        if (!CastsPointShadow(light) || (light.layer & cameraCullingMask) == 0U) {
            continue;
        }
        const Candidate candidate{ DistanceSquared(light.position, reference), entityId, &light };
        const auto closer = [](const Candidate& lhs, const Candidate& rhs) noexcept {
            return lhs.distanceSquared != rhs.distanceSquared ? lhs.distanceSquared < rhs.distanceSquared
                                                              : lhs.entityId < rhs.entityId;
        };
        std::uint32_t position = count;
        if (count < ScenePointShadowBinding::kMaxLights) {
            ++count;
        } else if (closer(candidate, best[count - 1U])) {
            position = count - 1U;
        } else {
            continue;
        }
        best[position] = candidate;
        while (position > 0U && closer(best[position], best[position - 1U])) {
            std::swap(best[position], best[position - 1U]);
            --position;
        }
    }
    PointShadowLightSelection selection{};
    selection.count = count;
    for (std::uint32_t index = 0U; index < count; ++index) {
        selection.lights[index] = best[index].light;
        selection.entityIds[index] = best[index].entityId;
    }
    return selection;
}

std::uint32_t PointShadowPassPlanner::TileSizeFor(const SceneRenderLightingConfig& lightingConfig) noexcept {
    // One tile per face: half the directional map size keeps the atlas near 3072 x 2048 at the default.
    return std::clamp(lightingConfig.shadowMapSize / 2U, 128U, 1024U);
}

PointShadowSetup PointShadowPassPlanner::Build(
    const RenderScene& renderScene,
    const SceneRenderLightingConfig& lightingConfig,
    const std::array<float, 3>* cameraPosition,
    std::uint32_t cameraCullingMask,
    bgfx::TextureHandle depthTexture) noexcept {
    PointShadowSetup setup{};
    if (!lightingConfig.shadowsEnabled) {
        return setup;
    }
    const PointShadowLightSelection selection = Select(renderScene, cameraPosition, cameraCullingMask);
    if (selection.count == 0U) {
        return setup;
    }

    const bool homogeneousDepth = SceneDepthPolicy::HomogeneousDepth();
    setup.tileSize = TileSizeFor(lightingConfig);
    setup.atlasWidth = setup.tileSize * ScenePointShadowBinding::kFaceCount;
    setup.atlasHeight = setup.tileSize * ScenePointShadowBinding::kMaxLights;
    setup.lightCount = selection.count;

    ScenePointShadowBinding& binding = setup.binding;
    binding.depthTexture = depthTexture;
    binding.lightCount = selection.count;
    binding.strength = std::clamp(lightingConfig.shadowStrength, 0.0F, 1.0F);
    const float tanHalfFov = std::tan(bx::toRad(kFaceFovDegrees * 0.5F));
    binding.atlas = {
        1.0F / static_cast<float>(setup.atlasWidth),
        1.0F / static_cast<float>(setup.atlasHeight),
        static_cast<float>(setup.tileSize),
        tanHalfFov,
    };
    for (std::uint32_t slot = 0U; slot < selection.count; ++slot) {
        const LightRenderProxyDesc& light = *selection.lights[slot];
        const float far = std::max(light.range, 0.1F);
        const float near = std::clamp(far * 0.002F, 0.02F, 0.25F);
        binding.entityId[slot] = selection.entityIds[slot];
        binding.positionRange[slot * 4U + 0U] = light.position[0];
        binding.positionRange[slot * 4U + 1U] = light.position[1];
        binding.positionRange[slot * 4U + 2U] = light.position[2];
        binding.positionRange[slot * 4U + 3U] = far;
        binding.depthParams[slot * 4U + 0U] = near;
        binding.depthParams[slot * 4U + 1U] = std::max(lightingConfig.shadowDepthBias * 10.0F, 0.01F);
        const bx::Vec3 eye{ light.position[0], light.position[1], light.position[2] };
        for (std::uint32_t face = 0U; face < ScenePointShadowBinding::kFaceCount; ++face) {
            PointShadowFace& output = setup.faces[slot][face];
            const bx::Vec3 at{ eye.x + kFaces[face].direction.x, eye.y + kFaces[face].direction.y, eye.z + kFaces[face].direction.z };
            bx::mtxLookAt(output.camera.view.data(), eye, at, kFaces[face].up);
            SceneDepthPolicy::MakePerspective(output.camera.projection.data(), kFaceFovDegrees, 1.0F, near, far, homogeneousDepth);
            output.camera.cullingMask = cameraCullingMask;
            output.tileColumn = face;
            output.tileRow = slot;
        }
    }
    setup.valid = true;
    return setup;
}

} // namespace kb::render
