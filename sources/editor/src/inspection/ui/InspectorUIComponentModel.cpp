#include "inspection/ui/InspectorUIComponentModel.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/ui/UIComponentValidation.hpp"

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
    if (name == "imageAssetId") return "Image";
    if (name == "fontAssetId") return "Font";
    if (name == "spriteAssetId") return "Sprite";
    if (name == "content") return "Text";
    if (name == "sizeDelta.x") return "Width / Size Delta X";
    if (name == "sizeDelta.y") return "Height / Size Delta Y";
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

[[nodiscard]] std::vector<std::string_view> Choices(kb::scene::UIComponentType component, std::string_view name) {
    using enum kb::scene::UIComponentType;
    if (name == "horizontalAlignment")
        return component == Text ? std::vector<std::string_view>{"Left", "Center", "Right"}
                                 : std::vector<std::string_view>{"Left", "Center", "Right", "Stretch"};
    if (name == "verticalAlignment")
        return component == Text ? std::vector<std::string_view>{"Top", "Center", "Bottom"}
                                 : std::vector<std::string_view>{"Top", "Center", "Bottom", "Stretch"};
    if (component == Text && name == "wrapMode") return {"No Wrap", "Word", "Character"};
    if (component == Image && name == "scaleMode") return {"Stretch", "Fit Inside", "Fill and Crop"};
    if (component == CanvasScaler && name == "scaleMode") return {"Constant Pixel Size", "Scale with Screen Size"};
    if (component == ContentSizeFitter && (name == "horizontalFit" || name == "verticalFit"))
        return {"Unconstrained", "Minimum Size", "Preferred Size"};
    if (component == AspectRatioFitter && name == "mode")
        return {"None", "Width Controls Height", "Height Controls Width", "Fit Inside Parent", "Fill Parent"};
    if (component == Selectable && name == "navigationMode") return {"None", "Automatic", "Explicit"};
    if ((component == Slider || component == Scrollbar) && name == "direction")
        return {"Left to Right", "Right to Left", "Bottom to Top", "Top to Bottom"};
    return {};
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

std::array<InspectorUIPropertyRow, 4> InspectorUIComponentModel::RectLayoutFields(
    const kb::scene::UIRectTransform& rect) {
    const bool stretchX = rect.anchorMin.x != rect.anchorMax.x;
    const bool stretchY = rect.anchorMin.y != rect.anchorMax.y;
    const float width = rect.offsetMax.x - rect.offsetMin.x;
    const float height = rect.offsetMax.y - rect.offsetMin.y;
    std::array<InspectorUIPropertyRow, 4> fields;
    fields[0].label = stretchX ? "Left" : "Position X";
    fields[1].label = stretchY ? "Top" : "Position Y";
    fields[2].label = stretchX ? "Right" : "Width";
    fields[3].label = stretchY ? "Bottom" : "Height";
    fields[0].value = Format(stretchX ? rect.offsetMin.x : rect.offsetMin.x + width * rect.pivot.x);
    fields[1].value = Format(stretchY ? rect.offsetMin.y : rect.offsetMin.y + height * rect.pivot.y);
    fields[2].value = Format(stretchX ? -rect.offsetMax.x : width);
    fields[3].value = Format(stretchY ? -rect.offsetMax.y : height);
    return fields;
}

bool InspectorUIComponentModel::EditRectLayout(kb::scene::UIRectTransform& rect, int field, float value) noexcept {
    if (field < 0 || field > 3 || !std::isfinite(value)) return false;
    auto candidate = rect;
    const bool horizontal = field % 2 == 0;
    float& low = horizontal ? candidate.offsetMin.x : candidate.offsetMin.y;
    float& high = horizontal ? candidate.offsetMax.x : candidate.offsetMax.y;
    const float pivot = horizontal ? candidate.pivot.x : candidate.pivot.y;
    const bool stretch = horizontal ? candidate.anchorMin.x != candidate.anchorMax.x
                                    : candidate.anchorMin.y != candidate.anchorMax.y;
    if (stretch) {
        if (field < 2) low = value;
        else high = -value;
    } else if (field < 2) {
        const float delta = value - (low + (high - low) * pivot);
        low += delta;
        high += delta;
    } else {
        if (value < 0.0F) return false;
        const float delta = value - (high - low);
        low -= delta * pivot;
        high += delta * (1.0F - pivot);
    }
    if (!kb::scene::IsUIComponentValid(candidate)) return false;
    rect = candidate;
    return true;
}

int InspectorUIComponentModel::AnchorPreset(const kb::scene::UIRectTransform& rect) noexcept {
    const auto axis = [](float low, float high) {
        if (low == 0.0F && high == 1.0F) return 3;
        for (int index = 0; index < 3; ++index) {
            if (low == static_cast<float>(index) * 0.5F && high == low) return index;
        }
        return -1;
    };
    const int x = axis(rect.anchorMin.x, rect.anchorMax.x);
    const int y = axis(rect.anchorMin.y, rect.anchorMax.y);
    return x < 0 || y < 0 ? -1 : y * 4 + x;
}

bool InspectorUIComponentModel::ApplyAnchorPreset(kb::scene::UIRectTransform& rect,
    int preset, kb::math::Vec2 parentSize, bool alignPosition, bool alignPivot) noexcept {
    if (preset < 0 || preset >= 16 || !std::isfinite(parentSize.x) || !std::isfinite(parentSize.y) ||
        parentSize.x < 0.0F || parentSize.y < 0.0F) return false;
    auto candidate = rect;
    const auto axis = [alignPosition, alignPivot](int mode, float extent,
                          float& anchorLow, float& anchorHigh, float& low, float& high, float& pivot) {
        const float nextLow = mode == 3 ? 0.0F : static_cast<float>(mode) * 0.5F;
        const float nextHigh = mode == 3 ? 1.0F : nextLow;
        const float size = (anchorHigh - anchorLow) * extent + high - low;
        if (alignPivot) pivot = mode == 3 ? 0.5F : nextLow;
        if (alignPosition) {
            low = mode == 3 ? 0.0F : -size * pivot;
            high = mode == 3 ? 0.0F : size * (1.0F - pivot);
        } else {
            low += (anchorLow - nextLow) * extent;
            high += (anchorHigh - nextHigh) * extent;
        }
        anchorLow = nextLow;
        anchorHigh = nextHigh;
    };
    axis(preset % 4, parentSize.x, candidate.anchorMin.x, candidate.anchorMax.x,
        candidate.offsetMin.x, candidate.offsetMax.x, candidate.pivot.x);
    axis(preset / 4, parentSize.y, candidate.anchorMin.y, candidate.anchorMax.y,
        candidate.offsetMin.y, candidate.offsetMax.y, candidate.pivot.y);
    if (!kb::scene::IsUIComponentValid(candidate)) return false;
    rect = candidate;
    return true;
}

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
            .choices = Choices(component, property.name),
        });
    }
    // Keep canonical indices for editing and automation; only the leading field
    // occupies a visual row. Group complete typed tuples, never unrelated fields.
    for (std::size_t index = 0; index < output.size(); ++index) {
        auto& row = output[index];
        row.groupStart = static_cast<int>(index);
        if (component == kb::scene::UIComponentType::RectTransform) continue;
        const auto dot = row.name.rfind('.');
        if (dot == std::string_view::npos) continue;
        const auto prefix = row.name.substr(0, dot);
        const auto suffix = row.name.substr(dot);
        const bool color = suffix == ".r";
        const bool vec4 = suffix == ".x" && index + 3 < output.size() &&
            output[index + 2].name == std::string{prefix} + ".z" && output[index + 3].name == std::string{prefix} + ".w";
        const bool rectangle = suffix == ".x" && index + 3 < output.size() &&
            output[index + 2].name == std::string{prefix} + ".width" && output[index + 3].name == std::string{prefix} + ".height";
        const std::array<std::string_view, 4> channels = color
            ? std::array<std::string_view, 4>{".r", ".g", ".b", ".a"}
            : suffix == ".left" ? std::array<std::string_view, 4>{".left", ".top", ".right", ".bottom"}
            : vec4 ? std::array<std::string_view, 4>{".x", ".y", ".z", ".w"}
            : rectangle ? std::array<std::string_view, 4>{".x", ".y", ".width", ".height"}
            : std::array<std::string_view, 4>{".x", ".y", "", ""};
        const int count = color || suffix == ".left" || vec4 || rectangle ? 4 : suffix == ".x" ? 2 : 0;
        if (count == 0 || index + count > output.size()) continue;
        bool complete = true;
        for (int lane = 0; lane < count; ++lane) {
            const auto& field = output[index + lane];
            complete &= field.name == std::string{prefix} + std::string{channels[lane]} &&
                field.type == kb::scene::UIComponentPropertyType::Float && field.writable == row.writable;
        }
        if (!complete) continue;
        row.fieldCount = count;
        row.color = color;
        row.label = Label(prefix);
        for (int lane = 0; lane < count; ++lane) {
            auto& field = output[index + lane];
            field.groupStart = static_cast<int>(index);
            if (lane != 0) field.fieldCount = 0;
            if (color) {
                kb::scene::UIComponentPropertyValue channel;
                if (kb::scene::ReadUIComponentProperty(values, component, field.name, channel))
                    row.rgba[lane] = std::get<float>(channel);
            }
        }
        index += count - 1;
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
