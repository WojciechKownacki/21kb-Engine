#include "scene/postprocess/EditorPostProcessProfileAssets.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/PostProcessProfileAssetLoader.hpp"

#include <system_error>
#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] std::optional<std::filesystem::path> ProfileFile(const kb::scene::Scene& scene, kb::assets::AssetId id) {
    const kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr || metadata->type != kb::render::kPostProcessProfileAssetType) {
        return std::nullopt;
    }
    return !metadata->physicalPath.empty()
        ? std::optional<std::filesystem::path>{ metadata->physicalPath }
        : manager.Mounts().Resolve(metadata->virtualPath);
}

[[nodiscard]] bool Same(const kb::render::ScenePostProcessSettings& left, const kb::render::ScenePostProcessSettings& right) {
    return kb::render::PostProcessProfileAssetLoader::Describe(left) == kb::render::PostProcessProfileAssetLoader::Describe(right);
}

// The asset manager drops its cached profile and reads the file again, so the next frame renders
// with what the file now holds.
[[nodiscard]] bool Reload(kb::scene::Scene& scene, kb::assets::AssetId id, const kb::render::ScenePostProcessSettings& expected) {
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    static_cast<void>(manager.Unload(id));
    static_cast<void>(scene.Assets().Discover());
    const kb::assets::AssetHandle<kb::render::ScenePostProcessSettings> loaded = manager.Load<kb::render::ScenePostProcessSettings>(id);
    return loaded.IsLoaded() && Same(*loaded, expected);
}

} // namespace

std::filesystem::path EditorPostProcessProfileAssets::Create(
    kb::scene::Scene& scene,
    EditorAssetBrowserState& browser,
    const std::filesystem::path& virtualFolder) {
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    const std::optional<std::filesystem::path> probe =
        virtualFolder.empty() ? std::nullopt : manager.Mounts().Resolve(virtualFolder / "probe");
    if (!probe.has_value()) {
        return {};
    }
    const std::filesystem::path folder = probe->parent_path();
    std::filesystem::path file = folder / (std::string{ "PostProcessProfile" } + kb::render::kPostProcessProfileAssetExtension);
    for (std::uint32_t suffix = 1U; std::filesystem::exists(file); ++suffix) {
        file = folder / ("PostProcessProfile" + std::to_string(suffix) + kb::render::kPostProcessProfileAssetExtension);
    }
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (!kb::render::PostProcessProfileAssetLoader::SaveProfile(file, kb::render::ScenePostProcessSettings{})) {
        return {};
    }
    static_cast<void>(scene.Assets().Discover());
    const std::optional<std::filesystem::path> virtualPath = manager.Mounts().ToVirtual(file);
    const kb::assets::AssetMetadata* metadata = virtualPath.has_value() ? manager.Registry().FindByPath(*virtualPath) : nullptr;
    if (metadata == nullptr || metadata->type != kb::render::kPostProcessProfileAssetType ||
        !manager.Load<kb::render::ScenePostProcessSettings>(metadata->id).IsLoaded()) {
        std::filesystem::remove(file, error);
        static_cast<void>(scene.Assets().Discover());
        return {};
    }
    static_cast<void>(browser.SelectAsset(metadata->id, manager));
    return *virtualPath;
}

bool EditorPostProcessProfileAssets::IsProfile(const kb::scene::Scene& scene, kb::assets::AssetId id) {
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(id);
    return metadata != nullptr && metadata->type == kb::render::kPostProcessProfileAssetType;
}

std::optional<kb::render::ScenePostProcessSettings> EditorPostProcessProfileAssets::Read(
    const kb::scene::Scene& scene,
    kb::assets::AssetId id) {
    const std::optional<std::filesystem::path> file = ProfileFile(scene, id);
    return file.has_value() ? kb::render::PostProcessProfileAssetLoader::LoadProfile(*file) : std::nullopt;
}

bool EditorPostProcessProfileAssets::Write(
    kb::scene::Scene& scene,
    kb::assets::AssetId id,
    const kb::render::ScenePostProcessSettings& settings) {
    const std::optional<std::filesystem::path> file = ProfileFile(scene, id);
    if (!file.has_value()) {
        return false;
    }
    const std::optional<kb::render::ScenePostProcessSettings> previous = kb::render::PostProcessProfileAssetLoader::LoadProfile(*file);
    if (!kb::render::PostProcessProfileAssetLoader::SaveProfile(*file, settings)) {
        return false;
    }
    if (Reload(scene, id, settings)) {
        return true;
    }
    // The file did not load back as written: put the previous values back.
    if (previous.has_value() && kb::render::PostProcessProfileAssetLoader::SaveProfile(*file, *previous)) {
        static_cast<void>(Reload(scene, id, *previous));
    }
    return false;
}

EditorPostProcessProfileEditCommand::EditorPostProcessProfileEditCommand(
    kb::scene::Scene& scene,
    kb::assets::AssetId profile,
    std::string label,
    kb::render::ScenePostProcessSettings before,
    kb::render::ScenePostProcessSettings after)
    : scene_(scene)
    , profile_(profile)
    , label_(std::move(label))
    , before_(std::move(before))
    , after_(std::move(after)) {}

std::string_view EditorPostProcessProfileEditCommand::Label() const noexcept {
    return label_;
}

bool EditorPostProcessProfileEditCommand::AffectsSceneDocument() const noexcept {
    return false;
}

bool EditorPostProcessProfileEditCommand::AffectsHierarchySelection() const noexcept {
    return false;
}

bool EditorPostProcessProfileEditCommand::Execute() {
    return EditorPostProcessProfileAssets::Write(scene_, profile_, after_);
}

bool EditorPostProcessProfileEditCommand::Undo() {
    return EditorPostProcessProfileAssets::Write(scene_, profile_, before_);
}

bool EditorPostProcessProfileEditCommand::Redo() {
    return EditorPostProcessProfileAssets::Write(scene_, profile_, after_);
}

} // namespace kb::editor
