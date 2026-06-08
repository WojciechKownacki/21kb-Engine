#pragma once

#include "engine/project/ProjectDescriptor.hpp"

#include <filesystem>
#include <string>

namespace kb::editor {

struct EditorProjectBootstrapResult {
    bool succeeded = false;
    kb::project::ProjectDescriptor descriptor{};
    std::filesystem::path projectFile;
    std::string error;
    bool created = false;
};

class EditorProjectBootstrap {
public:
    EditorProjectBootstrap() = delete;

    [[nodiscard]] static EditorProjectBootstrapResult BootstrapDefaultProject();
};

} // namespace kb::editor
