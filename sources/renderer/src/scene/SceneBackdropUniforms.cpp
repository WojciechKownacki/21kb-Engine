#include "kb/render/scene/SceneBackdropUniforms.hpp"

#include "kb/render/scene/RenderScene.hpp"

#include "engine/math/EngineMath.hpp"

namespace kb::render {

SceneBackdropUniformValues BuildSceneBackdropUniforms(
    const SceneRenderWorldBackdrop* backdrop, bool environmentAvailable, const RenderScene& renderScene) noexcept {
    SceneBackdropUniformValues values{};
    if (backdrop != nullptr && backdrop->mode == SceneRenderWorldBackdropMode::ProceduralSky) {
        // Unity's Skybox/Procedural model reuses the gradient slots: horizon = ground colour + sun disc,
        // zenith = sky tint + exposure, params = sun size, convergence, atmosphere thickness.
        values.horizon = { backdrop->groundColor[0], backdrop->groundColor[1], backdrop->groundColor[2],
            static_cast<float>(backdrop->sunDisk) };
        values.zenith = { backdrop->skyTint[0], backdrop->skyTint[1], backdrop->skyTint[2], backdrop->exposure };
        values.params = { 3.0F, backdrop->sunSize, backdrop->sunSizeConvergence, backdrop->atmosphereThickness };
    } else if (backdrop != nullptr && backdrop->mode == SceneRenderWorldBackdropMode::VerticalGradient) {
        values.horizon = { backdrop->horizonColor[0], backdrop->horizonColor[1], backdrop->horizonColor[2], 1.0F };
        values.zenith = { backdrop->zenithColor[0], backdrop->zenithColor[1], backdrop->zenithColor[2], 1.0F };
        values.params = { 1.0F, backdrop->horizonHeight, backdrop->gradientExponent, 0.0F };
    } else if (backdrop != nullptr && backdrop->mode == SceneRenderWorldBackdropMode::EnvironmentMap && environmentAvailable) {
        values.params = { 2.0F, 0.0F, 0.0F, 0.0F };
    }
    // The sky's sun is the brightest directional light, as Unity's Sun Source defaults to.
    float brightestSun = 0.0F;
    for (const auto& [entityId, proxy] : renderScene.LightProxies()) {
        static_cast<void>(entityId);
        const LightRenderProxyDesc& light = proxy.desc;
        const float brightness = light.intensity * (light.color[0] + light.color[1] + light.color[2]);
        if (light.kind != RenderLightKind::Directional || brightness <= brightestSun) {
            continue;
        }
        brightestSun = brightness;
        const kb::math::Vec3 shining = kb::math::Rotate(
            kb::math::Quat{ light.rotation[0], light.rotation[1], light.rotation[2], light.rotation[3] },
            kb::math::Vec3{ 0.0F, 0.0F, 1.0F });
        values.sun = { -shining.x, -shining.y, -shining.z, 1.0F };
        values.sunColor = { light.color[0] * light.intensity, light.color[1] * light.intensity,
            light.color[2] * light.intensity, 1.0F };
    }
    return values;
}

} // namespace kb::render
