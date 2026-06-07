#pragma once

#include "assets/EditorAssetBrowserFolderExpansionState.hpp"
#include "assets/EditorAssetBrowserSelectionModel.hpp"
#include "engine/assets/AssetManager.hpp"
#include "kb/editor/assets/EditorAssetBrowserTypes.hpp"

#include <filesystem>
#include <vector>

namespace kb::editor {

class EditorAssetBrowserSelectionState {
public:
    [[nodiscard]] const std::filesystem::path& SelectedFolder() const noexcept;
    [[nodiscard]] const std::filesystem::path& SelectedContentFolder() const noexcept;
    [[nodiscard]] kb::assets::AssetId SelectedAsset() const noexcept;
    [[nodiscard]] EditorAssetBrowserSelectionKind SelectionKind() const noexcept;
    [[nodiscard]] bool IsSelectionFocused() const noexcept;
    [[nodiscard]] bool IsContentFolderSelected(const std::filesystem::path& virtualPath) const;
    [[nodiscard]] bool IsAssetSelected(kb::assets::AssetId id) const noexcept;

    void FocusSelection(bool focused) noexcept;
    [[nodiscard]] bool SelectFolder(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager);
    [[nodiscard]] bool SelectContentFolder(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager);
    [[nodiscard]] bool AddContentFolder(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager);
    [[nodiscard]] bool ToggleContentFolder(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager);
    [[nodiscard]] bool SelectAsset(kb::assets::AssetId id, const kb::assets::AssetManager& manager);
    [[nodiscard]] bool AddAsset(kb::assets::AssetId id, const kb::assets::AssetManager& manager);
    [[nodiscard]] bool ToggleAsset(kb::assets::AssetId id, const kb::assets::AssetManager& manager);
    void ClearContentSelection() noexcept;
    [[nodiscard]] bool ToggleFolderExpanded(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager);
    void ClearSelection() noexcept;

    [[nodiscard]] bool FolderExists(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) const;
    [[nodiscard]] std::vector<EditorAssetFolderRow> FolderRows(const kb::assets::AssetManager& manager) const;
    [[nodiscard]] std::vector<EditorAssetFolderRow> ChildFolderRows(const kb::assets::AssetManager& manager) const;

private:
    EditorAssetBrowserSelectionModel selection_;
    EditorAssetBrowserFolderExpansionState expansion_;
};

} // namespace kb::editor
