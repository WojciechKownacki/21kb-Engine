#pragma once

#include "engine/assets/AssetImportTypes.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kb::assets {

class AssetImportCatalog {
public:
    AssetImportCatalog() = delete;

    [[nodiscard]] static AssetImportCategory ClassifyExtension(const std::filesystem::path& extension);
    [[nodiscard]] static bool IsMetaExtension(const std::filesystem::path& extension);
    [[nodiscard]] static bool IsEngineAssetExtension(const std::filesystem::path& extension);
    [[nodiscard]] static std::vector<std::string> SupportedSourceExtensions();
    [[nodiscard]] static std::string WindowsFileDialogFilter();
};

} // namespace kb::assets
