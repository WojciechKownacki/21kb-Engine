#pragma once

#include "engine/assets/AssetId.hpp"
#include "kb/editor/assets/EditorAssetBrowserTypes.hpp"

#include <filesystem>

namespace kb::editor {

class EditorAssetBrowserSelectionModel {
public:
    [[nodiscard]] const std::filesystem::path& SelectedFolder() const noexcept;
    [[nodiscard]] const std::filesystem::path& SelectedContentFolder() const noexcept;
    [[nodiscard]] kb::assets::AssetId SelectedAsset() const noexcept;
    [[nodiscard]] EditorAssetBrowserSelectionKind SelectionKind() const noexcept;
    [[nodiscard]] bool IsSelectionFocused() const noexcept;

    void FocusSelection(bool focused) noexcept;
    void SelectFolder(std::filesystem::path virtualPath);
    void SelectContentFolder(std::filesystem::path virtualPath);
    void SelectAsset(kb::assets::AssetId id, std::filesystem::path folder);
    void ClearSelection() noexcept;

private:
    std::filesystem::path selectedFolder_{ "/Game" };
    std::filesystem::path selectedContentFolder_;
    EditorAssetBrowserSelectionKind selectionKind_ = EditorAssetBrowserSelectionKind::None;
    bool selectionFocused_ = false;
    kb::assets::AssetId selectedAsset_{};
};

} // namespace kb::editor
