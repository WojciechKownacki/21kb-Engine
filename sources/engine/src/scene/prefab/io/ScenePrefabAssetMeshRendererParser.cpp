#include "scene/prefab/io/ScenePrefabAssetMeshRendererParser.hpp"

#include "engine/scene/SceneComponents.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace kb::scene {
namespace {

template <typename T>
[[nodiscard]] bool ParseField(const ScenePrefabAssetFieldMap& fields, std::string_view key, T& output) {
    const auto iterator = fields.find(std::string{ key });
    return iterator != fields.end() && ScenePrefabAssetFieldParser::ParseNumber(iterator->second, output);
}

[[nodiscard]] bool ParseAssetId(const ScenePrefabAssetFieldMap& fields, std::string_view key, std::uint64_t& output) {
    std::size_t value = 0;
    if (!ParseField(fields, key, value)) {
        return false;
    }
    output = static_cast<std::uint64_t>(value);
    return true;
}

[[nodiscard]] bool ParseOptionalMaterialSlotOverrides(const ScenePrefabAssetFieldMap& fields, MeshRendererComponent& meshRenderer) {
    const auto countIterator = fields.find("meshRenderer.materialSlotOverrideCount");
    if (countIterator == fields.end()) {
        return true;
    }

    std::size_t parsedCount = 0U;
    if (!ScenePrefabAssetFieldParser::ParseNumber(countIterator->second, parsedCount)) {
        return false;
    }

    meshRenderer.materialSlotOverrideCount = static_cast<std::uint32_t>(
        std::min<std::size_t>(parsedCount, kMaxMeshRendererMaterialSlotOverrides));
    for (std::uint32_t slotIndex = 0U; slotIndex < meshRenderer.materialSlotOverrideCount; ++slotIndex) {
        const std::string key = "meshRenderer.materialSlotAssetId." + std::to_string(slotIndex);
        if (!ParseAssetId(fields, key, meshRenderer.materialSlotAssetIds[slotIndex])) {
            return false;
        }
    }
    return true;
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
    if (!ParseAssetId(fields, "meshRenderer.meshAssetId", meshRenderer.meshAssetId)
        || !ParseAssetId(fields, "meshRenderer.materialAssetId", meshRenderer.materialAssetId)
        || !ParseOptionalMaterialSlotOverrides(fields, meshRenderer)
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
