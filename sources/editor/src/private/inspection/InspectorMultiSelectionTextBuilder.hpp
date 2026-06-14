#pragma once

#include "scene/EditorSceneContext.hpp"

#include <string>

namespace kb::editor {

class InspectorMultiSelectionTextBuilder {
public:
    [[nodiscard]] std::string Build(const EditorSceneContext& sceneContext) const;
};

} // namespace kb::editor
