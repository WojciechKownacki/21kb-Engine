#pragma once

#include "engine/scene/Scene.hpp"

namespace kb::editor {

class EditorDefaultSceneFactory {
public:
    EditorDefaultSceneFactory() = delete;

    [[nodiscard]] static kb::scene::SceneEntity Seed(kb::scene::Scene& scene);
};

} // namespace kb::editor
