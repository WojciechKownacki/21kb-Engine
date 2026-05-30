#pragma once

#include "scene/EditorSceneContext.hpp"

#include <optional>
#include <string>

namespace kb::editor {

class InspectorPanelTextBuilder {
public:
    [[nodiscard]] std::optional<std::string> Build(const EditorSceneContext& sceneContext) const;
};

} // namespace kb::editor
