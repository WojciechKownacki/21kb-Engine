#include "kb/render/shadow/DirectionalShadowPassPlanner.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "DirectionalShadowLightSelector.hpp"
#include "ShadowCasterBoundsCollector.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace kb::render {
namespace {

struct Basis {
    float zx = 0.0F;
    float zy = 0.0F;
    float zz = 1.0F;
};

[[nodiscard]] std::array<float, 16> MultiplyColumnMajor(const std::array<float, 16>& lhs, const std::array<float, 16>& rhs) noexcept {
    std::array<float, 16> out{};
    for (std::uint32_t column = 0U; column < 4U; ++column) {
        for (std::uint32_t row = 0U; row < 4U; ++row) {
            out[column * 4U + row] =
                lhs[0U * 4U + row] * rhs[column * 4U + 0U] +
                lhs[1U * 4U + row] * rhs[column * 4U + 1U] +
                lhs[2U * 4U + row] * rhs[column * 4U + 2U] +
                lhs[3U * 4U + row] * rhs[column * 4U + 3U];
        }
    }
    return out;
}

[[nodiscard]] std::array<float, 16> ShadowTextureMatrix(bool homogeneousDepth) noexcept {
    return std::array<float, 16>{
        0.5F, 0.0F, 0.0F, 0.0F,
        0.0F, 0.5F, 0.0F, 0.0F,
        0.0F, 0.0F, homogeneousDepth ? 0.5F : 1.0F, 0.0F,
        0.5F, 0.5F, homogeneousDepth ? 0.5F : 0.0F, 1.0F,
    };
}

[[nodiscard]] std::array<float, 3> TransformPointColumnMajor(const std::array<float, 16>& matrix, const std::array<float, 3>& point) noexcept {
    return {
        matrix[0] * point[0] + matrix[4] * point[1] + matrix[8] * point[2] + matrix[12],
        matrix[1] * point[0] + matrix[5] * point[1] + matrix[9] * point[2] + matrix[13],
        matrix[2] * point[0] + matrix[6] * point[1] + matrix[10] * point[2] + matrix[14],
    };
}

void SnapShadowViewToTexel(
    std::array<float, 16>& view,
    const std::array<float, 3>& worldCenter,
    float orthoHeight,
    std::uint32_t shadowMapSize) noexcept {
    if (shadowMapSize == 0U || orthoHeight <= 0.0F) {
        return;
    }

    const float texelWorldSize = orthoHeight / static_cast<float>(shadowMapSize);
    if (texelWorldSize <= 0.0F) {
        return;
    }

    const std::array<float, 3> lightCenter = TransformPointColumnMajor(view, worldCenter);
    const float snappedX = std::round(lightCenter[0] / texelWorldSize) * texelWorldSize;
    const float snappedY = std::round(lightCenter[1] / texelWorldSize) * texelWorldSize;
    view[12] += snappedX - lightCenter[0];
    view[13] += snappedY - lightCenter[1];
}

[[nodiscard]] float ShadowFilterModeValue(SceneRenderShadowFilter filter) noexcept {
    switch (filter) {
    case SceneRenderShadowFilter::Hard:
        return 1.0F;
    case SceneRenderShadowFilter::Pcf3x3:
        return 3.0F;
    case SceneRenderShadowFilter::Evsm:
        return 4.0F;
    case SceneRenderShadowFilter::Msm:
        return 5.0F;
    case SceneRenderShadowFilter::Pcss:
        return 6.0F;
    }
    return 3.0F;
}

[[nodiscard]] Basis LightBasisFromQuat(const std::array<float, 4>& q) noexcept {
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

void Normalize3(float& x, float& y, float& z) noexcept {
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length <= 0.0001F) {
        x = 0.0F;
        y = -1.0F;
        z = 0.0F;
        return;
    }
    x /= length;
    y /= length;
    z /= length;
}

} // namespace

DirectionalShadowSetup DirectionalShadowPassPlanner::Build(
    const RenderScene& renderScene,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    SceneRenderLightingConfig lightingConfig,
    bgfx::TextureHandle shadowDepthTexture) const noexcept {
    DirectionalShadowSetup setup{};
    if (!lightingConfig.shadowsEnabled) {
        return setup;
    }

    const DirectionalShadowLightSelection selectedLight = DirectionalShadowLightSelector::Select(renderScene);
    if (selectedLight.light == nullptr) {
        return setup;
    }
    setup.lightEntityId = selectedLight.entityId;
    const LightRenderProxyDesc& light = *selectedLight.light;

    const ShadowCasterBounds casterBounds = ShadowCasterBoundsCollector::Collect(renderScene, resources, resourceMap);
    setup.casterCount = casterBounds.casterCount;
    if (setup.casterCount == 0U || !casterBounds.bounds.IsValid()) {
        return setup;
    }

    Basis basis = LightBasisFromQuat(light.rotation);
    Normalize3(basis.zx, basis.zy, basis.zz);
    const float radius = std::max(casterBounds.bounds.radius, 1.0F);
    const float shadowDistance = std::max(lightingConfig.shadowDistance, radius * 2.0F);
    const bx::Vec3 center{ casterBounds.bounds.center[0], casterBounds.bounds.center[1], casterBounds.bounds.center[2] };
    const bx::Vec3 eye{
        casterBounds.bounds.center[0] - basis.zx * shadowDistance,
        casterBounds.bounds.center[1] - basis.zy * shadowDistance,
        casterBounds.bounds.center[2] - basis.zz * shadowDistance,
    };
    const float upX = std::abs(basis.zy) > 0.95F ? 1.0F : 0.0F;
    const float upY = std::abs(basis.zy) > 0.95F ? 0.0F : 1.0F;
    const bx::Vec3 up{ upX, upY, 0.0F };

    bx::mtxLookAt(setup.camera.view.data(), eye, center, up);
    const bool homogeneousDepth = SceneDepthPolicy::HomogeneousDepth();
    const float orthoHeight = std::max(radius * 2.0F, 1.0F);
    SnapShadowViewToTexel(setup.camera.view, casterBounds.bounds.center, orthoHeight, lightingConfig.shadowMapSize);
    SceneDepthPolicy::MakeOrthographic(
        setup.camera.projection.data(),
        orthoHeight,
        1.0F,
        0.1F,
        shadowDistance + radius * 2.0F,
        homogeneousDepth);

    const std::array<float, 16> viewProjection = MultiplyColumnMajor(setup.camera.projection, setup.camera.view);
    setup.binding = SceneRenderShadowMapBinding{
        .depthTexture = shadowDepthTexture,
        .lightViewProjection = MultiplyColumnMajor(ShadowTextureMatrix(homogeneousDepth), viewProjection),
        .params = {
            std::max(lightingConfig.shadowDepthBias, 0.0F),
            std::clamp(lightingConfig.shadowStrength, 0.0F, 1.0F),
            lightingConfig.shadowMapSize == 0U ? 0.0F : 1.0F / static_cast<float>(lightingConfig.shadowMapSize),
            ShadowFilterModeValue(lightingConfig.shadowFilter),
        },
    };
    setup.valid = true;
    return setup;
}

} // namespace kb::render
