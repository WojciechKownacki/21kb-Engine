#include "CliCommands.hpp"

#include "TerrainAnalysis.hpp"
#include "TerrainChannel.hpp"
#include "TerrainChannel.hpp"
#include "TerrainErosion.hpp"
#include "TerrainGenerators.hpp"
#include "TerrainHeightmapImporter.hpp"
#include "TerrainImageExport.hpp"
#include "TerrainLayerPainter.hpp"
#include "TerrainOps.hpp"
#include "TerrainPresets.hpp"
#include "TerrainTask.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/assets/TerrainAssetIO.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// terrain: the terrain editor's brushes, material layers and heightmap import for agents, on
// .kbterrain files - the same code the Terrain Editor runs, so a file edited here is the file the
// editor opens.
namespace kb::cli {
namespace {

using kb::terrain_editor::TerrainBackgroundTask;
using kb::terrain_editor::TerrainTaskControl;

constexpr std::string_view kUsage =
    "usage: kb_cli terrain <action> [--project <dir>] --terrain <file> ...\n"
    "  --terrain is /Game/Terrains/Hills.kbterrain, Assets/..., or a file path (a path outside a project\n"
    "  needs no --project). Positions x,z are in world units from the terrain's centre (see info).\n"
    "  new        [--name <Entity> [--scene <path>] [--position x,y,z]] [--terrain <file>] [--resolution 129]\n"
    "             [--size 128|256x128] [--chunk-quads 32] [--lod-count 4] [--preset flat|plains|hills|valley|mountains|island|plateau] [--seed 1]\n"
    "             [--character 0..1 (gentle to dramatic, default 0.5)]\n"
    "             (flat unless a preset makes a whole world: generated, then eroded; resolution is 2^n+1, 17..2049;\n"
    "             --name also places it in the scene; the generating options below apply to a preset too)\n"
    "  info       --terrain <file>              (size, heights, slopes, layers, height hash)\n"
    "  sample     --terrain <file> --x <n> --z <n>   (height, normal, slope and layer weights there)\n"
    "  brush      --terrain <file> [--mode raise|lower|smooth|flatten|noise|terrace|cut-hole|fill-hole|ramp|stamp]\n"
    "             [--shape soft-round|hard-round|linear-round|bell|ring|speckle]\n"
    "             (--x <n> --z <n> | --path \"x,z;x,z\") [--radius 8] [--strength 1] [--falloff 0.65]\n"
    "             [--invert yes (Ctrl: raise lowers, ...)] [--symmetry x|z|xz (mirrored across x=0 and/or z=0)]\n"
    "             [--spacing <n>] (strength is per stamp; a path stamps every --spacing, default a quarter radius)\n"
    "             flatten: [--height <n>|sample]   terrace: [--step 2] [--softness 0..1]   noise: [--seed <n>]\n"
    "             [--noise-kind gradient|value] [--noise-scale 16] [--noise-octaves 3]\n"
    "             ramp: --x/--z the start, --to x,z the end, [--height <n> --to-height <n>] (none: the terrain's own),\n"
    "             --radius the half width, --falloff the flat share of the strip\n"
    "             stamp: --image <heightmap.png> [--rotation 0] [--blend add|max|min] [--height <base for max/min>]\n"
    "             (strength = height of white; the image covers a square of side 2 x radius)\n"
    "  apply      --terrain <file> --ops <ops.json | inline JSON>   (many strokes in one save; op members\n"
    "             are the brush/paint/auto-paint options: {\"ops\":[{\"op\":\"brush\",\"mode\":\"raise\",\"x\":0,\"z\":0}]})\n"
    "  layer      list | add (--material <.kbmat path> | --material-id <n>)\n"
    "             | set --index <n> (--material <path> | --material-id <n>) | remove --index <n>   (up to 4)\n"
    "  paint      --terrain <file> --layer <n> (--x <n> --z <n> | --path \"x,z;x,z\") [--radius 8] [--opacity 1]\n"
    "             [--falloff 0.65] [--shape soft-round] [--erase yes]\n"
    "             | --terrain <file> --layer <n> --auto slope=35..90,height=-5..40 [--slope-blend 3] [--height-blend 1]\n"
    "             [--opacity 1]   (by the ground's slope in degrees and height)\n"
    "  generate   --terrain <file> [--kind fbm|ridged|island|plateau|valley] [--seed 1] [--amplitude 24] [--base 0] [--scale <world units>]\n"
    "             [--octaves 6] [--lacunarity 2] [--gain 0.5] [--noise-kind gradient|value] [--warp 0] [--add yes (instead of replace)]\n"
    "             [--island-radius 0.65] [--island-falloff 0.3] [--plateau-level 0.55] [--plateau-softness 0.04]\n"
    "             [--valley-width 0.15] [--valley-wall 0.7]\n"
    "             (heights from noise, on every core, the same seed gives the same ground; --threads <n> --max-seconds <n>)\n"
    "  erode      --terrain <file> [--mode hydraulic|thermal] [--iterations 150] (hydraulic: [--rain 0.0015] [--evaporation 0.03]\n"
    "             [--strength 0.4]; thermal: [--talus 35] [--strength 0.5]) [--threads <n>] [--max-seconds <n>]\n"
    "  import     --source <heightmap.png|.raw|.r16> --terrain <file> [--min -32] [--max 32] [--size 512[xD]]\n"
    "             [--flip yes] [--replace]   (the resolution becomes the nearest 2^n+1)\n"
    "  export     --terrain <file> --out <file.png|.raw|.r16> [--min <n> --max <n>]   (16 bit; prints the range)\n"
    "  preview    --terrain <file> --out <file.png> [--kind height|slope|splat] [--pixels 512]\n"
    "  river      rivers of the scene, drawn as water and cut into a terrain (a River component; --scene as in entity):\n"
    "             add --project <dir> --name <Entity> --points \"x,y,z[,width,depth,speed];...\" [--index <n>] [--position x,y,z]\n"
    "                 [--bank-width 3] [--waterfall-slope 35] [--material </Game/..kbmat>] [--fall-material </Game/..kbmat>] [--mist-effect </Game/..kbvfx>]\n"
    "                 (makes the river with 2 or more points, or inserts points into it; x,y,z are local to the entity, y the water\n"
    "                 surface, width bank to bank, depth to the bed, speed of the current; default 4, 1, 1.5)\n"
    "             move --name <Entity> --index <n> --to \"x,y,z[,width,depth,speed]\"   (3 numbers keep width, depth and speed)\n"
    "             remove --name <Entity> (--index <n> | --all)      list [--name <Entity>]   (points, waterfalls, length)\n"
    "             carve --project <dir> --name <Entity> --terrain <file>   (cuts the river's bed and banks into the terrain that the\n"
    "                 scene draws from that file: deepest in the middle, eased to the water level over the bank width)\n";

[[nodiscard]] std::filesystem::path Utf8Path(std::string_view text) {
    return kb::assets::Utf8Path(text);
}

template <typename T>
[[nodiscard]] std::optional<T> ParseValue(std::string_view text) {
    T value{};
    const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (code != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(value)) return std::nullopt;
    }
    return value;
}

// "512" or "512x256".
[[nodiscard]] std::optional<std::pair<float, float>> ParseWorldSize(std::string_view text) {
    const std::size_t split = text.find_first_of("xX");
    const std::optional<float> width = ParseValue<float>(text.substr(0U, split));
    const std::optional<float> depth = split == std::string_view::npos ? width : ParseValue<float>(text.substr(split + 1U));
    if (!width || !depth || *width <= 0.0F || *depth <= 0.0F || *width > 1.0e6F || *depth > 1.0e6F) return std::nullopt;
    return std::pair{ *width, *depth };
}

struct Session {
    std::filesystem::path projectRoot; // empty without --project
    std::filesystem::path file;
    kb::assets::TerrainAsset terrain;
};

// A misspelled option must fail, not be ignored: the lists name the options without the dashes.
template <typename... Lists>
[[nodiscard]] bool CheckOptions(const ArgumentList& arguments, CommandIo io, const Lists&... lists) {
    std::vector<std::string> names{ "--project", "--help", "--dry-run", "--command-timeout-ms", "--command-cancel-file" };
    (..., [&names](const auto& keys) { for (const std::string_view key : keys) names.push_back("--" + std::string{ key }); }(lists));
    const std::vector<std::string_view> known{ names.begin(), names.end() };
    const std::vector<std::string> unknown = arguments.UnknownOptions(known);
    if (unknown.empty()) return true;
    io.err << "error: terrain has no option '" << unknown.front() << "' for this action; kb_cli help terrain lists them\n";
    return false;
}

[[nodiscard]] std::optional<std::filesystem::path> ProjectRoot(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) return std::filesystem::path{};
    std::string error;
    const std::filesystem::path projectFile = kb::project::ProjectManager::FindProjectFile(Utf8Path(*project), error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return std::nullopt;
    }
    return projectFile.parent_path();
}

