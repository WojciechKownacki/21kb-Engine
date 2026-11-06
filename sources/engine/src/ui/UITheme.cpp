#include "engine/ui/UITheme.hpp"

#include <algorithm>
#include <utility>

namespace kb::scene {
namespace {

bool Starts(std::string_view value, std::string_view prefix) noexcept {
    return value.starts_with(prefix);
}

bool Allowed(const UIThemePropertyKey& key) noexcept {
    const std::string_view name = key.property;
    switch (key.component) {
    case UIComponentType::Text:
        return name == "fontAssetId" || name == "fallbackFontAssetId" || name == "fontVariations" ||
            name == "fontRenderMode" || name == "fontSize" || name == "minFontSize" || name == "lineSpacing" || name == "characterSpacing" ||
            name == "horizontalAlignment" || name == "verticalAlignment" || name == "wrapMode" ||
            name == "overflow" || name == "maxLines" || name == "autoSize" || Starts(name, "color.");
    case UIComponentType::Sprite:
        return name == "spriteAssetId" || name == "preserveAspect" || Starts(name, "color.");
    case UIComponentType::Image:
        return name == "imageAssetId" || name == "materialAssetId" || name == "scaleMode" ||
            name == "preserveAspect" || Starts(name, "uvRect.") || Starts(name, "nineSlice.") || Starts(name, "color.");
    case UIComponentType::RawImage:
        return name == "imageAssetId" || Starts(name, "uvRect.") || Starts(name, "color.");
    case UIComponentType::Border:
    case UIComponentType::Shadow:
    case UIComponentType::Outline:
    case UIComponentType::BackgroundBlur:
        return true;
    case UIComponentType::Selectable:
        return Starts(name, "normalColor.") || Starts(name, "highlightedColor.") || Starts(name, "pressedColor.") ||
            Starts(name, "selectedColor.") || Starts(name, "disabledColor.") || name == "colorFadeSeconds" ||
            name == "transition" || name == "highlightedImage" || name == "pressedImage" ||
            name == "selectedImage" || name == "disabledImage";
    default:
        return false;
    }
}

bool ValidProperties(std::span<const UIThemeProperty> properties) {
    for (std::size_t i = 0; i < properties.size(); ++i) {
        if (!IsUIThemeProperty(properties[i].key)) return false;
        UIComponentSet sample;
        if (!AddUIComponent(sample, properties[i].key.component) ||
            WriteUIComponentProperty(sample, properties[i].key.component, properties[i].key.property,
                                     properties[i].value) != UIComponentPropertyWriteResult::Succeeded) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (properties[i].key == properties[j].key) return false;
    }
    return true;
}

} // namespace

bool IsUIThemeProperty(const UIThemePropertyKey& key) noexcept {
    const auto* property = FindUIComponentProperty(key.component, key.property);
    return property && property->writable && property->type != UIComponentPropertyType::Entity && Allowed(key);
}

bool ParseUIThemeOverrides(std::string_view text, std::vector<UIThemePropertyKey>& output) {
    std::vector<UIThemePropertyKey> keys;
    while (!text.empty()) {
        const auto separator = text.find(';');
        const auto item = text.substr(0U, separator);
        const auto colon = item.find(':');
        if (colon == std::string_view::npos || colon == 0U || colon + 1U == item.size()) return false;
        const auto* descriptor = FindUIComponentDescriptor(item.substr(0U, colon));
        if (!descriptor || descriptor->stableId != item.substr(0U, colon)) return false;
        UIThemePropertyKey key{descriptor->type, std::string{item.substr(colon + 1U)}};
        if (!IsUIThemeProperty(key) || std::ranges::find(keys, key) != keys.end()) return false;
        keys.push_back(std::move(key));
        if (separator == std::string_view::npos) break;
        text.remove_prefix(separator + 1U);
        if (text.empty()) return false;
    }
    output = std::move(keys);
    return true;
}

bool ValidateUITheme(const UITheme& theme) {
    if (theme.properties.size() > 1024U || theme.variants.size() > 128U || !ValidProperties(theme.properties)) return false;
    for (std::size_t i = 0; i < theme.variants.size(); ++i) {
        const auto& item = theme.variants[i];
        if (item.name.empty() || item.name.size() > 128U || item.name.find('\0') != std::string::npos ||
            item.properties.size() > 1024U || !ValidProperties(item.properties)) return false;
        for (std::size_t j = 0; j < i; ++j) if (item.name == theme.variants[j].name) return false;
    }
    return true;
}

UIThemeResolveResult ResolveUITheme(const UITheme& theme, std::string_view variant,
    std::span<const UIThemePropertyKey> localOverrides, const UIComponentSet& authored, UIComponentSet& output) {
    if (!ValidateUITheme(theme)) return UIThemeResolveResult::InvalidTheme;
    const UIThemeVariant* selected = nullptr;
    for (const auto& item : theme.variants) if (item.name == variant) selected = &item;
    if (!variant.empty() && !selected) return UIThemeResolveResult::UnknownVariant;
    for (std::size_t i = 0; i < localOverrides.size(); ++i) {
        if (!IsUIThemeProperty(localOverrides[i])) return UIThemeResolveResult::InvalidOverride;
        for (std::size_t j = 0; j < i; ++j)
            if (localOverrides[i] == localOverrides[j]) return UIThemeResolveResult::InvalidOverride;
    }
    // Merge before writing, so a base value shadowed by a variant is never applied.
    std::vector<UIThemeProperty> merged = theme.properties;
    if (selected) for (const auto& property : selected->properties) {
        const auto found = std::ranges::find(merged, property.key, &UIThemeProperty::key);
        if (found == merged.end()) merged.push_back(property);
        else *found = property;
    }
    UIComponentSet resolved = authored;
    for (const auto& property : merged) {
        if (std::ranges::find(localOverrides, property.key) != localOverrides.end() ||
            !HasUIComponent(resolved, property.key.component)) continue;
        if (WriteUIComponentProperty(resolved, property.key.component, property.key.property, property.value) !=
            UIComponentPropertyWriteResult::Succeeded) return UIThemeResolveResult::InvalidValue;
    }
    output = std::move(resolved);
    return UIThemeResolveResult::Succeeded;
}

} // namespace kb::scene
