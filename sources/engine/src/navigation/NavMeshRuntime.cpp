#include "navigation/NavMeshRuntime.hpp"

#include <DetourCommon.h>
#include <DetourNavMeshBuilder.h>
#include <DetourTileCacheBuilder.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace kb::navigation {
namespace {

using kb::math::Vec3;

constexpr int kInitialTileCapacity = 64;
constexpr int kMaxTileCapacity = 1 << 24;
constexpr int kQueryNodes = 4096;
constexpr int kMaxPathPolygons = 512;
constexpr int kMaxStraightCorners = 256;
// Runtime tile coordinates (relative to the origin's tile) stay well inside Detour's int range.
constexpr std::int64_t kMaxRuntimeTile = std::int64_t{ 1 } << 28U;

// Owns the buffers the tile cache builder fills.
struct TileCacheScratch {
    dtTileCacheAlloc alloc;
    dtTileCacheContourSet* contours = nullptr;
    dtTileCachePolyMesh* polygons = nullptr;
    ~TileCacheScratch() {
        if (contours != nullptr) dtFreeTileCacheContourSet(&alloc, contours);
        if (polygons != nullptr) dtFreeTileCachePolyMesh(&alloc, polygons);
    }
};

[[nodiscard]] std::uint8_t AreaCode(kb::scene::NavAreaId area) noexcept {
    return static_cast<std::uint8_t>(area + 1U);
}

void ToArray(Vec3 value, float* out) noexcept {
    out[0] = value.x;
    out[1] = value.y;
    out[2] = value.z;
}

[[nodiscard]] Vec3 FromArray(const float* value) noexcept {
    return Vec3{ value[0], value[1], value[2] };
}

} // namespace

NavAreaFilter::NavAreaFilter() noexcept {
    setIncludeFlags(0xFFFFU);
    setExcludeFlags(0U);
}

bool NavAreaFilter::passFilter(const dtPolyRef, const dtMeshTile*, const dtPoly* poly) const {
    const unsigned char area = poly->getArea();
    if (area == 0U || area > kb::scene::kNavAreaCount || (poly->flags & getIncludeFlags()) == 0U) {
        return false;
    }
    return (areas_ & kb::scene::NavAreaBit(static_cast<kb::scene::NavAreaId>(area - 1U))) != 0U;
}

void NavMeshRuntime::MeshDeleter::operator()(dtNavMesh* mesh) const noexcept {
    dtFreeNavMesh(mesh);
}

void NavMeshRuntime::QueryDeleter::operator()(dtNavMeshQuery* query) const noexcept {
    dtFreeNavMeshQuery(query);
}

NavMeshRuntime::NavMeshRuntime() {
    costs_.fill(1.0F);
}

NavMeshRuntime::~NavMeshRuntime() = default;

std::uint32_t NavMeshRuntime::ProfileCount() const noexcept {
    return layout_ ? static_cast<std::uint32_t>(layout_->profiles.size()) : 0U;
}

std::uint32_t NavMeshRuntime::ProfileForRadius(float radius) const noexcept {
    if (!layout_) {
        return 0U;
    }
    std::uint32_t best = 0U;
    bool fits = false;
    std::uint32_t widest = 0U;
    for (std::uint32_t profile = 0U; profile < layout_->profiles.size(); ++profile) {
        const float profileRadius = layout_->profiles[profile].radius;
        if (profileRadius > layout_->profiles[widest].radius) widest = profile;
        if (profileRadius + 1.0e-4F >= radius && (!fits || profileRadius < layout_->profiles[best].radius)) {
            best = profile;
            fits = true;
        }
    }
    return fits ? best : widest;
}

bool NavMeshRuntime::CreateMesh(std::uint32_t profile, int capacity) {
    ProfileMesh& target = meshes_[profile];
    target.query.reset();
    target.mesh.reset(dtAllocNavMesh());
    target.query.reset(dtAllocNavMeshQuery());
    if (!target.mesh || !target.query) {
        return false;
    }
    dtNavMeshParams params{};
    params.orig[0] = static_cast<float>(static_cast<double>(originTile_.x) * tileSize_ - origin_.x);
    params.orig[1] = 0.0F;
    params.orig[2] = static_cast<float>(static_cast<double>(originTile_.z) * tileSize_ - origin_.z);
    params.tileWidth = static_cast<float>(tileSize_);
    params.tileHeight = static_cast<float>(tileSize_);
    params.maxTiles = capacity;
    params.maxPolys = 1 << 16;
    if (dtStatusFailed(target.mesh->init(&params)) || dtStatusFailed(target.query->init(target.mesh.get(), kQueryNodes))) {
        target.query.reset();
        target.mesh.reset();
        return false;
    }
    target.capacity = capacity;
    target.generation = nextGeneration_++;
    // The tiles of the previous mesh went with it.
    for (auto& [key, slot] : tiles_) {
        if (key.profile == profile) slot.placed.clear();
    }
    return true;
}

