#include "scene/EditorWorldPartition.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/world/WorldPartitionGrid.hpp"
#include "kb/render/world/WorldHlodMeshBaker.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace kb::editor {

bool EditorWorldPartition::Open(kb::scene::Scene& scene, const std::filesystem::path& descriptorPath, std::string& error) {
    reloadOrder_.clear();
    return session_.Open(scene, descriptorPath, error);
}

void EditorWorldPartition::Close() noexcept {
    session_.Close();
    reloadOrder_.clear();
}

bool EditorWorldPartition::IsOpen() const noexcept {
    return session_.IsOpen();
}

kb::world::WorldEditSession& EditorWorldPartition::Session() noexcept {
    return session_;
}

const kb::world::WorldEditSession& EditorWorldPartition::Session() const noexcept {
    return session_;
}

bool EditorWorldPartition::GridVisible() const noexcept {
    return gridVisible_;
}

void EditorWorldPartition::SetGridVisible(bool visible) noexcept {
    gridVisible_ = visible;
}

std::vector<EditorWorldGridLine> EditorWorldPartition::GridLines(double x, double y, double z) const {
    std::vector<EditorWorldGridLine> lines;
    if (!session_.IsOpen() || !gridVisible_) {
        return lines;
    }
    const kb::world::WorldPartitionGrid grid{ session_.CellSize() };
    // Loaded cells win over merely occupied ones; nearest cells are outlined first.
    std::map<kb::world::WorldCellCoord, bool> cells;
    for (const kb::world::WorldCellCoord& cell : session_.OccupiedCells()) cells.emplace(cell, false);
    for (const kb::world::WorldCellCoord& cell : session_.LoadedCells()) cells[cell] = true;
    std::vector<std::pair<double, std::pair<kb::world::WorldCellCoord, bool>>> ordered;
    ordered.reserve(cells.size());
    for (const auto& [cell, loaded] : cells) {
        ordered.push_back({ grid.DistanceSquared(cell, x, z), { cell, loaded } });
    }
    std::ranges::sort(ordered, [](const auto& left, const auto& right) {
        return left.first != right.first ? left.first < right.first : left.second.first < right.second.first;
    });
    if (ordered.size() > MaxGridCells) {
        ordered.resize(MaxGridCells);
    }
    const float height = static_cast<float>(y);
    lines.reserve(ordered.size() * 4U);
    for (const auto& [distance, entry] : ordered) {
        static_cast<void>(distance);
        const auto& [cell, loaded] = entry;
        const float minX = static_cast<float>(grid.MinX(cell));
        const float minZ = static_cast<float>(grid.MinZ(cell));
        const float maxX = static_cast<float>(grid.MinX(cell) + grid.CellSize());
        const float maxZ = static_cast<float>(grid.MinZ(cell) + grid.CellSize());
        const std::array<float, 3U> color = loaded ? LoadedCellColor : UnloadedCellColor;
        lines.push_back({ { minX, height, minZ }, { maxX, height, minZ }, color });
        lines.push_back({ { maxX, height, minZ }, { maxX, height, maxZ }, color });
        lines.push_back({ { maxX, height, maxZ }, { minX, height, maxZ }, color });
        lines.push_back({ { minX, height, maxZ }, { minX, height, minZ }, color });
    }
    return lines;
}

std::size_t EditorWorldPartition::LoadAround(double x, double z, std::int64_t radiusCells, std::string& error) {
    if (!session_.IsOpen()) {
        error = "no world is open";
        return 0U;
    }
    const kb::world::WorldPartitionGrid grid{ session_.CellSize() };
    const std::optional<kb::world::WorldCellCoord> centre = grid.CellOf(x, z);
    if (!centre.has_value()) {
        error = "the camera is outside the addressable world";
        return 0U;
    }
    const std::int64_t radius = std::max<std::int64_t>(0, radiusCells);
    return session_.LoadRegion({ centre->x - radius, centre->z - radius }, { centre->x + radius, centre->z + radius }, error);
}

kb::world::WorldBuildResult EditorWorldPartition::Build(kb::assets::AssetManager& assets) {
    if (!session_.IsOpen()) {
        return { .succeeded = false, .report = {}, .error = "no world is open" };
    }
    kb::render::WorldHlodMeshBaker baker{ assets };
    return kb::world::WorldCellBuilder::Build(session_.DescriptorPath(), &baker);
}

bool EditorWorldPartition::CycleDataLayer(kb::scene::SceneEntity root, std::string& layer, std::string& error) {
    const std::optional<kb::world::WorldEditObjectInfo> object = session_.FindObject(root);
    const std::vector<kb::world::WorldDataLayerDesc>& layers = session_.Descriptor().dataLayers;
    std::string next;
    if (object.has_value() && !layers.empty()) {
        const auto current = std::ranges::find(layers, object->dataLayer, &kb::world::WorldDataLayerDesc::name);
        if (current == layers.end()) {
            next = layers.front().name;
        } else if (std::next(current) != layers.end()) {
            next = std::next(current)->name;
        }
    }
    if (!session_.SetObjectDataLayer(root, next, error)) {
        return false;
    }
    layer = next;
    return true;
}

void EditorWorldPartition::PrepareSceneReload() {
    reloadOrder_ = session_.IsOpen() ? session_.RootObjectGuids() : std::vector<std::string>{};
}

void EditorWorldPartition::CompleteSceneReload() {
    if (!session_.IsOpen() || reloadOrder_.empty()) {
        return;
    }
    std::string error;
    static_cast<void>(session_.RebindRootObjects(reloadOrder_, error));
    reloadOrder_.clear();
}

} // namespace kb::editor
