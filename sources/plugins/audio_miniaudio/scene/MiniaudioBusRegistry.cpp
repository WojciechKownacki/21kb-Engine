#include "scene/MiniaudioBusRegistry.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/audio/AudioMixerAsset.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"

#include <algorithm>
#include <cmath>

namespace kb::audio_miniaudio {
namespace {

[[nodiscard]] float ValidVolumeOr(float value, float fallback) noexcept {
    return std::isfinite(value) ? std::max(0.0F, value) : fallback;
}

} // namespace

MiniaudioBusRegistry::~MiniaudioBusRegistry() {
    DestroyGroups();
}

bool MiniaudioBusRegistry::Sync(ma_engine& engine, kb::scene::Scene& scene, bool playbackAvailable) {
    const std::uint64_t mixerAssetId = kb::scene::SceneAudioMixerAccess::ActiveMixer(scene);
    if (!playbackAvailable || mixerAssetId == 0U) {
        return TearDown();
    }

    const kb::assets::AssetHandle<kb::audio::AudioMixerAsset> mixer =
        scene.Assets().Manager().Load<kb::audio::AudioMixerAsset>(kb::assets::AssetId{ mixerAssetId });
    if (!mixer.IsLoaded()) {
        // Unresolvable/wrong-type mixer: honest fallback to the implicit master, exactly
        // like an unresolvable clipAssetId silently plays nothing rather than crashing.
        return TearDown();
    }

    bool structureMatches = mixerAssetId == activeMixerAssetId_ && buses_.size() == mixer->buses.size();
    if (structureMatches) {
        for (std::size_t index = 0U; index < buses_.size(); ++index) {
            if (buses_[index].name != mixer->buses[index].name || buses_[index].parent != mixer->buses[index].parentBus) {
                structureMatches = false;
                break;
            }
        }
    }

    bool rebuilt = false;
    if (!structureMatches) {
        DestroyGroups();
        buses_.reserve(mixer->buses.size());
        // Parent-before-child creation. The asset is validated acyclic with every parent
        // authored, so each pass over the pending list resolves at least one bus and the
        // loop terminates; a failed ma_sound_group_init degrades that bus (and, since its
        // children then never find their parent, its whole subtree) to the implicit
        // master rather than aborting the mixer.
        std::vector<const kb::audio::AudioMixerBus*> pending;
        pending.reserve(mixer->buses.size());
        for (const kb::audio::AudioMixerBus& bus : mixer->buses) {
            pending.push_back(&bus);
        }
        bool progressed = true;
        while (!pending.empty() && progressed) {
            progressed = false;
            for (auto iterator = pending.begin(); iterator != pending.end();) {
                const kb::audio::AudioMixerBus& bus = **iterator;
                ma_sound_group* parentGroup = nullptr;
                if (!bus.parentBus.empty()) {
                    parentGroup = FindGroup(bus.parentBus);
                    if (parentGroup == nullptr) {
                        ++iterator; // parent not created yet - a later pass picks this bus up.
                        continue;
                    }
                }
                auto group = std::make_unique<ma_sound_group>();
                if (ma_sound_group_init(&engine, 0U, parentGroup, group.get()) == MA_SUCCESS) {
                    buses_.push_back(BusRecord{ .name = bus.name, .parent = bus.parentBus, .group = std::move(group) });
                }
                iterator = pending.erase(iterator);
                progressed = true;
            }
        }
        activeMixerAssetId_ = mixerAssetId;
        ++generation_;
        rebuilt = true;
    }

    // Volumes re-apply every tick: authored value, overridden by the active snapshot when
    // it names this bus (an unknown snapshot name resolves to nullptr and applies
    // nothing), silenced entirely by mute. Cheap - a mixer has a handful of buses.
    const kb::audio::AudioMixerSnapshot* snapshot = mixer->FindSnapshot(kb::scene::SceneAudioMixerAccess::ActiveSnapshot(scene));
    for (BusRecord& record : buses_) {
        const kb::audio::AudioMixerBus* bus = mixer->FindBus(record.name);
        if (bus == nullptr || record.group == nullptr) {
            continue;
        }
        float volume = ValidVolumeOr(bus->volume, 1.0F);
        if (snapshot != nullptr) {
            for (const kb::audio::AudioMixerSnapshotBusVolume& value : snapshot->busVolumes) {
                if (value.bus == record.name) {
                    volume = ValidVolumeOr(value.volume, volume);
                    break;
                }
            }
        }
        ma_sound_group_set_volume(record.group.get(), bus->mute ? 0.0F : volume);
    }
    return rebuilt;
}

ma_sound_group* MiniaudioBusRegistry::FindGroup(std::string_view busName) noexcept {
    if (busName.empty()) {
        return nullptr;
    }
    for (BusRecord& record : buses_) {
        if (record.name == busName) {
            return record.group.get();
        }
    }
    return nullptr;
}

void MiniaudioBusRegistry::StopAll() noexcept {
    static_cast<void>(TearDown());
}

void MiniaudioBusRegistry::DestroyGroups() noexcept {
    // Children before parents - creation appended parents first, so destroy in reverse.
    for (auto iterator = buses_.rbegin(); iterator != buses_.rend(); ++iterator) {
        if (iterator->group != nullptr) {
            ma_sound_group_uninit(iterator->group.get());
        }
    }
    buses_.clear();
}

bool MiniaudioBusRegistry::TearDown() noexcept {
    if (buses_.empty() && activeMixerAssetId_ == 0U) {
        return false;
    }
    DestroyGroups();
    activeMixerAssetId_ = 0U;
    ++generation_;
    return true;
}

} // namespace kb::audio_miniaudio
