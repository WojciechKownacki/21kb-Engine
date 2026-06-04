#include "scene/EditorSceneContext.hpp"

#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/script/ScriptBehaviourAsset.hpp"

#include "scene/EditorDefaultSceneFactory.hpp"
#include "scene/EditorHierarchyRowBuilder.hpp"
#include "scene/EditorSceneAssetBrowserCommands.hpp"
#include "scene/EditorSceneHierarchyActions.hpp"
#include "scene/EditorScenePrefabActions.hpp"
#include "project/EditorProjectPaths.hpp"

#include <optional>
#include <utility>

namespace kb::editor {

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

EditorViewportPreviewState& EditorSceneContext::ViewportPreview() noexcept {
    return ViewportPreview(0U);
}

const EditorViewportPreviewState& EditorSceneContext::ViewportPreview() const noexcept {
    return ViewportPreview(0U);
}

EditorViewportPreviewState& EditorSceneContext::ViewportPreview(std::uint64_t viewportKey) noexcept {
    return viewportPreviews_.try_emplace(viewportKey).first->second;
}

const EditorViewportPreviewState& EditorSceneContext::ViewportPreview(std::uint64_t viewportKey) const noexcept {
    return viewportPreviews_.try_emplace(viewportKey).first->second;
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
    return EditorSceneAssetBrowserCommands::CommitTextEdit(scene_, assetBrowser_);
}

void EditorSceneContext::CancelAssetTextEdit() noexcept {
    assetBrowser_.CancelTextEdit();
}

bool EditorSceneContext::DeleteSelectedAssetBrowserItem() {
    return EditorSceneAssetBrowserCommands::DeleteSelected(scene_, assetBrowser_);
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
    return EditorSceneAssetBrowserCommands::DeleteAsset(scene_, assetBrowser_, id);
}

bool EditorSceneContext::DeleteAssetBrowserFolder(const std::filesystem::path& virtualFolder) {
    return EditorSceneAssetBrowserCommands::DeleteFolder(scene_, assetBrowser_, virtualFolder);
}

bool EditorSceneContext::MoveAssetToFolder(kb::assets::AssetId id, const std::filesystem::path& destinationVirtualFolder) {
    return EditorSceneAssetBrowserCommands::MoveAssetToFolder(scene_, assetBrowser_, id, destinationVirtualFolder);
}

bool EditorSceneContext::MoveAssetFolderToFolder(const std::filesystem::path& sourceVirtualFolder, const std::filesystem::path& destinationVirtualFolder) {
    return EditorSceneAssetBrowserCommands::MoveFolderToFolder(scene_, assetBrowser_, sourceVirtualFolder, destinationVirtualFolder);
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

bool EditorSceneContext::AddBehaviourAssetToEntity(kb::assets::AssetId assetId, kb::scene::SceneEntity entity) {
    if (!assetId.IsValid() || !entity.IsValid() || !scene_.Entities().IsAlive(entity)) {
        return false;
    }

    const kb::assets::AssetMetadata* metadata = scene_.Assets().Manager().Registry().Find(assetId);
    if (metadata == nullptr) {
        return false;
    }

    const std::optional<kb::scene::BehaviourComponent> behaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*metadata);
    if (!behaviour.has_value()) {
        return false;
    }

    scene_.Components().Behaviours().Set(entity, *behaviour);
    SelectEntity(entity);
    return true;
}

} // namespace kb::editor
