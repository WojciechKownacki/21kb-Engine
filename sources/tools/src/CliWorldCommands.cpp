#include "CliCommands.hpp"

#include "engine/world/WorldCellBuilder.hpp"
#include "engine/world/WorldDescriptor.hpp"
#include "engine/world/WorldEditSession.hpp"

#include <charconv>
#include <optional>
#include <string>

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

[[nodiscard]] int Build(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> worldOption = arguments.Option("--world");
    if (!worldOption.has_value()) {
        io.err << "error: world build needs --world <file.21kbworld>\n";
        return 1;
    }
    const std::filesystem::path world = ResolveInputPath(*worldOption, arguments.Option("--project").value_or(""));
    // kb_cli has no mesh loaders; the editor and kb_cooker also build HLOD proxies.
    const kb::world::WorldBuildResult built = kb::world::WorldCellBuilder::Build(world, nullptr);
    if (!built.succeeded) {
        io.err << "error: " << built.error << '\n';
        return 1;
    }
    for (const std::string& warning : built.report.warnings) {
        io.err << "warning: " << warning << '\n';
    }
    io.out << "built " << built.report.unitCount << " cells from " << built.report.objectCount << " objects into "
           << kb::world::WorldPaths::CellsDirectory(world).generic_string() << '\n';
    return 0;
}

} // namespace

int RunWorldCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    if (positionals.size() != 1U) {
        io.err << "error: world needs one subcommand: migrate or build\n";
        return 1;
    }
    if (positionals.front() == "migrate") {
        return Migrate(arguments, io);
    }
    if (positionals.front() == "build") {
        return Build(arguments, io);
    }
    io.err << "error: unknown world subcommand '" << positionals.front() << "'\n";
    return 1;
}

} // namespace kb::cli
