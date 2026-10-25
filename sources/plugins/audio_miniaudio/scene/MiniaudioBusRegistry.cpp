#include "scene/MiniaudioBusRegistry.hpp"
#include "runtime/MiniaudioEngine.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/audio/AudioMixerAsset.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <new>

namespace kb::audio_miniaudio {
namespace {

[[nodiscard]] float ValidVolumeOr(float value, float fallback) noexcept {
    return std::isfinite(value) ? std::max(0.0F, value) : fallback;
}

} // namespace

MiniaudioBusRegistry::~MiniaudioBusRegistry() {
    DestroyGroups();
}

bool MiniaudioBusRegistry::Sync(kb::scene::Scene& scene, bool playbackAvailable) {
    ma_engine& engine = engine_.Native();
    const std::uint64_t mixerAssetId = kb::scene::SceneAudioMixerAccess::ActiveMixer(scene);
    if (!playbackAvailable || mixerAssetId == 0U) {
        return TearDown();
    }

    const kb::assets::AssetHandle<kb::audio::AudioMixerAsset> mixer =
        scene.Assets().Manager().Load<kb::audio::AudioMixerAsset>(kb::assets::AssetId{ mixerAssetId });
    if (!mixer.IsLoaded()) {
        // A requested named route remains unavailable until the selected mixer resolves.
        return TearDown();
    }

    bool structureMatches = mixerLoaded_ && mixerAssetId == activeMixerAssetId_ && topology_.size() == mixer->buses.size();
    if (structureMatches) {
        for (std::size_t index = 0U; index < topology_.size(); ++index) {
            if (topology_[index].name != mixer->buses[index].name || topology_[index].parent != mixer->buses[index].parentBus) {
                structureMatches = false;
                break;
            }
        }
    }

    bool rebuilt = false;
    if (!structureMatches) {
        DestroyGroups();
        topology_.clear();
        topology_.reserve(mixer->buses.size());
        for (const kb::audio::AudioMixerBus& bus : mixer->buses) {
            topology_.push_back(BusTopology{ .name = bus.name, .parent = bus.parentBus });
        }
        buses_.reserve(mixer->buses.size());
        // Parent-before-child creation. The asset is validated acyclic with every parent
        // authored, so each pass over the pending list resolves at least one bus and the
        // loop terminates. Any initialization failure rejects the whole routing graph;
        // partial routing could otherwise send children to an unintended output.
        std::vector<const kb::audio::AudioMixerBus*> pending;
        pending.reserve(mixer->buses.size());
        for (const kb::audio::AudioMixerBus& bus : mixer->buses) {
            pending.push_back(&bus);
        }
        bool progressed = true;
        bool initializationFailed = false;
        while (!pending.empty() && progressed) {
            progressed = false;
            for (auto iterator = pending.begin(); iterator != pending.end();) {
                const kb::audio::AudioMixerBus& bus = **iterator;
                ma_sound_group* parentGroup = nullptr;
                if (!bus.parentBus.empty()) {
                    for (BusRecord& record : buses_) {
                        if (record.name == bus.parentBus) {
                            parentGroup = record.group.get();
                            break;
                        }
                    }
                    if (parentGroup == nullptr) {
                        ++iterator; // parent not created yet - a later pass picks this bus up.
                        continue;
                    }
                }
                auto group = std::make_unique<ma_sound_group>();
                const auto lock = engine_.LockGraph();
                if (ma_sound_group_init(&engine, 0U, parentGroup, group.get()) == MA_SUCCESS) {
                    buses_.push_back(BusRecord{ .name = bus.name, .parent = bus.parentBus, .group = std::move(group) });
                } else {
                    initializationFailed = true;
                }
                iterator = pending.erase(iterator);
                progressed = true;
            }
        }
        initializationFailed = initializationFailed || !pending.empty() || buses_.size() != mixer->buses.size();
        if (initializationFailed) {
            DestroyGroups();
        }
        activeMixerAssetId_ = mixerAssetId;
        mixerLoaded_ = true;
        groupsInitialized_ = !initializationFailed;
        ++generation_;
        rebuilt = true;
    }

    // Volumes re-apply every tick, resolved in layers: authored value -> active snapshot
    // (or, mid-transition, a deterministic lerp between the FROM and TO snapshot states,
    // driven by scene delta time) -> LIB-150 runtime per-bus override (strongest) -> mute
    // silences everything. Cheap - a mixer has a handful of buses.
    const kb::audio::AudioMixerSnapshot* fromSnapshot = mixer->FindSnapshot(kb::scene::SceneAudioMixerAccess::ActiveSnapshot(scene));
    const kb::scene::AudioMixerSnapshotTransition& transition = kb::scene::SceneAudioMixerAccess::SnapshotTransition(scene);
    const kb::audio::AudioMixerSnapshot* toSnapshot = transition.IsActive() ? mixer->FindSnapshot(transition.toSnapshot) : nullptr;
    const std::span<const kb::scene::AudioMixerBusVolumeOverride> overrides = kb::scene::SceneAudioMixerAccess::BusVolumeOverrides(scene);
    const auto snapshotVolume = [](const kb::audio::AudioMixerSnapshot* snapshot, const std::string& busName, float authored) noexcept {
        if (snapshot != nullptr) {
            for (const kb::audio::AudioMixerSnapshotBusVolume& value : snapshot->busVolumes) {
                if (value.bus == busName) {
                    return ValidVolumeOr(value.volume, authored);
                }
            }
        }
        return authored;
    };
    for (BusRecord& record : buses_) {
        const kb::audio::AudioMixerBus* bus = mixer->FindBus(record.name);
        if (bus == nullptr || record.group == nullptr) {
            continue;
        }
        const float authored = ValidVolumeOr(bus->volume, 1.0F);
        float volume = snapshotVolume(fromSnapshot, record.name, authored);
        if (transition.IsActive()) {
            for (const auto& captured : transition.fromVolumes) {
                if (captured.bus == record.name) {
                    volume = ValidVolumeOr(captured.volume, volume);
                    break;
                }
            }
            // An empty/unknown transition target resolves to the authored volumes - the
            // same honest fallback an unknown active snapshot already has.
            const float target = snapshotVolume(toSnapshot, record.name, authored);
            const float progress = transition.Progress();
            volume = volume + ((target - volume) * progress);
        }
        for (const kb::scene::AudioMixerBusVolumeOverride& override_ : overrides) {
            if (override_.bus == record.name) {
                volume = ValidVolumeOr(override_.volume, volume);
                break;
            }
        }
        SyncEffects(record, bus->effects, bus->mute ? 0.0F : volume);
    }
    return rebuilt;
}

void MiniaudioBusRegistry::SyncEffects(BusRecord& record,
    const std::vector<kb::audio::AudioDspEffect>& requested, float volume) {
    if (record.effects != nullptr && record.appliedEffects == requested) {
        record.effectError.clear();
        record.settingsCurrent = true;
        static_cast<void>(record.effects->SetOutputVolume(volume, record.group.get()));
        return;
    }
    record.settingsCurrent = false;
    const auto now = std::chrono::steady_clock::now();
    if (record.attemptedEffects == requested && now < record.nextEffectAttempt) {
        if (record.effectError.empty()) record.effectError = "DSP update is awaiting retry";
        if (record.effects != nullptr) static_cast<void>(record.effects->SetOutputVolume(volume, record.group.get()));
        return;
    }
    record.nextEffectAttempt = now + std::chrono::seconds{1};
    ma_node* target = ma_engine_get_endpoint(&engine_.Native());
    if (!record.parent.empty()) {
        const auto parent = std::find_if(buses_.begin(), buses_.end(), [&](const auto& bus) { return bus.name == record.parent; });
        if (parent == buses_.end() || parent->group == nullptr) {
            record.effectError = "DSP parent bus is unavailable";
            return;
        }
        target = parent->group.get();
    }
    std::unique_ptr<MiniaudioFilterChain> candidate;
    std::vector<kb::audio::AudioDspEffect> accepted;
    ma_result result = MA_OUT_OF_MEMORY;
    try {
        accepted = requested;
        record.attemptedEffects = requested;
        std::vector<MiniaudioFilterDesc> parameters;
        parameters.reserve(requested.size());
        for (const auto& effect : requested) parameters.push_back(effect.parameters);
        candidate = std::make_unique<MiniaudioFilterChain>();
        result = candidate->Initialize(*ma_engine_get_node_graph(&engine_.Native()),
            ma_engine_get_sample_rate(&engine_.Native()), parameters);
    } catch (const std::bad_alloc&) {
        result = MA_OUT_OF_MEMORY;
    }
    if (result == MA_SUCCESS) {
        // Candidate construction stays outside the read exclusion. The callback
        // sees either complete routing, and old state is freed after detachment.
        const auto lock = engine_.LockGraph();
        if (record.effects != nullptr) result = record.effects->DetachOutput(record.group.get());
        else result = ma_node_detach_output_bus(record.group.get(), 0U);
        if (result == MA_SUCCESS) result = ma_node_set_output_bus_volume(record.group.get(), 0U,
            candidate->ActiveNodeCount() == 0U ? volume : 1.0F);
        if (result == MA_SUCCESS) result = candidate->SetOutputVolume(volume, record.group.get());
        if (result == MA_SUCCESS) result = candidate->Attach(record.group.get(), target);
        if (result != MA_SUCCESS) {
            static_cast<void>(candidate->DetachOutput(record.group.get()));
            if (record.effects != nullptr) {
                static_cast<void>(ma_node_set_output_bus_volume(record.group.get(), 0U,
                    record.effects->ActiveNodeCount() == 0U ? volume : 1.0F));
                if (record.effects->Attach(record.group.get(), target) == MA_SUCCESS) {
                    static_cast<void>(record.effects->SetOutputVolume(volume, record.group.get()));
                } else {
                    record.effects.reset();
                }
            }
        }
    }
    if (result == MA_SUCCESS) {
        record.effects.swap(candidate);
        record.appliedEffects.swap(accepted);
        record.effectError.clear();
        record.settingsCurrent = true;
    } else {
        record.effectError = ma_result_description(result);
        if (record.effects == nullptr) {
            const auto lock = engine_.LockGraph();
            static_cast<void>(ma_node_set_output_bus_volume(record.group.get(), 0U, 0.0F));
        } else {
            static_cast<void>(record.effects->SetOutputVolume(volume, record.group.get()));
        }
    }
}

bool MiniaudioBusRegistry::RoutingWillChange(kb::scene::Scene& scene, bool playbackAvailable) const {
    const std::uint64_t mixerAssetId = kb::scene::SceneAudioMixerAccess::ActiveMixer(scene);
    if (!playbackAvailable || mixerAssetId == 0U) {
        return mixerLoaded_ || activeMixerAssetId_ != 0U || !topology_.empty() || !buses_.empty();
    }
    const kb::assets::AssetHandle<kb::audio::AudioMixerAsset> mixer =
        scene.Assets().Manager().Load<kb::audio::AudioMixerAsset>(kb::assets::AssetId{ mixerAssetId });
    if (!mixer.IsLoaded()) {
        return mixerLoaded_ || activeMixerAssetId_ != 0U || !topology_.empty() || !buses_.empty();
    }
    if (!mixerLoaded_ || mixerAssetId != activeMixerAssetId_ || topology_.size() != mixer->buses.size()) {
        return true;
    }
    for (std::size_t index = 0U; index < topology_.size(); ++index) {
        if (topology_[index].name != mixer->buses[index].name || topology_[index].parent != mixer->buses[index].parentBus) {
            return true;
        }
    }
    return false;
}

MiniaudioBusRegistry::Route MiniaudioBusRegistry::Resolve(std::string_view busName) noexcept {
    if (busName.empty()) {
        return Route{ .status = RouteStatus::Master, .group = nullptr };
    }
    if (!mixerLoaded_) {
        return Route{ .status = RouteStatus::MixerUnavailable, .group = nullptr };
    }
    if (!groupsInitialized_) {
        return Route{ .status = RouteStatus::InitializationFailed, .group = nullptr };
    }
    for (BusRecord& record : buses_) {
        if (record.name == busName) {
            const BusRecord* current = &record;
            for (std::size_t depth = 0U; depth < buses_.size(); ++depth) {
                if (current->effects == nullptr) return Route{.status = RouteStatus::InitializationFailed};
                if (current->parent.empty()) break;
                const auto parent = std::find_if(buses_.begin(), buses_.end(), [&](const auto& bus) { return bus.name == current->parent; });
                if (parent == buses_.end()) return Route{.status = RouteStatus::InitializationFailed};
                current = &*parent;
            }
            return Route{ .status = RouteStatus::Routed, .group = record.group.get() };
        }
    }
    return Route{ .status = RouteStatus::UnknownBus, .group = nullptr };
}

void MiniaudioBusRegistry::StopAll() noexcept {
    static_cast<void>(TearDown());
}

kb::audio::AudioDspRuntimeReport MiniaudioBusRegistry::DspReport() const {
    kb::audio::AudioDspRuntimeReport report{.backendAvailable = true, .mixerAssetId = activeMixerAssetId_};
    report.buses.reserve(buses_.size());
    for (const auto& bus : buses_) {
        report.buses.push_back({.bus = bus.name, .initialized = bus.effects != nullptr,
            .settingsCurrent = bus.effects != nullptr && bus.settingsCurrent,
            .activeEffects = bus.effects != nullptr ? bus.effects->ActiveNodeCount() : 0U,
            .appliedEffects = bus.appliedEffects, .error = bus.effectError});
    }
    return report;
}

void MiniaudioBusRegistry::DestroyGroups() noexcept {
    const auto lock = engine_.LockGraph();
    // Children before parents - creation appended parents first, so destroy in reverse.
    for (auto iterator = buses_.rbegin(); iterator != buses_.rend(); ++iterator) {
        iterator->effects.reset();
        if (iterator->group != nullptr) {
            ma_sound_group_uninit(iterator->group.get());
        }
    }
    buses_.clear();
}

bool MiniaudioBusRegistry::TearDown() noexcept {
    if (buses_.empty() && activeMixerAssetId_ == 0U && topology_.empty() && !mixerLoaded_) {
        return false;
    }
    DestroyGroups();
    topology_.clear();
    activeMixerAssetId_ = 0U;
    mixerLoaded_ = false;
    groupsInitialized_ = false;
    ++generation_;
    return true;
}

} // namespace kb::audio_miniaudio
