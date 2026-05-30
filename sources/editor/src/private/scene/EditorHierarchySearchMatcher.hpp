#pragma once

#include <string>
#include <string_view>

namespace kb::editor {

class EditorHierarchySearchMatcher {
public:
    EditorHierarchySearchMatcher() = delete;

    [[nodiscard]] static bool Matches(std::string_view name, std::string_view normalizedQuery);
    [[nodiscard]] static std::string Normalize(std::string_view text);
};

} // namespace kb::editor
