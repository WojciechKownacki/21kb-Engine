#pragma once

#include "engine/project/ProjectDescriptorMeta.hpp"

#include <filesystem>

namespace kb::project {

class ProjectDescriptorMetaWriter {
public:
    ProjectDescriptorMetaWriter() = delete;

    [[nodiscard]] static bool Write(const std::filesystem::path& path, const ProjectDescriptorMeta& meta);
};

} // namespace kb::project
