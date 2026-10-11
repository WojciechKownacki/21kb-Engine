#pragma once

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/audio/AudioSettings.hpp"

#include <string_view>

namespace kb::editor {

// Component values as the Inspector shows them, in the editor language.
class InspectorComponentLabelFormatter {
public:
    [[nodiscard]] static std::string_view LightKindName(kb::scene::LightKind kind);
    [[nodiscard]] static std::string_view ProjectionName(kb::scene::CameraProjection projection);
    [[nodiscard]] static std::string_view CameraClearModeName(kb::scene::CameraClearMode clearMode);
    [[nodiscard]] static std::string_view RigidbodyBodyTypeName(kb::scene::RigidbodyBodyType bodyType);
    [[nodiscard]] static std::string_view ColliderShapeName(kb::scene::ColliderShape shape);
    [[nodiscard]] static std::string_view AudioAttenuationModelName(kb::audio::AudioAttenuationModel model);
};

} // namespace kb::editor
