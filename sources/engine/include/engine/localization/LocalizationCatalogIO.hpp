#pragma once

#include "engine/localization/LocalizationCatalog.hpp"

#include <filesystem>
#include <optional>

namespace kb::localization {

class LocalizationCatalogIO final {
public:
    LocalizationCatalogIO() = delete;
    [[nodiscard]] static std::optional<LocalizationCatalog> Load(const std::filesystem::path& path);
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const LocalizationCatalog& catalog);
};

} // namespace kb::localization
