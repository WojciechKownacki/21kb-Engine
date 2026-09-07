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
bool ReadValue(Reader& in, UICanvasScaler& v) { return Read(in,v.scaleMode)&&Read(in,v.referenceResolution)&&in.ReadFloat(v.scaleFactor)&&in.ReadFloat(v.matchWidthOrHeight); }
bool ReadValue(Reader& in, UICanvasGroup& v) { return in.ReadFloat(v.opacity)&&in.ReadBool(v.interactable)&&in.ReadBool(v.blocksRaycasts)&&in.ReadBool(v.ignoreParentGroups); }
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
bool ReadValue(Reader& in, UIRawImage& v) { return in.ReadUInt64(v.imageAssetId)&&Read(in,v.uvRect)&&Read(in,v.color); }
bool ReadValue(Reader& in, UIText& v) { std::string text; return in.ReadString(text,static_cast<std::uint32_t>(UIText::MaxUtf8Bytes-1U))&&SetUITextContent(v,text)&&in.ReadUInt64(v.fontAssetId)&&in.ReadFloat(v.fontSize)&&Read(in,v.color)&&Read(in,v.horizontalAlignment)&&Read(in,v.verticalAlignment)&&Read(in,v.wrapMode)&&in.ReadFloat(v.lineSpacing)&&in.ReadBool(v.richText); }
bool ReadValue(Reader& in, UIBorder& v) { return Read(in,v.backgroundColor)&&Read(in,v.borderColor)&&Read(in,v.borderWidth)&&Read(in,v.cornerRadius)&&in.ReadFloat(v.opacity); }
bool ReadValue(Reader& in, UIMask& v) { return in.ReadBool(v.showGraphic); }
bool ReadValue(Reader& in, UIShadow& v) { return Read(in,v.offset)&&Read(in,v.color)&&in.ReadFloat(v.blur); }
bool ReadValue(Reader& in, UIOutline& v) { return Read(in,v.color)&&in.ReadFloat(v.width); }
bool ReadValue(Reader& in, UIBackgroundBlur& v) { return in.ReadFloat(v.radius); }
bool ReadValue(Reader& in, UISelectable& v) { std::string name; return in.ReadBool(v.raycastTarget)&&in.ReadBool(v.interactable)&&Read(in,v.navigationMode)&&in.ReadUInt64(v.navigationUp)&&in.ReadUInt64(v.navigationDown)&&in.ReadUInt64(v.navigationLeft)&&in.ReadUInt64(v.navigationRight)&&in.ReadString(name,static_cast<std::uint32_t>(UISelectable::MaxEventNameBytes-1U))&&SetUIEventName(v,name)&&Read(in,v.normalColor)&&Read(in,v.highlightedColor)&&Read(in,v.pressedColor)&&Read(in,v.selectedColor)&&Read(in,v.disabledColor)&&in.ReadFloat(v.colorFadeSeconds); }
bool ReadValue(Reader& in, UIButton& v) { return in.ReadBool(v.submitOnRelease); }
bool ReadValue(Reader& in, UIToggle& v) { return in.ReadBool(v.toggled); }
bool ReadValue(Reader& in, UISlider& v) { return in.ReadFloat(v.minimum)&&in.ReadFloat(v.maximum)&&in.ReadFloat(v.value)&&Read(in,v.direction)&&in.ReadBool(v.wholeNumbers); }
bool ReadValue(Reader& in, UIScrollbar& v) { return in.ReadFloat(v.value)&&in.ReadFloat(v.size)&&Read(in,v.direction); }
bool ReadValue(Reader& in, UIScrollView& v) { return in.ReadFloat(v.scrollX)&&in.ReadFloat(v.scrollY)&&in.ReadFloat(v.scrollSensitivity)&&in.ReadBool(v.horizontal)&&in.ReadBool(v.vertical)&&in.ReadBool(v.inertia); }
bool ReadValue(Reader& in, UIInputField& v) { return in.ReadUInt32(v.characterLimit)&&in.ReadBool(v.multiline)&&in.ReadBool(v.readOnly); }
bool ReadValue(Reader& in, UIDropdown& v) { return in.ReadUInt32(v.selectedIndex); }
bool ReadValue(Reader& in, UIProgressBar& v) { return in.ReadFloat(v.minimum)&&in.ReadFloat(v.maximum)&&in.ReadFloat(v.value); }
bool ReadValue(Reader& in, UIWidgetSwitcher& v) { return in.ReadUInt32(v.visibleChildIndex); }

