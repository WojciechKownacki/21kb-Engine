#pragma once

#include "engine/packaging/PackagingTargetCatalog.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "settings/EditorBuildGameSettingsStore.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

// Game: what players see the game called and who made it. Appearance: its icon and loading screen. Advanced:
// file names and the engine build the package uses, filled in for the user and folded away by default.
enum class BuildGameSection : std::uint8_t { Game, Appearance, Application, Content, Signing, Toolchain, Output, Advanced };
enum class BuildGameField : std::uint8_t {
    None, Publisher, Version, ProductName, ExecutableName, ApplicationIcon, Description, Copyright, LoadingScreenImage,
    StartupMap, AndroidApplicationId, AndroidVersionCode, AndroidLabel, AndroidKeystore,
    AndroidKeyAlias, AndroidStorePassword, AndroidKeyPassword,
    EmsdkRoot, LinuxHost, LinuxUser, LinuxHostKey, LinuxPort, LinuxEngineRoot,
    LinuxDisplay, LinuxIdentity,
    OutputDirectory, LaunchAfterBuild, BuilderExecutable, BuildRoot,
    CrashReportUrl,
    WindowsCertificateThumbprint, WindowsCertificateFile, WindowsCertificatePassword, WindowsTimestampUrl,
    AndroidFormat,
};
// ScenePicker: chosen from the project's scenes (a list), not typed. ImagePicker: a picture chosen from disk and
// imported into the project.
// Choice: one of a few values, picked from a list.
enum class BuildGameRowKind : std::uint8_t { Text, Password, FolderPicker, FilePicker, IconPicker, ImagePicker, Checkbox, ScenePicker, Choice };

struct BuildGameRowSpec {
    BuildGameField field = BuildGameField::None;
    // The row's label in the editor language.
    std::string_view (*label)() = nullptr;
    BuildGameRowKind kind = BuildGameRowKind::Text;
    bool required = false;
};

struct BuildGameSectionSpec {
    BuildGameSection section = BuildGameSection::Game;
    std::string_view title;
    std::span<const BuildGameRowSpec> rows;
};

struct BuildGameValidation {
    bool canBuild = false;
    std::string reason;
};

class BuildGamePanelModel final {
public:
    BuildGamePanelModel() = delete;
    // A package target's name and summary in the editor language; the engine catalog keeps English for tools.
    [[nodiscard]] static std::string_view TargetDisplayName(kb::packaging::PackagingTarget target);
    [[nodiscard]] static std::string_view TargetSummary(kb::packaging::PackagingTarget target);
    [[nodiscard]] static std::vector<BuildGameSectionSpec> Sections(kb::packaging::PackagingTarget target, bool release);
    // What the field holds; empty when nothing is set (Hint then says what an empty field means).
    [[nodiscard]] static std::string Value(BuildGameField field, kb::packaging::PackagingTarget target,
        const kb::project::ProjectSettings& project, const EditorBuildGameSettings& local);
    // The dimmed text an empty field shows in place of a value.
    [[nodiscard]] static std::string_view Hint(BuildGameField field);
    // The name the game ships under: the game name, or the project's name until one is given.
    [[nodiscard]] static std::string ShippedGameName(const kb::project::ProjectSettings& project);
    // The file and folder name of the package: the executable name, or one made from the game name in the letters,
    // digits and marks every platform accepts ("Zolta Lodz" for "Żółta Łódź").
    [[nodiscard]] static std::string ShippedFileName(const kb::project::ProjectSettings& project);
    // The Android application id the game ships under: the one chosen, or "com.<studio>.<game>" made from the
    // studio and the file name in the letters Android accepts.
    [[nodiscard]] static std::string ShippedAndroidApplicationId(const kb::project::ProjectSettings& project);
    // The name under the icon on an Android home screen: the label chosen, or the game name.
    [[nodiscard]] static std::string ShippedAndroidLabel(const kb::project::ProjectSettings& project);
    // The text a Choice row shows for one of its values ("aab", "apk").
    [[nodiscard]] static std::string_view ChoiceLabel(BuildGameField field, std::string_view value);
    // Sections folded until the user opens them.
    [[nodiscard]] static constexpr bool CollapsedByDefault(BuildGameSection section) noexcept {
        return section == BuildGameSection::Advanced;
    }
    [[nodiscard]] static BuildGameValidation Validate(kb::packaging::PackagingTarget target,
        const kb::project::ProjectSettings& project, const EditorBuildGameSettings& local,
        bool release, bool jobRunning, bool hasStorePassword = true, bool hasKeyPassword = true,
        bool hasCertificatePassword = true);
    [[nodiscard]] static bool ApplyText(BuildGameField field, std::string_view value,
        kb::packaging::PackagingTarget target, kb::project::ProjectSettings& project,
        EditorBuildGameSettings& local, std::string& error);
    [[nodiscard]] static bool InsertPrintableText(std::string& buffer, bool& selectAll,
        std::string_view text, std::size_t maximumLength);
    [[nodiscard]] static std::vector<BuildGameField> TextFocusOrder(kb::packaging::PackagingTarget target, bool release);
    // Signing passwords: held in memory for one build, never shown, copied or saved.
    [[nodiscard]] static constexpr bool IsSecret(BuildGameField field) noexcept {
        return field == BuildGameField::AndroidStorePassword || field == BuildGameField::AndroidKeyPassword ||
            field == BuildGameField::WindowsCertificatePassword;
    }
};

} // namespace kb::editor
