#include "scene/audio/EditorAudioMixerAuthoring.hpp"

#include "console/EditorConsoleState.hpp"
#include "engine/audio/AudioMixerAssetIO.hpp"
#include "engine/scene/Scene.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace kb::editor {
namespace {

template <typename Range>
[[nodiscard]] auto FindNamed(Range& range, std::string_view name) {
    return std::find_if(range.begin(), range.end(), [name](const auto& value) {
        return value.name == name;
    });
}

[[nodiscard]] bool ValidVolume(float volume) noexcept {
    return std::isfinite(volume) && volume >= 0.0F;
}

} // namespace

EditorAudioMixerAuthoring::EditorAudioMixerAuthoring(
    kb::scene::Scene& scene,
    EditorAssetBrowserState& browser,
    EditorConsoleState& console) noexcept
    : gateway_(scene, browser)
    , scene_(scene)
    , console_(console) {}

bool EditorAudioMixerAuthoring::Create(const std::filesystem::path& virtualFolder) {
    const std::optional<std::filesystem::path> folder = gateway_.ResolveFolder(virtualFolder);
    if (!folder.has_value()) {
        console_.Error("Audio", "Could not resolve a physical folder for the new audio mixer.");
        return false;
    }
    const std::filesystem::path path = EditorAudioMixerAssetGateway::UniqueFilePath(
        *folder,
        "NewAudioMixer",
        kb::audio::kAudioMixerAssetExtension);
    if (!gateway_.Create(path, kb::audio::AudioMixerAsset{})) {
        console_.Error("Audio", "Audio mixer asset could not be created: " + path.generic_string());
        return false;
    }
    console_.Info("Audio", "Audio mixer asset created: " + path.generic_string());
    return true;
}

std::optional<kb::audio::AudioMixerAsset> EditorAudioMixerAuthoring::Read(kb::assets::AssetId id) const {
    return EditorAudioMixerAssetGateway::Read(scene_, id);
}

bool EditorAudioMixerAuthoring::AddBus(kb::assets::AssetId id, std::string_view name) {
    if (!kb::audio::IsAudioMixerNameTokenValid(name)) {
        return false;
    }
    return gateway_.Mutate(id, [name](kb::audio::AudioMixerAsset& asset) {
        if (FindNamed(asset.buses, name) != asset.buses.end()) {
            return false;
        }
        asset.buses.push_back(kb::audio::AudioMixerBus{ .name = std::string{ name } });
        return true;
    });
}

bool EditorAudioMixerAuthoring::RemoveBus(kb::assets::AssetId id, std::string_view name) {
    return gateway_.Mutate(id, [name](kb::audio::AudioMixerAsset& asset) {
        const auto bus = FindNamed(asset.buses, name);
        if (bus == asset.buses.end()) {
            return false;
        }
        const std::string parent = bus->parentBus;
        asset.buses.erase(bus);
        for (kb::audio::AudioMixerBus& child : asset.buses) {
            if (child.parentBus == name) {
                child.parentBus = parent;
            }
        }
        for (kb::audio::AudioMixerSnapshot& snapshot : asset.snapshots) {
            std::erase_if(snapshot.busVolumes, [name](const kb::audio::AudioMixerSnapshotBusVolume& value) {
                return value.bus == name;
            });
        }
        return true;
    });
}

bool EditorAudioMixerAuthoring::RenameBus(
    kb::assets::AssetId id,
    std::string_view name,
    std::string_view replacement) {
    if (!kb::audio::IsAudioMixerNameTokenValid(replacement) || name == replacement) {
        return false;
    }
    return gateway_.Mutate(id, [name, replacement](kb::audio::AudioMixerAsset& asset) {
        const auto bus = FindNamed(asset.buses, name);
        if (bus == asset.buses.end() || FindNamed(asset.buses, replacement) != asset.buses.end()) {
            return false;
        }
        bus->name = replacement;
        for (kb::audio::AudioMixerBus& routed : asset.buses) {
            if (routed.parentBus == name) {
                routed.parentBus = replacement;
            }
        }
        for (kb::audio::AudioMixerSnapshot& snapshot : asset.snapshots) {
            for (kb::audio::AudioMixerSnapshotBusVolume& value : snapshot.busVolumes) {
                if (value.bus == name) {
                    value.bus = replacement;
                }
            }
        }
        return true;
    });
}

bool EditorAudioMixerAuthoring::SetBusParent(
    kb::assets::AssetId id,
    std::string_view name,
    std::string_view parent) {
    if ((!parent.empty() && !kb::audio::IsAudioMixerNameTokenValid(parent)) || name == parent) {
        return false;
    }
    return gateway_.Mutate(id, [name, parent](kb::audio::AudioMixerAsset& asset) {
        const auto bus = FindNamed(asset.buses, name);
        if (bus == asset.buses.end() || bus->parentBus == parent
            || (!parent.empty() && FindNamed(asset.buses, parent) == asset.buses.end())) {
            return false;
        }
        bus->parentBus = parent;
        return true;
    });
}

