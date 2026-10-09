#include "CliCommands.hpp"

#include "engine/project/ProjectManager.hpp"
#include "engine/world/WorldCellBuilder.hpp"
#include "engine/world/WorldDescriptor.hpp"
#include "engine/world/WorldEditSession.hpp"
#include "engine/world/WorldPackChunks.hpp"
#if defined(KB_CLI_WORLD_HLOD)
#include "kb/render/world/WorldBuildTool.hpp"
#endif

#include <algorithm>
#include <charconv>
#include <optional>
#include <string>
#include <vector>

namespace kb::cli {
namespace {

[[nodiscard]] std::optional<double> ParseCellSize(std::string_view text) {
    double value = 0.0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] int Migrate(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> sceneOption = arguments.Option("--scene");
    const std::optional<std::string> outOption = arguments.Option("--out");
    if (!sceneOption.has_value() || !outOption.has_value()) {
        io.err << "error: world migrate needs --scene <file.21kbscene> --out <file.21kbworld>\n";
        return 1;
    }
    const std::filesystem::path projectRoot = arguments.Option("--project").value_or("");
    double cellSize = 128.0;
    if (const std::optional<std::string> size = arguments.Option("--cell-size"); size.has_value()) {
        const std::optional<double> parsed = ParseCellSize(*size);
        if (!parsed.has_value()) {
            io.err << "error: --cell-size must be a number of metres\n";
            return 1;
        }
        cellSize = *parsed;
    }
    const std::filesystem::path scene = ResolveInputPath(*sceneOption, projectRoot);
    std::filesystem::path out{ *outOption };
    if (out.is_relative() && !projectRoot.empty()) {
        out = projectRoot / out;
    }
    const kb::world::WorldMigrationResult migrated = kb::world::WorldMigration::ConvertScene(scene, out, cellSize);
    if (!migrated.succeeded) {
        io.err << "error: " << migrated.error << '\n';
        return 1;
    }
    io.out << "migrated " << migrated.objectCount << " objects from " << scene.generic_string() << " to "
           << migrated.descriptorPath.generic_string() << '\n';
    return 0;
}

// The project file of a project directory: Project.21kbproject, else the only .21kbproject in it.
[[nodiscard]] std::optional<std::filesystem::path> ProjectFileIn(const std::filesystem::path& directory) {
    std::error_code code;
    if (std::filesystem::is_regular_file(directory / "Project.21kbproject", code)) {
        return directory / "Project.21kbproject";
    }
    std::vector<std::filesystem::path> candidates;
    for (std::filesystem::directory_iterator it{ directory, code }, end; !code && it != end; it.increment(code)) {
        if (it->is_regular_file(code) && it->path().extension() == ".21kbproject") candidates.push_back(it->path());
    }
    if (candidates.size() != 1U) return std::nullopt;
    return candidates.front();
}

// The directory the cooker and the editor mount as /Game: the project's content root. Without
// --project it is found from the nearest folder above the world that holds a project file.
[[nodiscard]] std::optional<std::filesystem::path> ContentRoot(
    const std::optional<std::string>& project, const std::filesystem::path& world, std::string& error) {
    std::optional<std::filesystem::path> projectFile;
    if (project.has_value()) {
        projectFile = ProjectFileIn(*project);
    } else {
        for (std::filesystem::path folder = std::filesystem::absolute(world).parent_path(); !folder.empty();
             folder = folder.parent_path()) {
            projectFile = ProjectFileIn(folder);
            if (projectFile.has_value() || folder == folder.root_path()) break;
        }
    }
    if (!projectFile.has_value()) {
        error = "world build needs the world's project (--project <dir>) to resolve the meshes of its HLOD proxies";
        return std::nullopt;
    }
    const kb::project::ProjectDescriptorReadResult descriptor = kb::project::ProjectManager::LoadProject(*projectFile);
    if (!descriptor.succeeded) {
        error = "project descriptor could not be read: " + descriptor.error;
        return std::nullopt;
    }
    return (projectFile->parent_path() / std::filesystem::path{ descriptor.descriptor.contentRoot }).lexically_normal();
}

[[nodiscard]] int Build(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> worldOption = arguments.Option("--world");
    if (!worldOption.has_value()) {
        io.err << "error: world build needs --world <file.21kbworld>\n";
        return 1;
    }
    const std::optional<std::string> project = arguments.Option("--project");
    const std::filesystem::path world = ResolveInputPath(*worldOption, project.value_or(""));
#if defined(KB_CLI_WORLD_HLOD)
    std::string error;
    const std::optional<std::filesystem::path> contentRoot = ContentRoot(project, world, error);
    if (!contentRoot.has_value()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    // The cooker's and the editor's build: cells, cell index and HLOD proxies.
    const kb::world::WorldBuildResult built = kb::render::BuildWorldFromContentRoot(*contentRoot, world);
#else
    // This kb_cli was configured without the renderer, which owns mesh geometry: cells only.
    const kb::world::WorldBuildResult built = kb::world::WorldCellBuilder::Build(world, nullptr);
    io.err << "note: this kb_cli was built without the renderer; HLOD proxies are not built\n";
#endif
    if (!built.succeeded) {
        io.err << "error: " << built.error << '\n';
        return 1;
    }
    for (const std::string& warning : built.report.warnings) {
        io.err << "warning: " << warning << '\n';
    }
    io.out << "built " << built.report.unitCount << " cells and " << built.report.hlodCount << " HLOD proxies from "
           << built.report.objectCount << " objects into " << kb::world::WorldPaths::CellsDirectory(world).generic_string() << '\n';
    return 0;
}

// One "chunk LABEL=PREFIX,..." line per region of every world built in the project: the
// --chunk rules of kb_cli pack split and the --pack-chunk rules of package_game.py.
[[nodiscard]] int Chunks(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: world chunks needs --project <dir>\n";
        return 1;
    }
    std::string error;
    const std::optional<std::filesystem::path> contentRoot = ContentRoot(project, *project, error);
    if (!contentRoot.has_value()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    std::vector<std::string> excluded;
    const std::string excludeText = arguments.Option("--exclude").value_or("");
    for (std::size_t start = 0U; start < excludeText.size();) {
        const std::size_t comma = std::min(excludeText.find(',', start), excludeText.size());
        if (comma > start) excluded.push_back(excludeText.substr(start, comma - start));
        start = comma + 1U;
    }
    const kb::world::WorldRegionChunksResult chunks = kb::world::CollectWorldRegionChunks(*contentRoot, excluded);
    if (!chunks.succeeded) {
        io.err << "error: " << chunks.error << '\n';
        return 1;
    }
    for (const kb::world::WorldRegionChunk& chunk : chunks.chunks) {
        io.out << "chunk " << chunk.label << '=';
        for (std::size_t prefix = 0U; prefix < chunk.prefixes.size(); ++prefix) {
            io.out << (prefix == 0U ? "" : ",") << chunk.prefixes[prefix];
        }
        io.out << '\n';
    }
    return 0;
}

} // namespace

int RunWorldCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    if (positionals.size() != 1U) {
        io.err << "error: world needs one subcommand: migrate, build or chunks\n";
        return 1;
    }
    if (positionals.front() == "migrate") {
        return Migrate(arguments, io);
    }
    if (positionals.front() == "build") {
        return Build(arguments, io);
    }
    if (positionals.front() == "chunks") {
        return Chunks(arguments, io);
    }
    io.err << "error: unknown world subcommand '" << positionals.front() << "'\n";
    return 1;
}

} // namespace kb::cli
