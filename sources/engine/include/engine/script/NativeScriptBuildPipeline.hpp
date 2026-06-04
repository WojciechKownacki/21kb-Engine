#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace kb::script {

struct NativeScriptBuildDesc {
    bool enabled = false;
    std::string command;
    std::filesystem::path workingDirectory;
};

struct NativeScriptBuildResult {
    int exitCode = 0;
    std::vector<std::string> errors;

    [[nodiscard]] bool Succeeded() const noexcept {
        return errors.empty() && exitCode == 0;
    }
};

class NativeScriptBuildPipeline final {
public:
    NativeScriptBuildPipeline() = delete;

    [[nodiscard]] static NativeScriptBuildResult Build(const NativeScriptBuildDesc& desc);
};

} // namespace kb::script
