#pragma once

namespace kb::editor {

class EditorSceneContext;

class EditorPendingTextEditCommitter {
public:
    explicit EditorPendingTextEditCommitter(EditorSceneContext& sceneContext) noexcept;

    [[nodiscard]] bool CommitPendingEdits();

private:
    [[nodiscard]] bool CommitPendingNewAssetFolder();
    [[nodiscard]] bool CommitPendingHierarchyRename();

    EditorSceneContext& sceneContext_;
};

} // namespace kb::editor
