#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace kb::world {

struct WorldSplitObject {
    // Index of the object's root node in the source prefab.
    std::uint32_t sourceRootNode = 0U;
    std::string guid;
    // Root first, then its descendants in source order; node ids are the
    // object's world-unique ids and every link points at such an id.
    kb::scene::ScenePrefab prefab;
    // Guids of the other objects this one links to.
    std::vector<std::string> references;
};

// Splits a multi-root prefab (a captured scene or a scene file's world prefab)
// into one object per root. `guidFor(rootOrdinal, rootNode)` names each object.
// Node ids are rewritten to WorldObjectStableId and every node link (joint,
// region portal, lens echo, UI) is rewritten with them. Fails when a link names
// a node outside the prefab.
[[nodiscard]] bool SplitIntoWorldObjects(
    const kb::scene::ScenePrefab& source,
    const std::function<std::string(std::size_t, const kb::scene::ScenePrefabNodeDesc&)>& guidFor,
    std::vector<WorldSplitObject>& objects,
    std::string& error);

} // namespace kb::world
