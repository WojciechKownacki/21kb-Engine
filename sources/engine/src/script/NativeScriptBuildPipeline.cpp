#include "engine/script/NativeScriptBuildPipeline.hpp"

#include <cstdlib>

namespace kb::script {
namespace {

class ScopedWorkingDirectory final {
public:
    explicit ScopedWorkingDirectory(const std::filesystem::path& workingDirectory, std::vector<std::string>& errors)
        : errors_(errors) {
        std::error_code error;
        originalDirectory_ = std::filesystem::current_path(error);
        if (error) {
            errors_.push_back("native script build could not read current working directory");
            return;
        }
        active_ = true;
        if (!workingDirectory.empty()) {
            std::filesystem::current_path(workingDirectory, error);
            if (error) {
                errors_.push_back("native script build working directory could not be selected");
                active_ = false;
            }
        }
    }

    ~ScopedWorkingDirectory() {
        if (!active_) {
            return;
        }
        std::error_code error;
        std::filesystem::current_path(originalDirectory_, error);
        if (error) {
            errors_.push_back("native script build could not restore working directory");
        }
    }

    ScopedWorkingDirectory(const ScopedWorkingDirectory&) = delete;
    ScopedWorkingDirectory& operator=(const ScopedWorkingDirectory&) = delete;

    [[nodiscard]] bool Succeeded() const noexcept {
        return active_;
    }

private:
    std::filesystem::path originalDirectory_;
    std::vector<std::string>& errors_;
    bool active_ = false;
};

} // namespace

NativeScriptBuildResult NativeScriptBuildPipeline::Build(const NativeScriptBuildDesc& desc) {
    NativeScriptBuildResult result{};
    if (!desc.enabled) {
        return result;
    }
    if (desc.command.empty()) {
        result.errors.push_back("native script build command is empty");
        return result;
    }

    ScopedWorkingDirectory workingDirectory{ desc.workingDirectory, result.errors };
    if (!workingDirectory.Succeeded()) {
        return result;
    }
    result.exitCode = std::system(desc.command.c_str());
    if (result.exitCode != 0) {
        result.errors.push_back("native script build command failed with exit code " + std::to_string(result.exitCode));
    }
    return result;
}

} // namespace kb::script
