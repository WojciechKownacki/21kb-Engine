#include "inspection/ui/InspectorUIThemeModel.hpp"

#include "inspection/ui/InspectorUIComponentModel.hpp"
#include "scene/ui/EditorUIThemeAssets.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <type_traits>

namespace kb::editor {
namespace {
std::string Format(const kb::scene::UIComponentPropertyValue& value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, bool>) output << (item ? "true" : "false");
        else output << item;
    }, value);
    return output.str();
}
} // namespace

std::vector<InspectorUIThemeRow> InspectorUIThemeModel::Rows(const kb::scene::UITheme& theme,
    std::string_view variant) {
    using namespace kb::scene;
    const UIThemeVariant* selected = nullptr;
    if (!variant.empty()) {
        const auto found = std::ranges::find(theme.variants, variant, &UIThemeVariant::name);
        if (found == theme.variants.end()) return {};
        selected = &*found;
    }
    std::vector<InspectorUIThemeRow> rows;
    for (const auto& component : UIComponentCatalog()) {
        UIComponentSet defaults;
        if (!AddUIComponent(defaults, component.type)) continue;
        for (const auto& descriptor : UIComponentPropertyCatalog(component.type)) {
            UIThemePropertyKey key{component.type, std::string{descriptor.name}};
            if (!IsUIThemeProperty(key)) continue;
            UIComponentPropertyValue value;
            if (!ReadUIComponentProperty(defaults, component.type, descriptor.name, value)) continue;
            const auto base = std::ranges::find(theme.properties, key, &UIThemeProperty::key);
            if (base != theme.properties.end()) value = base->value;
            bool authored = !selected && base != theme.properties.end();
            if (selected) {
                const auto override = std::ranges::find(selected->properties, key, &UIThemeProperty::key);
                authored = override != selected->properties.end();
                if (authored) value = override->value;
            }
            rows.push_back({std::move(key), descriptor.type,
                std::string{component.displayName} + " / " + std::string{descriptor.name}, Format(value), authored, std::nullopt});
        }
    }
    for (auto& row : rows) {
        if (!row.key.property.ends_with(".r")) continue;
        std::array<float, 4> color{};
        bool valid = true;
        for (std::size_t channel = 0; channel < color.size(); ++channel) {
            auto key = row.key;
            key.property.back() = "rgba"[channel];
            const auto found = std::ranges::find(rows, key, &InspectorUIThemeRow::key);
            const auto value = found != rows.end() ? InspectorUIComponentModel::Parse(
                kb::scene::UIComponentPropertyType::Float, found->value) : std::nullopt;
            if (!value || !std::holds_alternative<float>(*value)) { valid = false; break; }
            color[channel] = std::get<float>(*value);
        }
        if (valid) row.color = color;
    }
    return rows;
}

std::string_view InspectorUIThemeModel::EffectiveVariant(const kb::scene::UITheme& theme,
    std::uint64_t asset, std::uint64_t selectedAsset, std::string_view selectedVariant) noexcept {
    if (asset != selectedAsset || selectedVariant.empty()) return {};
    return std::ranges::any_of(theme.variants, [&](const auto& variant) { return variant.name == selectedVariant; })
        ? selectedVariant : std::string_view{};
}
void InspectorUIThemeModel::NormalizeSelection(const kb::scene::UITheme& theme, std::uint64_t asset,
    std::uint64_t& selectedAsset, std::string& selectedVariant) {
    if (EffectiveVariant(theme, asset, selectedAsset, selectedVariant).empty()) selectedVariant.clear();
    selectedAsset = asset;
}

bool InspectorUIThemeModel::Edit(kb::scene::UITheme& theme, std::string_view variant,
    const kb::scene::UIThemePropertyKey& key, std::string_view text) {
    const auto* descriptor = kb::scene::FindUIComponentProperty(key.component, key.property);
    if (!descriptor) return false;
    if (text == "@inherit" || (text.empty() && descriptor->type != kb::scene::UIComponentPropertyType::String))
        return EditorUIThemeAssets::RemoveProperty(theme, variant, key);
    const auto value = InspectorUIComponentModel::Parse(descriptor->type, text);
    return value && EditorUIThemeAssets::SetProperty(theme, variant, {key, *value});
}

bool InspectorUIThemeModel::EditColor(kb::scene::UITheme& theme, std::string_view variant,
    const kb::scene::UIThemePropertyKey& key, const std::array<float, 4>& value) {
    if (!key.property.ends_with(".r")) return false;
    auto candidate = theme;
    for (std::size_t channel = 0; channel < value.size(); ++channel) {
        auto channelKey = key;
        channelKey.property.back() = "rgba"[channel];
        if (!EditorUIThemeAssets::SetProperty(candidate, variant, {channelKey, value[channel]})) return false;
    }
    theme = std::move(candidate);
    return true;
}

} // namespace kb::editor
