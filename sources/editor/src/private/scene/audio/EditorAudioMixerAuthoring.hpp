#pragma once

#include "engine/assets/AssetId.hpp"
#include "scene/audio/EditorAudioMixerAssetGateway.hpp"

#include <filesystem>
#include <optional>
#include <string_view>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::editor {

class EditorAssetBrowserState;
class EditorConsoleState;

class EditorAudioMixerAuthoring {
public:
    EditorAudioMixerAuthoring(
        kb::scene::Scene& scene,
        EditorAssetBrowserState& browser,
        EditorConsoleState& console) noexcept;

    [[nodiscard]] bool Create(const std::filesystem::path& virtualFolder);
    [[nodiscard]] std::optional<kb::audio::AudioMixerAsset> Read(kb::assets::AssetId id) const;

    [[nodiscard]] bool AddBus(kb::assets::AssetId id, std::string_view name);
    [[nodiscard]] bool RemoveBus(kb::assets::AssetId id, std::string_view name);
    [[nodiscard]] bool RenameBus(kb::assets::AssetId id, std::string_view name, std::string_view replacement);
    [[nodiscard]] bool SetBusParent(kb::assets::AssetId id, std::string_view name, std::string_view parent);
    [[nodiscard]] bool SetBusVolume(kb::assets::AssetId id, std::string_view name, float volume);
    [[nodiscard]] bool SetBusMute(kb::assets::AssetId id, std::string_view name, bool mute);

    [[nodiscard]] bool AddSnapshot(kb::assets::AssetId id, std::string_view name);
    [[nodiscard]] bool RemoveSnapshot(kb::assets::AssetId id, std::string_view name);
    [[nodiscard]] bool RenameSnapshot(kb::assets::AssetId id, std::string_view name, std::string_view replacement);
    [[nodiscard]] bool AddSnapshotOverride(
        kb::assets::AssetId id,
        std::string_view snapshot,
        std::string_view bus,
        float volume);
    [[nodiscard]] bool RemoveSnapshotOverride(
        kb::assets::AssetId id,
        std::string_view snapshot,
        std::string_view bus);
    [[nodiscard]] bool SetSnapshotOverrideVolume(
        kb::assets::AssetId id,
        std::string_view snapshot,
        std::string_view bus,
        float volume);

private:
    EditorAudioMixerAssetGateway gateway_;
    const kb::scene::Scene& scene_;
    EditorConsoleState& console_;
};

} // namespace kb::editor
