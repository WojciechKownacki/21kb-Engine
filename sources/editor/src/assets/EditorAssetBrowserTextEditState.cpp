#include "assets/EditorAssetBrowserTextEditState.hpp"

#include <utility>

namespace kb::editor {

EditorAssetTextEditMode EditorAssetBrowserTextEditState::Mode() const noexcept {
    return mode_;
}

kb::assets::AssetId EditorAssetBrowserTextEditState::TargetAsset() const noexcept {
    return asset_;
}

const std::filesystem::path& EditorAssetBrowserTextEditState::TargetFolder() const noexcept {
    return folder_;
}

std::string_view EditorAssetBrowserTextEditState::Value() const noexcept {
    return value_;
}

bool EditorAssetBrowserTextEditState::IsEditing() const noexcept {
    return mode_ != EditorAssetTextEditMode::None;
}

void EditorAssetBrowserTextEditState::BeginNewFolder(const std::filesystem::path& parentFolder) {
    mode_ = EditorAssetTextEditMode::NewFolder;
    asset_ = {};
    folder_ = parentFolder;
    value_ = "NewFolder";
    replaceOnInput_ = true;
}

void EditorAssetBrowserTextEditState::BeginRenameAsset(kb::assets::AssetId id, std::string value) {
    mode_ = EditorAssetTextEditMode::RenameAsset;
    asset_ = id;
    folder_.clear();
    value_ = std::move(value);
    replaceOnInput_ = true;
}

void EditorAssetBrowserTextEditState::BeginRenameFolder(const std::filesystem::path& virtualFolder, std::string value) {
    mode_ = EditorAssetTextEditMode::RenameFolder;
    asset_ = {};
    folder_ = virtualFolder;
    value_ = std::move(value);
    replaceOnInput_ = true;
}

void EditorAssetBrowserTextEditState::SetValue(std::string value) {
    value_ = std::move(value);
    replaceOnInput_ = false;
}

void EditorAssetBrowserTextEditState::Append(wchar_t character) {
    if (character >= 32 && character < 127) {
        if (replaceOnInput_) {
            value_.clear();
            replaceOnInput_ = false;
        }
        value_.push_back(static_cast<char>(character));
    }
}

void EditorAssetBrowserTextEditState::Backspace() {
    if (replaceOnInput_) {
        value_.clear();
        replaceOnInput_ = false;
        return;
    }
    if (!value_.empty()) {
        value_.pop_back();
    }
}

void EditorAssetBrowserTextEditState::Cancel() noexcept {
    mode_ = EditorAssetTextEditMode::None;
    asset_ = {};
    folder_.clear();
    value_.clear();
    replaceOnInput_ = false;
}

} // namespace kb::editor
