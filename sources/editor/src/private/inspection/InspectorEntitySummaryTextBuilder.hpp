#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "scene/EditorSceneContext.hpp"

#include <string>

namespace kb::editor {

class InspectorEntitySummaryTextBuilder {
public:
    [[nodiscard]] std::string Build(const EditorSceneContext& sceneContext, kb::scene::SceneEntity entity) const;
};

} // namespace kb::editor
