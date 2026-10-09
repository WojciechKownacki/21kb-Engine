#include "engine/world/WorldCellBuilder.hpp"

#include "engine/navigation/NavGeometryCollector.hpp"
#include "engine/navigation/NavMeshAsset.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/world/WorldDescriptor.hpp"
#include "engine/world/WorldObjectFile.hpp"
#include "world/WorldTextFormat.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <tuple>
#include <unordered_map>

namespace kb::world {
namespace {

using Affine = std::array<double, 12U>;

struct UnitKey {
    bool persistent = false;
    WorldCellCoord coord{};
    std::string layer;

    friend auto operator<=>(const UnitKey&, const UnitKey&) = default;
};

[[nodiscard]] WorldBuildResult Failure(std::string error) {
    return { .succeeded = false, .report = {}, .error = std::move(error) };
}

class UnionFind {
public:
    explicit UnionFind(std::size_t count) : parent_(count) { std::iota(parent_.begin(), parent_.end(), std::size_t{ 0 }); }
    [[nodiscard]] std::size_t Find(std::size_t value) {
        while (parent_[value] != value) {
            parent_[value] = parent_[parent_[value]];
            value = parent_[value];
        }
        return value;
    }
    // The smaller index (= smaller guid) stays the representative.
    void Join(std::size_t left, std::size_t right) {
        const std::size_t a = Find(left);
        const std::size_t b = Find(right);
        if (a != b) parent_[std::max(a, b)] = std::min(a, b);
    }

private:
    std::vector<std::size_t> parent_;
};

[[nodiscard]] Affine Compose(const Affine& parent, const Affine& local) {
    Affine out{};
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            out[column * 3 + row] = parent[0 * 3 + row] * local[column * 3 + 0] + parent[1 * 3 + row] * local[column * 3 + 1] +
                parent[2 * 3 + row] * local[column * 3 + 2];
        }
    }
    for (int row = 0; row < 3; ++row) {
        out[9 + row] = parent[0 * 3 + row] * local[9] + parent[1 * 3 + row] * local[10] + parent[2 * 3 + row] * local[11] + parent[9 + row];
    }
    return out;
}

[[nodiscard]] Affine LocalMatrix(const kb::scene::ScenePrefabNodeDesc& node) {
    const kb::scene::TransformComponent& transform = node.transform;
    const kb::math::DVec3 translation = node.LocalTranslation();
    double x = transform.localRotation.x;
    double y = transform.localRotation.y;
    double z = transform.localRotation.z;
    double w = transform.localRotation.w;
    const double length = std::sqrt(x * x + y * y + z * z + w * w);
    if (length > 1e-12) {
        x /= length; y /= length; z /= length; w /= length;
    } else {
        x = 0.0; y = 0.0; z = 0.0; w = 1.0;
    }
    const double sx = transform.localScale.x;
    const double sy = transform.localScale.y;
    const double sz = transform.localScale.z;
    return Affine{
        (1.0 - 2.0 * (y * y + z * z)) * sx, (2.0 * (x * y + z * w)) * sx, (2.0 * (x * z - y * w)) * sx,
        (2.0 * (x * y - z * w)) * sy, (1.0 - 2.0 * (x * x + z * z)) * sy, (2.0 * (y * z + x * w)) * sy,
        (2.0 * (x * z + y * w)) * sz, (2.0 * (y * z - x * w)) * sz, (1.0 - 2.0 * (x * x + y * y)) * sz,
        translation.x, translation.y, translation.z,
    };
}

[[nodiscard]] std::string UnitDirectory(const UnitKey& key) {
    return key.layer.empty() ? std::string{ "base" } : "layer." + key.layer;
}

[[nodiscard]] std::int64_t FloorDivide(std::int64_t value, std::int64_t divisor) noexcept {
    std::int64_t quotient = value / divisor;
    if (value % divisor != 0 && ((value < 0) != (divisor < 0))) {
        --quotient;
    }
    return quotient;
}

// Cells and proxies are grouped by region so a region's output shares a folder prefix.
[[nodiscard]] std::string RegionDirectory(const WorldCellCoord& coord, std::int64_t regionCells) {
    return "r_" + std::to_string(FloorDivide(coord.x, regionCells)) + "_" + std::to_string(FloorDivide(coord.z, regionCells));
}