NavTileCoord NavMeshRuntime::TileOf(double x, double z) const noexcept {
    return { static_cast<std::int64_t>(std::floor((x + origin_.x) / tileSize_)), static_cast<std::int64_t>(std::floor((z + origin_.z) / tileSize_)) };
}

std::uint64_t NavMeshRuntime::AddTileSet(std::shared_ptr<const NavMeshAsset> asset, std::string& error) {
    if (asset == nullptr) {
        error = "no navigation mesh";
        return 0U;
    }
    if (std::string invalid = NavMeshAssetIO::Validate(*asset); !invalid.empty()) {
        error = std::move(invalid);
        return 0U;
    }
    if (layout_ && !SameNavMeshLayout(*layout_, asset->settings)) {
        error = "the navigation mesh was baked with other cell, tile or agent settings than the meshes already in the scene";
        return 0U;
    }
    if (!layout_) {
        layout_ = asset->settings;
        tileSize_ = layout_->TileWorldSize();
        originTile_ = { static_cast<std::int64_t>(std::floor(origin_.x / tileSize_)), static_cast<std::int64_t>(std::floor(origin_.z / tileSize_)) };
        meshes_.clear();
        meshes_.resize(layout_->profiles.size());
        for (std::uint32_t profile = 0U; profile < meshes_.size(); ++profile) {
            if (!CreateMesh(profile, kInitialTileCapacity)) {
                Reset();
                error = "out of memory creating the navigation mesh";
                return 0U;
            }
        }
    }
    const std::uint64_t handle = nextHandle_++;
    sets_.emplace(handle, asset);
    for (std::size_t index = 0U; index < asset->tiles.size(); ++index) {
        const NavTile& tile = asset->tiles[index];
        const TileKey key{ tile.profile, tile.coord };
        TileSlot& slot = tiles_[key];
        slot.owners.push_back({ handle, index });
        if (slot.owners.size() == 1U) {
            Place(key, slot);
        }
    }
    ++revision_;
    return handle;
}

bool NavMeshRuntime::RemoveTileSet(std::uint64_t handle) {
    const auto set = sets_.find(handle);
    if (set == sets_.end()) {
        return false;
    }
    for (const NavTile& tile : set->second->tiles) {
        const TileKey key{ tile.profile, tile.coord };
        const auto found = tiles_.find(key);
        if (found == tiles_.end()) {
            continue;
        }
        TileSlot& slot = found->second;
        const bool active = !slot.rebuilt.has_value() && !slot.owners.empty() && slot.owners.front().handle == handle;
        std::erase_if(slot.owners, [handle](const TileOwner& owner) { return owner.handle == handle; });
        if (active) {
            Unplace(key, slot);
            if (!slot.owners.empty()) {
                Place(key, slot);
            }
        }
        if (!slot.Used()) {
            tiles_.erase(found);
        }
    }
    sets_.erase(set);
    ++revision_;
    if (sets_.empty()) {
        Reset();
    }
    return true;
}

void NavMeshRuntime::Reset() {
    tiles_.clear();
    meshes_.clear();
    layout_.reset();
}

void NavMeshRuntime::SetOrigin(const kb::math::DVec3& origin) {
    if (origin == origin_) {
        return;
    }
    origin_ = origin;
    if (!layout_) {
        return;
    }
    originTile_ = { static_cast<std::int64_t>(std::floor(origin_.x / tileSize_)), static_cast<std::int64_t>(std::floor(origin_.z / tileSize_)) };
    for (std::uint32_t profile = 0U; profile < meshes_.size(); ++profile) {
        static_cast<void>(CreateMesh(profile, std::max(meshes_[profile].capacity, kInitialTileCapacity)));
        PlaceAll(profile);
    }
    ++revision_;
}

void NavMeshRuntime::PlaceAll(std::uint32_t profile) {
    for (auto& [key, slot] : tiles_) {
        if (key.profile == profile && slot.Used()) {
            Place(key, slot);
        }
    }
}

void NavMeshRuntime::Unplace(const TileKey& key, TileSlot& slot) {
    dtNavMesh* mesh = Mesh(key.profile);
    for (const dtTileRef reference : slot.placed) {
        if (mesh != nullptr) {
            static_cast<void>(mesh->removeTile(reference, nullptr, nullptr));
        }
    }
    slot.placed.clear();
}

