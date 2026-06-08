#pragma once

#include "engine/project/ProjectDescriptor.hpp"

#include <filesystem>
#include <string>

namespace kb::project {

struct ProjectDescriptorReadResult {
    bool succeeded = false;
    ProjectDescriptor descriptor{};
    std::string error;
};

class ProjectDescriptorReader {
public:
    ProjectDescriptorReader() = delete;

    [[nodiscard]] static ProjectDescriptorReadResult Read(const std::filesystem::path& path);
};

} // namespace kb::project