[[nodiscard]] std::string UnitFile(const UnitKey& key, std::int64_t regionCells) {
    return key.persistent ? std::string{ "persistent.21kbscene" }
                          : RegionDirectory(key.coord, regionCells) + "/c_" + std::to_string(key.coord.x) + "_" +
            std::to_string(key.coord.z) + ".21kbscene";
}

[[nodiscard]] std::string UnitLabel(const UnitKey& key) {
    std::string label = key.persistent ? std::string{ "persistent" } : "cell " + std::to_string(key.coord.x) + "," + std::to_string(key.coord.z);
    return key.layer.empty() ? label : label + " [" + key.layer + "]";
}

void CollectHlodInstances(const WorldObjectFile& object, const WorldPartitionGrid& grid, WorldCellCoord coord, std::vector<WorldHlodMeshInstance>& instances) {
    const auto nodes = object.prefab.Nodes();
    std::vector<Affine> world(nodes.size());
    std::vector<bool> visible(nodes.size(), true);
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        const kb::scene::ScenePrefabNodeDesc& node = nodes[index];
        const Affine local = LocalMatrix(node);
        const bool hidden = node.visibility.mode == kb::scene::VisibilityMode::Hidden || !node.visibility.visible;
        if (node.parentNode == kb::scene::ScenePrefabNodeDesc::NoParent) {
            world[index] = local;
            visible[index] = !hidden;
        } else {
            world[index] = Compose(world[node.parentNode], local);
            visible[index] = visible[node.parentNode] && !hidden;
        }
        if (!visible[index] || !node.components.meshRenderer.has_value() || node.components.meshRenderer->meshAssetId == 0U) {
            continue;
        }
        const kb::scene::MeshRendererComponent& renderer = *node.components.meshRenderer;
        WorldHlodMeshInstance instance;
        instance.meshAssetId = renderer.meshAssetId;
        instance.rendererMaterial = renderer.materialAssetId;
        instance.slotMaterialCount = std::min<std::uint32_t>(renderer.materialSlotOverrideCount, static_cast<std::uint32_t>(instance.slotMaterials.size()));
        for (std::uint32_t slot = 0U; slot < instance.slotMaterialCount; ++slot) {
            instance.slotMaterials[slot] = renderer.materialSlotAssetIds[slot];
        }
        instance.transform = world[index];
        instance.transform[9] -= grid.MinX(coord);
        instance.transform[11] -= grid.MinZ(coord);
        instances.push_back(instance);
    }
}

// Bakes the static geometry of the base layer (always-loaded objects included) and writes one
// navigation mesh per cell with the tiles whose centre lies in the cell.
[[nodiscard]] std::string BuildNavigation(const WorldDescriptor& descriptor, const std::vector<WorldObjectFile>& objects,
    const WorldPartitionGrid& grid, std::int64_t regionCells, kb::navigation::INavGeometrySource* geometrySource,
    const std::filesystem::path& staging, WorldCellIndex& index, WorldBuildReport& report) {
    const kb::navigation::NavMeshBuildSettings& settings = descriptor.navigation.build;
    kb::navigation::NavGeometry geometry;
    kb::navigation::NavGeometryCollectStats stats;
    for (const WorldObjectFile& object : objects) {
        if (object.header.dataLayer.empty()) {
            kb::navigation::CollectNavGeometry(object.prefab.Nodes(), settings, geometrySource, geometry, stats);
        }
    }
    if (stats.unresolved != 0U) {
        report.warnings.push_back("navigation: " + std::to_string(stats.unresolved) + " mesh(es) could not be read and were left out");
    }
    kb::navigation::NavMeshBakeResult baked = kb::navigation::NavMeshBuilder::Bake(settings, geometry);
    if (!baked.succeeded) {
        return "navigation bake failed: " + baked.error;
    }
    report.navBakeMilliseconds = baked.stats.milliseconds;
    std::map<WorldCellCoord, kb::navigation::NavMeshAsset> cells;
    const double tileSize = settings.TileWorldSize();
    for (kb::navigation::NavTile& tile : baked.tiles) {
        const std::optional<WorldCellCoord> cell = grid.CellOf((static_cast<double>(tile.coord.x) + 0.5) * tileSize,
            (static_cast<double>(tile.coord.z) + 0.5) * tileSize);
        if (!cell.has_value()) {
            continue;
        }
        kb::navigation::NavMeshAsset& asset = cells[*cell];
        asset.settings = settings;
        asset.tiles.push_back(std::move(tile));
    }
    for (auto& [coord, asset] : cells) {
        // The bake sorted its tiles by profile and coordinate; each cell keeps that order.
        const std::string relative = "nav/" + RegionDirectory(coord, regionCells) + "/n_" + std::to_string(coord.x) + "_" + std::to_string(coord.z) +
            std::string{ kb::navigation::NavMeshAsset::Extension };
        std::string error;
        if (!kb::navigation::NavMeshAssetIO::Write(staging / std::filesystem::path{ relative }, asset, error)) {
            return "could not write the navigation mesh of cell " + std::to_string(coord.x) + "," + std::to_string(coord.z) + ": " + error;
        }
        std::error_code code;
        const std::uint64_t bytes = std::filesystem::file_size(staging / std::filesystem::path{ relative }, code);
        report.writtenBytes += code ? 0U : bytes;
        report.navTileCount += asset.tiles.size();
        index.navMeshes.push_back({ .coord = coord, .mesh = relative, .tileCount = static_cast<std::uint32_t>(asset.tiles.size()) });
    }
    report.navMeshCount = index.navMeshes.size();
    return {};
}

