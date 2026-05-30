#include "scene/EditorHierarchySearchMatcher.hpp"

#include <cctype>

namespace kb::editor {

bool EditorHierarchySearchMatcher::Matches(std::string_view name, std::string_view normalizedQuery) {
    return Normalize(name).find(normalizedQuery) != std::string::npos;
}

std::string EditorHierarchySearchMatcher::Normalize(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (const char ch : text) {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return normalized;
}

} // namespace kb::editor
