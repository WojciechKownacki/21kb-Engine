#pragma once

#include "engine/scene/BlendSpace2D.hpp"

#include <cstdint>
#include <filesystem>
#include <span>

namespace kb::scene {
class BlendSpace2DAssetIO final {
public:
    [[nodiscard]] static std::optional<BlendSpace2DAsset> Load(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] static std::optional<BlendSpace2DAsset> Load(std::span<const std::uint8_t> bytes, std::string& error);
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const BlendSpace2DAsset& asset, std::string& error);
};
} // namespace kb::scene
