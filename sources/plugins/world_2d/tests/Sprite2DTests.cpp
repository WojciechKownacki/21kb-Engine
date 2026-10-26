#include "Sprite2DRuntime.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentAuthoring.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneHistory.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "kb/render/resources/RenderMap2DMeshBuilder.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>

namespace {

using kb::scene::Sprite2DComponent;

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

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

struct Extent {
    float minX = std::numeric_limits<float>::max(), maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max(), maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max(), maxZ = std::numeric_limits<float>::lowest();
    float minU = std::numeric_limits<float>::max(), maxU = std::numeric_limits<float>::lowest();
};

[[nodiscard]] Extent ExtentOf(const kb::render::RenderMeshAssetData& mesh) {
    Extent extent;
    for (const auto& vertex : mesh.tangentVertices) {
        extent.minX = std::min(extent.minX, vertex.x); extent.maxX = std::max(extent.maxX, vertex.x);
        extent.minY = std::min(extent.minY, vertex.y); extent.maxY = std::max(extent.maxY, vertex.y);
        extent.minZ = std::min(extent.minZ, vertex.z); extent.maxZ = std::max(extent.maxZ, vertex.z);
        extent.minU = std::min(extent.minU, vertex.u); extent.maxU = std::max(extent.maxU, vertex.u);
    }
    return extent;
}

[[nodiscard]] bool Near(float a, float b, float tolerance = 0.0005F) { return std::abs(a - b) <= tolerance; }

[[nodiscard]] kb::render::RenderMeshAssetData BuiltMesh(const Sprite2DComponent& sprite, std::uint32_t width, std::uint32_t height, std::uint32_t cell) {
    const auto map = kb::world2d::BuildSprite2DMap(sprite, "/Game/Sprites/hero.png", "", width, height, cell);
    Require(map.has_value(), "a valid sprite builds a map");
    auto mesh = kb::render::RenderMap2DMeshBuilder::Build(*map, 0.0F, 0.0);
    Require(mesh.has_value() && !mesh->tangentVertices.empty(), "the sprite map builds a mesh");
    return std::move(*mesh);
}

} // namespace

