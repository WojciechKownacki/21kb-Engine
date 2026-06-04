#include "engine/visual/VisualGraphNativeBuildPipeline.hpp"

#include <cstdlib>
#include <string>

namespace kb::visual {
namespace {

class ScopedWorkingDirectory final {
public:
    explicit ScopedWorkingDirectory(const std::filesystem::path& workingDirectory, std::vector<std::string>& errors)
        : errors_(errors) {
        std::error_code error;
        originalDirectory_ = std::filesystem::current_path(error);
        if (error) {
            errors_.push_back("visual graph native build could not read current working directory");
            return;
        }
        active_ = true;
        if (!workingDirectory.empty()) {
            std::filesystem::current_path(workingDirectory, error);
            if (error) {
                errors_.push_back("visual graph native build working directory could not be selected");
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
            errors_.push_back("visual graph native build could not restore working directory");
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

VisualGraphNativeBuildResult VisualGraphNativeBuildPipeline::Build(
    const VisualGraphGeneratedCodeFiles& generatedFiles,
    const VisualGraphNativeBuildDesc& desc) {
    VisualGraphNativeBuildResult result{};
    if (!desc.enabled) {
        return result;
    }
    if (desc.command.empty()) {
        result.errors.push_back("visual graph native build command is empty");
        return result;
    }
    if (generatedFiles.headerPath.empty() || generatedFiles.sourcePath.empty()) {
        result.errors.push_back("visual graph native build requires generated code files");
        return result;
    }
    if (!std::filesystem::is_regular_file(generatedFiles.headerPath) || !std::filesystem::is_regular_file(generatedFiles.sourcePath)) {
        result.errors.push_back("visual graph native build generated code files are missing");
        return result;
    }

    ScopedWorkingDirectory workingDirectory{ desc.workingDirectory, result.errors };
    if (!workingDirectory.Succeeded()) {
        return result;
    }
    result.exitCode = std::system(desc.command.c_str());
    if (result.exitCode != 0) {
        result.errors.push_back("visual graph native build command failed with exit code " + std::to_string(result.exitCode));
    }
    return result;
}

} // namespace kb::visual
