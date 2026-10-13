#include "inspection/ui/InspectorUIComponentModel.hpp"
#include "localization/EditorLocalization.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/ui/UIComponentValidation.hpp"

#include <algorithm>
#include <charconv>
#include <initializer_list>
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

// The Inspector name of a UI property in the editor language, by the engine's property name. A
// property the engine adds without an entry here shows its name split into words.
[[nodiscard]] std::string Label(std::string_view name) {
    // "options.3.text" -> "Option 4", "options.3.icon" -> "Option 4 Icon": the list reads as numbered
    // choices rather than as indexed fields.
    if (name.starts_with("options.")) {
        const std::string_view rest = name.substr(8U);
        const std::size_t dot = rest.find('.');
        std::uint32_t index = 0U;
        if (dot != std::string_view::npos &&
            std::from_chars(rest.data(), rest.data() + dot, index).ec == std::errc{}) {
            const std::string number = std::to_string(index + 1U);
            return rest.substr(dot) == ".icon"
                ? EditorLocalization::Format("uiProperty.optionIcon", "Option {index} Icon", { { "index", number } })
                : EditorLocalization::Format("uiProperty.option", "Option {index}", { { "index", number } });
        }
    }
    if (name == "allowSwitchOff") return std::string{ EditorText("uiProperty.allowSwitchOff", "Allow Switch Off") };
    if (name == "alphaFadeSpeed") return std::string{ EditorText("uiProperty.alphaFadeSpeed", "Alpha Fade Speed") };
    if (name == "alphaHitThreshold") return std::string{ EditorText("uiProperty.alphaHitThreshold", "Alpha Hit Threshold") };
    if (name == "anchorMax.x") return std::string{ EditorText("uiProperty.anchorMax.x", "Anchor Max x") };
    if (name == "anchorMax.y") return std::string{ EditorText("uiProperty.anchorMax.y", "Anchor Max y") };
    if (name == "anchorMin.x") return std::string{ EditorText("uiProperty.anchorMin.x", "Anchor Min x") };
    if (name == "anchorMin.y") return std::string{ EditorText("uiProperty.anchorMin.y", "Anchor Min y") };
    if (name == "aspectRatio") return std::string{ EditorText("uiProperty.aspectRatio", "Aspect Ratio") };
    if (name == "autoSize") return std::string{ EditorText("uiProperty.autoSize", "Auto Size") };
    if (name == "backgroundColor") return std::string{ EditorText("uiProperty.backgroundColor", "Background Color") };
    if (name == "blocksRaycasts") return std::string{ EditorText("uiProperty.blocksRaycasts", "Blocks Raycasts") };
    if (name == "blur") return std::string{ EditorText("uiProperty.blur", "Blur") };
    if (name == "borderColor") return std::string{ EditorText("uiProperty.borderColor", "Border Color") };
    if (name == "borderWidth") return std::string{ EditorText("uiProperty.borderWidth", "Border Width") };
    if (name == "captionImage") return std::string{ EditorText("uiProperty.captionImage", "Caption Image") };
    if (name == "captionText") return std::string{ EditorText("uiProperty.captionText", "Caption Text") };
    if (name == "cellSize") return std::string{ EditorText("uiProperty.cellSize", "Cell Size") };
    if (name == "characterLimit") return std::string{ EditorText("uiProperty.characterLimit", "Character Limit") };
    if (name == "characterSpacing") return std::string{ EditorText("uiProperty.characterSpacing", "Character Spacing") };
    if (name == "color") return std::string{ EditorText("uiProperty.color", "Color") };
    if (name == "colorFadeSeconds") return std::string{ EditorText("uiProperty.colorFadeSeconds", "Color Fade Seconds") };
    if (name == "columns") return std::string{ EditorText("uiProperty.columns", "Columns") };
    if (name == "content") return std::string{ EditorText("uiProperty.content", "Text") };
    if (name == "contentType") return std::string{ EditorText("uiProperty.contentType", "Content Type") };
    if (name == "controlChildHeight") return std::string{ EditorText("uiProperty.controlChildHeight", "Control Child Height") };
    if (name == "controlChildWidth") return std::string{ EditorText("uiProperty.controlChildWidth", "Control Child Width") };
    if (name == "cornerRadius") return std::string{ EditorText("uiProperty.cornerRadius", "Corner Radius") };
    if (name == "direction") return std::string{ EditorText("uiProperty.direction", "Direction") };
    if (name == "disabledColor") return std::string{ EditorText("uiProperty.disabledColor", "Disabled Color") };
    if (name == "disabledImage") return std::string{ EditorText("uiProperty.disabledImage", "Disabled Image") };
    if (name == "draggable") return std::string{ EditorText("uiProperty.draggable", "Draggable") };
    if (name == "elasticity") return std::string{ EditorText("uiProperty.elasticity", "Elasticity") };
    if (name == "eventName") return std::string{ EditorText("uiProperty.eventName", "Event Name") };
    if (name == "eventTarget") return std::string{ EditorText("uiProperty.eventTarget", "Event Target") };
    if (name == "expandChildHeight") return std::string{ EditorText("uiProperty.expandChildHeight", "Expand Child Height") };
    if (name == "expandChildWidth") return std::string{ EditorText("uiProperty.expandChildWidth", "Expand Child Width") };
    if (name == "fillAmount") return std::string{ EditorText("uiProperty.fillAmount", "Fill Amount") };
    if (name == "fillClockwise") return std::string{ EditorText("uiProperty.fillClockwise", "Fill Clockwise") };
    if (name == "fillMethod") return std::string{ EditorText("uiProperty.fillMethod", "Fill Method") };
    if (name == "fillOrigin") return std::string{ EditorText("uiProperty.fillOrigin", "Fill Origin") };
    if (name == "fillRect") return std::string{ EditorText("uiProperty.fillRect", "Fill") };
    if (name == "flexibleHeight") return std::string{ EditorText("uiProperty.flexibleHeight", "Flexible Height") };
    if (name == "flexibleWidth") return std::string{ EditorText("uiProperty.flexibleWidth", "Flexible Width") };
    if (name == "fontAssetId") return std::string{ EditorText("uiProperty.fontAssetId", "Font") };
    if (name == "fontSize") return std::string{ EditorText("uiProperty.fontSize", "Font Size") };
    if (name == "graphic") return std::string{ EditorText("uiProperty.graphic", "Graphic") };
    if (name == "group") return std::string{ EditorText("uiProperty.group", "Radio Group") };
    if (name == "handleRect") return std::string{ EditorText("uiProperty.handleRect", "Handle") };
    if (name == "hiddenOffset") return std::string{ EditorText("uiProperty.hiddenOffset", "Hidden Offset") };
    if (name == "hiddenScale") return std::string{ EditorText("uiProperty.hiddenScale", "Hidden Scale") };
    if (name == "highlightedColor") return std::string{ EditorText("uiProperty.highlightedColor", "Highlighted Color") };
    if (name == "highlightedImage") return std::string{ EditorText("uiProperty.highlightedImage", "Highlighted Image") };
    if (name == "horizontal") return std::string{ EditorText("uiProperty.horizontal", "Horizontal") };
    if (name == "horizontalAlignment") return std::string{ EditorText("uiProperty.horizontalAlignment", "Horizontal Alignment") };
    if (name == "horizontalFit") return std::string{ EditorText("uiProperty.horizontalFit", "Horizontal Fit") };
    if (name == "horizontalScrollbar") return std::string{ EditorText("uiProperty.horizontalScrollbar", "Horizontal Scrollbar") };
    if (name == "ignoreLayout") return std::string{ EditorText("uiProperty.ignoreLayout", "Ignore Layout") };
    if (name == "ignoreParentGroups") return std::string{ EditorText("uiProperty.ignoreParentGroups", "Ignore Parent Groups") };
    if (name == "imageAssetId") return std::string{ EditorText("uiProperty.imageAssetId", "Image") };
    if (name == "inertia") return std::string{ EditorText("uiProperty.inertia", "Inertia") };
    if (name == "interactable") return std::string{ EditorText("uiProperty.interactable", "Interactable") };
    if (name == "itemImage") return std::string{ EditorText("uiProperty.itemImage", "Item Image") };
    if (name == "itemText") return std::string{ EditorText("uiProperty.itemText", "Item Text") };
    if (name == "layoutPriority") return std::string{ EditorText("uiProperty.layoutPriority", "Layout Priority") };
    if (name == "lineSpacing") return std::string{ EditorText("uiProperty.lineSpacing", "Line Spacing") };
    if (name == "localizationKey") return std::string{ EditorText("uiProperty.localizationKey", "Localization Key") };
    if (name == "materialAssetId") return std::string{ EditorText("uiProperty.materialAssetId", "Material") };
    if (name == "matchWidthOrHeight") return std::string{ EditorText("uiProperty.matchWidthOrHeight", "Match Width Or Height") };
    if (name == "maxLines") return std::string{ EditorText("uiProperty.maxLines", "Max Lines (0 = All)") };
    if (name == "maximum") return std::string{ EditorText("uiProperty.maximum", "Maximum") };
    if (name == "minFontSize") return std::string{ EditorText("uiProperty.minFontSize", "Min Font Size") };
    if (name == "minimum") return std::string{ EditorText("uiProperty.minimum", "Minimum") };
    if (name == "minimumHeight") return std::string{ EditorText("uiProperty.minimumHeight", "Minimum Height") };
    if (name == "minimumWidth") return std::string{ EditorText("uiProperty.minimumWidth", "Minimum Width") };
    if (name == "mode") return std::string{ EditorText("uiProperty.mode", "Mode") };
    if (name == "movementType") return std::string{ EditorText("uiProperty.movementType", "Movement Type") };
    if (name == "multiline") return std::string{ EditorText("uiProperty.multiline", "Multiline") };
    if (name == "navigationDown") return std::string{ EditorText("uiProperty.navigationDown", "Navigation Down") };
    if (name == "navigationLeft") return std::string{ EditorText("uiProperty.navigationLeft", "Navigation Left") };
    if (name == "navigationMode") return std::string{ EditorText("uiProperty.navigationMode", "Navigation Mode") };
    if (name == "navigationRight") return std::string{ EditorText("uiProperty.navigationRight", "Navigation Right") };
    if (name == "navigationUp") return std::string{ EditorText("uiProperty.navigationUp", "Navigation Up") };
    if (name == "nineSlice") return std::string{ EditorText("uiProperty.nineSlice", "Nine Slice") };
    if (name == "normalColor") return std::string{ EditorText("uiProperty.normalColor", "Normal Color") };
    if (name == "offset") return std::string{ EditorText("uiProperty.offset", "Offset") };
    if (name == "offsetMax.x") return std::string{ EditorText("uiProperty.offsetMax.x", "Offset Max x") };
    if (name == "offsetMax.y") return std::string{ EditorText("uiProperty.offsetMax.y", "Offset Max y") };
    if (name == "offsetMin.x") return std::string{ EditorText("uiProperty.offsetMin.x", "Offset Min x") };
    if (name == "offsetMin.y") return std::string{ EditorText("uiProperty.offsetMin.y", "Offset Min y") };
    if (name == "opacity") return std::string{ EditorText("uiProperty.opacity", "Opacity") };
    if (name == "optionCount") return std::string{ EditorText("uiProperty.optionCount", "Option Count") };
    if (name == "overflow") return std::string{ EditorText("uiProperty.overflow", "Overflow") };
    if (name == "padding") return std::string{ EditorText("uiProperty.padding", "Padding") };
    if (name == "pivot.x") return std::string{ EditorText("uiProperty.pivot.x", "Pivot x") };
    if (name == "pivot.y") return std::string{ EditorText("uiProperty.pivot.y", "Pivot y") };
    if (name == "pixelPerfect") return std::string{ EditorText("uiProperty.pixelPerfect", "Pixel Perfect") };
    if (name == "pixelsPerUnit") return std::string{ EditorText("uiProperty.pixelsPerUnit", "Pixels Per Unit") };
    if (name == "placeholder") return std::string{ EditorText("uiProperty.placeholder", "Placeholder") };
    if (name == "player") return std::string{ EditorText("uiProperty.player", "Player (-1 = Everyone)") };
    if (name == "preferredHeight") return std::string{ EditorText("uiProperty.preferredHeight", "Preferred Height") };
    if (name == "preferredWidth") return std::string{ EditorText("uiProperty.preferredWidth", "Preferred Width") };
    if (name == "preserveAspect") return std::string{ EditorText("uiProperty.preserveAspect", "Preserve Aspect") };
    if (name == "pressedColor") return std::string{ EditorText("uiProperty.pressedColor", "Pressed Color") };
    if (name == "pressedImage") return std::string{ EditorText("uiProperty.pressedImage", "Pressed Image") };
    if (name == "radius") return std::string{ EditorText("uiProperty.radius", "Radius") };
    if (name == "raycastPadding") return std::string{ EditorText("uiProperty.raycastPadding", "Raycast Padding") };
    if (name == "raycastTarget") return std::string{ EditorText("uiProperty.raycastTarget", "Raycast Target") };
    if (name == "readOnly") return std::string{ EditorText("uiProperty.readOnly", "Read Only") };
    if (name == "referenceResolution") return std::string{ EditorText("uiProperty.referenceResolution", "Reference Resolution") };
    if (name == "renderMode") return std::string{ EditorText("uiProperty.renderMode", "Render Mode") };
    if (name == "respectSafeArea") return std::string{ EditorText("uiProperty.respectSafeArea", "Fit Safe Area") };
    if (name == "richText") return std::string{ EditorText("uiProperty.richText", "Rich Text") };
    if (name == "rotationDegrees") return std::string{ EditorText("uiProperty.rotationDegrees", "Rotation Degrees") };
    if (name == "scale.x") return std::string{ EditorText("uiProperty.scale.x", "Scale x") };
    if (name == "scale.y") return std::string{ EditorText("uiProperty.scale.y", "Scale y") };
    if (name == "scaleFactor") return std::string{ EditorText("uiProperty.scaleFactor", "Scale Factor") };
    if (name == "scaleMode") return std::string{ EditorText("uiProperty.scaleMode", "Scale Mode") };
    if (name == "scrollSensitivity") return std::string{ EditorText("uiProperty.scrollSensitivity", "Scroll Sensitivity") };
    if (name == "scrollX") return std::string{ EditorText("uiProperty.scrollX", "Scroll X") };
    if (name == "scrollY") return std::string{ EditorText("uiProperty.scrollY", "Scroll Y") };
    if (name == "selectedColor") return std::string{ EditorText("uiProperty.selectedColor", "Selected Color") };
    if (name == "selectedImage") return std::string{ EditorText("uiProperty.selectedImage", "Selected Image") };
    if (name == "showGraphic") return std::string{ EditorText("uiProperty.showGraphic", "Show Graphic") };
    if (name == "size") return std::string{ EditorText("uiProperty.size", "Size") };
    if (name == "sizeDelta.x") return std::string{ EditorText("uiProperty.sizeDelta.x", "Width / Size Delta X") };
    if (name == "sizeDelta.y") return std::string{ EditorText("uiProperty.sizeDelta.y", "Height / Size Delta Y") };
    if (name == "snapToChildren") return std::string{ EditorText("uiProperty.snapToChildren", "Snap to Children") };
    if (name == "softness") return std::string{ EditorText("uiProperty.softness", "Softness") };
    if (name == "sortingOrder") return std::string{ EditorText("uiProperty.sortingOrder", "Sorting Order") };
    if (name == "spacing") return std::string{ EditorText("uiProperty.spacing", "Spacing") };
    if (name == "spriteAssetId") return std::string{ EditorText("uiProperty.spriteAssetId", "Sprite") };
    if (name == "submitOnRelease") return std::string{ EditorText("uiProperty.submitOnRelease", "Submit On Release") };
    if (name == "targetGraphic") return std::string{ EditorText("uiProperty.targetGraphic", "Target Graphic") };
    if (name == "templateEntity") return std::string{ EditorText("uiProperty.templateEntity", "Template") };
    if (name == "toggled") return std::string{ EditorText("uiProperty.toggled", "Toggled") };
    if (name == "tooltip") return std::string{ EditorText("uiProperty.tooltip", "Tooltip") };
    if (name == "transition") return std::string{ EditorText("uiProperty.transition", "Transition") };
    if (name == "transitionSeconds") return std::string{ EditorText("uiProperty.transitionSeconds", "Show/Hide Seconds") };
    if (name == "uvRect") return std::string{ EditorText("uiProperty.uvRect", "Uv Rect") };
    if (name == "value") return std::string{ EditorText("uiProperty.value", "Value") };
    if (name == "vertical") return std::string{ EditorText("uiProperty.vertical", "Vertical") };
    if (name == "verticalAlignment") return std::string{ EditorText("uiProperty.verticalAlignment", "Vertical Alignment") };
    if (name == "verticalFit") return std::string{ EditorText("uiProperty.verticalFit", "Vertical Fit") };
    if (name == "verticalScrollbar") return std::string{ EditorText("uiProperty.verticalScrollbar", "Vertical Scrollbar") };
    if (name == "visible") return std::string{ EditorText("uiProperty.visible", "Visible") };
    if (name == "wholeNumbers") return std::string{ EditorText("uiProperty.wholeNumbers", "Whole Numbers") };
    if (name == "width") return std::string{ EditorText("uiProperty.width", "Width") };
    if (name == "wrapMode") return std::string{ EditorText("uiProperty.wrapMode", "Wrap Mode") };
    if (name == "zOrder") return std::string{ EditorText("uiProperty.zOrder", "Z Order") };
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
        return component == Text ? std::vector<std::string_view>{EditorText("uiChoice.left", "Left"), EditorText("uiChoice.center", "Center"), EditorText("uiChoice.right", "Right")}
                                 : std::vector<std::string_view>{EditorText("uiChoice.left", "Left"), EditorText("uiChoice.center", "Center"), EditorText("uiChoice.right", "Right"), EditorText("uiChoice.stretch", "Stretch")};
    if (name == "verticalAlignment")
        return component == Text ? std::vector<std::string_view>{EditorText("uiChoice.top", "Top"), EditorText("uiChoice.center", "Center"), EditorText("uiChoice.bottom", "Bottom")}
                                 : std::vector<std::string_view>{EditorText("uiChoice.top", "Top"), EditorText("uiChoice.center", "Center"), EditorText("uiChoice.bottom", "Bottom"), EditorText("uiChoice.stretch", "Stretch")};
    if (component == Text && name == "wrapMode") return {EditorText("uiChoice.noWrap", "No Wrap"), EditorText("uiChoice.word", "Word"), EditorText("uiChoice.character", "Character")};
    if (component == Image && name == "scaleMode") return {EditorText("uiChoice.stretch", "Stretch"), EditorText("uiChoice.fitInside", "Fit Inside"), EditorText("uiChoice.fillAndCrop", "Fill and Crop"), EditorText("uiChoice.tiled", "Tiled")};
    if (component == Image && name == "fillMethod") return {EditorText("uiChoice.none", "None"), EditorText("uiChoice.horizontal", "Horizontal"), EditorText("uiChoice.vertical", "Vertical"), EditorText("uiChoice.radial360", "Radial 360")};
    if (component == Image && name == "fillOrigin") return {EditorText("uiChoice.start", "Start"), EditorText("uiChoice.end", "End")};
    if (component == Canvas && name == "renderMode") return {EditorText("uiChoice.screenSpace", "Screen Space"), EditorText("uiChoice.worldSpace", "World Space")};
    if (component == Text && name == "overflow") return {EditorText("uiChoice.overflow", "Overflow"), EditorText("uiChoice.truncate", "Truncate"), EditorText("uiChoice.ellipsis", "Ellipsis")};
    if (component == Selectable && name == "transition") return {EditorText("uiChoice.none", "None"), EditorText("uiChoice.colorTint", "Color Tint"), EditorText("uiChoice.spriteSwap", "Sprite Swap")};
    if (component == ScrollView && name == "movementType") return {EditorText("uiChoice.clamped", "Clamped"), EditorText("uiChoice.elastic", "Elastic"), EditorText("uiChoice.unrestricted", "Unrestricted")};
    if (component == InputField && name == "contentType")
        return {EditorText("uiChoice.standard", "Standard"), EditorText("uiChoice.integerNumber", "Integer Number"), EditorText("uiChoice.decimalNumber", "Decimal Number"), EditorText("uiChoice.alphanumeric", "Alphanumeric"), EditorText("uiChoice.emailAddress", "Email Address"), EditorText("uiChoice.password", "Password"), EditorText("uiChoice.pin", "Pin")};
    if (component == CanvasScaler && name == "scaleMode") return {EditorText("uiChoice.constantPixelSize", "Constant Pixel Size"), EditorText("uiChoice.scaleWithScreenSize", "Scale with Screen Size")};
    if (component == ContentSizeFitter && (name == "horizontalFit" || name == "verticalFit"))
        return {EditorText("uiChoice.unconstrained", "Unconstrained"), EditorText("uiChoice.minimumSize", "Minimum Size"), EditorText("uiChoice.preferredSize", "Preferred Size")};
    if (component == AspectRatioFitter && name == "mode")
        return {EditorText("uiChoice.none", "None"), EditorText("uiChoice.widthControlsHeight", "Width Controls Height"), EditorText("uiChoice.heightControlsWidth", "Height Controls Width"), EditorText("uiChoice.fitInsideParent", "Fit Inside Parent"), EditorText("uiChoice.fillParent", "Fill Parent")};
    if (component == Selectable && name == "navigationMode") return {EditorText("uiChoice.none", "None"), EditorText("uiChoice.automatic", "Automatic"), EditorText("uiChoice.explicit", "Explicit")};
    if ((component == Slider || component == Scrollbar || component == ProgressBar) && name == "direction")
        return {EditorText("uiChoice.leftToRight", "Left to Right"), EditorText("uiChoice.rightToLeft", "Right to Left"), EditorText("uiChoice.bottomToTop", "Bottom to Top"), EditorText("uiChoice.topToBottom", "Top to Bottom")};
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
    fields[0].label = stretchX ? EditorText("uiRect.left", "Left") : EditorText("uiRect.positionX", "Position X");
    fields[1].label = stretchY ? EditorText("uiRect.top", "Top") : EditorText("uiRect.positionY", "Position Y");
    fields[2].label = stretchX ? EditorText("uiRect.right", "Right") : EditorText("uiRect.width", "Width");
    fields[3].label = stretchY ? EditorText("uiRect.bottom", "Bottom") : EditorText("uiRect.height", "Height");
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
    // Settings that do nothing in the current mode stay out of the way: a row with no fields is neither
    // drawn nor hit, and its index keeps addressing the same property.
    const auto value = [&output](std::string_view name) {
        const auto found = std::ranges::find(output, name, &InspectorUIPropertyRow::name);
        return found == output.end() ? std::string{} : found->value;
    };
    const auto hide = [&output](std::initializer_list<std::string_view> names) {
        for (InspectorUIPropertyRow& row : output)
            if (std::ranges::find(names, row.name) != names.end()) row.fieldCount = 0;
    };
    using enum kb::scene::UIComponentType;
    switch (component) {
    case Canvas:
        hide(value("renderMode") == "1" ? std::initializer_list<std::string_view>{"respectSafeArea"}
                                         : std::initializer_list<std::string_view>{"pixelsPerUnit"});
        break;
    case CanvasGroup:
        if (value("transitionSeconds") == "0") hide({"hiddenOffset.x", "hiddenScale"});
        break;
    case Image: {
        const std::string method = value("fillMethod");
        if (method == "0") hide({"fillOrigin", "fillAmount", "fillClockwise"});
        else if (method != "3") hide({"fillClockwise"});
        // The two ends of a fill read as the sides of the image it grows from.
        const auto origin = std::ranges::find(output, std::string_view{"fillOrigin"}, &InspectorUIPropertyRow::name);
        if (origin != output.end())
            origin->choices = method == "1" ? std::vector<std::string_view>{EditorText("uiChoice.left", "Left"), EditorText("uiChoice.right", "Right")}
                : method == "2"             ? std::vector<std::string_view>{EditorText("uiChoice.bottom", "Bottom"), EditorText("uiChoice.top", "Top")}
                                            : std::vector<std::string_view>{EditorText("uiChoice.top", "Top"), EditorText("uiChoice.bottom", "Bottom")};
        break;
    }
    case Text:
        if (value("autoSize") != "true") hide({"minFontSize"});
        break;
    case Selectable: {
        const std::string transition = value("transition");
        if (transition != "1")
            hide({"normalColor.r", "highlightedColor.r", "pressedColor.r", "selectedColor.r", "disabledColor.r", "colorFadeSeconds"});
        if (transition != "2") hide({"highlightedImage", "pressedImage", "selectedImage", "disabledImage"});
        break;
    }
    case ScrollView:
        if (value("movementType") != "1") hide({"elasticity"});
        break;
    default:
        break;
    }
    return output;
}

