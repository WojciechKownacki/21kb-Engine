#pragma once

#include "engine/project/ProjectDescriptor.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace kb::project {

struct NewProjectResult {
    bool succeeded = false;
    std::filesystem::path projectFile;
    std::string error;
};

// The one way a new project comes into being - the Hub and kb_cli both call it - so a project made
// without a window is the same project the Hub makes: <parent>/<name>/<name>.21kbproject, the
// Assets/Scenes and Assets/Prefabs folders and the settings file the editor and the game both read.
// `name` must already be a valid project name (letters, digits, '-' and '_'); nothing is left on
// disk when the project cannot be created.
class ProjectCreator {
public:
    ProjectCreator() = delete;

    [[nodiscard]] static bool IsValidName(std::string_view name) noexcept;
    // What every new project starts with - the editor's default project, the Hub and `kb_cli new`
    // alike: content under Assets, Windows target, and the built-in plugins enabled (physics,
    // audio, lighting, particles), referenced by their file name next to the executables.
    [[nodiscard]] static ProjectDescriptor DefaultDescriptor();
    [[nodiscard]] static NewProjectResult Create(const std::filesystem::path& parentDirectory, std::string_view name);
};

} // namespace kb::project
