#pragma once

#include "engine/scene/TransformComponent.hpp"
#include "inspection/InspectorPanelState.hpp"

namespace kb::editor {

enum class EditorTransformPropertyGroup {
    None,
    Position,
    Rotation,
    Scale,
};

class EditorTransformProperty {
public:
    EditorTransformProperty() = delete;

    [[nodiscard]] static EditorTransformPropertyGroup Group(InspectorPropertyId property) noexcept;
    [[nodiscard]] static bool IsTransform(InspectorPropertyId property) noexcept;
    [[nodiscard]] static bool IsPosition(InspectorPropertyId property) noexcept;
    [[nodiscard]] static bool IsRotation(InspectorPropertyId property) noexcept;
    [[nodiscard]] static bool IsScale(InspectorPropertyId property) noexcept;
    [[nodiscard]] static float Read(const kb::scene::TransformComponent& transform, InspectorPropertyId property) noexcept;
    [[nodiscard]] static float ReadAxis(kb::scene::Vec3 value, InspectorPropertyId property) noexcept;
    [[nodiscard]] static kb::scene::Vec3 WithAxis(kb::scene::Vec3 value, InspectorPropertyId property, float axisValue) noexcept;
    static void Write(kb::scene::TransformComponent& transform, InspectorPropertyId property, float value) noexcept;
};

} // namespace kb::editor
