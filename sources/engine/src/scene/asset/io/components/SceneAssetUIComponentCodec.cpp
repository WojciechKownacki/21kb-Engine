#include "scene/asset/io/components/SceneAssetUIComponentCodec.hpp"

#include "engine/ui/UIComponentCatalog.hpp"
#include "engine/ui/UIComponentValidation.hpp"

#include <stdexcept>
#include <string>

namespace kb::scene {
namespace {

using Reader = SceneAssetBinaryIO::ByteReader;
using kb::math::Color;
using kb::math::Rect;
using kb::math::Vec2;
using kb::math::Vec4;

constexpr std::uint64_t Bit(UIComponentType type) noexcept { return 1ULL << static_cast<unsigned int>(type); }
constexpr std::uint64_t KnownBits = (1ULL << (static_cast<unsigned int>(UIComponentType::WidgetSwitcher) + 1U)) - 1ULL;

bool Read(Reader& in, Vec2& v) { return in.ReadFloat(v.x) && in.ReadFloat(v.y); }
bool Read(Reader& in, Vec4& v) { return in.ReadFloat(v.x) && in.ReadFloat(v.y) && in.ReadFloat(v.z) && in.ReadFloat(v.w); }
bool Read(Reader& in, Color& v) { return in.ReadFloat(v.r) && in.ReadFloat(v.g) && in.ReadFloat(v.b) && in.ReadFloat(v.a); }
bool Read(Reader& in, Rect& v) { return in.ReadFloat(v.x) && in.ReadFloat(v.y) && in.ReadFloat(v.width) && in.ReadFloat(v.height); }
bool Read(Reader& in, UIEdges& v) { return in.ReadFloat(v.left) && in.ReadFloat(v.top) && in.ReadFloat(v.right) && in.ReadFloat(v.bottom); }
template <typename E> requires std::is_enum_v<E>
bool Read(Reader& in, E& v) { std::uint32_t raw = 0U; if (!in.ReadUInt32(raw)) return false; v = static_cast<E>(raw); return true; }

void Write(std::vector<std::uint8_t>& out, Vec2 v) { SceneAssetBinaryIO::WriteFloat(out, v.x); SceneAssetBinaryIO::WriteFloat(out, v.y); }
void Write(std::vector<std::uint8_t>& out, Vec4 v) { SceneAssetBinaryIO::WriteFloat(out, v.x); SceneAssetBinaryIO::WriteFloat(out, v.y); SceneAssetBinaryIO::WriteFloat(out, v.z); SceneAssetBinaryIO::WriteFloat(out, v.w); }
void Write(std::vector<std::uint8_t>& out, Color v) { SceneAssetBinaryIO::WriteFloat(out, v.r); SceneAssetBinaryIO::WriteFloat(out, v.g); SceneAssetBinaryIO::WriteFloat(out, v.b); SceneAssetBinaryIO::WriteFloat(out, v.a); }
void Write(std::vector<std::uint8_t>& out, Rect v) { SceneAssetBinaryIO::WriteFloat(out, v.x); SceneAssetBinaryIO::WriteFloat(out, v.y); SceneAssetBinaryIO::WriteFloat(out, v.width); SceneAssetBinaryIO::WriteFloat(out, v.height); }
void Write(std::vector<std::uint8_t>& out, UIEdges v) { SceneAssetBinaryIO::WriteFloat(out, v.left); SceneAssetBinaryIO::WriteFloat(out, v.top); SceneAssetBinaryIO::WriteFloat(out, v.right); SceneAssetBinaryIO::WriteFloat(out, v.bottom); }
template <typename E> requires std::is_enum_v<E>
void Write(std::vector<std::uint8_t>& out, E v) { SceneAssetBinaryIO::WriteUInt32(out, static_cast<std::uint32_t>(v)); }

bool ReadValue(Reader& in, UIRectTransform& v) { return Read(in,v.anchorMin)&&Read(in,v.anchorMax)&&Read(in,v.offsetMin)&&Read(in,v.offsetMax)&&Read(in,v.pivot)&&Read(in,v.scale)&&in.ReadFloat(v.rotationDegrees)&&in.ReadInt32(v.zOrder); }
bool ReadValue(Reader& in, UICanvas& v) { return in.ReadInt32(v.sortingOrder)&&in.ReadBool(v.pixelPerfect); }
// v39 added the render mode, world scale, safe-area layout and owning player. An older canvas keeps
// filling the whole screen, which is how it was authored.
bool ReadValue(Reader& in, UICanvas& v, std::uint32_t fileVersion) {
    if (!ReadValue(in, v)) return false;
    if (fileVersion < 39U) { v.respectSafeArea = false; return true; }
    return Read(in,v.renderMode)&&in.ReadFloat(v.pixelsPerUnit)&&in.ReadBool(v.respectSafeArea)&&in.ReadInt32(v.player);
}
bool ReadValue(Reader& in, UICanvasScaler& v) { return Read(in,v.scaleMode)&&Read(in,v.referenceResolution)&&in.ReadFloat(v.scaleFactor)&&in.ReadFloat(v.matchWidthOrHeight); }
bool ReadValue(Reader& in, UICanvasGroup& v) { return in.ReadFloat(v.opacity)&&in.ReadBool(v.interactable)&&in.ReadBool(v.blocksRaycasts)&&in.ReadBool(v.ignoreParentGroups); }
// v39 added the show/hide animation.
bool ReadValue(Reader& in, UICanvasGroup& v, std::uint32_t fileVersion) {
    return ReadValue(in, v) && (fileVersion < 39U || (in.ReadBool(v.visible)&&in.ReadFloat(v.transitionSeconds)&&Read(in,v.hiddenOffset)&&in.ReadFloat(v.hiddenScale)));
}
bool ReadValue(Reader& in, UIHorizontalLayout& v) { return Read(in,v.padding)&&in.ReadFloat(v.spacing)&&Read(in,v.horizontalAlignment)&&Read(in,v.verticalAlignment)&&in.ReadBool(v.controlChildWidth)&&in.ReadBool(v.controlChildHeight)&&in.ReadBool(v.expandChildWidth)&&in.ReadBool(v.expandChildHeight); }
bool ReadValue(Reader& in, UIVerticalLayout& v) { return Read(in,v.padding)&&in.ReadFloat(v.spacing)&&Read(in,v.horizontalAlignment)&&Read(in,v.verticalAlignment)&&in.ReadBool(v.controlChildWidth)&&in.ReadBool(v.controlChildHeight)&&in.ReadBool(v.expandChildWidth)&&in.ReadBool(v.expandChildHeight); }
bool ReadValue(Reader& in, UIGridLayout& v) { return Read(in,v.padding)&&Read(in,v.spacing)&&Read(in,v.cellSize)&&in.ReadUInt32(v.columns)&&Read(in,v.horizontalAlignment)&&Read(in,v.verticalAlignment); }
bool ReadValue(Reader& in, UIWrapLayout& v) { return Read(in,v.padding)&&Read(in,v.spacing)&&Read(in,v.horizontalAlignment)&&Read(in,v.verticalAlignment); }
bool ReadValue(Reader& in, UIOverlayLayout& v) { return Read(in,v.padding)&&Read(in,v.horizontalAlignment)&&Read(in,v.verticalAlignment); }
bool ReadValue(Reader& in, UILayoutElement& v) { return in.ReadFloat(v.minimumWidth)&&in.ReadFloat(v.minimumHeight)&&in.ReadFloat(v.preferredWidth)&&in.ReadFloat(v.preferredHeight)&&in.ReadFloat(v.flexibleWidth)&&in.ReadFloat(v.flexibleHeight)&&in.ReadInt32(v.layoutPriority)&&in.ReadBool(v.ignoreLayout); }
bool ReadValue(Reader& in, UIContentSizeFitter& v) { return Read(in,v.horizontalFit)&&Read(in,v.verticalFit); }
bool ReadValue(Reader& in, UIAspectRatioFitter& v) { return Read(in,v.mode)&&in.ReadFloat(v.aspectRatio); }
bool ReadValue(Reader& in, UISprite& v) { return in.ReadUInt64(v.spriteAssetId)&&Read(in,v.color)&&in.ReadBool(v.preserveAspect); }
bool ReadValue(Reader& in, UIImage& v) { return in.ReadUInt64(v.imageAssetId)&&Read(in,v.uvRect)&&Read(in,v.scaleMode)&&in.ReadBool(v.preserveAspect)&&Read(in,v.nineSlice)&&Read(in,v.color); }
// v39 added the fill and the alpha hit threshold.
bool ReadValue(Reader& in, UIImage& v, std::uint32_t fileVersion) {
    return ReadValue(in, v) && (fileVersion < 39U || (Read(in,v.fillMethod)&&Read(in,v.fillOrigin)&&in.ReadFloat(v.fillAmount)&&in.ReadBool(v.fillClockwise)&&in.ReadFloat(v.alphaHitThreshold)));
}
bool ReadValue(Reader& in, UIRawImage& v) { return in.ReadUInt64(v.imageAssetId)&&Read(in,v.uvRect)&&Read(in,v.color); }
bool ReadValue(Reader& in, UIText& v) { std::string text; return in.ReadString(text,static_cast<std::uint32_t>(UIText::MaxUtf8Bytes-1U))&&SetUITextContent(v,text)&&in.ReadUInt64(v.fontAssetId)&&in.ReadFloat(v.fontSize)&&Read(in,v.color)&&Read(in,v.horizontalAlignment)&&Read(in,v.verticalAlignment)&&Read(in,v.wrapMode)&&in.ReadFloat(v.lineSpacing)&&in.ReadBool(v.richText); }
// v39 added the localization key, overflow, auto size, line limit and character spacing.
bool ReadValue(Reader& in, UIText& v, std::uint32_t fileVersion) {
    if (!ReadValue(in, v)) return false;
    if (fileVersion < 39U) return true;
    std::string key;
    return in.ReadString(key,static_cast<std::uint32_t>(UIText::MaxLocalizationKeyBytes-1U))&&SetUITextLocalizationKey(v,key)&&Read(in,v.overflow)&&in.ReadBool(v.autoSize)&&in.ReadFloat(v.minFontSize)&&in.ReadUInt32(v.maxLines)&&in.ReadFloat(v.characterSpacing);
}
bool ReadValue(Reader& in, UIBorder& v) { return Read(in,v.backgroundColor)&&Read(in,v.borderColor)&&Read(in,v.borderWidth)&&Read(in,v.cornerRadius)&&in.ReadFloat(v.opacity); }
bool ReadValue(Reader& in, UIMask& v) { return in.ReadBool(v.showGraphic); }
// v39 added the soft edge.
bool ReadValue(Reader& in, UIMask& v, std::uint32_t fileVersion) {
    return ReadValue(in, v) && (fileVersion < 39U || in.ReadFloat(v.softness));
}
bool ReadValue(Reader& in, UIShadow& v) { return Read(in,v.offset)&&Read(in,v.color)&&in.ReadFloat(v.blur); }
bool ReadValue(Reader& in, UIOutline& v) { return Read(in,v.color)&&in.ReadFloat(v.width); }
bool ReadValue(Reader& in, UIBackgroundBlur& v) { return in.ReadFloat(v.radius); }
bool ReadValue(Reader& in, UISelectable& v) { std::string name; return in.ReadBool(v.raycastTarget)&&in.ReadBool(v.interactable)&&Read(in,v.navigationMode)&&in.ReadUInt64(v.navigationUp)&&in.ReadUInt64(v.navigationDown)&&in.ReadUInt64(v.navigationLeft)&&in.ReadUInt64(v.navigationRight)&&in.ReadString(name,static_cast<std::uint32_t>(UISelectable::MaxEventNameBytes-1U))&&SetUIEventName(v,name)&&Read(in,v.normalColor)&&Read(in,v.highlightedColor)&&Read(in,v.pressedColor)&&Read(in,v.selectedColor)&&Read(in,v.disabledColor)&&in.ReadFloat(v.colorFadeSeconds); }
bool ReadValue(Reader& in, UIButton& v) { return in.ReadBool(v.submitOnRelease); }
bool ReadValue(Reader& in, UIToggle& v) { return in.ReadBool(v.toggled); }
// v38 added the toggle's on-state graphic.
bool ReadValue(Reader& in, UIToggle& v, std::uint32_t fileVersion) {
    return ReadValue(in, v) && (fileVersion < 38U || in.ReadUInt64(v.graphic)) &&
        (fileVersion < 39U || (in.ReadUInt64(v.group) && in.ReadBool(v.allowSwitchOff)));
}
bool ReadValue(Reader& in, UISlider& v) { return in.ReadFloat(v.minimum)&&in.ReadFloat(v.maximum)&&in.ReadFloat(v.value)&&Read(in,v.direction)&&in.ReadBool(v.wholeNumbers); }
// v39 added the fill and handle widgets.
bool ReadValue(Reader& in, UISlider& v, std::uint32_t fileVersion) {
    return ReadValue(in, v) && (fileVersion < 39U || (in.ReadUInt64(v.fillRect)&&in.ReadUInt64(v.handleRect)));
}
bool ReadValue(Reader& in, UIScrollbar& v) { return in.ReadFloat(v.value)&&in.ReadFloat(v.size)&&Read(in,v.direction); }
bool ReadValue(Reader& in, UIScrollView& v) { return in.ReadFloat(v.scrollX)&&in.ReadFloat(v.scrollY)&&in.ReadFloat(v.scrollSensitivity)&&in.ReadBool(v.horizontal)&&in.ReadBool(v.vertical)&&in.ReadBool(v.inertia); }
// v38 added the linked vertical scrollbar.
bool ReadValue(Reader& in, UIScrollView& v, std::uint32_t fileVersion) {
    return ReadValue(in, v) && (fileVersion < 38U || in.ReadUInt64(v.verticalScrollbar)) &&
        (fileVersion < 39U || (in.ReadUInt64(v.horizontalScrollbar)&&Read(in,v.movementType)&&in.ReadFloat(v.elasticity)&&in.ReadBool(v.snapToChildren)));
}
bool ReadValue(Reader& in, UIInputField& v) { return in.ReadUInt32(v.characterLimit)&&in.ReadBool(v.multiline)&&in.ReadBool(v.readOnly); }
// v39 added the content type and placeholder.
bool ReadValue(Reader& in, UIInputField& v, std::uint32_t fileVersion) {
    if (!ReadValue(in, v)) return false;
    if (fileVersion < 39U) return true;
    std::string placeholder;
    return Read(in,v.contentType)&&in.ReadString(placeholder,static_cast<std::uint32_t>(UIInputField::MaxPlaceholderUtf8Bytes-1U))&&SetUIInputPlaceholder(v,placeholder);
}
// Before v35 navigation links were written as live entity ids, which name nothing once the scene is
// loaded again. From v35 they are stable node ids resolved on instantiation, so an older id is not
// reinterpreted as one - it is dropped, which is what those links already amounted to on reload.
bool ReadValue(Reader& in, UISelectable& v, std::uint32_t fileVersion) {
    if (!ReadValue(in, v)) return false;
    if (fileVersion < 35U) v.navigationUp = v.navigationDown = v.navigationLeft = v.navigationRight = 0U;
    // v38 added the target graphic.
    if (fileVersion >= 38U && !in.ReadUInt64(v.targetGraphic)) return false;
    // v39 added transitions, sprite-swap images, raycast padding, the tooltip, dragging and the event target.
    if (fileVersion < 39U) return true;
    std::string tooltip;
    return Read(in,v.transition)&&in.ReadUInt64(v.highlightedImage)&&in.ReadUInt64(v.pressedImage)&&in.ReadUInt64(v.selectedImage)&&
        in.ReadUInt64(v.disabledImage)&&Read(in,v.raycastPadding)&&in.ReadString(tooltip,static_cast<std::uint32_t>(UISelectable::MaxTooltipUtf8Bytes-1U))&&
        SetUITooltipText(v,tooltip)&&in.ReadBool(v.draggable)&&in.ReadUInt64(v.eventTarget);
}

// The v38 dropdown points at the widgets it drives and keeps only labels and images for its options.
// Older payloads carried a list style and, from v37, option content widgets; those are read past and
// dropped. v35-v37 wrote the chosen index where v38 writes the value.
bool ReadValue(Reader& in, UIDropdown& v, std::uint32_t fileVersion) {
    if (fileVersion >= 38U) {
        if (!in.ReadUInt64(v.templateEntity) || !in.ReadUInt64(v.captionText) || !in.ReadUInt64(v.captionImage) ||
            !in.ReadUInt64(v.itemText) || !in.ReadUInt64(v.itemImage) || !in.ReadUInt32(v.value) ||
            !in.ReadFloat(v.alphaFadeSpeed) || !in.ReadUInt32(v.optionCount) || v.optionCount > UIDropdown::MaxOptions)
            return false;
        for (std::uint32_t index = 0U; index < v.optionCount; ++index) {
            std::string text;
            if (!in.ReadString(text, static_cast<std::uint32_t>(UIDropdownOption::MaxUtf8Bytes - 1U)) ||
                !SetUIDropdownOptionText(v.options[index], text) || !in.ReadUInt64(v.options[index].imageAssetId))
                return false;
        }
        return true;
    }
    if (!in.ReadUInt32(v.value)) return false;
    if (fileVersion < 35U) return true;
    std::uint32_t maxVisibleOptions = 0U;
    if (!in.ReadUInt32(maxVisibleOptions)) return false;
    if (fileVersion < 36U) return true;
    if (!in.ReadUInt32(v.optionCount) || v.optionCount > UIDropdown::MaxOptions) return false;
    for (std::uint32_t index = 0U; index < v.optionCount; ++index) {
        std::string text;
        std::uint64_t content = 0U;
        if (!in.ReadString(text, static_cast<std::uint32_t>(UIDropdownOption::MaxUtf8Bytes - 1U)) ||
            !SetUIDropdownOptionText(v.options[index], text) || !in.ReadUInt64(v.options[index].imageAssetId) ||
            (fileVersion >= 37U && !in.ReadUInt64(content)))
            return false;
    }
    std::uint64_t fontAssetId = 0U;
    float fontSize = 0.0F;
    Color textColor{}, itemColor{}, itemHighlightedColor{}, itemSelectedColor{};
    return in.ReadUInt64(fontAssetId) && in.ReadFloat(fontSize) && Read(in, textColor) && Read(in, itemColor) &&
        Read(in, itemHighlightedColor) && Read(in, itemSelectedColor);
}

bool ReadValue(Reader& in, UIProgressBar& v) { return in.ReadFloat(v.minimum)&&in.ReadFloat(v.maximum)&&in.ReadFloat(v.value); }
// v39 added the direction and fill widget.
bool ReadValue(Reader& in, UIProgressBar& v, std::uint32_t fileVersion) {
    return ReadValue(in, v) && (fileVersion < 39U || (Read(in,v.direction)&&in.ReadUInt64(v.fillRect)));
}
bool ReadValue(Reader& in, UIWidgetSwitcher& v) { return in.ReadUInt32(v.visibleChildIndex); }

// Every component whose payload never changed shape reads the same way at any version; the
// one that did takes the version explicitly through the overload below.
template <typename T> bool ReadValue(Reader& in, T& v, std::uint32_t) requires requires(Reader& r, T& t) { ReadValue(r, t); }
{ return ReadValue(in, v); }

#define KB_WRITE_FIELD(Field) Write(out, v.Field)
void WriteValue(std::vector<std::uint8_t>& out, const UIRectTransform& v) { KB_WRITE_FIELD(anchorMin);KB_WRITE_FIELD(anchorMax);KB_WRITE_FIELD(offsetMin);KB_WRITE_FIELD(offsetMax);KB_WRITE_FIELD(pivot);KB_WRITE_FIELD(scale);SceneAssetBinaryIO::WriteFloat(out,v.rotationDegrees);SceneAssetBinaryIO::WriteInt32(out,v.zOrder); }
void WriteValue(std::vector<std::uint8_t>& out, const UICanvas& v) { SceneAssetBinaryIO::WriteInt32(out,v.sortingOrder);SceneAssetBinaryIO::WriteBool(out,v.pixelPerfect);KB_WRITE_FIELD(renderMode);SceneAssetBinaryIO::WriteFloat(out,v.pixelsPerUnit);SceneAssetBinaryIO::WriteBool(out,v.respectSafeArea);SceneAssetBinaryIO::WriteInt32(out,v.player); }
void WriteValue(std::vector<std::uint8_t>& out, const UICanvasScaler& v) { KB_WRITE_FIELD(scaleMode);KB_WRITE_FIELD(referenceResolution);SceneAssetBinaryIO::WriteFloat(out,v.scaleFactor);SceneAssetBinaryIO::WriteFloat(out,v.matchWidthOrHeight); }
void WriteValue(std::vector<std::uint8_t>& out, const UICanvasGroup& v) { SceneAssetBinaryIO::WriteFloat(out,v.opacity);SceneAssetBinaryIO::WriteBool(out,v.interactable);SceneAssetBinaryIO::WriteBool(out,v.blocksRaycasts);SceneAssetBinaryIO::WriteBool(out,v.ignoreParentGroups);SceneAssetBinaryIO::WriteBool(out,v.visible);SceneAssetBinaryIO::WriteFloat(out,v.transitionSeconds);KB_WRITE_FIELD(hiddenOffset);SceneAssetBinaryIO::WriteFloat(out,v.hiddenScale); }
void WriteValue(std::vector<std::uint8_t>& out, const UIHorizontalLayout& v) { KB_WRITE_FIELD(padding);SceneAssetBinaryIO::WriteFloat(out,v.spacing);KB_WRITE_FIELD(horizontalAlignment);KB_WRITE_FIELD(verticalAlignment);SceneAssetBinaryIO::WriteBool(out,v.controlChildWidth);SceneAssetBinaryIO::WriteBool(out,v.controlChildHeight);SceneAssetBinaryIO::WriteBool(out,v.expandChildWidth);SceneAssetBinaryIO::WriteBool(out,v.expandChildHeight); }
void WriteValue(std::vector<std::uint8_t>& out, const UIVerticalLayout& v) { KB_WRITE_FIELD(padding);SceneAssetBinaryIO::WriteFloat(out,v.spacing);KB_WRITE_FIELD(horizontalAlignment);KB_WRITE_FIELD(verticalAlignment);SceneAssetBinaryIO::WriteBool(out,v.controlChildWidth);SceneAssetBinaryIO::WriteBool(out,v.controlChildHeight);SceneAssetBinaryIO::WriteBool(out,v.expandChildWidth);SceneAssetBinaryIO::WriteBool(out,v.expandChildHeight); }
void WriteValue(std::vector<std::uint8_t>& out, const UIGridLayout& v) { KB_WRITE_FIELD(padding);KB_WRITE_FIELD(spacing);KB_WRITE_FIELD(cellSize);SceneAssetBinaryIO::WriteUInt32(out,v.columns);KB_WRITE_FIELD(horizontalAlignment);KB_WRITE_FIELD(verticalAlignment); }
void WriteValue(std::vector<std::uint8_t>& out, const UIWrapLayout& v) { KB_WRITE_FIELD(padding);KB_WRITE_FIELD(spacing);KB_WRITE_FIELD(horizontalAlignment);KB_WRITE_FIELD(verticalAlignment); }
void WriteValue(std::vector<std::uint8_t>& out, const UIOverlayLayout& v) { KB_WRITE_FIELD(padding);KB_WRITE_FIELD(horizontalAlignment);KB_WRITE_FIELD(verticalAlignment); }
void WriteValue(std::vector<std::uint8_t>& out, const UILayoutElement& v) { SceneAssetBinaryIO::WriteFloat(out,v.minimumWidth);SceneAssetBinaryIO::WriteFloat(out,v.minimumHeight);SceneAssetBinaryIO::WriteFloat(out,v.preferredWidth);SceneAssetBinaryIO::WriteFloat(out,v.preferredHeight);SceneAssetBinaryIO::WriteFloat(out,v.flexibleWidth);SceneAssetBinaryIO::WriteFloat(out,v.flexibleHeight);SceneAssetBinaryIO::WriteInt32(out,v.layoutPriority);SceneAssetBinaryIO::WriteBool(out,v.ignoreLayout); }
void WriteValue(std::vector<std::uint8_t>& out, const UIContentSizeFitter& v) { KB_WRITE_FIELD(horizontalFit);KB_WRITE_FIELD(verticalFit); }
void WriteValue(std::vector<std::uint8_t>& out, const UIAspectRatioFitter& v) { KB_WRITE_FIELD(mode);SceneAssetBinaryIO::WriteFloat(out,v.aspectRatio); }
void WriteValue(std::vector<std::uint8_t>& out, const UISprite& v) { SceneAssetBinaryIO::WriteUInt64(out,v.spriteAssetId);KB_WRITE_FIELD(color);SceneAssetBinaryIO::WriteBool(out,v.preserveAspect); }
void WriteValue(std::vector<std::uint8_t>& out, const UIImage& v) { SceneAssetBinaryIO::WriteUInt64(out,v.imageAssetId);KB_WRITE_FIELD(uvRect);KB_WRITE_FIELD(scaleMode);SceneAssetBinaryIO::WriteBool(out,v.preserveAspect);KB_WRITE_FIELD(nineSlice);KB_WRITE_FIELD(color);KB_WRITE_FIELD(fillMethod);KB_WRITE_FIELD(fillOrigin);SceneAssetBinaryIO::WriteFloat(out,v.fillAmount);SceneAssetBinaryIO::WriteBool(out,v.fillClockwise);SceneAssetBinaryIO::WriteFloat(out,v.alphaHitThreshold); }
void WriteValue(std::vector<std::uint8_t>& out, const UIRawImage& v) { SceneAssetBinaryIO::WriteUInt64(out,v.imageAssetId);KB_WRITE_FIELD(uvRect);KB_WRITE_FIELD(color); }
void WriteValue(std::vector<std::uint8_t>& out, const UIText& v) { SceneAssetBinaryIO::WriteString(out,UITextContent(v));SceneAssetBinaryIO::WriteUInt64(out,v.fontAssetId);SceneAssetBinaryIO::WriteFloat(out,v.fontSize);KB_WRITE_FIELD(color);KB_WRITE_FIELD(horizontalAlignment);KB_WRITE_FIELD(verticalAlignment);KB_WRITE_FIELD(wrapMode);SceneAssetBinaryIO::WriteFloat(out,v.lineSpacing);SceneAssetBinaryIO::WriteBool(out,v.richText);SceneAssetBinaryIO::WriteString(out,UITextLocalizationKey(v));KB_WRITE_FIELD(overflow);SceneAssetBinaryIO::WriteBool(out,v.autoSize);SceneAssetBinaryIO::WriteFloat(out,v.minFontSize);SceneAssetBinaryIO::WriteUInt32(out,v.maxLines);SceneAssetBinaryIO::WriteFloat(out,v.characterSpacing); }
void WriteValue(std::vector<std::uint8_t>& out, const UIBorder& v) { KB_WRITE_FIELD(backgroundColor);KB_WRITE_FIELD(borderColor);KB_WRITE_FIELD(borderWidth);KB_WRITE_FIELD(cornerRadius);SceneAssetBinaryIO::WriteFloat(out,v.opacity); }
void WriteValue(std::vector<std::uint8_t>& out, const UIMask& v) { SceneAssetBinaryIO::WriteBool(out,v.showGraphic);SceneAssetBinaryIO::WriteFloat(out,v.softness); }
void WriteValue(std::vector<std::uint8_t>& out, const UIShadow& v) { KB_WRITE_FIELD(offset);KB_WRITE_FIELD(color);SceneAssetBinaryIO::WriteFloat(out,v.blur); }
void WriteValue(std::vector<std::uint8_t>& out, const UIOutline& v) { KB_WRITE_FIELD(color);SceneAssetBinaryIO::WriteFloat(out,v.width); }
void WriteValue(std::vector<std::uint8_t>& out, const UIBackgroundBlur& v) { SceneAssetBinaryIO::WriteFloat(out,v.radius); }
void WriteValue(std::vector<std::uint8_t>& out, const UISelectable& v) { SceneAssetBinaryIO::WriteBool(out,v.raycastTarget);SceneAssetBinaryIO::WriteBool(out,v.interactable);KB_WRITE_FIELD(navigationMode);SceneAssetBinaryIO::WriteUInt64(out,v.navigationUp);SceneAssetBinaryIO::WriteUInt64(out,v.navigationDown);SceneAssetBinaryIO::WriteUInt64(out,v.navigationLeft);SceneAssetBinaryIO::WriteUInt64(out,v.navigationRight);SceneAssetBinaryIO::WriteString(out,UIEventName(v));KB_WRITE_FIELD(normalColor);KB_WRITE_FIELD(highlightedColor);KB_WRITE_FIELD(pressedColor);KB_WRITE_FIELD(selectedColor);KB_WRITE_FIELD(disabledColor);SceneAssetBinaryIO::WriteFloat(out,v.colorFadeSeconds);SceneAssetBinaryIO::WriteUInt64(out,v.targetGraphic);KB_WRITE_FIELD(transition);SceneAssetBinaryIO::WriteUInt64(out,v.highlightedImage);SceneAssetBinaryIO::WriteUInt64(out,v.pressedImage);SceneAssetBinaryIO::WriteUInt64(out,v.selectedImage);SceneAssetBinaryIO::WriteUInt64(out,v.disabledImage);KB_WRITE_FIELD(raycastPadding);SceneAssetBinaryIO::WriteString(out,UITooltipText(v));SceneAssetBinaryIO::WriteBool(out,v.draggable);SceneAssetBinaryIO::WriteUInt64(out,v.eventTarget); }
void WriteValue(std::vector<std::uint8_t>& out, const UIButton& v) { SceneAssetBinaryIO::WriteBool(out,v.submitOnRelease); }
void WriteValue(std::vector<std::uint8_t>& out, const UIToggle& v) { SceneAssetBinaryIO::WriteBool(out,v.toggled);SceneAssetBinaryIO::WriteUInt64(out,v.graphic);SceneAssetBinaryIO::WriteUInt64(out,v.group);SceneAssetBinaryIO::WriteBool(out,v.allowSwitchOff); }
void WriteValue(std::vector<std::uint8_t>& out, const UISlider& v) { SceneAssetBinaryIO::WriteFloat(out,v.minimum);SceneAssetBinaryIO::WriteFloat(out,v.maximum);SceneAssetBinaryIO::WriteFloat(out,v.value);KB_WRITE_FIELD(direction);SceneAssetBinaryIO::WriteBool(out,v.wholeNumbers);SceneAssetBinaryIO::WriteUInt64(out,v.fillRect);SceneAssetBinaryIO::WriteUInt64(out,v.handleRect); }
void WriteValue(std::vector<std::uint8_t>& out, const UIScrollbar& v) { SceneAssetBinaryIO::WriteFloat(out,v.value);SceneAssetBinaryIO::WriteFloat(out,v.size);KB_WRITE_FIELD(direction); }
void WriteValue(std::vector<std::uint8_t>& out, const UIScrollView& v) { SceneAssetBinaryIO::WriteFloat(out,v.scrollX);SceneAssetBinaryIO::WriteFloat(out,v.scrollY);SceneAssetBinaryIO::WriteFloat(out,v.scrollSensitivity);SceneAssetBinaryIO::WriteBool(out,v.horizontal);SceneAssetBinaryIO::WriteBool(out,v.vertical);SceneAssetBinaryIO::WriteBool(out,v.inertia);SceneAssetBinaryIO::WriteUInt64(out,v.verticalScrollbar);SceneAssetBinaryIO::WriteUInt64(out,v.horizontalScrollbar);KB_WRITE_FIELD(movementType);SceneAssetBinaryIO::WriteFloat(out,v.elasticity);SceneAssetBinaryIO::WriteBool(out,v.snapToChildren); }
void WriteValue(std::vector<std::uint8_t>& out, const UIInputField& v) { SceneAssetBinaryIO::WriteUInt32(out,v.characterLimit);SceneAssetBinaryIO::WriteBool(out,v.multiline);SceneAssetBinaryIO::WriteBool(out,v.readOnly);KB_WRITE_FIELD(contentType);SceneAssetBinaryIO::WriteString(out,UIInputPlaceholder(v)); }
void WriteValue(std::vector<std::uint8_t>& out, const UIDropdown& v) {
    SceneAssetBinaryIO::WriteUInt64(out, v.templateEntity);
    SceneAssetBinaryIO::WriteUInt64(out, v.captionText);
    SceneAssetBinaryIO::WriteUInt64(out, v.captionImage);
    SceneAssetBinaryIO::WriteUInt64(out, v.itemText);
    SceneAssetBinaryIO::WriteUInt64(out, v.itemImage);
    SceneAssetBinaryIO::WriteUInt32(out, v.value);
    SceneAssetBinaryIO::WriteFloat(out, v.alphaFadeSpeed);
    SceneAssetBinaryIO::WriteUInt32(out, v.optionCount);
    for (std::uint32_t index = 0U; index < v.optionCount; ++index) {
        SceneAssetBinaryIO::WriteString(out, UIDropdownOptionText(v.options[index]));
        SceneAssetBinaryIO::WriteUInt64(out, v.options[index].imageAssetId);
    }
}
void WriteValue(std::vector<std::uint8_t>& out, const UIProgressBar& v) { SceneAssetBinaryIO::WriteFloat(out,v.minimum);SceneAssetBinaryIO::WriteFloat(out,v.maximum);SceneAssetBinaryIO::WriteFloat(out,v.value);KB_WRITE_FIELD(direction);SceneAssetBinaryIO::WriteUInt64(out,v.fillRect); }
void WriteValue(std::vector<std::uint8_t>& out, const UIWidgetSwitcher& v) { SceneAssetBinaryIO::WriteUInt32(out,v.visibleChildIndex); }
#undef KB_WRITE_FIELD

#define KB_COMPONENTS(X) \
 X(RectTransform,UIRectTransform,rectTransform) X(Canvas,UICanvas,canvas) X(CanvasScaler,UICanvasScaler,canvasScaler) X(CanvasGroup,UICanvasGroup,canvasGroup) \
 X(HorizontalLayout,UIHorizontalLayout,horizontalLayout) X(VerticalLayout,UIVerticalLayout,verticalLayout) X(GridLayout,UIGridLayout,gridLayout) X(WrapLayout,UIWrapLayout,wrapLayout) X(OverlayLayout,UIOverlayLayout,overlayLayout) \
 X(LayoutElement,UILayoutElement,layoutElement) X(ContentSizeFitter,UIContentSizeFitter,contentSizeFitter) X(AspectRatioFitter,UIAspectRatioFitter,aspectRatioFitter) \
 X(Sprite,UISprite,sprite) X(Image,UIImage,image) X(RawImage,UIRawImage,rawImage) X(Text,UIText,text) X(Border,UIBorder,border) X(Mask,UIMask,mask) X(Shadow,UIShadow,shadow) X(Outline,UIOutline,outline) X(BackgroundBlur,UIBackgroundBlur,backgroundBlur) \
 X(Selectable,UISelectable,selectable) X(Button,UIButton,button) X(Toggle,UIToggle,toggle) X(Slider,UISlider,slider) X(Scrollbar,UIScrollbar,scrollbar) X(ScrollView,UIScrollView,scrollView) X(InputField,UIInputField,inputField) X(Dropdown,UIDropdown,dropdown) X(ProgressBar,UIProgressBar,progressBar) X(WidgetSwitcher,UIWidgetSwitcher,widgetSwitcher)

} // namespace

bool SceneAssetUIComponentCodec::Read(Reader& input, std::uint32_t fileVersion, UIComponentSet& output) {
    std::uint64_t bits = 0U;
    if (!input.ReadUInt64(bits) || (bits & ~KnownBits) != 0U) return false;
    UIComponentSet decoded;
#define KB_READ(Type, Component, Field) if ((bits & Bit(UIComponentType::Type)) != 0U) { Component value{}; if (!ReadValue(input,value,fileVersion)) return false; decoded.Field=value; }
    KB_COMPONENTS(KB_READ)
#undef KB_READ
    if (!IsUIComponentSetValid(decoded)) return false;
    output = std::move(decoded);
    return true;
}

void SceneAssetUIComponentCodec::Write(std::vector<std::uint8_t>& output, const UIComponentSet& components) {
    if (!IsUIComponentSetValid(components)) throw std::invalid_argument("Cannot persist invalid UI components");
    std::uint64_t bits = 0U;
#define KB_BIT(Type, Component, Field) if (components.Field) bits |= Bit(UIComponentType::Type);
    KB_COMPONENTS(KB_BIT)
#undef KB_BIT
    SceneAssetBinaryIO::WriteUInt64(output,bits);
#define KB_WRITE(Type, Component, Field) if (components.Field) WriteValue(output,*components.Field);
    KB_COMPONENTS(KB_WRITE)
#undef KB_WRITE
}

#undef KB_COMPONENTS

} // namespace kb::scene
