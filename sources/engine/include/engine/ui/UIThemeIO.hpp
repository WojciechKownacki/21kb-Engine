#pragma once

#include "engine/ui/UITheme.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace kb::scene {

class UIThemeIO final {
public:
    UIThemeIO() = delete;
    [[nodiscard]] static std::optional<UITheme> Load(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<UITheme> Load(std::span<const std::uint8_t> bytes);
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const UITheme& theme);
};

} // namespace kb::scene