void NavMeshRuntime::Place(const TileKey& key, TileSlot& slot) {
    Unplace(key, slot);
    if (!slot.Used() || key.profile >= meshes_.size() || !meshes_[key.profile].mesh) {
        return;
    }
    const NavTile& tile = slot.rebuilt.has_value() ? *slot.rebuilt : sets_.at(slot.owners.front().handle)->tiles[slot.owners.front().index];
    if (std::llabs(tile.coord.x - originTile_.x) >= kMaxRuntimeTile || std::llabs(tile.coord.z - originTile_.z) >= kMaxRuntimeTile) {
        // Too far from the origin to address: the tile waits until the origin comes closer.
        return;
    }
    for (std::size_t layer = 0U; layer < tile.layers.size(); ++layer) {
        unsigned char* data = nullptr;
        int size = 0;
        if (!BuildLayer(key, tile, layer, data, size)) {
            continue;
        }
        dtTileRef reference = 0U;
        dtStatus status = meshes_[key.profile].mesh->addTile(data, size, DT_TILE_FREE_DATA, 0U, &reference);
        if (dtStatusFailed(status) && dtStatusDetail(status, DT_OUT_OF_MEMORY) && meshes_[key.profile].capacity < kMaxTileCapacity) {
            // Out of tile slots: start over with twice as many and place every tile again.
            dtFree(data);
            const int capacity = meshes_[key.profile].capacity * 2;
            if (!CreateMesh(key.profile, capacity)) {
                return;
            }
            PlaceAll(key.profile);
            return;
        }
        if (dtStatusFailed(status)) {
            dtFree(data);
            continue;
        }
        slot.placed.push_back(reference);
    }
}

