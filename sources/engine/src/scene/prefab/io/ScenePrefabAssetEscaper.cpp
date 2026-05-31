#include "scene/prefab/io/ScenePrefabAssetEscaper.hpp"

namespace kb::scene {

std::string ScenePrefabAssetEscaper::Escape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(character);
            break;
        }
    }
    return escaped;
}

std::optional<std::string> ScenePrefabAssetEscaper::Unescape(std::string_view value) {
    std::string unescaped;
    unescaped.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char character = value[index];
        if (character != '\\') {
            unescaped.push_back(character);
            continue;
        }

        if (++index >= value.size()) {
            return std::nullopt;
        }

        switch (value[index]) {
        case '\\':
            unescaped.push_back('\\');
            break;
        case 'n':
            unescaped.push_back('\n');
            break;
        case 'r':
            unescaped.push_back('\r');
            break;
        case 't':
            unescaped.push_back('\t');
            break;
        default:
            return std::nullopt;
        }
    }
    return unescaped;
}

} // namespace kb::scene
