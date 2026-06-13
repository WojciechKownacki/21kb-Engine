#include "scene/prefab/io/ScenePrefabAssetComponentParser.hpp"

#include "scene/prefab/io/ScenePrefabAssetCameraParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetInputParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetLightParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetMeshRendererParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetTagsParser.hpp"

namespace kb::scene {
bool ScenePrefabAssetComponentParser::Parse(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    return ScenePrefabAssetCameraParser::Parse(fields, components)
        && ScenePrefabAssetMeshRendererParser::Parse(fields, components)
        && ScenePrefabAssetLightParser::Parse(fields, components)
        && ScenePrefabAssetInputParser::Parse(fields, components)
        && ScenePrefabAssetTagsParser::Parse(fields, components);
}

} // namespace kb::scene
