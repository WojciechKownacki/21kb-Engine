#include "Camera2D.hpp"
#include "Map2DCollision.hpp"
#include "Map2DEditing.hpp"
#include "Map2DTileset.hpp"
#include "Map2DView.hpp"
#include "Map2DRuntime.hpp"

#include "engine/assets/Map2DAsset.hpp"
#include "engine/assets/Map2DNavigation.hpp"
#include "engine/assets/CollisionMeshAsset.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptMap2DApi.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneHistory.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "kb/render/resources/RenderMap2DMeshBuilder.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"
#include "kb/render/scene/RenderScene.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <memory>
#include <set>
#include <variant>

namespace {

using kb::assets::Map2DAsset;
using kb::assets::Map2DSprite;
using kb::assets::Map2DTileKey;
using kb::world2d::Map2DBorderPiece;

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

// Only the header: the editing code reads a sheet's size, never its pixels.
void WritePngHeader(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height) {
    std::filesystem::create_directories(path.parent_path());
    std::array<unsigned char, 33U> bytes{ 0x89U, 'P', 'N', 'G', 0x0DU, 0x0AU, 0x1AU, 0x0AU, 0U, 0U, 0U, 13U, 'I', 'H', 'D', 'R' };
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[16U + index] = static_cast<unsigned char>(width >> (24U - 8U * index));
        bytes[20U + index] = static_cast<unsigned char>(height >> (24U - 8U * index));
    }
    bytes[24U] = 8U;
    bytes[25U] = 6U;
    std::ofstream output{ path, std::ios::binary };
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

[[nodiscard]] const kb::assets::Map2DTile* Tile(const Map2DAsset& map, std::uint32_t x, std::uint32_t y) {
    const auto found = map.tiles.find(Map2DTileKey(x, y, 0U));
    return found == map.tiles.end() ? nullptr : &found->second;
}

[[nodiscard]] bool HasBorder(const Map2DAsset& map, std::uint32_t x, std::uint32_t y, std::uint32_t cell) {
    const kb::assets::Map2DTile* tile = Tile(map, x, y);
    if (tile == nullptr) return false;
    for (const Map2DSprite sprite : tile->borders) {
        if (sprite.cell == cell) return true;
    }
    return false;
}

// Memory the process holds now, in bytes; zero where it cannot be read.
[[nodiscard]] std::size_t WorkingSetBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) return static_cast<std::size_t>(counters.WorkingSetSize);
#endif
    return 0U;
}

} // namespace

