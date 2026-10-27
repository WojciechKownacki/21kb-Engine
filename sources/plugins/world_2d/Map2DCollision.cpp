#include "Map2DCollision.hpp"
#include "engine/assets/CollisionMeshAsset.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include <algorithm>
#include <numeric>
#include <set>
#include <sstream>
#include <iomanip>

namespace kb::world2d {
namespace {
using namespace kb::assets;
using namespace kb::scene;
// The outline of a cell's shape in cell units (positive down), counter-clockwise on screen once flipped, and its triangles.
struct Triangulated {
    std::vector<std::array<float, 2>> polygon;
    std::vector<std::array<unsigned, 3>> faces;
};

std::optional<Triangulated> Triangulate(const Map2DCollision& shape) {
    auto polygon = shape.polygon;
    if (shape.kind == Map2DCollisionKind::SlopeUp) polygon = {{0,1}, {1,0}, {1,1}};
    if (shape.kind == Map2DCollisionKind::SlopeDown) polygon = {{0,0}, {1,1}, {0,1}};
    float area = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()]; area += b[0] * a[1] - a[0] * b[1];
    }
    if (area < 0) std::ranges::reverse(polygon);
    const auto cross = [](auto a, auto b, auto c) { return (b[1]-a[1])*(c[0]-a[0]) - (b[0]-a[0])*(c[1]-a[1]); };
    std::vector<unsigned> remaining(polygon.size()); std::iota(remaining.begin(), remaining.end(), 0U);
    std::vector<std::array<unsigned, 3>> faces;
    while (remaining.size() > 2) {
        bool clipped = false;
        for (std::size_t i = 0; i < remaining.size(); ++i) {
            const auto a = remaining[(i + remaining.size() - 1) % remaining.size()], b = remaining[i], c = remaining[(i + 1) % remaining.size()];
            if (cross(polygon[a], polygon[b], polygon[c]) <= 0.000001F) continue;
            bool contains = false;
            for (const auto p : remaining) {
                if (p == a || p == b || p == c) continue;
                if (cross(polygon[a], polygon[b], polygon[p]) >= 0 && cross(polygon[b], polygon[c], polygon[p]) >= 0 && cross(polygon[c], polygon[a], polygon[p]) >= 0) { contains = true; break; }
            }
            if (contains) continue;
            faces.push_back({a,b,c}); remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i)); clipped = true; break;
        }
        if (!clipped) return {};
    }
    return Triangulated{ std::move(polygon), std::move(faces) };
}

std::optional<CollisionMeshAsset> Extrude(const Map2DCollision& shape, const Map2DAsset& map) {
    const auto triangulated = Triangulate(shape);
    if (!triangulated) return {};
    const auto& polygon = triangulated->polygon;
    const auto& faces = triangulated->faces;
    CollisionMeshAsset mesh;
    const auto count = static_cast<unsigned>(polygon.size());
    for (int side = 0; side < 2; ++side) for (const auto point : polygon) {
        const float x = (point[0] - 0.5F) * map.tileSize, y = (0.5F - point[1]) * map.tileSize;
        const float depth = (side == 0 ? -0.5F : 0.5F) * map.tileSize;
        mesh.positions.push_back(map.orientation == Map2DOrientation::Wall ? Vec3{x,y,depth} : Vec3{x,-depth,y});
    }
    for (auto face : faces) {
        mesh.indices.insert(mesh.indices.end(), {face[2],face[1],face[0],face[0]+count,face[1]+count,face[2]+count});
    }
    for (unsigned i = 0; i < count; ++i) {
        const auto j = (i + 1) % count;
        mesh.indices.insert(mesh.indices.end(), {i,j,i+count,j,j+count,i+count});
    }
    std::string error;
    return ValidateCollisionMesh(mesh, error) ? std::optional{std::move(mesh)} : std::nullopt;
}
struct Area {
    unsigned x = 0U, y = 0U, endX = 0U, endY = 0U;
    std::int32_t floor = 0;
    Map2DCollision shape;
};

