#pragma once

#include "engine/scene/SceneTransforms.hpp"
#include "scene/prefab/io/ScenePrefabAssetFieldParser.hpp"

#include <string>
#include <string_view>

namespace kb::scene {

class ScenePrefabAssetNodeFieldParser {
public:
    ScenePrefabAssetNodeFieldParser() = delete;

    [[nodiscard]] static bool ParseInt(const ScenePrefabAssetFieldMap& fields, std::string_view key, int& output);
    [[nodiscard]] static bool ParseQuat(const ScenePrefabAssetFieldMap& fields, std::string_view key, Quat& output);
    [[nodiscard]] static bool ParseEscapedString(const ScenePrefabAssetFieldMap& fields, std::string_view key, std::string& output);
    [[nodiscard]] static bool ParseOptionalEscapedString(const ScenePrefabAssetFieldMap& fields, std::string_view key, std::string& output);
};

} // namespace kb::scene
