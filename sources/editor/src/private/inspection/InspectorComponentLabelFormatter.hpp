#pragma once

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"

namespace kb::editor {

class InspectorComponentLabelFormatter {
public:
    [[nodiscard]] static const char* LightKindName(kb::scene::LightKind kind) noexcept;
    [[nodiscard]] static const char* ProjectionName(kb::scene::CameraProjection projection) noexcept;
    [[nodiscard]] static const char* RigidbodyBodyTypeName(kb::scene::RigidbodyBodyType bodyType) noexcept;
    [[nodiscard]] static const char* ColliderShapeName(kb::scene::ColliderShape shape) noexcept;
};

} // namespace kb::editor
