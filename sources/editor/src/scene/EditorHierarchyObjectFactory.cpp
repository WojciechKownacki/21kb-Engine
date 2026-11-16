#include "scene/EditorHierarchyObjectFactory.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "scene/EditorHierarchyRowBuilder.hpp"

#include <string>
#include <unordered_set>
#include <vector>
#include <utility>

namespace kb::editor {

kb::scene::SceneEntity EditorHierarchyObjectFactory::CreateObject(kb::scene::Scene& scene, std::string_view baseName) {
    kb::scene::SceneObjectDesc desc{};
    desc.name = MakeUniqueName(scene, baseName);
    return scene.Entities().CreateEntity(std::move(desc));
}

std::string EditorHierarchyObjectFactory::MakeUniqueName(const kb::scene::Scene& scene, std::string_view baseName,
    kb::scene::SceneEntity parent, std::size_t* visited) {
    // The names beside the new object, listed once: the cost follows the siblings, not the scene.
    const kb::scene::SceneHierarchyQueries hierarchy = scene.Hierarchy();
    const std::vector<kb::scene::SceneEntity> siblings = parent.IsValid() ? hierarchy.ChildEntities(parent) : hierarchy.RootEntities();
    std::unordered_set<std::string> names;
    names.reserve(siblings.size());
    for (const kb::scene::SceneEntity sibling : siblings) {
        if (!EditorHierarchyRowBuilder::IsTransient(scene, sibling)) {
            names.insert(scene.Entities().Name(sibling));
        }
    }
    if (visited != nullptr) {
        *visited = siblings.size();
    }
    std::string candidate{baseName};
    for (int suffix = 1; suffix < 10000 && names.contains(candidate); ++suffix) {
        candidate = std::string{ baseName } + " " + std::to_string(suffix);
    }
    return candidate;
}

} // namespace kb::editor
