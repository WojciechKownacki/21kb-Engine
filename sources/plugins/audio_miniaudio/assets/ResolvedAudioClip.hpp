#pragma once

#include "engine/assets/ImportedAsset.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace kb::audio_miniaudio {

struct ResolvedAudioClip final {
    std::filesystem::path path;
    std::shared_ptr<const kb::assets::ImportedAsset> imported;
    std::string extension;
    std::string identity;

    [[nodiscard]] bool IsMemoryBacked() const noexcept {
        return imported != nullptr;
    }

    [[nodiscard]] std::span<const std::byte> EncodedBytes() const noexcept {
        return imported == nullptr
            ? std::span<const std::byte>{}
            : std::span<const std::byte>{ imported->payload.data(), imported->payload.size() };
    }

    [[nodiscard]] bool IsValid() const noexcept {
        return !identity.empty() && !extension.empty()
            && (IsMemoryBacked() ? path.empty() && !imported->payload.empty() : !path.empty());
    }
};

} // namespace kb::audio_miniaudio
