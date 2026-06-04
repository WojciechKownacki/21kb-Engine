#pragma once

#include "engine/visual/VisualGraphGeneratedCodeWriter.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace kb::visual {

struct VisualGraphNativeBuildDesc {
    bool enabled = false;
    std::string command;
    std::filesystem::path workingDirectory;
};

struct VisualGraphNativeBuildResult {
    int exitCode = 0;
    std::vector<std::string> errors;

    [[nodiscard]] bool Succeeded() const noexcept {
        return errors.empty() && exitCode == 0;
    }
};

class VisualGraphNativeBuildPipeline final {
public:
    VisualGraphNativeBuildPipeline() = delete;

    [[nodiscard]] static VisualGraphNativeBuildResult Build(
        const VisualGraphGeneratedCodeFiles& generatedFiles,
        const VisualGraphNativeBuildDesc& desc);
};

} // namespace kb::visual