#define KB_WRITE_FIELD(Field) Write(out, v.Field)
void WriteValue(std::vector<std::uint8_t>& out, const UIRectTransform& v) { KB_WRITE_FIELD(anchorMin);KB_WRITE_FIELD(anchorMax);KB_WRITE_FIELD(offsetMin);KB_WRITE_FIELD(offsetMax);KB_WRITE_FIELD(pivot);KB_WRITE_FIELD(scale);SceneAssetBinaryIO::WriteFloat(out,v.rotationDegrees);SceneAssetBinaryIO::WriteInt32(out,v.zOrder); }
void WriteValue(std::vector<std::uint8_t>& out, const UICanvas& v) { SceneAssetBinaryIO::WriteInt32(out,v.sortingOrder);SceneAssetBinaryIO::WriteBool(out,v.pixelPerfect); }
void WriteValue(std::vector<std::uint8_t>& out, const UICanvasScaler& v) { KB_WRITE_FIELD(scaleMode);KB_WRITE_FIELD(referenceResolution);SceneAssetBinaryIO::WriteFloat(out,v.scaleFactor);SceneAssetBinaryIO::WriteFloat(out,v.matchWidthOrHeight); }
void WriteValue(std::vector<std::uint8_t>& out, const UICanvasGroup& v) { SceneAssetBinaryIO::WriteFloat(out,v.opacity);SceneAssetBinaryIO::WriteBool(out,v.interactable);SceneAssetBinaryIO::WriteBool(out,v.blocksRaycasts);SceneAssetBinaryIO::WriteBool(out,v.ignoreParentGroups); }
void WriteValue(std::vector<std::uint8_t>& out, const UIHorizontalLayout& v) { KB_WRITE_FIELD(padding);SceneAssetBinaryIO::WriteFloat(out,v.spacing);KB_WRITE_FIELD(horizontalAlignment);KB_WRITE_FIELD(verticalAlignment);SceneAssetBinaryIO::WriteBool(out,v.controlChildWidth);SceneAssetBinaryIO::WriteBool(out,v.controlChildHeight);SceneAssetBinaryIO::WriteBool(out,v.expandChildWidth);SceneAssetBinaryIO::WriteBool(out,v.expandChildHeight); }
void WriteValue(std::vector<std::uint8_t>& out, const UIVerticalLayout& v) { KB_WRITE_FIELD(padding);SceneAssetBinaryIO::WriteFloat(out,v.spacing);KB_WRITE_FIELD(horizontalAlignment);KB_WRITE_FIELD(verticalAlignment);SceneAssetBinaryIO::WriteBool(out,v.controlChildWidth);SceneAssetBinaryIO::WriteBool(out,v.controlChildHeight);SceneAssetBinaryIO::WriteBool(out,v.expandChildWidth);SceneAssetBinaryIO::WriteBool(out,v.expandChildHeight); }
void WriteValue(std::vector<std::uint8_t>& out, const UIGridLayout& v) { KB_WRITE_FIELD(padding);KB_WRITE_FIELD(spacing);KB_WRITE_FIELD(cellSize);SceneAssetBinaryIO::WriteUInt32(out,v.columns);KB_WRITE_FIELD(horizontalAlignment);KB_WRITE_FIELD(verticalAlignment); }
void WriteValue(std::vector<std::uint8_t>& out, const UIWrapLayout& v) { KB_WRITE_FIELD(padding);KB_WRITE_FIELD(spacing);KB_WRITE_FIELD(horizontalAlignment);KB_WRITE_FIELD(verticalAlignment); }
void WriteValue(std::vector<std::uint8_t>& out, const UIOverlayLayout& v) { KB_WRITE_FIELD(padding);KB_WRITE_FIELD(horizontalAlignment);KB_WRITE_FIELD(verticalAlignment); }
void WriteValue(std::vector<std::uint8_t>& out, const UILayoutElement& v) { SceneAssetBinaryIO::WriteFloat(out,v.minimumWidth);SceneAssetBinaryIO::WriteFloat(out,v.minimumHeight);SceneAssetBinaryIO::WriteFloat(out,v.preferredWidth);SceneAssetBinaryIO::WriteFloat(out,v.preferredHeight);SceneAssetBinaryIO::WriteFloat(out,v.flexibleWidth);SceneAssetBinaryIO::WriteFloat(out,v.flexibleHeight);SceneAssetBinaryIO::WriteInt32(out,v.layoutPriority);SceneAssetBinaryIO::WriteBool(out,v.ignoreLayout); }
void WriteValue(std::vector<std::uint8_t>& out, const UIContentSizeFitter& v) { KB_WRITE_FIELD(horizontalFit);KB_WRITE_FIELD(verticalFit); }
void WriteValue(std::vector<std::uint8_t>& out, const UIAspectRatioFitter& v) { KB_WRITE_FIELD(mode);SceneAssetBinaryIO::WriteFloat(out,v.aspectRatio); }
void WriteValue(std::vector<std::uint8_t>& out, const UISprite& v) { SceneAssetBinaryIO::WriteUInt64(out,v.spriteAssetId);KB_WRITE_FIELD(color);SceneAssetBinaryIO::WriteBool(out,v.preserveAspect); }
void WriteValue(std::vector<std::uint8_t>& out, const UIImage& v) { SceneAssetBinaryIO::WriteUInt64(out,v.imageAssetId);KB_WRITE_FIELD(uvRect);KB_WRITE_FIELD(scaleMode);SceneAssetBinaryIO::WriteBool(out,v.preserveAspect);KB_WRITE_FIELD(nineSlice);KB_WRITE_FIELD(color); }
void WriteValue(std::vector<std::uint8_t>& out, const UIRawImage& v) { SceneAssetBinaryIO::WriteUInt64(out,v.imageAssetId);KB_WRITE_FIELD(uvRect);KB_WRITE_FIELD(color); }
void WriteValue(std::vector<std::uint8_t>& out, const UIText& v) { SceneAssetBinaryIO::WriteString(out,UITextContent(v));SceneAssetBinaryIO::WriteUInt64(out,v.fontAssetId);SceneAssetBinaryIO::WriteFloat(out,v.fontSize);KB_WRITE_FIELD(color);KB_WRITE_FIELD(horizontalAlignment);KB_WRITE_FIELD(verticalAlignment);KB_WRITE_FIELD(wrapMode);SceneAssetBinaryIO::WriteFloat(out,v.lineSpacing);SceneAssetBinaryIO::WriteBool(out,v.richText); }
void WriteValue(std::vector<std::uint8_t>& out, const UIBorder& v) { KB_WRITE_FIELD(backgroundColor);KB_WRITE_FIELD(borderColor);KB_WRITE_FIELD(borderWidth);KB_WRITE_FIELD(cornerRadius);SceneAssetBinaryIO::WriteFloat(out,v.opacity); }
void WriteValue(std::vector<std::uint8_t>& out, const UIMask& v) { SceneAssetBinaryIO::WriteBool(out,v.showGraphic); }
void WriteValue(std::vector<std::uint8_t>& out, const UIShadow& v) { KB_WRITE_FIELD(offset);KB_WRITE_FIELD(color);SceneAssetBinaryIO::WriteFloat(out,v.blur); }
void WriteValue(std::vector<std::uint8_t>& out, const UIOutline& v) { KB_WRITE_FIELD(color);SceneAssetBinaryIO::WriteFloat(out,v.width); }
void WriteValue(std::vector<std::uint8_t>& out, const UIBackgroundBlur& v) { SceneAssetBinaryIO::WriteFloat(out,v.radius); }
void WriteValue(std::vector<std::uint8_t>& out, const UISelectable& v) { SceneAssetBinaryIO::WriteBool(out,v.raycastTarget);SceneAssetBinaryIO::WriteBool(out,v.interactable);KB_WRITE_FIELD(navigationMode);SceneAssetBinaryIO::WriteUInt64(out,v.navigationUp);SceneAssetBinaryIO::WriteUInt64(out,v.navigationDown);SceneAssetBinaryIO::WriteUInt64(out,v.navigationLeft);SceneAssetBinaryIO::WriteUInt64(out,v.navigationRight);SceneAssetBinaryIO::WriteString(out,UIEventName(v));KB_WRITE_FIELD(normalColor);KB_WRITE_FIELD(highlightedColor);KB_WRITE_FIELD(pressedColor);KB_WRITE_FIELD(selectedColor);KB_WRITE_FIELD(disabledColor);SceneAssetBinaryIO::WriteFloat(out,v.colorFadeSeconds); }
void WriteValue(std::vector<std::uint8_t>& out, const UIButton& v) { SceneAssetBinaryIO::WriteBool(out,v.submitOnRelease); }
void WriteValue(std::vector<std::uint8_t>& out, const UIToggle& v) { SceneAssetBinaryIO::WriteBool(out,v.toggled); }
void WriteValue(std::vector<std::uint8_t>& out, const UISlider& v) { SceneAssetBinaryIO::WriteFloat(out,v.minimum);SceneAssetBinaryIO::WriteFloat(out,v.maximum);SceneAssetBinaryIO::WriteFloat(out,v.value);KB_WRITE_FIELD(direction);SceneAssetBinaryIO::WriteBool(out,v.wholeNumbers); }
void WriteValue(std::vector<std::uint8_t>& out, const UIScrollbar& v) { SceneAssetBinaryIO::WriteFloat(out,v.value);SceneAssetBinaryIO::WriteFloat(out,v.size);KB_WRITE_FIELD(direction); }
void WriteValue(std::vector<std::uint8_t>& out, const UIScrollView& v) { SceneAssetBinaryIO::WriteFloat(out,v.scrollX);SceneAssetBinaryIO::WriteFloat(out,v.scrollY);SceneAssetBinaryIO::WriteFloat(out,v.scrollSensitivity);SceneAssetBinaryIO::WriteBool(out,v.horizontal);SceneAssetBinaryIO::WriteBool(out,v.vertical);SceneAssetBinaryIO::WriteBool(out,v.inertia); }
void WriteValue(std::vector<std::uint8_t>& out, const UIInputField& v) { SceneAssetBinaryIO::WriteUInt32(out,v.characterLimit);SceneAssetBinaryIO::WriteBool(out,v.multiline);SceneAssetBinaryIO::WriteBool(out,v.readOnly); }
void WriteValue(std::vector<std::uint8_t>& out, const UIDropdown& v) { SceneAssetBinaryIO::WriteUInt32(out,v.selectedIndex); }
void WriteValue(std::vector<std::uint8_t>& out, const UIProgressBar& v) { SceneAssetBinaryIO::WriteFloat(out,v.minimum);SceneAssetBinaryIO::WriteFloat(out,v.maximum);SceneAssetBinaryIO::WriteFloat(out,v.value); }
void WriteValue(std::vector<std::uint8_t>& out, const UIWidgetSwitcher& v) { SceneAssetBinaryIO::WriteUInt32(out,v.visibleChildIndex); }
#undef KB_WRITE_FIELD

