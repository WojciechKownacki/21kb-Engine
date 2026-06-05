#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace kb::editor {

struct EditorHierarchyRow {
    kb::scene::SceneEntity entity{};
    std::uint32_t depth = 0;
    std::string name;
    std::size_t componentCount = 0;
    bool hasChildren = false;
    bool expanded = false;
    bool visible = true;
    bool prefabRoot = false;
};

} // namespace kb::editor
