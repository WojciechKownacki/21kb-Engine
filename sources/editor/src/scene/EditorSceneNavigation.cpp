#include "scene/EditorSceneContext.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/navigation/NavGeometryCollector.hpp"
#include "engine/navigation/NavMeshAsset.hpp"
#include "engine/scene/ContentInstanceComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/world/WorldCellIndex.hpp"
#include "engine/world/WorldDescriptor.hpp"
#include "kb/render/world/NavMeshBakeTool.hpp"

#include <filesystem>
#include <memory>
#include <string>
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
    navigation_.Show(meshes);
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

    std::error_code code;
    if (sceneDocumentDirty_ || currentScenePath_.empty() || !std::filesystem::is_regular_file(currentScenePath_, code)) {
        if (!SaveCurrentScene()) {
            console_.Error("Navigation", "Save the scene before baking its navigation mesh.");
            return false;
        }
    }
    const std::filesystem::path scenePath = currentScenePath_;
    const kb::navigation::NavMeshBuildSettings settings = kb::navigation::SceneNavMeshSettings(scenePath);
    const kb::navigation::NavSceneBakeResult baked = kb::render::BakeSceneNavMeshWithAssets(scene_->Assets().Manager(), scenePath, settings);
    if (!baked.succeeded) {
        console_.Error("Navigation", "Navigation bake failed: " + baked.error);
        return false;
    }
    if (baked.geometry.unresolved != 0U) {
        console_.Warning("Navigation", std::to_string(baked.geometry.unresolved) + " mesh(es) could not be read and were left out of the navigation mesh.");
    }
    const std::filesystem::path navPath = kb::navigation::SceneNavMeshPath(scenePath);
    std::string error;
    if (!kb::navigation::NavMeshAssetIO::Write(navPath, baked.asset, error)) {
        console_.Error("Navigation", "Navigation mesh could not be written: " + error);
        return false;
    }
    static_cast<void>(scene_->Assets().Discover());
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
    navigation_.Show({ std::make_shared<kb::navigation::NavMeshAsset>(baked.asset) });
    console_.Info("Navigation", "Baked " + std::to_string(baked.stats.tiles) + " navigation tile(s) for " +
        std::to_string(settings.profiles.size()) + " agent profile(s) from " + std::to_string(baked.geometry.meshes) + " mesh(es) and " +
        std::to_string(baked.geometry.colliders) + " collider(s) in " + std::to_string(static_cast<long long>(baked.stats.milliseconds)) +
        " ms into " + navPath.generic_string() + ".");
    return true;
}

} // namespace kb::editor
