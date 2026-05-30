#include "scene/prefab/ScenePrefabChildrenReader.hpp"

namespace kb::scene {

std::vector<SceneObject> ScenePrefabChildrenReader::Read(SceneObject object, const ScenePrefabCaptureSettings& settings) {
    return settings.includeChildren ? object.Children() : std::vector<SceneObject>{};
}

} // namespace kb::scene