int main() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "kb_world2d_tests";
    std::filesystem::remove_all(root);
    const std::filesystem::path assets = root / "Assets";
    WritePngHeader(assets / "Sprites" / "ground.png", 256U, 256U);

    // A tileset: grass (an island of borders at cell 16, a hole at cell 19) over dirt.
    kb::world2d::Map2DTileset tileset{};
    kb::world2d::Map2DGroundBrush grass{ .name = "Grass", .zOrder = 20, .texture = "/Game/Sprites/ground.png", .cellSize = 32U, .tiles = { 1U } };
    kb::world2d::AssignMap2DIslandBorders(grass, 16U, 8U);
    kb::world2d::AssignMap2DHoleBorders(grass, 19U, 8U);
    Require(grass.tiles.front() == 25U && grass.tiles.size() == 2U, "the middle of an island must become the ground");
    Require(grass.Border(Map2DBorderPiece::S) == 17U && grass.Border(Map2DBorderPiece::CornerSE) == 16U &&
            grass.Border(Map2DBorderPiece::N) == 33U && grass.Border(Map2DBorderPiece::CornerNW) == 34U &&
            grass.Border(Map2DBorderPiece::DiagonalNW) == 19U && grass.Border(Map2DBorderPiece::DiagonalSE) == 28U,
        "island and hole blocks must fill the RME border pieces");
    grass.tiles = { 25U };
    tileset.grounds.push_back(grass);
    tileset.grounds.push_back(kb::world2d::Map2DGroundBrush{ .name = "Dirt", .zOrder = 10, .texture = "/Game/Sprites/ground.png", .cellSize = 32U, .tiles = { 8U } });
    Require(kb::world2d::Map2DTilesetIO::Save(assets / "Maps" / "Ground.kbtileset", tileset), "a tileset must save");
    const std::optional<kb::world2d::Map2DTileset> reloaded = kb::world2d::Map2DTilesetIO::Load(assets / "Maps" / "Ground.kbtileset");
    Require(reloaded.has_value() && reloaded->grounds.size() == 2U && reloaded->grounds[0].borders == grass.borders &&
            reloaded->grounds[0].zOrder == 20, "a tileset must load what it saved");

    const kb::world2d::Map2DBrushLibrary library = kb::world2d::Map2DBrushLibrary::Load(assets);
    const kb::world2d::Map2DGroundBrush* grassBrush = library.Find("Grass");
    const kb::world2d::Map2DGroundBrush* dirtBrush = library.Find("Dirt");
    Require(grassBrush != nullptr && dirtBrush != nullptr, "the library must find the project's brushes");

    // Grass on an empty map borders onto the void around it, like Remere's Map Editor.
    {
        Map2DAsset map = kb::assets::MakeEmptyMap2DAsset(12U, 12U);
        kb::world2d::Map2DEditor editor{ map, library, assets };
        Require(editor.PaintGround(*grassBrush, { 5U, 5U, 5U, 5U }, 0U), "grass must paint");
        Require(map.sheets.size() == 1U && map.sheets[0].columns == 8U && map.sheets[0].rows == 8U, "the sheet must be cut into 32 px cells");
        Require(std::filesystem::exists(kb::world2d::Map2DAssetFile(assets, map.sheets[0].material)), "the sheet must get its sprite material");
        Require(map.tiles.size() == 9U, "a painted tile and its eight bordered neighbours");
        Require(HasBorder(map, 5U, 4U, 17U), "the tile north of the grass shows the s piece");
        Require(HasBorder(map, 4U, 4U, 16U), "the tile north-west of the grass shows the cse corner");
        Require(HasBorder(map, 6U, 6U, 34U), "the tile south-east of the grass shows the cnw corner");
        Require(Tile(map, 5U, 5U)->borders.empty(), "grass does not border onto itself");

        // Grass north and west of a tile meet in one diagonal piece; north and east too.
        Require(editor.PaintGround(*grassBrush, { 5U, 7U, 5U, 7U }, 0U) && editor.PaintGround(*grassBrush, { 4U, 8U, 4U, 8U }, 0U) &&
                editor.PaintGround(*grassBrush, { 6U, 8U, 6U, 8U }, 0U), "grass must paint");
        const kb::assets::Map2DTile* corner = Tile(map, 5U, 8U);
        Require(corner != nullptr && HasBorder(map, 5U, 8U, 19U) && HasBorder(map, 5U, 8U, 20U) && !HasBorder(map, 5U, 8U, 33U),
            "grass on two sides of a tile uses the diagonal pieces instead of the edges");
    }

    // Dirt everywhere, grass on it: the lower dirt shows the grass borders, and undo restores it.
    {
        Map2DAsset map = kb::assets::MakeEmptyMap2DAsset(10U, 10U);
        kb::world2d::Map2DEditor editor{ map, library, assets };
        Require(editor.FillGround(*dirtBrush, 0U, 0U, 0U) == 100U, "flood fill must cover the empty map");
        Require(editor.FillGround(*dirtBrush, 3U, 3U, 0U) == 0U, "filling a ground with itself changes nothing");
        kb::world2d::Map2DChangeSet changes;
        editor.SetChangeSet(&changes);
        Require(editor.PaintGround(*grassBrush, { 4U, 4U, 5U, 5U }, 0U), "a 2x2 grass patch must paint");
        changes.Finish(map);
        editor.SetChangeSet(nullptr);
        Require(!changes.Empty(), "the stroke must be recorded");
        Require(Tile(map, 4U, 4U)->ground.cell == 25U && HasBorder(map, 4U, 3U, 17U) && HasBorder(map, 3U, 3U, 16U),
            "dirt next to grass shows the grass borders");
        Require(Tile(map, 4U, 3U)->ground.cell == 8U, "a border lies over the dirt, which stays");
        const Map2DAsset painted = map;
        changes.Undo(map);
        Require(Tile(map, 4U, 4U)->ground.cell == 8U && Tile(map, 4U, 3U)->borders.empty(), "undo must restore the dirt");
        changes.Redo(map);
        Require(map.tiles == painted.tiles, "redo must repeat the stroke");

        editor.Erase({ 4U, 4U, 4U, 4U }, 0U, false);
        Require(Tile(map, 4U, 4U) == nullptr || !Tile(map, 4U, 4U)->ground.IsValid(), "the eraser clears the tile's ground");
        Require(editor.PaintSprite("/Game/Sprites/ground.png", 32U, 40U, false, { 2U, 2U, 2U, 2U }, 0U), "an item must stack");
        Require(Tile(map, 2U, 2U)->items.size() == 1U && Tile(map, 2U, 2U)->ground.cell == 8U, "an item lies on the ground");
        editor.Erase({ 2U, 2U, 2U, 2U }, 0U, true);
        Require(Tile(map, 2U, 2U)->items.empty() && Tile(map, 2U, 2U)->ground.IsValid(), "erasing items keeps the ground");

        // The text file keeps every tile; the render mesh has a quad per sprite and the background.
        Require(kb::assets::Map2DAssetIO::Save(assets / "Maps" / "World.kbmap2d", map), "the map must save");
        const std::optional<Map2DAsset> loaded = kb::assets::Map2DAssetIO::Load(assets / "Maps" / "World.kbmap2d");
        Require(loaded.has_value() && loaded->tiles == map.tiles && loaded->sheets.size() == 1U &&
                loaded->sheets[0].material == map.sheets[0].material, "the map must load what it saved");
        std::size_t sprites = 0U;
        for (const auto& [key, tile] : map.tiles) sprites += (tile.ground.IsValid() ? 1U : 0U) + tile.borders.size() + tile.items.size();
        const std::optional<kb::render::RenderMeshAssetData> mesh = kb::render::RenderMap2DMeshBuilder::Build(map);
        Require(mesh.has_value() && mesh->sections.size() == 2U && mesh->tangentVertices.size() == 4U * (sprites + 1U) &&
                mesh->materialSlots.size() == 2U && mesh->embeddedMaterials.size() == 2U &&
                mesh->materialSlots[1].defaultMaterialAssetId == kb::render::RenderMap2DMeshBuilder::MaterialAssetId(map.sheets[0].material),
            "the mesh draws the background and one section per sheet with its material");
    }

    // Identically named sheets from different folders must keep different materials.
    {
        WritePngHeader(assets / "Other" / "ground.png", 64U, 64U);
        Map2DAsset map = kb::assets::MakeEmptyMap2DAsset(4U, 4U);
        const auto first = kb::world2d::EnsureMap2DSheet(map, assets, "/Game/Sprites/ground.png", 32U);
        const auto second = kb::world2d::EnsureMap2DSheet(map, assets, "/Game/Other/ground.png", 32U);
        Require(first && second && map.sheets[*first].material != map.sheets[*second].material,
            "sheet material paths must include the texture's folder identity");
        const auto imported = kb::world2d::ImportMap2DImage(assets, assets / "Sprites" / "ground.png");
        const auto other = kb::world2d::ImportMap2DImage(assets, assets / "Other" / "ground.png");
        Require(imported && other && *imported != *other, "import must keep both same-named images");
        Require(kb::world2d::ReadMap2DImageSize(kb::world2d::Map2DAssetFile(assets, *imported))->first == 256U,
            "import must never overwrite an earlier image");
        WritePngHeader(assets / "Sprites" / "partial.png", 65U, 64U);
        Require(!kb::world2d::EnsureMap2DSheet(map, assets, "/Game/Sprites/partial.png", 32U),
            "partial cells must not distort sprite UVs");
    }

    // Plain palette tiles fill without a brush; resizing and its removed tiles undo together.
    {
        Map2DAsset map = kb::assets::MakeEmptyMap2DAsset(4U, 3U);
        kb::world2d::Map2DEditor editor{ map, library, assets };
        Require(editor.FillSprite("/Game/Sprites/ground.png", 32U, 2U, 0U, 0U, 0) == 12U,
            "a plain floor tile must flood fill");
        Require(editor.FillSprite("/Game/Sprites/ground.png", 32U, 2U, 0U, 0U, 0) == 0U,
            "filling the same tile must be a no-op");
        const auto original = map.tiles;
        kb::world2d::Map2DChangeSet resize;
        kb::world2d::ResizeMap2D(map, 2U, 2U, &resize);
        resize.Finish(map);
        Require(!resize.Empty() && map.tiles.size() == 4U, "resize must record the removed tiles");
        resize.Undo(map);
        Require(map.width == 4U && map.height == 3U && map.tiles == original, "undo resize must restore every tile and the dimensions");
        resize.Redo(map);
        Require(map.width == 2U && map.height == 2U && map.tiles.size() == 4U, "redo must shrink again");
    }

    // Painted collision is independent of graphics and becomes merged, persistent scene boxes.
    {
        Map2DAsset map = kb::assets::MakeEmptyMap2DAsset(8U, 8U);
        kb::world2d::Map2DEditor editor{ map, library, assets };
        kb::world2d::Map2DChangeSet change;
        editor.SetChangeSet(&change);
        editor.PaintCollision({ 1U, 2U, 4U, 3U }, 0, true);
        change.Finish(map);
        const auto loaded = kb::assets::Map2DAssetIO::Parse(kb::assets::Map2DAssetIO::Write(map));
        Require(loaded && loaded->tiles == map.tiles && loaded->tiles.size() == 8U, "invisible solid tiles must round-trip");
        kb::scene::Scene scene;
        const auto owner = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "World" });
        bool changed = false;
        Require(kb::world2d::SetUpMap2DCollision(scene, owner.Entity(), map, &changed) && changed, "walls must produce colliders");
        const auto group = owner.Children().front();
        Require(group.Children().size() == 1U, "eight adjacent solid tiles must merge into one box");
        const auto* flat = scene.Components().Colliders().TryGet(group.Children().front().Entity());
        Require(flat && flat->boxSize.x == 4.0F && flat->boxSize.z == 2.0F && flat->center.y == 0.5F,
            "top-down walls must rise above the map");
        Require(kb::world2d::SetUpMap2DCollision(scene, owner.Entity(), map, &changed) && !changed,
            "unchanged collisions must keep their entities");
        map.orientation = kb::assets::Map2DOrientation::Wall;
        Require(kb::world2d::SetUpMap2DCollision(scene, owner.Entity(), map), "platform collision must rebuild");
        const auto* wall = scene.Components().Colliders().TryGet(owner.Children().front().Children().front().Entity());
        Require(wall && wall->boxSize.x == 4.0F && wall->boxSize.y == 2.0F && wall->center.y == 5.0F,
            "side-view platforms must cover the painted rows");
        change.Undo(map);
        Require(map.tiles.empty() && kb::world2d::SetUpMap2DCollision(scene, owner.Entity(), map) && owner.Children().empty(),
            "undoing solid painting must remove the generated colliders");
        change.Redo(map);
        Require(map.tiles.size() == 8U, "redo must restore solid tiles");
    }

    // Floors -50..50, a wall map and the client box survive the file; north is the top of a camera
    // looking down (+Z) on the ground and the top of the wall (+Y).
    {
        Map2DAsset map = kb::assets::MakeEmptyMap2DAsset(4U, 3U);
        map.orientation = kb::assets::Map2DOrientation::Wall;
        map.viewWidth = 15U;
        map.viewHeight = 10U;
        kb::world2d::Map2DEditor editor{ map, library, assets };
        Require(editor.PaintGround(*dirtBrush, { 0U, 0U, 0U, 0U }, -50) && editor.PaintGround(*dirtBrush, { 3U, 2U, 3U, 2U }, 50),
            "the lowest and highest floors must paint");
        Require(kb::assets::Map2DTileKeyZ(map.tiles.begin()->first) == -50 && kb::assets::Map2DTileKeyZ(map.tiles.rbegin()->first) == 50,
            "floors sort from the lowest up");
        const std::string text = kb::assets::Map2DAssetIO::Write(map);
        const std::optional<Map2DAsset> loaded = kb::assets::Map2DAssetIO::Parse(text);
        Require(loaded.has_value() && loaded->tiles == map.tiles && loaded->orientation == kb::assets::Map2DOrientation::Wall &&
                loaded->viewWidth == 15U && loaded->viewHeight == 10U, "floors, orientation and view must load");
        Require(!kb::assets::Map2DAssetIO::Parse("21kb-map2d 1\nsize 4 4\nt 0 0 51 g=0:0\n").has_value(), "floor 51 is outside the map");

        map.background = false;
        const std::optional<kb::render::RenderMeshAssetData> wall = kb::render::RenderMap2DMeshBuilder::Build(map);
        Require(wall.has_value() && wall->tangentVertices.size() == 8U, "two tiles, two quads");
        const auto& low = wall->tangentVertices[0];
        Require(low.nz < 0.0F && low.z > 140.0F && low.y >= 1.99F && low.y <= 2.01F,
            "a wall faces -Z, the row 0 tile stands at the top (its lower edge at y 2), floor -50 150 m behind");
        map.orientation = kb::assets::Map2DOrientation::Flat;
        const std::optional<kb::render::RenderMeshAssetData> flat = kb::render::RenderMap2DMeshBuilder::Build(map);
        Require(flat.has_value() && flat->tangentVertices[0].ny > 0.0F && flat->tangentVertices[0].z >= 0.49F && flat->tangentVertices[0].z <= 0.51F && flat->tangentVertices[0].x <= -1.99F &&
                flat->tangentVertices[0].y < -149.0F, "a flat map's row 0 lies north (+Z), floor -50 lies 150 m down");
    }

    // The 2D view: a Map Camera under the map, high above its middle, looking straight down with
    // north (+Z) at the top; run again it moves the same camera in front of a wall map.
    {
        kb::scene::Scene scene;
        const kb::scene::SceneObject owner = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "World" });
        Map2DAsset map = kb::assets::MakeEmptyMap2DAsset(64U, 64U);
        kb::world2d::Map2DViewResult view{};
        Require(kb::world2d::SetUpMap2DView(scene, owner.Entity(), map, assets, &view) && view.cameraCreated && view.profileAssigned,
            "the 2D view must make a camera and a profile");
        static_cast<void>(scene.Runtime().Update(0.0F));
        const kb::scene::TransformComponent placed = scene.Transforms().Get(view.camera);
        const kb::scene::CameraComponent* lens = scene.Components().Cameras().TryGet(view.camera);
        Require(lens != nullptr && lens->primary && lens->projection == kb::scene::CameraProjection::Orthographic &&
                lens->orthographicHeight > 10.9F && lens->orthographicHeight < 11.1F, "an orthographic primary camera framing 11 tiles");
        Require(placed.worldPosition.y > 100.0F && std::abs(placed.worldPosition.x) < 0.01F && std::abs(placed.worldPosition.z) < 0.01F,
            "the camera hangs above the middle of the map");
        // Forward is the rotated +Z; it must point down (-Y).
        const kb::scene::Quat q = placed.worldRotation;
        const float forwardY = 2.0F * (q.y * q.z - q.w * q.x);
        Require(forwardY < -0.99F, "the camera looks straight down");
        Require(std::filesystem::exists(assets / "Maps" / "PP_Map2D.kbppfx"), "the pixel profile is written");
        map.orientation = kb::assets::Map2DOrientation::Wall;
        Require(kb::world2d::SetUpMap2DView(scene, owner.Entity(), map, assets, &view) && !view.cameraCreated, "run again it keeps the camera");
        static_cast<void>(scene.Runtime().Update(0.0F));
        const kb::scene::TransformComponent wall = scene.Transforms().Get(view.camera);
        Require(wall.worldPosition.z < -100.0F && std::abs(wall.worldRotation.x) < 0.01F && wall.worldPosition.y > 31.0F,
            "in front of a wall map the camera looks along +Z at its middle");
    }

    // A sheet's palette pieces: empty cells left out, a floor tile alone, a drawing that spans cells
    // (a tree) joined into one object unless the sheet keeps sprites separate, used cells skipped.
    {
        constexpr std::uint32_t kCell = 4U;
        constexpr std::uint32_t kWidth = 4U * kCell;
        constexpr std::uint32_t kHeight = 2U * kCell;
        std::vector<std::uint32_t> pixels(kWidth * kHeight, 0U);
        const auto paint = [&](std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
            for (std::uint32_t y = y0; y < y1; ++y) {
                for (std::uint32_t x = x0; x < x1; ++x) pixels[y * kWidth + x] = 0xFF336633U;
            }
        };
        paint(0U, 0U, 4U, 4U);      // cell 0: a whole floor tile
        paint(6U, 1U, 12U, 3U);     // cells 1 and 2: one drawing over their shared edge
        paint(1U, 5U, 3U, 7U);      // cell 4: a small item
        paint(13U, 5U, 15U, 7U);    // cell 7: used by a brush
        const std::vector<std::uint32_t> skip{ 7U };
        const std::vector<kb::world2d::Map2DSheetPiece> joined = kb::world2d::FindMap2DSheetPieces(pixels, kWidth, kHeight, kCell, false, skip);
        Require(joined.size() == 3U, "a floor tile, a joined drawing and an item");
        Require(joined[0].firstCell == 0U && joined[0].opaque && joined[0].columns == 1U, "a full cell is a floor tile");
        Require(joined[1].firstCell == 1U && joined[1].columns == 2U && joined[1].rows == 1U && joined[1].cells.size() == 2U && !joined[1].opaque,
            "sprites that touch join into one object");
        Require(joined[2].firstCell == 4U && joined[2].columns == 1U, "a lone sprite stays an item");
        const std::vector<kb::world2d::Map2DSheetPiece> separate = kb::world2d::FindMap2DSheetPieces(pixels, kWidth, kHeight, kCell, true, skip);
        Require(separate.size() == 4U, "a separate sheet keeps every sprite single");
        const auto origin = kb::world2d::Map2DObjectOrigin(3U, 2U, 10, 10);
        Require(origin.first == 9 && origin.second == 9, "an object stands with its bottom middle tile on the clicked one");

        const unsigned atlasWidth = 9, atlasHeight = 5;
        std::vector<unsigned> atlas(atlasWidth * atlasHeight, 0xFFFFFFFFU);
        const auto rectangular = kb::world2d::FindMap2DSheetPieces(atlas, atlasWidth, atlasHeight, 3, true, {}, 3, 1, 1);
        Require(rectangular.size() == 2 && rectangular[1].firstCell == 1, "atlas margins and spacing are not treated as sprites");
        kb::world2d::Map2DTileset withObject{};
        withObject.objects.push_back(kb::world2d::Map2DObjectBrush{ .name = "Oak", .texture = "/Game/2D Map Sprites/Nature/trees.png", .firstCell = 5U, .columns = 2U, .rows = 2U });
        withObject.sheets.push_back(kb::world2d::Map2DSheetSetting{ .texture = "/Game/2D Map Sprites/Nature/trees.png", .cellSize = 64U, .separate = true });
        const std::optional<kb::world2d::Map2DTileset> parsed = kb::world2d::Map2DTilesetIO::Parse(kb::world2d::Map2DTilesetIO::Write(withObject));
        Require(parsed.has_value() && parsed->objects.size() == 1U && parsed->objects[0].columns == 2U && parsed->objects[0].firstCell == 5U &&
                parsed->sheets.size() == 1U && parsed->sheets[0].cellSize == 64U && parsed->sheets[0].separate, "objects and sheet settings round-trip");
        Require(kb::world2d::Map2DCategoryOf(assets, assets / "2D Map Sprites" / "Nature" / "trees.png") == "Nature" &&
                kb::world2d::Map2DCategoryOf(assets, assets / "2D Map Sprites" / "loose.png") == "General",
            "a folder of the sprites folder is a category");
    }

    // Real ECS components survive binary scenes, text prefabs, bulk duplication and history.
    {
        kb::scene::Scene scene;
        const auto owner = scene.Entities().CreateObject({ .name = "Tilemap" });
        auto map = kb::assets::MakeEmptyMap2DAsset();
        map.orientation = kb::assets::Map2DOrientation::Wall;
        kb::world2d::SetUpMap2DComponents(scene, owner.Entity(), 987U, map);
        scene.Components().TilemapRenderers().Set(owner.Entity(), { .sortingOrder = -12, .enabled = false });
        scene.Components().TilemapColliders().Set(owner.Entity(), { .enabled = false });
        Require(scene.History().Record("Lock tilemap"), "record tilemap history");
        scene.Components().Tilemaps().Set(owner.Entity(), { .mapAssetId = 987U, .locked = true });
        scene.History().Commit();
        Require(scene.History().Undo() && !scene.Components().Tilemaps().TryGet(owner.Entity())->locked, "undo tilemap settings");
        Require(scene.History().Redo() && scene.Components().Tilemaps().TryGet(owner.Entity())->locked, "redo tilemap settings");
        const auto document = kb::scene::SceneDocumentService::Capture(scene, "Tilemap");
        Require(kb::scene::SceneDocumentService::Save(document, root / "Tilemap.21kbscene"), "save tilemap scene");
        const auto loaded = kb::scene::SceneDocumentService::Load(root / "Tilemap.21kbscene");
        Require(loaded.succeeded, "load tilemap scene");
        const auto& components = loaded.document.worldPrefab.Nodes().front().components;
        Require(components.tilemap && components.grid && components.tilemapRenderer && components.tilemapCollider &&
            components.tilemap->mapAssetId == 987U && components.grid->wall && components.tilemap->locked &&
            components.tilemapRenderer->sortingOrder == -12 && !components.tilemapCollider->enabled, "scene retains every tilemap field");
        const auto prefab = scene.Prefabs().CreateAsset(owner, "Tilemap", root / "Tilemap.kbprefab");
        Require(prefab.IsValid(), "save tilemap prefab");
        kb::scene::Scene destination;
        const auto restored = destination.Prefabs().Load(root / "Tilemap.kbprefab");
        Require(restored.IsValid(), "load tilemap prefab");
        const auto copies = destination.Prefabs().InstantiateMany(restored, 3U);
        Require(copies.size() == 3U, "bulk duplicate tilemap prefab");
        for (const auto& copy : copies) {
            const auto entity = copy.RootObject().Entity();
            Require(destination.Components().Tilemaps().TryGet(entity)->mapAssetId == 987U &&
                destination.Components().Grids().TryGet(entity)->wall &&
                destination.Components().TilemapRenderers().TryGet(entity)->sortingOrder == -12 &&
                !destination.Components().TilemapColliders().TryGet(entity)->enabled, "bulk copy retains authored components");
        }
    }
    // Rich cells survive source IO and undo, and share geometry rules with the renderer.
    {
        auto map = kb::assets::MakeEmptyMap2DAsset(32, 16);
        map.sheets.push_back({ .texture = "/Game/Sprites/ground.png", .material = "/Game/Maps/Materials/Test.kbmat",
            .cellSize = 16, .columns = 4, .rows = 2, .cellHeight = 24, .margin = 2, .spacing = 1, .imageWidth = 71, .imageHeight = 53 });
        auto& definition = map.sheets[0].definitions[0];
        definition.frames = {0,1,2}; definition.framesPerSecond = 4;
        definition.collision.kind = kb::assets::Map2DCollisionKind::SlopeUp;
        definition.properties["surface"] = std::string("stone\nwith spaces");
        definition.blocksNavigation = true; definition.occludesLight = true;
        auto& tile = map.tiles[Map2DTileKey(5, 4, -2)];
        tile.ground = {.sheet = 0, .cell = 0, .rotation = 1, .flipX = true, .tint = 0xAABBCCDD, .offsetX = -0.25F, .offsetY = 0.5F};
        tile.properties["damage"] = std::int64_t{9223372036854775806LL};
        tile.properties["enabled"] = true; tile.properties["amount"] = 1.234567890123456;
        tile.prefab = "/Game/Prefabs/Decoration.kbprefab";
        tile.collision = kb::assets::Map2DCollision{.kind = kb::assets::Map2DCollisionKind::Polygon,
            .polygon = {{0,0},{1,0},{1,1},{0.5F,0.5F},{0,1}}};
        map.parallaxX = 0.5F; map.sortByY = true;
        std::string error;
        Require(kb::assets::IsMap2DAssetValid(map, &error), error.c_str());
        const auto loaded = kb::assets::Map2DAssetIO::Parse(kb::assets::Map2DAssetIO::Write(map), &error);
        Require(loaded && loaded->sheets == map.sheets && loaded->tiles == map.tiles && loaded->parallaxX == 0.5F && loaded->sortByY, "rich tile source roundtrip");
        Require(kb::assets::Map2DAnimatedSprite(map, tile.ground, 0.5).cell == 2, "animation chooses its timed frame");
        const auto uv = kb::assets::Map2DSpriteUV(tile.ground, 0.25F, 0.75F);
        Require(uv[0] == 0.25F && uv[1] == 0.75F, "rotation and flip share UV mapping");
        const auto mesh = kb::render::RenderMap2DMeshBuilder::Build(map, 0, 0.5);
        Require(mesh && mesh->embeddedMaterials.size() == 3 && mesh->embeddedMaterials.back().desc.baseColor[3] < 1.0F, "tinted animated tile builds an alpha material");
        Require(mesh->embeddedMaterials.back().albedoTexturePath == map.sheets[0].texture &&
            mesh->embeddedMaterials.back().emissiveTexturePath == map.sheets[0].texture,
            "tinted tile keeps the texture references required by embedded material resolution");
        Require(kb::assets::Map2DCellProperty(map, tile, "surface") == definition.properties["surface"], "cell properties inherit the graphic defaults");
        kb::world2d::Map2DChangeSet change; change.RecordSettings(map); change.Record(map, Map2DTileKey(5,4,-2));
        map.sheets[0].definitions[0].frames.clear(); map.tiles.clear(); change.Finish(map); change.Undo(map);
        Require(map.tiles == loaded->tiles && map.sheets == loaded->sheets, "undo retains animation, properties and collision");
        auto invalid = *map.tiles.begin()->second.collision; invalid.polygon = {{0,0},{1,1},{0,1},{1,0}};
        Require(!kb::assets::IsMap2DCollisionValid(invalid), "crossing polygon is rejected");
        kb::scene::Scene scene;
        Require(scene.Assets().Manager().Mounts().Mount("Game", assets), "mount collision output");
        const auto owner = scene.Entities().CreateObject({.name = "Shapes"});
        map.orientation = kb::assets::Map2DOrientation::Wall;
        map.tiles[Map2DTileKey(2,2,0)].collision = kb::assets::Map2DCollision{.kind = kb::assets::Map2DCollisionKind::OneWay};
        Require(kb::world2d::SetUpMap2DCollision(scene, owner.Entity(), map, nullptr, &error), error.c_str());
        const auto colliders = owner.Children().front().Children();
        Require(colliders.size() == 2, "polygon and one-way generate two ECS colliders");
        bool polygon = false, oneWay = false;
        for (const auto child : colliders) {
            const auto* collider = scene.Components().Colliders().TryGet(child.Entity());
            polygon |= collider && collider->shape == kb::scene::ColliderShape::Mesh;
            oneWay |= collider && collider->shape == kb::scene::ColliderShape::OneWayBox;
        }
        Require(polygon && oneWay, "collision kinds are preserved in ECS");
    }
    {
        auto map = kb::assets::MakeEmptyMap2DAsset(10,10);
        for (unsigned y = 0; y < 9; ++y) map.tiles[Map2DTileKey(4,y,0)].solid = true;
        const auto path = kb::assets::FindMap2DPath(map, {1,1}, {8,1});
        Require(!path.empty() && path.back() == std::array<int,2>{8,1}, "navigation finds the opening around a wall");
        map.tiles[Map2DTileKey(4,9,0)].solid = true;
        Require(kb::assets::FindMap2DPath(map, {1,1}, {8,1}).empty(), "navigation respects updated cells");
        map.tiles.clear();
        for (unsigned x = 0; x < 10; ++x) if (x != 4 && x != 5) map.tiles[Map2DTileKey(x,8,0)].solid = true;
        const auto platformPath = kb::assets::FindMap2DPath(map, {2,7}, {7,7}, {.platformer = true});
        Require(!platformPath.empty(), "platformer navigation can jump a clear gap");
    }
    // Runtime cells are independent per entity; a local edit rebuilds just its chunk.
    {
        kb::scene::Scene scene;
        auto& manager = scene.Assets().Manager();
        Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()), "register tilemap renderer");
        auto map = kb::assets::MakeEmptyMap2DAsset(32U, 32U);
        map.background = false;
        map.tiles[Map2DTileKey(0U, 0U, 0)].solid = true;
        map.tiles[Map2DTileKey(20U, 20U, 0)].solid = true;
        const auto file = root / "Runtime.kbmap2d";
        Require(kb::assets::Map2DAssetIO::Save(file, map), "save runtime map");
        const kb::assets::AssetId source{ 6789U };
        Require(manager.RegisterAsset({ .id = source, .type = "RenderMesh", .virtualPath = "/Game/Runtime.kbmap2d",
            .physicalPath = file, .contentHash = 1U }), "register runtime map");
        const auto first = scene.Entities().CreateEntity({ .name = "First" });
        const auto second = scene.Entities().CreateEntity({ .name = "Second" });
        scene.Components().MeshRenderers().Set(second, {.meshAssetId = 34567U, .materialAssetId = 45678U, .castsShadow = false, .layer = 4U});
        kb::world2d::SetUpMap2DComponents(scene, first, source.value, map);
        kb::world2d::SetUpMap2DComponents(scene, second, source.value, map);
        const auto requireIndependentMesh = [&]() {
            const auto* mesh = std::as_const(scene).Components().MeshRenderers().TryGet(second);
            Require(mesh && mesh->meshAssetId == 34567U && mesh->materialAssetId == 45678U &&
                !mesh->castsShadow && mesh->layer == 4U && !mesh->previewHidden, "tilemap preserves the owner's independent mesh renderer");
        };
        requireIndependentMesh();
        Require(kb::script::ScriptSceneComponentApi::HasComponent(scene, first, "Tilemap"), "scripts see the authored tilemap");
        Require(!kb::script::ScriptSceneComponentApi::SetProperty(scene, first, "Grid", "cellSize", kb::script::ScriptValue{ 0.0F }).succeeded,
            "scripts reject an invalid cell size");
        Require(!manager.RegisterGeneratedAsset({ .id = source, .type = "RenderMesh", .virtualPath = "/Runtime/Collision" }),
            "generated resources cannot overwrite source assets");
        Require(kb::world2d::SetUpMap2DCollision(scene, first, map), "create authored collision before entering play");
        auto system = std::make_unique<kb::world2d::Map2DRuntime>();
        auto* runtime = system.get();
        const auto handle = scene.Runtime().AddSceneSystem(std::move(system));
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(scene.Runtime().DrainSceneSystemErrors().empty(), "runtime tilemap initialization succeeds");
        Require(runtime->RebuiltChunksLastUpdate() == 4U, "two maps build two chunks each");
        requireIndependentMesh();
        kb::render::RenderScene renderScene;
        kb::render::EcsRenderSceneSynchronizer synchronizer;
        synchronizer.Sync(scene, renderScene);
        const auto* originalProxy = renderScene.FindMeshByEntity(second.Id());
        Require(originalProxy && originalProxy->desc.visible, "independent mesh reaches the renderer");
        const auto originalModel = originalProxy->desc.model;
        scene.Components().TilemapRenderers().Set(second, {.sortingOrder = 7, .enabled = false});
        synchronizer.Sync(scene, renderScene);
        const auto* hiddenLayerMesh = renderScene.FindMeshByEntity(second.Id());
        Require(hiddenLayerMesh && hiddenLayerMesh->desc.visible && hiddenLayerMesh->desc.model == originalModel,
            "tilemap visibility and sorting never hide or displace the owner's ordinary mesh");
        std::size_t hiddenChunks = 0;
        for (const auto child : scene.Entities().Object(second).Children()) {
            if (const auto* proxy = renderScene.FindMeshByEntity(child.Entity().Id())) {
                Require(!proxy->desc.visible, "hidden tilemap still hides its generated chunks");
                ++hiddenChunks;
            }
        }
        Require(hiddenChunks == 2U, "both generated chunks participate in tilemap visibility");
        scene.Components().TilemapRenderers().Set(second, {});
        Require(runtime->EraseTile(first, 0U, 0U), "erase runtime cell");
        Require(runtime->Tile(second, 0U, 0U).has_value(), "another instance retains its cell");
        Require(!runtime->SetTile(first, 32U, 0U, 0, {}), "runtime rejects out of bounds edit");
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(runtime->RebuiltChunksLastUpdate() == 1U, "one edited cell rebuilds one chunk");
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(runtime->RebuiltChunksLastUpdate() == 0U, "unchanged maps do not rebuild");
        kb::assets::AssetId generated{};
        for (const auto& asset : manager.Registry().All()) if (asset.id != source) { generated = asset.id; break; }
        Require(manager.Unload(generated), "evict one generated chunk");
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(runtime->RebuiltChunksLastUpdate() == 1U, "evicted geometry is restored without changing other chunks");
        Require(kb::assets::Map2DAssetIO::Load(file)->tiles.size() == 2U, "runtime never edits the source asset");
        scene.Runtime().SetPlaying(false);
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(runtime->Map(first) == nullptr && manager.Registry().Count() == 1U, "stopping releases runtime map state");
        requireIndependentMesh();
        scene.Runtime().SetPlaying(true);
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(runtime->RebuiltChunksLastUpdate() == 4U && runtime->Tile(first, 0U, 0U).has_value(), "next play starts from the authored map");
        Require(scene.Runtime().RemoveSceneSystem(handle), "detach tilemap runtime");
        Require(manager.Registry().Count() == 1U && scene.Components().MeshRenderers().Has(first), "detach releases generated assets and restores preview");
        requireIndependentMesh();
    }
    // Packaged map rules and script calls operate on isolated runtime entities.
    {
        using namespace kb::assets;
        using namespace kb::scene;
        using namespace kb::script;
        Scene scene;
        auto& manager = scene.Assets().Manager();
        Require(manager.Mounts().Mount("Game", assets), "mount runtime assets");
        Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()), "register runtime mesh loader");
        const auto prefabSource = scene.Entities().CreateObject({.name = "Decoration"});
        const auto prefabFile = assets / "Decoration.kbprefab";
        Require(scene.Prefabs().CreateAsset(prefabSource, "Decoration", prefabFile).IsValid(), "save cell prefab");
        scene.Entities().Destroy(prefabSource.Entity());
        static_cast<void>(scene.Assets().Discover());
        auto map = MakeEmptyMap2DAsset(32,16); map.background = false; map.parallaxX = 0.5F;
        const auto camera = scene.Entities().CreateEntity({.name = "MapCamera"});
        scene.Components().Cameras().Set(camera, {.primary = true});
        map.sheets.push_back({.texture = "/Game/Sprites/ground.png", .material = "/Game/Maps/Materials/Test.kbmat", .columns = 4});
        map.sheets[0].definitions[0].frames = {0,2}; map.sheets[0].definitions[0].framesPerSecond = 4;
        map.sheets[0].definitions[0].occludesLight = true;
        Map2DGroundRule rule{.name = "Ground", .sheet = 0, .variants = {0}, .weights = {1}};
        rule.borders.fill(1); map.groundRules.push_back(rule);
        map.tiles[Map2DTileKey(15,3,0)].ground = {0,0};
        map.tiles[Map2DTileKey(2,6,0)].prefab = "/Game/Decoration.kbprefab";
        const auto file = root / "RichRuntime.kbmap2d";
        Require(Map2DAssetIO::Save(file, map), "save rich runtime source");
        const AssetId source{77789};
        Require(manager.RegisterAsset({.id = source, .type = "RenderMesh", .virtualPath = "/Game/RichRuntime.kbmap2d", .physicalPath = file, .contentHash = 1}), "register rich source");
        const auto owner = scene.Entities().CreateEntity({.name = "LiveMap"});
        kb::world2d::SetUpMap2DComponents(scene, owner, source.value, map);
        auto system = std::make_unique<kb::world2d::Map2DRuntime>(); auto* runtime = system.get();
        const auto handle = scene.Runtime().AddSceneSystem(std::move(system));
        static_cast<void>(scene.Runtime().Update(0));
        Require(scene.Runtime().DrainSceneSystemErrors().empty(), "rich map initializes with a real prefab");
        const auto entityCount = scene.Entities().Count();
        Require(entityCount > 3, "prefab and shadow entities are instantiated");
        const auto captured = SceneDocumentService::Capture(scene, "LiveMap");
        Require(captured.worldPrefab.Nodes().size() == 2, "generated chunks, shadow geometry and cell prefabs never enter saved scenes");
        Require(runtime->PaintGround(owner,16,3,0,"Ground"), "runtime paints embedded ground rules");
        Require(runtime->Tile(owner,17,3)->borders.size() > 0, "runtime autoborder crosses a chunk boundary");
        static_cast<void>(scene.Runtime().Update(0));
        Require(runtime->RebuiltChunksLastUpdate() == 2, "boundary painting only rebuilds the two adjacent chunks");
        static_cast<void>(scene.Runtime().Update(0.26F));
        Require(runtime->RebuiltChunksLastUpdate() == 2, "animated cells rebuild only when their frame changes");
        static_cast<void>(scene.Runtime().Update(0.01F));
        Require(runtime->RebuiltChunksLastUpdate() == 0, "animation does not rebuild between frames");
        auto cameraTransform = scene.Transforms().Get(camera); cameraTransform.localPosition.x = 4;
        scene.Transforms().Set(camera, cameraTransform);
        static_cast<void>(scene.Runtime().Update(0));
        for (const auto child : scene.Entities().Object(owner).Children()) {
            if (scene.Components().MeshRenderers().Has(child.Entity())) Require(std::abs(scene.Transforms().Get(child).localPosition.x - 2.0F) < 0.001F, "parallax follows the primary camera in map-local coordinates");
        }
        for (const auto& metadata : manager.Registry().All()) {
            if (metadata.type != "RenderMesh" || !NormalizeAssetPath(metadata.virtualPath).starts_with("/Runtime/")) continue;
            const auto mesh = manager.AcquireLoaded<kb::render::RenderMeshAssetData>(metadata.id);
            Require(mesh.IsLoaded() && mesh->desc.vertexData == mesh->tangentVertices.data() && mesh->desc.sections == mesh->sections.data() &&
                mesh->desc.indices32 == mesh->indices32.data() && mesh->desc.gpuDriven.lods == mesh->lods.data(),
                "generated tiles and occluders retain their own geometry buffers");
        }
        ScriptRuntimeHost host(scene);
        Require(host.Succeeded(), "script host installs the map API");
        std::vector<ScriptFunctionArgument> args{{"entity", ScriptValue{owner.Id(), ScriptValueType::Entity}}, {"x", ScriptValue{3}}, {"y", ScriptValue{3}}, {"name", ScriptValue{std::string{"score"}}}, {"value", ScriptValue{std::int64_t{9223372036854775806LL}}}};
        const auto changed = host.Functions().Call("Map2D.SetInteger", args, {.scene = &scene});
        Require(changed.executed && changed.Succeeded(), "scripts set exact integer tile metadata");
        args.pop_back();
        const auto read = host.Functions().Call("Map2D.GetInteger", args, {.scene = &scene});
        Require(read.Succeeded() && read.Output("value")->AsInt64() == 9223372036854775806LL, "scripts read integer metadata without precision loss");
        Require(Map2DAssetIO::Load(file)->tiles == map.tiles, "runtime rules and scripts leave source data unchanged");
        Require(scene.Runtime().RemoveSceneSystem(handle), "detach rich runtime");
        for (const auto& asset : manager.Registry().All()) Require(!NormalizeAssetPath(asset.virtualPath).starts_with("/Runtime/"), "runtime releases generated resources on detach");
    }
    // Every rotation and flip turns the collision outline the same way as the graphic: the UV mapping
    // that draws the sprite sends each displayed outline point back to the authored one.
    {
        auto map = kb::assets::MakeEmptyMap2DAsset(4, 4);
        map.sheets.push_back({ .texture = "/Game/Sprites/ground.png", .material = "/Game/Maps/Materials/Turn.kbmat", .cellSize = 16, .columns = 4, .rows = 2 });
        const std::vector<std::array<float, 2>> authored{ { 0.1F, 0.2F }, { 0.9F, 0.3F }, { 0.4F, 0.8F } };
        for (const bool slope : { false, true }) {
            auto& collision = map.sheets[0].definitions[0].collision;
            collision = kb::assets::Map2DCollision{ .kind = slope ? kb::assets::Map2DCollisionKind::SlopeUp : kb::assets::Map2DCollisionKind::Polygon,
                .polygon = slope ? std::vector<std::array<float, 2>>{} : authored };
            const std::vector<std::array<float, 2>> original = slope ? std::vector<std::array<float, 2>>{ { 0.0F, 1.0F }, { 1.0F, 0.0F }, { 1.0F, 1.0F } } : authored;
            for (std::uint8_t rotation = 0U; rotation < 4U; ++rotation) for (const bool flipX : { false, true }) for (const bool flipY : { false, true }) {
                kb::assets::Map2DTile tile;
                tile.ground = { .sheet = 0, .cell = 0, .rotation = rotation, .flipX = flipX, .flipY = flipY };
                const kb::assets::Map2DCollision shape = kb::assets::Map2DCellCollision(map, tile);
                Require((shape.polygon.size() == original.size() && shape.kind == kb::assets::Map2DCollisionKind::Polygon) ||
                        (!rotation && !flipX && !flipY && slope && shape.kind == kb::assets::Map2DCollisionKind::SlopeUp),
                    "a turned or mirrored outline becomes a polygon with the same points");
                if (shape.polygon.empty()) continue;
                for (std::size_t point = 0U; point < original.size(); ++point) {
                    const auto texture = kb::assets::Map2DSpriteUV(tile.ground, shape.polygon[point][0], shape.polygon[point][1]);
                    Require(std::abs(texture[0] - original[point][0]) < 0.0001F && std::abs(texture[1] - original[point][1]) < 0.0001F,
                        "the collision outline follows the same rotation and flip as the graphic");
                }
            }
        }
    }
    // The last graphic of a sheet samples exactly its own cell, half a texel inside, for square and
    // rectangular cells, with and without margin, spacing and spare image area.
    {
        struct Atlas { std::uint32_t cellSize, cellHeight, columns, rows, margin, spacing, imageWidth, imageHeight; };
        for (const Atlas atlas : { Atlas{ 16, 0, 4, 2, 0, 0, 0, 0 }, Atlas{ 16, 24, 4, 2, 2, 1, 71, 53 }, Atlas{ 32, 0, 3, 3, 1, 2, 0, 0 },
                Atlas{ 10, 15, 5, 3, 0, 1, 60, 50 } }) {
            auto map = kb::assets::MakeEmptyMap2DAsset(4, 4);
            map.sheets.push_back({ .texture = "/Game/Sprites/ground.png", .material = "/Game/Maps/Materials/Atlas.kbmat", .cellSize = atlas.cellSize,
                .columns = atlas.columns, .rows = atlas.rows, .cellHeight = atlas.cellHeight, .margin = atlas.margin, .spacing = atlas.spacing,
                .imageWidth = atlas.imageWidth, .imageHeight = atlas.imageHeight });
            const std::uint32_t last = atlas.columns * atlas.rows - 1U;
            map.tiles[Map2DTileKey(1, 1, 0)].ground = { .sheet = 0, .cell = last };
            const auto mesh = kb::render::RenderMap2DMeshBuilder::Build(map);
            Require(mesh && mesh->sections.size() == 2U, "an atlas map builds its sheet section");
            const auto& sheet = map.sheets[0];
            const std::uint32_t height = atlas.cellHeight ? atlas.cellHeight : atlas.cellSize;
            const float left = static_cast<float>(atlas.margin + (last % atlas.columns) * (atlas.cellSize + atlas.spacing));
            const float top = static_cast<float>(atlas.margin + (last / atlas.columns) * (height + atlas.spacing));
            const float width = static_cast<float>(sheet.ImageWidth()), imageHeight = static_cast<float>(sheet.ImageHeight());
            const float expected[4] = { (left + 0.5F) / width, (top + 0.5F) / imageHeight,
                (left + static_cast<float>(atlas.cellSize) - 0.5F) / width, (top + static_cast<float>(height) - 0.5F) / imageHeight };
            float lowU = 2.0F, lowV = 2.0F, highU = -1.0F, highV = -1.0F;
            const auto& section = mesh->sections[1];
            for (std::uint32_t vertex = section.vertexStart; vertex < section.vertexStart + section.vertexCount; ++vertex) {
                const auto& corner = mesh->tangentVertices[vertex];
                lowU = std::min(lowU, corner.u); highU = std::max(highU, corner.u);
                lowV = std::min(lowV, corner.v); highV = std::max(highV, corner.v);
            }
            Require(std::abs(lowU - expected[0]) < 0.00001F && std::abs(lowV - expected[1]) < 0.00001F &&
                    std::abs(highU - expected[2]) < 0.00001F && std::abs(highV - expected[3]) < 0.00001F && highU <= 1.0F && highV <= 1.0F,
                "the last graphic of an atlas must sample only its own cell");
        }
    }
    // A large map: a fill, a stroke across a 16 cell chunk seam, undo and redo keep every tile,
    // and the whole sequence stays inside a generous time budget.
    {
        constexpr std::uint32_t kSide = 400U;
        const std::size_t memoryBefore = WorkingSetBytes();
        const auto started = std::chrono::steady_clock::now();
        Map2DAsset map = kb::assets::MakeEmptyMap2DAsset(kSide, kSide);
        kb::world2d::Map2DEditor editor{ map, library, assets };
        Require(editor.FillGround(*dirtBrush, 0U, 0U, 0U) == kSide * kSide, "a large fill must cover every cell");
        const Map2DAsset filled = map;
        kb::world2d::Map2DChangeSet changes;
        editor.SetChangeSet(&changes);
        Require(editor.PaintGround(*grassBrush, { 15U, 15U, 16U, 16U }, 0U), "a stroke across the chunk seam must paint");
        changes.Finish(map);
        editor.SetChangeSet(nullptr);
        Require(Tile(map, 15U, 15U)->ground.cell == 25U && Tile(map, 16U, 16U)->ground.cell == 25U &&
                HasBorder(map, 16U, 14U, grassBrush->Border(Map2DBorderPiece::S)) && HasBorder(map, 14U, 16U, grassBrush->Border(Map2DBorderPiece::E)),
            "borders must cross the chunk seam on both axes");
        Require(map.tiles.size() == filled.tiles.size(), "the stroke must not add tiles beyond the fill");
        const Map2DAsset painted = map;
        changes.Undo(map);
        Require(map.tiles == filled.tiles, "undo must restore every tile of the large map");
        changes.Redo(map);
        Require(map.tiles == painted.tiles, "redo must repeat the stroke on the large map");
        // The map, the filled copy, the painted copy and the change set are all alive here.
        const std::size_t memoryAfter = WorkingSetBytes();
        const std::size_t memoryGrowth = memoryAfter > memoryBefore ? memoryAfter - memoryBefore : 0U;
        std::printf("map2d large map %ux%u memory growth %zu MiB\n", kSide, kSide, memoryGrowth / (1024U * 1024U));
        Require(memoryGrowth < 300U * 1024U * 1024U, "editing a large map must stay inside its memory budget");
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        std::printf("map2d large map %ux%u edit sequence %lld ms\n", kSide, kSide, static_cast<long long>(elapsed.count()));
        Require(elapsed < std::chrono::seconds(5), "editing a large map must stay inside its time budget");
    }
    // Typed tile data: every type survives the file exactly, a cell beats the defaults of its graphics, the top
    // graphic beats the ground, old files still load, and data the loader cannot trust is refused, not repaired.
    {
        using kb::assets::Map2DValue;
        auto map = kb::assets::MakeEmptyMap2DAsset(8, 8);
        map.sheets.push_back({ .texture = "/Game/Sprites/ground.png", .material = "/Game/Maps/Materials/Data.kbmat", .cellSize = 16, .columns = 4, .rows = 2 });
        auto& groundDefaults = map.sheets[0].definitions[0].properties;
        groundDefaults["surface"] = std::string("stone");
        groundDefaults["weight"] = std::int64_t{ 3 };
        groundDefaults["slippery"] = false;
        map.sheets[0].definitions[1].properties["surface"] = std::string("moss");
        auto& tile = map.tiles[Map2DTileKey(2, 3, 0)];
        tile.ground = { .sheet = 0, .cell = 0 };
        tile.items.push_back({ .sheet = 0, .cell = 1 });
        tile.properties["weight"] = std::int64_t{ 7 };
        tile.properties["ratio"] = 3.0;
        tile.properties["label"] = std::string("7");
        tile.properties["lowest"] = std::numeric_limits<std::int64_t>::min();
        tile.properties["highest"] = std::numeric_limits<std::int64_t>::max();
        tile.properties["quote"] = std::string("a \"b\"\n\tc");
        tile.properties["off"] = false;
        const auto read = [&](const char* name) { return kb::assets::Map2DCellProperty(map, tile, name); };
        Require(read("weight") == Map2DValue{ std::int64_t{ 7 } }, "a cell's own data beats the graphic's default");
        Require(read("surface") == Map2DValue{ std::string("moss") }, "the top graphic's default beats the ground's");
        Require(read("slippery") == Map2DValue{ false }, "a default that nothing above overrides comes from the ground");
        Require(!read("missing").has_value(), "data that exists nowhere is absent");

        std::string error;
        Require(kb::assets::IsMap2DAssetValid(map, &error), error.c_str());
        const std::string text = kb::assets::Map2DAssetIO::Write(map);
        const auto loaded = kb::assets::Map2DAssetIO::Parse(text, &error);
        Require(loaded.has_value() && loaded->tiles == map.tiles && loaded->sheets == map.sheets, "typed data must survive the file");
        const auto& back = loaded->tiles.begin()->second.properties;
        Require(std::holds_alternative<double>(back.at("ratio")) && std::holds_alternative<std::string>(back.at("label")) &&
                std::holds_alternative<bool>(back.at("off")) && std::holds_alternative<std::int64_t>(back.at("weight")) &&
                std::get<std::int64_t>(back.at("lowest")) == std::numeric_limits<std::int64_t>::min() &&
                std::get<std::int64_t>(back.at("highest")) == std::numeric_limits<std::int64_t>::max() &&
                std::get<std::string>(back.at("quote")) == "a \"b\"\n\tc",
            "a double 3.0 stays a double, the string \"7\" stays a string, 64 bit integers and control characters are exact");

        const auto refused = [&](const auto& change) {
            auto copy = map;
            change(copy);
            return !kb::assets::IsMap2DAssetValid(copy);
        };
        Require(!refused([](auto&) {}), "the untouched map is valid");
        Require(refused([](auto& m) { m.tiles.begin()->second.properties[""] = true; }), "data needs a name");
        Require(refused([](auto& m) { m.tiles.begin()->second.properties[std::string(129U, 'k')] = true; }), "a data name is at most 128 characters");
        Require(refused([](auto& m) {
            for (int index = 0; index < 64; ++index) m.tiles.begin()->second.properties["extra" + std::to_string(index)] = true;
        }), "a cell holds at most 64 values");
        Require(refused([](auto& m) { m.tiles.begin()->second.properties["bad"] = std::numeric_limits<double>::quiet_NaN(); }), "NaN data is refused");
        Require(refused([](auto& m) { m.tiles.begin()->second.properties["bad"] = std::numeric_limits<double>::infinity(); }), "infinite data is refused");
        Require(refused([](auto& m) { m.tiles.begin()->second.properties["bad"] = std::string(4097U, 'x'); }), "text over 4096 characters is refused");
        Require(refused([](auto& m) { m.sheets[0].definitions[0].properties["bad"] = std::numeric_limits<double>::quiet_NaN(); }),
            "a graphic's default data is checked like a cell's");
        Require(refused([](auto& m) { m.sheets[0].definitions[9].properties["late"] = true; }), "a default for a graphic outside the sheet is refused");

        const auto replaced = [&](const std::string& from, const std::string& to) {
            std::string edited = text;
            const std::size_t at = edited.find(from);
            Require(at != std::string::npos, "the saved text must hold the line a corruption test edits");
            edited.replace(at, from.size(), to);
            return kb::assets::Map2DAssetIO::Parse(edited).has_value();
        };
        Require(replaced(" int 776569676874 7\n", " int 776569676874 7\n"), "an unchanged edit still loads");
        Require(!replaced(" int 776569676874 7\n", " quad 776569676874 7\n"), "an unknown data type is refused");
        Require(!replaced(" int 776569676874 7\n", " int 776569676874 7x\n"), "an integer with trailing text is refused");
        Require(!replaced(" int 776569676874 7\n", " int 776569676874 99999999999999999999\n"), "an integer over 64 bits is refused");
        Require(!replaced(" float 726174696f 3\n", " float 726174696f nan\n"), "a NaN number is refused");
        Require(!replaced(" string 6c6162656c 37\n", " string 6c6162656c 3\n"), "text with an odd number of hex digits is refused");

        // Version 1 files have none of the later fields; they load with the defaults, a newer version is refused.
        const std::string v1 = "21kb-map2d 1\nsize 4 4\nsheet 16 4 2 \"/Game/Sprites/ground.png\" \"/Game/Maps/Materials/Data.kbmat\"\nt 1 1 0 g=0:1\n";
        const auto old = kb::assets::Map2DAssetIO::Parse(v1, &error);
        Require(old.has_value() && old->width == 4U && old->tileSize == 1.0F && old->orientation == kb::assets::Map2DOrientation::Flat &&
                old->parallaxX == 1.0F && !old->sortByY && Tile(*old, 1U, 1U) != nullptr && Tile(*old, 1U, 1U)->ground.cell == 1U &&
                Tile(*old, 1U, 1U)->properties.empty() && old->sheets[0].definitions.empty(),
            "a version 1 map must load with the defaults of the newer fields");
        std::string future = v1;
        future.replace(future.find("map2d 1"), 7U, "map2d 3");
        Require(!kb::assets::Map2DAssetIO::Parse(future).has_value(), "a map of a newer version than this build is refused");
    }
    // Animated graphics: every clip runs on its own frames and speed, loops, keeps the sprite's turn, flip, tint and
    // offset, survives the file, and time that is not a number never picks a frame.
    {
        auto map = kb::assets::MakeEmptyMap2DAsset(8, 8);
        map.sheets.push_back({ .texture = "/Game/Sprites/ground.png", .material = "/Game/Maps/Materials/Clips.kbmat", .cellSize = 16, .columns = 4, .rows = 2 });
        auto& threeFrames = map.sheets[0].definitions[0];
        threeFrames.frames = { 0, 1, 2 };
        threeFrames.framesPerSecond = 4.0F;
        auto& twoFrames = map.sheets[0].definitions[3];
        twoFrames.frames = { 5, 6 };
        twoFrames.framesPerSecond = 2.0F;
        auto& shifted = map.sheets[0].definitions[4];
        shifted.frames = { 6, 7 };
        shifted.framesPerSecond = 1.0F;
        const auto cellAt = [&](std::uint32_t cell, double seconds) {
            return kb::assets::Map2DAnimatedSprite(map, { .sheet = 0, .cell = cell }, seconds).cell;
        };
        Require(cellAt(0, 0.0) == 0U && cellAt(0, 0.249) == 0U && cellAt(0, 0.25) == 1U && cellAt(0, 0.5) == 2U,
            "a clip steps through its frames at its speed");
        Require(cellAt(0, 0.75) == 0U && cellAt(0, 1.0) == 1U && cellAt(0, 3.0) == 0U && cellAt(0, 1.0e6) == 1U,
            "a clip loops");
        Require(cellAt(3, 0.0) == 5U && cellAt(3, 0.49) == 5U && cellAt(3, 0.5) == 6U && cellAt(3, 1.0) == 5U,
            "another clip keeps its own frames and speed");
        Require(cellAt(7, 5.0) == 7U && cellAt(2, 5.0) == 2U, "a graphic without a clip never changes");
        Require(cellAt(4, -1.0) == 6U && cellAt(4, std::numeric_limits<double>::quiet_NaN()) == 4U &&
                cellAt(4, std::numeric_limits<double>::infinity()) == 4U,
            "negative time shows the first frame; time that is not a number leaves the sprite alone");
        const kb::assets::Map2DSprite turned{ .sheet = 0, .cell = 0, .rotation = 3, .flipX = true, .flipY = true, .tint = 0x11223344U,
            .offsetX = 0.25F, .offsetY = -0.5F };
        kb::assets::Map2DSprite expected = turned;
        expected.cell = 2U;
        Require(kb::assets::Map2DAnimatedSprite(map, turned, 0.5) == expected, "an animated frame keeps the sprite's turn, flip, tint and offset");

        std::string error;
        const auto loaded = kb::assets::Map2DAssetIO::Parse(kb::assets::Map2DAssetIO::Write(map), &error);
        Require(loaded.has_value() && loaded->sheets == map.sheets, "clips must survive the file");
        for (double seconds : { 0.0, 0.3, 0.6, 0.9, 1.7 }) {
            for (std::uint32_t cell : { 0U, 3U, 4U }) {
                Require(kb::assets::Map2DAnimatedSprite(*loaded, { .sheet = 0, .cell = cell }, seconds) ==
                        kb::assets::Map2DAnimatedSprite(map, { .sheet = 0, .cell = cell }, seconds), "a reloaded clip plays the same");
            }
        }
        const auto refused = [&](const auto& change) {
            auto copy = map;
            change(copy);
            return !kb::assets::IsMap2DAssetValid(copy);
        };
        Require(!refused([](auto&) {}), "the untouched clips are valid");
        Require(refused([](auto& m) { m.sheets[0].definitions[0].frames.push_back(8U); }), "a frame outside the sheet is refused");
        Require(refused([](auto& m) { m.sheets[0].definitions[0].framesPerSecond = 0.0F; }), "a clip needs a speed");
        Require(refused([](auto& m) { m.sheets[0].definitions[0].framesPerSecond = std::numeric_limits<float>::quiet_NaN(); }), "a NaN speed is refused");
        Require(refused([](auto& m) { m.sheets[0].definitions[0].framesPerSecond = 241.0F; }), "a clip over 240 frames per second is refused");
        Require(refused([](auto& m) { m.sheets[0].definitions[0].frames.assign(257U, 1U); }), "a clip of more than 256 frames is refused");
        Require(!refused([](auto& m) { m.sheets[0].definitions[0].frames.assign(256U, 1U); }), "a clip of 256 frames is valid");
    }
    // At runtime a clip rebuilds only the chunk it is drawn in, and only when its frame changes: a paused scene
    // (no time passing) rebuilds nothing, a loop shows the first frame again, and Play starts every clip over.
    {
        kb::scene::Scene scene;
        auto& manager = scene.Assets().Manager();
        Require(manager.Mounts().Mount("Game", assets), "mount clip assets");
        Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()), "register clip renderer");
        auto map = kb::assets::MakeEmptyMap2DAsset(32U, 32U);
        map.background = false;
        map.sheets.push_back({ .texture = "/Game/Sprites/ground.png", .material = "/Game/Maps/Materials/Test.kbmat", .columns = 4, .rows = 2 });
        map.sheets[0].definitions[0].frames = { 0, 1, 2 };
        map.sheets[0].definitions[0].framesPerSecond = 4.0F;
        map.tiles[Map2DTileKey(1U, 1U, 0)].ground = { .sheet = 0, .cell = 0 };
        map.tiles[Map2DTileKey(20U, 20U, 0)].ground = { .sheet = 0, .cell = 5 };
        const auto file = root / "Clips.kbmap2d";
        Require(kb::assets::Map2DAssetIO::Save(file, map), "save the clip map");
        const kb::assets::AssetId source{ 8123U };
        Require(manager.RegisterAsset({ .id = source, .type = "RenderMesh", .virtualPath = "/Game/Clips.kbmap2d", .physicalPath = file, .contentHash = 1U }),
            "register the clip map");
        const auto owner = scene.Entities().CreateEntity({ .name = "Clips" });
        kb::world2d::SetUpMap2DComponents(scene, owner, source.value, map);
        auto system = std::make_unique<kb::world2d::Map2DRuntime>();
        auto* runtime = system.get();
        static_cast<void>(scene.Runtime().AddSceneSystem(std::move(system)));
        const auto step = [&](float seconds) {
            static_cast<void>(scene.Runtime().Update(seconds));
            Require(scene.Runtime().DrainSceneSystemErrors().empty(), "the clip map runs without errors");
            return runtime->RebuiltChunksLastUpdate();
        };
        Require(step(0.0F) == 2U, "both chunks are built when Play starts");
        Require(step(0.3F) == 1U, "a clip's frame change rebuilds only the chunk it is drawn in");
        Require(step(0.0F) == 0U, "no time passing (a pause) rebuilds nothing");
        Require(step(0.1F) == 0U, "time inside the same frame rebuilds nothing");
        Require(step(0.2F) == 1U, "the next frame rebuilds its chunk");
        Require(step(0.2F) == 1U, "the loop shows the first frame again");
        scene.Runtime().SetPlaying(false);
        static_cast<void>(scene.Runtime().Update(0.0F));
        scene.Runtime().SetPlaying(true);
        Require(step(0.0F) == 2U, "Play builds the map again");
        Require(step(0.1F) == 0U, "Play starts every clip over: 0.1 s is still the first frame, as it was not after 0.8 s");
        Require(step(0.2F) == 1U, "and the clip then advances on its own time");
    }
    // Prefab cells at scale: thousands of instances appear once per cell, are never duplicated by painting the
    // same cell again or changing its other data, vanish with the cell, and Stop releases every one of them.
    {
        using namespace kb::scene;
        using kb::assets::Map2DTile;
        Scene scene;
        auto& manager = scene.Assets().Manager();
        Require(manager.Mounts().Mount("Game", assets), "mount prefab cell assets");
        Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()), "register prefab cell renderer");
        const auto prefabSource = scene.Entities().CreateObject({ .name = "Marker" });
        Require(scene.Prefabs().CreateAsset(prefabSource, "Marker", assets / "Marker.kbprefab").IsValid(), "save the marker prefab");
        scene.Entities().Destroy(prefabSource.Entity());
        static_cast<void>(scene.Assets().Discover());
        constexpr std::uint32_t kSide = 64U;
        auto map = kb::assets::MakeEmptyMap2DAsset(kSide, kSide);
        map.background = false;
        map.sheets.push_back({ .texture = "/Game/Sprites/ground.png", .material = "/Game/Maps/Materials/Test.kbmat", .columns = 4, .rows = 2 });
        // A ground tile in every 16 cell chunk, so that the chunks exist before the prefabs are counted.
        for (std::uint32_t chunkY = 0U; chunkY < kSide / 16U; ++chunkY) for (std::uint32_t chunkX = 0U; chunkX < kSide / 16U; ++chunkX) {
            map.tiles[Map2DTileKey(chunkX * 16U, chunkY * 16U, 0)].ground = { .sheet = 0, .cell = 0 };
        }
        const auto file = root / "Markers.kbmap2d";
        Require(kb::assets::Map2DAssetIO::Save(file, map), "save the prefab cell map");
        const kb::assets::AssetId source{ 8456U };
        Require(manager.RegisterAsset({ .id = source, .type = "RenderMesh", .virtualPath = "/Game/Markers.kbmap2d", .physicalPath = file, .contentHash = 1U }),
            "register the prefab cell map");
        const auto owner = scene.Entities().CreateEntity({ .name = "Markers" });
        kb::world2d::SetUpMap2DComponents(scene, owner, source.value, map);
        auto system = std::make_unique<kb::world2d::Map2DRuntime>();
        auto* runtime = system.get();
        static_cast<void>(scene.Runtime().AddSceneSystem(std::move(system)));
        const auto step = [&]() {
            static_cast<void>(scene.Runtime().Update(0.0F));
            Require(scene.Runtime().DrainSceneSystemErrors().empty(), "prefab cells run without errors");
        };
        const std::size_t beforePlay = scene.Entities().Count();
        step();
        const std::size_t withChunks = scene.Entities().Count();
        Require(withChunks > beforePlay, "Play builds the chunk entities");

        // Sets the prefab (or none) of every cell in rows [first, end) except the chunk anchors.
        const auto mark = [&](std::uint32_t first, std::uint32_t end, const std::string& prefab) {
            std::size_t cells = 0U;
            for (std::uint32_t y = first; y < end; ++y) for (std::uint32_t x = 0U; x < kSide; ++x) {
                if (x % 16U == 0U && y % 16U == 0U) continue;
                Map2DTile tile = runtime->Tile(owner, x, y).value_or(Map2DTile{});
                tile.prefab = prefab;
                std::string error;
                Require(runtime->SetTile(owner, x, y, 0, tile, &error), error.c_str());
                ++cells;
            }
            return cells;
        };
        const std::string marker = "/Game/Marker.kbprefab";
        const auto started = std::chrono::steady_clock::now();
        const std::size_t marked = mark(0U, kSide, marker);
        Require(marked == 4080U, "every cell but the chunk anchors gets a prefab");
        step();
        Require(scene.Entities().Count() - withChunks == marked, "one instance per prefab cell");
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        std::printf("map2d %zu prefab cells %lld ms\n", marked, static_cast<long long>(elapsed.count()));
        Require(elapsed < std::chrono::seconds(30), "thousands of prefab cells stay inside their time budget");

        static_cast<void>(mark(0U, kSide, marker));
        step();
        Require(scene.Entities().Count() - withChunks == marked && runtime->RebuiltChunksLastUpdate() == 0U,
            "painting a cell with the prefab it already has changes nothing and creates no duplicate");
        for (std::uint32_t x = 1U; x < 16U; ++x) {
            Map2DTile tile = *runtime->Tile(owner, x, 5U);
            tile.properties["visited"] = true;
            Require(runtime->SetTile(owner, x, 5U, 0, tile), "set data on a prefab cell");
        }
        step();
        Require(scene.Entities().Count() - withChunks == marked, "changing a prefab cell's data keeps its single instance");

        const std::size_t cleared = mark(0U, 8U, "");
        step();
        Require(scene.Entities().Count() - withChunks == marked - cleared, "clearing the prefab of cells removes exactly their instances");
        Require(runtime->Tile(owner, 3U, 5U).has_value() && runtime->Tile(owner, 3U, 5U)->properties.contains("visited"),
            "clearing a prefab keeps the rest of the cell");
        static_cast<void>(mark(0U, 8U, marker));
        step();
        Require(scene.Entities().Count() - withChunks == marked, "painting them again restores one instance each");

        static_cast<void>(mark(0U, kSide, ""));
        step();
        Require(scene.Entities().Count() == withChunks, "erasing every prefab leaves no instance behind");
        static_cast<void>(mark(0U, 3U, marker));
        step();
        Require(scene.Entities().Count() > withChunks, "the instances are back");
        scene.Runtime().SetPlaying(false);
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(scene.Entities().Count() == beforePlay, "Stop releases every chunk and every prefab instance");
        scene.Runtime().SetPlaying(true);
        step();
        Require(scene.Entities().Count() == withChunks, "the next Play starts from the authored map, which has no prefab cells");
    }
    // Prefab cells in the editor: painting one is a single undoable step, it survives a save, and redo and undo can be
    // repeated without leaving a stray prefab behind. Tile data reaches a game from the bytes the cooker stores: a map
    // loaded from the written bytes answers the same cell queries as the source file.
    {
        using kb::assets::Map2DTile;
        Map2DAsset map = kb::assets::MakeEmptyMap2DAsset(8U, 8U);
        kb::world2d::Map2DEditor editor{ map, library, assets };
        kb::world2d::Map2DChangeSet change;
        editor.SetChangeSet(&change);
        Map2DTile tile;
        tile.prefab = "/Game/Marker.kbprefab";
        tile.properties["owner"] = std::string{ "scout" };
        tile.properties["level"] = std::int64_t{ 7 };
        editor.SetTile(3U, 3U, 0, tile);
        change.Finish(map);
        Require(map.tiles.contains(Map2DTileKey(3U, 3U, 0)) && map.tiles.at(Map2DTileKey(3U, 3U, 0)).prefab == tile.prefab, "painting a prefab cell stores its prefab");
        const std::string written = kb::assets::Map2DAssetIO::Write(map);
        const auto cooked = kb::assets::Map2DAssetIO::Load(std::span<const std::uint8_t>{ reinterpret_cast<const std::uint8_t*>(written.data()), written.size() });
        Require(cooked.has_value() && cooked->tiles == map.tiles, "a prefab cell and its data survive the bytes the cooker stores");
        const Map2DTile& cell = cooked->tiles.at(Map2DTileKey(3U, 3U, 0));
        const auto level = kb::assets::Map2DCellProperty(*cooked, cell, "level");
        const auto owner = kb::assets::Map2DCellProperty(*cooked, cell, "owner");
        Require(level.has_value() && std::get<std::int64_t>(*level) == 7 && owner.has_value() && std::get<std::string>(*owner) == "scout" &&
                !kb::assets::Map2DCellProperty(*cooked, cell, "missing").has_value(),
            "typed cell data is read from the cooked bytes, and a missing name is absent");
        for (int round = 0; round < 3; ++round) {
            change.Undo(map);
            Require(map.tiles.empty(), "undoing a prefab paint removes the cell and its prefab");
            change.Redo(map);
            Require(map.tiles.size() == 1U && map.tiles.at(Map2DTileKey(3U, 3U, 0)).prefab == tile.prefab, "redoing a prefab paint restores it exactly once");
        }
    }
    // Tile collision: neighbouring equal shapes join into boxes that cover exactly the solid cells, different
    // shapes, floors and one-way ledges stay apart, and every box sits where its cells are.
    {
        using kb::assets::Map2DCollisionKind;
        kb::scene::Scene scene;
        const auto owner = scene.Entities().CreateEntity({ .name = "Shapes" });
        const auto build = [&](const Map2DAsset& map, std::vector<kb::assets::AssetId>* meshes = nullptr) {
            std::vector<kb::scene::ColliderComponent> colliders;
            std::string error;
            Require(kb::world2d::BuildMap2DCollision(scene, owner, map, colliders, meshes, &error), error.c_str());
            return colliders;
        };
        const auto newMap = [](kb::assets::Map2DOrientation orientation) {
            auto map = kb::assets::MakeEmptyMap2DAsset(8U, 8U);
            map.orientation = orientation;
            map.background = false;
            return map;
        };
        struct Cells { int x0, y0, x1, y1; };
        // The cells (x0..x1, y0..y1 exclusive; row 0 on top) a flat map's collider covers.
        const auto covered = [](const Map2DAsset& map, const kb::scene::ColliderComponent& collider) {
            const float size = map.tileSize;
            const auto round = [](float value) { return static_cast<int>(std::lround(value)); };
            return Cells{ round(collider.center.x / size + static_cast<float>(map.width) * 0.5F - collider.boxSize.x / size * 0.5F),
                round(static_cast<float>(map.height) * 0.5F - collider.center.z / size - collider.boxSize.z / size * 0.5F),
                round(collider.center.x / size + static_cast<float>(map.width) * 0.5F + collider.boxSize.x / size * 0.5F),
                round(static_cast<float>(map.height) * 0.5F - collider.center.z / size + collider.boxSize.z / size * 0.5F) };
        };

        // A pattern with gaps, bars and a block: the boxes tile the solid cells exactly, with no overlap.
        {
            auto map = newMap(kb::assets::Map2DOrientation::Flat);
            std::set<std::pair<int, int>> solid;
            for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
                if ((x * 7 + y * 3) % 5 < 3 || (x >= 2 && x <= 4 && y >= 2 && y <= 4)) {
                    map.tiles[Map2DTileKey(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 0)].solid = true;
                    solid.insert({ x, y });
                }
            }
            const auto colliders = build(map);
            std::set<std::pair<int, int>> seen;
            for (const auto& collider : colliders) {
                Require(collider.shape == kb::scene::ColliderShape::Box, "solid cells become boxes");
                const Cells cells = covered(map, collider);
                for (int y = cells.y0; y < cells.y1; ++y) for (int x = cells.x0; x < cells.x1; ++x) {
                    Require(seen.insert({ x, y }).second, "no two collision boxes overlap");
                }
            }
            Require(seen == solid, "the boxes cover exactly the solid cells");
            Require(colliders.size() < solid.size(), "neighbouring solid cells join into larger boxes");
        }
        // A 2 x 2 block is one box, placed on the right cells; a lifted floor lifts its box and stays apart.
        {
            auto map = newMap(kb::assets::Map2DOrientation::Flat);
            for (std::uint32_t y = 1U; y < 3U; ++y) for (std::uint32_t x = 1U; x < 3U; ++x) map.tiles[Map2DTileKey(x, y, 0)].solid = true;
            map.tiles[Map2DTileKey(1U, 3U, 1)].solid = true;
            const auto colliders = build(map);
            Require(colliders.size() == 2U, "a block is one box and another floor's cell is its own box");
            const auto block = std::ranges::find_if(colliders, [](const auto& collider) { return collider.boxSize.x > 1.5F; });
            Require(block != colliders.end(), "the block's box is two cells wide");
            Require(std::abs(block->center.x - -2.0F) < 0.0001F && std::abs(block->center.z - 2.0F) < 0.0001F &&
                    std::abs(block->center.y - 0.5F) < 0.0001F && std::abs(block->boxSize.x - 2.0F) < 0.0001F &&
                    std::abs(block->boxSize.z - 2.0F) < 0.0001F && std::abs(block->boxSize.y - 1.0F) < 0.0001F,
                "a 2 x 2 block's box is two cells square, one cell high, centred over its cells");
            const auto lifted = std::ranges::find_if(colliders, [](const auto& collider) { return collider.boxSize.x < 1.5F; });
            Require(std::abs(lifted->center.y - 3.5F) < 0.0001F, "a cell on floor 1 stands one floor height higher");
        }
        // Shapes that differ in any property do not join.
        {
            auto map = newMap(kb::assets::Map2DOrientation::Flat);
            map.tiles[Map2DTileKey(1U, 1U, 0)].solid = true;
            map.tiles[Map2DTileKey(2U, 1U, 0)].collision = kb::assets::Map2DCollision{ .kind = Map2DCollisionKind::Solid, .friction = 0.1F };
            map.tiles[Map2DTileKey(3U, 1U, 0)].collision = kb::assets::Map2DCollision{ .kind = Map2DCollisionKind::Solid, .trigger = true };
            map.tiles[Map2DTileKey(4U, 1U, 0)].collision = kb::assets::Map2DCollision{ .kind = Map2DCollisionKind::Solid, .trigger = true };
            const auto colliders = build(map);
            Require(colliders.size() == 3U, "different friction or trigger flags keep neighbouring cells apart; equal ones join");
            const auto trigger = std::ranges::find_if(colliders, [](const auto& collider) { return collider.trigger; });
            Require(trigger != colliders.end() && trigger->boxSize.x > 1.5F, "equal triggers join into one trigger box");
            const auto slippery = std::ranges::find_if(colliders, [](const auto& collider) { return collider.friction < 0.2F; });
            Require(slippery != colliders.end() && std::abs(slippery->friction - 0.1F) < 0.0001F, "the friction of a cell reaches its box");
        }
        // One-way ledges join along a row, not down a column, and stop bodies on their top face only.
        {
            auto map = newMap(kb::assets::Map2DOrientation::Flat);
            for (std::uint32_t x = 1U; x < 4U; ++x) map.tiles[Map2DTileKey(x, 2U, 0)].collision = kb::assets::Map2DCollision{ .kind = Map2DCollisionKind::OneWay };
            map.tiles[Map2DTileKey(1U, 3U, 0)].collision = kb::assets::Map2DCollision{ .kind = Map2DCollisionKind::OneWay };
            map.tiles[Map2DTileKey(6U, 6U, 0)].solid = true;
            const auto colliders = build(map);
            Require(colliders.size() == 3U, "a ledge of three cells is one collider, the cell below it another, a solid cell its own");
            for (const auto& collider : colliders) {
                if (collider.shape != kb::scene::ColliderShape::OneWayBox) continue;
                Require(std::abs(collider.center.y + collider.boxSize.y * 0.5F - 1.0F) < 0.0001F && collider.boxSize.y < 0.1F,
                    "a ledge is a thin slab whose top face is the top of its cell");
            }
            Require(std::ranges::count_if(colliders, [](const auto& collider) { return collider.shape == kb::scene::ColliderShape::OneWayBox; }) == 2,
                "two ledge colliders");
        }
        // Wall maps stand the tile plane upright: rows count down from the top of the map.
        {
            auto map = newMap(kb::assets::Map2DOrientation::Wall);
            map.tiles[Map2DTileKey(3U, 6U, 0)].solid = true;
            map.tiles[Map2DTileKey(4U, 6U, 0)].solid = true;
            const auto colliders = build(map);
            Require(colliders.size() == 1U, "two neighbouring cells of a wall map join");
            Require(std::abs(colliders[0].center.x) < 0.0001F && std::abs(colliders[0].center.y - 1.5F) < 0.0001F &&
                    std::abs(colliders[0].boxSize.x - 2.0F) < 0.0001F && std::abs(colliders[0].boxSize.y - 1.0F) < 0.0001F &&
                    std::abs(colliders[0].boxSize.z - 1.0F) < 0.0001F,
                "a wall map's box stands on row 6 of 8: its middle is 1.5 above the bottom");
        }
        // Slopes: the mesh is closed and faces outward, its sloped face looks up and away from the solid side,
        // equal slopes share one mesh, and a slope never joins a solid cell next to it.
        for (const bool wall : { true, false }) {
            for (const bool up : { true, false }) {
                auto map = newMap(wall ? kb::assets::Map2DOrientation::Wall : kb::assets::Map2DOrientation::Flat);
                const auto slope = up ? Map2DCollisionKind::SlopeUp : Map2DCollisionKind::SlopeDown;
                map.tiles[Map2DTileKey(2U, 3U, 0)].collision = kb::assets::Map2DCollision{ .kind = slope };
                map.tiles[Map2DTileKey(3U, 3U, 0)].collision = kb::assets::Map2DCollision{ .kind = slope };
                map.tiles[Map2DTileKey(4U, 3U, 0)].solid = true;
                std::vector<kb::assets::AssetId> meshes;
                const auto colliders = build(map, &meshes);
                Require(colliders.size() == 3U && meshes.size() == 1U, "two slopes and a solid cell are three colliders using one shared slope mesh");
                const auto first = std::ranges::find_if(colliders, [](const auto& collider) { return collider.shape == kb::scene::ColliderShape::Mesh; });
                Require(first != colliders.end() && first->meshAssetId != 0U && std::ranges::count_if(colliders, [&](const auto& collider) {
                    return collider.shape == kb::scene::ColliderShape::Mesh && collider.meshAssetId == first->meshAssetId; }) == 2,
                    "equal slopes share one mesh");
                const auto mesh = scene.Assets().Manager().AcquireLoaded<kb::assets::CollisionMeshAsset>(meshes.front());
                Require(mesh.IsLoaded(), "the slope mesh is published");
                const auto& positions = mesh->positions;
                const auto& indices = mesh->indices;
                // Directions: in a wall map the plane is X/Y and the thickness Z; in a flat map X/Z and Y.
                const auto lateral = [&](const kb::math::Vec3& p) { return p.x; };
                const auto vertical = [&](const kb::math::Vec3& p) { return wall ? p.y : p.z; };
                float volume = 0.0F;
                float best = -2.0F;
                for (std::size_t index = 0U; index + 2U < indices.size(); index += 3U) {
                    const auto& a = positions[indices[index]];
                    const auto& b = positions[indices[index + 1U]];
                    const auto& c = positions[indices[index + 2U]];
                    const kb::math::Vec3 ab{ b.x - a.x, b.y - a.y, b.z - a.z };
                    const kb::math::Vec3 ac{ c.x - a.x, c.y - a.y, c.z - a.z };
                    const kb::math::Vec3 cross{ ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x };
                    volume += (a.x * cross.x + a.y * cross.y + a.z * cross.z) / 6.0F;
                    // The sloped face: all three corners on the line through the cell's corners, whatever the thickness.
                    const float expectedRise = up ? 1.0F : -1.0F;
                    const auto onLine = [&](const kb::math::Vec3& p) { return std::abs(vertical(p) - expectedRise * lateral(p)) < 0.0001F; };
                    if (!(onLine(a) && onLine(b) && onLine(c))) continue;
                    const float length = std::sqrt(cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
                    const float normalLateral = cross.x / length;
                    const float normalVertical = (wall ? cross.y : cross.z) / length;
                    // Up and away from the solid side: SlopeUp rises to the right (normal leans left), SlopeDown leans right.
                    best = std::max(best, normalVertical * 0.7071F + normalLateral * (up ? -0.7071F : 0.7071F));
                }
                Require(std::abs(volume - 0.5F) < 0.0001F, "the slope mesh is closed, faces outward and is half a cell");
                Require(best > 0.999F, "the sloped face looks up and away from the solid side");
            }
        }
    }
    // Scripts read every type of tile data through the cell's own value, then the top graphic's default; a value of
    // another type is not found; a script's write is a cell override that never reaches the source asset.
    {
        using namespace kb::scene;
        using namespace kb::script;
        Scene scene;
        auto& manager = scene.Assets().Manager();
        Require(manager.Mounts().Mount("Game", assets), "mount script data assets");
        Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()), "register script data renderer");
        auto map = kb::assets::MakeEmptyMap2DAsset(8U, 8U);
        map.background = false;
        map.sheets.push_back({ .texture = "/Game/Sprites/ground.png", .material = "/Game/Maps/Materials/Test.kbmat", .columns = 4, .rows = 2 });
        auto& defaults = map.sheets[0].definitions[0].properties;
        defaults["title"] = std::string("stone");
        defaults["mass"] = std::int64_t{ 5 };
        defaults["ratio"] = 0.5;
        defaults["hard"] = true;
        map.tiles[Map2DTileKey(3U, 3U, 0)].ground = { .sheet = 0, .cell = 0 };
        const auto file = root / "ScriptData.kbmap2d";
        Require(kb::assets::Map2DAssetIO::Save(file, map), "save the script data map");
        const kb::assets::AssetId source{ 8789U };
        Require(manager.RegisterAsset({ .id = source, .type = "RenderMesh", .virtualPath = "/Game/ScriptData.kbmap2d", .physicalPath = file, .contentHash = 1U }),
            "register the script data map");
        const auto owner = scene.Entities().CreateEntity({ .name = "ScriptData" });
        kb::world2d::SetUpMap2DComponents(scene, owner, source.value, map);
        static_cast<void>(scene.Runtime().AddSceneSystem(std::make_unique<kb::world2d::Map2DRuntime>()));
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(scene.Runtime().DrainSceneSystemErrors().empty(), "the script data map runs without errors");
        ScriptRuntimeHost host(scene);
        Require(host.Succeeded(), "the script host installs the map API");
        const auto call = [&](const char* function, const char* name, std::optional<ScriptValue> value = std::nullopt) {
            std::vector<ScriptFunctionArgument> args{ { "entity", ScriptValue{ owner.Id(), ScriptValueType::Entity } }, { "x", ScriptValue{ 3 } },
                { "y", ScriptValue{ 3 } }, { "name", ScriptValue{ std::string{ name } } } };
            if (value) args.push_back({ "value", *value });
            const auto result = host.Functions().Call(function, args, { .scene = &scene });
            Require(result.executed && result.Succeeded(), function);
            return result;
        };
        Require(call("Map2D.GetText", "title").Output("found")->AsBool() && call("Map2D.GetText", "title").Output("value")->AsString() == "stone",
            "a script reads a graphic's text default");
        Require(call("Map2D.GetInteger", "mass").Output("value")->AsInt64() == 5 && call("Map2D.GetNumber", "ratio").Output("value")->AsDouble() == 0.5 &&
                call("Map2D.GetBool", "hard").Output("value")->AsBool(), "a script reads integer, number and flag defaults");
        Require(!call("Map2D.GetInteger", "title").Output("found")->AsBool() && !call("Map2D.GetText", "mass").Output("found")->AsBool() &&
                !call("Map2D.GetBool", "missing").Output("found")->AsBool(), "a value of another type, or none, is not found");
        Require(call("Map2D.SetText", "title", ScriptValue{ std::string{ "ice" } }).Output("applied")->AsBool() &&
                call("Map2D.SetBool", "hard", ScriptValue{ false }).Output("applied")->AsBool() &&
                call("Map2D.SetNumber", "ratio", ScriptValue{ 2.5 }).Output("applied")->AsBool() &&
                call("Map2D.SetInteger", "mass", ScriptValue{ std::int64_t{ -9 } }).Output("applied")->AsBool(), "a script sets every type");
        const auto hard = call("Map2D.GetBool", "hard");
        Require(call("Map2D.GetText", "title").Output("value")->AsString() == "ice" && call("Map2D.GetNumber", "ratio").Output("value")->AsDouble() == 2.5 &&
                call("Map2D.GetInteger", "mass").Output("value")->AsInt64() == -9 && hard.Output("found")->AsBool() && !hard.Output("value")->AsBool(),
            "the cell's own value beats the graphic's default, also when it is false");
        Require(call("Map2D.SetText", "mass", ScriptValue{ std::string{ "heavy" } }).Output("applied")->AsBool() &&
                !call("Map2D.GetInteger", "mass").Output("found")->AsBool() && call("Map2D.GetText", "mass").Output("value")->AsString() == "heavy",
            "a value of a new type under an existing name replaces the old one");
        Require(kb::assets::Map2DAssetIO::Load(file)->tiles == map.tiles, "script writes never change the source asset");
    }
    // The cell functions of the script API: read a cell with its pathfinding obstruction, replace and erase its
    // ground, refuse a bad sprite or a cell outside the map, and ask for the next step towards a target.
    {
        using namespace kb::scene;
        using namespace kb::script;
        Scene scene;
        auto& manager = scene.Assets().Manager();
        Require(manager.Mounts().Mount("Game", assets), "mount script cell assets");
        Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()), "register script cell renderer");
        auto map = kb::assets::MakeEmptyMap2DAsset(8U, 8U);
        map.background = false;
        map.sheets.push_back({ .texture = "/Game/Sprites/ground.png", .material = "/Game/Maps/Materials/Test.kbmat", .columns = 4, .rows = 2 });
        map.tiles[Map2DTileKey(3U, 3U, 0)].ground = { .sheet = 0, .cell = 0 };
        for (std::uint32_t y = 0U; y < 7U; ++y) map.tiles[Map2DTileKey(4U, y, 0)].solid = true;
        const auto file = root / "ScriptCells.kbmap2d";
        Require(kb::assets::Map2DAssetIO::Save(file, map), "save the script cell map");
        const kb::assets::AssetId source{ 8790U };
        Require(manager.RegisterAsset({ .id = source, .type = "RenderMesh", .virtualPath = "/Game/ScriptCells.kbmap2d", .physicalPath = file, .contentHash = 1U }),
            "register the script cell map");
        const auto owner = scene.Entities().CreateEntity({ .name = "ScriptCells" });
        kb::world2d::SetUpMap2DComponents(scene, owner, source.value, map);
        static_cast<void>(scene.Runtime().AddSceneSystem(std::make_unique<kb::world2d::Map2DRuntime>()));
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(scene.Runtime().DrainSceneSystemErrors().empty(), "the script cell map runs without errors");
        ScriptRuntimeHost host(scene);
        Require(host.Succeeded(), "the script host installs the cell functions");
        const auto call = [&](const char* function, int x, int y, std::vector<ScriptFunctionArgument> extra = {}) {
            std::vector<ScriptFunctionArgument> args{ { "entity", ScriptValue{ owner.Id(), ScriptValueType::Entity } }, { "x", ScriptValue{ x } }, { "y", ScriptValue{ y } } };
            args.insert(args.end(), extra.begin(), extra.end());
            return host.Functions().Call(function, args, { .scene = &scene });
        };
        const auto ground = call("Map2D.Get", 3, 3);
        Require(ground.Succeeded() && ground.Output("present")->AsBool() && ground.Output("sprite")->AsString() == "0:0" && !ground.Output("blocked")->AsBool(),
            "Get reads a painted cell that does not block");
        const auto wall = call("Map2D.Get", 4, 3);
        Require(wall.Succeeded() && wall.Output("present")->AsBool() && wall.Output("blocked")->AsBool(), "Get reports a solid cell as blocked");
        const auto empty = call("Map2D.Get", 0, 0);
        Require(empty.Succeeded() && !empty.Output("present")->AsBool() && empty.Output("sprite")->AsString().empty(), "Get reports an empty cell as absent");
        Require(!call("Map2D.Get", 8, 0).Succeeded() && !call("Map2D.Get", -1, 0).Succeeded(), "Get refuses a cell outside the map");

        const auto set = call("Map2D.Set", 1, 1, { { "sprite", ScriptValue{ std::string{ "0:5" } } } });
        Require(set.Succeeded() && set.Output("applied")->AsBool(), "Set replaces the ground of a cell");
        Require(call("Map2D.Get", 1, 1).Output("sprite")->AsString() == "0:5", "Get reads what Set wrote");
        Require(!call("Map2D.Set", 1, 1, { { "sprite", ScriptValue{ std::string{ "garbage" } } } }).Succeeded() &&
                !call("Map2D.Set", 1, 1, { { "sprite", ScriptValue{ std::string{ "0:99" } } } }).Succeeded(),
            "Set refuses text that is not a sprite and a sprite outside the sheet");
        Require(call("Map2D.Get", 1, 1).Output("sprite")->AsString() == "0:5", "a refused Set leaves the cell as it was");
        const auto erased = call("Map2D.Erase", 1, 1);
        Require(erased.Succeeded() && erased.Output("applied")->AsBool() && !call("Map2D.Get", 1, 1).Output("present")->AsBool(), "Erase clears the cell");

        // The wall of column 4 has its only gap in the bottom row.
        const auto step = call("Map2D.NextStep", 2, 3, { { "targetX", ScriptValue{ 6 } }, { "targetY", ScriptValue{ 3 } } });
        Require(step.Succeeded() && step.Output("found")->AsBool() && step.Output("cells")->AsInt() >= 6, "NextStep finds a way around the wall");
        Require(std::abs(step.Output("x")->AsInt() - 2) <= 1 && std::abs(step.Output("y")->AsInt() - 3) <= 1 &&
                (step.Output("x")->AsInt() != 2 || step.Output("y")->AsInt() != 3), "NextStep names a neighbouring cell as the next step");
        const auto blocked = call("Map2D.NextStep", 2, 3, { { "targetX", ScriptValue{ 4 } }, { "targetY", ScriptValue{ 3 } } });
        Require(blocked.Succeeded() && !blocked.Output("found")->AsBool() && blocked.Output("x")->AsInt() == 2 && blocked.Output("y")->AsInt() == 3,
            "NextStep stays where it is when the target cannot be reached");
        Require(kb::assets::Map2DAssetIO::Load(file)->tiles == map.tiles, "script cell edits never change the source asset");
    }
    // Pixel perfect view: for every viewport and density the view is a whole number of screen pixels per
    // texel, as large as the wanted height allows, and never changes the picture's proportions.
    {
        std::size_t checked = 0U;
        for (const float viewport : { 180.0F, 240.0F, 360.0F, 540.0F, 576.0F, 720.0F, 768.0F, 900.0F, 1000.0F, 1080.0F, 1200.0F, 1440.0F, 2160.0F }) {
            for (const float density : { 8.0F, 16.0F, 32.0F, 100.0F }) {
                for (const float wanted : { 6.0F, 11.0F, 15.5F }) {
                    const kb::world2d::PixelPerfectView view = kb::world2d::ComputePixelPerfectView(viewport, density, wanted);
                    Require(view.pixelPerfect && view.zoom >= 1U, "a valid viewport gives a pixel perfect view");
                    const double pixelsPerUnit = static_cast<double>(density) * view.zoom;
                    Require(std::abs(static_cast<double>(view.orthographicHeight) * pixelsPerUnit - viewport) <= viewport * 1.0e-6,
                        "the view height is a whole number of pixels per texel");
                    Require(std::abs(static_cast<double>(view.unitsPerPixel) * pixelsPerUnit - 1.0) <= 1.0e-6, "one screen pixel is 1/zoom of a texel");
                    if (viewport >= wanted * density) {
                        Require(view.orthographicHeight >= wanted - 1.0e-4F, "the view shows at least the wanted height");
                        Require(viewport < wanted * density * static_cast<float>(view.zoom + 1U) + 1.0e-3F, "the zoom is the largest that shows it");
                    } else {
                        Require(view.zoom == 1U, "a viewport shorter than the wanted height shows one pixel per texel");
                    }
                    ++checked;
                }
            }
        }
        Require(checked == 13U * 4U * 3U, "every combination was checked");
        // A window drag: every height keeps whole pixels per texel and the zoom never shrinks as the window grows.
        std::uint32_t previousZoom = 0U;
        for (float viewport = 120.0F; viewport <= 2400.0F; viewport += 1.0F) {
            const kb::world2d::PixelPerfectView view = kb::world2d::ComputePixelPerfectView(viewport, 16.0F, 11.0F);
            Require(std::abs(static_cast<double>(view.orthographicHeight) * 16.0 * view.zoom - viewport) <= viewport * 1.0e-6 && view.zoom >= previousZoom,
                "resizing the window keeps whole pixels per texel and a zoom that only grows");
            previousZoom = view.zoom;
        }
        for (const float bad : { 0.0F, -1.0F, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
            Require(!kb::world2d::ComputePixelPerfectView(bad, 16.0F, 11.0F).pixelPerfect && !kb::world2d::ComputePixelPerfectView(720.0F, bad, 11.0F).pixelPerfect &&
                    !kb::world2d::ComputePixelPerfectView(720.0F, 16.0F, bad).pixelPerfect,
                "values that are not positive numbers give no pixel perfect view");
        }
        Require(kb::world2d::ComputePixelPerfectView(720.0F, 0.0F, 11.0F).orthographicHeight == 11.0F, "without a pixel density the wanted height is kept");
    }
    // Resting on whole screen pixels: a slow fractional move steps one pixel at a time, never back, never
    // between pixels, so that the sprites do not shimmer.
    {
        const float pixel = 1.0F / 48.0F;
        float previous = kb::world2d::SnapToPixels(-1.0F, pixel);
        std::size_t steps = 0U;
        for (int index = -1000; index <= 1000; ++index) {
            const float x = static_cast<float>(index) * 0.001F;
            const float snapped = kb::world2d::SnapToPixels(x, pixel);
            const double pixels = static_cast<double>(snapped) / pixel;
            Require(std::abs(pixels - std::round(pixels)) < 1.0e-3, "a snapped position is a whole number of pixels");
            Require(std::abs(snapped - x) <= pixel * 0.5F + 1.0e-6F, "snapping moves at most half a pixel");
            Require(snapped >= previous, "snapping never moves backwards");
            Require(snapped - previous < pixel * 1.5F, "snapping moves at most one pixel at a time");
            Require(kb::world2d::SnapToPixels(snapped, pixel) == snapped, "a snapped position stays where it is");
            if (snapped != previous) ++steps;
            previous = snapped;
        }
        Require(steps >= 95U && steps <= 97U, "two units of movement make 96 pixel steps");
        Require(kb::world2d::SnapToPixels(0.37F, 0.0F) == 0.37F && kb::world2d::SnapToPixels(0.37F, -1.0F) == 0.37F, "no pixel size leaves the position alone");
        Require(kb::world2d::SnapToPixels(123456.789F, pixel) > 123456.0F, "a far position still snaps");
    }
    // The follow camera: drag margins, smoothing that does not depend on the frame rate, teleports, map bounds.
    {
        using kb::world2d::Camera2DFollow;
        Camera2DFollow frame;
        frame.halfWidth = 5.0F;
        frame.halfHeight = 5.0F;
        frame.clamp = true;
        frame.area = { -10.0F, -10.0F, 10.0F, 10.0F };
        frame.target = { 100.0F, -100.0F };
        auto at = kb::world2d::StepCamera2DFollow(frame);
        Require(at[0] == 5.0F && at[1] == -5.0F, "the first frame goes to the target, kept inside the map");
        frame.target = { 2.0F, 3.0F };
        at = kb::world2d::StepCamera2DFollow(frame);
        Require(at[0] == 2.0F && at[1] == 3.0F, "a target inside the map is followed exactly");
        frame.halfWidth = 15.0F;
        at = kb::world2d::StepCamera2DFollow(frame);
        Require(at[0] == 0.0F && at[1] == 3.0F, "a map narrower than the view stays centred across");
        frame.halfWidth = 5.0F;
        frame.clamp = false;
        frame.target = { 100.0F, 0.0F };
        Require(kb::world2d::StepCamera2DFollow(frame)[0] == 100.0F, "without bounds the camera follows anywhere");

        Camera2DFollow still;
        still.started = true;
        still.deadZoneWidth = 2.0F;
        still.deadZoneHeight = 1.0F;
        still.target = { 0.9F, 0.4F };
        at = kb::world2d::StepCamera2DFollow(still);
        Require(at[0] == 0.0F && at[1] == 0.0F, "a target inside the drag margins does not move the camera");
        still.target = { 3.0F, -2.0F };
        at = kb::world2d::StepCamera2DFollow(still);
        Require(std::abs(at[0] - 2.0F) < 1.0e-6F && std::abs(at[1] - -1.5F) < 1.0e-6F, "a target past the margins drags the camera, which keeps the margin");

        Camera2DFollow smooth;
        smooth.started = true;
        smooth.smoothingSeconds = 0.5F;
        smooth.target = { 10.0F, 0.0F };
        Camera2DFollow once = smooth;
        once.deltaSeconds = 0.1F;
        const auto single = kb::world2d::StepCamera2DFollow(once);
        Camera2DFollow half = smooth;
        half.deltaSeconds = 0.05F;
        auto twice = kb::world2d::StepCamera2DFollow(half);
        half.current = twice;
        twice = kb::world2d::StepCamera2DFollow(half);
        Require(std::abs(single[0] - twice[0]) < 1.0e-4F && single[0] > 1.0F && single[0] < 2.0F, "smoothing gives the same result at any frame rate");
        Camera2DFollow second = smooth;
        second.deltaSeconds = 0.5F;
        Require(std::abs(kb::world2d::StepCamera2DFollow(second)[0] - 10.0F * (1.0F - std::exp(-1.0F))) < 1.0e-4F, "after the smoothing time about two thirds of the way is covered");
        Camera2DFollow frozen = smooth;
        frozen.deltaSeconds = 0.0F;
        Require(kb::world2d::StepCamera2DFollow(frozen)[0] == 0.0F, "no time passing means no movement");
        Camera2DFollow instant = smooth;
        instant.smoothingSeconds = 0.0F;
        instant.deltaSeconds = 0.0F;
        Require(kb::world2d::StepCamera2DFollow(instant)[0] == 10.0F, "no smoothing lands on the target");
        Camera2DFollow jump = smooth;
        jump.deltaSeconds = 1.0F / 60.0F;
        jump.teleportDistance = 15.0F;
        jump.target = { 50.0F, 0.0F };
        Require(kb::world2d::StepCamera2DFollow(jump)[0] == 50.0F, "a teleport is not smoothed");
        jump.target = { 14.0F, 0.0F };
        Require(kb::world2d::StepCamera2DFollow(jump)[0] < 1.0F, "a move shorter than the teleport distance is smoothed");

        Camera2DFollow converge = smooth;
        converge.smoothingSeconds = 0.2F;
        converge.deltaSeconds = 1.0F / 60.0F;
        for (int frameIndex = 0; frameIndex < 600; ++frameIndex) converge.current = kb::world2d::StepCamera2DFollow(converge);
        Require(std::abs(converge.current[0] - 10.0F) < 1.0e-3F, "the camera comes to rest on the target");
        // The window gets smaller: the same map and the same target keep the view inside the map.
        Camera2DFollow resized;
        resized.started = true;
        resized.clamp = true;
        resized.area = { -20.0F, -15.0F, 20.0F, 15.0F };
        resized.target = { 19.0F, 14.0F };
        for (const float viewHalf : { 5.0F, 8.0F, 11.25F, 14.0F, 16.0F }) {
            resized.halfHeight = viewHalf;
            resized.halfWidth = viewHalf * 16.0F / 9.0F;
            const auto position = kb::world2d::StepCamera2DFollow(resized);
            Require(position[1] + viewHalf <= 15.0F + 1.0e-4F || position[1] == 0.0F, "the view stays inside the map as the window changes");
            Require(position[0] + resized.halfWidth <= 20.0F + 1.0e-4F || position[0] == 0.0F, "and across");
        }
    }
    // The camera settings are part of the map file: they keep their values, an old file has none, and nonsense is refused.
    {
        auto map = kb::assets::MakeEmptyMap2DAsset(12U, 8U);
        std::string error;
        const auto plain = kb::assets::Map2DAssetIO::Write(map);
        Require(plain.find("camera") == std::string::npos, "a map without camera settings writes no camera line");
        map.camera = { .pixelsPerUnit = 16.0F, .followTarget = "Hero One", .smoothingSeconds = 0.25F, .deadZoneWidth = 1.5F, .deadZoneHeight = 0.5F, .clampToMap = true };
        const auto loaded = kb::assets::Map2DAssetIO::Parse(kb::assets::Map2DAssetIO::Write(map), &error);
        Require(loaded.has_value() && loaded->camera == map.camera, "camera settings survive the file");
        const auto without = kb::assets::Map2DAssetIO::Parse(plain, &error);
        Require(without.has_value() && without->camera == kb::assets::Map2DCamera{}, "a file without a camera line keeps the plain camera");
        for (const auto& change : std::initializer_list<void (*)(kb::assets::Map2DCamera&)>{
                 [](kb::assets::Map2DCamera& camera) { camera.pixelsPerUnit = -1.0F; },
                 [](kb::assets::Map2DCamera& camera) { camera.pixelsPerUnit = std::numeric_limits<float>::quiet_NaN(); },
                 [](kb::assets::Map2DCamera& camera) { camera.smoothingSeconds = 61.0F; },
                 [](kb::assets::Map2DCamera& camera) { camera.deadZoneWidth = -0.5F; },
                 [](kb::assets::Map2DCamera& camera) { camera.deadZoneHeight = 300.0F; },
                 [](kb::assets::Map2DCamera& camera) { camera.followTarget = std::string(129U, 'n'); },
                 [](kb::assets::Map2DCamera& camera) { camera.followTarget = "bad\"name"; } }) {
            auto broken = map;
            change(broken.camera);
            Require(!kb::assets::IsMap2DAssetValid(broken), "camera settings that are not sensible are refused");
        }
        std::string text = kb::assets::Map2DAssetIO::Write(map);
        const std::size_t at = text.find("camera ");
        Require(at != std::string::npos, "the camera line is written");
        text.replace(at, 7U, "camera x ");
        Require(!kb::assets::Map2DAssetIO::Parse(text).has_value(), "a camera line with a word instead of a number is refused");
    }
    // At runtime: the play camera of both presets frames the viewport in whole pixels per texel, follows its target,
    // stays on whole pixels, is bounded by the map, does not smooth a teleport and still frames the view after a resize.
    {
        struct Rig {
            kb::scene::Scene scene;
            kb::scene::SceneEntity owner{};
            kb::scene::SceneEntity camera{};
            kb::scene::SceneEntity hero{};
        };
        int rigNumber = 0;
        const auto build = [&](kb::assets::Map2DOrientation orientation, kb::assets::Map2DCamera settings) {
            auto rig = std::make_unique<Rig>();
            auto& scene = rig->scene;
            auto& manager = scene.Assets().Manager();
            Require(manager.Mounts().Mount("Game", assets), "mount camera assets");
            Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()), "register camera renderer");
            auto map = kb::assets::MakeEmptyMap2DAsset(40U, 30U);
            map.background = false;
            map.orientation = orientation;
            map.camera = std::move(settings);
            map.sheets.push_back({ .texture = "/Game/Sprites/ground.png", .material = "/Game/Maps/Materials/Test.kbmat", .columns = 4, .rows = 2 });
            map.tiles[Map2DTileKey(1U, 1U, 0)].ground = { .sheet = 0, .cell = 0 };
            const auto file = root / ("Camera" + std::to_string(++rigNumber) + ".kbmap2d");
            Require(kb::assets::Map2DAssetIO::Save(file, map), "save the camera map");
            const kb::assets::AssetId source{ 8900U + static_cast<std::uint64_t>(rigNumber) };
            Require(manager.RegisterAsset({ .id = source, .type = "RenderMesh", .virtualPath = "/Game/Camera" + std::to_string(rigNumber) + ".kbmap2d",
                .physicalPath = file, .contentHash = 1U }), "register the camera map");
            rig->owner = scene.Entities().CreateEntity({ .name = "CameraMap" });
            kb::world2d::SetUpMap2DComponents(scene, rig->owner, source.value, map);
            kb::world2d::Map2DViewResult view{};
            Require(kb::world2d::SetUpMap2DView(scene, rig->owner, map, assets, &view), "the camera preset must be set up");
            rig->camera = view.camera;
            rig->hero = scene.Entities().CreateEntity({ .name = "Hero" });
            static_cast<void>(scene.Runtime().AddSceneSystem(std::make_unique<kb::world2d::Map2DRuntime>()));
            return rig;
        };
        const auto place = [](Rig& rig, double across, double up, bool wall) {
            rig.scene.Transforms().SetLocalTranslation(rig.hero, kb::math::DVec3{ across, wall ? up : 0.0, wall ? 0.0 : up });
        };
        const auto frames = [](Rig& rig, int count, float width, float height) {
            for (int index = 0; index < count; ++index) {
                static_cast<void>(rig.scene.UI().SetViewport(width, height));
                static_cast<void>(rig.scene.Runtime().Update(1.0F / 60.0F));
            }
            Require(rig.scene.Runtime().DrainSceneSystemErrors().empty(), "the camera map runs without errors");
        };
        const auto position = [](Rig& rig, bool wall) {
            const auto local = rig.scene.Transforms().Get(rig.camera).localPosition;
            return std::array<float, 2>{ local.x, wall ? local.y : local.z };
        };
        const auto height = [](Rig& rig) { return rig.scene.Components().Cameras().TryGet(rig.camera)->orthographicHeight; };

        for (const bool wall : { false, true }) {
            const auto orientation = wall ? kb::assets::Map2DOrientation::Wall : kb::assets::Map2DOrientation::Flat;
            auto rig = build(orientation, { .pixelsPerUnit = 16.0F, .followTarget = "Hero", .clampToMap = true });
            place(*rig, 3.3, wall ? 12.2 : 2.2, wall);
            frames(*rig, 3, 1280.0F, 720.0F);
            // 11 wanted tiles are 176 texels: 4 pixels per texel fit in 720.
            Require(std::abs(height(*rig) - 11.25F) < 1.0e-5F, "the view is 720 pixels at 4 pixels per texel");
            const auto at = position(*rig, wall);
            Require(std::abs(at[0] - 211.0F / 64.0F) < 1.0e-5F && std::abs(at[1] - (wall ? 12.2F : 2.2F)) <= 1.0F / 128.0F + 1.0e-5F,
                "the camera follows the target and rests on whole pixels");
            const double pixels = at[1] * 64.0;
            Require(std::abs(pixels - std::round(pixels)) < 1.0e-3, "also up the screen");

            // The target creeps by a fraction of a pixel: the camera stays or steps one pixel, and never moves back.
            float previous = at[0];
            for (int index = 1; index <= 120; ++index) {
                place(*rig, 3.3 + index * 0.002, wall ? 12.2 : 2.2, wall);
                frames(*rig, 1, 1280.0F, 720.0F);
                const float now = position(*rig, wall)[0];
                Require(now >= previous && now - previous < 1.0F / 64.0F * 1.5F, "a creeping target moves the camera a pixel at a time");
                previous = now;
            }
            // Another window: 1000 pixels is 5 pixels per texel and a view of 12.5 units.
            frames(*rig, 2, 1778.0F, 1000.0F);
            Require(std::abs(height(*rig) - 12.5F) < 1.0e-5F, "a taller window changes the pixels per texel, not the pixel grid");
            // Bounds: the map is 40 x 30; the view is 12.5 high and 22.2 wide.
            place(*rig, 500.0, 500.0, wall);
            frames(*rig, 2, 1778.0F, 1000.0F);
            const auto corner = position(*rig, wall);
            const float halfWidth = 12.5F * 0.5F * (1778.0F / 1000.0F);
            Require(std::abs(corner[0] - (20.0F - halfWidth)) < 0.02F, "the camera stops where the view meets the edge of the map");
            Require(std::abs(corner[1] - (wall ? 30.0F - 6.25F : 15.0F - 6.25F)) < 0.02F, "and the top");
        }

        // Smoothing is not applied to a teleport; a short move is eased.
        auto eased = build(kb::assets::Map2DOrientation::Flat, { .followTarget = "Hero", .smoothingSeconds = 1.0F });
        place(*eased, 0.0, 0.0, false);
        frames(*eased, 2, 1280.0F, 720.0F);
        place(*eased, 4.0, 0.0, false);
        frames(*eased, 1, 1280.0F, 720.0F);
        const float step = position(*eased, false)[0];
        Require(step > 0.01F && step < 0.2F, "a short move of the target is smoothed");
        place(*eased, 18.0, 0.0, false);
        frames(*eased, 1, 1280.0F, 720.0F);
        Require(std::abs(position(*eased, false)[0] - 18.0F) < 1.0e-4F, "a teleport of the target is followed at once");
        Require(std::abs(height(*eased) - 11.0F) < 1.0e-5F, "without a pixel density the view keeps the height of the map's view");

        // No settings: the camera stays where the preset put it.
        auto plain = build(kb::assets::Map2DOrientation::Flat, {});
        place(*plain, 7.0, 7.0, false);
        frames(*plain, 3, 1280.0F, 720.0F);
        Require(position(*plain, false)[0] == 0.0F && position(*plain, false)[1] == 0.0F && std::abs(height(*plain) - 11.0F) < 1.0e-5F,
            "a map without camera settings keeps its still camera");
    }
    // Golden: the borders of a plus-shaped grass patch on an empty map, as every cell lists them (x,y:ground+borders). A change
    // here changes how existing maps look: update it only on purpose.
    {
        Map2DAsset map = kb::assets::MakeEmptyMap2DAsset(12U, 12U);
        kb::world2d::Map2DEditor editor{ map, library, assets };
        Require(editor.PaintGround(*grassBrush, { 4U, 5U, 6U, 5U }, 0U) && editor.PaintGround(*grassBrush, { 5U, 4U, 5U, 6U }, 0U), "the patch must paint");
        std::string golden;
        for (const auto& [key, tile] : map.tiles) {
            golden += std::to_string(kb::assets::Map2DTileKeyX(key)) + "," + std::to_string(kb::assets::Map2DTileKeyY(key)) + ":";
            golden += tile.ground.IsValid() ? std::to_string(tile.ground.cell) : std::string("-");
            for (const Map2DSprite border : tile.borders) golden += "+" + std::to_string(border.cell);
            golden += ";";
        }
        const std::string expected =
            "4,3:-+16;5,3:-+17;6,3:-+18;3,4:-+16;4,4:-+28;5,4:25;6,4:-+27;7,4:-+18;3,5:-+24;4,5:25;5,5:25;6,5:25;7,5:-+26;3,6:-+32;4,6:-+20;5,6:25;6,6:-+19;7,6:-+34;4,7:-+32;5,7:-+33;6,7:-+34;";
        Require(golden == expected, ("the autoborder of a plus-shaped patch changed: " + golden).c_str());
    }
    // Top-down paths: a body wider than a cell, diagonal policy, cell costs, the visited budget and why a path is empty.
    {
        using kb::assets::Map2DPathStatus;
        auto map = kb::assets::MakeEmptyMap2DAsset(12U, 12U);
        for (unsigned x = 0; x < 12; ++x) if (x != 5U) map.tiles[Map2DTileKey(x, 6U, 0)].solid = true;
        const kb::assets::Map2DPathResult thin = kb::assets::FindMap2DPathResult(map, { 5, 1 }, { 5, 10 });
        Require(thin.status == Map2DPathStatus::Found && thin.path.size() == 10U, "a body of one cell passes a one cell gap");
        const kb::assets::Map2DPathResult wide = kb::assets::FindMap2DPathResult(map, { 5, 1 }, { 5, 10 }, { .clearanceCells = 1U });
        Require(wide.status == Map2DPathStatus::NoPath && wide.path.empty(), "a body of three cells does not fit a one cell gap");
        map.tiles.clear();
        for (unsigned x = 0; x < 12; ++x) if (x < 4U || x > 6U) map.tiles[Map2DTileKey(x, 6U, 0)].solid = true;
        Require(kb::assets::FindMap2DPathResult(map, { 5, 1 }, { 5, 10 }, { .clearanceCells = 1U }).status == Map2DPathStatus::Found,
            "a body of three cells passes a gap of three");
        auto tall = kb::assets::MakeEmptyMap2DAsset(20U, 20U);
        for (unsigned x = 0; x < 20; ++x) if (x < 4U || x > 6U) tall.tiles[Map2DTileKey(x, 10U, 0)].solid = true;
        Require(kb::assets::FindMap2DPathResult(tall, { 5, 3 }, { 5, 16 }, { .clearanceCells = 1U }).status == Map2DPathStatus::Found &&
                kb::assets::FindMap2DPathResult(tall, { 5, 3 }, { 5, 16 }, { .clearanceCells = 2U }).status == Map2DPathStatus::NoPath,
            "a body of five cells does not fit a gap of three");

        map.tiles.clear();
        const kb::assets::Map2DPathResult four = kb::assets::FindMap2DPathResult(map, { 0, 0 }, { 4, 4 });
        const kb::assets::Map2DPathResult eight = kb::assets::FindMap2DPathResult(map, { 0, 0 }, { 4, 4 }, { .diagonal = kb::assets::Map2DDiagonalPolicy::Allowed });
        Require(four.path.size() == 9U && eight.path.size() == 5U, "diagonal steps shorten a path across open ground");
        map.tiles[Map2DTileKey(2U, 1U, 0)].solid = true;
        map.tiles[Map2DTileKey(1U, 2U, 0)].solid = true;
        const kb::assets::Map2DPathResult cut = kb::assets::FindMap2DPathResult(map, { 1, 1 }, { 2, 2 }, { .diagonal = kb::assets::Map2DDiagonalPolicy::Allowed });
        const kb::assets::Map2DPathResult careful = kb::assets::FindMap2DPathResult(map, { 1, 1 }, { 2, 2 }, { .diagonal = kb::assets::Map2DDiagonalPolicy::NoCornerCutting });
        Require(cut.path.size() == 2U && careful.status == Map2DPathStatus::Found && careful.path.size() > 2U,
            "a policy that does not cut corners goes around two blocked cells that a free diagonal squeezes between");

        map.tiles.clear();
        kb::assets::Map2DTile mud;
        mud.properties["navCost"] = 100.0;
        for (unsigned x = 3; x < 6; ++x) map.tiles[Map2DTileKey(x, 1U, 0)] = mud;
        const kb::assets::Map2DPathResult straight = kb::assets::FindMap2DPathResult(map, { 0, 1 }, { 9, 1 });
        const kb::assets::Map2DPathResult detour = kb::assets::FindMap2DPathResult(map, { 0, 1 }, { 9, 1 }, { .costProperty = "navCost" });
        const auto touchesMud = [](const kb::assets::Map2DPathResult& result) {
            return std::ranges::any_of(result.path, [](const std::array<int, 2>& cell) { return cell[1] == 1 && cell[0] >= 3 && cell[0] <= 5; });
        };
        Require(straight.path.size() == 10U && detour.status == Map2DPathStatus::Found && !touchesMud(detour) && detour.path.size() > 10U,
            "a costly cell is avoided when a detour is cheaper, and ignored without the cost option");

        auto large = kb::assets::MakeEmptyMap2DAsset(128U, 128U);
        const kb::assets::Map2DPathResult big = kb::assets::FindMap2DPathResult(large, { 0, 0 }, { 127, 127 });
        Require(big.status == Map2DPathStatus::Found && big.visited < 65536U, "a path across a large open map stays inside the default budget");
        const kb::assets::Map2DPathResult tight = kb::assets::FindMap2DPathResult(large, { 0, 0 }, { 127, 127 }, { .maximumVisited = 100U });
        Require(tight.status == Map2DPathStatus::BudgetExceeded && tight.path.empty(), "a search that runs out of budget says so instead of saying there is no way");
        large.tiles[Map2DTileKey(0U, 1U, 0)].solid = true;
        large.tiles[Map2DTileKey(1U, 0U, 0)].solid = true;
        const kb::assets::Map2DPathResult boxed = kb::assets::FindMap2DPathResult(large, { 0, 0 }, { 127, 127 });
        Require(boxed.status == Map2DPathStatus::NoPath && boxed.visited == 1U, "a start walled in has no way, found at once");
        large.tiles[Map2DTileKey(127U, 127U, 0)].solid = true;
        Require(kb::assets::FindMap2DPathResult(large, { 5, 5 }, { 127, 127 }).status == Map2DPathStatus::EndBlocked &&
                kb::assets::FindMap2DPathResult(large, { 127, 127 }, { 5, 5 }).status == Map2DPathStatus::StartBlocked,
            "a blocked end and a blocked start are told apart");
    }
    // Platformer paths: falling down a ledge, a ceiling that stops a jump, a one-way platform entered from below.
    {
        using kb::assets::Map2DPathStatus;
        auto map = kb::assets::MakeEmptyMap2DAsset(12U, 12U);
        for (unsigned x = 0; x < 12; ++x) map.tiles[Map2DTileKey(x, 9U, 0)].solid = true;
        for (unsigned x = 0; x < 3; ++x) map.tiles[Map2DTileKey(x, 6U, 0)].solid = true;
        const kb::assets::Map2DPathResult fall = kb::assets::FindMap2DPathResult(map, { 1, 5 }, { 4, 8 }, { .platformer = true });
        bool falls = false;
        for (std::size_t index = 1U; index < fall.path.size(); ++index) falls = falls || fall.path[index][1] > fall.path[index - 1U][1];
        Require(fall.status == Map2DPathStatus::Found && falls, "a path off a ledge goes down by falling");

        map.tiles.clear();
        for (unsigned x = 0; x < 12; ++x) if (x != 4U && x != 5U) map.tiles[Map2DTileKey(x, 8U, 0)].solid = true;
        Require(kb::assets::FindMap2DPathResult(map, { 2, 7 }, { 7, 7 }, { .platformer = true }).status == Map2DPathStatus::Found,
            "a gap in the floor is jumped");
        // A low ceiling is a platform to the planner: the way over the gap then leads along its top, not across the floor.
        for (unsigned x = 1; x < 9; ++x) map.tiles[Map2DTileKey(x, 5U, 0)].solid = true;
        const kb::assets::Map2DPathResult overTop = kb::assets::FindMap2DPathResult(map, { 2, 7 }, { 7, 7 }, { .platformer = true });
        Require(overTop.status == Map2DPathStatus::Found &&
                std::ranges::any_of(overTop.path, [](const std::array<int, 2>& cell) { return cell[1] <= 4; }) &&
                std::ranges::none_of(overTop.path, [](const std::array<int, 2>& cell) { return cell[0] >= 4 && cell[0] <= 5 && cell[1] == 7; }),
            "with a low ceiling above the gap the way leads over the ceiling, never across the gap on the floor");
        // A ceiling that fills the space up to the top of the map leaves no arc and no platform: the gap cannot be crossed.
        for (unsigned y = 0; y < 5; ++y) for (unsigned x = 1; x < 9; ++x) map.tiles[Map2DTileKey(x, y, 0)].solid = true;
        Require(kb::assets::FindMap2DPathResult(map, { 2, 7 }, { 7, 7 }, { .platformer = true }).status != Map2DPathStatus::Found,
            "a ceiling that the arc of the jump would hit makes the jump impossible");

        map.tiles.clear();
        for (unsigned x = 0; x < 12; ++x) map.tiles[Map2DTileKey(x, 9U, 0)].solid = true;
        kb::assets::Map2DTile platform;
        platform.collision = kb::assets::Map2DCollision{ .kind = kb::assets::Map2DCollisionKind::OneWay };
        for (unsigned x = 0; x < 12; ++x) map.tiles[Map2DTileKey(x, 6U, 0)] = platform;
        Require(kb::assets::FindMap2DPathResult(map, { 4, 8 }, { 4, 5 }, { .platformer = true }).status == Map2DPathStatus::Found,
            "a one-way floor across the whole map is entered by jumping up through it");
        for (unsigned x = 0; x < 12; ++x) { map.tiles[Map2DTileKey(x, 6U, 0)] = kb::assets::Map2DTile{}; map.tiles[Map2DTileKey(x, 6U, 0)].solid = true; }
        Require(kb::assets::FindMap2DPathResult(map, { 4, 8 }, { 4, 5 }, { .platformer = true }).status != Map2DPathStatus::Found,
            "a solid floor across the whole map cannot be entered from below");
        // A sealed pocket is unreachable and says so.
        for (unsigned x = 8; x < 11; ++x) { map.tiles[Map2DTileKey(x, 4U, 0)].solid = true; map.tiles[Map2DTileKey(x, 6U, 0)].solid = true; }
        map.tiles[Map2DTileKey(8U, 5U, 0)].solid = true;
        map.tiles[Map2DTileKey(10U, 5U, 0)].solid = true;
        const kb::assets::Map2DPathResult sealed = kb::assets::FindMap2DPathResult(map, { 1, 8 }, { 9, 5 }, { .platformer = true });
        Require(sealed.status == Map2DPathStatus::NoPath && sealed.path.empty(), "a goal in a sealed pocket is reported as unreachable");
    }
    // Map limits: a map of 4096 cells a side is valid and one cell more is not, tiles on the last row and column are
    // kept exactly, cells outside the map are refused rather than wrapped, and shrinking a map is undoable.
    {
        Require(kb::assets::Map2DAssetIO::Parse("21kb-map2d 1\nsize 4096 4096\n").has_value(), "a map of 4096 by 4096 cells is valid");
        Require(!kb::assets::Map2DAssetIO::Parse("21kb-map2d 1\nsize 4097 4096\n").has_value() && !kb::assets::Map2DAssetIO::Parse("21kb-map2d 1\nsize 4096 4097\n").has_value(),
            "a map of 4097 cells in one direction is refused");
        auto corner = kb::assets::MakeEmptyMap2DAsset(4096U, 4096U);
        corner.tiles[Map2DTileKey(4095U, 4095U, 50)].solid = true;
        corner.tiles[Map2DTileKey(0U, 0U, -50)].solid = true;
        const std::optional<Map2DAsset> corners = kb::assets::Map2DAssetIO::Parse(kb::assets::Map2DAssetIO::Write(corner));
        Require(corners.has_value() && corners->tiles == corner.tiles, "tiles on the last row, last column and the outermost floors survive a save exactly");
        auto small = kb::assets::MakeEmptyMap2DAsset(8U, 8U);
        small.tiles[Map2DTileKey(7U, 7U, 0)].solid = true;
        Require(kb::assets::IsMap2DAssetValid(small), "a tile on the last cell of the map is valid");
        small.tiles[Map2DTileKey(8U, 7U, 0)].solid = true;
        Require(!kb::assets::IsMap2DAssetValid(small), "a tile one cell outside the map is invalid, not wrapped to the next row");
        Require(!kb::assets::Map2DAssetIO::Parse("21kb-map2d 1\nsize 8 8\nt 8 0 0 g=0:0\n").has_value() && !kb::assets::Map2DAssetIO::Parse("21kb-map2d 1\nsize 8 8\nt -1 0 0 g=0:0\n").has_value(),
            "an imported tile outside the map fails the whole import and nothing is truncated");
    }
    std::filesystem::remove_all(root);
    std::puts("kb_world2d_tests passed");
    return 0;
}
