#include "engine/visual/VisualGraphNativeBuildPipeline.hpp"

#include "engine/script/NativeScriptBuildPipeline.hpp"
#include <string>
#include <string_view>
#include <utility>

namespace kb::visual {
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

    const auto built = kb::script::NativeScriptBuildPipeline::Build({
        .enabled = true, .command = desc.command, .workingDirectory = desc.workingDirectory});
    result.exitCode = built.exitCode;
    for (auto error : built.errors) {
        constexpr std::string_view prefix = "native script build";
        if (error.starts_with(prefix)) error.replace(0, prefix.size(), "visual graph native build");
        result.errors.push_back(std::move(error));
    }
    return result;
}

} // namespace kb::visual
