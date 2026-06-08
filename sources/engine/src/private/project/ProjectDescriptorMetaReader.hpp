#pragma once

#include "engine/project/ProjectDescriptorMeta.hpp"

#include <filesystem>
#include <string>

namespace kb::project {

struct ProjectDescriptorMetaReadResult {
    bool succeeded = false;
    ProjectDescriptorMeta meta{};
    std::string error;
};

class ProjectDescriptorMetaReader {
public:
    ProjectDescriptorMetaReader() = delete;

    [[nodiscard]] static ProjectDescriptorMetaReadResult Read(const std::filesystem::path& path);
};

} // namespace kb::project
