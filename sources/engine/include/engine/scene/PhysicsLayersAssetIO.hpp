#pragma once

#include "engine/scene/PhysicsLayersAsset.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

// Binary format constants for the physics layers asset. Magic / version are
// part of the on-disk format and must stay stable - mirrors
// kb::input::InputAssetFormat's shape exactly.
struct PhysicsLayersAssetFormat {
    static constexpr std::array<std::uint8_t, 8U> Magic{ '2', '1', 'K', 'B', 'P', 'H', 'Y', 'L' };
    static constexpr std::uint32_t BinaryVersion = 1U;
    static constexpr std::uint32_t MaxNameBytes = 1U << 16U;
    static constexpr std::string_view Extension = ".21kbphysicslayers";
};

struct PhysicsLayersAssetLoadResult {
    bool succeeded = false;
    PhysicsLayersAsset asset{};
    std::string error;
};

[[nodiscard]] std::vector<std::uint8_t> EncodePhysicsLayersAsset(const PhysicsLayersAsset& asset);
[[nodiscard]] PhysicsLayersAssetLoadResult DecodePhysicsLayersAsset(std::span<const std::uint8_t> bytes);
[[nodiscard]] PhysicsLayersAssetLoadResult ReadPhysicsLayersAsset(const std::filesystem::path& path);
[[nodiscard]] bool WritePhysicsLayersAsset(const std::filesystem::path& path, const PhysicsLayersAsset& asset);

} // namespace kb::scene
