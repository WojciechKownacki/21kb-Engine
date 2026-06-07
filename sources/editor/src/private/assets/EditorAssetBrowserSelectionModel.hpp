#pragma once

#include "engine/assets/AssetId.hpp"
#include "kb/editor/assets/EditorAssetBrowserTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <unordered_set>

namespace kb::editor {

class EditorAssetBrowserSelectionModel {
public:
    [[nodiscard]] const std::filesystem::path& SelectedFolder() const noexcept;
    [[nodiscard]] const std::filesystem::path& SelectedContentFolder() const noexcept;
    [[nodiscard]] kb::assets::AssetId SelectedAsset() const noexcept;
    [[nodiscard]] EditorAssetBrowserSelectionKind SelectionKind() const noexcept;
    [[nodiscard]] bool IsSelectionFocused() const noexcept;
    [[nodiscard]] bool IsContentFolderSelected(const std::filesystem::path& virtualPath) const;
    [[nodiscard]] bool IsAssetSelected(kb::assets::AssetId id) const noexcept;

    void FocusSelection(bool focused) noexcept;
    void SelectFolder(std::filesystem::path virtualPath);
    void SelectContentFolder(std::filesystem::path virtualPath);
    void AddContentFolder(std::filesystem::path virtualPath);
    void ToggleContentFolder(std::filesystem::path virtualPath);
    void SelectAsset(kb::assets::AssetId id, std::filesystem::path folder);
    void AddAsset(kb::assets::AssetId id, std::filesystem::path folder);
    void ToggleAsset(kb::assets::AssetId id, std::filesystem::path folder);
    void ClearContentSelection() noexcept;
    void ClearSelection() noexcept;

private:
    void ClearSelectedContentItems() noexcept;
    void NormalizePrimaryAfterToggle() noexcept;

    std::filesystem::path selectedFolder_{ "/Game" };
    std::filesystem::path selectedContentFolder_;
    EditorAssetBrowserSelectionKind selectionKind_ = EditorAssetBrowserSelectionKind::None;
    bool selectionFocused_ = false;
    kb::assets::AssetId selectedAsset_{};
    std::unordered_set<std::string> selectedContentFolders_;
    std::unordered_set<std::uint64_t> selectedAssets_;
};

} // namespace kb::editor
