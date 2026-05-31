#pragma once

#include "engine/assets/AssetId.hpp"
#include "kb/editor/assets/EditorAssetBrowserTypes.hpp"

#include <filesystem>
#include <vector>

namespace kb::editor {

class EditorAssetBrowserContextMenuState {
public:
    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] int X() const noexcept;
    [[nodiscard]] int Y() const noexcept;
    [[nodiscard]] EditorAssetContextTargetKind TargetKind() const noexcept;
    [[nodiscard]] kb::assets::AssetId TargetAsset() const noexcept;
    [[nodiscard]] const std::filesystem::path& TargetFolder() const noexcept;
    [[nodiscard]] EditorAssetContextCommand HoveredCommand() const noexcept;
    [[nodiscard]] std::vector<EditorAssetContextMenuItem> Items(bool targetAssetExists, bool targetFolderCanMutate) const;

    void OpenForBackground(int x, int y, const std::filesystem::path& selectedFolder);
    void OpenForAsset(int x, int y, kb::assets::AssetId id);
    void OpenForFolder(int x, int y, const std::filesystem::path& virtualFolder);
    void Close() noexcept;
    [[nodiscard]] bool SetHoveredCommand(EditorAssetContextCommand command) noexcept;

private:
    bool open_ = false;
    int x_ = 0;
    int y_ = 0;
    EditorAssetContextTargetKind targetKind_ = EditorAssetContextTargetKind::None;
    kb::assets::AssetId targetAsset_{};
    std::filesystem::path targetFolder_;
    EditorAssetContextCommand hoveredCommand_ = EditorAssetContextCommand::None;
};

} // namespace kb::editor
