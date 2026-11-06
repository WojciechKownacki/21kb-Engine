#include "CliCommands.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/bake/MeshBaker.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// mesh-lod: how many levels of detail an imported model gets and how far each one reduces it. The settings live
// in the model asset (AssetMeshLodSettings), so the editor's Static Mesh window, the cooker and a game that
// draws the source file all build the same levels from them.
namespace kb::cli {
namespace {

constexpr std::string_view kUsage =
    "usage: kb_cli mesh-lod <show|generate|add|remove|set> --project <dir> --mesh <model asset> ...\n"
    "  show                     the settings and, per level, its triangles, share, error and the size it is drawn from\n"
    "  generate                 Auto mode: every level keeps --keep percent of the one above (default 50) and may add\n"
    "                           --error percent of the mesh's extent (default 20)\n"
    "                           [--levels 1..8 (default 8)] [--keep PCT] [--error PCT] [--min-triangles N]\n"
    "  add                      a coarser level at the end (Manual mode): --triangles PCT [--error PCT]\n"
    "  remove                   the last level, or --level N (1 is the first coarser level)\n"
    "  set                      --level N [--triangles PCT] [--error PCT] for one level (Manual mode), or\n"
    "                           --mode auto|manual and --min-triangles N for the whole chain\n"
    "  --triangles is the share of the finest level's triangles a level keeps; each level keeps fewer than the one above.\n"
    "  A mesh with fewer than --min-triangles triangles (default 1024) gets no chain. --levels 1 turns the chain off.\n"
    "  The model must be an imported one (kb_cli import); the change is stored in the asset and survives a reimport.\n";

struct Session {
    kb::scene::Scene scene;
    const kb::assets::AssetMetadata* metadata = nullptr;
    kb::assets::AssetMeshLodSettings lod{};
};

[[nodiscard]] std::optional<float> ParsePercent(const ArgumentList& arguments, std::string_view name, CommandIo io, bool& bad) {
    const std::optional<std::string> text = arguments.Option(name);
    if (!text.has_value()) return std::nullopt;
    float value = 0.0F;
    const auto [end, code] = std::from_chars(text->data(), text->data() + text->size(), value);
    if (code != std::errc{} || end != text->data() + text->size() || !std::isfinite(value) || value <= 0.0F || value > 100.0F) {
        io.err << "error: " << name << " expects a percent above 0 and up to 100\n";
        bad = true;
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<std::uint32_t> ParseCount(const ArgumentList& arguments, std::string_view name, std::uint32_t maximum, CommandIo io, bool& bad) {
    const std::optional<std::string> text = arguments.Option(name);
    if (!text.has_value()) return std::nullopt;
    std::uint32_t value = 0U;
    const auto [end, code] = std::from_chars(text->data(), text->data() + text->size(), value);
    if (code != std::errc{} || end != text->data() + text->size() || value > maximum) {
        io.err << "error: " << name << " expects a whole number up to " << maximum << '\n';
        bad = true;
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] bool Open(Session& session, const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    const std::optional<std::string> mesh = arguments.Option("--mesh");
    if (!project.has_value() || !mesh.has_value()) {
        io.err << "error: mesh-lod requires --project <dir> and --mesh <model asset>\n" << kUsage;
        return false;
    }
    std::string error;
    if (!MountProjectAssets(session.scene, kb::assets::Utf8Path(*project), error)) {
        io.err << "error: " << error << '\n';
        return false;
    }
    session.metadata = FindAssetByFlexiblePath(session.scene.Assets().Manager().Registry(), kb::assets::Utf8Path(*mesh));
    const std::optional<kb::assets::AssetMeshLodSettings> lod = session.metadata == nullptr || session.metadata->physicalPath.empty()
        ? std::nullopt : kb::assets::AssetImportService::ReadMeshLod(session.metadata->physicalPath);
    if (!lod.has_value()) {
        io.err << "error: '" << *mesh << "' is not an imported model of the project (import one with kb_cli import)\n";
        return false;
    }
    session.lod = *lod;
    return true;
}

// Manual mode that looks exactly as the Auto settings do now, so a change to one level leaves the others where they were.
void ToManual(kb::assets::AssetMeshLodSettings& lod) {
    if (lod.manual) return;
    for (std::uint32_t level = 1U; level < lod.levelCount; ++level) lod.levels[level - 1U] = lod.Level(level);
    lod.manual = true;
}

[[nodiscard]] bool Save(Session& session, CommandIo io) {
    if (!session.lod.IsValid()) {
        io.err << "error: the levels are not valid: 1 to 8 levels, each level keeps a share above 0 and below the level above it, and adds a deviation above 0 up to 100 percent\n";
        return false;
    }
    const std::string failure = kb::assets::AssetImportService::SetMeshLod(session.scene.Assets().Manager(), session.metadata->id, session.lod);
    if (!failure.empty()) {
        io.err << "error: " << failure << '\n';
        return false;
    }
    return true;
}

// The chain the settings build, baked in memory as the cooker bakes it.
[[nodiscard]] bool PrintChain(const Session& session, CommandIo io) {
    kb::render::RenderMeshAssetLoader loader;
    const kb::assets::AssetLoadResult loaded = loader.Load(kb::assets::AssetLoadRequest{ *session.metadata, session.metadata->physicalPath });
    if (!loaded.Succeeded()) {
        io.err << "error: the model could not be read: " << loaded.error << '\n';
        return false;
    }
    const auto source = std::static_pointer_cast<kb::render::RenderMeshAssetData>(loaded.asset);
    const std::size_t indexCount = source->indices32.empty() ? source->indices16.size() : source->indices32.size();
    const std::uint64_t triangles = indexCount / 3U;
    const kb::assets::AssetMeshLodSettings& lod = session.lod;
    io.out << session.metadata->virtualPath.generic_string() << ": " << triangles << " triangles, "
           << (lod.manual ? "manual levels" : "automatic levels") << ", " << static_cast<int>(lod.levelCount) << " asked for";
    if (!lod.manual) io.out << " (each keeps " << lod.keepPercent << "%, adds up to " << lod.errorPercent << "% deviation)";
    io.out << ", chain from " << lod.minTriangles << " triangles\n";
    if (!lod.BuildsChain(triangles)) {
        io.out << "  no chain: " << (lod.levelCount <= 1U ? "one level was asked for" : "the mesh is under the triangle threshold") << '\n';
        return true;
    }
    const kb::render::bake::MeshBakeOutput baked = kb::render::bake::BakeMeshInMemory(*source, kb::assets::bake::WindowsX64BakeTargetProfile());
    kb::render::RenderMeshAssetData mesh{};
    kb::render::bake::BakedMeshLayout layout{};
    if (baked.status != kb::render::bake::MeshBakeStatus::Success || !kb::render::bake::ReadBakedMesh(baked.primaryBlock, baked.chunks, mesh) ||
        !kb::render::bake::ReadBakedMeshLayout(baked.primaryBlock, layout) || layout.lods.size() != mesh.lods.size()) {
        io.err << "error: the levels could not be built (" << kb::render::bake::ToString(baked.status) << ")\n";
        return false;
    }
    std::vector<float> errors;
    for (const kb::render::bake::BakedMeshLodLayout& level : layout.lods) errors.push_back(level.error);
    kb::render::bake::AssignLodScreenCoverage(mesh, errors, layout.bounds.radius);
    std::uint64_t finest = 0U;
    for (std::size_t level = 0U; level < mesh.lods.size(); ++level) {
        std::uint64_t levelIndices = 0U;
        for (std::uint32_t section = 0U; section < mesh.lods[level].sectionCount; ++section) {
            levelIndices += mesh.sections[mesh.lods[level].firstSection + section].indexCount;
        }
        if (level == 0U) finest = std::max<std::uint64_t>(levelIndices, 1U);
        io.out << "  LOD" << level << "  " << std::setw(9) << levelIndices / 3U << " triangles  " << std::fixed << std::setprecision(1)
               << std::setw(5) << 100.0 * static_cast<double>(levelIndices) / static_cast<double>(finest) << "%  error " << std::setprecision(4)
               << layout.lods[level].error << "  ";
        if (level == 0U) io.out << "drawn from the closest";
        else io.out << "drawn from " << std::setprecision(0) << mesh.lods[level - 1U].minScreenCoverage * kb::render::bake::kRuntimeLodReferenceViewportHeight
                    << " px tall at 1080p down";
        io.out << '\n';
    }
    if (mesh.lods.size() < lod.levelCount) {
        io.out << "  (the simplifier stopped at " << mesh.lods.size() << " levels: the next one could not get smaller inside the deviation allowed)\n";
    }
    return true;
}

[[nodiscard]] int Show(const ArgumentList& arguments, CommandIo io) {
    Session session;
    if (!Open(session, arguments, io)) return 1;
    return PrintChain(session, io) ? 0 : 1;
}

[[nodiscard]] int Generate(const ArgumentList& arguments, CommandIo io) {
    Session session;
    if (!Open(session, arguments, io)) return 1;
    bool bad = false;
    const auto levels = ParseCount(arguments, "--levels", kb::assets::kMeshLodMaxLevels, io, bad);
    const auto keep = ParsePercent(arguments, "--keep", io, bad);
    const auto error = ParsePercent(arguments, "--error", io, bad);
    const auto minimum = ParseCount(arguments, "--min-triangles", 100'000'000U, io, bad);
    if (bad) return 1;
    if (levels.has_value() && *levels < 1U) {
        io.err << "error: --levels expects 1 to " << kb::assets::kMeshLodMaxLevels << '\n';
        return 1;
    }
    kb::assets::AssetMeshLodSettings& lod = session.lod;
    lod.manual = false;
    if (levels.has_value()) lod.levelCount = static_cast<std::uint8_t>(*levels);
    if (keep.has_value()) lod.keepPercent = *keep;
    if (error.has_value()) lod.errorPercent = *error;
    if (minimum.has_value()) lod.minTriangles = *minimum;
    if (!Save(session, io)) return 1;
    return PrintChain(session, io) ? 0 : 1;
}

[[nodiscard]] int Add(const ArgumentList& arguments, CommandIo io) {
    Session session;
    if (!Open(session, arguments, io)) return 1;
    bool bad = false;
    const auto triangles = ParsePercent(arguments, "--triangles", io, bad);
    const auto error = ParsePercent(arguments, "--error", io, bad);
    if (bad) return 1;
    kb::assets::AssetMeshLodSettings& lod = session.lod;
    if (lod.levelCount >= kb::assets::kMeshLodMaxLevels) {
        io.err << "error: the mesh already has " << kb::assets::kMeshLodMaxLevels << " levels, the most there can be\n";
        return 1;
    }
    ToManual(lod);
    const float last = lod.levelCount > 1U ? lod.levels[lod.levelCount - 2U].trianglePercent : 100.0F;
    lod.levels[lod.levelCount - 1U] = kb::assets::AssetMeshLodLevel{
        .trianglePercent = triangles.value_or(last * 0.5F), .errorPercent = error.value_or(lod.errorPercent) };
    ++lod.levelCount;
    if (!Save(session, io)) return 1;
    return PrintChain(session, io) ? 0 : 1;
}

[[nodiscard]] int Remove(const ArgumentList& arguments, CommandIo io) {
    Session session;
    if (!Open(session, arguments, io)) return 1;
    bool bad = false;
    const auto level = ParseCount(arguments, "--level", kb::assets::kMeshLodMaxLevels, io, bad);
    if (bad) return 1;
    kb::assets::AssetMeshLodSettings& lod = session.lod;
    const std::uint32_t removed = level.value_or(lod.levelCount - 1U);
    if (removed < 1U || removed >= lod.levelCount) {
        io.err << "error: the mesh has " << static_cast<int>(lod.levelCount) - 1 << " coarser levels; --level expects 1 to " << static_cast<int>(lod.levelCount) - 1 << '\n';
        return 1;
    }
    ToManual(lod);
    for (std::uint32_t index = removed; index + 1U < lod.levelCount; ++index) lod.levels[index - 1U] = lod.levels[index];
    --lod.levelCount;
    if (!Save(session, io)) return 1;
    return PrintChain(session, io) ? 0 : 1;
}

[[nodiscard]] int Set(const ArgumentList& arguments, CommandIo io) {
    Session session;
    if (!Open(session, arguments, io)) return 1;
    bool bad = false;
    const auto level = ParseCount(arguments, "--level", kb::assets::kMeshLodMaxLevels, io, bad);
    const auto triangles = ParsePercent(arguments, "--triangles", io, bad);
    const auto error = ParsePercent(arguments, "--error", io, bad);
    const auto minimum = ParseCount(arguments, "--min-triangles", 100'000'000U, io, bad);
    if (bad) return 1;
    kb::assets::AssetMeshLodSettings& lod = session.lod;
    if (const std::optional<std::string> mode = arguments.Option("--mode"); mode.has_value()) {
        if (*mode == "manual") ToManual(lod);
        else if (*mode == "auto") lod.manual = false;
        else {
            io.err << "error: --mode expects auto or manual\n";
            return 1;
        }
    }
    if (minimum.has_value()) lod.minTriangles = *minimum;
    if (level.has_value()) {
        if (*level < 1U || *level >= lod.levelCount) {
            io.err << "error: the mesh has " << static_cast<int>(lod.levelCount) - 1 << " coarser levels; --level expects 1 to " << static_cast<int>(lod.levelCount) - 1
                   << " (add one with: mesh-lod add)\n";
            return 1;
        }
        if (!triangles.has_value() && !error.has_value()) {
            io.err << "error: --level needs --triangles and/or --error\n";
            return 1;
        }
        ToManual(lod);
        kb::assets::AssetMeshLodLevel& entry = lod.levels[*level - 1U];
        if (triangles.has_value()) entry.trianglePercent = *triangles;
        if (error.has_value()) entry.errorPercent = *error;
    } else if (triangles.has_value() || error.has_value()) {
        io.err << "error: --triangles and --error name a level: add --level N (the whole chain: generate --keep --error)\n";
        return 1;
    }
    if (!Save(session, io)) return 1;
    return PrintChain(session, io) ? 0 : 1;
}

} // namespace

int RunMeshLodCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.empty() ? std::string{} : positionals[0];
    if (action == "show") return Show(arguments, io);
    if (action == "generate") return Generate(arguments, io);
    if (action == "add") return Add(arguments, io);
    if (action == "remove") return Remove(arguments, io);
    if (action == "set") return Set(arguments, io);
    io.err << kUsage;
    return 1;
}

} // namespace kb::cli
