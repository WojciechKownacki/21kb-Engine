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

[[nodiscard]] bool ParseOptionalLod(const ScenePrefabAssetFieldMap& fields, MeshRendererComponent& meshRenderer) {
    const auto parse = [&fields](std::string_view key, auto& output) {
        const auto iterator = fields.find(std::string{ key });
        return iterator == fields.end() || ScenePrefabAssetFieldParser::ParseNumber(iterator->second, output);
    };
    int lodEnabled = meshRenderer.lodEnabled ? 1 : 0;
    int affectsNavigation = meshRenderer.affectsNavigation ? 1 : 0;
    std::uint32_t mobility = static_cast<std::uint32_t>(meshRenderer.mobility);
    if (!parse("meshRenderer.lodBias", meshRenderer.lodBias) || !parse("meshRenderer.lodEnabled", lodEnabled) ||
        (lodEnabled != 0 && lodEnabled != 1) || !parse("meshRenderer.mobility", mobility) ||
        mobility > static_cast<std::uint32_t>(MeshMobility::Static) ||
        !parse("meshRenderer.affectsNavigation", affectsNavigation) || (affectsNavigation != 0 && affectsNavigation != 1)) {
        return false;
    }
    meshRenderer.lodEnabled = lodEnabled != 0;
    meshRenderer.affectsNavigation = affectsNavigation != 0;
    meshRenderer.mobility = static_cast<MeshMobility>(mobility);
    return IsMeshRendererComponentValid(meshRenderer);
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
        || !ScenePrefabAssetFieldParser::ParseBool(fields, "meshRenderer.receivesShadow", receivesShadow)
        || !ParseField(fields, "meshRenderer.layer", meshRenderer.layer)
        || !ParseOptionalLod(fields, meshRenderer)) {
        return false;
    }

    meshRenderer.castsShadow = castsShadow;
    meshRenderer.receivesShadow = receivesShadow;
    components.meshRenderer = meshRenderer;
    return true;
}

} // namespace kb::scene
