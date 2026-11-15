#include "scene/EditorSceneContext.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/navigation/NavGeometryCollector.hpp"
#include "engine/navigation/NavMeshAsset.hpp"
#include "engine/scene/ContentInstanceComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneLightProbeAccess.hpp"
#include "engine/world/WorldCellIndex.hpp"
#include "engine/world/WorldDescriptor.hpp"
#include "kb/render/world/RenderNavGeometrySource.hpp"
#include "kb/render/bake/SceneLightingBaker.hpp"

#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace kb::editor {
namespace {

[[nodiscard]] const kb::assets::AssetMetadata* FindByPhysicalPath(const kb::assets::AssetManager& manager, const std::filesystem::path& path) {
    std::error_code code;
    const std::filesystem::path wanted = std::filesystem::weakly_canonical(path, code);
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (metadata.physicalPath.empty()) continue;
        std::error_code other;
        if (std::filesystem::weakly_canonical(metadata.physicalPath, other) == wanted) return &metadata;
    }
    return nullptr;
}

// The open scene's static geometry as a bake of the saved scene reads it, collected on this thread
// because the source reads the scene's assets.
// The source keeps the meshes it read for the updates that follow; a bake the user starts reads them afresh.
[[nodiscard]] std::shared_ptr<const kb::navigation::NavScenePreparation> PrepareOpenScene(
    kb::scene::Scene& scene, EditorNavigationUpdates& updates, const kb::navigation::NavMeshBuildSettings& settings) {
    const kb::scene::SceneDocument document = kb::scene::SceneDocumentService::Capture(scene, "navigation");
    if (updates.source == nullptr) updates.source = std::make_unique<kb::render::RenderNavGeometrySource>(scene.Assets().Manager());
    return std::make_shared<const kb::navigation::NavScenePreparation>(
        kb::navigation::PrepareSceneNavBake(document.worldPrefab.Nodes(), settings, updates.source.get()));
}

// Starts the job on a worker: a whole bake, or the job's tiles of `base` rebuilt.
void StartJob(EditorNavigationUpdates& updates, std::unique_ptr<EditorNavigationBakeJob> job) {
    std::shared_ptr<const kb::navigation::NavMeshAsset> base = job->tiles.empty() ? nullptr : updates.asset;
    job->result = std::async(std::launch::async,
        [prepared = job->prepared, tiles = job->tiles, base = std::move(base), progress = &job->progress]() {
            if (base == nullptr) return kb::navigation::BakePreparedSceneNavMesh(*prepared, progress);
            kb::navigation::NavSceneBakeResult result;
            result.geometry = prepared->stats;
            kb::navigation::NavMeshBakeResult rebuilt = kb::navigation::NavMeshBuilder::RebuildTiles(
                prepared->settings, prepared->geometry, base->tiles, tiles, nullptr, progress);
            result.succeeded = rebuilt.succeeded;
            result.error = std::move(rebuilt.error);
            result.asset.settings = prepared->settings;
            result.asset.tiles = std::move(rebuilt.tiles);
            result.stats = rebuilt.stats;
            return result;
        });
    updates.job = std::move(job);
}

} // namespace

EditorNavigation& EditorSceneContext::Navigation() noexcept {
    return navigation_;
}

const EditorNavigation& EditorSceneContext::Navigation() const noexcept {
    return navigation_;
}

void EditorSceneContext::RefreshNavigationView() {
    std::vector<std::shared_ptr<const kb::navigation::NavMeshAsset>> meshes;
    if (worldPartition_.IsOpen()) {
        const std::filesystem::path descriptor = worldPartition_.Session().DescriptorPath();
        const kb::world::WorldCellIndexReadResult index = kb::world::WorldCellIndexIO::Read(kb::world::WorldPaths::CellIndexPath(descriptor));
        if (index.succeeded) {
            const std::filesystem::path cells = kb::world::WorldPaths::CellsDirectory(descriptor);
            for (const kb::world::WorldCellNavMesh& navMesh : index.index.navMeshes) {
                kb::navigation::NavMeshAssetReadResult read = kb::navigation::NavMeshAssetIO::Read(cells / std::filesystem::path{ navMesh.mesh });
                if (read.succeeded) meshes.push_back(std::make_shared<kb::navigation::NavMeshAsset>(std::move(read.asset)));
            }
        }
    } else if (!currentScenePath_.empty()) {
        std::error_code code;
        const std::filesystem::path path = kb::navigation::SceneNavMeshPath(currentScenePath_);
        if (std::filesystem::is_regular_file(path, code)) {
            kb::navigation::NavMeshAssetReadResult read = kb::navigation::NavMeshAssetIO::Read(path);
            if (read.succeeded) meshes.push_back(std::make_shared<kb::navigation::NavMeshAsset>(std::move(read.asset)));
        }
    }
    navigation_.Show(meshes, projectConfig_.navigationAreas);
}

