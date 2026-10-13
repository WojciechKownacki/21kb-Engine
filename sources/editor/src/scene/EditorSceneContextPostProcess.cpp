#include "scene/EditorSceneContext.hpp"

#include "engine/assets/AssetImportPreset.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/ScenePostProcessAccess.hpp"
#include "kb/render/resources/PostProcessProfileAssetLoader.hpp"
#include "scene/postprocess/EditorPostProcessProfileAssets.hpp"

#include <algorithm>
#include <memory>
#include <system_error>
#include <utility>

// Post-process profiles and import presets: the assets and the scene setting the editor's
// Project Files, Inspector and import dialog work with.

namespace kb::editor {

bool EditorSceneContext::CreatePostProcessProfileAsset(const std::filesystem::path& virtualFolder) {
    const std::filesystem::path created = EditorPostProcessProfileAssets::Create(*scene_, assetBrowser_, virtualFolder);
    if (created.empty()) {
        console_.Error("Post Processing", "The post-process profile could not be created in " + virtualFolder.generic_string() + ".");
        return false;
    }
    console_.Info("Post Processing", "Post-process profile created: " + created.generic_string());
    return true;
}

bool EditorSceneContext::CreateImportPresetAsset(const std::filesystem::path& virtualFolder) {
    kb::assets::AssetManager& manager = scene_->Assets().Manager();
    const std::optional<std::filesystem::path> probe =
        virtualFolder.empty() ? std::nullopt : manager.Mounts().Resolve(virtualFolder / "probe");
    if (!probe.has_value()) {
        console_.Error("Import", "Could not resolve a folder for the new import preset.");
        return false;
    }
    const std::filesystem::path folder = probe->parent_path();
    std::filesystem::path file = folder / (std::string{ "ImportPreset" } + kb::assets::kAssetImportPresetExtension);
    for (std::uint32_t suffix = 1U; std::filesystem::exists(file); ++suffix) {
        file = folder / ("ImportPreset" + std::to_string(suffix) + kb::assets::kAssetImportPresetExtension);
    }
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    // The import dialog's own defaults, so a new preset imports as the dialog does untouched.
    kb::assets::AssetImportOptions defaults{};
    defaults.mesh.importTextures = true;
    defaults.mesh.importMaterials = true;
    defaults.mesh.combineMeshes = true;
    if (!kb::assets::AssetImportPresetIO::Save(file, defaults)) {
        console_.Error("Import", "The import preset could not be written: " + file.generic_string());
        return false;
    }
    static_cast<void>(scene_->Assets().Discover());
    const std::optional<std::filesystem::path> virtualPath = manager.Mounts().ToVirtual(file);
    const kb::assets::AssetMetadata* metadata = virtualPath.has_value() ? manager.Registry().FindByPath(*virtualPath) : nullptr;
    if (metadata == nullptr || metadata->type != kb::assets::kAssetImportPresetAssetType) {
        std::filesystem::remove(file, error);
        static_cast<void>(scene_->Assets().Discover());
        console_.Error("Import", "The new import preset was not recognised as an asset.");
        return false;
    }
    static_cast<void>(assetBrowser_.SelectAsset(metadata->id, manager));
    console_.Info("Import", "Import preset created: " + virtualPath->generic_string());
    return true;
}

std::optional<kb::render::ScenePostProcessSettings> EditorSceneContext::ReadPostProcessProfile(kb::assets::AssetId id) const {
    return EditorPostProcessProfileAssets::Read(*scene_, id);
}

bool EditorSceneContext::SetPostProcessProfile(kb::assets::AssetId id, const kb::render::ScenePostProcessSettings& settings) {
    const std::optional<kb::render::ScenePostProcessSettings> before = EditorPostProcessProfileAssets::Read(*scene_, id);
    if (!before.has_value()) {
        console_.Error("Post Processing", "The post-process profile could not be read.");
        return false;
    }
    if (kb::render::PostProcessProfileAssetLoader::Describe(*before) == kb::render::PostProcessProfileAssetLoader::Describe(settings)) {
        return false;
    }
    if (!commandStack_.Execute(std::make_unique<EditorPostProcessProfileEditCommand>(
            *scene_, id, "Edit Post-Process Profile", *before, settings))) {
        console_.Error("Post Processing", "The post-process profile could not be saved.");
        return false;
    }
    MarkSceneRenderDirty();
    return true;
}

bool EditorSceneContext::SetScenePostProcessProfile(kb::assets::AssetId id) {
    if (id.value == kb::scene::ScenePostProcessAccess::ActiveProfile(*scene_)) {
        return false;
    }
    if (id.IsValid()) {
        kb::assets::AssetManager& manager = scene_->Assets().Manager();
        if (!EditorPostProcessProfileAssets::IsProfile(*scene_, id) ||
            !manager.Load<kb::render::ScenePostProcessSettings>(id).IsLoaded()) {
            console_.Error("Post Processing", "Only a post-process profile that loads can be the scene's profile.");
            return false;
        }
    }
    kb::scene::Scene* scene = scene_;
    const bool changed = ExecuteSceneCommand(id.IsValid() ? "Set Scene Post-Process Profile" : "Clear Scene Post-Process Profile",
        [scene, id]() {
            kb::scene::ScenePostProcessAccess::SetActiveProfile(*scene, id.value);
            return true;
        });
    if (changed) {
        MarkSceneRenderDirty();
    }
    return changed;
}

std::optional<kb::render::ScenePostProcessSettings> EditorSceneContext::ScenePostProcessProfileSettings() const {
    const kb::assets::AssetId id{ kb::scene::ScenePostProcessAccess::ActiveProfile(*scene_) };
    if (!id.IsValid()) {
        return std::nullopt;
    }
    const kb::assets::AssetHandle<kb::render::ScenePostProcessSettings> loaded =
        scene_->Assets().Manager().Load<kb::render::ScenePostProcessSettings>(id);
    return loaded.IsLoaded() ? std::optional<kb::render::ScenePostProcessSettings>{ *loaded } : std::nullopt;
}

std::vector<std::pair<std::string, kb::assets::AssetImportOptions>> EditorSceneContext::ImportPresets() {
    std::vector<std::pair<std::string, kb::assets::AssetImportOptions>> presets;
    const kb::assets::AssetManager& manager = scene_->Assets().Manager();
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (metadata.type != kb::assets::kAssetImportPresetAssetType) continue;
        const std::optional<std::filesystem::path> file = !metadata.physicalPath.empty()
            ? std::optional<std::filesystem::path>{ metadata.physicalPath }
            : manager.Mounts().Resolve(metadata.virtualPath);
        std::string error;
        const std::optional<kb::assets::AssetImportOptions> options =
            file.has_value() ? kb::assets::AssetImportPresetIO::Load(*file, &error) : std::nullopt;
        if (!options.has_value()) {
            console_.Warning("Import", "Import preset " + metadata.virtualPath.generic_string() + " could not be read: " + error);
            continue;
        }
        presets.emplace_back(metadata.name, *options);
    }
    std::ranges::sort(presets, [](const auto& left, const auto& right) { return left.first < right.first; });
    return presets;
}

} // namespace kb::editor
