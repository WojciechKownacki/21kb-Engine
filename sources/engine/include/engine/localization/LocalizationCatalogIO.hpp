#pragma once

#include "engine/localization/LocalizationCatalog.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace kb::localization {

class LocalizationCatalogIO final {
public:
    LocalizationCatalogIO() = delete;
    [[nodiscard]] static std::optional<LocalizationCatalog> Load(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<LocalizationCatalog> Load(std::span<const std::uint8_t> bytes);
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const LocalizationCatalog& catalog);
};

} // namespace kb::localization
