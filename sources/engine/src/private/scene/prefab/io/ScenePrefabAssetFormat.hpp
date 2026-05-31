#pragma once

#include <string_view>

namespace kb::scene {

struct ScenePrefabAssetFormat {
    static constexpr std::string_view Header = "21kb.prefab.v1";

    static constexpr std::string_view NameKey = "name";
    static constexpr std::string_view NodesKey = "nodes";
    static constexpr std::string_view ParentKey = "parent";
    static constexpr std::string_view LocalPositionKey = "localPosition";
    static constexpr std::string_view LocalRotationKey = "localRotation";
    static constexpr std::string_view LocalScaleKey = "localScale";
    static constexpr std::string_view VisibleKey = "visible";

    static constexpr std::string_view NodeMarker = "node";
    static constexpr std::string_view EndNodeMarker = "endnode";
};

} // namespace kb::scene
