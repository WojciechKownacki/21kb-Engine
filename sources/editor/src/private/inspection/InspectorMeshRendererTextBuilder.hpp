#pragma once

#include "engine/scene/MeshRendererComponent.hpp"

#include <string>

namespace kb::editor {

class InspectorMeshRendererTextBuilder {
public:
    void Append(std::string& text, const kb::scene::MeshRendererComponent& renderer) const;
};

} // namespace kb::editor
