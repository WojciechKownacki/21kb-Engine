#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/audio/AudioMixerAsset.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::editor {

class EditorAssetBrowserState;

class EditorAudioMixerAssetGateway {
public:
    using Mutation = std::function<bool(kb::audio::AudioMixerAsset&)>;

    EditorAudioMixerAssetGateway(kb::scene::Scene& scene, EditorAssetBrowserState& browser) noexcept;

    [[nodiscard]] std::optional<std::filesystem::path> ResolveFolder(const std::filesystem::path& virtualFolder) const;
    [[nodiscard]] static std::filesystem::path UniqueFilePath(
        const std::filesystem::path& folder,
        std::string_view baseName,
        std::string_view extension);
    [[nodiscard]] bool Create(const std::filesystem::path& path, const kb::audio::AudioMixerAsset& asset);
    [[nodiscard]] static std::optional<kb::audio::AudioMixerAsset> Read(
        const kb::scene::Scene& scene,
        kb::assets::AssetId id);
    [[nodiscard]] bool Mutate(kb::assets::AssetId id, const Mutation& mutation);

private:
    [[nodiscard]] static std::optional<std::filesystem::path> ResolveFile(
        const kb::scene::Scene& scene,
        kb::assets::AssetId id);
    [[nodiscard]] bool DiscoverSelectAndLoad(const std::filesystem::path& path);
    [[nodiscard]] bool RefreshAndLoad(kb::assets::AssetId id, const kb::audio::AudioMixerAsset& expected);
    [[nodiscard]] bool RestoreAfterFailedRefresh(
        const std::filesystem::path& path,
        kb::assets::AssetId id,
        const kb::audio::AudioMixerAsset& original,
        bool& restoredOnDisk);

    kb::scene::Scene& scene_;
    EditorAssetBrowserState& browser_;
};

} // namespace kb::editor
