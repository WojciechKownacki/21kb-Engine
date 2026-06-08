#pragma once

#include "HubState.hpp"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace kb::hub::HubProjectFilters {

[[nodiscard]] inline std::wstring Lower(std::wstring value) {
    std::ranges::transform(value, value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

[[nodiscard]] inline bool MatchesSearch(const HubProjectItem& project, const std::wstring& query) {
    if (query.empty()) {
        return true;
    }

    const std::wstring needle = Lower(query);
    return Lower(project.name).find(needle) != std::wstring::npos ||
        Lower(project.projectRoot.wstring()).find(needle) != std::wstring::npos ||
        Lower(project.description).find(needle) != std::wstring::npos;
}

[[nodiscard]] inline std::vector<int> VisibleIndices(const HubState& state) {
    std::vector<int> indices;
    indices.reserve(state.projects.size());
    for (std::size_t index = 0; index < state.projects.size(); ++index) {
        if (MatchesSearch(state.projects[index], state.searchQuery)) {
            indices.push_back(static_cast<int>(index));
        }
    }
    return indices;
}

[[nodiscard]] inline int VisibleCount(const HubState& state) {
    return static_cast<int>(std::ranges::count_if(state.projects, [&state](const HubProjectItem& project) {
        return MatchesSearch(project, state.searchQuery);
    }));
}

} // namespace kb::hub::HubProjectFilters