[[nodiscard]] bool ReplaceDirectory(const std::filesystem::path& staging, const std::filesystem::path& target, std::string& error) {
    std::error_code code;
    std::filesystem::remove_all(target, code);
    if (code) {
        error = "could not remove the previous build " + target.generic_string() + ": " + code.message();
        return false;
    }
    std::filesystem::rename(staging, target, code);
    if (code) {
        error = "could not publish the build to " + target.generic_string() + ": " + code.message();
        return false;
    }
    return true;
}

} // namespace

WorldBuildResult WorldCellBuilder::Build(const std::filesystem::path& descriptorPath, IWorldHlodBaker* hlodBaker) {
    return Build(descriptorPath, hlodBaker, nullptr);
}

WorldBuildResult WorldCellBuilder::Build(const std::filesystem::path& descriptorPath, IWorldHlodBaker* hlodBaker,
    kb::navigation::INavGeometrySource* navigationGeometry) {
    const WorldDescriptorReadResult descriptorRead = WorldDescriptorIO::Read(descriptorPath);
    if (!descriptorRead.succeeded) {
        return Failure(descriptorRead.error);
    }
    const WorldDescriptor& descriptor = descriptorRead.descriptor;
    const WorldPartitionGrid grid{ descriptor.cellSize };
    WorldBuildReport report;

    std::vector<WorldObjectFile> objects;
    for (const std::filesystem::path& file : WorldObjectFileIO::List(WorldPaths::ObjectsDirectory(descriptorPath, descriptor))) {
        WorldObjectReadResult read = WorldObjectFileIO::Read(file);
        if (!read.succeeded) {
            return Failure(read.error);
        }
        if (!read.object.header.dataLayer.empty() && descriptor.FindDataLayer(read.object.header.dataLayer) == nullptr) {
            return Failure("object " + read.object.header.guid + " (" + read.object.header.name + ") uses data layer \"" +
                read.object.header.dataLayer + "\" which the world does not declare");
        }
        objects.push_back(std::move(read.object));
    }
    report.objectCount = objects.size();

    // Node ids are unique across the world; a reference names its target's object.
    std::unordered_map<std::uint64_t, std::size_t> owners;
    for (std::size_t object = 0U; object < objects.size(); ++object) {
        for (const kb::scene::ScenePrefabNodeDesc& node : objects[object].prefab.Nodes()) {
            const auto [existing, inserted] = owners.emplace(node.stableId, object);
            if (!inserted) {
                return Failure("objects " + objects[existing->second].header.guid + " and " + objects[object].header.guid +
                    " share node id " + std::to_string(node.stableId) + "; save one of them again in the editor");
            }
        }
    }
    UnionFind groups{ objects.size() };
    for (std::size_t object = 0U; object < objects.size(); ++object) {
        for (const std::uint64_t reference : CollectNodeReferences(objects[object].prefab)) {
            const auto target = owners.find(reference);
            if (target == owners.end()) {
                return Failure("object " + objects[object].header.guid + " (" + objects[object].header.name +
                    ") references node " + std::to_string(reference) + " that no object of the world contains");
            }
            groups.Join(object, target->second);
        }
    }

    // A linked group is placed by its representative (lowest guid): one cell, one layer.
    std::vector<bool> groupAlwaysLoaded(objects.size(), false);
    for (std::size_t object = 0U; object < objects.size(); ++object) {
        if (objects[object].header.alwaysLoaded) {
            groupAlwaysLoaded[groups.Find(object)] = true;
        }
    }
    std::map<UnitKey, std::vector<std::size_t>> units;
    for (std::size_t object = 0U; object < objects.size(); ++object) {
        const std::size_t representative = groups.Find(object);
        const WorldObjectHeader& lead = objects[representative].header;
        if (objects[object].header.dataLayer != lead.dataLayer) {
            return Failure("linked objects " + lead.guid + " and " + objects[object].header.guid +
                " are in different data layers; linked objects must share one layer");
        }
        const bool persistent = groupAlwaysLoaded[representative];
        UnitKey key{ .persistent = persistent, .coord = {}, .layer = lead.dataLayer };
        if (!persistent) {
            const std::optional<WorldCellCoord> cell = grid.CellOf(lead.position.x, lead.position.z);
            if (!cell.has_value()) {
                return Failure("object " + lead.guid + " (" + lead.name + ") lies outside the addressable world");
            }
            key.coord = *cell;
        }
        units[key].push_back(object);
    }

    const std::filesystem::path cellsDirectory = WorldPaths::CellsDirectory(descriptorPath);
    std::filesystem::path staging = cellsDirectory;
    staging += ".building";
    std::error_code code;
    std::filesystem::remove_all(staging, code);
    std::filesystem::create_directories(staging, code);
    if (code) {
        return Failure("could not create " + staging.generic_string() + ": " + code.message());
    }
    struct StagingCleanup {
        std::filesystem::path path;
        bool active = true;
        ~StagingCleanup() {
            if (active) {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        }
    } cleanup{ staging };

    WorldCellIndex index;
    index.worldGuid = descriptor.guid;
    index.worldName = descriptor.name;
    index.cellSize = descriptor.cellSize;
    index.hlodRange = descriptor.hlod.enabled ? descriptor.hlod.range : 0.0;
    index.dataLayers = descriptor.dataLayers;
    index.regionCells = descriptor.regionCells;
    const std::int64_t regionCells = descriptor.regionCells;
    for (const auto& [key, members] : units) {
        kb::scene::SceneDocument document;
        document.guid = "worldcell:" + descriptor.guid + ":" + UnitDirectory(key) + ":" + UnitFile(key, regionCells);
        document.name = descriptor.name + " " + UnitLabel(key);
        document.worldType = "WorldCell";
        document.tagDefinitions = descriptor.tagDefinitions;
        std::uint32_t nodeCount = 0U;
        for (const std::size_t object : members) {
            const std::uint32_t base = static_cast<std::uint32_t>(document.worldPrefab.NodeCount());
            for (kb::scene::ScenePrefabNodeDesc node : objects[object].prefab.Nodes()) {
                if (node.parentNode != kb::scene::ScenePrefabNodeDesc::NoParent) {
                    node.parentNode += base;
                }
                static_cast<void>(document.worldPrefab.AddNode(std::move(node)));
            }
        }
        nodeCount = static_cast<std::uint32_t>(document.worldPrefab.NodeCount());
        const std::string relative = UnitDirectory(key) + "/" + UnitFile(key, regionCells);
        const std::filesystem::path scenePath = staging / std::filesystem::path{ relative };
        std::filesystem::create_directories(scenePath.parent_path(), code);
        if (code || !kb::scene::SceneDocumentService::Save(document, scenePath)) {
            return Failure("could not write " + UnitLabel(key) + " of world " + descriptor.name + " to " + scenePath.generic_string());
        }
        const std::uint64_t bytes = std::filesystem::file_size(scenePath, code);
        report.writtenBytes += code ? 0U : bytes;
        index.units.push_back({
            .coord = key.coord,
            .persistent = key.persistent,
            .dataLayer = key.layer,
            .scene = relative,
            .objectCount = static_cast<std::uint32_t>(members.size()),
            .nodeCount = nodeCount,
            .estimatedBytes = (code ? 0U : bytes) + static_cast<std::uint64_t>(nodeCount) * WorldCellIndex::EstimatedBytesPerNode,
        });

        if (hlodBaker == nullptr || !descriptor.hlod.enabled || key.persistent || !key.layer.empty()) {
            continue;
        }
        WorldHlodRequest request{ .coord = key.coord, .cellSize = descriptor.cellSize, .triangleRatio = descriptor.hlod.triangleRatio, .instances = {} };
        for (const std::size_t object : members) {
            CollectHlodInstances(objects[object], grid, key.coord, request.instances);
        }
        if (request.instances.empty()) {
            continue;
        }
        WorldHlodResult hlod = hlodBaker->Bake(request);
        if (!hlod.succeeded) {
            if (!hlod.error.empty()) {
                report.warnings.push_back("HLOD for " + UnitLabel(key) + " skipped: " + hlod.error);
            }
            continue;
        }
        const std::string meshRelative = "hlod/" + RegionDirectory(key.coord, regionCells) + "/h_" + std::to_string(key.coord.x) + "_" +
            std::to_string(key.coord.z) + ".obj";
        std::string error;
        if (!text::WriteTextFileAtomically(staging / std::filesystem::path{ meshRelative }, hlod.objText, error)) {
            return Failure(error);
        }
        report.writtenBytes += hlod.objText.size();
        index.hlods.push_back({
            .coord = key.coord,
            .mesh = meshRelative,
            .materials = std::move(hlod.materials),
            .triangleCount = hlod.triangleCount,
            .sourceTriangleCount = hlod.sourceTriangleCount,
        });
    }
    if (descriptor.navigation.enabled) {
        if (std::string failed = BuildNavigation(descriptor, objects, grid, regionCells, navigationGeometry, staging, index, report); !failed.empty()) {
            return Failure(std::move(failed));
        }
    }
    std::string error;
    const std::filesystem::path indexPath = staging / WorldPaths::CellIndexPath(descriptorPath).filename();
    if (!WorldCellIndexIO::Write(indexPath, index, error)) {
        return Failure("could not write the cell index: " + error);
    }
    if (!ReplaceDirectory(staging, cellsDirectory, error)) {
        return Failure(error);
    }
    cleanup.active = false;
    report.unitCount = index.units.size();
    report.hlodCount = index.hlods.size();
    return { .succeeded = true, .report = std::move(report), .error = {} };
}

WorldBuildResult WorldCellBuilder::BuildAll(const std::filesystem::path& root, IWorldHlodBaker* hlodBaker, std::size_t& builtWorlds) {
    return BuildAll(root, hlodBaker, nullptr, builtWorlds);
}

WorldBuildResult WorldCellBuilder::BuildAll(const std::filesystem::path& root, IWorldHlodBaker* hlodBaker,
    kb::navigation::INavGeometrySource* navigationGeometry, std::size_t& builtWorlds) {
    builtWorlds = 0U;
    std::vector<std::filesystem::path> descriptors;
    std::error_code code;
    for (std::filesystem::recursive_directory_iterator it{ root, code }, end; !code && it != end; it.increment(code)) {
        if (it->is_regular_file(code) && it->path().extension() == WorldDescriptor::Extension) {
            descriptors.push_back(it->path());
        }
    }
    if (code) {
        return Failure("could not search " + root.generic_string() + " for worlds: " + code.message());
    }
    std::ranges::sort(descriptors);
    WorldBuildResult total{ .succeeded = true, .report = {}, .error = {} };
    for (const std::filesystem::path& descriptor : descriptors) {
        WorldBuildResult built = Build(descriptor, hlodBaker, navigationGeometry);
        if (!built.succeeded) {
            built.error = descriptor.generic_string() + ": " + built.error;
            return built;
        }
        ++builtWorlds;
        total.report.objectCount += built.report.objectCount;
        total.report.unitCount += built.report.unitCount;
        total.report.hlodCount += built.report.hlodCount;
        total.report.navMeshCount += built.report.navMeshCount;
        total.report.navTileCount += built.report.navTileCount;
        total.report.navBakeMilliseconds += built.report.navBakeMilliseconds;
        total.report.writtenBytes += built.report.writtenBytes;
        total.report.warnings.insert(total.report.warnings.end(), built.report.warnings.begin(), built.report.warnings.end());
    }
    return total;
}

} // namespace kb::world
