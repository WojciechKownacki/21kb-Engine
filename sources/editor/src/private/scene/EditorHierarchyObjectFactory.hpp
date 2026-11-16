#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::editor {

class EditorHierarchyObjectFactory {
public:
    [[nodiscard]] static kb::scene::SceneEntity CreateObject(kb::scene::Scene& scene, std::string_view baseName = "Entity");
    // `baseName`, or the first "baseName N" no sibling carries, among the children of `parent` (the roots when it is
    // invalid). Reads only the siblings; `visited` receives how many.
    [[nodiscard]] static std::string MakeUniqueName(const kb::scene::Scene& scene, std::string_view baseName,
        kb::scene::SceneEntity parent = {}, std::size_t* visited = nullptr);
};

} // namespace kb::editor
