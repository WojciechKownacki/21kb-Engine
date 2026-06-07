#include "app/EditorPendingTextEditCommitter.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "scene/EditorSceneContext.hpp"

namespace kb::editor {

EditorPendingTextEditCommitter::EditorPendingTextEditCommitter(EditorSceneContext& sceneContext) noexcept
    : sceneContext_(sceneContext) {}

bool EditorPendingTextEditCommitter::CommitPendingEdits() {
    const bool committedNewFolder = CommitPendingNewAssetFolder();
    const bool committedHierarchyRename = CommitPendingHierarchyRename();
    return committedNewFolder || committedHierarchyRename;
}

bool EditorPendingTextEditCommitter::CommitPendingNewAssetFolder() {
    if (sceneContext_.AssetBrowser().TextEditMode() != EditorAssetTextEditMode::NewFolder) {
        return false;
    }
    static_cast<void>(sceneContext_.CommitAssetTextEdit());
    return true;
}

bool EditorPendingTextEditCommitter::CommitPendingHierarchyRename() {
    if (!sceneContext_.IsHierarchyRenaming()) {
        return false;
    }
    static_cast<void>(sceneContext_.CommitHierarchyRename());
    return true;
}

} // namespace kb::editor
