#include "scene/EditorHierarchyObjectFactory.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "scene/EditorHierarchyRowBuilder.hpp"

#include <string>
#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] bool NameExists(const kb::scene::Scene& scene, const std::string& name) {
    for (const EditorHierarchyRow& row : EditorHierarchyRowBuilder::Build(scene, EditorHierarchyRowBuilder::CollapsedEntitySet{}, {})) {
        if (row.name == name) {
            return true;
        }
    }
    return false;
}

} // namespace

kb::scene::SceneEntity EditorHierarchyObjectFactory::CreateObject(kb::scene::Scene& scene, std::string_view baseName) {
    kb::scene::SceneObjectDesc desc{};
    desc.name = MakeUniqueName(scene, baseName);
    return scene.Entities().CreateEntity(std::move(desc));
}

std::string EditorHierarchyObjectFactory::MakeUniqueName(const kb::scene::Scene& scene, std::string_view baseName) {
    std::string candidate{baseName};
    for (int suffix = 1; suffix < 10000; ++suffix) {
        if (!NameExists(scene, candidate)) {
            return candidate;
        }
        candidate = std::string{ baseName } + " " + std::to_string(suffix);
    }
    return candidate;
}

} // namespace kb::editor
