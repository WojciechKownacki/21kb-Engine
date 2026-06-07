#pragma once

#include "engine/assets/AssetId.hpp"
#include "kb/editor/assets/EditorAssetBrowserTypes.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace kb::editor {

class EditorAssetBrowserTextEditState {
public:
    [[nodiscard]] EditorAssetTextEditMode Mode() const noexcept;
    [[nodiscard]] kb::assets::AssetId TargetAsset() const noexcept;
    [[nodiscard]] const std::filesystem::path& TargetFolder() const noexcept;
    [[nodiscard]] std::string_view Value() const noexcept;
    [[nodiscard]] bool IsEditing() const noexcept;

    void BeginNewFolder(const std::filesystem::path& parentFolder);
    void BeginRenameAsset(kb::assets::AssetId id, std::string value);
    void BeginRenameFolder(const std::filesystem::path& virtualFolder, std::string value);
    void SetValue(std::string value);
    void Append(wchar_t character);
    void Insert(std::string_view text);
    void Backspace();
    void Clear() noexcept;
    void SelectAll() noexcept;
    void Cancel() noexcept;

private:
    EditorAssetTextEditMode mode_ = EditorAssetTextEditMode::None;
    kb::assets::AssetId asset_{};
    std::filesystem::path folder_;
    std::string value_;
    bool replaceOnInput_ = false;
};

} // namespace kb::editor