// Neighbouring cells with the same shape become one area: solids and ledges along a row, solids also down a column.
std::vector<Area> MergeAreas(const Map2DAsset& map, bool groundFloorOnly) {
    std::vector<Area> areas;
    std::map<std::uint64_t, Map2DCollision> pending;
    for (const auto& [key, tile] : map.tiles) {
        if (groundFloorOnly && Map2DTileKeyZ(key) != 0) continue;
        const auto shape = Map2DCellCollision(map, tile);
        if (shape.kind != Map2DCollisionKind::None) pending.emplace(key, shape);
    }
    while (!pending.empty()) {
        const auto key = pending.begin()->first;
        const auto shape = pending.begin()->second;
        const auto x = Map2DTileKeyX(key), y = Map2DTileKeyY(key); const auto floor = Map2DTileKeyZ(key);
        auto endX = x + 1U, endY = y + 1U;
        const auto matches = [&](unsigned cx, unsigned cy) { const auto it = pending.find(Map2DTileKey(cx,cy,floor)); return it != pending.end() && it->second == shape; };
        if (shape.kind == Map2DCollisionKind::Solid || shape.kind == Map2DCollisionKind::OneWay) {
            while (endX < map.width && matches(endX, y)) ++endX;
            if (shape.kind == Map2DCollisionKind::Solid) while (endY < map.height) {
                bool complete = true; for (auto col = x; col < endX; ++col) if (!matches(col,endY)) { complete = false; break; }
                if (!complete) break; ++endY;
            }
        }
        for (auto row = y; row < endY; ++row) for (auto col = x; col < endX; ++col) pending.erase(Map2DTileKey(col,row,floor));
        areas.push_back({ x, y, endX, endY, floor, shape });
    }
    return areas;
}
}


