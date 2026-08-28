#pragma once

namespace kb::editor {

class EditorDockModel;
class EditorSceneContext;

// Reopening a project puts the workspace back the way it was left. The dock tree
// stored in the editor settings file places every docked panel; the per-panel
// sessions beside it settle what the tree cannot hold - which panels stayed closed,
// and which ones were torn off into their own window.
//
// The particle editor panel is excluded from both directions: it carries an open
// document and its own floating window, so its host restores it separately.
class EditorWorkspaceSession {
public:
    EditorWorkspaceSession() = delete;

    static void Restore(EditorDockModel& dockModel, EditorSceneContext& context);
    static void Save(EditorDockModel& dockModel, EditorSceneContext& context);
};

} // namespace kb::editor
