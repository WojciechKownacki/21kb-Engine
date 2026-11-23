#pragma once

#include "packaging/EditorProjectPackageService.hpp"
#include "settings/EditorBuildGameSettingsStore.hpp"

#include "engine/packaging/PackagingTargetCatalog.hpp"
#include "engine/project/ProjectSettings.hpp"

#include <filesystem>
#include <string>

namespace kb::editor {

// What a package job is made from: the project's settings, the editor's build settings (builder, build folder, output
// folders, signing references), the target, and what only this run knows.
struct EditorPackageRequestInput {
    const kb::project::ProjectSettings& project;
    const EditorBuildGameSettings& local;
    const kb::packaging::PackagingTargetSpec& target;
    bool release = false;
    std::filesystem::path projectFile;
    std::filesystem::path projectRoot;
    std::filesystem::path engineRoot;
    // Passwords exist for one build only and are never stored.
    std::string androidStorePassword;
    std::string androidKeyPassword;
    std::string windowsCertificatePassword;
};

// The one place that turns the settings into a package job (scripts/package_game.py's request), shared by the editor's
// Build Game panel and `kb_cli build --platform`. Returns false with `error` when the job cannot be made: output or build
// folders inside the project, a name that cannot form a package folder, an icon that is not a PNG of the project.
[[nodiscard]] bool BuildPackageRequest(const EditorPackageRequestInput& input, EditorPackageRequest& request, std::string& error);

} // namespace kb::editor
