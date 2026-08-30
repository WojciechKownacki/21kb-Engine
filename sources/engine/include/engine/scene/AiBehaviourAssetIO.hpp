#pragma once

#include "engine/scene/AiBehaviourAsset.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace kb::scene {

inline constexpr const char* kAiBehaviourAssetExtension = ".kbai";
inline constexpr const char* kAiBehaviourAssetType = "AiBehaviour";

class AiBehaviourAssetIO final {
public:
    AiBehaviourAssetIO() = delete;

    [[nodiscard]] static std::optional<AiBehaviourAsset> Load(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<AiBehaviourAsset> Load(std::span<const std::uint8_t> bytes);
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const AiBehaviourAsset& asset);
};

} // namespace kb::scene
