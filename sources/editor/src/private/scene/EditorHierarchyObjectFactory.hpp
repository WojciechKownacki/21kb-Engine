#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <string>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::editor {

class EditorHierarchyObjectFactory {
public:
    [[nodiscard]] static kb::scene::SceneEntity CreateObject(kb::scene::Scene& scene);

private:
    [[nodiscard]] static std::string MakeUniqueName(const kb::scene::Scene& scene);
};

} // namespace kb::editor
