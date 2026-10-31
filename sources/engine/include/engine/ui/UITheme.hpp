#pragma once

#include "engine/ui/UIComponentPropertyCatalog.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

// Keys use the stable component type and the property names in UIComponentPropertyCatalog.
struct UIThemePropertyKey {
    UIComponentType component{};
    std::string property;
    [[nodiscard]] bool operator==(const UIThemePropertyKey&) const = default;
};

struct UIThemeProperty {
    UIThemePropertyKey key;
    UIComponentPropertyValue value;
};

struct UIThemeVariant {
    std::string name;
    std::vector<UIThemeProperty> properties;
};

inline constexpr std::string_view kUIThemeAssetType = "UITheme";
inline constexpr std::string_view kUIThemeAssetExtension = ".kbtheme";

struct UITheme {
    std::vector<UIThemeProperty> properties;
    std::vector<UIThemeVariant> variants;
};

enum class UIThemeResolveResult {
    Succeeded,
    InvalidTheme,
    UnknownVariant,
    InvalidOverride,
    InvalidValue,
};

// Theme properties are limited to visual fields. The descriptor also exposes typed asset
// references so a persistent theme can enumerate the same dependencies as UI components.
[[nodiscard]] bool IsUIThemeProperty(const UIThemePropertyKey& key) noexcept;

[[nodiscard]] bool ValidateUITheme(const UITheme& theme);
// Parse semicolon-separated stable-component-id:property keys; output is unchanged on failure.
[[nodiscard]] bool ParseUIThemeOverrides(std::string_view text, std::vector<UIThemePropertyKey>& output);

// Resolve base, then the named variant. A local override key preserves the authored value.
// Missing components are ignored. On error output is unchanged. Empty variant means base.
[[nodiscard]] UIThemeResolveResult ResolveUITheme(const UITheme& theme, std::string_view variant,
    std::span<const UIThemePropertyKey> localOverrides, const UIComponentSet& authored, UIComponentSet& output);

} // namespace kb::scene
