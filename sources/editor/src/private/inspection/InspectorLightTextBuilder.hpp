#pragma once

#include "engine/scene/LightComponent.hpp"

#include <string>

namespace kb::editor {

class InspectorLightTextBuilder {
public:
    void Append(std::string& text, const kb::scene::LightComponent& light) const;
};

} // namespace kb::editor