bool NavMeshRuntime::BuildLayer(const TileKey& key, const NavTile& tile, std::size_t layerIndex, unsigned char*& data, int& size) const {
    const NavMeshBuildSettings& settings = *layout_;
    const NavAgentProfile& profile = settings.profiles[key.profile];
    const NavTileLayer& source = tile.layers[layerIndex];
    const int cells = static_cast<int>(settings.tileCells);
    const std::size_t count = static_cast<std::size_t>(cells) * static_cast<std::size_t>(cells);

    dtTileCacheLayerHeader header{};
    header.magic = DT_TILECACHE_MAGIC;
    header.version = DT_TILECACHE_VERSION;
    header.tx = static_cast<int>(tile.coord.x - originTile_.x);
    header.ty = static_cast<int>(tile.coord.z - originTile_.z);
    header.tlayer = static_cast<int>(layerIndex);
    header.bmin[0] = static_cast<float>(static_cast<double>(tile.coord.x) * tileSize_ - origin_.x);
    header.bmin[1] = static_cast<float>(static_cast<double>(source.minY) - origin_.y);
    header.bmin[2] = static_cast<float>(static_cast<double>(tile.coord.z) * tileSize_ - origin_.z);
    header.bmax[0] = static_cast<float>(static_cast<double>(tile.coord.x + 1) * tileSize_ - origin_.x);
    header.bmax[1] = static_cast<float>(static_cast<double>(source.maxY) - origin_.y);
    header.bmax[2] = static_cast<float>(static_cast<double>(tile.coord.z + 1) * tileSize_ - origin_.z);
    header.hmin = source.heightMin;
    header.hmax = source.heightMax;
    header.width = static_cast<unsigned char>(cells);
    header.height = static_cast<unsigned char>(cells);
    header.minx = source.minX;
    header.maxx = source.maxX;
    header.miny = source.minZ;
    header.maxy = source.maxZ;

    std::vector<unsigned char> heights(source.heights.begin(), source.heights.end());
    std::vector<unsigned char> areas(source.areas.begin(), source.areas.end());
    std::vector<unsigned char> connections(source.connections.begin(), source.connections.end());
    std::vector<unsigned char> regions(count, 0xFFU);
    dtTileCacheLayer layer{};
    layer.header = &header;
    layer.heights = heights.data();
    layer.areas = areas.data();
    layer.cons = connections.data();
    layer.regs = regions.data();

    // Obstacles: areas first, then carving, so a carving obstacle always wins. Carved holes grow by
    // the agent radius: the layer's walkable area is already shrunk by it from walls.
    const float cellSize = settings.cellSize;
    const float cellHeight = settings.cellHeight;
    const float inflate = profile.radius;
    const auto overlaps = [&](const NavRuntimeObstacle& obstacle, float reach) {
        return obstacle.center.x + reach >= header.bmin[0] && obstacle.center.x - reach <= header.bmax[0] &&
            obstacle.center.z + reach >= header.bmin[2] && obstacle.center.z - reach <= header.bmax[2];
    };
    for (int pass = 0; pass < 2; ++pass) {
        for (const NavRuntimeObstacle& obstacle : obstacles_) {
            const bool carve = obstacle.carve;
            if ((pass == 0) == carve || (!carve && obstacle.area == kb::scene::kDefaultNavArea) || !kb::scene::IsValidNavArea(obstacle.area)) {
                continue;
            }
            const unsigned char code = carve ? DT_TILECACHE_NULL_AREA : AreaCode(obstacle.area);
            const float grow = carve ? inflate : 0.0F;
            if (obstacle.shape == kb::scene::NavObstacleShape::Cylinder) {
                const float radius = obstacle.radius + grow;
                if (!overlaps(obstacle, radius)) continue;
                const float bottom[3] = { obstacle.center.x, obstacle.center.y - obstacle.height * 0.5F, obstacle.center.z };
                static_cast<void>(dtMarkCylinderArea(layer, header.bmin, cellSize, cellHeight, bottom, radius, obstacle.height, code));
            } else {
                const float half[3] = { obstacle.halfExtents.x + grow, obstacle.halfExtents.y, obstacle.halfExtents.z + grow };
                const float reach = std::sqrt(half[0] * half[0] + half[2] * half[2]);
                if (!overlaps(obstacle, reach)) continue;
                if (obstacle.yaw == 0.0F) {
                    const float low[3] = { obstacle.center.x - half[0], obstacle.center.y - half[1], obstacle.center.z - half[2] };
                    const float high[3] = { obstacle.center.x + half[0], obstacle.center.y + half[1], obstacle.center.z + half[2] };
                    static_cast<void>(dtMarkBoxArea(layer, header.bmin, cellSize, cellHeight, low, high, code));
                } else {
                    const float center[3] = { obstacle.center.x, obstacle.center.y, obstacle.center.z };
                    const float cosHalf = std::cos(0.5F * obstacle.yaw);
                    const float sinHalf = std::sin(-0.5F * obstacle.yaw);
                    const float rotation[2] = { cosHalf * sinHalf, cosHalf * cosHalf - 0.5F };
                    static_cast<void>(dtMarkBoxArea(layer, header.bmin, cellSize, cellHeight, center, half, rotation, code));
                }
            }
        }
    }

    const int climb = static_cast<int>(std::floor(profile.maxClimb / cellHeight));
    TileCacheScratch scratch;
    if (dtStatusFailed(dtBuildTileCacheRegions(&scratch.alloc, layer, climb))) {
        return false;
    }
    scratch.contours = dtAllocTileCacheContourSet(&scratch.alloc);
    scratch.polygons = dtAllocTileCachePolyMesh(&scratch.alloc);
    if (scratch.contours == nullptr || scratch.polygons == nullptr ||
        dtStatusFailed(dtBuildTileCacheContours(&scratch.alloc, layer, climb, settings.edgeMaxError, *scratch.contours)) ||
        dtStatusFailed(dtBuildTileCachePolyMesh(&scratch.alloc, *scratch.contours, *scratch.polygons))) {
        return false;
    }
    if (scratch.polygons->npolys == 0) {
        return false;
    }
    for (int polygon = 0; polygon < scratch.polygons->npolys; ++polygon) {
        scratch.polygons->flags[polygon] = PolygonFlags(scratch.polygons->areas[polygon]);
    }

    // Links whose start lies in this tile belong to it; Detour keeps the ones within the layer.
    std::vector<float> linkVertices;
    std::vector<float> linkRadii;
    std::vector<unsigned short> linkFlags;
    std::vector<unsigned char> linkAreas;
    std::vector<unsigned char> linkDirections;
    std::vector<unsigned int> linkIds;
    for (const auto& [userId, link] : links_) {
        if (TileOf(link.start.x, link.start.z) != tile.coord || !kb::scene::IsValidNavArea(link.area)) {
            continue;
        }
        linkVertices.insert(linkVertices.end(), { link.start.x, link.start.y, link.start.z, link.end.x, link.end.y, link.end.z });
        linkRadii.push_back(link.radius);
        linkFlags.push_back(PolygonFlags(AreaCode(link.area)));
        linkAreas.push_back(AreaCode(link.area));
        linkDirections.push_back(link.bidirectional ? static_cast<unsigned char>(DT_OFFMESH_CON_BIDIR) : 0U);
        linkIds.push_back(userId);
    }

    dtNavMeshCreateParams params{};
    params.verts = scratch.polygons->verts;
    params.vertCount = scratch.polygons->nverts;
    params.polys = scratch.polygons->polys;
    params.polyAreas = scratch.polygons->areas;
    params.polyFlags = scratch.polygons->flags;
    params.polyCount = scratch.polygons->npolys;
    params.nvp = DT_VERTS_PER_POLYGON;
    params.walkableHeight = profile.height;
    params.walkableRadius = profile.radius;
    params.walkableClimb = profile.maxClimb;
    params.tileX = header.tx;
    params.tileY = header.ty;
    params.tileLayer = header.tlayer;
    params.cs = cellSize;
    params.ch = cellHeight;
    params.buildBvTree = true;
    dtVcopy(params.bmin, header.bmin);
    dtVcopy(params.bmax, header.bmax);
    if (!linkIds.empty()) {
        params.offMeshConVerts = linkVertices.data();
        params.offMeshConRad = linkRadii.data();
        params.offMeshConFlags = linkFlags.data();
        params.offMeshConAreas = linkAreas.data();
        params.offMeshConDir = linkDirections.data();
        params.offMeshConUserID = linkIds.data();
        params.offMeshConCount = static_cast<int>(linkIds.size());
    }
    return dtCreateNavMeshData(&params, &data, &size);
}

