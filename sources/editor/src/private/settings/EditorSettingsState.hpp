#pragma once

#include "settings/EditorSettingsStore.hpp"

#include <filesystem>
#include <string>

namespace kb::editor {

class EditorSettingsState final {
public:
    [[nodiscard]] int SelectedCategory() const noexcept { return selectedCategory_; }

    [[nodiscard]] bool SelectCategory(int category) noexcept {
        if (category < 0 || category >= 4 || selectedCategory_ == category) return false;
        selectedCategory_ = category;
        return true;
    }

    [[nodiscard]] bool Load(
        const std::filesystem::path& path,
        EditorSettingsDocument& settings,
        std::string& error) {
        path_ = path;
        const EditorSettingsLoadResult result = EditorSettingsStore::Load(path_);
        error = result.error;
        if (!result.Succeeded()) return false;
        settings = result.settings;
        return true;
    }

    [[nodiscard]] bool Commit(
        const EditorSettingsDocument& settings,
        std::string& error) {
        return EditorSettingsStore::Save(path_, settings, error);
    }

private:
    std::filesystem::path path_;
    int selectedCategory_ = 0;
};

} // namespace kb::editor
