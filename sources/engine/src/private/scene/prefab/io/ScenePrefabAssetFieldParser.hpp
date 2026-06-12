#pragma once

#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <charconv>
#include <iosfwd>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>

namespace kb::scene {

using ScenePrefabAssetFieldMap = std::unordered_map<std::string, std::string>;

class ScenePrefabAssetFieldParser {
public:
    ScenePrefabAssetFieldParser() = delete;

    [[nodiscard]] static bool ReadLine(std::istream& input, std::string& line);
    [[nodiscard]] static bool SplitKeyValue(std::string_view line, std::string& key, std::string& value);
    [[nodiscard]] static bool ReadNodeFields(std::istream& input, ScenePrefabAssetFieldMap& fields);
    [[nodiscard]] static bool ParseBool(const ScenePrefabAssetFieldMap& fields, std::string_view key, bool& output);
    [[nodiscard]] static bool ParseVec3(const ScenePrefabAssetFieldMap& fields, std::string_view key, Vec3& output);
    [[nodiscard]] static bool ParseNode(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeDesc& node);

    template <typename T>
    [[nodiscard]] static bool ParseNumber(std::string_view text, T& output) {
        if constexpr (std::is_floating_point_v<T>) {
            std::istringstream stream{ std::string{ text } };
            std::string extra;
            return static_cast<bool>(stream >> output) && !(stream >> extra);
        } else {
            const char* first = text.data();
            const char* last = text.data() + text.size();
            const std::from_chars_result result = std::from_chars(first, last, output);
            return result.ec == std::errc{} && result.ptr == last;
        }
    }
};

} // namespace kb::scene
