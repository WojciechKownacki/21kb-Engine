#include "scene/input/EditorInputAssetGateway.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/input/InputAssetIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

namespace kb::editor {
namespace {

// Applies an in-memory edit to an asset loaded from `path` and writes it back.
// Returns false if the file could not be read or written.
template <typename Asset, typename ReadFn, typename WriteFn>
[[nodiscard]] bool ApplyFileEdit(const std::filesystem::path& path, ReadFn read, WriteFn write,
                                 const std::function<void(Asset&)>& mutate) {
    auto loaded = read(path);
    if (!loaded.succeeded) {
        return false;
    }
    mutate(loaded.asset);
    return write(path, loaded.asset);
}

} // namespace

EditorInputAssetGateway::EditorInputAssetGateway(kb::scene::Scene& scene, EditorAssetBrowserState& browser) noexcept
    : scene_(scene)
    , browser_(browser) {}

std::optional<std::filesystem::path> EditorInputAssetGateway::ResolveFolder(const std::filesystem::path& virtualFolder) const {
    if (virtualFolder.empty()) {
        return std::nullopt;
    }
    // AssetMountTable::Resolve only resolves a path *under* a mount, not the bare
    // mount root, so resolve a probe file and take its parent folder.
    const std::optional<std::filesystem::path> probe = scene_.Assets().Manager().Mounts().Resolve(virtualFolder / "probe");
    return probe.has_value() ? std::optional<std::filesystem::path>{ probe->parent_path() } : std::nullopt;
}

std::filesystem::path EditorInputAssetGateway::UniqueFilePath(const std::filesystem::path& folder, std::string_view baseName, std::string_view extension) {
    std::filesystem::path candidate = folder / (std::string{ baseName } + std::string{ extension });
    int suffix = 1;
    while (std::filesystem::exists(candidate)) {
        candidate = folder / (std::string{ baseName } + std::to_string(suffix) + std::string{ extension });
        ++suffix;
    }
    return candidate;
}

void EditorInputAssetGateway::DiscoverAndSelect(const std::filesystem::path& path) {
    kb::assets::AssetManager& manager = scene_.Assets().Manager();
    static_cast<void>(scene_.Assets().Discover());
    if (const std::optional<std::filesystem::path> created = manager.Mounts().ToVirtual(path)) {
        if (const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(*created); metadata != nullptr) {
            static_cast<void>(browser_.SelectAsset(metadata->id, manager));
        }
    }
}

std::optional<std::filesystem::path> EditorInputAssetGateway::ResolveFile(const kb::scene::Scene& scene, kb::assets::AssetId id, std::string_view type) {
    const kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr || metadata->type != type) {
        return std::nullopt;
    }
    if (!metadata->physicalPath.empty()) {
        return metadata->physicalPath;
    }
    return manager.Mounts().Resolve(metadata->virtualPath);
}

void EditorInputAssetGateway::RefreshAfterWrite(kb::assets::AssetId id) {
    static_cast<void>(scene_.Assets().Manager().Unload(id));
    static_cast<void>(scene_.Assets().Discover());
}

bool EditorInputAssetGateway::WriteNewAction(const std::filesystem::path& path, const kb::input::InputActionAsset& asset) {
    if (!kb::input::WriteInputAction(path, asset)) {
        return false;
    }
    DiscoverAndSelect(path);
    return true;
}

bool EditorInputAssetGateway::WriteNewContext(const std::filesystem::path& path, const kb::input::InputMappingContextAsset& asset) {
    if (!kb::input::WriteInputMappingContext(path, asset)) {
        return false;
    }
    DiscoverAndSelect(path);
    return true;
}

std::optional<kb::input::InputActionAsset> EditorInputAssetGateway::ReadAction(const kb::scene::Scene& scene, kb::assets::AssetId id) {
    const std::optional<std::filesystem::path> physical = ResolveFile(scene, id, "InputAction");
    if (!physical.has_value()) {
        return std::nullopt;
    }
    kb::input::InputAssetLoadResult<kb::input::InputActionAsset> loaded = kb::input::ReadInputAction(*physical);
    return loaded.succeeded ? std::optional<kb::input::InputActionAsset>{ std::move(loaded.asset) } : std::nullopt;
}

std::optional<kb::input::InputMappingContextAsset> EditorInputAssetGateway::ReadContext(const kb::scene::Scene& scene, kb::assets::AssetId id) {
    const std::optional<std::filesystem::path> physical = ResolveFile(scene, id, "InputMappingContext");
    if (!physical.has_value()) {
        return std::nullopt;
    }
    kb::input::InputAssetLoadResult<kb::input::InputMappingContextAsset> loaded = kb::input::ReadInputMappingContext(*physical);
    return loaded.succeeded ? std::optional<kb::input::InputMappingContextAsset>{ std::move(loaded.asset) } : std::nullopt;
}

bool EditorInputAssetGateway::MutateAction(kb::assets::AssetId id, const std::function<void(kb::input::InputActionAsset&)>& mutate) {
    const std::optional<std::filesystem::path> physical = ResolveFile(scene_, id, "InputAction");
    if (!physical.has_value()) {
        return false;
    }
    if (!ApplyFileEdit<kb::input::InputActionAsset>(*physical, &kb::input::ReadInputAction, &kb::input::WriteInputAction, mutate)) {
        return false;
    }
    RefreshAfterWrite(id);
    return true;
}

bool EditorInputAssetGateway::MutateContext(kb::assets::AssetId id, const std::function<void(kb::input::InputMappingContextAsset&)>& mutate) {
    const std::optional<std::filesystem::path> physical = ResolveFile(scene_, id, "InputMappingContext");
    if (!physical.has_value()) {
        return false;
    }
    if (!ApplyFileEdit<kb::input::InputMappingContextAsset>(*physical, &kb::input::ReadInputMappingContext, &kb::input::WriteInputMappingContext, mutate)) {
        return false;
    }
    RefreshAfterWrite(id);
    return true;
}

} // namespace kb::editor
