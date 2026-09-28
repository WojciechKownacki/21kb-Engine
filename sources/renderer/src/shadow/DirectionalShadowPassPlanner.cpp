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
    const bgfx::Caps* caps = bgfx::getCaps();
    const float textureYScale = caps != nullptr && caps->originBottomLeft ? 0.5F : -0.5F;
    return std::array<float, 16>{
        0.5F, 0.0F, 0.0F, 0.0F,
        0.0F, textureYScale, 0.0F, 0.0F,
        0.0F, 0.0F, homogeneousDepth ? 0.5F : 1.0F, 0.0F,
        0.5F, 0.5F, homogeneousDepth ? 0.5F : 0.0F, 1.0F,
    };
}

void SnapShadowViewToTexel(
    std::array<float, 16>& view,
    float orthoHeight,
    std::uint32_t shadowMapSize) noexcept {
    if (shadowMapSize == 0U || orthoHeight <= 0.0F) {
        return;
    }

    const float texelWorldSize = orthoHeight / static_cast<float>(shadowMapSize);
    if (texelWorldSize <= 0.0F) {
        return;
    }

    view[12] = std::round(view[12] / texelWorldSize) * texelWorldSize;
    view[13] = std::round(view[13] / texelWorldSize) * texelWorldSize;
}

void SetShadowViewOrigin(std::array<float, 16>& view, const bx::Vec3& origin) noexcept {
    view[12] = -(view[0] * origin.x + view[4] * origin.y + view[8] * origin.z);
    view[13] = -(view[1] * origin.x + view[5] * origin.y + view[9] * origin.z);
    view[14] = -(view[2] * origin.x + view[6] * origin.y + view[10] * origin.z);
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
    bgfx::TextureHandle shadowDepthTexture,
    std::uint32_t cameraCullingMask,
    const std::array<float, 3>* cameraPosition) const noexcept {
    DirectionalShadowSetup setup{};
    if (!lightingConfig.shadowsEnabled) {
        return setup;
    }

    const DirectionalShadowLightSelection selectedLight =
        DirectionalShadowLightSelector::Select(renderScene, cameraCullingMask);
    if (selectedLight.light == nullptr) {
        return setup;
    }
    setup.lightEntityId = selectedLight.entityId;
    const LightRenderProxyDesc& light = *selectedLight.light;

    if (!std::isfinite(lightingConfig.shadowDistance) || lightingConfig.shadowDistance <= 0.0F) {
        return setup;
    }
    Basis basis = LightBasisFromQuat(light.rotation);
    Normalize3(basis.zx, basis.zy, basis.zz);
    const float upX = std::abs(basis.zy) > 0.95F ? 1.0F : 0.0F;
    const float upY = std::abs(basis.zy) > 0.95F ? 0.0F : 1.0F;
    const bx::Vec3 up{ upX, upY, 0.0F };
    std::array<float, 16> focusView{};
    bx::mtxLookAt(focusView.data(), bx::Vec3{0.0F, 0.0F, 0.0F},
        bx::Vec3{basis.zx, basis.zy, basis.zz}, up);
    if (cameraPosition != nullptr) {
        const bx::Vec3 origin{(*cameraPosition)[0], (*cameraPosition)[1], (*cameraPosition)[2]};
        SetShadowViewOrigin(focusView, origin);
        if (lightingConfig.stableShadowCascades) {
            SnapShadowViewToTexel(focusView, 2.0F * lightingConfig.shadowDistance, lightingConfig.shadowMapSize);
        }
    }
    const ShadowCasterBounds casterBounds = ShadowCasterBoundsCollector::Collect(
        renderScene, resources, resourceMap, cameraCullingMask,
        cameraPosition != nullptr ? &focusView : nullptr, lightingConfig.shadowDistance);
    setup.casterCount = casterBounds.casterCount;
    if (setup.casterCount == 0U || !casterBounds.bounds.IsValid()) {
        return setup;
    }

    const float radius = std::max(casterBounds.bounds.radius, 1.0F);
    const bool focusCamera = cameraPosition != nullptr && radius > lightingConfig.shadowDistance;
    const auto& focus = focusCamera ? *cameraPosition : casterBounds.bounds.center;
    if (focusCamera && casterBounds.focusedCasterCount == 0U) {
        return setup;
    }
    const float minimumDepth = focusCamera ? casterBounds.focusedDepthMinimum : -radius;
    const float maximumDepth = focusCamera ? casterBounds.focusedDepthMaximum : radius;
    const bx::Vec3 eye{
        focus[0] + basis.zx * (minimumDepth - 1.0F),
        focus[1] + basis.zy * (minimumDepth - 1.0F),
        focus[2] + basis.zz * (minimumDepth - 1.0F),
    };
    setup.camera.view = focusView;
    SetShadowViewOrigin(setup.camera.view, eye);
    setup.casterCount = focusCamera ? casterBounds.focusedCasterCount : casterBounds.casterCount;
    const bool homogeneousDepth = SceneDepthPolicy::HomogeneousDepth();
    const float orthoHeight = 2.0F * (focusCamera ? lightingConfig.shadowDistance : radius);
    if (lightingConfig.stableShadowCascades) {
        SnapShadowViewToTexel(setup.camera.view, orthoHeight, lightingConfig.shadowMapSize);
    }
    setup.camera.cullingMask = cameraCullingMask;
    SceneDepthPolicy::MakeOrthographic(
        setup.camera.projection.data(),
        orthoHeight,
        1.0F,
        0.1F,
        maximumDepth - minimumDepth + 2.0F,
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