bool EditorAudioMixerAuthoring::SetBusVolume(
    kb::assets::AssetId id,
    std::string_view name,
    float volume) {
    if (!ValidVolume(volume)) {
        return false;
    }
    return gateway_.Mutate(id, [name, volume](kb::audio::AudioMixerAsset& asset) {
        const auto bus = FindNamed(asset.buses, name);
        if (bus == asset.buses.end() || bus->volume == volume) {
            return false;
        }
        bus->volume = volume;
        return true;
    });
}

bool EditorAudioMixerAuthoring::SetBusMute(
    kb::assets::AssetId id,
    std::string_view name,
    bool mute) {
    return gateway_.Mutate(id, [name, mute](kb::audio::AudioMixerAsset& asset) {
        const auto bus = FindNamed(asset.buses, name);
        if (bus == asset.buses.end() || bus->mute == mute) {
            return false;
        }
        bus->mute = mute;
        return true;
    });
}

bool EditorAudioMixerAuthoring::AddSnapshot(kb::assets::AssetId id, std::string_view name) {
    if (!kb::audio::IsAudioMixerNameTokenValid(name)) {
        return false;
    }
    return gateway_.Mutate(id, [name](kb::audio::AudioMixerAsset& asset) {
        if (FindNamed(asset.snapshots, name) != asset.snapshots.end()) {
            return false;
        }
        asset.snapshots.push_back(kb::audio::AudioMixerSnapshot{ .name = std::string{ name } });
        return true;
    });
}

bool EditorAudioMixerAuthoring::RemoveSnapshot(kb::assets::AssetId id, std::string_view name) {
    return gateway_.Mutate(id, [name](kb::audio::AudioMixerAsset& asset) {
        const auto snapshot = FindNamed(asset.snapshots, name);
        if (snapshot == asset.snapshots.end()) {
            return false;
        }
        asset.snapshots.erase(snapshot);
        return true;
    });
}

bool EditorAudioMixerAuthoring::RenameSnapshot(
    kb::assets::AssetId id,
    std::string_view name,
    std::string_view replacement) {
    if (!kb::audio::IsAudioMixerNameTokenValid(replacement) || name == replacement) {
        return false;
    }
    return gateway_.Mutate(id, [name, replacement](kb::audio::AudioMixerAsset& asset) {
        const auto snapshot = FindNamed(asset.snapshots, name);
        if (snapshot == asset.snapshots.end()
            || FindNamed(asset.snapshots, replacement) != asset.snapshots.end()) {
            return false;
        }
        snapshot->name = replacement;
        return true;
    });
}

bool EditorAudioMixerAuthoring::AddSnapshotOverride(
    kb::assets::AssetId id,
    std::string_view snapshot,
    std::string_view bus,
    float volume) {
    if (!ValidVolume(volume)) {
        return false;
    }
    return gateway_.Mutate(id, [snapshot, bus, volume](kb::audio::AudioMixerAsset& asset) {
        const auto snapshotIt = FindNamed(asset.snapshots, snapshot);
        if (snapshotIt == asset.snapshots.end() || FindNamed(asset.buses, bus) == asset.buses.end()
            || std::find_if(snapshotIt->busVolumes.begin(), snapshotIt->busVolumes.end(), [bus](const auto& value) {
                return value.bus == bus;
            }) != snapshotIt->busVolumes.end()) {
            return false;
        }
        snapshotIt->busVolumes.push_back(kb::audio::AudioMixerSnapshotBusVolume{
            .bus = std::string{ bus },
            .volume = volume,
        });
        return true;
    });
}

bool EditorAudioMixerAuthoring::RemoveSnapshotOverride(
    kb::assets::AssetId id,
    std::string_view snapshot,
    std::string_view bus) {
    return gateway_.Mutate(id, [snapshot, bus](kb::audio::AudioMixerAsset& asset) {
        const auto snapshotIt = FindNamed(asset.snapshots, snapshot);
        if (snapshotIt == asset.snapshots.end()) {
            return false;
        }
        const auto value = std::find_if(snapshotIt->busVolumes.begin(), snapshotIt->busVolumes.end(), [bus](const auto& candidate) {
            return candidate.bus == bus;
        });
        if (value == snapshotIt->busVolumes.end()) {
            return false;
        }
        snapshotIt->busVolumes.erase(value);
        return true;
    });
}

bool EditorAudioMixerAuthoring::SetSnapshotOverrideVolume(
    kb::assets::AssetId id,
    std::string_view snapshot,
    std::string_view bus,
    float volume) {
    if (!ValidVolume(volume)) {
        return false;
    }
    return gateway_.Mutate(id, [snapshot, bus, volume](kb::audio::AudioMixerAsset& asset) {
        const auto snapshotIt = FindNamed(asset.snapshots, snapshot);
        if (snapshotIt == asset.snapshots.end()) {
            return false;
        }
        const auto value = std::find_if(snapshotIt->busVolumes.begin(), snapshotIt->busVolumes.end(), [bus](const auto& candidate) {
            return candidate.bus == bus;
        });
        if (value == snapshotIt->busVolumes.end() || value->volume == volume) {
            return false;
        }
        value->volume = volume;
        return true;
    });
}

} // namespace kb::editor
