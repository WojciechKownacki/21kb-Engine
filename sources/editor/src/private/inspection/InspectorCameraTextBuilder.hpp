#pragma once

#include "engine/scene/CameraComponent.hpp"

#include <string>

namespace kb::editor {

class InspectorCameraTextBuilder {
public:
    void Append(std::string& text, const kb::scene::CameraComponent& camera) const;
};

} // namespace kb::editor