void NavMeshRuntime::MarkTiles(Vec3 low, Vec3 high, std::set<TileKey>& dirty) const {
    if (!layout_) {
        return;
    }
    const NavTileCoord first = TileOf(low.x, low.z);
    const NavTileCoord last = TileOf(high.x, high.z);
    if (last.x - first.x > 4096 || last.z - first.z > 4096) {
        // Wider than any tile set could hold here: rebuild what is loaded.
        for (const auto& [key, slot] : tiles_) dirty.insert(key);
        return;
    }
    for (const auto& [key, slot] : tiles_) {
        if (key.coord.x >= first.x && key.coord.x <= last.x && key.coord.z >= first.z && key.coord.z <= last.z) {
            dirty.insert(key);
        }
    }
}

void NavMeshRuntime::Rebuild(const std::set<TileKey>& dirty) {
    for (const TileKey& key : dirty) {
        const auto found = tiles_.find(key);
        if (found == tiles_.end() || !found->second.Used()) continue;
        Place(key, found->second);
        ++rebuilds_;
    }
    if (!dirty.empty()) {
        ++revision_;
    }
}

void NavMeshRuntime::SetObstacles(std::vector<NavRuntimeObstacle> obstacles) {
    std::ranges::sort(obstacles, {}, &NavRuntimeObstacle::id);
    if (obstacles == obstacles_) {
        return;
    }
    std::set<TileKey> dirty;
    if (layout_) {
        float reach = 0.0F;
        for (const NavAgentProfile& profile : layout_->profiles) reach = std::max(reach, profile.radius);
        reach += layout_->cellSize * 2.0F;
        const auto touch = [&](const NavRuntimeObstacle& obstacle) {
            const float extent = (obstacle.shape == kb::scene::NavObstacleShape::Cylinder
                ? obstacle.radius
                : std::sqrt(obstacle.halfExtents.x * obstacle.halfExtents.x + obstacle.halfExtents.z * obstacle.halfExtents.z)) + reach;
            MarkTiles(obstacle.center - Vec3{ extent, 0.0F, extent }, obstacle.center + Vec3{ extent, 0.0F, extent }, dirty);
        };
        // Only obstacles that differ (appeared, changed or went away) touch tiles.
        std::size_t left = 0U;
        std::size_t right = 0U;
        while (left < obstacles_.size() || right < obstacles.size()) {
            if (right >= obstacles.size() || (left < obstacles_.size() && obstacles_[left].id < obstacles[right].id)) {
                touch(obstacles_[left++]);
            } else if (left >= obstacles_.size() || obstacles[right].id < obstacles_[left].id) {
                touch(obstacles[right++]);
            } else {
                if (!(obstacles_[left] == obstacles[right])) {
                    touch(obstacles_[left]);
                    touch(obstacles[right]);
                }
                ++left;
                ++right;
            }
        }
    }
    obstacles_ = std::move(obstacles);
    Rebuild(dirty);
}

void NavMeshRuntime::SetLinks(std::vector<NavRuntimeLink> links) {
    std::ranges::sort(links, {}, &NavRuntimeLink::id);
    std::map<std::uint32_t, NavRuntimeLink> next;
    for (const NavRuntimeLink& link : links) {
        auto [found, inserted] = linkUserIds_.try_emplace(link.id, nextLinkUserId_);
        if (inserted) ++nextLinkUserId_;
        next.emplace(found->second, link);
    }
    if (next == links_) {
        return;
    }
    std::set<TileKey> dirty;
    const auto touch = [&](const NavRuntimeLink& link) { MarkTiles(link.start, link.start, dirty); };
    for (const auto& [userId, link] : links_) {
        const auto other = next.find(userId);
        if (other == next.end() || !(other->second == link)) touch(link);
    }
    for (const auto& [userId, link] : next) {
        const auto other = links_.find(userId);
        if (other == links_.end() || !(other->second == link)) touch(link);
    }
    // Forget the ids of links that are gone so the table cannot grow without bound.
    std::erase_if(linkUserIds_, [&](const auto& entry) { return !next.contains(entry.second); });
    links_ = std::move(next);
    Rebuild(dirty);
}

