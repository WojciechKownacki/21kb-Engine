#pragma once

#include "engine/assets/ImportedAsset.hpp"
#include "engine/audio/AudioClipAsset.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace kb::audio_miniaudio {

struct ResolvedAudioClip final {
    std::filesystem::path path;
    std::shared_ptr<const kb::audio::AudioClipAsset> audioClip;
    std::shared_ptr<const kb::assets::ImportedAsset> imported;
    std::string extension;
    std::string identity;
    std::uint64_t expectedEncodedHash = 0U;

    [[nodiscard]] bool IsMemoryBacked() const noexcept {
        return audioClip != nullptr || imported != nullptr;
    }

    [[nodiscard]] std::span<const std::byte> EncodedBytes() const noexcept {
        if (audioClip != nullptr) {
            return std::as_bytes(std::span<const std::uint8_t>{ audioClip->encodedBytes });
        }
        return imported != nullptr
            ? std::span<const std::byte>{ imported->payload.data(), imported->payload.size() }
            : std::span<const std::byte>{};
    }

    [[nodiscard]] std::shared_ptr<const void> EncodedOwner() const noexcept {
        if (audioClip != nullptr) {
            return audioClip;
        }
        return imported;
    }

    [[nodiscard]] bool IsValid() const noexcept {
        return !identity.empty() && !extension.empty()
            && (IsMemoryBacked() ? path.empty() && !EncodedBytes().empty() : !path.empty());
    }
};

} // namespace kb::audio_miniaudio
