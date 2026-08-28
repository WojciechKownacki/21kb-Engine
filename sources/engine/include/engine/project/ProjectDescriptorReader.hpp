#pragma once

#include "engine/project/ProjectSettings.hpp"

#include <filesystem>
#include <string>

namespace kb::project {

struct ProjectDescriptorReadResult {
    bool succeeded = false;
    ProjectDescriptor descriptor{};
    std::string error;
    // Populated when the file predates the settings file, so its configuration can
    // be carried into one instead of being dropped on the floor.
    ProjectLegacySettings legacySettings{};
};

class ProjectDescriptorReader {
public:
    ProjectDescriptorReader() = delete;

    [[nodiscard]] static ProjectDescriptorReadResult Read(const std::filesystem::path& path);
};

} // namespace kb::project
