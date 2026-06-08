#pragma once

#include "engine/project/ProjectDescriptorReader.hpp"

#include <filesystem>
#include <string_view>

namespace kb::project {

class ProjectManager {
public:
    ProjectManager() = delete;

    [[nodiscard]] static std::string_view Extension() noexcept;
    [[nodiscard]] static bool IsProjectFile(const std::filesystem::path& path);
    [[nodiscard]] static bool SaveProject(const std::filesystem::path& path, const ProjectDescriptor& descriptor);
    [[nodiscard]] static ProjectDescriptorReadResult LoadProject(const std::filesystem::path& path);
    [[nodiscard]] static bool CreateProject(const std::filesystem::path& path, const ProjectDescriptor& descriptor);
};

} // namespace kb::project
