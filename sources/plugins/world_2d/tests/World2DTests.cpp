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
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "kb/render/resources/RenderMap2DMeshBuilder.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"
#include "kb/render/scene/RenderScene.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
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
    std::filesystem::remove_all(root);
    std::puts("kb_world2d_tests passed");
    return 0;
}