void EditorSceneContext::SetNavigationMeshVisible(bool visible) {
    navigation_.SetVisible(visible);
    if (visible) {
        RefreshNavigationView();
    }
}

bool EditorSceneContext::BakeNavigation() {
    if (playModeSceneSession_.Active()) {
        console_.Warning("Navigation", "Stop play mode before baking the navigation mesh.");
        return false;
    }
    if (RejectWhilePrefabEditing()) {
        return false;
    }
    if (worldPartition_.IsOpen()) {
        // A world bakes one navigation mesh per cell with its build.
        if (!worldPartition_.Session().Descriptor().navigation.enabled) {
            kb::world::WorldNavigationSettings navigation = worldPartition_.Session().Descriptor().navigation;
            navigation.enabled = true;
            std::string error;
            if (!worldPartition_.Session().SetNavigation(navigation, error)) {
                console_.Error("Navigation", "Navigation could not be enabled for the world: " + error);
                return false;
            }
            MarkSceneDocumentDirty();
            console_.Info("Navigation", "Navigation enabled in the world file; every build now bakes the cells' navigation meshes.");
        }
        if (!BuildOpenWorld()) {
            return false;
        }
        RefreshNavigationView();
        console_.Info("Navigation", "Navigation mesh shown: " + std::to_string(navigation_.TriangleCount()) + " triangle(s).");
        return true;
    }

    // A running update ends first, so the bake starts from what it wrote.
    static_cast<void>(WaitForNavigationBake());
    std::error_code code;
    if (sceneDocumentDirty_ || currentScenePath_.empty() || !std::filesystem::is_regular_file(currentScenePath_, code)) {
        if (!SaveCurrentScene()) {
            console_.Error("Navigation", "Save the scene before baking its navigation mesh.");
            return false;
        }
    }
    auto job = std::make_unique<EditorNavigationBakeJob>();
    job->scenePath = currentScenePath_;
    navigationUpdates_.source.reset();
    job->prepared = PrepareOpenScene(*scene_, navigationUpdates_, kb::navigation::SceneNavMeshSettings(currentScenePath_));
    if (!job->prepared->succeeded) {
        console_.Error("Navigation", "Navigation bake failed: " + job->prepared->error);
        return false;
    }
    navigationUpdates_.scenePath = currentScenePath_;
    StartJob(navigationUpdates_, std::move(job));
    console_.Info("Navigation", "Baking the navigation mesh in the background...");
    return true;
}

