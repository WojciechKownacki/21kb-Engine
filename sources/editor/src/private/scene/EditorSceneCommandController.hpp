#pragma once

#include "scene/EditorHierarchyRow.hpp"

#include <functional>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorAssetBrowserState;
class EditorCommandStack;
class EditorConsoleState;
class EditorHierarchyExpansionState;
class EditorHierarchySearchState;
class EditorHierarchySelectionState;
class EditorSceneViewportStateStore;

class EditorSceneCommandController {
public:
    using Mutation = std::function<bool()>;

    EditorSceneCommandController(
        kb::scene::Scene& scene,
        EditorCommandStack& commandStack,
        EditorConsoleState& console,
        EditorSceneViewportStateStore& viewportState,
        EditorHierarchySelectionState& hierarchySelection,
        EditorAssetBrowserState& assetBrowser,
        EditorHierarchyExpansionState& hierarchyExpansion,
        EditorHierarchySearchState& hierarchySearch,
        std::optional<std::string>& pendingTransactionLabel,
        std::uint64_t& sceneRenderRevision,
        bool& hierarchyRowsDirty) noexcept;

    [[nodiscard]] bool Undo();
    [[nodiscard]] bool Redo();
    [[nodiscard]] bool BeginTransaction(std::string label);
    [[nodiscard]] bool CommitTransaction();
    void CancelTransaction();
    [[nodiscard]] bool Execute(std::string label, Mutation mutation);

private:
    [[nodiscard]] std::vector<EditorHierarchyRow> HierarchyRows() const;
    void NormalizeHierarchySelectionAfterSceneRestore();
    void NotifySceneChanged();

    kb::scene::Scene& scene_;
    EditorCommandStack& commandStack_;
    EditorConsoleState& console_;
    EditorSceneViewportStateStore& viewportState_;
    EditorHierarchySelectionState& hierarchySelection_;
    EditorAssetBrowserState& assetBrowser_;
    EditorHierarchyExpansionState& hierarchyExpansion_;
    EditorHierarchySearchState& hierarchySearch_;
    std::optional<std::string>& pendingTransactionLabel_;
    std::uint64_t& sceneRenderRevision_;
    bool& hierarchyRowsDirty_;
};

} // namespace kb::editor
