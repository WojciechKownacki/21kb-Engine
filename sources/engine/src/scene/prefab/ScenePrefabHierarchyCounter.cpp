#include "scene/prefab/ScenePrefabHierarchyCounter.hpp"

#include "scene/prefab/ScenePrefabChildrenReader.hpp"

namespace kb::scene {

std::size_t ScenePrefabHierarchyCounter::Count(SceneObject root, const ScenePrefabCaptureSettings& settings) {
    std::size_t count = 1;
    for (const SceneObject child : ScenePrefabChildrenReader::Read(root, settings)) {
        count += Count(child, settings);
    }
    return count;
}

} // namespace kb::scene