int main() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "kb_world2d_sprite_tests";
    std::filesystem::remove_all(root);
    const std::filesystem::path assets = root / "Assets";
    WritePngHeader(assets / "Sprites" / "hero.png", 64U, 32U);

    // Which cell a sprite shows: a still one, a looping animation, a one-shot that stops on its last frame.
    {
        Sprite2DComponent sprite{ .cell = 2U, .frameCount = 4U, .framesPerSecond = 4.0F };
        Require(kb::world2d::Sprite2DFrameCell(sprite, 0.0) == 2U && kb::world2d::Sprite2DFrameCell(sprite, 0.26) == 3U &&
                kb::world2d::Sprite2DFrameCell(sprite, 0.76) == 5U && kb::world2d::Sprite2DFrameCell(sprite, 1.01) == 2U,
            "a looping animation advances by frame and wraps");
        sprite.loop = false;
        Require(kb::world2d::Sprite2DFrameCell(sprite, 0.76) == 5U && kb::world2d::Sprite2DFrameCell(sprite, 100.0) == 5U &&
                kb::world2d::Sprite2DFrameCell(sprite, -1.0) == 2U,
            "a one-shot animation stops on its last frame, and a negative clock is the first");
        sprite.frameCount = 1U;
        Require(kb::world2d::Sprite2DFrameCell(sprite, 9.0) == 2U, "a sprite with one frame is still");
        sprite.frameCount = 0U;
        Require(kb::world2d::Sprite2DFrameCell(sprite, 9.0) == 2U, "a sprite with no frames is still");
    }

    // The sheet layout: whole image, a sheet of equal cells, margin and spacing, and an image too small for a cell.
    {
        Sprite2DComponent sprite{};
        const auto whole = kb::world2d::Sprite2DLayout(sprite, 64U, 32U);
        Require(whole && whole->cellWidth == 64U && whole->cellHeight == 32U && whole->columns == 1U && whole->rows == 1U, "no cell size uses the whole image");
        sprite.cellWidth = 16U;
        const auto square = kb::world2d::Sprite2DLayout(sprite, 64U, 32U);
        Require(square && square->cellHeight == 16U && square->columns == 4U && square->rows == 2U, "a cell width alone makes square cells");
        sprite.cellHeight = 32U;
        const auto tall = kb::world2d::Sprite2DLayout(sprite, 64U, 32U);
        Require(tall && tall->columns == 4U && tall->rows == 1U, "a cell height makes rectangular cells");
        sprite.cellWidth = 13U; sprite.cellHeight = 13U; sprite.margin = 2U; sprite.spacing = 2U;
        const auto spaced = kb::world2d::Sprite2DLayout(sprite, 64U, 32U);
        Require(spaced && spaced->columns == 4U && spaced->rows == 2U, "margin and spacing are left out of the cells");
        sprite.cellWidth = 128U; sprite.cellHeight = 128U; sprite.margin = 0U; sprite.spacing = 0U;
        Require(!kb::world2d::Sprite2DLayout(sprite, 64U, 32U).has_value(), "an image smaller than one cell has no layout");
        std::string error;
        Require(!kb::world2d::BuildSprite2DMap(sprite, "/Game/Sprites/hero.png", "", 64U, 32U, 0U, &error).has_value() && !error.empty(), "an impossible sheet is refused with a reason");
    }

    // Geometry: size follows pixels per unit, the pivot sits on the origin, flipping mirrors about the pivot, and the
    // UV rectangle is the cell.
    {
        Sprite2DComponent sprite{ .cellWidth = 32U, .pixelsPerUnit = 32.0F, .pivotX = 0.5F, .pivotY = 1.0F };
        auto mesh = BuiltMesh(sprite, 64U, 32U, 1U);
        auto extent = ExtentOf(mesh);
        Require(Near(extent.minX, -0.5F) && Near(extent.maxX, 0.5F) && Near(extent.minY, 0.0F) && Near(extent.maxY, 1.0F),
            "a 32 pixel sprite at 32 pixels per unit is one unit, standing on its bottom pivot");
        Require(Near(extent.minU, 32.5F / 64.0F) && Near(extent.maxU, 63.5F / 64.0F), "cell 1 of a 2 by 1 sheet reads the right half of the image");
        sprite.pivotY = 0.0F;
        extent = ExtentOf(BuiltMesh(sprite, 64U, 32U, 0U));
        Require(Near(extent.minY, -1.0F) && Near(extent.maxY, 0.0F), "a top pivot hangs the sprite below the origin");
        sprite.pivotY = 0.5F; sprite.pivotX = 0.0F;
        extent = ExtentOf(BuiltMesh(sprite, 64U, 32U, 0U));
        Require(Near(extent.minX, 0.0F) && Near(extent.maxX, 1.0F), "a left pivot puts the sprite to the right of the origin");
        sprite.flipX = true;
        extent = ExtentOf(BuiltMesh(sprite, 64U, 32U, 0U));
        Require(Near(extent.minX, -1.0F) && Near(extent.maxX, 0.0F), "flipping about a left pivot mirrors the sprite to its left");
        sprite = Sprite2DComponent{ .cellWidth = 32U, .cellHeight = 16U, .pixelsPerUnit = 16.0F, .pivotY = 0.0F, .wall = false };
        extent = ExtentOf(BuiltMesh(sprite, 64U, 32U, 0U));
        Require(Near(extent.minX, -1.0F) && Near(extent.maxX, 1.0F) && Near(extent.minY, extent.maxY) && Near(extent.maxZ, 0.0F) && Near(extent.minZ, -1.0F),
            "a flat 32 by 16 sprite lies on the ground with its top pivot at the origin");
        sprite.tintA = 0.5F;
        const auto tinted = kb::world2d::BuildSprite2DMap(sprite, "/Game/Sprites/hero.png", "", 64U, 32U, 0U);
        Require(tinted && (tinted->tiles.begin()->second.items.front().tint & 0xFFU) == 128U, "the tint reaches the tile");
        auto lifted = kb::world2d::BuildSprite2DMap(Sprite2DComponent{ .cellWidth = 32U }, "/Game/Sprites/hero.png", "/Game/M.kbmat", 64U, 32U, 0U);
        Require(lifted && lifted->sheets.front().material == "/Game/M.kbmat", "a material is passed to the sheet");
    }

    // A scene: the sprite draws through a hidden child, rebuilt only when it changes, animated while playing.
    {
        kb::scene::Scene scene;
        auto& manager = scene.Assets().Manager();
        Require(manager.Mounts().Mount("Game", assets), "mount sprite assets");
        Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()), "register the mesh loader");
        const kb::assets::AssetId image{ 880011U };
        Require(manager.RegisterAsset({ .id = image, .type = "RenderTexture", .virtualPath = "/Game/Sprites/hero.png",
                    .physicalPath = assets / "Sprites" / "hero.png", .contentHash = 1U }), "register the image");
        const auto owner = scene.Entities().CreateObject({ .name = "Hero" });
        Sprite2DComponent sprite{ .imageAssetId = image.value, .cellWidth = 16U, .cell = 0U, .frameCount = 4U, .framesPerSecond = 4.0F, .pixelsPerUnit = 16.0F };
        scene.Components().Sprites2D().Set(owner.Entity(), sprite);
        auto system = std::make_unique<kb::world2d::Sprite2DRuntime>();
        auto* runtime = system.get();
        const auto handle = scene.Runtime().AddSceneSystem(std::move(system));
        const auto update = [&](float seconds) { static_cast<void>(scene.Runtime().Update(seconds)); };

        update(0.0F);
        Require(scene.Runtime().DrainSceneSystemErrors().empty(), "a valid sprite reports no errors");
        const auto visual = runtime->VisualOf(owner.Entity());
        Require(visual.IsValid() && scene.Entities().IsAlive(visual) && runtime->RebuiltLastUpdate() == 1U, "the sprite gets a visual and builds it once");
        const auto* renderer = std::as_const(scene).Components().MeshRenderers().TryGet(visual);
        Require(renderer != nullptr && renderer->transient && !renderer->castsShadow, "the visual is a transient mesh");
        const auto mesh = manager.AcquireLoaded<kb::render::RenderMeshAssetData>(kb::assets::AssetId{ renderer->meshAssetId });
        Require(mesh.IsLoaded() && !mesh->sections.empty() && mesh->sections.front().bounds.radius > 0.0F, "the visual's mesh has bounds, so it can be picked and culled");
        Require(scene.Hierarchy().Parent(visual) == owner.Entity(), "the visual is a child of the sprite's entity");

        // Editing does not advance the animation; playing does, and a frame change rebuilds exactly once.
        update(1.0F);
        Require(runtime->RebuiltLastUpdate() == 0U && runtime->CurrentCell(owner.Entity()) == 0U, "while editing the animation stays on its first frame");
        scene.Runtime().SetPlaying(true);
        update(0.1F);
        Require(runtime->RebuiltLastUpdate() == 0U, "no rebuild between frames");
        update(0.2F);
        Require(runtime->RebuiltLastUpdate() == 1U && runtime->CurrentCell(owner.Entity()) == 1U, "the next frame rebuilds once");
        update(0.5F);
        Require(runtime->CurrentCell(owner.Entity()) == 3U, "the clock keeps counting");
        update(0.25F);
        Require(runtime->CurrentCell(owner.Entity()) == 0U, "a looping animation wraps");

        // Pause, change the time, switch to one-shot.
        sprite.playing = false;
        scene.Components().Sprites2D().Set(owner.Entity(), sprite);
        update(1.0F);
        Require(runtime->CurrentCell(owner.Entity()) == 0U, "a paused animation holds its frame");
        sprite.time = 0.6F;
        scene.Components().Sprites2D().Set(owner.Entity(), sprite);
        update(0.0F);
        Require(runtime->CurrentCell(owner.Entity()) == 2U, "setting Time moves the animation");
        sprite.playing = true; sprite.loop = false; sprite.time = 0.0F;
        scene.Components().Sprites2D().Set(owner.Entity(), sprite);
        update(10.0F);
        Require(runtime->CurrentCell(owner.Entity()) == 3U, "a one-shot animation stops on its last frame");

        // A property change rebuilds; so does a different picture size; an invisible sprite has no visual.
        sprite.flipX = true;
        scene.Components().Sprites2D().Set(owner.Entity(), sprite);
        update(0.0F);
        Require(runtime->RebuiltLastUpdate() == 1U, "flipping rebuilds");
        sprite.visible = false;
        scene.Components().Sprites2D().Set(owner.Entity(), sprite);
        update(0.0F);
        Require(!runtime->VisualOf(owner.Entity()).IsValid() && !scene.Entities().IsAlive(visual), "an invisible sprite has no visual");
        sprite.visible = true;
        scene.Components().Sprites2D().Set(owner.Entity(), sprite);
        update(0.0F);
        Require(runtime->VisualOf(owner.Entity()).IsValid(), "showing it again draws it again");

        // The hidden child never enters a saved scene.
        const auto document = kb::scene::SceneDocumentService::Capture(scene, "Sprite");
        Require(document.worldPrefab.Nodes().size() == 1U && document.worldPrefab.Nodes().front().components.sprite2D.has_value(), "the generated visual is not saved, the sprite is");

        // A broken sprite is reported, once, and draws nothing.
        sprite.pivotX = std::numeric_limits<float>::quiet_NaN();
        scene.Components().Sprites2D().Set(owner.Entity(), sprite);
        update(0.0F);
        update(0.0F);
        Require(scene.Runtime().DrainSceneSystemErrors().size() == 1U && !runtime->VisualOf(owner.Entity()).IsValid(), "an invalid sprite is reported once and not drawn");
        sprite.pivotX = 0.5F;
        sprite.imageAssetId = 424242U;
        scene.Components().Sprites2D().Set(owner.Entity(), sprite);
        update(0.0F);
        Require(scene.Runtime().DrainSceneSystemErrors().size() == 1U, "a missing image is reported");

        // Removing the component, or the entity, releases the visual and the generated mesh.
        sprite.imageAssetId = image.value;
        scene.Components().Sprites2D().Set(owner.Entity(), sprite);
        update(0.0F);
        const auto drawn = runtime->VisualOf(owner.Entity());
        Require(drawn.IsValid(), "the sprite draws again with its image back");
        scene.Components().Sprites2D().Remove(owner.Entity());
        update(0.0F);
        Require(!scene.Entities().IsAlive(drawn), "removing the component removes the visual");
        std::size_t generated = 0U;
        for (const auto& metadata : manager.Registry().All()) generated += kb::assets::NormalizeAssetPath(metadata.virtualPath).starts_with("/Runtime/Sprite2D") ? 1U : 0U;
        Require(generated == 0U, "removing the component releases the generated mesh");
        Require(scene.Runtime().RemoveSceneSystem(handle), "detach the sprite runtime");
    }

    // The component through every door: scene file, history, prefab, scripts and the shared authoring table.
    {
        kb::scene::Scene scene;
        const auto owner = scene.Entities().CreateObject({ .name = "Sprite" });
        Sprite2DComponent sprite{ .imageAssetId = 1234567890123ULL, .materialAssetId = 77U, .cellWidth = 16U, .cellHeight = 24U, .margin = 1U, .spacing = 2U,
            .imageWidth = 128U, .imageHeight = 96U, .cell = 5U, .frameCount = 3U, .framesPerSecond = 12.5F, .loop = false, .playing = false, .time = 0.75F,
            .pixelsPerUnit = 48.0F, .pivotX = 0.25F, .pivotY = 0.875F, .flipX = true, .flipY = true, .tintR = 0.5F, .tintG = 0.25F, .tintB = 0.125F, .tintA = 0.75F,
            .sortingOrder = -7, .wall = false, .visible = false };
        scene.Components().Sprites2D().Set(owner.Entity(), sprite);
        Require(scene.History().Record("Edit sprite"), "record sprite history");
        auto changed = sprite; changed.cell = 9U;
        scene.Components().Sprites2D().Set(owner.Entity(), changed);
        scene.History().Commit();
        Require(scene.History().Undo() && scene.Components().Sprites2D().TryGet(owner.Entity())->cell == 5U, "undo restores the sprite");
        Require(scene.History().Redo() && scene.Components().Sprites2D().TryGet(owner.Entity())->cell == 9U, "redo changes it again");
        scene.Components().Sprites2D().Set(owner.Entity(), sprite);

        const auto document = kb::scene::SceneDocumentService::Capture(scene, "Sprite");
        Require(kb::scene::SceneDocumentService::Save(document, root / "Sprite.21kbscene"), "save the sprite scene");
        const auto loaded = kb::scene::SceneDocumentService::Load(root / "Sprite.21kbscene");
        Require(loaded.succeeded && loaded.document.worldPrefab.Nodes().front().components.sprite2D == sprite, "the scene file keeps every sprite field");

        const auto prefab = scene.Prefabs().CreateAsset(owner, "Sprite", root / "Sprite.kbprefab");
        Require(prefab.IsValid(), "save the sprite prefab");
        kb::scene::Scene destination;
        const auto restored = destination.Prefabs().Load(root / "Sprite.kbprefab");
        Require(restored.IsValid(), "load the sprite prefab");
        const auto copies = destination.Prefabs().InstantiateMany(restored, 3U);
        Require(copies.size() == 3U, "bulk duplicate the sprite prefab");
        for (const auto& copy : copies) {
            const auto* value = destination.Components().Sprites2D().TryGet(copy.RootObject().Entity());
            Require(value != nullptr && *value == sprite, "every prefab copy keeps every sprite field");
        }

        // Scripts read and write the fields; a bad value is refused.
        auto got = kb::script::ScriptSceneComponentApi::GetProperty(scene, owner.Entity(), "Sprite2D", "framesPerSecond");
        Require(got.succeeded && got.value.AsFloat() == 12.5F, "scripts read a sprite field");
        auto set = kb::script::ScriptSceneComponentApi::SetProperty(scene, owner.Entity(), "Sprite2D", "cell", kb::script::ScriptValue{ 3 });
        Require(set.succeeded && scene.Components().Sprites2D().TryGet(owner.Entity())->cell == 3U, "scripts write a sprite field");
        set = kb::script::ScriptSceneComponentApi::SetProperty(scene, owner.Entity(), "Sprite2D", "flipX", kb::script::ScriptValue{ 5 });
        Require(!set.succeeded, "a script value of the wrong type is refused");
        got = kb::script::ScriptSceneComponentApi::GetProperty(scene, owner.Entity(), "Sprite2D", "imageAssetId");
        Require(got.succeeded && got.value.AsUInt64() == 1234567890123ULL, "scripts read the image id exactly");
        Require(kb::script::ScriptSceneComponentApi::HasComponent(scene, owner.Entity(), "Sprite2D"), "scripts see the component");

        // The authoring table CLI, editor and automation share.
        const auto other = scene.Entities().CreateObject({ .name = "Other" });
        kb::project::ProjectDescriptor project;
        project.plugins.push_back({ .name = "World.Map2D", .enabled = true });
        const auto added = kb::scene::SceneComponentAuthoring::Add(scene, other.Entity(), "Sprite2D", project);
        Require(added.status == kb::scene::SceneComponentAddStatus::Added && scene.Components().Sprites2D().Has(other.Entity()), "the shared table adds a sprite");
        Require(kb::scene::SceneComponentAuthoring::Add(scene, other.Entity(), "Sprite2D", project).status == kb::scene::SceneComponentAddStatus::AlreadyPresent, "a second sprite is refused");
        Require(kb::scene::SceneComponentAuthoring::Remove(scene, other.Entity(), "Sprite2D") && !scene.Components().Sprites2D().Has(other.Entity()), "the shared table removes it");
        kb::project::ProjectDescriptor without;
        Require(kb::scene::SceneComponentAuthoring::Add(scene, other.Entity(), "Sprite2D", without).status == kb::scene::SceneComponentAddStatus::PluginDisabled, "without the World 2D plugin the add is refused");
    }

    std::filesystem::remove_all(root);
    std::puts("kb_world2d_sprite_tests passed");
    return 0;
}
