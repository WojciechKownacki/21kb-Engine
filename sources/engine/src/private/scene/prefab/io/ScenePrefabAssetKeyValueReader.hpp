#pragma once

#include <iosfwd>
#include <string>
#include <string_view>

namespace kb::scene {

class ScenePrefabAssetKeyValueReader {
public:
    ScenePrefabAssetKeyValueReader() = delete;

    [[nodiscard]] static bool Read(std::istream& input, std::string_view expectedKey, std::string& value);
    [[nodiscard]] static bool ReadEscaped(std::string_view value, std::string& output);
};

} // namespace kb::scene