// /Game/..., Assets/... or a file path; with a project the file must lie inside it.
[[nodiscard]] std::filesystem::path ResolveFile(const std::filesystem::path& projectRoot, std::string_view text, CommandIo io) {
    if (text.starts_with("/Game/")) {
        if (projectRoot.empty()) {
            io.err << "error: " << text << " is a project path; pass --project <dir>\n";
            return {};
        }
        return (projectRoot / "Assets" / Utf8Path(text.substr(6U))).lexically_normal();
    }
    std::string error;
    const std::filesystem::path file = ResolveProjectFile(Utf8Path(text), projectRoot, error);
    if (file.empty()) io.err << "error: " << error << '\n';
    return file;
}

[[nodiscard]] std::optional<Session> OpenTerrain(const ArgumentList& arguments, CommandIo io) {
    Session session{};
    const std::optional<std::filesystem::path> root = ProjectRoot(arguments, io);
    if (!root.has_value()) return std::nullopt;
    session.projectRoot = *root;
    const std::optional<std::string> terrain = arguments.Option("--terrain");
    if (!terrain.has_value()) {
        io.err << "error: --terrain <file> is required (e.g. /Game/Terrains/Hills.kbterrain)\n";
        return std::nullopt;
    }
    session.file = ResolveFile(session.projectRoot, *terrain, io);
    if (session.file.empty()) return std::nullopt;
    std::string error;
    std::optional<kb::assets::TerrainAsset> loaded = kb::assets::TerrainAssetIO::Load(session.file, &error);
    if (!loaded.has_value()) {
        io.err << "error: " << session.file.generic_string() << ": " << error << '\n';
        return std::nullopt;
    }
    session.terrain = std::move(*loaded);
    return session;
}

[[nodiscard]] std::string Hex(std::uint64_t value) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    return buffer;
}

