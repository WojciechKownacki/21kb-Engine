#pragma once

#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneLightGridResources.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <array>
#include <cstdint>

namespace kb::render {

struct PackedSceneLighting {
    SceneLightGridBinding lightGrid{};
    std::uint64_t primaryLightId = 0U;
    // Entity id of the light packed into each uniform slot, so point-light shadow slots can be
    // matched to the slot the shader loops over.
    std::array<std::uint64_t, kMaxSceneForwardPlusLights> slotEntityId{};
    // Packed light slot of each point-shadow light, or -1 when it did not fit the uniform budget.
    std::array<float, ScenePointShadowBinding::kMaxLights> pointShadowSlot = [] {
        std::array<float, ScenePointShadowBinding::kMaxLights> slots{};
        slots.fill(-1.0F);
        return slots;
    }();
    std::array<float, kMaxSceneForwardPlusLights * 4U> dirKind{};
    std::array<float, kMaxSceneForwardPlusLights * 4U> positionRange{};
    std::array<float, kMaxSceneForwardPlusLights * 4U> colorIntensity{};
    std::array<float, kMaxSceneForwardPlusLights * 4U> spot{};
    // World-space local X axis of surface emitters. Spot values z/w carry
    // their authored width/height; this preserves the ECS transform's roll.
    std::array<float, kMaxSceneForwardPlusLights * 4U> areaRight{};
    std::array<float, 4U> params{};
    std::array<float, 4U> ambient{ 0.18F, 0.20F, 0.23F, 1.0F };
    std::array<float, 4U> environmentZenith{ 0.36F, 0.42F, 0.52F, 1.0F };
    std::array<float, 4U> environmentGround{ 0.08F, 0.075F, 0.065F, 1.0F };
    std::array<float, 4U> environmentParams{ 1.0F, 1.0F, 0.25F, 0.0F };
};

class SceneLightingPacker {
public:
    SceneLightingPacker() = delete;

    [[nodiscard]] static PackedSceneLighting Build(
        const RenderScene& renderScene,
        SceneRenderSubmitStats& stats,
        SceneRenderLightingConfig config,
        const SceneRenderCamera* camera) noexcept;
    [[nodiscard]] static std::array<float, 4> CameraPosition(const SceneRenderCamera* camera) noexcept;
    // Maps each point-shadow light to the uniform slot the shader loops over.
    static void AssignPointShadowSlots(PackedSceneLighting& lighting, const ScenePointShadowBinding& binding) noexcept;
};

} // namespace kb::render
