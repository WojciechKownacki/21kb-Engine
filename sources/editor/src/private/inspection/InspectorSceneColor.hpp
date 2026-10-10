#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "inspection/InspectorPanelState.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace kb::editor {

class EditorSceneContext;

// Every colour of a scene component in the Inspector (Light, Camera clear colour, World
// Backdrop, Ambient Radiance) is one row with a swatch, edited with the colour picker, as in
// Unity. Component colours are linear; the swatch and the picker show them in sRGB, as Unity's
// inspector does in a linear project.
class InspectorSceneColor {
public:
    InspectorSceneColor() = delete;

    [[nodiscard]] static bool IsColorProperty(InspectorPropertyId property) noexcept;
    // The row label, also the colour picker's title.
    [[nodiscard]] static std::string_view Label(InspectorPropertyId property) noexcept;
    // Linear RGB of the selected entity's colour, or nullopt when it has no such component.
    [[nodiscard]] static std::optional<std::array<float, 3U>> Read(
        const EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property);
    // One undoable edit. False when the colour did not change or the component refused it.
    [[nodiscard]] static bool Apply(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity,
        InspectorPropertyId property, const std::array<float, 3U>& linear);

    [[nodiscard]] static float LinearToSrgb(float channel) noexcept;
    [[nodiscard]] static float SrgbToLinear(float channel) noexcept;
};

} // namespace kb::editor
