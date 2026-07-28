#pragma once

#include "engine/scene/UIAssets.hpp"

#include <filesystem>
#include <optional>

namespace kb::scene {

inline constexpr const char* kUIDocumentAssetExtension = ".kbui";
inline constexpr const char* kUIDocumentAssetType = "UIDocument";
inline constexpr const char* kUIStyleAssetExtension = ".kbuistyle";
inline constexpr const char* kUIStyleAssetType = "UIStyle";

class UIAssetIO final {
public:
    UIAssetIO() = delete;

    [[nodiscard]] static std::optional<UIDocument> LoadDocument(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<UIStyleAsset> LoadStyle(const std::filesystem::path& path);
    [[nodiscard]] static bool SaveDocument(const std::filesystem::path& path, const UIDocument& document);
    [[nodiscard]] static bool SaveStyle(const std::filesystem::path& path, const UIStyleAsset& style);
};

} // namespace kb::scene
