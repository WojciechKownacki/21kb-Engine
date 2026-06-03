#include "kb/render/shadow/DirectionalShadowPassPlanner.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace kb::render {
namespace {

struct SelectedShadowLight {
    const LightRenderProxyDesc* light = nullptr;
    std::uint64_t entityId = 0;
};

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

[[nodiscard]] RenderBoundsSphere TransformBoundsForShadow(const RenderBoundsSphere& localBounds, const std::array<float, 16>& model) noexcept {
    if (!localBounds.IsValid()) {
        return RenderBoundsSphere{
            .center = { model[12], model[13], model[14] },
            .radius = 1.0F,
        };
    }

    const float sx = std::sqrt(model[0] * model[0] + model[1] * model[1] + model[2] * model[2]);
    const float sy = std::sqrt(model[4] * model[4] + model[5] * model[5] + model[6] * model[6]);
    const float sz = std::sqrt(model[8] * model[8] + model[9] * model[9] + model[10] * model[10]);
    return RenderBoundsSphere{
        .center = {
            model[0] * localBounds.center[0] + model[4] * localBounds.center[1] + model[8] * localBounds.center[2] + model[12],
            model[1] * localBounds.center[0] + model[5] * localBounds.center[1] + model[9] * localBounds.center[2] + model[13],
            model[2] * localBounds.center[0] + model[6] * localBounds.center[1] + model[10] * localBounds.center[2] + model[14],
        },
        .radius = localBounds.radius * std::max(std::max(sx, sy), sz),
    };
}

[[nodiscard]] RenderBoundsSphere MergeBounds(RenderBoundsSphere lhs, const RenderBoundsSphere& rhs) noexcept {
    if (!lhs.IsValid()) {
        return rhs;
    }
    if (!rhs.IsValid()) {
        return lhs;
    }

    const float dx = rhs.center[0] - lhs.center[0];
    const float dy = rhs.center[1] - lhs.center[1];
    const float dz = rhs.center[2] - lhs.center[2];
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (distance + rhs.radius <= lhs.radius) {
        return lhs;
    }
    if (distance + lhs.radius <= rhs.radius) {
        return rhs;
    }

    const float mergedRadius = (distance + lhs.radius + rhs.radius) * 0.5F;
    const float centerShift = distance > 0.0001F ? (mergedRadius - lhs.radius) / distance : 0.0F;
    return RenderBoundsSphere{
        .center = {
            lhs.center[0] + dx * centerShift,
            lhs.center[1] + dy * centerShift,
            lhs.center[2] + dz * centerShift,
        },
        .radius = mergedRadius,
    };
}

[[nodiscard]] float MaxLightChannel(const LightRenderProxyDesc& light) noexcept {
    return std::max(std::max(light.color[0], light.color[1]), light.color[2]);
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

[[nodiscard]] SelectedShadowLight SelectShadowDirectionalLight(const RenderScene& renderScene) noexcept {
    SelectedShadowLight selected{};
    std::uint64_t selectedEntityId = 0U;
    float selectedScore = 0.0F;
    for (const auto& [entityId, proxy] : renderScene.LightProxies()) {
        const LightRenderProxyDesc& light = proxy.desc;
        if (!light.visible || !light.castsShadow || light.kind != RenderLightKind::Directional || light.intensity <= 0.0F || MaxLightChannel(light) <= 0.0F) {
            continue;
        }
        const float score = light.intensity * MaxLightChannel(light);
        if (selected.light == nullptr || score > selectedScore || (score == selectedScore && entityId < selectedEntityId)) {
            selected.light = &light;
            selected.entityId = entityId;
            selectedEntityId = entityId;
            selectedScore = score;
        }
    }
    return selected;
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

    const SelectedShadowLight selectedLight = SelectShadowDirectionalLight(renderScene);
    if (selectedLight.light == nullptr) {
        return setup;
    }
    setup.lightEntityId = selectedLight.entityId;
    const LightRenderProxyDesc& light = *selectedLight.light;

    RenderBoundsSphere casterBounds{};
    for (const auto& [entityId, proxy] : renderScene.MeshProxies()) {
        static_cast<void>(entityId);
        const MeshRenderProxyDesc& mesh = proxy.desc;
        if (!mesh.visible || !mesh.castsShadow) {
            continue;
        }

        RenderBoundsSphere localBounds{};
        const RenderMeshHandle meshHandle = resourceMap.ResolveMesh(mesh.meshAssetId);
        if (const RenderMeshResource* meshResource = resources.FindMesh(meshHandle); meshResource != nullptr) {
            localBounds = meshResource->bounds;
        }
        casterBounds = MergeBounds(casterBounds, TransformBoundsForShadow(localBounds, mesh.model));
        ++setup.casterCount;
    }
    if (setup.casterCount == 0U || !casterBounds.IsValid()) {
        return setup;
    }

    Basis basis = LightBasisFromQuat(light.rotation);
    Normalize3(basis.zx, basis.zy, basis.zz);
    const float radius = std::max(casterBounds.radius, 1.0F);
    const float shadowDistance = std::max(lightingConfig.shadowDistance, radius * 2.0F);
    const bx::Vec3 center{ casterBounds.center[0], casterBounds.center[1], casterBounds.center[2] };
    const bx::Vec3 eye{
        casterBounds.center[0] - basis.zx * shadowDistance,
        casterBounds.center[1] - basis.zy * shadowDistance,
        casterBounds.center[2] - basis.zz * shadowDistance,
    };
    const float upX = std::abs(basis.zy) > 0.95F ? 1.0F : 0.0F;
    const float upY = std::abs(basis.zy) > 0.95F ? 0.0F : 1.0F;
    const bx::Vec3 up{ upX, upY, 0.0F };

    bx::mtxLookAt(setup.camera.view.data(), eye, center, up);
    const bool homogeneousDepth = SceneDepthPolicy::HomogeneousDepth();
    const float orthoHeight = std::max(radius * 2.0F, 1.0F);
    SnapShadowViewToTexel(setup.camera.view, casterBounds.center, orthoHeight, lightingConfig.shadowMapSize);
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
