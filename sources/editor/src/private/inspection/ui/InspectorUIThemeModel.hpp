#pragma once

#include "engine/ui/UITheme.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <optional>

namespace kb::editor {

struct InspectorUIThemeRow {
    kb::scene::UIThemePropertyKey key;
    kb::scene::UIComponentPropertyType type{};
    std::string label;
    std::string value;
    bool authored = false;
    std::optional<std::array<float, 4>> color;
};

// Rows cover all visual properties, including inherited values. The token @inherit
// removes a property; empty string remains an editable string value.
class InspectorUIThemeModel final {
public:
    [[nodiscard]] static std::vector<InspectorUIThemeRow> Rows(const kb::scene::UITheme& theme,
        std::string_view variant);
    [[nodiscard]] static std::string_view EffectiveVariant(const kb::scene::UITheme& theme,
        std::uint64_t asset, std::uint64_t selectedAsset, std::string_view selectedVariant) noexcept;
    static void NormalizeSelection(const kb::scene::UITheme& theme, std::uint64_t asset,
        std::uint64_t& selectedAsset, std::string& selectedVariant);
    [[nodiscard]] static bool Edit(kb::scene::UITheme& theme, std::string_view variant,
        const kb::scene::UIThemePropertyKey& key, std::string_view value);
    [[nodiscard]] static bool EditColor(kb::scene::UITheme& theme, std::string_view variant,
        const kb::scene::UIThemePropertyKey& key, const std::array<float, 4>& value);
};

} // namespace kb::editor
