#pragma once

#include "engine/scene/ColliderComponent.hpp"

#include <string>

namespace kb::editor {

class InspectorColliderTextBuilder {
public:
    void Append(std::string& text, const kb::scene::ColliderComponent& collider) const;
};

} // namespace kb::editor
