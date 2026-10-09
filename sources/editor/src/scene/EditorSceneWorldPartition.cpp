#include "scene/EditorSceneContext.hpp"

#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/world/WorldDescriptor.hpp"
#include "project/EditorProjectPaths.hpp"
#include "scene/audio/EditorSceneAudioSettingsService.hpp"

#include <string>
#include <vector>

namespace kb::editor {
namespace {

[[nodiscard]] kb::scene::SceneEntity RootOf(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    for (kb::scene::SceneEntity parent = scene.Hierarchy().Parent(entity); parent.IsValid(); parent = scene.Hierarchy().Parent(entity)) {
        entity = parent;
    }
    return entity;
}

} // namespace

bool EditorSceneContext::IsWorldOpen() const noexcept {
    return worldPartition_.IsOpen();
}

EditorWorldPartition& EditorSceneContext::WorldPartition() noexcept {
    return worldPartition_;
}

const EditorWorldPartition& EditorSceneContext::WorldPartition() const noexcept {
    return worldPartition_;
}

bool EditorSceneContext::OpenWorld(const std::filesystem::path& path, EditorDirtySceneResolution dirtyResolution) {
    if (RejectWhilePrefabEditing() || !RestorePlayModeSceneSession()) {
        return false;
    }
    if (!PrepareDirtySceneTransition("opening a world", dirtyResolution)) {
        return false;
    }
    const std::vector<kb::scene::SceneEntity> roots = scene_->Hierarchy().RootEntities();
    for (const kb::scene::SceneEntity root : roots) {
        scene_->Entities().Destroy(root);
    }
    EditorSceneAudioSettingsService::ResetForNewDocument(*scene_);
    ReleaseRenderedSceneResources();
    worldPartition_.Close();
    std::string error;
    const bool opened = worldPartition_.Open(*scene_, path, error);
    currentScenePath_ = opened ? path : EditorProjectPaths::UniqueScenePath("Untitled");
    if (opened) {
        RememberLastOpenMap();
    }
    AdvanceSceneDocumentGeneration();
    SelectFirstSceneEntityOrClear();
    ResetSceneEditState();
    InvalidateHierarchyRows();
    ClearSceneDocumentDirty();
    if (!opened) {
        console_.Error("World", "World could not be opened: " + error);
        return false;
    }
    console_.Info("World", "Opened world " + path.generic_string() + " (" + std::to_string(worldPartition_.Session().Objects().size()) +
        " objects, " + std::to_string(static_cast<long long>(worldPartition_.Session().CellSize())) +
        " m cells). Load cells from the World menu.");
    return true;
}

bool EditorSceneContext::LoadWorldRegion(kb::world::WorldCellCoord min, kb::world::WorldCellCoord max) {
    if (!worldPartition_.IsOpen() || playModeSceneSession_.Active() || RejectWhilePrefabEditing()) {
        console_.Warning("World", "Open a world and stop play mode to load cells.");
        return false;
    }
    std::string error;
    const std::size_t loaded = worldPartition_.Session().LoadRegion(min, max, error);
    if (!error.empty()) {
        console_.Error("World", "Cells could not be loaded: " + error);
        return false;
    }
    sceneGraphCookPending_ = true;
    InvalidateHierarchyRows();
    MarkSceneRenderDirty();
    console_.Info("World", "Loaded " + std::to_string(loaded) + " object(s).");
    return true;
}

bool EditorSceneContext::LoadWorldCellsNearCamera() {
    if (!worldPartition_.IsOpen()) {
        console_.Warning("World", "Open a world to load cells.");
        return false;
    }
    const kb::world::WorldPartitionGrid grid{ worldPartition_.Session().CellSize() };
    const kb::scene::Vec3& position = ViewportCamera().Position();
    const std::optional<kb::world::WorldCellCoord> centre = grid.CellOf(position.x, position.z);
    if (!centre.has_value()) {
        console_.Warning("World", "The camera is outside the addressable world.");
        return false;
    }
    const std::int64_t radius = EditorWorldPartition::NearCameraRadiusCells;
    return LoadWorldRegion({ centre->x - radius, centre->z - radius }, { centre->x + radius, centre->z + radius });
}

bool EditorSceneContext::UnloadWorldRegion(kb::world::WorldCellCoord min, kb::world::WorldCellCoord max) {
    if (!worldPartition_.IsOpen() || playModeSceneSession_.Active() || RejectWhilePrefabEditing()) {
        console_.Warning("World", "Open a world and stop play mode to unload cells.");
        return false;
    }
    std::string error;
    const std::size_t removed = worldPartition_.Session().UnloadRegion(min, max, error);
    if (!error.empty()) {
        console_.Error("World", "Cells could not be unloaded: " + error);
        return false;
    }
    // Undo steps may name the removed objects; edits themselves are kept for saving.
    ReleaseRenderedSceneResources();
    SelectFirstSceneEntityOrClear();
    ResetSceneEditState();
    InvalidateHierarchyRows();
    console_.Info("World", "Unloaded " + std::to_string(removed) + " object(s); their unsaved edits are kept until the world is saved.");
    return true;
}

bool EditorSceneContext::LoadAllWorldCells() {
    if (!worldPartition_.IsOpen() || playModeSceneSession_.Active() || RejectWhilePrefabEditing()) {
        console_.Warning("World", "Open a world and stop play mode to load cells.");
        return false;
    }
    std::string error;
    const std::size_t loaded = worldPartition_.Session().LoadAll(error);
    if (!error.empty()) {
        console_.Error("World", "Cells could not be loaded: " + error);
        return false;
    }
    sceneGraphCookPending_ = true;
    InvalidateHierarchyRows();
    MarkSceneRenderDirty();
    console_.Info("World", "Loaded all cells (" + std::to_string(loaded) + " object(s)).");
    return true;
}

bool EditorSceneContext::UnloadAllWorldCells() {
    if (!worldPartition_.IsOpen() || playModeSceneSession_.Active() || RejectWhilePrefabEditing()) {
        console_.Warning("World", "Open a world and stop play mode to unload cells.");
        return false;
    }
    std::string error;
    const std::size_t removed = worldPartition_.Session().UnloadAll(error);
    if (!error.empty()) {
        console_.Error("World", "Cells could not be unloaded: " + error);
        return false;
    }
    ReleaseRenderedSceneResources();
    SelectFirstSceneEntityOrClear();
    ResetSceneEditState();
    InvalidateHierarchyRows();
    console_.Info("World", "Unloaded " + std::to_string(removed) + " object(s); always-loaded objects stay.");
    return true;
}

bool EditorSceneContext::BuildOpenWorld() {
    if (!worldPartition_.IsOpen()) {
        console_.Warning("World", "Open a world to build it.");
        return false;
    }
    if (playModeSceneSession_.Active()) {
        console_.Warning("World", "Stop play mode before building the world.");
        return false;
    }
    if (!SaveDirtySceneDocument("building the world")) {
        return false;
    }
    const kb::world::WorldBuildResult built = worldPartition_.Build(scene_->Assets().Manager());
    if (!built.succeeded) {
        console_.Error("World", "World build failed: " + built.error);
        return false;
    }
    for (const std::string& warning : built.report.warnings) {
        console_.Warning("World", warning);
    }
    static_cast<void>(scene_->Assets().Discover());
    console_.Info("World", "Built " + std::to_string(built.report.unitCount) + " cell(s) and " + std::to_string(built.report.hlodCount) +
        " HLOD proxy mesh(es) from " + std::to_string(built.report.objectCount) + " object(s).");
    return true;
}

bool EditorSceneContext::ConvertCurrentSceneToWorld(double cellSize, EditorDirtySceneResolution dirtyResolution) {
    if (worldPartition_.IsOpen()) {
        console_.Warning("World", "The open document is already a world.");
        return false;
    }
    if (RejectWhilePrefabEditing() || !RestorePlayModeSceneSession() ||
        !PrepareDirtySceneTransition("converting the scene to a world", dirtyResolution)) {
        return false;
    }
    if (currentScenePath_.empty() || !std::filesystem::is_regular_file(currentScenePath_)) {
        console_.Warning("World", "Save the scene before converting it to a world.");
        return false;
    }
    std::filesystem::path world = currentScenePath_;
    world.replace_extension(kb::world::WorldDescriptor::Extension);
    const kb::world::WorldMigrationResult migrated = kb::world::WorldMigration::ConvertScene(currentScenePath_, world, cellSize);
    if (!migrated.succeeded) {
        console_.Error("World", "Scene could not be converted: " + migrated.error);
        return false;
    }
    static_cast<void>(scene_->Assets().Discover());
    console_.Info("World", "Converted " + currentScenePath_.generic_string() + " into " + world.generic_string() + " with " +
        std::to_string(migrated.objectCount) + " object file(s); the scene file is unchanged.");
    return OpenWorld(world, EditorDirtySceneResolution::Discard);
}

bool EditorSceneContext::DeclareWorldDataLayer(std::string_view name, bool initiallyActive) {
    if (!worldPartition_.IsOpen()) {
        console_.Warning("World", "Open a world to add data layers.");
        return false;
    }
    std::string error;
    if (!worldPartition_.Session().DeclareDataLayer(name, initiallyActive, error)) {
        console_.Error("World", "Data layer could not be added: " + error);
        return false;
    }
    MarkSceneDocumentDirty();
    console_.Info("World", "Added data layer " + std::string{ name } + (initiallyActive ? " (active at start)." : " (inactive at start)."));
    return true;
}

bool EditorSceneContext::CycleSelectedObjectDataLayer() {
    const kb::scene::SceneEntity selected = SelectedEntity();
    if (!worldPartition_.IsOpen() || !selected.IsValid() || !scene_->Entities().IsAlive(selected)) {
        console_.Warning("World", "Select an object of the open world first.");
        return false;
    }
    const kb::scene::SceneEntity root = RootOf(*scene_, selected);
    std::string layer;
    std::string error;
    if (!worldPartition_.CycleDataLayer(root, layer, error)) {
        console_.Error("World", "Data layer could not be changed: " + error);
        return false;
    }
    MarkSceneDocumentDirty();
    console_.Info("World", scene_->Entities().Name(root) + " is now in " + (layer.empty() ? std::string{ "the base layer" } : "data layer " + layer) + ".");
    return true;
}

bool EditorSceneContext::ToggleSelectedObjectAlwaysLoaded() {
    const kb::scene::SceneEntity selected = SelectedEntity();
    if (!worldPartition_.IsOpen() || !selected.IsValid() || !scene_->Entities().IsAlive(selected)) {
        console_.Warning("World", "Select an object of the open world first.");
        return false;
    }
    const kb::scene::SceneEntity root = RootOf(*scene_, selected);
    const std::optional<kb::world::WorldEditObjectInfo> object = worldPartition_.Session().FindObject(root);
    const bool alwaysLoaded = !(object.has_value() && object->alwaysLoaded);
    std::string error;
    if (!worldPartition_.Session().SetObjectAlwaysLoaded(root, alwaysLoaded, error)) {
        console_.Error("World", "Object could not be changed: " + error);
        return false;
    }
    MarkSceneDocumentDirty();
    console_.Info("World", scene_->Entities().Name(root) + (alwaysLoaded ? " is now always loaded." : " now streams with its cell."));
    return true;
}

} // namespace kb::editor