bool NavMeshRuntime::UseRebuiltTile(NavTile tile) {
    if (!layout_ || tile.profile >= layout_->profiles.size() || tile.layers.size() > NavMeshAsset::MaxLayersPerTile) {
        return false;
    }
    for (const NavTileLayer& layer : tile.layers) {
        const std::size_t cells = static_cast<std::size_t>(layout_->tileCells) * layout_->tileCells;
        if (layer.heights.size() != cells || layer.areas.size() != cells || layer.connections.size() != cells) return false;
    }
    const TileKey key{ tile.profile, tile.coord };
    TileSlot& slot = tiles_[key];
    slot.rebuilt = std::move(tile);
    Place(key, slot);
    ++rebuilds_;
    ++revision_;
    return true;
}

std::size_t NavMeshRuntime::DropRebuiltTiles() {
    std::size_t dropped = 0U;
    for (auto slot = tiles_.begin(); slot != tiles_.end();) {
        if (!slot->second.rebuilt.has_value()) {
            ++slot;
            continue;
        }
        ++dropped;
        slot->second.rebuilt.reset();
        Unplace(slot->first, slot->second);
        if (slot->second.Used()) {
            Place(slot->first, slot->second);
            ++slot;
        } else {
            slot = tiles_.erase(slot);
        }
    }
    if (dropped != 0U) ++revision_;
    return dropped;
}

std::size_t NavMeshRuntime::ActiveTileCount(std::uint32_t profile) const noexcept {
    std::size_t count = 0U;
    for (const auto& [key, slot] : tiles_) {
        count += key.profile == profile && !slot.placed.empty() ? 1U : 0U;
    }
    return count;
}

std::size_t NavMeshRuntime::PolygonTileCount(std::uint32_t profile) const noexcept {
    std::size_t count = 0U;
    for (const auto& [key, slot] : tiles_) {
        count += key.profile == profile ? slot.placed.size() : 0U;
    }
    return count;
}

bool NavMeshRuntime::HasTile(std::uint32_t profile, NavTileCoord coord) const noexcept {
    const auto found = tiles_.find(TileKey{ profile, coord });
    return found != tiles_.end() && !found->second.placed.empty();
}

dtNavMesh* NavMeshRuntime::Mesh(std::uint32_t profile) noexcept {
    return profile < meshes_.size() ? meshes_[profile].mesh.get() : nullptr;
}

const dtNavMesh* NavMeshRuntime::Mesh(std::uint32_t profile) const noexcept {
    return profile < meshes_.size() ? meshes_[profile].mesh.get() : nullptr;
}

dtNavMeshQuery* NavMeshRuntime::Query(std::uint32_t profile) noexcept {
    return profile < meshes_.size() ? meshes_[profile].query.get() : nullptr;
}

const NavRuntimeLink* NavMeshRuntime::LinkForPolygon(std::uint32_t profile, dtPolyRef polygon) const {
    const dtNavMesh* mesh = Mesh(profile);
    const dtOffMeshConnection* connection = mesh != nullptr ? mesh->getOffMeshConnectionByRef(polygon) : nullptr;
    if (connection == nullptr) {
        return nullptr;
    }
    const auto found = links_.find(connection->userId);
    return found == links_.end() ? nullptr : &found->second;
}

void NavMeshRuntime::SetAreaCost(kb::scene::NavAreaId area, float cost) noexcept {
    if (kb::scene::IsValidNavArea(area) && std::isfinite(cost) && cost > 0.0F) {
        costs_[area] = cost;
    }
}

float NavMeshRuntime::AreaCost(kb::scene::NavAreaId area) const noexcept {
    return kb::scene::IsValidNavArea(area) ? costs_[area] : 0.0F;
}

void NavMeshRuntime::ConfigureFilter(NavAreaFilter& filter, kb::scene::NavAreaMask areas) const noexcept {
    filter.SetAreas(areas);
    for (std::uint32_t area = 0U; area < kb::scene::kNavAreaCount; ++area) {
        filter.setAreaCost(static_cast<int>(area + 1U), costs_[area]);
    }
}

unsigned short NavMeshRuntime::PolygonFlags(unsigned char areaCode) const noexcept {
    if (areaCode == 0U || areaCode > kb::scene::kNavAreaCount) {
        return 0U;
    }
    const kb::scene::NavAreaMask bit = kb::scene::NavAreaBit(static_cast<kb::scene::NavAreaId>(areaCode - 1U));
    unsigned short flags = 0U;
    for (int slot = 0; slot < kCrowdFilterCount; ++slot) {
        if ((filterMasks_[static_cast<std::size_t>(slot)] & bit) != 0U) flags = static_cast<unsigned short>(flags | (1U << slot));
    }
    return flags;
}

