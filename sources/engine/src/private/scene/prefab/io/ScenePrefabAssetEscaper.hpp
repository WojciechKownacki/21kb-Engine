#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace kb::scene {

class ScenePrefabAssetEscaper {
public:
    ScenePrefabAssetEscaper() = delete;

    [[nodiscard]] static std::string Escape(std::string_view value);
    [[nodiscard]] static std::optional<std::string> Unescape(std::string_view value);
};

} // namespace kb::scene
