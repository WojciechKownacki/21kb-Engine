#pragma once

#include "engine/scene/RigidbodyComponent.hpp"

#include <string>

namespace kb::editor {

class InspectorRigidbodyTextBuilder {
public:
    void Append(std::string& text, const kb::scene::RigidbodyComponent& rigidbody) const;
};

} // namespace kb::editor
