#pragma once

#include "assets/ResolvedAudioClip.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::audio_miniaudio {

class MiniaudioClipResolver final {
public:
    enum class ResolutionStatus : std::uint8_t {
        Resolved,
        MissingOrInvalid,
        DecodeRejected,
    };

    struct Resolution final {
        ResolutionStatus status = ResolutionStatus::MissingOrInvalid;
        ResolvedAudioClip clip;

        [[nodiscard]] bool Succeeded() const noexcept {
            return status == ResolutionStatus::Resolved && clip.IsValid();
        }
    };

    [[nodiscard]] Resolution Resolve(kb::scene::Scene& scene, std::uint64_t clipAssetId) const;
    void Reset() noexcept;

#if defined(KB_AUDIO_MINIAUDIO_TESTING)
    struct ValidationStatsForTesting final {
        std::uint64_t attempts = 0U;
        std::uint64_t payloadHashAttempts = 0U;
        std::uint64_t payloadBytesHashed = 0U;
        std::uint64_t maxDecodedProbeFrames = 0U;
        std::uint64_t maxTailBytesInspected = 0U;
        std::size_t cacheEntries = 0U;
        std::size_t maxCacheEntries = 0U;
    };

    [[nodiscard]] ValidationStatsForTesting StatsForTesting() const noexcept;
    [[nodiscard]] static constexpr std::size_t ValidationCacheCapacityForTesting() noexcept {
        return kValidationCacheCapacity;
    }
#endif

private:
    struct ValidationCacheEntry final {
        std::string identity;
        std::uint64_t lastUse = 0U;
        bool valid = false;
    };

    static constexpr std::size_t kValidationCacheCapacity = 128U;

    [[nodiscard]] Resolution ResolveImportedAudio(
        kb::scene::Scene& scene,
        std::uint64_t clipAssetId,
        std::uint64_t metadataContentHash) const;
    [[nodiscard]] bool ValidateDecodeReady(std::uint64_t clipAssetId, const ResolvedAudioClip& clip) const;
    void ForgetValidation(std::uint64_t clipAssetId) const noexcept;
    void StoreValidation(std::uint64_t clipAssetId, std::string identity, bool valid) const;

    mutable std::unordered_map<std::uint64_t, ValidationCacheEntry> validatedContent_;
    mutable std::uint64_t validationUseSerial_ = 0U;
#if defined(KB_AUDIO_MINIAUDIO_TESTING)
    mutable ValidationStatsForTesting validationStats_{};
#endif
};

} // namespace kb::audio_miniaudio
