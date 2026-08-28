#pragma once

#include "engine/project/ParticleProjectPolicy.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/project/ProjectSettings.hpp"

#include <filesystem>
#include <string>

namespace kb::editor {

struct EditorProjectBootstrapResult {
    bool succeeded = false;
    kb::project::ProjectDescriptor descriptor{};
    std::filesystem::path projectFile;
    std::string error;
    bool created = false;
    kb::project::ParticleProjectPolicyResult particlePolicy{};
    // Read from Config/ProjectSettings.ini, or seeded from the descriptor and written
    // there when the project does not have one yet.
    kb::project::ProjectSettings settings{};
    std::string settingsError;
};

class EditorProjectBootstrap {
public:
    EditorProjectBootstrap() = delete;

    [[nodiscard]] static EditorProjectBootstrapResult BootstrapDefaultProject();
    [[nodiscard]] static bool AcceptParticleProvider(
        const std::filesystem::path& projectFile,
        kb::project::ProjectDescriptor& descriptor);
};

} // namespace kb::editor