bool EditorSceneContext::ApplyNavigationBake(EditorNavigationBakeJob& job) {
    kb::navigation::NavSceneBakeResult baked = job.result.get();
    if (job.scenePath != currentScenePath_ || worldPartition_.IsOpen()) {
        return false;
    }
    if (!baked.succeeded) {
        console_.Error("Navigation", (job.automatic ? "Navigation update failed: " : "Navigation bake failed: ") + baked.error);
        return false;
    }
    if (!job.automatic && baked.geometry.unresolved != 0U) {
        console_.Warning("Navigation", std::to_string(baked.geometry.unresolved) + " mesh(es) could not be read and were left out of the navigation mesh.");
    }
    const std::filesystem::path navPath = kb::navigation::SceneNavMeshPath(job.scenePath);
    std::string error;
    if (!kb::navigation::NavMeshAssetIO::Write(navPath, baked.asset, error)) {
        console_.Error("Navigation", "Navigation mesh could not be written: " + error);
        return false;
    }
    static_cast<void>(scene_->Assets().Discover());
    navigationUpdates_.baseline = job.prepared;
    navigationUpdates_.asset = std::make_shared<const kb::navigation::NavMeshAsset>(baked.asset);
    navigation_.Show({ navigationUpdates_.asset }, projectConfig_.navigationAreas);
    if (job.automatic) {
        console_.Info("Navigation", "Navigation updated: " + std::to_string(job.tiles.empty() ? baked.stats.tiles : job.tiles.size()) +
            " tile(s) rebuilt in " + std::to_string(static_cast<long long>(baked.stats.milliseconds)) + " ms.");
        return true;
    }
    const kb::assets::AssetMetadata* metadata = FindByPhysicalPath(scene_->Assets().Manager(), navPath);
    if (metadata == nullptr) {
        console_.Error("Navigation", "The navigation mesh " + navPath.generic_string() + " is not inside the project's content.");
        return false;
    }
    // The scene places its navigation mesh through a ContentInstance, so it streams in when the scene plays.
    const std::uint64_t assetId = metadata->id.value;
    bool placed = false;
    for (const kb::scene::SceneEntity root : scene_->Hierarchy().RootEntities()) {
        const kb::scene::ContentInstanceComponent* content = scene_->Components().ContentInstances().TryGet(root);
        placed = placed || (content != nullptr && content->kind == kb::scene::ContentInstanceKind::NavigationMesh && content->assetId == assetId);
    }
    if (!placed) {
        const bool added = ExecuteSceneCommand("Place Navigation Mesh", [this, assetId]() {
            const kb::scene::SceneObject holder = scene_->Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Navigation Mesh" });
            if (!holder.IsValid()) return false;
            scene_->Components().ContentInstances().Set(holder.Entity(), kb::scene::ContentInstanceComponent{
                .assetId = assetId, .kind = kb::scene::ContentInstanceKind::NavigationMesh });
            return true;
        });
        if (!added || !SaveCurrentScene()) {
            console_.Error("Navigation", "The navigation mesh could not be placed in the scene.");
            return false;
        }
    }
    console_.Info("Navigation", "Baked " + std::to_string(baked.stats.tiles) + " navigation tile(s) for " +
        std::to_string(baked.asset.settings.profiles.size()) + " agent profile(s) from " + std::to_string(baked.geometry.meshes) + " mesh(es) and " +
        std::to_string(baked.geometry.colliders) + " collider(s) in " + std::to_string(static_cast<long long>(baked.stats.milliseconds)) +
        " ms into " + navPath.generic_string() + ".");
    return true;
}

bool EditorSceneContext::PumpNavigationBake() {
    EditorNavigationUpdates& updates = navigationUpdates_;
    if (updates.job != nullptr) {
        EditorNavigationBakeJob& job = *updates.job;
        if (job.result.wait_for(std::chrono::seconds{ 0 }) == std::future_status::ready) {
            const std::unique_ptr<EditorNavigationBakeJob> finished = std::move(updates.job);
            return ApplyNavigationBake(*finished);
        }
        const std::size_t total = job.progress.total.load();
        const int percent = total == 0U ? 0 : static_cast<int>(job.progress.built.load() * 100U / total);
        // A bake the user started reports each quarter it passes.
        if (!job.automatic && percent / 25 != job.shownPercent / 25 && percent < 100) {
            job.shownPercent = percent;
            console_.Info("Navigation", "Baking the navigation mesh... " + std::to_string(percent) + "%");
        }
        return false;
    }
    if (!updates.automatic || worldPartition_.IsOpen() || playModeSceneSession_.Active() || InPrefabEditMode() || currentScenePath_.empty()) {
        return false;
    }
    if (updates.scenePath != currentScenePath_) {
        // Another scene: its mesh is rebaked whole on its first edit, then by tiles.
        updates.baseline.reset();
        updates.asset.reset();
        updates.source.reset();
        updates.scenePath = currentScenePath_;
        updates.checkedRevision = sceneRenderRevision_;
        return false;
    }
    if (sceneRenderRevision_ == updates.checkedRevision) {
        return false;
    }
    // An edit settles (a drag ends) before the tiles are rebuilt.
    const auto now = std::chrono::steady_clock::now();
    if (sceneRenderRevision_ != updates.pendingRevision) {
        updates.pendingRevision = sceneRenderRevision_;
        updates.pendingSince = now;
        return false;
    }
    if (now - updates.pendingSince < EditorNavigationUpdates::SettleDelay) {
        return false;
    }
    updates.checkedRevision = sceneRenderRevision_;
    std::error_code code;
    if (updates.asset == nullptr && !std::filesystem::is_regular_file(kb::navigation::SceneNavMeshPath(currentScenePath_), code)) {
        return false;
    }
    auto job = std::make_unique<EditorNavigationBakeJob>();
    job->scenePath = currentScenePath_;
    job->automatic = true;
    job->prepared = PrepareOpenScene(*scene_, updates,
        updates.asset != nullptr ? updates.asset->settings : kb::navigation::SceneNavMeshSettings(currentScenePath_));
    if (!job->prepared->succeeded) {
        console_.Warning("Navigation", "Navigation not updated: " + job->prepared->error);
        return false;
    }
    if (updates.baseline != nullptr && updates.asset != nullptr && job->prepared->settings == updates.baseline->settings) {
        std::optional<std::vector<kb::navigation::NavTileCoord>> changed =
            kb::navigation::ChangedNavTiles(job->prepared->settings, updates.baseline->geometry, job->prepared->geometry);
        if (changed.has_value() && changed->empty()) {
            updates.baseline = job->prepared;
            return false;
        }
        if (changed.has_value()) job->tiles = std::move(*changed);
    }
    StartJob(updates, std::move(job));
    return false;
}

