#pragma once

#include "engine/math/EngineMath.hpp"
#include "kb/render/scene/RenderScene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace kb::render {

struct ScenePackedLight {
    std::array<std::array<float, 4>, 5> texels{};
    [[nodiscard]] bool operator==(const ScenePackedLight&) const noexcept = default;
};

// One shader representation shared by uniform lighting and spatial light lists.
class SceneLightShaderData {
public:
    [[nodiscard]] static bool IsValid(const LightRenderProxyDesc& light) noexcept {
        if (!light.visible || !(light.intensity > 0.0F) ||
            !(std::max({light.color[0], light.color[1], light.color[2]}) > 0.0F) ||
            (light.kind != RenderLightKind::Directional && !(light.range > 0.0F))) return false;
        for (const auto& values : {light.position, light.color})
            for (const float value : values) if (!std::isfinite(value)) return false;
        for (const float value : light.rotation) if (!std::isfinite(value)) return false;
        for (const float value : {light.intensity, light.range, light.innerConeDegrees,
                light.outerConeDegrees, light.areaWidth, light.areaHeight})
            if (!std::isfinite(value)) return false;
        return true;
    }

    [[nodiscard]] static ScenePackedLight Pack(const LightRenderProxyDesc& light) noexcept {
        auto q = light.rotation;
        double squaredLength = 0.0;
        for (const auto value : q) squaredLength += static_cast<double>(value) * value;
        if (squaredLength <= 0.00000001) q = {0.0F, 0.0F, 0.0F, 1.0F};
        else for (auto& value : q) value = static_cast<float>(value / std::sqrt(squaredLength));
        const float x2 = q[0] + q[0], y2 = q[1] + q[1], z2 = q[2] + q[2];
        auto direction = Normalize({q[0]*z2 + q[3]*y2, q[1]*z2 - q[3]*x2,
            1.0F - (q[0]*x2 + q[1]*y2)});
        const float inner = std::cos(kb::math::ToRadians(kb::math::Degrees{light.innerConeDegrees}).Value());
        const float outer = std::cos(kb::math::ToRadians(kb::math::Degrees{light.outerConeDegrees}).Value());
        return ScenePackedLight{{{
            {direction[0], direction[1], direction[2], Kind(light.kind)},
            {light.position[0], light.position[1], light.position[2], std::max(light.range, 0.0F)},
            {std::max(light.color[0], 0.0F), std::max(light.color[1], 0.0F),
                std::max(light.color[2], 0.0F), light.intensity},
            {std::max(inner, outer), std::min(inner, outer),
                std::max(light.areaWidth, 0.0F), std::max(light.areaHeight, 0.0F)},
            {1.0F - (q[1]*y2 + q[2]*z2), q[0]*y2 + q[3]*z2, q[0]*z2 - q[3]*y2, 0.0F},
        }}};
    }

    [[nodiscard]] static std::optional<ScenePackedLight> EditorPreview(SceneRenderLightingConfig config) noexcept {
        if (!config.editorPreviewKeyLightEnabled || !(config.editorPreviewKeyLightIntensity > 0.0F)) return {};
        const auto direction = Normalize(config.editorPreviewKeyLightDirection);
        ScenePackedLight result{};
        result.texels[0] = {direction[0], direction[1], direction[2], 0.0F};
        result.texels[2] = {std::max(config.editorPreviewKeyLightColor[0], 0.0F),
            std::max(config.editorPreviewKeyLightColor[1], 0.0F),
            std::max(config.editorPreviewKeyLightColor[2], 0.0F), config.editorPreviewKeyLightIntensity};
        for (const auto& texel : result.texels)
            for (const auto value : texel) if (!std::isfinite(value)) return {};
        return result;
    }

private:
    [[nodiscard]] static std::array<float, 3> Normalize(std::array<float, 3> value) noexcept {
        const double length = std::sqrt(static_cast<double>(value[0])*value[0] +
            static_cast<double>(value[1])*value[1] + static_cast<double>(value[2])*value[2]);
        if (length <= 0.0001F) return {0.0F, 0.0F, 1.0F};
        for (auto& component : value) component = static_cast<float>(component / length);
        return value;
    }
    [[nodiscard]] static float Kind(RenderLightKind kind) noexcept {
        switch (kind) {
        case RenderLightKind::Directional: return 0.0F;
        case RenderLightKind::Point: return 1.0F;
        case RenderLightKind::Spot: return 2.0F;
        case RenderLightKind::AreaRect: return 3.0F;
        case RenderLightKind::AreaDisk: return 4.0F;
        case RenderLightKind::Tube: return 5.0F;
        }
        return 1.0F;
    }
};

} // namespace kb::render
