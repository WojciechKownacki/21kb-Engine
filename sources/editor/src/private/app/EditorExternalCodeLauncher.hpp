#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace kb::editor {

class EditorExternalCodeLauncher final {
public:
    EditorExternalCodeLauncher() = delete;

    [[nodiscard]] static bool OpenProjectFile(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& filePath,
        std::string& error);

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
