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

enum class BuildGameSection : std::uint8_t { Project, Application, Content, Signing, Toolchain, Output };
enum class BuildGameField : std::uint8_t {
    None, ProjectName, Publisher, Version, ProductName, ExecutableName, ApplicationIcon,
    StartupMap, AndroidApplicationId, AndroidVersionCode, AndroidLabel, AndroidKeystore,
    AndroidKeyAlias, AndroidStorePassword, AndroidKeyPassword,
    EmsdkRoot, LinuxHost, LinuxUser, LinuxHostKey, LinuxPort, LinuxEngineRoot,
    LinuxDisplay, LinuxIdentity,
    OutputDirectory, LaunchAfterBuild, BuilderExecutable, BuildRoot,
};
enum class BuildGameRowKind : std::uint8_t { ReadOnly, Text, Password, FolderPicker, FilePicker, IconPicker, Checkbox };

struct BuildGameRowSpec {
    BuildGameField field = BuildGameField::None;
    std::string_view label;
    BuildGameRowKind kind = BuildGameRowKind::ReadOnly;
    bool required = false;
};

struct BuildGameSectionSpec {
    BuildGameSection section = BuildGameSection::Project;
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
    [[nodiscard]] static std::vector<BuildGameSectionSpec> Sections(kb::packaging::PackagingTarget target, bool release);
    [[nodiscard]] static std::string Value(BuildGameField field, kb::packaging::PackagingTarget target,
        const kb::project::ProjectSettings& project, const EditorBuildGameSettings& local);
    [[nodiscard]] static BuildGameValidation Validate(kb::packaging::PackagingTarget target,
        const kb::project::ProjectSettings& project, const EditorBuildGameSettings& local,
        bool release, bool jobRunning, bool hasStorePassword = true, bool hasKeyPassword = true);
    [[nodiscard]] static bool ApplyText(BuildGameField field, std::string_view value,
        kb::packaging::PackagingTarget target, kb::project::ProjectSettings& project,
        EditorBuildGameSettings& local, std::string& error);
    [[nodiscard]] static bool InsertPrintableText(std::string& buffer, bool& selectAll,
        std::string_view text, std::size_t maximumLength);
    [[nodiscard]] static std::vector<BuildGameField> TextFocusOrder(kb::packaging::PackagingTarget target, bool release);
};

} // namespace kb::editor
