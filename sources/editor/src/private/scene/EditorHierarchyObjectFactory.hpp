#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <string>
#include <string_view>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::editor {

class EditorHierarchyObjectFactory {
public:
    [[nodiscard]] static kb::scene::SceneEntity CreateObject(kb::scene::Scene& scene, std::string_view baseName = "Entity");

private:
    [[nodiscard]] static std::string MakeUniqueName(const kb::scene::Scene& scene, std::string_view baseName);
};

} // namespace kb::editor