bool BuildMap2DCollision(kb::scene::Scene& scene, kb::scene::SceneEntity owner, const kb::assets::Map2DAsset& map,
    std::vector<kb::scene::ColliderComponent>& output, std::vector<kb::assets::AssetId>* runtimeAssets, std::string* error) {
    using namespace kb::assets; using namespace kb::scene;
    const auto fail = [&](std::string message) { if (error) *error = std::move(message); return false; };
    for (const auto& area : MergeAreas(map, false)) {
        const auto x = area.x, y = area.y, endX = area.endX, endY = area.endY; const auto floor = area.floor; const auto& shape = area.shape;
        const float size = map.tileSize, cx = (static_cast<float>(x + endX) - static_cast<float>(map.width)) * size * 0.5F;
        const float cy = static_cast<float>(y + endY) * size * 0.5F, lift = static_cast<float>(floor) * map.floorHeight;
        ColliderComponent collider; collider.trigger = shape.trigger; collider.friction = shape.friction; collider.restitution = shape.restitution;
        if (map.orientation == Map2DOrientation::Wall) {
            collider.center = {cx, static_cast<float>(map.height) * size - cy, -lift};
            collider.boxSize = {static_cast<float>(endX - x) * size, static_cast<float>(endY - y) * size, size};
        } else {
            collider.center = {cx, lift + size * 0.5F, static_cast<float>(map.height) * size * 0.5F - cy};
            collider.boxSize = {static_cast<float>(endX - x) * size, size, static_cast<float>(endY - y) * size};
        }
        if (shape.kind == Map2DCollisionKind::OneWay) {
            collider.shape = ColliderShape::OneWayBox;
            const float thickness = std::max(0.02F, size * 0.05F);
            collider.center.y += (collider.boxSize.y - thickness) * 0.5F; collider.boxSize.y = thickness;
        } else if (shape.kind != Map2DCollisionKind::Solid) {
            const auto mesh = Extrude(shape, map);
            if (!mesh) return fail("Collision outline could not be triangulated");
            std::ostringstream identity; identity << std::setprecision(9) << static_cast<int>(map.orientation) << ':' << size << ':' << static_cast<int>(shape.kind);
            for (const auto p : shape.polygon) identity << ':' << p[0] << ',' << p[1];
            const auto hash = MakeAssetId(identity.str()).value;
            auto& manager = scene.Assets().Manager();
            const auto path = runtimeAssets ? "/Runtime/TilemapCollision/" + std::to_string(owner.Id()) + "/" + std::to_string(hash)
                : "/Game/Maps/Collision/Shape_" + std::to_string(hash) + ".kbcollision";
            const auto id = MakeAssetId(path + ":CollisionMesh");
            if (runtimeAssets) {
                if (!manager.Registry().Find(id) && !manager.RegisterGeneratedAsset({.id = id, .type = "CollisionMesh", .virtualPath = path, .contentHash = hash})) return fail(manager.LastError());
                if (!manager.AcquireLoaded<CollisionMeshAsset>(id).IsLoaded() && !manager.PublishRuntimeAsset(id, std::make_shared<CollisionMeshAsset>(*mesh))) return fail(manager.LastError());
                if (std::ranges::find(*runtimeAssets, id) == runtimeAssets->end()) runtimeAssets->push_back(id);
            } else if (!manager.Registry().Find(id)) {
                const auto file = manager.Mounts().Resolve(path);
                if (!file) return fail("A project Assets mount is needed to save collision geometry");
                std::error_code ec; std::filesystem::create_directories(file->parent_path(), ec);
                std::string writeError;
                if (ec || !WriteCollisionMesh(*file, *mesh, writeError)) return fail(ec ? ec.message() : writeError);
                if (!manager.RegisterAsset({.id = id, .type = "CollisionMesh", .virtualPath = path, .physicalPath = *file, .contentHash = hash})) return fail(manager.LastError());
            }
            collider.shape = ColliderShape::Mesh; collider.meshAssetId = id.value;
        }
        output.push_back(collider);
        if (output.size() > 8192U) return fail("Too many separate collision areas; join adjacent areas or split the map into layers");
    }
    return true;
}
bool BuildMap2DShapes2D(const kb::assets::Map2DAsset& map, std::vector<Map2DShape2D>& output, std::string* error) {
    using namespace kb::assets;
    const float size = map.tileSize;
    const bool wall = map.orientation == Map2DOrientation::Wall;
    // The map's origin is the middle of its bottom edge (a wall) or of its centre line (flat): the same as the 3D colliders.
    const float baseline = static_cast<float>(map.height) * size * (wall ? 1.0F : 0.5F);
    const auto cellX = [&](float column) { return (column - static_cast<float>(map.width) * 0.5F) * size; };
    const auto cellY = [&](float row) { return baseline - row * size; };
    for (const auto& area : MergeAreas(map, true)) {
        const auto& shape = area.shape;
        Map2DShape2D out;
        out.trigger = shape.trigger;
        out.friction = shape.friction;
        out.restitution = shape.restitution;
        if (shape.kind == Map2DCollisionKind::Solid || shape.kind == Map2DCollisionKind::OneWay) {
            const float left = cellX(static_cast<float>(area.x)), right = cellX(static_cast<float>(area.endX));
            float top = cellY(static_cast<float>(area.y)), bottom = cellY(static_cast<float>(area.endY));
            if (shape.kind == Map2DCollisionKind::OneWay) {
                out.oneWay = true;
                bottom = top - std::max(0.02F, size * 0.05F);
            }
            out.points = { { left, bottom }, { right, bottom }, { right, top }, { left, top } };
            output.push_back(std::move(out));
        } else {
            const auto triangulated = Triangulate(shape);
            if (!triangulated) {
                if (error) *error = "Collision outline could not be triangulated";
                return false;
            }
            for (const auto& face : triangulated->faces) {
                Map2DShape2D piece = out;
                for (const unsigned index : face) {
                    const auto& point = triangulated->polygon[index];
                    piece.points.push_back({ cellX(static_cast<float>(area.x) + point[0]), cellY(static_cast<float>(area.y) + point[1]) });
                }
                output.push_back(std::move(piece));
            }
        }
        if (output.size() > 65536U) {
            if (error) *error = "Too many separate collision areas";
            return false;
        }
    }
    return true;
}

} // namespace kb::world2d
