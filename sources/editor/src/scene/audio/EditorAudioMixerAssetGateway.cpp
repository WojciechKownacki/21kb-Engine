#include "scene/audio/EditorAudioMixerAssetGateway.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/audio/AudioMixerAssetIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <system_error>
#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] bool Equal(const kb::audio::AudioMixerAsset& left, const kb::audio::AudioMixerAsset& right) noexcept {
    if (left.buses.size() != right.buses.size() || left.snapshots.size() != right.snapshots.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.buses.size(); ++index) {
        const kb::audio::AudioMixerBus& a = left.buses[index];
        const kb::audio::AudioMixerBus& b = right.buses[index];
        if (a.name != b.name || a.parentBus != b.parentBus || a.volume != b.volume || a.mute != b.mute) {
            return false;
        }
    }
    for (std::size_t snapshotIndex = 0U; snapshotIndex < left.snapshots.size(); ++snapshotIndex) {
        const kb::audio::AudioMixerSnapshot& a = left.snapshots[snapshotIndex];
        const kb::audio::AudioMixerSnapshot& b = right.snapshots[snapshotIndex];
        if (a.name != b.name || a.busVolumes.size() != b.busVolumes.size()) {
            return false;
        }
        for (std::size_t valueIndex = 0U; valueIndex < a.busVolumes.size(); ++valueIndex) {
            if (a.busVolumes[valueIndex].bus != b.busVolumes[valueIndex].bus
                || a.busVolumes[valueIndex].volume != b.busVolumes[valueIndex].volume) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

EditorAudioMixerAssetGateway::EditorAudioMixerAssetGateway(
    kb::scene::Scene& scene,
    EditorAssetBrowserState& browser) noexcept
    : scene_(scene)
    , browser_(browser) {}

std::optional<std::filesystem::path> EditorAudioMixerAssetGateway::ResolveFolder(
    const std::filesystem::path& virtualFolder) const {
    if (virtualFolder.empty()) {
        return std::nullopt;
    }
    const std::optional<std::filesystem::path> probe =
        scene_.Assets().Manager().Mounts().Resolve(virtualFolder / "probe");
    return probe.has_value()
        ? std::optional<std::filesystem::path>{ probe->parent_path() }
        : std::nullopt;
}

std::filesystem::path EditorAudioMixerAssetGateway::UniqueFilePath(
    const std::filesystem::path& folder,
    std::string_view baseName,
    std::string_view extension) {
    std::filesystem::path candidate = folder / (std::string{ baseName } + std::string{ extension });
    std::uint32_t suffix = 1U;
    while (std::filesystem::exists(candidate)) {
        candidate = folder / (std::string{ baseName } + std::to_string(suffix) + std::string{ extension });
        ++suffix;
    }
    return candidate;
}

bool EditorAudioMixerAssetGateway::Create(
    const std::filesystem::path& path,
    const kb::audio::AudioMixerAsset& asset) {
    if (path.empty() || std::filesystem::exists(path)
        || !kb::audio::ValidateAudioMixerAsset(asset).empty()
        || !kb::audio::AudioMixerAssetIO::Save(path, asset)) {
        return false;
    }
    if (DiscoverSelectAndLoad(path)) {
        return true;
    }
    std::error_code removeError;
    static_cast<void>(std::filesystem::remove(path, removeError));
    static_cast<void>(scene_.Assets().Discover());
    return false;
}

std::optional<kb::audio::AudioMixerAsset> EditorAudioMixerAssetGateway::Read(
    const kb::scene::Scene& scene,
    kb::assets::AssetId id) {
    const std::optional<std::filesystem::path> path = ResolveFile(scene, id);
    return path.has_value() ? kb::audio::AudioMixerAssetIO::Load(*path) : std::nullopt;
}

bool EditorAudioMixerAssetGateway::Mutate(kb::assets::AssetId id, const Mutation& mutation) {
    const std::optional<std::filesystem::path> path = ResolveFile(scene_, id);
    if (!path.has_value() || !mutation) {
        return false;
    }
    const std::optional<kb::audio::AudioMixerAsset> loaded = kb::audio::AudioMixerAssetIO::Load(*path);
    if (!loaded.has_value()) {
        return false;
    }
    kb::audio::AudioMixerAsset candidate = *loaded;
    if (!mutation(candidate) || !kb::audio::ValidateAudioMixerAsset(candidate).empty()
        || Equal(candidate, *loaded)) {
        return false;
    }
    if (!kb::audio::AudioMixerAssetIO::Save(*path, candidate)) {
        return false;
    }
    const std::optional<kb::audio::AudioMixerAsset> persisted = kb::audio::AudioMixerAssetIO::Load(*path);
    if (!persisted.has_value() || !Equal(*persisted, candidate)) {
        if (kb::audio::AudioMixerAssetIO::Save(*path, *loaded)) {
            return false;
        }
        return RefreshAndLoad(id, candidate);
    }
    if (RefreshAndLoad(id, candidate)) {
        return true;
    }
    bool restoredOnDisk = false;
    if (RestoreAfterFailedRefresh(*path, id, *loaded, restoredOnDisk) || restoredOnDisk) {
        return false;
    }
    return RefreshAndLoad(id, candidate);
}

std::optional<std::filesystem::path> EditorAudioMixerAssetGateway::ResolveFile(
    const kb::scene::Scene& scene,
    kb::assets::AssetId id) {
    const kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr || metadata->type != kb::audio::kAudioMixerAssetType) {
        return std::nullopt;
    }
    return !metadata->physicalPath.empty()
        ? std::optional<std::filesystem::path>{ metadata->physicalPath }
        : manager.Mounts().Resolve(metadata->virtualPath);
}

bool EditorAudioMixerAssetGateway::DiscoverSelectAndLoad(const std::filesystem::path& path) {
    kb::assets::AssetManager& manager = scene_.Assets().Manager();
    static_cast<void>(scene_.Assets().Discover());
    const std::optional<std::filesystem::path> virtualPath = manager.Mounts().ToVirtual(path);
    const kb::assets::AssetMetadata* metadata = virtualPath.has_value()
        ? manager.Registry().FindByPath(*virtualPath)
        : nullptr;
    if (metadata == nullptr || metadata->type != kb::audio::kAudioMixerAssetType) {
        return false;
    }
    const kb::assets::AssetHandle<kb::audio::AudioMixerAsset> loaded =
        manager.Load<kb::audio::AudioMixerAsset>(metadata->id);
    if (!loaded.IsLoaded()) {
        static_cast<void>(manager.Unload(metadata->id));
        return false;
    }
    if (!browser_.SelectAsset(metadata->id, manager)) {
        static_cast<void>(manager.Unload(metadata->id));
        return false;
    }
    return true;
}

bool EditorAudioMixerAssetGateway::RefreshAndLoad(
    kb::assets::AssetId id,
    const kb::audio::AudioMixerAsset& expected) {
    kb::assets::AssetManager& manager = scene_.Assets().Manager();
    static_cast<void>(manager.Unload(id));
    static_cast<void>(scene_.Assets().Discover());
    const kb::assets::AssetHandle<kb::audio::AudioMixerAsset> loaded =
        manager.Load<kb::audio::AudioMixerAsset>(id);
    return loaded.IsLoaded() && Equal(*loaded, expected);
}

bool EditorAudioMixerAssetGateway::RestoreAfterFailedRefresh(
    const std::filesystem::path& path,
    kb::assets::AssetId id,
    const kb::audio::AudioMixerAsset& original,
    bool& restoredOnDisk) {
    restoredOnDisk = false;
    if (!kb::audio::AudioMixerAssetIO::Save(path, original)) {
        return false;
    }
    restoredOnDisk = true;
    kb::assets::AssetManager& manager = scene_.Assets().Manager();
    static_cast<void>(manager.Unload(id));
    static_cast<void>(scene_.Assets().Discover());
    return manager.Load<kb::audio::AudioMixerAsset>(id).IsLoaded();
}

} // namespace kb::editor
