#include "inspection/ui/InspectorUIComponentModel.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"

#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <utility>

namespace kb::editor {
namespace {

constexpr std::uint8_t kComponentCount =
    static_cast<std::uint8_t>(kb::scene::UIComponentType::WidgetSwitcher) + 1U;

static_assert(static_cast<std::uint8_t>(InspectorSectionId::UIWidgetSwitcher) -
        static_cast<std::uint8_t>(InspectorSectionId::UIRectTransform) + 1U ==
    kComponentCount);
static_assert(static_cast<std::uint16_t>(InspectorPropertyId::UIWidgetSwitcherField) -
        static_cast<std::uint16_t>(InspectorPropertyId::UIRectTransformField) + 1U ==
    kComponentCount);

[[nodiscard]] std::string Label(std::string_view name) {
    std::string output;
    output.reserve(name.size() + 4U);
    for (std::size_t index = 0U; index < name.size(); ++index) {
        const char character = name[index];
        if (character == '.' || character == '_' || character == '-') {
            if (!output.empty() && output.back() != ' ') output.push_back(' ');
            continue;
        }
        const bool uppercaseBoundary = index > 0U && character >= 'A' && character <= 'Z' &&
            name[index - 1U] >= 'a' && name[index - 1U] <= 'z';
        if (uppercaseBoundary) output.push_back(' ');
        output.push_back(output.empty()
                ? static_cast<char>(std::toupper(static_cast<unsigned char>(character)))
                : character);
    }
    return output;
}

[[nodiscard]] std::string Format(const kb::scene::UIComponentPropertyValue& value) {
    return std::visit([](const auto& typed) -> std::string {
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, bool>) {
            return typed ? "true" : "false";
        } else if constexpr (std::is_same_v<T, float>) {
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "%.7g", static_cast<double>(typed));
            return buffer;
        } else if constexpr (std::is_same_v<T, std::string>) {
            return typed;
        } else {
            return std::to_string(typed);
        }
    }, value);
}

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1U);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1U);
    return text;
}

template <typename T>
[[nodiscard]] std::optional<T> ParseInteger(std::string_view text) noexcept {
    text = Trim(text);
    if (text.empty()) return std::nullopt;
    T value{};
    const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size()
        ? std::optional<T>{ value }
        : std::nullopt;
}

} // namespace

std::vector<kb::scene::UIComponentType> InspectorUIComponentModel::Components(
    const kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    const kb::scene::UIComponentSet values =
        kb::scene::CaptureSceneUIComponents(scene.Components().UI(), entity);
    std::vector<kb::scene::UIComponentType> output;
    for (const kb::scene::UIComponentDescriptor& descriptor : kb::scene::UIComponentCatalog()) {
        if (kb::scene::HasUIComponent(values, descriptor.type)) output.push_back(descriptor.type);
    }
    return output;
}

std::vector<InspectorUIPropertyRow> InspectorUIComponentModel::Properties(
    const kb::scene::Scene& scene, kb::scene::SceneEntity entity,
    kb::scene::UIComponentType component) {
    const kb::scene::UIComponentSet values =
        kb::scene::CaptureSceneUIComponents(scene.Components().UI(), entity);
    if (!kb::scene::HasUIComponent(values, component)) return {};

    std::vector<InspectorUIPropertyRow> output;
    for (const kb::scene::UIComponentPropertyDescriptor& property :
        kb::scene::UIComponentPropertyCatalog(component)) {
        kb::scene::UIComponentPropertyValue value;
        if (!kb::scene::ReadUIComponentProperty(values, component, property.name, value)) continue;
        output.push_back(InspectorUIPropertyRow{
            .name = property.name,
            .label = Label(property.name),
            .type = property.type,
            .value = Format(value),
            .boolValue = std::holds_alternative<bool>(value) && std::get<bool>(value),
            .writable = property.writable,
        });
    }
    return output;
}

InspectorSectionId InspectorUIComponentModel::Section(kb::scene::UIComponentType component) noexcept {
    return static_cast<InspectorSectionId>(
        static_cast<std::uint8_t>(InspectorSectionId::UIRectTransform) +
        static_cast<std::uint8_t>(component));
}

InspectorPropertyId InspectorUIComponentModel::Property(kb::scene::UIComponentType component) noexcept {
    return static_cast<InspectorPropertyId>(
        static_cast<std::uint16_t>(InspectorPropertyId::UIRectTransformField) +
        static_cast<std::uint16_t>(component));
}

std::optional<kb::scene::UIComponentType> InspectorUIComponentModel::Component(
    InspectorSectionId section) noexcept {
    const auto value = static_cast<std::uint8_t>(section);
    const auto first = static_cast<std::uint8_t>(InspectorSectionId::UIRectTransform);
    return value >= first && value < first + kComponentCount
        ? std::optional<kb::scene::UIComponentType>{
              static_cast<kb::scene::UIComponentType>(value - first) }
        : std::nullopt;
}

std::optional<kb::scene::UIComponentType> InspectorUIComponentModel::Component(
    InspectorPropertyId property) noexcept {
    const auto value = static_cast<std::uint16_t>(property);
    const auto first = static_cast<std::uint16_t>(InspectorPropertyId::UIRectTransformField);
    return value >= first && value < first + kComponentCount
        ? std::optional<kb::scene::UIComponentType>{
              static_cast<kb::scene::UIComponentType>(value - first) }
        : std::nullopt;
}

std::optional<kb::scene::UIComponentPropertyValue> InspectorUIComponentModel::Parse(
    kb::scene::UIComponentPropertyType type, std::string_view text) {
    switch (type) {
    case kb::scene::UIComponentPropertyType::Bool:
        text = Trim(text);
        if (text == "true" || text == "1") return kb::scene::UIComponentPropertyValue{ true };
        if (text == "false" || text == "0") return kb::scene::UIComponentPropertyValue{ false };
        return std::nullopt;
    case kb::scene::UIComponentPropertyType::Int:
        if (const auto value = ParseInteger<std::int32_t>(text))
            return kb::scene::UIComponentPropertyValue{ *value };
        return std::nullopt;
    case kb::scene::UIComponentPropertyType::UInt32:
        if (const auto value = ParseInteger<std::uint32_t>(text))
            return kb::scene::UIComponentPropertyValue{ *value };
        return std::nullopt;
    case kb::scene::UIComponentPropertyType::Float: {
        text = Trim(text);
        float value = 0.0F;
        const std::from_chars_result result = std::from_chars(
            text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size() &&
                std::isfinite(value)
            ? std::optional<kb::scene::UIComponentPropertyValue>{ value }
            : std::nullopt;
    }
    case kb::scene::UIComponentPropertyType::String:
        return kb::scene::UIComponentPropertyValue{ std::string{ text } };
    case kb::scene::UIComponentPropertyType::Entity:
    case kb::scene::UIComponentPropertyType::Asset:
        if (const auto value = ParseInteger<std::uint64_t>(text))
            return kb::scene::UIComponentPropertyValue{ *value };
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace kb::editor
