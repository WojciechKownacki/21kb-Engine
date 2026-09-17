#include "CliCommands.hpp"

#include "engine/assets/AssetImportService.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kb::cli {

int RunImportCommand(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    const std::optional<std::string> destination = arguments.Option("--destination");
    if (!project.has_value() || !destination.has_value() || arguments.Positionals().empty()) {
        io.err << "error: import requires --project <dir> --destination /Game/<folder> <file> [more files...]\n";
        return 1;
    }

    kb::scene::Scene scene;
    std::string mountError;
    if (!MountProjectAssets(scene, *project, mountError)) {
        io.err << "error: " << mountError << '\n';
        return 1;
    }

    std::vector<std::filesystem::path> sources;
    sources.reserve(arguments.Positionals().size());
    for (const std::string& source : arguments.Positionals()) {
        sources.push_back(ResolveInputPath(source, *project));
    }

    const kb::assets::AssetImportResult result = kb::assets::AssetImportService::ImportFiles(
        scene.Assets().Manager(), sources, std::filesystem::path{ *destination });
    for (const kb::assets::AssetImportItemResult& item : result.items) {
        if (item.Succeeded()) {
            io.out << kb::assets::ToString(item.status) << " " << item.virtualPath.generic_string() << '\n';
        } else {
            io.err << "error: " << item.sourcePath.string() << ": " << item.error << '\n';
        }
    }
    return result.Succeeded() ? 0 : 1;
}

} // namespace kb::cli
