#include "scene/prefab/io/ScenePrefabAssetMeshRendererParser.hpp"

#include "engine/scene/SceneComponents.hpp"

namespace kb::scene {
namespace {

template <typename T>
[[nodiscard]] bool ParseField(const ScenePrefabAssetFieldMap& fields, std::string_view key, T& output) {
    const auto iterator = fields.find(std::string{ key });
    return iterator != fields.end() && ScenePrefabAssetFieldParser::ParseNumber(iterator->second, output);
}

} // namespace

bool ScenePrefabAssetMeshRendererParser::Parse(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool hasMeshRenderer = false;
    if (!ScenePrefabAssetFieldParser::ParseBool(fields, "meshRenderer", hasMeshRenderer)) {
        return false;
    }
    if (!hasMeshRenderer) {
        return true;
    }

    MeshRendererComponent meshRenderer;
    bool castsShadow = false;
    bool receivesShadow = false;
    if (!ParseField(fields, "meshRenderer.meshAssetId", meshRenderer.meshAssetId)
        || !ParseField(fields, "meshRenderer.materialAssetId", meshRenderer.materialAssetId)
        || !ScenePrefabAssetFieldParser::ParseBool(fields, "meshRenderer.castsShadow", castsShadow)
        || !ScenePrefabAssetFieldParser::ParseBool(fields, "meshRenderer.receivesShadow", receivesShadow)) {
        return false;
    }

    meshRenderer.castsShadow = castsShadow;
    meshRenderer.receivesShadow = receivesShadow;
    components.meshRenderer = meshRenderer;
    return true;
}

} // namespace kb::scene
