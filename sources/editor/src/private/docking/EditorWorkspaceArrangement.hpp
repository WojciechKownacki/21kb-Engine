#pragma once

#include "settings/EditorLayoutLibrary.hpp"

namespace kb::editor {

class EditorDockModel;

// A workspace arrangement moving in and out of the dock model. The dock tree places
// every docked panel; the per-panel sessions beside it settle what the tree cannot
// hold - which panels stayed closed, and which ones were torn off into their own
// window.
//
// The particle editor panel is excluded from the per-panel pass: it carries an open
// document and its own floating window, so its host restores it separately.
class EditorWorkspaceArrangement {
public:
    EditorWorkspaceArrangement() = delete;

    // The arrangement on screen right now, ready to be written to a layout file.
    // Documents are deliberately left out: a layout is where panels sit, not what
    // was open in them.
    [[nodiscard]] static EditorLayoutPreset Capture(EditorDockModel& dockModel);

    // Returns false when the dock tree did not fit this build and was discarded, in
    // which case the panels are still placed as far as they can be.
    [[nodiscard]] static bool Apply(EditorDockModel& dockModel, const EditorLayoutPreset& preset);
};

} // namespace kb::editor