std::string_view InspectorUIComponentModel::DisplayName(kb::scene::UIComponentType component) {
    switch (component) {
    case kb::scene::UIComponentType::RectTransform: return EditorText("uiComponent.rectTransform", "Rect Transform");
    case kb::scene::UIComponentType::Canvas: return EditorText("uiComponent.canvas", "Canvas");
    case kb::scene::UIComponentType::CanvasScaler: return EditorText("uiComponent.canvasScaler", "Canvas Scaler");
    case kb::scene::UIComponentType::CanvasGroup: return EditorText("uiComponent.canvasGroup", "Canvas Group");
    case kb::scene::UIComponentType::HorizontalLayout: return EditorText("uiComponent.horizontalLayout", "Horizontal Layout");
    case kb::scene::UIComponentType::VerticalLayout: return EditorText("uiComponent.verticalLayout", "Vertical Layout");
    case kb::scene::UIComponentType::GridLayout: return EditorText("uiComponent.gridLayout", "Grid Layout");
    case kb::scene::UIComponentType::WrapLayout: return EditorText("uiComponent.wrapLayout", "Wrap Layout");
    case kb::scene::UIComponentType::OverlayLayout: return EditorText("uiComponent.overlayLayout", "Overlay Layout");
    case kb::scene::UIComponentType::LayoutElement: return EditorText("uiComponent.layoutElement", "Layout Element");
    case kb::scene::UIComponentType::ContentSizeFitter: return EditorText("uiComponent.contentSizeFitter", "Content Size Fitter");
    case kb::scene::UIComponentType::AspectRatioFitter: return EditorText("uiComponent.aspectRatioFitter", "Aspect Ratio Fitter");
    case kb::scene::UIComponentType::Sprite: return EditorText("uiComponent.sprite", "Sprite");
    case kb::scene::UIComponentType::Image: return EditorText("uiComponent.image", "Image");
    case kb::scene::UIComponentType::RawImage: return EditorText("uiComponent.rawImage", "Raw Image");
    case kb::scene::UIComponentType::Text: return EditorText("uiComponent.text", "Text");
    case kb::scene::UIComponentType::Border: return EditorText("uiComponent.border", "Border");
    case kb::scene::UIComponentType::Mask: return EditorText("uiComponent.mask", "Mask");
    case kb::scene::UIComponentType::Shadow: return EditorText("uiComponent.shadow", "Shadow");
    case kb::scene::UIComponentType::Outline: return EditorText("uiComponent.outline", "Outline");
    case kb::scene::UIComponentType::BackgroundBlur: return EditorText("uiComponent.backgroundBlur", "Background Blur");
    case kb::scene::UIComponentType::Selectable: return EditorText("uiComponent.selectable", "Selectable");
    case kb::scene::UIComponentType::Button: return EditorText("uiComponent.button", "Button");
    case kb::scene::UIComponentType::Toggle: return EditorText("uiComponent.toggle", "Toggle");
    case kb::scene::UIComponentType::Slider: return EditorText("uiComponent.slider", "Slider");
    case kb::scene::UIComponentType::Scrollbar: return EditorText("uiComponent.scrollbar", "Scrollbar");
    case kb::scene::UIComponentType::ScrollView: return EditorText("uiComponent.scrollView", "Scroll View");
    case kb::scene::UIComponentType::InputField: return EditorText("uiComponent.inputField", "Input Field");
    case kb::scene::UIComponentType::Dropdown: return EditorText("uiComponent.dropdown", "Dropdown");
    case kb::scene::UIComponentType::ProgressBar: return EditorText("uiComponent.progressBar", "Progress Bar");
    case kb::scene::UIComponentType::WidgetSwitcher: return EditorText("uiComponent.widgetSwitcher", "Widget Switcher");
    }
    return EditorText("inspector.uiComponent.title", "UI Component");
}