[[nodiscard]] int SaveTerrain(const Session& session, std::string_view what, CommandIo io) {
    std::string error;
    if (!kb::assets::IsTerrainAssetValid(session.terrain, &error) ||
        !kb::assets::TerrainAssetIO::Save(session.file, session.terrain, &error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    io.out << what << " (" << session.file.generic_string() << ", heightHash " << Hex(kb::terrain_editor::HashTerrainHeights(session.terrain)) << ")\n";
    return 0;
}

// ---- new ----------------------------------------------------------------------------------------

[[nodiscard]] int RunSubcommand(int (*run)(const ArgumentList&, CommandIo), std::vector<std::string> words, CommandIo io) {
    constexpr std::array<std::string_view, 0> kNoFlags{};
    const ArgumentList arguments{ words, kNoFlags };
    return run(arguments, io);
}

// A number option into `value` when given; false, with the error printed, when it is not a number.
[[nodiscard]] bool ReadFloat(const ArgumentList& arguments, std::string_view name, float& value, CommandIo io) {
    const std::optional<std::string> text = arguments.Option("--" + std::string{ name });
    if (!text.has_value()) return true;
    const std::optional<float> parsed = ParseValue<float>(*text);
    if (!parsed.has_value()) io.err << "error: --" << name << " takes a number\n";
    else value = *parsed;
    return parsed.has_value();
}

[[nodiscard]] bool ReadWhole(const ArgumentList& arguments, std::string_view name, std::uint32_t& value, CommandIo io) {
    const std::optional<std::string> text = arguments.Option("--" + std::string{ name });
    if (!text.has_value()) return true;
    const std::optional<std::uint32_t> parsed = ParseValue<std::uint32_t>(*text);
    if (!parsed.has_value()) io.err << "error: --" << name << " takes a whole number\n";
    else value = *parsed;
    return parsed.has_value();
}

// Runs a generator or an erosion on a worker thread over a copy of the terrain, as the editor does,
// and waits for it: --threads caps the cores it uses and --max-seconds stops it (nothing is saved then).
[[nodiscard]] std::optional<kb::assets::TerrainAsset> RunJob(
    kb::assets::TerrainAsset terrain, TerrainBackgroundTask::Work work, const ArgumentList& arguments, CommandIo io) {
    std::uint32_t threads = 0U;
    float seconds = 0.0F;
    if (!ReadWhole(arguments, "threads", threads, io) || !ReadFloat(arguments, "max-seconds", seconds, io)) return std::nullopt;
    if (seconds < 0.0F) {
        io.err << "error: --max-seconds takes a positive number\n";
        return std::nullopt;
    }
    TerrainBackgroundTask task{ std::move(terrain), [threads, work = std::move(work)](
        kb::assets::TerrainAsset& target, TerrainTaskControl& control, std::string& error) {
        control.maxWorkers = threads;
        return work(target, control, error);
    } };
    std::string error;
    std::optional<kb::assets::TerrainAsset> result = task.Wait(seconds, &error);
    if (!result.has_value()) io.err << "error: " << error << '\n';
    return result;
}

[[nodiscard]] int TerrainNew(const ArgumentList& arguments, CommandIo io) {
    constexpr std::string_view allowed[]{ "name", "terrain", "scene", "position", "resolution", "size", "chunk-quads", "lod-count", "preset", "seed", "character", "threads", "max-seconds" };
    if (!CheckOptions(arguments, io, allowed)) return 1;
    const std::optional<std::string> name = arguments.Option("--name");
    if (!name.has_value() && !arguments.Option("--terrain").has_value()) {
        io.err << "error: terrain new needs --name <Entity> (a terrain in the scene) or --terrain <file>\n";
        return 1;
    }
    kb::assets::TerrainAsset terrain{};
    const std::optional<std::uint32_t> resolution = ParseValue<std::uint32_t>(arguments.Option("--resolution").value_or("129"));
    const std::optional<std::pair<float, float>> size = ParseWorldSize(arguments.Option("--size").value_or("128"));
    if (!resolution.has_value() || !kb::assets::IsTerrainResolutionValid(*resolution)) {
        io.err << "error: --resolution must be 2^n+1 from 17 to 2049 (17 33 65 129 257 513 1025 2049)\n";
        return 1;
    }
    if (!size.has_value()) {
        io.err << "error: --size takes the world size in units, 128 or 256x128 (width x depth)\n";
        return 1;
    }
    terrain = kb::assets::MakeFlatTerrainAsset(*resolution, size->first, size->second);
    if (const auto chunk = arguments.Option("--chunk-quads")) {
        const auto parsed = ParseValue<std::uint32_t>(*chunk);
        if (!parsed.has_value()) {
            io.err << "error: --chunk-quads takes 8, 16, 32, 64 or 128\n";
            return 1;
        }
        terrain.chunkQuads = *parsed;
    }
    if (const auto lods = arguments.Option("--lod-count")) {
        const auto parsed = ParseValue<std::uint32_t>(*lods);
        if (!parsed.has_value()) {
            io.err << "error: --lod-count takes 1 to 8\n";
            return 1;
        }
        terrain.lodCount = *parsed;
    }
    std::string error;
    if (!kb::assets::IsTerrainAssetValid(terrain, &error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    const std::string preset = arguments.Option("--preset").value_or("flat");
    std::uint32_t seed = 1U;
    float character = kb::terrain_editor::kTerrainDefaultCharacter;
    if (!ReadWhole(arguments, "seed", seed, io) || !ReadFloat(arguments, "character", character, io)) return 1;
    if (!kb::terrain_editor::IsTerrainPreset(preset)) {
        io.err << "error: unknown preset '" << preset << "'; the presets are";
        for (const kb::terrain_editor::TerrainPresetInfo& info : kb::terrain_editor::TerrainPresets()) io.err << ' ' << info.name;
        io.err << '\n';
        return 1;
    }
    const std::optional<std::filesystem::path> root = ProjectRoot(arguments, io);
    if (!root.has_value()) return 1;
    if (name.has_value() && root->empty()) {
        io.err << "error: placing the terrain in a scene needs --project <dir>\n";
        return 1;
    }
    const std::string fileText = arguments.Option("--terrain").has_value()
        ? *arguments.Option("--terrain")
        : "/Game/Terrains/" + *name + std::string{ kb::assets::kTerrainAssetExtension };
    const std::filesystem::path file = ResolveFile(*root, fileText, io);
    if (file.empty()) return 1;
    std::error_code fileError;
    if (file.extension() != kb::assets::kTerrainAssetExtension) {
        io.err << "error: a terrain file ends in " << kb::assets::kTerrainAssetExtension << '\n';
        return 1;
    }
    if (std::filesystem::exists(file, fileError)) {
        io.err << "error: " << file.generic_string() << " already exists; pass another --terrain or edit it with terrain brush\n";
        return 1;
    }
    if (preset != "flat") {
        std::optional<kb::assets::TerrainAsset> made = RunJob(std::move(terrain), [preset, seed, character](
            kb::assets::TerrainAsset& target, TerrainTaskControl& control, std::string& failure) {
            return kb::terrain_editor::ApplyTerrainPreset(target, preset, seed, character, control, failure);
        }, arguments, io);
        if (!made.has_value()) return 1;
        terrain = std::move(*made);
    }
    if (!kb::assets::TerrainAssetIO::Save(file, terrain, &error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    io.out << "created " << file.generic_string() << " (" << terrain.width << 'x' << terrain.height << " samples, "
           << FormatCliNumber(terrain.worldSizeX) << 'x' << FormatCliNumber(terrain.worldSizeZ) << " units, " << preset
           << (preset == "flat" ? "" : ", seed " + std::to_string(seed)) << ", heightHash "
           << Hex(kb::terrain_editor::HashTerrainHeights(terrain)) << ")\n";
    if (!name.has_value()) return 0;

    // The scene entity the editor's Terrain component makes: an entity whose Mesh Renderer draws the terrain.
    const std::string virtualPath = "/Game/" + Utf8Path(file.lexically_relative(*root / "Assets").generic_string()).generic_string();
    std::vector<std::string> scene{ "--project", root->string() };
    if (const auto sceneOption = arguments.Option("--scene")) scene.insert(scene.end(), { "--scene", *sceneOption });
    std::vector<std::string> add{ "add" };
    add.insert(add.end(), scene.begin(), scene.end());
    add.insert(add.end(), { "--name", *name, "--create", "Empty" });
    if (const auto position = arguments.Option("--position")) add.insert(add.end(), { "--position", *position });
    std::vector<std::string> attach{ "add" };
    attach.insert(attach.end(), scene.begin(), scene.end());
    attach.insert(attach.end(), { "--entity", *name, "--type", "MeshRenderer", "--set", "mesh=" + virtualPath });
    std::vector<std::string> remove{ "remove" };
    remove.insert(remove.end(), scene.begin(), scene.end());
    remove.insert(remove.end(), { "--name", *name });
    if (RunSubcommand(&RunEntityCommand, std::move(add), io) != 0) {
        std::filesystem::remove(file, fileError);
        return 1;
    }
    if (RunSubcommand(&RunComponentCommand, std::move(attach), io) != 0) {
        static_cast<void>(RunSubcommand(&RunEntityCommand, std::move(remove), io));
        std::filesystem::remove(file, fileError);
        return 1;
    }
    return 0;
}

// ---- info / sample --------------------------------------------------------------------------------

[[nodiscard]] std::string LayerMaterialText(const Session& session, std::uint64_t materialId) {
    if (materialId == 0U || session.projectRoot.empty()) return std::to_string(materialId);
    kb::scene::Scene scene;
    std::string error;
    if (!MountProjectAssets(scene, session.projectRoot, error)) return std::to_string(materialId);
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ materialId });
    return metadata != nullptr ? metadata->virtualPath.generic_string() : std::to_string(materialId);
}

[[nodiscard]] int TerrainInfo(const ArgumentList& arguments, CommandIo io) {
    constexpr std::string_view allowed[]{ "terrain" };
    if (!CheckOptions(arguments, io, allowed)) return 1;
    const std::optional<Session> session = OpenTerrain(arguments, io);
    if (!session.has_value()) return 1;
    const kb::assets::TerrainAsset& terrain = session->terrain;
    const kb::terrain_editor::TerrainStats stats = kb::terrain_editor::ComputeTerrainStats(terrain);
    io.out << "terrain " << session->file.generic_string() << '\n'
           << "samples " << terrain.width << 'x' << terrain.height << ", cell "
           << FormatCliNumber(terrain.worldSizeX / static_cast<float>(terrain.width - 1U)) << 'x'
           << FormatCliNumber(terrain.worldSizeZ / static_cast<float>(terrain.height - 1U)) << " units, chunks of "
           << terrain.chunkQuads << " quads, " << terrain.lodCount << " levels of detail\n"
           << "world size " << FormatCliNumber(terrain.worldSizeX) << 'x' << FormatCliNumber(terrain.worldSizeZ)
           << " units; x, z run from " << FormatCliNumber(-terrain.worldSizeX * 0.5F) << ".." << FormatCliNumber(terrain.worldSizeX * 0.5F)
           << " and " << FormatCliNumber(-terrain.worldSizeZ * 0.5F) << ".." << FormatCliNumber(terrain.worldSizeZ * 0.5F) << '\n'
           << "height min " << FormatCliNumber(stats.minHeight) << " max " << FormatCliNumber(stats.maxHeight) << " mean "
           << FormatCliNumber(stats.meanHeight) << '\n'
           << "holes " << stats.holeCells << " cells\n"
           << "slopes (triangles per 10 degrees, 0-90):";
    for (const std::uint32_t count : stats.slopeHistogram) io.out << ' ' << count;
    io.out << "\nlayers " << terrain.materialLayers.size();
    if (!terrain.materialLayers.empty()) io.out << " (weight map " << terrain.layerWeightWidth << 'x' << terrain.layerWeightHeight << ')';
    io.out << '\n';
    for (std::size_t index = 0U; index < terrain.materialLayers.size(); ++index) {
        io.out << "  layer " << index << ": " << LayerMaterialText(*session, terrain.materialLayers[index].materialAssetId) << '\n';
    }
    io.out << "heightHash " << Hex(stats.heightHash) << '\n';
    return 0;
}

[[nodiscard]] int TerrainSample(const ArgumentList& arguments, CommandIo io) {
    constexpr std::string_view allowed[]{ "terrain", "x", "z" };
    if (!CheckOptions(arguments, io, allowed)) return 1;
    const std::optional<float> x = ParseValue<float>(arguments.Option("--x").value_or(""));
    const std::optional<float> z = ParseValue<float>(arguments.Option("--z").value_or(""));
    if (!x.has_value() || !z.has_value()) {
        io.err << "error: sample needs --x <n> and --z <n> (world units from the terrain's centre)\n";
        return 1;
    }
    const std::optional<Session> session = OpenTerrain(arguments, io);
    if (!session.has_value()) return 1;
    const kb::assets::TerrainAsset& terrain = session->terrain;
    if (std::abs(*x) > terrain.worldSizeX * 0.5F || std::abs(*z) > terrain.worldSizeZ * 0.5F) {
        io.err << "error: (" << FormatCliNumber(*x) << ", " << FormatCliNumber(*z) << ") is outside the terrain, x +-"
               << FormatCliNumber(terrain.worldSizeX * 0.5F) << " z +-" << FormatCliNumber(terrain.worldSizeZ * 0.5F) << '\n';
        return 1;
    }
    const kb::terrain_editor::TerrainSurfaceSample sample = kb::terrain_editor::SampleTerrainSurface(terrain, *x, *z);
    io.out << "height " << FormatCliNumber(sample.height) << '\n'
           << "normal " << FormatCliNumber(sample.normal[0]) << ',' << FormatCliNumber(sample.normal[1]) << ',' << FormatCliNumber(sample.normal[2]) << '\n'
           << "slope " << FormatCliNumber(sample.slopeDegrees) << " degrees\n"
           << "hole " << (sample.hole ? "yes" : "no") << '\n';
    if (terrain.materialLayers.empty()) {
        io.out << "weights none (no layers)\n";
        return 0;
    }
    io.out << "weights";
    for (std::size_t layer = 0U; layer < terrain.materialLayers.size(); ++layer) io.out << ' ' << FormatCliNumber(sample.layerWeights[layer]);
    io.out << '\n';
    return 0;
}

// ---- brush / apply / paint ------------------------------------------------------------------------

// A command line option and the operation member it fills.
struct OptionKey {
    std::string_view option;
    std::string_view key;
};

constexpr OptionKey kBrushOptions[]{ { "mode", "mode" }, { "shape", "shape" }, { "x", "x" }, { "z", "z" }, { "path", "path" },
    { "radius", "radius" }, { "strength", "strength" }, { "falloff", "falloff" }, { "height", "height" }, { "step", "step" },
    { "softness", "softness" }, { "seed", "seed" }, { "spacing", "spacing" }, { "invert", "invert" }, { "symmetry", "symmetry" },
    { "noise-kind", "noiseKind" }, { "noise-scale", "noiseScale" }, { "noise-octaves", "noiseOctaves" }, { "to", "to" },
    { "to-height", "toHeight" }, { "image", "image" }, { "rotation", "rotation" }, { "blend", "blend" } };
constexpr OptionKey kPaintOptions[]{ { "layer", "layer" }, { "x", "x" }, { "z", "z" }, { "path", "path" }, { "radius", "radius" },
    { "opacity", "opacity" }, { "falloff", "falloff" }, { "shape", "shape" }, { "erase", "erase" }, { "seed", "seed" } };

[[nodiscard]] std::vector<std::string_view> OptionNames(std::span<const OptionKey> options) {
    std::vector<std::string_view> names;
    for (const OptionKey& option : options) names.push_back(option.option);
    return names;
}

// The options an operation takes, as the text members of an operation object.
[[nodiscard]] kb::core::JsonValue OperationFromOptions(std::string_view kind, const ArgumentList& arguments, std::span<const OptionKey> options) {
    kb::core::JsonValue operation = kb::core::JsonValue::MakeObject();
    operation.Set("op", kb::core::JsonValue::MakeString(std::string{ kind }));
    for (const OptionKey& option : options) {
        if (const auto value = arguments.Option("--" + std::string{ option.option })) {
            operation.Set(std::string{ option.key }, kb::core::JsonValue::MakeString(*value));
        }
    }
    return operation;
}

[[nodiscard]] kb::core::JsonValue OneOperation(kb::core::JsonValue operation) {
    kb::core::JsonValue list = kb::core::JsonValue::MakeArray();
    list.Append(std::move(operation));
    return list;
}

[[nodiscard]] int Apply(Session& session, const kb::core::JsonValue& operations, std::string_view what, CommandIo io) {
    kb::terrain_editor::TerrainOpSummary summary{};
    kb::terrain_editor::TerrainOpContext context{ .baseDirectory = session.projectRoot };
    std::string error;
    if (!kb::terrain_editor::ApplyTerrainOps(session.terrain, operations, context, summary, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    if (summary.changedSamples == 0U && summary.changedTexels == 0U) {
        io.err << "error: nothing changed - the stroke misses the terrain or has no effect there (terrain info shows the extent)\n";
        return 1;
    }
    std::ostringstream text;
    text << what << ": " << summary.operations << (summary.operations == 1U ? " operation, " : " operations, ");
    if (summary.changedSamples != 0U) text << summary.changedSamples << " height/hole samples changed";
    if (summary.changedSamples != 0U && summary.changedTexels != 0U) text << ", ";
    if (summary.changedTexels != 0U) text << summary.changedTexels << " layer texels changed";
    return SaveTerrain(session, text.str(), io);
}

[[nodiscard]] int TerrainBrush(const ArgumentList& arguments, CommandIo io) {
    constexpr std::string_view own[]{ "terrain" };
    if (!CheckOptions(arguments, io, OptionNames(kBrushOptions), own)) return 1;
    std::optional<Session> session = OpenTerrain(arguments, io);
    if (!session.has_value()) return 1;
    const std::string mode = arguments.Option("--mode").value_or("raise");
    return Apply(*session, OneOperation(OperationFromOptions("brush", arguments, kBrushOptions)), "brush " + mode, io);
}

[[nodiscard]] std::optional<std::string> ReadText(const std::filesystem::path& file) {
    std::ifstream input{ file, std::ios::binary };
    if (!input) return std::nullopt;
    return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

[[nodiscard]] int TerrainApply(const ArgumentList& arguments, CommandIo io) {
    constexpr std::string_view own[]{ "terrain", "ops" };
    if (!CheckOptions(arguments, io, own)) return 1;
    const std::optional<std::string> ops = arguments.Option("--ops");
    if (!ops.has_value()) {
        io.err << "error: apply needs --ops <file.json> or inline JSON: [{\"op\":\"brush\",\"mode\":\"raise\",\"x\":0,\"z\":0,\"radius\":8}]\n";
        return 1;
    }
    std::optional<Session> session = OpenTerrain(arguments, io);
    if (!session.has_value()) return 1;
    std::string text = *ops;
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || (text[first] != '[' && text[first] != '{')) {
        const std::filesystem::path file = ResolveInputPath(Utf8Path(*ops), session->projectRoot);
        const std::optional<std::string> content = ReadText(file);
        if (!content.has_value()) {
            io.err << "error: cannot read the operations file " << file.generic_string() << '\n';
            return 1;
        }
        text = *content;
    }
    kb::core::JsonValue operations;
    std::string error;
    if (!kb::core::JsonValue::Parse(text, operations, error)) {
        io.err << "error: the operations are not valid JSON: " << error << '\n';
        return 1;
    }
    return Apply(*session, operations, "apply", io);
}

[[nodiscard]] int TerrainPaint(const ArgumentList& arguments, CommandIo io) {
    constexpr std::string_view own[]{ "terrain", "auto", "slope-blend", "height-blend" };
    if (!CheckOptions(arguments, io, OptionNames(kPaintOptions), own)) return 1;
    std::optional<Session> session = OpenTerrain(arguments, io);
    if (!session.has_value()) return 1;
    const std::optional<std::string> automatic = arguments.Option("--auto");
    if (!automatic.has_value()) return Apply(*session, OneOperation(OperationFromOptions("paint", arguments, kPaintOptions)), "paint", io);
    // --auto slope=35..90,height=-5..40
    kb::core::JsonValue operation = kb::core::JsonValue::MakeObject();
    operation.Set("op", kb::core::JsonValue::MakeString("auto-paint"));
    for (const std::string_view key : { "layer", "opacity" }) {
        if (const auto value = arguments.Option("--" + std::string{ key })) operation.Set(std::string{ key }, kb::core::JsonValue::MakeString(*value));
    }
    if (const auto value = arguments.Option("--slope-blend")) operation.Set("slopeBlend", kb::core::JsonValue::MakeString(*value));
    if (const auto value = arguments.Option("--height-blend")) operation.Set("heightBlend", kb::core::JsonValue::MakeString(*value));
    std::string_view rest = *automatic;
    while (!rest.empty()) {
        const std::size_t comma = rest.find(',');
        const std::string_view part = rest.substr(0U, comma);
        const std::size_t equals = part.find('=');
        const std::string_view key = part.substr(0U, equals);
        if (equals == std::string_view::npos || (key != "slope" && key != "height")) {
            io.err << "error: --auto takes slope=<low>..<high> and/or height=<low>..<high> (slope in degrees)\n";
            return 1;
        }
        operation.Set(std::string{ key }, kb::core::JsonValue::MakeString(std::string{ part.substr(equals + 1U) }));
        if (comma == std::string_view::npos) break;
        rest.remove_prefix(comma + 1U);
    }
    return Apply(*session, OneOperation(std::move(operation)), "auto paint", io);
}

// ---- layers ---------------------------------------------------------------------------------------

[[nodiscard]] std::optional<std::uint64_t> ResolveMaterial(const Session& session, const ArgumentList& arguments, CommandIo io) {
    if (const auto id = arguments.Option("--material-id")) {
        const std::optional<std::uint64_t> parsed = ParseValue<std::uint64_t>(*id);
        if (!parsed.has_value()) io.err << "error: --material-id takes the number terrain info shows\n";
        return parsed;
    }
    const std::optional<std::string> path = arguments.Option("--material");
    if (!path.has_value()) {
        io.err << "error: a layer needs --material </Game/....kbmat> (or --material-id <n>)\n";
        return std::nullopt;
    }
    if (session.projectRoot.empty()) {
        io.err << "error: --material is a project path; pass --project <dir> (or --material-id <n>)\n";
        return std::nullopt;
    }
    kb::scene::Scene scene;
    std::string error;
    if (!MountProjectAssets(scene, session.projectRoot, error)) {
        io.err << "error: " << error << '\n';
        return std::nullopt;
    }
    const kb::assets::AssetMetadata* metadata = FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), Utf8Path(*path));
    const std::string extension = metadata != nullptr ? metadata->physicalPath.extension().string() : std::string{};
    if (metadata == nullptr || (extension != ".kbmat" && extension != ".kbmatinst")) {
        io.err << "error: '" << *path << "' is not a material (.kbmat) of the project (create one with kb_cli material-new)\n";
        return std::nullopt;
    }
    return metadata->id.value;
}

[[nodiscard]] std::optional<std::uint32_t> LayerIndex(const kb::assets::TerrainAsset& terrain, const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::uint32_t> index = ParseValue<std::uint32_t>(arguments.Option("--index").value_or(""));
    if (!index.has_value() || *index >= terrain.materialLayers.size()) {
        io.err << "error: --index must be one of the " << terrain.materialLayers.size() << " layers (0-based; terrain info lists them)\n";
        return std::nullopt;
    }
    return index;
}

[[nodiscard]] int TerrainLayer(const ArgumentList& arguments, CommandIo io) {
    constexpr std::string_view own[]{ "terrain", "material", "material-id", "index" };
    if (!CheckOptions(arguments, io, own)) return 1;
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.size() > 1U ? positionals[1] : std::string{};
    if (action != "list" && action != "add" && action != "set" && action != "remove") {
        io.err << "error: layer takes list, add, set or remove\n";
        return 1;
    }
    std::optional<Session> session = OpenTerrain(arguments, io);
    if (!session.has_value()) return 1;
    kb::assets::TerrainAsset& terrain = session->terrain;
    if (action == "list") {
        io.out << terrain.materialLayers.size() << " layers\n";
        for (std::size_t index = 0U; index < terrain.materialLayers.size(); ++index) {
            io.out << index << ' ' << LayerMaterialText(*session, terrain.materialLayers[index].materialAssetId) << '\n';
        }
        return 0;
    }
    if (action == "remove") {
        const std::optional<std::uint32_t> index = LayerIndex(terrain, arguments, io);
        if (!index.has_value()) return 1;
        static_cast<void>(kb::terrain_editor::RemoveTerrainMaterialLayer(terrain, *index));
        return SaveTerrain(*session, "removed layer " + std::to_string(*index) + " (its weight went to layer 0)", io);
    }
    const std::optional<std::uint64_t> material = ResolveMaterial(*session, arguments, io);
    if (!material.has_value()) return 1;
    if (action == "add") {
        if (!kb::terrain_editor::AddTerrainMaterialLayer(terrain, *material)) {
            io.err << "error: a terrain has at most " << kb::assets::TerrainAsset::MaximumMaterialLayers << " layers\n";
            return 1;
        }
        return SaveTerrain(*session, "added layer " + std::to_string(terrain.materialLayers.size() - 1U), io);
    }
    const std::optional<std::uint32_t> index = LayerIndex(terrain, arguments, io);
    if (!index.has_value()) return 1;
    static_cast<void>(kb::terrain_editor::SetTerrainMaterialLayer(terrain, *index, *material));
    return SaveTerrain(*session, "set layer " + std::to_string(*index), io);
}

// ---- generate / erode ----------------------------------------------------------------------------

[[nodiscard]] int TerrainGenerate(const ArgumentList& arguments, CommandIo io) {
    constexpr std::string_view allowed[]{ "terrain", "kind", "seed", "amplitude", "base", "scale", "octaves", "lacunarity", "gain", "noise-kind",
        "warp", "add", "island-radius", "island-falloff", "plateau-level", "plateau-softness", "valley-width", "valley-wall", "threads", "max-seconds" };
    if (!CheckOptions(arguments, io, allowed)) return 1;
    std::optional<Session> session = OpenTerrain(arguments, io);
    if (!session.has_value()) return 1;
    kb::terrain_editor::TerrainGeneratorSettings settings{};
    // The largest feature a third of the terrain, unless told.
    settings.scale = std::max(session->terrain.worldSizeX, session->terrain.worldSizeZ) / 3.0F;
    if (const auto kind = arguments.Option("--kind")) {
        const auto parsed = kb::terrain_editor::ParseTerrainGeneratorKind(*kind);
        if (!parsed.has_value()) {
            io.err << "error: --kind is fbm, ridged, island, plateau or valley\n";
            return 1;
        }
        settings.kind = *parsed;
    }
    if (const auto kind = arguments.Option("--noise-kind")) {
        const auto parsed = kb::terrain_editor::ParseTerrainNoiseKind(*kind);
        if (!parsed.has_value()) {
            io.err << "error: --noise-kind is gradient or value\n";
            return 1;
        }
        settings.noiseKind = *parsed;
    }
    std::uint32_t octaves = settings.octaves;
    if (!ReadWhole(arguments, "seed", settings.seed, io) || !ReadWhole(arguments, "octaves", octaves, io) ||
        !ReadFloat(arguments, "amplitude", settings.amplitude, io) || !ReadFloat(arguments, "base", settings.baseHeight, io) ||
        !ReadFloat(arguments, "scale", settings.scale, io) || !ReadFloat(arguments, "lacunarity", settings.lacunarity, io) ||
        !ReadFloat(arguments, "gain", settings.gain, io) || !ReadFloat(arguments, "warp", settings.warp, io) ||
        !ReadFloat(arguments, "island-radius", settings.islandRadius, io) || !ReadFloat(arguments, "island-falloff", settings.islandFalloff, io) ||
        !ReadFloat(arguments, "plateau-level", settings.plateauLevel, io) || !ReadFloat(arguments, "plateau-softness", settings.plateauSoftness, io) ||
        !ReadFloat(arguments, "valley-width", settings.valleyWidth, io) || !ReadFloat(arguments, "valley-wall", settings.valleyWall, io)) {
        return 1;
    }
    settings.octaves = static_cast<std::uint8_t>(std::min(octaves, 255U));
    settings.additive = arguments.Option("--add").value_or("no") == "yes";
    std::optional<kb::assets::TerrainAsset> made = RunJob(std::move(session->terrain), [settings](
        kb::assets::TerrainAsset& target, TerrainTaskControl& control, std::string& error) {
        return kb::terrain_editor::GenerateTerrain(target, settings, control, error);
    }, arguments, io);
    if (!made.has_value()) return 1;
    session->terrain = std::move(*made);
    return SaveTerrain(*session, "generated " + std::string{ kb::terrain_editor::TerrainGeneratorKindName(settings.kind) } + " (seed " + std::to_string(settings.seed) + ")", io);
}

[[nodiscard]] int TerrainErode(const ArgumentList& arguments, CommandIo io) {
    constexpr std::string_view allowed[]{ "terrain", "mode", "iterations", "rain", "evaporation", "strength", "talus", "threads", "max-seconds" };
    if (!CheckOptions(arguments, io, allowed)) return 1;
    std::optional<Session> session = OpenTerrain(arguments, io);
    if (!session.has_value()) return 1;
    const std::string mode = arguments.Option("--mode").value_or("hydraulic");
    if (mode != "hydraulic" && mode != "thermal") {
        io.err << "error: --mode is hydraulic (water: channels and fans) or thermal (slumping to a talus angle)\n";
        return 1;
    }
    kb::terrain_editor::TerrainHydraulicSettings hydraulic{};
    kb::terrain_editor::TerrainThermalSettings thermal{};
    std::uint32_t iterations = mode == "thermal" ? thermal.iterations : hydraulic.iterations;
    if (!ReadWhole(arguments, "iterations", iterations, io)) return 1;
    const bool thermalMode = mode == "thermal";
    if (thermalMode) {
        thermal.iterations = iterations;
        if (!ReadFloat(arguments, "talus", thermal.talusDegrees, io) || !ReadFloat(arguments, "strength", thermal.strength, io)) return 1;
        if (arguments.Option("--rain") || arguments.Option("--evaporation")) {
            io.err << "error: --rain and --evaporation belong to --mode hydraulic\n";
            return 1;
        }
    } else {
        hydraulic.iterations = iterations;
        if (!ReadFloat(arguments, "rain", hydraulic.rain, io) || !ReadFloat(arguments, "evaporation", hydraulic.evaporation, io) ||
            !ReadFloat(arguments, "strength", hydraulic.strength, io)) {
            return 1;
        }
        if (arguments.Option("--talus")) {
            io.err << "error: --talus belongs to --mode thermal\n";
            return 1;
        }
    }
    std::optional<kb::assets::TerrainAsset> eroded = RunJob(std::move(session->terrain), [thermalMode, thermal, hydraulic](
        kb::assets::TerrainAsset& target, TerrainTaskControl& control, std::string& error) {
        return thermalMode ? kb::terrain_editor::ErodeTerrainThermal(target, thermal, control, error)
                           : kb::terrain_editor::ErodeTerrainHydraulic(target, hydraulic, control, error);
    }, arguments, io);
    if (!eroded.has_value()) return 1;
    session->terrain = std::move(*eroded);
    return SaveTerrain(*session, mode + " erosion, " + std::to_string(iterations) + " iterations", io);
}

// ---- import / export / preview --------------------------------------------------------------------

[[nodiscard]] int TerrainImport(const ArgumentList& arguments, CommandIo io) {
    constexpr std::string_view own[]{ "terrain", "source", "min", "max", "size", "flip", "replace" };
    if (!CheckOptions(arguments, io, own)) return 1;
    const std::optional<std::string> source = arguments.Option("--source");
    if (!source.has_value()) {
        io.err << "error: import needs --source <heightmap.png|.raw|.r16> (16 bit; 8 bit RAW also works)\n";
        return 1;
    }
    const std::optional<std::filesystem::path> root = ProjectRoot(arguments, io);
    if (!root.has_value()) return 1;
    const std::optional<std::string> target = arguments.Option("--terrain");
    if (!target.has_value()) {
        io.err << "error: --terrain <file> is required (the terrain to create)\n";
        return 1;
    }
    const std::filesystem::path file = ResolveFile(*root, *target, io);
    if (file.empty()) return 1;
    std::error_code fileError;
    if (std::filesystem::exists(file, fileError) && !arguments.Flag("--replace")) {
        io.err << "error: " << file.generic_string() << " already exists; add --replace to overwrite it\n";
        return 1;
    }
    kb::terrain_editor::TerrainHeightmapImportSettings settings{};
    if (const auto value = arguments.Option("--min")) {
        const auto parsed = ParseValue<float>(*value);
        if (!parsed.has_value()) { io.err << "error: --min takes a height\n"; return 1; }
        settings.minimumHeight = *parsed;
    }
    if (const auto value = arguments.Option("--max")) {
        const auto parsed = ParseValue<float>(*value);
        if (!parsed.has_value()) { io.err << "error: --max takes a height\n"; return 1; }
        settings.maximumHeight = *parsed;
    }
    settings.flipVertically = arguments.Option("--flip").value_or("no") == "yes";
    std::string error;
    std::optional<kb::assets::TerrainAsset> terrain = kb::terrain_editor::TerrainHeightmapImporter::Import(
        ResolveInputPath(Utf8Path(*source), *root), settings, &error);
    if (!terrain.has_value()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    if (const auto size = arguments.Option("--size")) {
        const std::optional<std::pair<float, float>> parsed = ParseWorldSize(*size);
        if (!parsed.has_value()) { io.err << "error: --size takes the world size in units, 512 or 512x256\n"; return 1; }
        terrain->worldSizeX = parsed->first;
        terrain->worldSizeZ = parsed->second;
    }
    Session session{ .projectRoot = *root, .file = file, .terrain = std::move(*terrain) };
    return SaveTerrain(session, "imported " + std::to_string(session.terrain.width) + "x" + std::to_string(session.terrain.height) + " samples", io);
}

[[nodiscard]] std::optional<std::filesystem::path> OutputFile(const Session& session, const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> out = arguments.Option("--out");
    if (!out.has_value()) {
        io.err << "error: --out <file> is required\n";
        return std::nullopt;
    }
    std::filesystem::path file = ResolveFile(session.projectRoot, *out, io);
    if (file.empty()) return std::nullopt;
    return file;
}

[[nodiscard]] int TerrainExport(const ArgumentList& arguments, CommandIo io) {
    constexpr std::string_view own[]{ "terrain", "out", "min", "max" };
    if (!CheckOptions(arguments, io, own)) return 1;
    const std::optional<Session> session = OpenTerrain(arguments, io);
    if (!session.has_value()) return 1;
    const std::optional<std::filesystem::path> file = OutputFile(*session, arguments, io);
    if (!file.has_value()) return 1;
    const std::optional<kb::terrain_editor::TerrainHeightFormat> format = kb::terrain_editor::TerrainHeightFormatFromPath(*file);
    if (!format.has_value()) {
        io.err << "error: --out ends in .png (16 bit), .raw or .r16 (little-endian 16 bit)\n";
        return 1;
    }
    std::optional<std::pair<float, float>> range;
    if (arguments.Option("--min").has_value() != arguments.Option("--max").has_value()) {
        io.err << "error: pass --min and --max together\n";
        return 1;
    }
    if (arguments.Option("--min").has_value()) {
        const auto low = ParseValue<float>(*arguments.Option("--min"));
        const auto high = ParseValue<float>(*arguments.Option("--max"));
        if (!low.has_value() || !high.has_value()) {
            io.err << "error: --min and --max take heights\n";
            return 1;
        }
        range = std::pair{ *low, *high };
    }
    std::string error;
    const std::optional<kb::terrain_editor::TerrainHeightExport> exported =
        kb::terrain_editor::ExportTerrainHeightmap(session->terrain, *file, *format, range, &error);
    if (!exported.has_value()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    io.out << "exported " << exported->width << 'x' << exported->height << " to " << file->generic_string() << "; import it with --min "
           << FormatCliNumber(exported->minHeight) << " --max " << FormatCliNumber(exported->maxHeight) << '\n';
    return 0;
}

[[nodiscard]] int TerrainPreview(const ArgumentList& arguments, CommandIo io) {
    constexpr std::string_view own[]{ "terrain", "out", "kind", "pixels" };
    if (!CheckOptions(arguments, io, own)) return 1;
    const std::optional<Session> session = OpenTerrain(arguments, io);
    if (!session.has_value()) return 1;
    const std::optional<std::filesystem::path> file = OutputFile(*session, arguments, io);
    if (!file.has_value()) return 1;
    const std::optional<kb::terrain_editor::TerrainPreviewKind> kind =
        kb::terrain_editor::ParseTerrainPreviewKind(arguments.Option("--kind").value_or("height"));
    const std::optional<std::uint32_t> pixels = ParseValue<std::uint32_t>(arguments.Option("--pixels").value_or("512"));
    if (!kind.has_value() || !pixels.has_value()) {
        io.err << "error: --kind is height, slope or splat; --pixels is the image size (16..4096)\n";
        return 1;
    }
    std::string error;
    if (!kb::terrain_editor::WriteTerrainPreviewPng(session->terrain, *kind, *pixels, *file, &error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    io.out << "preview " << file->generic_string() << '\n';
    return 0;
}

// ---- river ----------------------------------------------------------------------------------------

[[nodiscard]] int TerrainRiverCarve(const ArgumentList& arguments, CommandIo io) {
    constexpr std::string_view own[]{ "terrain", "scene", "name" };
    if (!CheckOptions(arguments, io, own)) return 1;
    std::optional<Session> session = OpenTerrain(arguments, io);
    if (!session.has_value()) return 1;
    const std::optional<RiverCarveSource> source = LoadRiverForCarve(arguments, session->file, io);
    if (!source.has_value()) return 1;
    kb::assets::TerrainAsset& terrain = session->terrain;
    // Stations as close as a cell of the terrain (at most 2 units), so that the cut follows the curve.
    const float cell = std::min(terrain.worldSizeX / static_cast<float>(terrain.width - 1U), terrain.worldSizeZ / static_cast<float>(terrain.height - 1U));
    const std::vector<kb::terrain_editor::TerrainChannelNode> nodes = kb::terrain_editor::RiverChannelNodes(
        source->river, source->riverPlacement, source->terrainPlacement, std::clamp(cell, 0.25F, 2.0F));
    const kb::terrain_editor::TerrainChannelSettings settings{ .bankWidth = source->river.bankWidth };
    if (!kb::terrain_editor::IsTerrainChannelValid(nodes, settings)) {
        io.err << "error: river '" << source->name << "' is not a valid channel (check terrain river list)\n";
        return 1;
    }
    const kb::terrain_editor::TerrainBrushResult carved = kb::terrain_editor::CarveTerrainChannel(terrain, nodes, settings);
    if (!carved.Changed()) {
        io.err << "error: nothing changed - the river lies outside the terrain (x +-" << FormatCliNumber(terrain.worldSizeX * 0.5F) << " z +-"
               << FormatCliNumber(terrain.worldSizeZ * 0.5F) << " from its centre) or the ground is cut there already\n";
        return 1;
    }
    return SaveTerrain(*session, "carved river '" + source->name + "': " + std::to_string(carved.changedSamples) + " height samples changed", io);
}

[[nodiscard]] int TerrainRiver(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.size() > 1U ? positionals[1] : std::string{};
    if (action == "carve") return TerrainRiverCarve(arguments, io);
    return RunRiverSceneCommand(action, arguments, io);
}

} // namespace

int RunTerrainCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.empty() ? std::string{} : positionals.front();
    if (action == "new") return TerrainNew(arguments, io);
    if (action == "info") return TerrainInfo(arguments, io);
    if (action == "sample") return TerrainSample(arguments, io);
    if (action == "brush") return TerrainBrush(arguments, io);
    if (action == "apply") return TerrainApply(arguments, io);
    if (action == "layer") return TerrainLayer(arguments, io);
    if (action == "paint") return TerrainPaint(arguments, io);
    if (action == "generate") return TerrainGenerate(arguments, io);
    if (action == "erode") return TerrainErode(arguments, io);
    if (action == "import") return TerrainImport(arguments, io);
    if (action == "export") return TerrainExport(arguments, io);
    if (action == "preview") return TerrainPreview(arguments, io);
    if (action == "river") return TerrainRiver(arguments, io);
    io.err << kUsage;
    return 1;
}

} // namespace kb::cli
