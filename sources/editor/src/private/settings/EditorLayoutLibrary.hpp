#pragma once

#include "settings/EditorConfigurationStore.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

// One saved workspace arrangement: the dock tree plus where each panel sat.
struct EditorLayoutPreset {
    std::string tree;
    std::vector<EditorPanelSession> panels;
};

// Named layouts a person saves and switches between, one file each under
// <project>/Config/Layouts. A file per layout means a layout can be copied to
// another project, shared with a team mate, or put under version control on its
// own, and a broken one cannot take the rest of the list down with it.
class EditorLayoutLibrary final {
public:
    EditorLayoutLibrary() = delete;

    static constexpr std::size_t MaximumNameLength = 48U;
    // Deep enough for any real set of arrangements, and shallow enough that the menu
    // built from it always fits on screen without needing to scroll. Saving past this
    // is refused with a message rather than quietly dropped.
    static constexpr std::size_t MaximumLayouts = 12U;

    // Letters, digits, spaces, dashes and underscores only, and never blank or
    // padded: the name is also the file name, so it must not be able to reach out
    // of the layouts folder or collide with the shell's reserved characters.
    [[nodiscard]] static bool IsValidName(std::string_view name) noexcept;

    [[nodiscard]] static std::filesystem::path DirectoryPath(const std::filesystem::path& projectRoot);
    [[nodiscard]] static std::filesystem::path FilePath(
        const std::filesystem::path& projectRoot, std::string_view name);

    // Sorted, so the menu keeps a stable order between sessions.
    [[nodiscard]] static std::vector<std::string> List(const std::filesystem::path& projectRoot);

    [[nodiscard]] static bool Save(
        const std::filesystem::path& projectRoot,
        std::string_view name,
        const EditorLayoutPreset& preset,
        std::string& error);

    [[nodiscard]] static std::optional<EditorLayoutPreset> Load(
        const std::filesystem::path& projectRoot, std::string_view name);

    [[nodiscard]] static bool Delete(const std::filesystem::path& projectRoot, std::string_view name);
};

} // namespace kb::editor
