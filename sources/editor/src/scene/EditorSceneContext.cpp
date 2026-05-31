#include "scene/EditorSceneContext.hpp"

#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAssets.hpp"

#include "scene/EditorDefaultSceneFactory.hpp"
#include "scene/EditorHierarchyRowBuilder.hpp"
#include "scene/EditorSceneHierarchyActions.hpp"
#include "scene/EditorScenePrefabActions.hpp"
#include "project/EditorProjectPaths.hpp"

#include <optional>
#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] bool SameVirtualPath(const std::filesystem::path& left, const std::filesystem::path& right) {
    return kb::assets::NormalizeAssetPath(left) == kb::assets::NormalizeAssetPath(right);
}

} // namespace

EditorSceneContext::EditorSceneContext() {
    std::filesystem::create_directories(EditorProjectPaths::PrefabsRoot());
    static_cast<void>(scene_.Assets().MountProject(EditorProjectPaths::ProjectRoot()));
    static_cast<void>(scene_.Assets().Discover());
    selectedEntity_ = EditorDefaultSceneFactory::Seed(scene_);
}

kb::scene::Scene& EditorSceneContext::Scene() noexcept {
    return scene_;
}

const kb::scene::Scene& EditorSceneContext::Scene() const noexcept {
    return scene_;
}

EditorAssetBrowserState& EditorSceneContext::AssetBrowser() noexcept {
    return assetBrowser_;
}

const EditorAssetBrowserState& EditorSceneContext::AssetBrowser() const noexcept {
    return assetBrowser_;
}

kb::scene::SceneEntity EditorSceneContext::SelectedEntity() const noexcept {
    return selectedEntity_;
}

void EditorSceneContext::SelectEntity(kb::scene::SceneEntity entity) noexcept {
    selectedEntity_ = scene_.Entities().IsAlive(entity) ? entity : kb::scene::SceneEntity{};
    assetBrowser_.ClearSelection();
}

void EditorSceneContext::ClearHierarchySelection() noexcept {
    selectedEntity_ = {};
}

bool EditorSceneContext::SelectHierarchyRow(std::size_t rowIndex) noexcept {
    const std::vector<EditorHierarchyRow> rows = HierarchyRows();
    if (rowIndex >= rows.size()) {
        selectedEntity_ = {};
        return false;
    }

    SelectEntity(rows[rowIndex].entity);
    return selectedEntity_.IsValid();
}

std::vector<EditorHierarchyRow> EditorSceneContext::HierarchyRows() const {
    return EditorHierarchyRowBuilder::Build(scene_, hierarchyExpansion_.CollapsedEntities(), hierarchySearch_.Query());
}

std::string_view EditorSceneContext::HierarchySearchQuery() const noexcept {
    return hierarchySearch_.Query();
}

bool EditorSceneContext::IsHierarchySearchFocused() const noexcept {
    return hierarchySearch_.IsFocused();
}

void EditorSceneContext::FocusHierarchySearch(bool focused) noexcept {
    hierarchySearch_.Focus(focused);
}

void EditorSceneContext::SetHierarchySearchQuery(std::string query) {
    hierarchySearch_.SetQuery(std::move(query));
}

void EditorSceneContext::AppendHierarchySearchText(wchar_t character) {
    hierarchySearch_.AppendAscii(character);
}

void EditorSceneContext::BackspaceHierarchySearch() {
    hierarchySearch_.Backspace();
}

void EditorSceneContext::ClearHierarchySearch() {
    hierarchySearch_.Clear();
}

bool EditorSceneContext::BeginAssetFolderCreation() {
    assetBrowser_.BeginNewFolder();
    return true;
}

bool EditorSceneContext::BeginAssetRename() {
    return assetBrowser_.BeginRenameSelection(scene_.Assets().Manager());
}

bool EditorSceneContext::BeginAssetRename(kb::assets::AssetId id) {
    return assetBrowser_.BeginRenameAsset(id, scene_.Assets().Manager());
}

bool EditorSceneContext::BeginAssetFolderRename(const std::filesystem::path& virtualFolder) {
    return assetBrowser_.BeginRenameFolder(virtualFolder, scene_.Assets().Manager());
}

