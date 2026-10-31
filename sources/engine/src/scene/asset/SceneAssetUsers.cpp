#include "engine/scene/SceneAssetUsers.hpp"

#include "scene/asset/SceneComponentAssetReferences.hpp"

#include <algorithm>
#include <string_view>

namespace kb::scene {

std::vector<SceneAssetUser> CollectSceneAssetUsers(const SceneDocument& document) {
    std::vector<SceneAssetUser> users;
    for (const ScenePrefabNodeDesc& node : document.worldPrefab.Nodes()) {
        const std::size_t first = users.size();
        const auto add = [&users, &node, first](std::uint64_t rawId, std::string_view, const UIComponentPropertyDescriptor*) {
            if (rawId == 0U || std::any_of(users.begin() + static_cast<std::ptrdiff_t>(first), users.end(),
                    [rawId](const SceneAssetUser& user) { return user.assetId == rawId; })) {
                return;
            }
            users.push_back(SceneAssetUser{ .assetId = rawId, .objectName = node.name });
        };
        SceneComponentAssetReferences::ForEachReference(node.components, add);
        static_cast<void>(SceneComponentAssetReferences::ForEachUIOverrideReference(node.nestedPrefabOverrides, add));
    }
    return users;
}

} // namespace kb::scene
