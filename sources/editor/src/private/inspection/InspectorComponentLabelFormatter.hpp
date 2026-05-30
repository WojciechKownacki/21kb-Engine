#pragma once

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"

namespace kb::editor {

class InspectorComponentLabelFormatter {
public:
    [[nodiscard]] static const char* LightKindName(kb::scene::LightKind kind) noexcept;
    [[nodiscard]] static const char* ProjectionName(kb::scene::CameraProjection projection) noexcept;
};

} // namespace kb::editor