bool EditorSceneContext::CommitAssetTextEdit() {
    kb::assets::AssetManager& manager = scene_.Assets().Manager();
    const std::string value{ assetBrowser_.TextEditValue() };
    if (value.empty()) {
        assetBrowser_.CancelTextEdit();
        return false;
    }

    bool committed = false;
    switch (assetBrowser_.TextEditMode()) {
    case EditorAssetTextEditMode::NewFolder: {
        const std::filesystem::path parent = assetBrowser_.TextEditTargetFolder().empty() ? assetBrowser_.SelectedFolder() : assetBrowser_.TextEditTargetFolder();
        const std::optional<std::filesystem::path> folder = manager.CreateUniqueFolder(parent, value);
        committed = folder.has_value();
        if (folder.has_value()) {
            static_cast<void>(scene_.Assets().Discover());
            static_cast<void>(assetBrowser_.SelectContentFolder(*folder, manager));
        }
        break;
    }
    case EditorAssetTextEditMode::RenameAsset: {
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(assetBrowser_.TextEditTargetAsset());
        if (metadata != nullptr) {
            const std::filesystem::path renamedVirtualPath = metadata->virtualPath.parent_path() / (value + metadata->virtualPath.extension().string());
            committed = manager.RenameAsset(metadata->id, value);
            if (committed) {
                if (const kb::assets::AssetMetadata* renamed = manager.Registry().FindByPath(renamedVirtualPath); renamed != nullptr) {
                    static_cast<void>(assetBrowser_.SelectAsset(renamed->id, manager));
                }
            }
        }
        break;
    }
    case EditorAssetTextEditMode::RenameFolder: {
        const std::filesystem::path oldFolder = assetBrowser_.TextEditTargetFolder();
        const std::filesystem::path renamedFolder = oldFolder.parent_path() / value;
        const bool renamedOpenFolder = SameVirtualPath(oldFolder, assetBrowser_.SelectedFolder());
        committed = manager.RenameFolder(oldFolder, value);
        if (committed) {
            if (renamedOpenFolder) {
                static_cast<void>(assetBrowser_.SelectFolder(renamedFolder, manager));
            } else {
                static_cast<void>(assetBrowser_.SelectContentFolder(renamedFolder, manager));
            }
        }
        break;
    }
    case EditorAssetTextEditMode::None:
    default:
        break;
    }

    assetBrowser_.CancelTextEdit();
    return committed;
}

void EditorSceneContext::CancelAssetTextEdit() noexcept {
    assetBrowser_.CancelTextEdit();
}

bool EditorSceneContext::DeleteSelectedAssetBrowserItem() {
    const kb::assets::AssetId selected = assetBrowser_.SelectedAsset();
    const std::filesystem::path selectedContentFolder = assetBrowser_.SelectedContentFolder();
    kb::assets::AssetManager& manager = scene_.Assets().Manager();
    const bool deletingAsset = selected.IsValid();
    const bool deletingContentFolder = !selectedContentFolder.empty();
    const std::filesystem::path folderToDelete = deletingContentFolder ? selectedContentFolder : assetBrowser_.SelectedFolder();
    const bool deleted = deletingAsset ? manager.DeleteAsset(selected) : manager.DeleteFolder(folderToDelete);
    if (deleted) {
        assetBrowser_.ClearSelection();
        if (!deletingAsset && !deletingContentFolder) {
            static_cast<void>(assetBrowser_.SelectFolder(assetBrowser_.SelectedFolder().parent_path(), manager));
        }
        static_cast<void>(scene_.Assets().Discover());
    }
    return deleted;
}

bool EditorSceneContext::DeleteSelectedHierarchyEntity() noexcept {
    if (!selectedEntity_.IsValid() || !scene_.Entities().IsAlive(selectedEntity_)) {
        selectedEntity_ = {};
        return false;
    }
    const kb::scene::SceneEntity deleting = selectedEntity_;
    selectedEntity_ = {};
    scene_.Entities().Destroy(deleting);
    return true;
}

bool EditorSceneContext::DeleteAssetBrowserItem(kb::assets::AssetId id) {
    kb::assets::AssetManager& manager = scene_.Assets().Manager();
    const bool deleted = manager.DeleteAsset(id);
    if (deleted) {
        assetBrowser_.ClearSelection();
        static_cast<void>(scene_.Assets().Discover());
    }
    return deleted;
}