void NavMeshRuntime::RefreshPolygonFlags() {
    for (ProfileMesh& profile : meshes_) {
        dtNavMesh* mesh = profile.mesh.get();
        if (mesh == nullptr) continue;
        const dtNavMesh& view = *mesh;
        for (int index = 0; index < view.getMaxTiles(); ++index) {
            const dtMeshTile* tile = view.getTile(index);
            if (tile == nullptr || tile->header == nullptr) continue;
            const dtPolyRef base = view.getPolyRefBase(tile);
            for (int polygon = 0; polygon < tile->header->polyCount; ++polygon) {
                static_cast<void>(mesh->setPolyFlags(base | static_cast<dtPolyRef>(polygon), PolygonFlags(tile->polys[polygon].getArea())));
            }
        }
    }
}

int NavMeshRuntime::FilterSlot(kb::scene::NavAreaMask areas) {
    int free = -1;
    for (int slot = 0; slot < kCrowdFilterCount; ++slot) {
        const kb::scene::NavAreaMask mask = filterMasks_[static_cast<std::size_t>(slot)];
        if (mask != 0U && mask == areas) return slot;
        if (mask == 0U && free < 0) free = slot;
    }
    if (free < 0 || areas == 0U) {
        return -1;
    }
    filterMasks_[static_cast<std::size_t>(free)] = areas;
    RefreshPolygonFlags();
    return free;
}

void NavMeshRuntime::KeepFilterSlots(std::span<const kb::scene::NavAreaMask> used) {
    for (int slot = 1; slot < kCrowdFilterCount; ++slot) {
        kb::scene::NavAreaMask& mask = filterMasks_[static_cast<std::size_t>(slot)];
        if (mask != 0U && !std::ranges::binary_search(used, mask)) mask = 0U;
    }
}

void NavMeshRuntime::ConfigureCrowdFilter(dtQueryFilter& filter, int slot) const noexcept {
    filter.setIncludeFlags(static_cast<unsigned short>(1U << static_cast<unsigned>(std::clamp(slot, 0, kCrowdFilterCount - 1))));
    filter.setExcludeFlags(0U);
    for (std::uint32_t area = 0U; area < kb::scene::kNavAreaCount; ++area) {
        filter.setAreaCost(static_cast<int>(area + 1U), costs_[area]);
    }
}

std::uint64_t NavMeshRuntime::MeshGeneration(std::uint32_t profile) const noexcept {
    return profile < meshes_.size() ? meshes_[profile].generation : 0U;
}

std::optional<std::pair<dtPolyRef, Vec3>> NavMeshRuntime::Nearest(std::uint32_t profile, Vec3 position, Vec3 extents, const NavAreaFilter& filter) {
    dtNavMeshQuery* query = Query(profile);
    if (query == nullptr) {
        return std::nullopt;
    }
    float center[3];
    float half[3];
    float nearest[3];
    ToArray(position, center);
    ToArray(extents, half);
    dtPolyRef reference = 0U;
    if (dtStatusFailed(query->findNearestPoly(center, half, &filter, &reference, nearest)) || reference == 0U) {
        return std::nullopt;
    }
    return std::pair{ reference, FromArray(nearest) };
}

NavStraightPath NavMeshRuntime::FindPath(std::uint32_t profile, Vec3 start, Vec3 end, Vec3 extents, const NavAreaFilter& filter) {
    NavStraightPath result;
    dtNavMeshQuery* query = Query(profile);
    const auto from = Nearest(profile, start, extents, filter);
    const auto to = Nearest(profile, end, extents, filter);
    if (query == nullptr || !from || !to) {
        return result;
    }
    std::array<dtPolyRef, kMaxPathPolygons> polygons{};
    int polygonCount = 0;
    float startPoint[3];
    float endPoint[3];
    ToArray(from->second, startPoint);
    ToArray(to->second, endPoint);
    const dtStatus status = query->findPath(from->first, to->first, startPoint, endPoint, &filter, polygons.data(), &polygonCount, kMaxPathPolygons);
    if (dtStatusFailed(status) || polygonCount == 0) {
        return result;
    }
    const bool partial = polygons[static_cast<std::size_t>(polygonCount - 1)] != to->first;
    if (partial) {
        float closest[3];
        if (dtStatusFailed(query->closestPointOnPoly(polygons[static_cast<std::size_t>(polygonCount - 1)], endPoint, closest, nullptr))) {
            return result;
        }
        dtVcopy(endPoint, closest);
    }
    std::array<float, kMaxStraightCorners * 3> corners{};
    std::array<unsigned char, kMaxStraightCorners> flags{};
    std::array<dtPolyRef, kMaxStraightCorners> references{};
    int cornerCount = 0;
    if (dtStatusFailed(query->findStraightPath(startPoint, endPoint, polygons.data(), polygonCount, corners.data(), flags.data(),
            references.data(), &cornerCount, kMaxStraightCorners, 0))) {
        return result;
    }
    for (int corner = 0; corner < cornerCount; ++corner) {
        result.corners.push_back(FromArray(&corners[static_cast<std::size_t>(corner) * 3U]));
        result.linkStarts.push_back((flags[static_cast<std::size_t>(corner)] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) != 0U);
    }
    result.status = partial || dtStatusDetail(status, DT_PARTIAL_RESULT) ? kb::scene::NavPathStatus::Partial : kb::scene::NavPathStatus::Complete;
    return result;
}

