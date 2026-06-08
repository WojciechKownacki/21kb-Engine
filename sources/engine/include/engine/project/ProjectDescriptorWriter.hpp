#pragma once

#include "engine/project/ProjectDescriptor.hpp"

#include <filesystem>

namespace kb::project {

class ProjectDescriptorWriter {
public:
    ProjectDescriptorWriter() = delete;

    [[nodiscard]] static bool Write(const std::filesystem::path& path, const ProjectDescriptor& descriptor);
};

} // namespace kb::project
