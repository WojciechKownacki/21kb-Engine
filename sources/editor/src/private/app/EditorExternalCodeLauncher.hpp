#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace kb::editor {

class EditorExternalCodeLauncher final {
public:
    EditorExternalCodeLauncher() = delete;

    // Opens `filePath` in the chosen code editor; with `line` (1-based, and `column`) the editor puts its cursor
    // there. This is how a compiler message in the Console takes the user to the line.
    [[nodiscard]] static bool OpenProjectFile(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& filePath,
        std::string& error,
        int line = 0,
        int column = 0);

    // The file a compiler message names: an absolute path as it is; a relative one from the project folder or,
    // failing that, the one file of that name under the project's Source folder. Empty when there is none.
    [[nodiscard]] static std::filesystem::path ResolveSourceFile(
        const std::filesystem::path& projectRoot, const std::filesystem::path& named);

    // A compiler message in the Console ("path(12): error ...", "path:12:5: error ..."): whether it names a source
    // line, and opening that line in the code editor.
    [[nodiscard]] static bool HasBuildMessageLocation(std::string_view message);
    [[nodiscard]] static bool OpenBuildMessage(
        const std::filesystem::path& projectRoot, std::string_view message, std::string& error);

    [[nodiscard]] static std::string Preference(const std::filesystem::path& projectRoot, std::string& error);
    [[nodiscard]] static bool SetPreference(const std::filesystem::path& projectRoot,
        std::string_view preference, std::string& error);

#if defined(_WIN32)
    // The automation host can exercise asset activation without opening an IDE window.
    using ProcessLauncher = bool (*)(const std::filesystem::path&, const std::wstring&,
        const std::filesystem::path&, std::string&);
    static ProcessLauncher ExchangeProcessLauncher(ProcessLauncher launcher) noexcept;
#endif
};

} // namespace kb::editor
