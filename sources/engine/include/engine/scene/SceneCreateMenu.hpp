#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <span>
#include <string>
#include <string_view>

namespace kb::project {
struct ProjectDescriptor;
}

namespace kb::scene {

class Scene;

// One object of the hierarchy Create menu, like Unity's GameObject menu. The editor's menu and
// `kb_cli entity add --create` both create through SceneCreateMenu::Create, so an object made by
// either is the same object.
struct SceneCreateItem {
    std::string_view id;      // what kb_cli --create takes
    std::string_view label;   // the menu row
    std::string_view name;    // the new object's default name
    std::string_view submenu; // empty for a top-level entry
    std::string_view hint;    // shown at the right edge of the menu row (size, shortcut)
};

struct SceneCreateResult {
    SceneEntity entity{};
    std::string error; // why nothing was created, when entity is invalid
};

class SceneCreateMenu {
public:
    SceneCreateMenu() = delete;

    // In menu order; entries of one submenu are adjacent.
    [[nodiscard]] static std::span<const SceneCreateItem> Items() noexcept;
    [[nodiscard]] static const SceneCreateItem* Find(std::string_view id) noexcept;
    // Creates the object at its parent's origin with the components and defaults of its kind.
    // A built-in shape gets a fitted collider when the project has the Physics.Jolt plugin, as
    // Unity's primitives come with one. Refuses, naming the plugin, when the object's own
    // component needs a plugin the project has disabled.
    [[nodiscard]] static SceneCreateResult Create(Scene& scene, std::string_view id, std::string name,
        const kb::project::ProjectDescriptor& project, SceneEntity parent = {});
};

} // namespace kb::scene
