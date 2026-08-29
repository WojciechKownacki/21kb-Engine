#pragma once

#include <string>

namespace kb::editor {

class EditorDockModel;
class EditorSceneContext;

// The editor settings file's half of the workspace: the arrangement a project was
// last left in, and the name of the saved layout it came from. The arrangement
// itself is moved in and out of the dock model by EditorWorkspaceArrangement.
class EditorWorkspaceSession {
public:
    EditorWorkspaceSession() = delete;

    static void Restore(EditorDockModel& dockModel, EditorSceneContext& context);
    // Records the arrangement, open documents included, keeping the name of the
    // layout it came from.
    static void Save(EditorDockModel& dockModel, EditorSceneContext& context);
    // The same, but the arrangement is now that of the named layout. An empty name
    // means the workspace is on an arrangement no saved layout owns.
    static void SaveAs(EditorDockModel& dockModel, EditorSceneContext& context, std::string layoutName);
};

} // namespace kb::editor