bool EditorSceneContext::WaitForNavigationBake() {
    if (navigationUpdates_.job == nullptr) {
        return true;
    }
    const std::unique_ptr<EditorNavigationBakeJob> finished = std::move(navigationUpdates_.job);
    return ApplyNavigationBake(*finished);
}

bool EditorSceneContext::NavigationBakeInProgress() const noexcept {
    return navigationUpdates_.job != nullptr;
}

bool EditorSceneContext::NavigationAutoUpdate() const noexcept {
    return navigationUpdates_.automatic;
}

void EditorSceneContext::SetNavigationAutoUpdate(bool enabled) {
    navigationUpdates_.automatic = enabled;
    console_.Info("Navigation", enabled ? "The navigation mesh now updates by itself after static edits." :
        "The navigation mesh now updates only when baked.");
}

bool EditorSceneContext::BuildLighting() {
    if (playModeSceneSession_.Active()) {
        console_.Warning("Lighting", "Stop play mode before building lighting.");
        return false;
    }
    if (RejectWhilePrefabEditing()) {
        return false;
    }
    if (worldPartition_.IsOpen()) {
        console_.Warning("Lighting", "Building lighting works on a single scene, not on an open world.");
        return false;
    }
    if (!SaveDirtySceneDocument("building lighting")) {
        return false;
    }
    console_.Info("Lighting", "Building lighting...");
    const std::u8string name = currentScenePath_.stem().generic_u8string();
    const kb::render::SceneLightingBakeResult baked = kb::render::BakeSceneLighting(*scene_,
        std::string_view{ reinterpret_cast<const char*>(name.data()), name.size() }, {},
        [this](float fraction, std::string_view stage) {
            console_.Info("Lighting", std::to_string(static_cast<int>(fraction * 100.0F)) + "% " + std::string{ stage });
            return true;
        });
    if (!baked.Succeeded()) {
        console_.Error("Lighting", "Lighting build failed: " + baked.error);
        return false;
    }
    static_cast<void>(scene_->Assets().Discover());
    MarkSceneDocumentDirty();
    if (!SaveCurrentScene()) {
        console_.Error("Lighting", "The scene could not be saved after building lighting.");
        return false;
    }
    kb::scene::SceneLightProbeAccess::AssignBakedGrid(*scene_, std::string_view{ reinterpret_cast<const char*>(name.data()), name.size() });
    console_.Info("Lighting", "Baked " + std::to_string(baked.receivers) + " static mesh(es) with " + std::to_string(baked.lights) + " static light(s).");
    return true;
}

} // namespace kb::editor
