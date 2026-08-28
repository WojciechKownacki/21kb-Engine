#pragma once

#include "settings/EditorLayoutLibrary.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

enum class EditorLayoutMenuAction {
    // Put the workspace back on the arrangement a new project starts with.
    Default,
    // Load one of the saved layouts.
    Apply,
    // Store the current arrangement under a name.
    Save,
    // Turn the menu into the list of layouts that can be removed.
    Delete,
    // Remove this one.
    Remove,
};

struct EditorLayoutMenuRow {
    std::string label;
    EditorLayoutMenuAction action = EditorLayoutMenuAction::Apply;
    // The layout this row acts on, for Apply and Remove rows.
    std::string layoutName;
    // The arrangement currently on screen, marked in the menu.
    bool active = false;
    // A caption row that only says what the list is for.
    bool enabled = true;
};

// The Layout menu: the default arrangement, then every saved layout, then the two
// commands that manage them. Kept apart from the window code so the menu's content
// and the meaning of each row can be checked without a window.
class EditorLayoutMenuModel final {
public:
    // The list the menu is built from is bounded by the library, so the menu cannot
    // grow past the screen no matter what the layouts folder holds: the default
    // arrangement, every saved layout, and the two commands.
    static constexpr std::size_t MaximumRows = EditorLayoutLibrary::MaximumLayouts + 3U;

    void Rebuild(const std::vector<std::string>& savedLayouts, std::string_view activeLayout);
    // The same list turned into the second step of Delete Layout: pick which one goes.
    void RebuildForDelete(const std::vector<std::string>& savedLayouts);

    [[nodiscard]] const std::vector<EditorLayoutMenuRow>& Rows() const noexcept;
    [[nodiscard]] const EditorLayoutMenuRow* Row(int index) const noexcept;

private:
    std::vector<EditorLayoutMenuRow> rows_;
};

} // namespace kb::editor