#define KB_COMPONENTS(X) \
 X(RectTransform,UIRectTransform,rectTransform) X(Canvas,UICanvas,canvas) X(CanvasScaler,UICanvasScaler,canvasScaler) X(CanvasGroup,UICanvasGroup,canvasGroup) \
 X(HorizontalLayout,UIHorizontalLayout,horizontalLayout) X(VerticalLayout,UIVerticalLayout,verticalLayout) X(GridLayout,UIGridLayout,gridLayout) X(WrapLayout,UIWrapLayout,wrapLayout) X(OverlayLayout,UIOverlayLayout,overlayLayout) \
 X(LayoutElement,UILayoutElement,layoutElement) X(ContentSizeFitter,UIContentSizeFitter,contentSizeFitter) X(AspectRatioFitter,UIAspectRatioFitter,aspectRatioFitter) \
 X(Sprite,UISprite,sprite) X(Image,UIImage,image) X(RawImage,UIRawImage,rawImage) X(Text,UIText,text) X(Border,UIBorder,border) X(Mask,UIMask,mask) X(Shadow,UIShadow,shadow) X(Outline,UIOutline,outline) X(BackgroundBlur,UIBackgroundBlur,backgroundBlur) \
 X(Selectable,UISelectable,selectable) X(Button,UIButton,button) X(Toggle,UIToggle,toggle) X(Slider,UISlider,slider) X(Scrollbar,UIScrollbar,scrollbar) X(ScrollView,UIScrollView,scrollView) X(InputField,UIInputField,inputField) X(Dropdown,UIDropdown,dropdown) X(ProgressBar,UIProgressBar,progressBar) X(WidgetSwitcher,UIWidgetSwitcher,widgetSwitcher)

} // namespace

bool SceneAssetUIComponentCodec::Read(Reader& input, UIComponentSet& output) {
    std::uint64_t bits = 0U;
    if (!input.ReadUInt64(bits) || (bits & ~KnownBits) != 0U) return false;
    UIComponentSet decoded;
#define KB_READ(Type, Component, Field) if ((bits & Bit(UIComponentType::Type)) != 0U) { Component value{}; if (!ReadValue(input,value)) return false; decoded.Field=value; }
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
