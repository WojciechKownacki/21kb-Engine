#pragma once

#include "engine/assets/AssetId.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace kb::editor {

// Which Lua script is currently open in the Script Editor panel. Pure model
// state (no Win32): the panel's editable text control loads/saves the file from
// FilePath() and reloads whenever Generation() changes.
class EditorScriptEditorState {
public:
    void Open(std::filesystem::path filePath, kb::assets::AssetId assetId, std::string title) {
        filePath_ = std::move(filePath);
        assetId_ = assetId;
        title_ = std::move(title);
        open_ = true;
        ++generation_;
    }

    void Close() noexcept {
        open_ = false;
        ++generation_;
    }

    [[nodiscard]] bool IsOpen() const noexcept { return open_; }
    [[nodiscard]] const std::filesystem::path& FilePath() const noexcept { return filePath_; }
    [[nodiscard]] kb::assets::AssetId AssetId() const noexcept { return assetId_; }
    [[nodiscard]] const std::string& Title() const noexcept { return title_; }

    // Bumped on every Open/Close so the text control knows to reload the file.
    [[nodiscard]] std::uint64_t Generation() const noexcept { return generation_; }

private:
    std::filesystem::path filePath_;
    kb::assets::AssetId assetId_{};
    std::string title_;
    std::uint64_t generation_ = 0;
    bool open_ = false;
};

} // namespace kb::editor