bool EditorSceneContext::DeleteAssetBrowserFolder(const std::filesystem::path& virtualFolder) {
    kb::assets::AssetManager& manager = scene_.Assets().Manager();
    const bool deleted = manager.DeleteFolder(virtualFolder);
    if (deleted) {
        if (SameVirtualPath(assetBrowser_.SelectedFolder(), virtualFolder)) {
            static_cast<void>(assetBrowser_.SelectFolder(virtualFolder.parent_path(), manager));
        } else if (SameVirtualPath(assetBrowser_.SelectedContentFolder(), virtualFolder)) {
            assetBrowser_.ClearSelection();
        }
        static_cast<void>(scene_.Assets().Discover());
    }
    return deleted;
}

bool EditorSceneContext::MoveAssetToFolder(kb::assets::AssetId id, const std::filesystem::path& destinationVirtualFolder) {
    kb::assets::AssetManager& manager = scene_.Assets().Manager();
    const kb::assets::AssetMoveResult moved = manager.MoveAssetIntoFolder(id, destinationVirtualFolder);
    if (!moved.succeeded) {
        return false;
    }

    static_cast<void>(scene_.Assets().Discover());
    if (const kb::assets::AssetMetadata* movedMetadata = manager.Registry().FindByPath(moved.virtualPath); movedMetadata != nullptr) {
        static_cast<void>(assetBrowser_.SelectAsset(movedMetadata->id, manager));
    }
    return true;
}

bool EditorSceneContext::MoveAssetFolderToFolder(const std::filesystem::path& sourceVirtualFolder, const std::filesystem::path& destinationVirtualFolder) {
    kb::assets::AssetManager& manager = scene_.Assets().Manager();
    const bool movedOpenFolder = SameVirtualPath(assetBrowser_.SelectedFolder(), sourceVirtualFolder);
    const kb::assets::AssetMoveResult moved = manager.MoveFolderIntoFolder(sourceVirtualFolder, destinationVirtualFolder);
    if (!moved.succeeded) {
        return false;
    }

    static_cast<void>(scene_.Assets().Discover());
    if (!moved.virtualPath.empty()) {
        if (movedOpenFolder) {
            static_cast<void>(assetBrowser_.SelectFolder(moved.virtualPath, manager));
        } else {
            static_cast<void>(assetBrowser_.SelectContentFolder(moved.virtualPath, manager));
        }
    }
    return true;
}

bool EditorSceneContext::ToggleHierarchyRowExpanded(std::size_t rowIndex) {
    const std::vector<EditorHierarchyRow> rows = HierarchyRows();
    if (rowIndex >= rows.size() || !rows[rowIndex].hasChildren) {
        return false;
    }

    hierarchyExpansion_.SetExpanded(rows[rowIndex].entity, !rows[rowIndex].expanded);
    return true;
}

bool EditorSceneContext::ToggleEntityVisibility(kb::scene::SceneEntity entity) {
    if (!EditorSceneHierarchyActions::ToggleVisibility(scene_, entity)) {
        return false;
    }
    SelectEntity(entity);
    return true;
}

kb::scene::SceneEntity EditorSceneContext::CreateHierarchyObject() {
    const kb::scene::SceneEntity entity = EditorSceneHierarchyActions::CreateObject(scene_);
    SelectEntity(entity);
    return entity;
}

bool EditorSceneContext::ReparentEntity(kb::scene::SceneEntity child, kb::scene::SceneEntity parent) {
    const bool moved = EditorSceneHierarchyActions::Reparent(scene_, child, parent);
    if (moved) {
        SelectEntity(child);
    }
    return moved;
}

bool EditorSceneContext::CreatePrefabAsset(kb::scene::SceneEntity entity, const std::filesystem::path& path) {
    const bool created = EditorScenePrefabActions::CreateAsset(scene_, entity, path);
    if (created) {
        static_cast<void>(scene_.Assets().Discover());
        if (const std::optional<std::filesystem::path> virtualPath = scene_.Assets().Manager().Mounts().ToVirtual(path)) {
            if (const kb::assets::AssetMetadata* metadata = scene_.Assets().Manager().Registry().FindByPath(*virtualPath); metadata != nullptr) {
                static_cast<void>(assetBrowser_.SelectAsset(metadata->id, scene_.Assets().Manager()));
            }
        }
    }
    return created;
}

bool EditorSceneContext::InstantiatePrefabAsset(const std::filesystem::path& path, kb::scene::SceneEntity parent) {
    const std::optional<kb::scene::SceneEntity> root = EditorScenePrefabActions::InstantiateAsset(scene_, path, parent);
    if (!root.has_value()) {
        return false;
    }
    SelectEntity(*root);
    return true;
}

} // namespace kb::editor