NavRaycast NavMeshRuntime::Raycast(std::uint32_t profile, Vec3 start, Vec3 end, Vec3 extents, const NavAreaFilter& filter) {
    NavRaycast result;
    dtNavMeshQuery* query = Query(profile);
    const auto from = Nearest(profile, start, extents, filter);
    if (query == nullptr || !from) {
        return result;
    }
    float startPoint[3];
    float endPoint[3];
    float normal[3] = { 0.0F, 0.0F, 0.0F };
    ToArray(from->second, startPoint);
    ToArray(end, endPoint);
    float t = 0.0F;
    std::array<dtPolyRef, kMaxPathPolygons> visited{};
    int visitedCount = 0;
    if (dtStatusFailed(query->raycast(from->first, startPoint, endPoint, &filter, &t, normal, visited.data(), &visitedCount, kMaxPathPolygons))) {
        return result;
    }
    result.valid = true;
    result.hit = t <= 1.0F;
    result.fraction = result.hit ? t : 1.0F;
    Vec3 position = from->second + (end - from->second) * result.fraction;
    // The ray runs over the surface: take the height of the polygon it ends on.
    if (visitedCount > 0) {
        float height = position.y;
        float point[3];
        ToArray(position, point);
        if (dtStatusSucceed(query->getPolyHeight(visited[static_cast<std::size_t>(visitedCount - 1)], point, &height))) {
            position.y = height;
        }
    }
    result.position = position;
    result.normal = FromArray(normal);
    return result;
}

std::vector<Vec3> NavMeshRuntime::DebugTriangles(std::uint32_t profile) const {
    std::vector<Vec3> triangles;
    const dtNavMesh* mesh = Mesh(profile);
    if (mesh == nullptr) {
        return triangles;
    }
    for (int index = 0; index < mesh->getMaxTiles(); ++index) {
        const dtMeshTile* tile = mesh->getTile(index);
        if (tile == nullptr || tile->header == nullptr) continue;
        for (int polygonIndex = 0; polygonIndex < tile->header->polyCount; ++polygonIndex) {
            const dtPoly& polygon = tile->polys[polygonIndex];
            if (polygon.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;
            const dtPolyDetail& detail = tile->detailMeshes[polygonIndex];
            for (int triangle = 0; triangle < detail.triCount; ++triangle) {
                const unsigned char* corners = &tile->detailTris[(detail.triBase + static_cast<unsigned int>(triangle)) * 4U];
                for (int corner = 0; corner < 3; ++corner) {
                    const float* vertex = corners[corner] < polygon.vertCount
                        ? &tile->verts[polygon.verts[corners[corner]] * 3U]
                        : &tile->detailVerts[(detail.vertBase + corners[corner] - polygon.vertCount) * 3U];
                    triangles.push_back(FromArray(vertex));
                }
            }
        }
    }
    return triangles;
}

std::vector<kb::math::DVec3> NavMeshAssetTriangles(std::shared_ptr<const NavMeshAsset> asset, std::uint32_t profile) {
    std::vector<kb::math::DVec3> triangles;
    if (asset == nullptr || asset->tiles.empty()) {
        return triangles;
    }
    // Centred on the first tile, so the polygons keep float precision however far out they lie.
    const double tileSize = asset->settings.TileWorldSize();
    const kb::math::DVec3 origin{ static_cast<double>(asset->tiles.front().coord.x) * tileSize, 0.0,
        static_cast<double>(asset->tiles.front().coord.z) * tileSize };
    NavMeshRuntime runtime;
    runtime.SetOrigin(origin);
    std::string error;
    if (runtime.AddTileSet(std::move(asset), error) == 0U) {
        return triangles;
    }
    for (const Vec3& corner : runtime.DebugTriangles(profile)) {
        triangles.push_back(origin + corner);
    }
    return triangles;
}

double NavMeshRuntime::WalkableArea(std::uint32_t profile) const {
    const std::vector<Vec3> triangles = DebugTriangles(profile);
    double area = 0.0;
    for (std::size_t index = 0U; index + 2U < triangles.size(); index += 3U) {
        const double ax = triangles[index + 1U].x - triangles[index].x;
        const double az = triangles[index + 1U].z - triangles[index].z;
        const double bx = triangles[index + 2U].x - triangles[index].x;
        const double bz = triangles[index + 2U].z - triangles[index].z;
        area += std::fabs(ax * bz - az * bx) * 0.5;
    }
    return area;
}

} // namespace kb::navigation
