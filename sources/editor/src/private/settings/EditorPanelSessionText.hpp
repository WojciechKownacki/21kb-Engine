#pragma once

#include "settings/EditorConfigurationStore.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace kb::editor {

// One panel's placement as a settings-file entry, shared by the editor settings and
// by the saved layouts so both files speak the same dialect:
//
//   Panel.<id> = <area> <visible> <x> <y> <w> <h> [document]
//
// The document path is stored project-relative, so moving a project folder does not
// strand the document a panel was showing.
class EditorPanelSessionText final {
public:
    EditorPanelSessionText() = delete;

    // Fails only when the document sits outside the project - a settings file records
    // where work happened inside one project, and must not point out of it.
    [[nodiscard]] static bool Format(
        const EditorPanelSession& session,
        const std::filesystem::path& projectRoot,
        std::string& value);

    [[nodiscard]] static std::string Key(std::uint32_t panelId);

    [[nodiscard]] static bool Parse(
        std::string_view key,
        std::string_view value,
        const std::filesystem::path& projectRoot,
        EditorPanelSession& session);
};

} // namespace kb::editor
