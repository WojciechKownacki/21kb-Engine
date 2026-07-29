#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kb::scene {

class Scene;

class SceneLocalizationService final {
public:
    [[nodiscard]] static bool SetCatalog(Scene& scene, std::uint64_t assetId);
    [[nodiscard]] static std::uint64_t Catalog(const Scene& scene) noexcept;
    [[nodiscard]] static bool SetLanguage(Scene& scene, std::string_view language);
    [[nodiscard]] static std::string Language(const Scene& scene);
    [[nodiscard]] static std::string FallbackLanguage(const Scene& scene);
    [[nodiscard]] static std::string Translate(const Scene& scene, std::string_view key);
    [[nodiscard]] static std::string FormatPlural(const Scene& scene, std::string_view key, std::int64_t count);
};

} // namespace kb::scene
