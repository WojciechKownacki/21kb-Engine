#include "scene/transform_edit/EditorTransformProperty.hpp"

#include <algorithm>

namespace kb::editor {

EditorTransformPropertyGroup EditorTransformProperty::Group(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::PositionX:
    case InspectorPropertyId::PositionY:
    case InspectorPropertyId::PositionZ:
        return EditorTransformPropertyGroup::Position;
    case InspectorPropertyId::RotationX:
    case InspectorPropertyId::RotationY:
    case InspectorPropertyId::RotationZ:
        return EditorTransformPropertyGroup::Rotation;
    case InspectorPropertyId::ScaleX:
    case InspectorPropertyId::ScaleY:
    case InspectorPropertyId::ScaleZ:
        return EditorTransformPropertyGroup::Scale;
    default:
        return EditorTransformPropertyGroup::None;
    }
}

bool EditorTransformProperty::IsTransform(InspectorPropertyId property) noexcept {
    return Group(property) != EditorTransformPropertyGroup::None;
}

bool EditorTransformProperty::IsPosition(InspectorPropertyId property) noexcept {
    return Group(property) == EditorTransformPropertyGroup::Position;
}

bool EditorTransformProperty::IsRotation(InspectorPropertyId property) noexcept {
    return Group(property) == EditorTransformPropertyGroup::Rotation;
}

bool EditorTransformProperty::IsScale(InspectorPropertyId property) noexcept {
    return Group(property) == EditorTransformPropertyGroup::Scale;
}

float EditorTransformProperty::Read(const kb::scene::TransformComponent& transform, InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::PositionX:
        return transform.localPosition.x;
    case InspectorPropertyId::PositionY:
        return transform.localPosition.y;
    case InspectorPropertyId::PositionZ:
        return transform.localPosition.z;
    case InspectorPropertyId::RotationX:
        return transform.localRotation.x;
    case InspectorPropertyId::RotationY:
        return transform.localRotation.y;
    case InspectorPropertyId::RotationZ:
        return transform.localRotation.z;
    case InspectorPropertyId::ScaleX:
        return transform.localScale.x;
    case InspectorPropertyId::ScaleY:
        return transform.localScale.y;
    case InspectorPropertyId::ScaleZ:
        return transform.localScale.z;
    default:
        return 0.0F;
    }
}

float EditorTransformProperty::ReadAxis(kb::scene::Vec3 value, InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::PositionX:
    case InspectorPropertyId::RotationX:
    case InspectorPropertyId::ScaleX:
        return value.x;
    case InspectorPropertyId::PositionY:
    case InspectorPropertyId::RotationY:
    case InspectorPropertyId::ScaleY:
        return value.y;
    case InspectorPropertyId::PositionZ:
    case InspectorPropertyId::RotationZ:
    case InspectorPropertyId::ScaleZ:
        return value.z;
    default:
        return 0.0F;
    }
}

kb::scene::Vec3 EditorTransformProperty::WithAxis(kb::scene::Vec3 value, InspectorPropertyId property, float axisValue) noexcept {
    switch (property) {
    case InspectorPropertyId::PositionX:
    case InspectorPropertyId::RotationX:
    case InspectorPropertyId::ScaleX:
        value.x = axisValue;
        break;
    case InspectorPropertyId::PositionY:
    case InspectorPropertyId::RotationY:
    case InspectorPropertyId::ScaleY:
        value.y = axisValue;
        break;
    case InspectorPropertyId::PositionZ:
    case InspectorPropertyId::RotationZ:
    case InspectorPropertyId::ScaleZ:
        value.z = axisValue;
        break;
    default:
        break;
    }
    return value;
}

void EditorTransformProperty::Write(kb::scene::TransformComponent& transform, InspectorPropertyId property, float value) noexcept {
    switch (property) {
    case InspectorPropertyId::PositionX:
        transform.localPosition.x = value;
        break;
    case InspectorPropertyId::PositionY:
        transform.localPosition.y = value;
        break;
    case InspectorPropertyId::PositionZ:
        transform.localPosition.z = value;
        break;
    case InspectorPropertyId::RotationX:
        transform.localRotation.x = value;
        break;
    case InspectorPropertyId::RotationY:
        transform.localRotation.y = value;
        break;
    case InspectorPropertyId::RotationZ:
        transform.localRotation.z = value;
        break;
    case InspectorPropertyId::ScaleX:
        transform.localScale.x = std::max(0.01F, value);
        break;
    case InspectorPropertyId::ScaleY:
        transform.localScale.y = std::max(0.01F, value);
        break;
    case InspectorPropertyId::ScaleZ:
        transform.localScale.z = std::max(0.01F, value);
        break;
    default:
        break;
    }
}

} // namespace kb::editor
