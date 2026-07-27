#include "DirectionalShadowLightSelector.hpp"

#include <algorithm>

namespace kb::render {
namespace {

[[nodiscard]] float MaxLightChannel(const LightRenderProxyDesc& light) noexcept {
    return std::max(std::max(light.color[0], light.color[1]), light.color[2]);
}

[[nodiscard]] bool IsShadowDirectionalLight(const LightRenderProxyDesc& light) noexcept {
    return light.visible &&
        light.castsShadow &&
        light.kind == RenderLightKind::Directional &&
        light.intensity > 0.0F &&
        MaxLightChannel(light) > 0.0F;
}

} // namespace

DirectionalShadowLightSelection DirectionalShadowLightSelector::Select(
    const RenderScene& renderScene,
    std::uint32_t cameraCullingMask) noexcept {
    DirectionalShadowLightSelection selected{};
    std::uint64_t selectedEntityId = 0U;
    float selectedScore = 0.0F;
    for (const auto& [entityId, proxy] : renderScene.LightProxies()) {
        const LightRenderProxyDesc& light = proxy.desc;
        if (!IsShadowDirectionalLight(light) || (light.layer & cameraCullingMask) == 0U) {
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

} // namespace kb::render
