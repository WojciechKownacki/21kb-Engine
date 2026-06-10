#pragma once

#include "engine/assets/AssetId.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kb::assets {

enum class AssetImportCategory : std::uint16_t {
    Unknown = 0,
    Model,
    Texture,
    Audio,
    Video,
    Animation,
    Material,
    Shader,
    Font,
    Script,
    Scene,
    Data,
    InputAction,
    InputMappingContext,
};

struct AssetImportItemResult {
    std::filesystem::path sourcePath;
    std::filesystem::path assetPhysicalPath;
    std::filesystem::path metaPhysicalPath;
    std::filesystem::path virtualPath;
    AssetId id{};
    AssetImportCategory category = AssetImportCategory::Unknown;
    std::uint64_t sourceHash = 0;
    std::uint64_t assetHash = 0;
    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept {
        return error.empty() && id.IsValid() && !assetPhysicalPath.empty() && !metaPhysicalPath.empty();
    }
};

struct AssetImportResult {
    std::vector<AssetImportItemResult> items;

    [[nodiscard]] std::size_t ImportedCount() const noexcept;
    [[nodiscard]] std::size_t FailedCount() const noexcept;
    [[nodiscard]] bool Succeeded() const noexcept;
};

[[nodiscard]] std::string_view ToString(AssetImportCategory category) noexcept;
[[nodiscard]] std::string_view RuntimeAssetType(AssetImportCategory category) noexcept;

} // namespace kb::assets
