#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "engine/scene/SceneDocumentService.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace kb::editor {

enum class EditorSceneChangeKind : std::uint8_t {
    Added,
    Removed,
    Changed,
};

// One value of one component that differs. `property` is empty when the component itself was added or
// removed: `before` is then empty for an added component and `after` for a removed one.
struct EditorSceneFieldChange {
    std::string component;
    std::string property;
    std::string before;
    std::string after;
};

struct EditorSceneObjectChange {
    EditorSceneChangeKind kind = EditorSceneChangeKind::Changed;
    // The object's name under its parents, "Level/Door".
    std::string path;
    // The object's place in the newer scene: its position among its siblings at each level, from the top.
    // Empty for a removed object.
    std::vector<std::uint32_t> address;
    // What differs on a changed object.
    std::vector<EditorSceneFieldChange> fields;
};

struct EditorSceneComparison {
    bool succeeded = false;
    std::string error;
    // An added or removed object stands for everything below it.
    std::vector<EditorSceneObjectChange> objects;
};

// Compares two versions of a scene by what they hold, not by how their files read. Objects are paired by their
// names under their parents (the nth object of a name under one parent pairs with the nth of that name); the
// values of every component both have are read through the same component fields scripts and tools use.
class EditorSceneComparer final {
public:
    EditorSceneComparer() = delete;

    // When `cancel` is set while the comparison runs, it stops and reports that it was cancelled.
    [[nodiscard]] static EditorSceneComparison Compare(const kb::scene::SceneDocument& before, const kb::scene::SceneDocument& after,
        const std::atomic<bool>* cancel = nullptr);
#if defined(_WIN32)
    // The scene file as the last saved version holds it.
    [[nodiscard]] static kb::scene::SceneDocumentLoadResult LoadSaved(const std::filesystem::path& projectRoot, const std::string& repositoryPath);
    // The scene file as the last saved version holds it against `current`.
    [[nodiscard]] static EditorSceneComparison CompareWithSaved(const std::filesystem::path& projectRoot,
        const std::string& repositoryPath, const kb::scene::SceneDocument& current, const std::atomic<bool>* cancel = nullptr);
#endif
    // The object of `scene` at an address of a change; invalid when the scene has no such object.
    [[nodiscard]] static kb::scene::SceneEntity Find(const kb::scene::Scene& scene, std::span<const std::uint32_t> address);
};

} // namespace kb::editor