std::string_view InspectorUIComponentModel::PresetDisplayName(kb::scene::UIComponentPreset preset) {
    switch (preset) {
    case kb::scene::UIComponentPreset::Canvas: return EditorText("uiPreset.canvas", "Canvas");
    case kb::scene::UIComponentPreset::Text: return EditorText("uiPreset.text", "Text");
    case kb::scene::UIComponentPreset::Image: return EditorText("uiPreset.image", "Image");
    case kb::scene::UIComponentPreset::RawImage: return EditorText("uiPreset.rawImage", "Raw Image");
    case kb::scene::UIComponentPreset::Button: return EditorText("uiPreset.button", "Button");
    case kb::scene::UIComponentPreset::Toggle: return EditorText("uiPreset.toggle", "Toggle");
    case kb::scene::UIComponentPreset::Slider: return EditorText("uiPreset.slider", "Slider");
    case kb::scene::UIComponentPreset::Scrollbar: return EditorText("uiPreset.scrollbar", "Scrollbar");
    case kb::scene::UIComponentPreset::ScrollView: return EditorText("uiPreset.scrollView", "Scroll View");
    case kb::scene::UIComponentPreset::InputField: return EditorText("uiPreset.inputField", "Input Field");
    case kb::scene::UIComponentPreset::Dropdown: return EditorText("uiPreset.dropdown", "Dropdown");
    case kb::scene::UIComponentPreset::ProgressBar: return EditorText("uiPreset.progressBar", "Progress Bar");
    case kb::scene::UIComponentPreset::HorizontalBox: return EditorText("uiPreset.horizontalBox", "Horizontal Box");
    case kb::scene::UIComponentPreset::VerticalBox: return EditorText("uiPreset.verticalBox", "Vertical Box");
    case kb::scene::UIComponentPreset::Grid: return EditorText("uiPreset.grid", "Grid");
    case kb::scene::UIComponentPreset::Wrap: return EditorText("uiPreset.wrap", "Wrap");
    case kb::scene::UIComponentPreset::Overlay: return EditorText("uiPreset.overlay", "Overlay");
    case kb::scene::UIComponentPreset::Border: return EditorText("uiPreset.border", "Border");
    case kb::scene::UIComponentPreset::Blur: return EditorText("uiPreset.blur", "Blur");
    case kb::scene::UIComponentPreset::WidgetSwitcher: return EditorText("uiPreset.widgetSwitcher", "Widget Switcher");
    }
    return EditorText("inspector.uiComponent.title", "UI Component");
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

std::string InspectorUIComponentModel::EntityReferenceLabel(const kb::scene::Scene& scene, std::uint64_t id,
    bool requireSelectable) {
    if (id == 0U) return std::string{ EditorText("inspector.noAsset", "(none)") };
    const kb::scene::SceneEntity entity{ id };
    if (!scene.Entities().IsAlive(entity)) {
        return EditorLocalization::Format("uiReference.missingObject", "(missing object #{id})", { { "id", std::to_string(id) } });
    }
    std::string name = scene.Entities().Name(entity);
    if (requireSelectable && !scene.Components().UI().Has<kb::scene::UISelectable>(entity)) {
        return EditorLocalization::Format("uiReference.notSelectable", "{name} (not selectable)", { { "name", name } });
    }
    return name;
}

std::vector<kb::scene::SceneEntity> InspectorUIComponentModel::NavigationTargets(
    const kb::scene::Scene& scene, kb::scene::SceneEntity source) {
    std::vector<kb::scene::SceneEntity> output;
    const std::vector<kb::scene::SceneEntity> roots = scene.Hierarchy().RootEntities();
    std::vector<kb::scene::SceneEntity> pending(roots.rbegin(), roots.rend());
    while (!pending.empty()) {
        const kb::scene::SceneEntity entity = pending.back();
        pending.pop_back();
        if (entity != source && scene.Components().UI().Has<kb::scene::UISelectable>(entity))
            output.push_back(entity);
        for (std::size_t index = scene.Hierarchy().ChildCount(entity); index > 0U; --index)
            pending.push_back(scene.Hierarchy().ChildAt(entity, index - 1U));
    }
    return output;
}

InspectorUIComponentModel::ReferenceChoice InspectorUIComponentModel::ReferenceTargets(
    const kb::scene::Scene& scene, kb::scene::SceneEntity source, kb::scene::UIComponentType component, std::string_view property) {
    ReferenceChoice choice;
    const auto ui = scene.Components().UI();
    const auto collect = [&](kb::scene::SceneEntity root, bool includeRoot, auto&& accept) {
        std::vector<kb::scene::SceneEntity> pending;
        if (root.IsValid()) pending.push_back(root);
        else {
            const std::vector<kb::scene::SceneEntity> roots = scene.Hierarchy().RootEntities();
            pending.assign(roots.rbegin(), roots.rend());
        }
        while (!pending.empty()) {
            const kb::scene::SceneEntity entity = pending.back();
            pending.pop_back();
            if ((includeRoot || entity != root) && entity != source && accept(entity)) choice.targets.push_back(entity);
            for (std::size_t index = scene.Hierarchy().ChildCount(entity); index > 0U; --index)
                pending.push_back(scene.Hierarchy().ChildAt(entity, index - 1U));
        }
    };
    const auto draws = [&ui](kb::scene::SceneEntity entity) {
        return ui.Has<kb::scene::UIImage>(entity) || ui.Has<kb::scene::UIRawImage>(entity) || ui.Has<kb::scene::UISprite>(entity) ||
            ui.Has<kb::scene::UIText>(entity) || ui.Has<kb::scene::UIBorder>(entity);
    };
    if (property.starts_with("navigation")) {
        choice.targets = NavigationTargets(scene, source);
        choice.title = EditorText("uiReference.selectNavigationTarget", "Select Navigation Target");
        choice.description = EditorText("uiReference.selectNavigationTargetHint", "Choose the widget focus moves to in this direction.");
    } else if (component == kb::scene::UIComponentType::Dropdown && property == "templateEntity") {
        for (std::size_t index = 0U; index < scene.Hierarchy().ChildCount(source); ++index) {
            const kb::scene::SceneEntity child = scene.Hierarchy().ChildAt(source, index);
            if (ui.Has<kb::scene::UIRectTransform>(child)) choice.targets.push_back(child);
        }
        choice.title = EditorText("uiReference.selectTemplate", "Select Template");
        choice.description = EditorText("uiReference.selectTemplateHint", "Choose the child object cloned as the open list.");
    } else if (component == kb::scene::UIComponentType::Dropdown && (property == "captionText" || property == "itemText")) {
        collect(source, false, [&ui](kb::scene::SceneEntity entity) { return ui.Has<kb::scene::UIText>(entity); });
        choice.title = property == "captionText" ? EditorText("uiReference.selectCaptionText", "Select Caption Text")
                                                 : EditorText("uiReference.selectItemText", "Select Item Text");
        choice.description = property == "captionText"
            ? EditorText("uiReference.selectCaptionTextHint", "Choose the Text that shows the chosen option.")
            : EditorText("uiReference.selectItemTextHint", "Choose the Text inside the template's item that shows each option.");
    } else if (component == kb::scene::UIComponentType::Dropdown && (property == "captionImage" || property == "itemImage")) {
        collect(source, false, [&ui](kb::scene::SceneEntity entity) { return ui.Has<kb::scene::UIImage>(entity); });
        choice.title = property == "captionImage" ? EditorText("uiReference.selectCaptionImage", "Select Caption Image")
                                                  : EditorText("uiReference.selectItemImage", "Select Item Image");
        choice.description = property == "captionImage"
            ? EditorText("uiReference.selectCaptionImageHint", "Choose the Image that shows the chosen option's image.")
            : EditorText("uiReference.selectItemImageHint", "Choose the Image inside the template's item that shows each option's image.");
    } else if (component == kb::scene::UIComponentType::Toggle && property == "group") {
        collect({}, true, [&ui](kb::scene::SceneEntity entity) { return ui.Has<kb::scene::UIRectTransform>(entity); });
        choice.title = EditorText("uiReference.selectRadioGroup", "Select Radio Group");
        choice.description = EditorText("uiReference.selectRadioGroupHint",
            "Toggles that name the same object switch each other off - usually the parent they share.");
    } else if (property == "fillRect" || property == "handleRect") {
        collect(source, false, [&ui](kb::scene::SceneEntity entity) { return ui.Has<kb::scene::UIRectTransform>(entity); });
        choice.title = property == "fillRect" ? EditorText("uiReference.selectFill", "Select Fill")
                                              : EditorText("uiReference.selectHandle", "Select Handle");
        choice.description = property == "fillRect"
            ? EditorText("uiReference.selectFillHint", "Choose the child stretched from the start up to the value.")
            : EditorText("uiReference.selectHandleHint", "Choose the child placed at the value.");
    } else if (property == "eventTarget") {
        collect({}, true, [](kb::scene::SceneEntity) { return true; });
        choice.title = EditorText("uiReference.selectEventTarget", "Select Event Target");
        choice.description = EditorText("uiReference.selectEventTargetHint", "Choose the object whose scripts receive the click's action event.");
    } else if (component == kb::scene::UIComponentType::ScrollView) {
        collect({}, true, [&ui](kb::scene::SceneEntity entity) { return ui.Has<kb::scene::UIScrollbar>(entity); });
        choice.title = EditorText("uiReference.selectScrollbar", "Select Scrollbar");
        choice.description = EditorText("uiReference.selectScrollbarHint", "Choose the scrollbar that follows this scroll view.");
    } else {
        collect({}, true, draws);
        choice.title = EditorText("uiReference.selectGraphic", "Select Graphic");
        choice.description = EditorText("uiReference.selectGraphicHint", "Choose the widget that shows this state.");
    }
    return choice;
}

std::string InspectorUIComponentModel::HierarchyPath(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    std::string path = scene.Entities().Name(entity);
    for (auto parent = scene.Hierarchy().Parent(entity); parent.IsValid(); parent = scene.Hierarchy().Parent(parent))
        path = scene.Entities().Name(parent) + " / " + path;
    return path;
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
