#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace kb::editor {

struct EditorSavingPreferences {
    bool autosaveEnabled = true;
    std::uint32_t autosaveIntervalMinutes = 10U;

    [[nodiscard]] bool operator==(const EditorSavingPreferences&) const noexcept = default;
};

struct EditorSettingsDocument {
    static constexpr std::uint32_t CurrentFileVersion = 2U;

    EditorSavingPreferences saving{};

    [[nodiscard]] bool operator==(const EditorSettingsDocument&) const noexcept = default;
};

struct EditorSettingsLoadResult {
    EditorSettingsDocument settings{};
    bool found = false;
    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept { return error.empty(); }
};

class EditorSettingsStore final {
public:
    static constexpr std::size_t MaximumBytes = 16U * 1024U;

    [[nodiscard]] static EditorSettingsLoadResult Load(const std::filesystem::path& path);
    [[nodiscard]] static bool Save(
        const std::filesystem::path& path,
        const EditorSettingsDocument& settings,
        std::string& error);
};

} // namespace kb::editor
