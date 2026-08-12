#pragma once

#include <miniaudio.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::audio_miniaudio {

// LIB-147: the runtime side of kb::audio::AudioMixerAsset - one ma_sound_group per
// authored AudioMixerBus, parented per the asset's routing graph (a bus with an empty
// parent feeds the engine's own endpoint, the implicit master). Synced once per audio
// tick from the scene's active mixer selection (kb::scene::SceneAudioMixerAccess):
// resolving the asset lazily through AssetManager keeps a hot-reloaded .kbmixer live, an
// unresolvable/absent mixer tears the groups down (only explicitly empty routes still
// select master), and the active snapshot's per-bus volumes are applied
// over the authored ones every tick (an unknown snapshot name applies nothing).
//
// Topology changes (different mixer asset, or the same asset re-authored with different
// bus names/parents) rebuild the groups. ma_sound objects attached to a destroyed group
// would dangle, so Sync reports the rebuild: the backend stops one-shot voices and the
// source registry recreates entity sounds through the bus generation baked into its sound
// signatures.
class MiniaudioBusRegistry final {
public:
    enum class RouteStatus : std::uint8_t {
        Master,
        Routed,
        MixerUnavailable,
        UnknownBus,
        InitializationFailed,
    };

    struct Route {
        RouteStatus status = RouteStatus::MixerUnavailable;
        ma_sound_group* group = nullptr;

        [[nodiscard]] bool Succeeded() const noexcept {
            return status == RouteStatus::Master || status == RouteStatus::Routed;
        }
    };

    ~MiniaudioBusRegistry();

    MiniaudioBusRegistry() = default;
    MiniaudioBusRegistry(const MiniaudioBusRegistry&) = delete;
    MiniaudioBusRegistry& operator=(const MiniaudioBusRegistry&) = delete;
    MiniaudioBusRegistry(MiniaudioBusRegistry&&) = delete;
    MiniaudioBusRegistry& operator=(MiniaudioBusRegistry&&) = delete;

    // Returns true when the bus topology was rebuilt (or torn down) this sync - every
    // ma_sound attached to a previous group must be dropped by the caller.
    [[nodiscard]] bool Sync(ma_engine& engine, kb::scene::Scene& scene, bool playbackAvailable);
    [[nodiscard]] bool RoutingWillChange(kb::scene::Scene& scene, bool playbackAvailable) const;
    [[nodiscard]] Route Resolve(std::string_view busName) noexcept;
    // Monotonic, bumped on every topology rebuild/teardown - baked into the source
    // registry's sound signatures so live entity sounds recreate against fresh groups.
    [[nodiscard]] std::uint64_t Generation() const noexcept { return generation_; }
#if defined(KB_AUDIO_MINIAUDIO_TESTING)
    [[nodiscard]] std::size_t BusCount() const noexcept { return buses_.size(); }
#endif
    void StopAll() noexcept;

private:
    struct BusRecord {
        std::string name;
        std::string parent;
        std::unique_ptr<ma_sound_group> group;
    };

    struct BusTopology {
        std::string name;
        std::string parent;

        [[nodiscard]] friend bool operator==(const BusTopology&, const BusTopology&) noexcept = default;
    };

    void DestroyGroups() noexcept;
    [[nodiscard]] bool TearDown() noexcept;

    std::vector<BusRecord> buses_;
    std::vector<BusTopology> topology_;
    std::uint64_t activeMixerAssetId_ = 0U;
    std::uint64_t generation_ = 0U;
    bool mixerLoaded_ = false;
    bool groupsInitialized_ = false;
};

} // namespace kb::audio_miniaudio
