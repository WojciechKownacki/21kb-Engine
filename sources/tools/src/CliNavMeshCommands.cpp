#include "CliCommands.hpp"

#include "engine/navigation/NavGeometryCollector.hpp"
#include "engine/navigation/NavMeshAsset.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/assets/AssetManager.hpp"
#if defined(KB_CLI_WORLD_HLOD)
#include "kb/render/world/NavMeshBakeTool.hpp"
#endif

#include <charconv>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kb::cli {
namespace {

[[nodiscard]] std::optional<float> ParseFloat(std::string_view text) {
    float value = 0.0F;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

// name:radius:height:climb:slope
[[nodiscard]] std::optional<kb::navigation::NavAgentProfile> ParseAgent(std::string_view text) {
    std::vector<std::string_view> parts;
    for (std::size_t start = 0U;;) {
        const std::size_t colon = text.find(':', start);
        parts.push_back(text.substr(start, colon == std::string_view::npos ? std::string_view::npos : colon - start));
        if (colon == std::string_view::npos) break;
        start = colon + 1U;
    }
    if (parts.size() != 5U || parts[0].empty()) {
        return std::nullopt;
    }
    const std::optional<float> radius = ParseFloat(parts[1]);
    const std::optional<float> height = ParseFloat(parts[2]);
    const std::optional<float> climb = ParseFloat(parts[3]);
    const std::optional<float> slope = ParseFloat(parts[4]);
    if (!radius || !height || !climb || !slope) {
        return std::nullopt;
    }
    return kb::navigation::NavAgentProfile{ .name = std::string{ parts[0] }, .radius = *radius, .height = *height, .maxClimb = *climb,
        .maxSlopeDegrees = *slope };
}

[[nodiscard]] int Bake(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> sceneOption = arguments.Option("--scene");
    if (!sceneOption.has_value()) {
        io.err << "error: navmesh bake needs --scene <file.21kbscene>\n";
        return 1;
    }
    const std::optional<std::string> project = arguments.Option("--project");
    const std::filesystem::path scene = ResolveInputPath(*sceneOption, project.value_or(""));
    std::filesystem::path out = kb::navigation::SceneNavMeshPath(scene);
    if (const std::optional<std::string> outOption = arguments.Option("--out"); outOption.has_value()) {
        out = std::filesystem::path{ *outOption };
        if (out.is_relative() && project.has_value()) out = std::filesystem::path{ *project } / out;
    }
    // The settings of the mesh being replaced, else the defaults; options override them.
    kb::navigation::NavMeshBuildSettings settings;
    if (std::error_code code; std::filesystem::is_regular_file(out, code)) {
        const kb::navigation::NavMeshAssetReadResult previous = kb::navigation::NavMeshAssetIO::Read(out);
        if (previous.succeeded) settings = previous.asset.settings;
    }
    const auto floatOption = [&](std::string_view name, float& target) {
        const std::optional<std::string> value = arguments.Option(name);
        if (!value.has_value()) return true;
        const std::optional<float> parsed = ParseFloat(*value);
        if (!parsed.has_value()) return false;
        target = *parsed;
        return true;
    };
    if (!floatOption("--cell-size", settings.cellSize) || !floatOption("--cell-height", settings.cellHeight)) {
        io.err << "error: --cell-size and --cell-height must be numbers of metres\n";
        return 1;
    }
    if (const std::optional<std::string> tileCells = arguments.Option("--tile-cells"); tileCells.has_value()) {
        std::uint32_t value = 0U;
        const auto [end, error] = std::from_chars(tileCells->data(), tileCells->data() + tileCells->size(), value);
        if (error != std::errc{} || end != tileCells->data() + tileCells->size()) {
            io.err << "error: --tile-cells must be a whole number\n";
            return 1;
        }
        settings.tileCells = value;
    }
    if (const std::vector<std::string> agents = arguments.Options("--agent"); !agents.empty()) {
        settings.profiles.clear();
        for (const std::string& agent : agents) {
            const std::optional<kb::navigation::NavAgentProfile> profile = ParseAgent(agent);
            if (!profile.has_value()) {
                io.err << "error: --agent must be <name>:<radius>:<height>:<climb>:<slope>\n";
                return 1;
            }
            settings.profiles.push_back(*profile);
        }
    }
    if (const std::string invalid = kb::navigation::ValidateNavMeshBuildSettings(settings); !invalid.empty()) {
        io.err << "error: " << invalid << '\n';
        return 1;
    }
    std::string error;
    const std::optional<std::filesystem::path> contentRoot = FindProjectContentRoot(project, scene, error);
    if (!contentRoot.has_value()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
#if defined(KB_CLI_WORLD_HLOD)
    // The editor's bake: meshes, terrain and colliders resolved through the cooker's loaders.
    const kb::navigation::NavSceneBakeResult baked = kb::render::BakeSceneNavMeshFromContentRoot(*contentRoot, scene, settings);
#else
    // This kb_cli was configured without the renderer, which owns mesh geometry: terrain and colliders only.
    kb::scene::Scene loaders{ kb::scene::SceneMode::PrefabPrivate };
    kb::assets::AssetManager& assets = loaders.Assets().Manager();
    if (!assets.Mounts().Mount("Game", *contentRoot)) {
        io.err << "error: could not mount the content root " << contentRoot->generic_string() << '\n';
        return 1;
    }
    static_cast<void>(assets.DiscoverMountedAssets());
    kb::navigation::AssetNavGeometrySource source{ assets };
    const kb::navigation::NavSceneBakeResult baked = kb::navigation::BakeSceneNavMesh(scene, settings, &source);
    io.err << "note: this kb_cli was built without the renderer; static meshes are not baked\n";
#endif
    if (!baked.succeeded) {
        io.err << "error: " << baked.error << '\n';
        return 1;
    }
    if (baked.geometry.unresolved != 0U) {
        io.err << "warning: " << baked.geometry.unresolved << " mesh(es) could not be read and were left out\n";
    }
    if (!kb::navigation::NavMeshAssetIO::Write(out, baked.asset, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    io.out << "baked " << baked.stats.tiles << " tiles (" << baked.stats.layers << " layers) for " << settings.profiles.size()
           << " agent profile(s) from " << baked.geometry.meshes << " meshes and " << baked.geometry.colliders << " colliders ("
           << baked.stats.sourceTriangles << " triangles) in " << static_cast<long long>(baked.stats.milliseconds) << " ms into "
           << out.generic_string() << '\n';
    return 0;
}

[[nodiscard]] int Info(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    if (positionals.size() != 2U) {
        io.err << "error: navmesh info needs one <file.21kbnavmesh>\n";
        return 1;
    }
    const kb::navigation::NavMeshAssetReadResult read = kb::navigation::NavMeshAssetIO::Read(positionals[1]);
    if (!read.succeeded) {
        io.err << "error: " << read.error << '\n';
        return 1;
    }
    const kb::navigation::NavMeshBuildSettings& settings = read.asset.settings;
    io.out << "cell size " << settings.cellSize << " m, cell height " << settings.cellHeight << " m, tiles " << settings.tileCells
           << " cells (" << settings.TileWorldSize() << " m)\n";
    std::map<std::uint32_t, std::pair<std::size_t, std::size_t>> perProfile;
    for (const kb::navigation::NavTile& tile : read.asset.tiles) {
        perProfile[tile.profile].first += 1U;
        perProfile[tile.profile].second += tile.layers.size();
    }
    for (std::uint32_t profile = 0U; profile < settings.profiles.size(); ++profile) {
        const kb::navigation::NavAgentProfile& agent = settings.profiles[profile];
        io.out << "agent " << agent.name << ": radius " << agent.radius << ", height " << agent.height << ", climb " << agent.maxClimb
               << ", slope " << agent.maxSlopeDegrees << "; " << perProfile[profile].first << " tiles, " << perProfile[profile].second
               << " layers\n";
    }
    return 0;
}

} // namespace

int RunNavMeshCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    if (positionals.empty()) {
        io.err << "error: navmesh needs one subcommand: bake or info\n";
        return 1;
    }
    if (positionals.front() == "bake" && positionals.size() == 1U) {
        return Bake(arguments, io);
    }
    if (positionals.front() == "info") {
        return Info(arguments, io);
    }
    io.err << "error: unknown navmesh subcommand '" << positionals.front() << "'\n";
    return 1;
}

} // namespace kb::cli
