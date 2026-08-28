#pragma once

#include "kb/editor/docking/DockTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kb::editor {

struct EditorSavingPreferences {
    bool autosaveEnabled = true;
    std::uint32_t autosaveIntervalMinutes = 10U;

    [[nodiscard]] bool operator==(const EditorSavingPreferences&) const noexcept = default;
};

struct EditorPanelSession {
    std::uint32_t panelId = 0U;
    bool visible = true;
    DockArea area = DockArea::Center;
    DockRect floatingRect{148, 140, 900, 640};
    // Project-relative, so a project keeps working when its folder moves.
    std::filesystem::path documentPath;
};

// Everything the editor remembers about itself for one project: how it saves, how
// its panels were arranged, and which documents were open. This is the editor's
// half of the configuration - the project's own settings live beside it in
// ProjectSettings.ini and are read by the game, not just by the editor.
struct EditorConfiguration {
    EditorSavingPreferences saving{};
    // Where each panel was and whether it was open, so reopening a project puts the
    // workspace back the way it was left. Panels absent from the list keep the
    // default workspace's arrangement.
    std::vector<EditorPanelSession> panels;
    // The dock tree, serialized. Empty means the default workspace.
    std::string layout;

    [[nodiscard]] const EditorPanelSession* FindPanel(std::uint32_t panelId) const noexcept;
};

struct EditorConfigurationLoadResult {
    EditorConfiguration configuration{};
    bool found = false;
    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept { return error.empty(); }
};

class EditorConfigurationStore final {
public:
    EditorConfigurationStore() = delete;

    // <project root>/Config/EditorSettings.ini
    [[nodiscard]] static std::filesystem::path FilePath(const std::filesystem::path& projectRoot);

    [[nodiscard]] static EditorConfigurationLoadResult Load(
        const std::filesystem::path& path,
        const std::filesystem::path& projectRoot);
    [[nodiscard]] static bool Save(
        const std::filesystem::path& path,
        const std::filesystem::path& projectRoot,
        const EditorConfiguration& configuration,
        std::string& error);
};

} // namespace kb::editor
