#include "engine/scene/SceneUI.hpp"

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputText.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneLocalization.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneVisibilityResolution.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefabInstantiationSettings.hpp"
#include "engine/scene/ScenePrefabInstance.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/ui/UIComponentValidation.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/entities/SceneEntityCounter.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace kb::scene {
namespace {

using kb::math::Rect;
using kb::math::Vec2;

constexpr std::array kEventDescriptors{
    SceneUIEventDescriptor{SceneUIEventType::HoverEntered, "OnUIHoverEntered"},
    SceneUIEventDescriptor{SceneUIEventType::HoverExited, "OnUIHoverExited"},
    SceneUIEventDescriptor{SceneUIEventType::Pressed, "OnUIPressed"},
    SceneUIEventDescriptor{SceneUIEventType::Released, "OnUIReleased"},
    SceneUIEventDescriptor{SceneUIEventType::Clicked, "OnUIClicked"},
    SceneUIEventDescriptor{SceneUIEventType::Focused, "OnUIFocused"},
    SceneUIEventDescriptor{SceneUIEventType::Blurred, "OnUIBlurred"},
    SceneUIEventDescriptor{SceneUIEventType::Submitted, "OnUISubmitted"},
    SceneUIEventDescriptor{SceneUIEventType::Canceled, "OnUICanceled"},
    SceneUIEventDescriptor{SceneUIEventType::Changed, "OnUIChanged"},
    SceneUIEventDescriptor{SceneUIEventType::DragBegan, "OnUIDragBegan"},
    SceneUIEventDescriptor{SceneUIEventType::Dragged, "OnUIDragged"},
    SceneUIEventDescriptor{SceneUIEventType::DragEnded, "OnUIDragEnded"},
    SceneUIEventDescriptor{SceneUIEventType::Dropped, "OnUIDropped"},
};

struct GroupState {
    float opacity = 1.0F;
    bool interactable = true;
    bool blocksRaycasts = true;
};

struct DesiredSize {
    float width = 0.0F;
    float height = 0.0F;
};

struct Affine2D {
    float m00 = 1.0F;
    float m01 = 0.0F;
    float m10 = 0.0F;
    float m11 = 1.0F;
    float tx = 0.0F;
    float ty = 0.0F;
};

// The open dropdown list and the full-screen blocker under it sort above the canvas they belong to
// by this much, so the list covers every widget of that canvas and the blocker covers everything else.
constexpr std::int32_t kUIDropdownListSortingOffset = 30000;
// A tooltip bubble sorts over every canvas, including an open dropdown list.
constexpr std::int32_t kUITooltipSortingOrder = std::numeric_limits<std::int32_t>::max();
// How long the pointer rests on a widget before its tooltip shows, and how the bubble is laid out, in
// canvas units: label size, padding around it and the gap from the pointer.
constexpr float kUITooltipDelaySeconds = 0.5F;
constexpr float kUITooltipFontSize = 14.0F;
constexpr float kUITooltipPadding = 8.0F;
constexpr kb::math::Vec2 kUITooltipPointerGap{12.0F, 20.0F};
// A press on a draggable widget becomes a drag once the pointer has moved this many pixels.
constexpr float kUIDragThresholdPixels = 6.0F;
// Rate at which a snapping scroll view closes on its child, per second.
constexpr float kUIScrollSnapRate = 14.0F;

// Held navigation: the first repeat waits long enough that a single press never double-steps,
// then steps at a rate a player can still stop on the row they want.
constexpr float kUINavigationRepeatDelaySeconds = 0.4F;
constexpr float kUINavigationRepeatIntervalSeconds = 0.1F;
// How far the left stick has to lean before it counts as a navigation press. Below this a resting
// or drifting stick must not walk focus around a menu.
constexpr float kUIStickNavigationThreshold = 0.5F;
// A focused slider moves by this fraction of its range per step unless it snaps to whole numbers.
constexpr float kUISliderNavigationStepFraction = 0.05F;
constexpr float kUIScrollbarNavigationStep = 0.1F;

// Full on/off cycle for the text caret. The engine has no system caret setting to read, so this
// is the one place the rate is defined.
constexpr float kUITextCaretBlinkPeriodSeconds = 1.0F;

[[nodiscard]] bool IsUITextCaretVisible(float phase) noexcept {
    return phase < kUITextCaretBlinkPeriodSeconds * 0.5F;
}

[[nodiscard]] Vec2 TransformPoint(const Affine2D& transform, Vec2 point) noexcept {
    return {transform.m00 * point.x + transform.m01 * point.y + transform.tx,
            transform.m10 * point.x + transform.m11 * point.y + transform.ty};
}

[[nodiscard]] Affine2D Compose(const Affine2D& parent, const Affine2D& child) noexcept {
    return {
        parent.m00 * child.m00 + parent.m01 * child.m10,
        parent.m00 * child.m01 + parent.m01 * child.m11,
        parent.m10 * child.m00 + parent.m11 * child.m10,
        parent.m10 * child.m01 + parent.m11 * child.m11,
        parent.m00 * child.tx + parent.m01 * child.ty + parent.tx,
        parent.m10 * child.tx + parent.m11 * child.ty + parent.ty,
    };
}

[[nodiscard]] Affine2D UniformScale(float scale) noexcept {
    return {.m00 = scale, .m11 = scale};
}

[[nodiscard]] Affine2D AuthoredTransform(Rect rect, const UIRectTransform& transform) noexcept {
    constexpr float radians = 0.01745329251994329577F;
    const float cosine = std::cos(transform.rotationDegrees * radians);
    const float sine = std::sin(transform.rotationDegrees * radians);
    const float m00 = cosine * transform.scale.x;
    const float m01 = -sine * transform.scale.y;
    const float m10 = sine * transform.scale.x;
    const float m11 = cosine * transform.scale.y;
    const Vec2 pivot{rect.x + rect.width * transform.pivot.x, rect.y + rect.height * transform.pivot.y};
    return {m00, m01, m10, m11, pivot.x - m00 * pivot.x - m01 * pivot.y,
            pivot.y - m10 * pivot.x - m11 * pivot.y};
}

[[nodiscard]] Rect Bounds(const std::array<Vec2, 4U>& corners) noexcept {
    float left = corners[0].x;
    float top = corners[0].y;
    float right = corners[0].x;
    float bottom = corners[0].y;
    for (const Vec2 corner : corners) {
        left = std::min(left, corner.x);
        top = std::min(top, corner.y);
        right = std::max(right, corner.x);
        bottom = std::max(bottom, corner.y);
    }
    return {left, top, std::max(0.0F, right - left), std::max(0.0F, bottom - top)};
}

[[nodiscard]] float Width(const UIRectTransform& rect) noexcept {
    return std::max(0.0F, rect.offsetMax.x - rect.offsetMin.x);
}
[[nodiscard]] float Height(const UIRectTransform& rect) noexcept {
    return std::max(0.0F, rect.offsetMax.y - rect.offsetMin.y);
}
[[nodiscard]] Rect Inner(Rect rect, UIEdges edges) noexcept {
    rect.x += edges.left;
    rect.y += edges.top;
    rect.width = std::max(0.0F, rect.width - edges.left - edges.right);
    rect.height = std::max(0.0F, rect.height - edges.top - edges.bottom);
    return rect;
}
[[nodiscard]] Rect Intersect(Rect a, Rect b) noexcept {
    const float left = std::max(a.x, b.x);
    const float top = std::max(a.y, b.y);
    const float right = std::min(a.x + a.width, b.x + b.width);
    const float bottom = std::min(a.y + a.height, b.y + b.height);
    return {left, top, std::max(0.0F, right - left), std::max(0.0F, bottom - top)};
}
[[nodiscard]] bool ContainsRect(Rect rect, Vec2 point) noexcept {
    return point.x >= rect.x && point.y >= rect.y && point.x <= rect.x + rect.width && point.y <= rect.y + rect.height;
}
[[nodiscard]] bool InQuad(const std::array<Vec2, 4U>& corners, Vec2 point) noexcept {
    float sign = 0.0F;
    for (std::size_t index = 0U; index < corners.size(); ++index) {
        const Vec2 a = corners[index];
        const Vec2 b = corners[(index + 1U) % corners.size()];
        const float cross = (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x);
        if (std::abs(cross) <= 0.0001F)
            continue;
        if (sign == 0.0F)
            sign = cross;
        else if ((sign < 0.0F) != (cross < 0.0F))
            return false;
    }
    return true;
}
[[nodiscard]] float CanvasScale(Vec2 viewport, const UICanvasScaler* scaler) noexcept {
    if (scaler == nullptr)
        return 1.0F;
    if (scaler->scaleMode == UICanvasScaleMode::ConstantPixelSize)
        return scaler->scaleFactor;
    const float widthRatio = viewport.x / scaler->referenceResolution.x;
    const float heightRatio = viewport.y / scaler->referenceResolution.y;
    return std::exp2(std::lerp(std::log2(widthRatio), std::log2(heightRatio), scaler->matchWidthOrHeight)) *
           scaler->scaleFactor;
}
[[nodiscard]] Rect AnchoredRect(const UIRectTransform& value, Rect parent) noexcept {
    const float left = parent.x + parent.width * value.anchorMin.x + value.offsetMin.x;
    const float top = parent.y + parent.height * value.anchorMin.y + value.offsetMin.y;
    const float right = parent.x + parent.width * value.anchorMax.x + value.offsetMax.x;
    const float bottom = parent.y + parent.height * value.anchorMax.y + value.offsetMax.y;
    return {left, top, std::max(0.0F, right - left), std::max(0.0F, bottom - top)};
}
[[nodiscard]] Rect ResizeAroundPivot(Rect rect, float width, float height, Vec2 pivot) noexcept {
    rect.x += (rect.width - width) * pivot.x;
    rect.y += (rect.height - height) * pivot.y;
    rect.width = std::max(0.0F, width);
    rect.height = std::max(0.0F, height);
    return rect;
}
[[nodiscard]] Rect ApplyAspect(Rect rect, const UIAspectRatioFitter* fitter, Vec2 pivot) noexcept {
    if (fitter == nullptr || fitter->mode == UIAspectFitMode::None)
        return rect;
    switch (fitter->mode) {
    case UIAspectFitMode::WidthControlsHeight:
        return ResizeAroundPivot(rect, rect.width, rect.width / fitter->aspectRatio, pivot);
    case UIAspectFitMode::HeightControlsWidth:
        return ResizeAroundPivot(rect, rect.height * fitter->aspectRatio, rect.height, pivot);
    case UIAspectFitMode::FitInsideParent:
        return rect.width / std::max(rect.height, 0.0001F) > fitter->aspectRatio
                   ? ResizeAroundPivot(rect, rect.height * fitter->aspectRatio, rect.height, pivot)
                   : ResizeAroundPivot(rect, rect.width, rect.width / fitter->aspectRatio, pivot);
    case UIAspectFitMode::EnvelopeParent:
        return rect.width / std::max(rect.height, 0.0001F) < fitter->aspectRatio
                   ? ResizeAroundPivot(rect, rect.height * fitter->aspectRatio, rect.height, pivot)
                   : ResizeAroundPivot(rect, rect.width, rect.width / fitter->aspectRatio, pivot);
    case UIAspectFitMode::None:
        break;
    }
    return rect;
}
[[nodiscard]] float AlignOffset(float available, float size, UIAlignment alignment) noexcept {
    if (alignment == UIAlignment::Center)
        return (available - size) * 0.5F;
    if (alignment == UIAlignment::End)
        return available - size;
    return 0.0F;
}
[[nodiscard]] float NormalizedRange(float minimum, float maximum, float value) noexcept {
    return maximum > minimum ? std::clamp((value - minimum) / (maximum - minimum), 0.0F, 1.0F)
                             : (value >= maximum ? 1.0F : 0.0F);
}
[[nodiscard]] std::size_t Utf8CodePointCount(std::string_view text) noexcept;
// Sets text, cutting it at the last whole character that fits - a translation may be longer than the
// authored text the component was sized for.
void SetTextClipped(UIText& text, std::string_view value) noexcept {
    if (value.size() >= UIText::MaxUtf8Bytes) {
        std::size_t end = UIText::MaxUtf8Bytes - 1U;
        while (end > 0U && (static_cast<unsigned char>(value[end]) & 0xC0U) == 0x80U)
            --end;
        value = value.substr(0U, end);
    }
    static_cast<void>(SetUITextContent(text, value));
}

[[nodiscard]] UIEdges ScaledEdges(UIEdges edges, float scale) noexcept {
    return {edges.left * scale, edges.top * scale, edges.right * scale, edges.bottom * scale};
}

[[nodiscard]] SceneUIEvent MakeEvent(SceneUIEventType type, SceneEntity entity, const UISelectable* selectable) {
    SceneUIEvent event{.type = type, .entity = entity, .actionTarget = entity};
    if (selectable != nullptr) {
        const std::string_view name = UIEventName(*selectable);
        std::copy(name.begin(), name.end(), event.name.begin());
        if (selectable->eventTarget != 0U)
            event.actionTarget = SceneEntity{selectable->eventTarget};
    }
    return event;
}

class FrameBuilder {
  public:
    FrameBuilder(const Scene& scene, Vec2 viewport)
        : scene_(scene), state_(SceneAccess::State(scene)), ui_(scene.Components().UI()), viewport_(viewport) {
        frame_.viewportSize = viewport;
    }

    [[nodiscard]] bool Build(SceneUIFrame& output) {
        const std::vector<SceneEntity> roots = scene_.Hierarchy().RootEntities();
        std::vector<SceneEntity> pending(roots.begin(), roots.end());
        while (!pending.empty()) {
            const SceneEntity entity = pending.back();
            pending.pop_back();
            if (const UIDropdown* dropdown = ui_.TryGet<UIDropdown>(entity)) {
                if (dropdown->captionText != 0U)
                    captionTexts_.emplace(dropdown->captionText, dropdown);
                if (dropdown->captionImage != 0U)
                    captionImages_.emplace(dropdown->captionImage, dropdown);
            }
            if (const UIToggle* toggle = ui_.TryGet<UIToggle>(entity); toggle != nullptr && toggle->graphic != 0U)
                toggleGraphics_.emplace(toggle->graphic, toggle->toggled);
            if (const UISlider* slider = ui_.TryGet<UISlider>(entity)) {
                const float fraction = NormalizedRange(slider->minimum, slider->maximum, slider->value);
                if (slider->fillRect != 0U)
                    rangeParts_[slider->fillRect] = RangePart{fraction, slider->direction, false};
                if (slider->handleRect != 0U)
                    rangeParts_[slider->handleRect] = RangePart{fraction, slider->direction, true};
            }
            if (const UIProgressBar* progress = ui_.TryGet<UIProgressBar>(entity); progress != nullptr && progress->fillRect != 0U)
                rangeParts_[progress->fillRect] =
                    RangePart{NormalizedRange(progress->minimum, progress->maximum, progress->value), progress->direction, false};
            for (std::size_t index = 0U; index < scene_.Hierarchy().ChildCount(entity); ++index)
                pending.push_back(scene_.Hierarchy().ChildAt(entity, index));
        }
        for (const SceneEntity root : roots)
            SearchForCanvas(root);
        if (valid_)
            AppendTooltip();
        if (!valid_) {
            output.elements.clear();
            output.refusal = refusal_;
            return false;
        }
        std::ranges::stable_sort(frame_.elements, [](const SceneUIFrameElement& a, const SceneUIFrameElement& b) {
            if (a.canvasSortingOrder != b.canvasSortingOrder)
                return a.canvasSortingOrder < b.canvasSortingOrder;
            if (a.zOrder != b.zOrder)
                return a.zOrder < b.zOrder;
            return a.traversalOrder < b.traversalOrder;
        });
        output = std::move(frame_);
        return true;
    }

  private:
    // Returns the reason the entity cannot be laid out, or nullptr when it is sound. A bare
    // bool would force the caller to re-derive what went wrong, which is what made an invalid
    // widget indistinguishable from a renderer fault.
    [[nodiscard]] const char* ValidateComponents(SceneEntity entity) const noexcept {
        std::size_t layoutCount = 0U;
#define KB_VALIDATE(Type)                                                                                              \
    if (const Type* value = ui_.TryGet<Type>(entity); value != nullptr && !IsUIComponentValid(*value))                 \
    return #Type " holds an invalid value"
        KB_VALIDATE(UIRectTransform);
        KB_VALIDATE(UICanvas);
        KB_VALIDATE(UICanvasScaler);
        KB_VALIDATE(UICanvasGroup);
        if (ui_.Has<UIHorizontalLayout>(entity))
            ++layoutCount;
        if (ui_.Has<UIVerticalLayout>(entity))
            ++layoutCount;
        if (ui_.Has<UIGridLayout>(entity))
            ++layoutCount;
        if (ui_.Has<UIWrapLayout>(entity))
            ++layoutCount;
        if (ui_.Has<UIOverlayLayout>(entity))
            ++layoutCount;
        KB_VALIDATE(UIHorizontalLayout);
        KB_VALIDATE(UIVerticalLayout);
        KB_VALIDATE(UIGridLayout);
        KB_VALIDATE(UIWrapLayout);
        KB_VALIDATE(UIOverlayLayout);
        KB_VALIDATE(UILayoutElement);
        KB_VALIDATE(UIContentSizeFitter);
        KB_VALIDATE(UIAspectRatioFitter);
        KB_VALIDATE(UISprite);
        KB_VALIDATE(UIImage);
        KB_VALIDATE(UIRawImage);
        KB_VALIDATE(UIText);
        KB_VALIDATE(UIBorder);
        KB_VALIDATE(UIMask);
        KB_VALIDATE(UIShadow);
        KB_VALIDATE(UIOutline);
        KB_VALIDATE(UIBackgroundBlur);
        KB_VALIDATE(UISelectable);
        KB_VALIDATE(UIButton);
        KB_VALIDATE(UIToggle);
        KB_VALIDATE(UISlider);
        KB_VALIDATE(UIScrollbar);
        KB_VALIDATE(UIScrollView);
        KB_VALIDATE(UIInputField);
        KB_VALIDATE(UIDropdown);
        KB_VALIDATE(UIProgressBar);
        KB_VALIDATE(UIWidgetSwitcher);
#undef KB_VALIDATE
        if (layoutCount > 1U)
            return "UI object carries more than one layout component";
        const std::size_t directChildCount = scene_.Hierarchy().ChildCount(entity);
        const UIWidgetSwitcher* switcher = ui_.TryGet<UIWidgetSwitcher>(entity);
        if (switcher != nullptr && directChildCount != 0U && switcher->visibleChildIndex >= directChildCount)
            return "Widget Switcher visibleChildIndex is past its last child";
        return nullptr;
    }

    [[nodiscard]] DesiredSize Measure(SceneEntity entity, bool minimumOnly = false) {
        const UIRectTransform* rect = ui_.TryGet<UIRectTransform>(entity);
        DesiredSize size{rect != nullptr ? Width(*rect) : 0.0F, rect != nullptr ? Height(*rect) : 0.0F};
        if (const UIText* text = ui_.TryGet<UIText>(entity)) {
            size.width = std::max(size.width,
                                  static_cast<float>(Utf8CodePointCount(UITextContent(*text))) * text->fontSize * 0.5F);
            size.height = std::max(size.height, text->fontSize * text->lineSpacing);
        }
        const std::size_t childCount = scene_.Hierarchy().ChildCount(entity);
        std::vector<DesiredSize> children;
        children.reserve(childCount);
        for (std::size_t i = 0U; i < childCount; ++i) {
            const SceneEntity child = scene_.Hierarchy().ChildAt(entity, i);
            const UILayoutElement* element = ui_.TryGet<UILayoutElement>(child);
            if (scene_.Entities().IsActive(child) && (element == nullptr || !element->ignoreLayout) &&
                ui_.Has<UIRectTransform>(child))
                children.push_back(Measure(child, minimumOnly));
        }
        if (const UIHorizontalLayout* layout = ui_.TryGet<UIHorizontalLayout>(entity)) {
            float width = layout->padding.left + layout->padding.right;
            float height = 0.0F;
            for (const DesiredSize child : children) {
                width += child.width;
                height = std::max(height, child.height);
            }
            if (!children.empty())
                width += layout->spacing * static_cast<float>(children.size() - 1U);
            size.width = std::max(size.width, width);
            size.height = std::max(size.height, height + layout->padding.top + layout->padding.bottom);
        } else if (const UIVerticalLayout* verticalLayout = ui_.TryGet<UIVerticalLayout>(entity)) {
            float width = 0.0F;
            float height = verticalLayout->padding.top + verticalLayout->padding.bottom;
            for (const DesiredSize child : children) {
                width = std::max(width, child.width);
                height += child.height;
            }
            if (!children.empty())
                height += verticalLayout->spacing * static_cast<float>(children.size() - 1U);
            size.width = std::max(size.width, width + verticalLayout->padding.left + verticalLayout->padding.right);
            size.height = std::max(size.height, height);
        } else if (const UIGridLayout* gridLayout = ui_.TryGet<UIGridLayout>(entity);
                   gridLayout != nullptr && !children.empty()) {
            const std::size_t columns = gridLayout->columns;
            const std::size_t rows = (children.size() + columns - 1U) / columns;
            size.width = std::max(size.width, gridLayout->padding.left + gridLayout->padding.right +
                                                  static_cast<float>(columns) * gridLayout->cellSize.x +
                                                  static_cast<float>(columns - 1U) * gridLayout->spacing.x);
            size.height = std::max(size.height, gridLayout->padding.top + gridLayout->padding.bottom +
                                                    static_cast<float>(rows) * gridLayout->cellSize.y +
                                                    static_cast<float>(rows - 1U) * gridLayout->spacing.y);
        } else if (const UIWrapLayout* wrapLayout = ui_.TryGet<UIWrapLayout>(entity);
                   wrapLayout != nullptr && !children.empty()) {
            float width = wrapLayout->padding.left + wrapLayout->padding.right;
            float height = 0.0F;
            for (const DesiredSize child : children) {
                width += child.width;
                height = std::max(height, child.height);
            }
            width += wrapLayout->spacing.x * static_cast<float>(children.size() - 1U);
            size.width = std::max(size.width, width);
            size.height = std::max(size.height, height + wrapLayout->padding.top + wrapLayout->padding.bottom);
        } else if (const UIOverlayLayout* overlayLayout = ui_.TryGet<UIOverlayLayout>(entity);
                   overlayLayout != nullptr) {
            float width = 0.0F;
            float height = 0.0F;
            for (const DesiredSize child : children) {
                width = std::max(width, child.width);
                height = std::max(height, child.height);
            }
            size.width = std::max(size.width, width + overlayLayout->padding.left + overlayLayout->padding.right);
            size.height = std::max(size.height, height + overlayLayout->padding.top + overlayLayout->padding.bottom);
        }
        if (const UILayoutElement* element = ui_.TryGet<UILayoutElement>(entity)) {
            if (!minimumOnly && element->preferredWidth >= 0.0F)
                size.width = element->preferredWidth;
            else if (element->minimumWidth >= 0.0F)
                size.width = minimumOnly ? element->minimumWidth : std::max(size.width, element->minimumWidth);
            if (!minimumOnly && element->preferredHeight >= 0.0F)
                size.height = element->preferredHeight;
            else if (element->minimumHeight >= 0.0F)
                size.height = minimumOnly ? element->minimumHeight : std::max(size.height, element->minimumHeight);
        }
        return size;
    }

    void SearchForCanvas(SceneEntity entity) {
        if (!scene_.Entities().IsActive(entity))
            return;
        const UICanvas* canvas = ui_.TryGet<UICanvas>(entity);
        const UIRectTransform* rect = ui_.TryGet<UIRectTransform>(entity);
        if (canvas != nullptr) {
            if (rect == nullptr) {
                Refuse(entity, "Canvas has no Rect Transform");
                return;
            }
            if (!IsUIComponentValid(*canvas) || !IsUIComponentValid(*rect)) {
                Refuse(entity, "Canvas or its Rect Transform holds an invalid value");
                return;
            }
            const UICanvasScaler* scaler = ui_.TryGet<UICanvasScaler>(entity);
            if (scaler != nullptr && !IsUIComponentValid(*scaler)) {
                Refuse(entity, "Canvas Scaler holds an invalid value");
                return;
            }
            player_ = canvas->player;
            clipSoftness_ = 0.0F;
            if (canvas->renderMode == UICanvasRenderMode::WorldSpace) {
                ArrangeWorldCanvas(entity, *canvas, *rect);
                return;
            }
            const float scale = CanvasScale(viewport_, scaler);
            if (!std::isfinite(scale) || scale <= 0.0F) {
                Refuse(entity, "Canvas Scaler resolved to a non-positive scale");
                return;
            }
            // The scale follows the whole screen, so a widget keeps its size whatever the insets are; only
            // the area the canvas lays out in shrinks to the safe area.
            Rect area{0.0F, 0.0F, viewport_.x, viewport_.y};
            if (canvas->respectSafeArea) {
                const UIEdges insets = state_.uiSafeAreaInsets;
                area.x = std::min(insets.left, viewport_.x);
                area.y = std::min(insets.top, viewport_.y);
                area.width = std::max(0.0F, viewport_.x - insets.left - insets.right);
                area.height = std::max(0.0F, viewport_.y - insets.top - insets.bottom);
            }
            const Rect logicalViewport{area.x / scale, area.y / scale, area.width / scale, area.height / scale};
            Arrange(entity, logicalViewport, {0.0F, 0.0F, viewport_.x, viewport_.y}, entity, scale,
                    canvas->sortingOrder, canvas->pixelPerfect, {}, UniformScale(scale), {});
            return;
        }
        const std::size_t count = scene_.Hierarchy().ChildCount(entity);
        for (std::size_t index = 0U; index < count; ++index)
            SearchForCanvas(scene_.Hierarchy().ChildAt(entity, index));
    }

    void Arrange(SceneEntity entity, Rect allocated, Rect inheritedClip, SceneEntity canvas, float scale,
                 std::int32_t sortingOrder, bool pixelPerfect, GroupState inheritedGroup,
                 Affine2D inheritedTransform, const std::vector<std::array<Vec2, 4U>>& inheritedClipQuads) {
        if (!valid_ || !scene_.Entities().IsActive(entity))
            return;
        if (const char* invalid = ValidateComponents(entity); invalid != nullptr) {
            Refuse(entity, invalid);
            return;
        }
        if (entity != canvas) {
            if (const UICanvas* nestedCanvas = ui_.TryGet<UICanvas>(entity)) {
                const UICanvasScaler* nestedScaler = ui_.TryGet<UICanvasScaler>(entity);
                const float nestedScale = CanvasScale(viewport_, nestedScaler);
                if (!std::isfinite(nestedScale) || nestedScale <= 0.0F) {
                    Refuse(entity, "Nested Canvas Scaler resolved to a non-positive scale");
                    return;
                }
                const float scaleRatio = scale / nestedScale;
                allocated = {allocated.x * scaleRatio, allocated.y * scaleRatio, allocated.width * scaleRatio,
                             allocated.height * scaleRatio};
                inheritedTransform = Compose(inheritedTransform, UniformScale(nestedScale / scale));
                canvas = entity;
                scale = nestedScale;
                sortingOrder = nestedCanvas->sortingOrder;
                pixelPerfect = nestedCanvas->pixelPerfect;
                if (nestedCanvas->player != -1)
                    player_ = nestedCanvas->player;
            }
        }
        const std::int32_t outerPlayer = player_;
        const float outerSoftness = clipSoftness_;
        const UIRectTransform* transform = ui_.TryGet<UIRectTransform>(entity);
        if (transform == nullptr) {
            Refuse(entity, "UI object has no Rect Transform");
            return;
        }
        if (!IsUIComponentValid(*transform)) {
            Refuse(entity, "Rect Transform holds an invalid value");
            return;
        }
        const DesiredSize desired = Measure(entity);
        const DesiredSize minimum = Measure(entity, true);
        Rect rect = allocated;
        if (const UIContentSizeFitter* fitter = ui_.TryGet<UIContentSizeFitter>(entity)) {
            if (!IsUIComponentValid(*fitter)) {
                Refuse(entity, "Content Size Fitter holds an invalid value");
                return;
            }
            const float width = fitter->horizontalFit == UIFitMode::Unconstrained
                                    ? rect.width
                                    : (fitter->horizontalFit == UIFitMode::MinimumSize ? minimum.width : desired.width);
            const float height =
                fitter->verticalFit == UIFitMode::Unconstrained
                    ? rect.height
                    : (fitter->verticalFit == UIFitMode::MinimumSize ? minimum.height : desired.height);
            rect = ResizeAroundPivot(rect, width, height, transform->pivot);
        }
        const UIAspectRatioFitter* aspect = ui_.TryGet<UIAspectRatioFitter>(entity);
        if (aspect != nullptr && !IsUIComponentValid(*aspect)) {
            Refuse(entity, "Aspect Ratio Fitter holds an invalid value");
            return;
        }
        rect = ApplyAspect(rect, aspect, transform->pivot);
        if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) ||
            !std::isfinite(rect.height)) {
            Refuse(entity, "Layout produced a non-finite rectangle");
            return;
        }

        GroupState group = inheritedGroup;
        Affine2D groupMotion{};
        if (const UICanvasGroup* authored = ui_.TryGet<UICanvasGroup>(entity)) {
            if (!IsUIComponentValid(*authored)) {
                Refuse(entity, "Canvas Group holds an invalid value");
                return;
            }
            if (authored->ignoreParentGroups)
                group = {};
            // Show/Hide: the group fades with how far it is shown, sits `hiddenOffset` away and at
            // `hiddenScale` when fully hidden, and takes no input while it is set hidden.
            const auto shownEntry = state_.uiGroupShown.find(entity.Id());
            const float shown = shownEntry != state_.uiGroupShown.end() ? shownEntry->second : (authored->visible ? 1.0F : 0.0F);
            group.opacity *= authored->opacity * shown;
            group.interactable = group.interactable && authored->interactable && authored->visible;
            group.blocksRaycasts = group.blocksRaycasts && authored->blocksRaycasts && authored->visible;
            if (shown < 1.0F) {
                const float motionScale = std::lerp(authored->hiddenScale, 1.0F, shown);
                const Vec2 pivot{rect.x + rect.width * transform->pivot.x, rect.y + rect.height * transform->pivot.y};
                groupMotion = {.m00 = motionScale, .m11 = motionScale,
                               .tx = pivot.x - motionScale * pivot.x + authored->hiddenOffset.x * (1.0F - shown),
                               .ty = pivot.y - motionScale * pivot.y + authored->hiddenOffset.y * (1.0F - shown)};
            }
        }
        if (const auto graphic = toggleGraphics_.find(entity.Id()); graphic != toggleGraphics_.end() && !graphic->second)
            group.opacity = 0.0F;

        SceneUIFrameElement element;
        element.entity = entity;
        element.canvas = canvas;
        element.canvasScale = scale;
        element.canvasSortingOrder = sortingOrder;
        element.player = player_;
        element.clipSoftness = clipSoftness_;
        element.zOrder = transform->zOrder;
        element.traversalOrder = traversal_++;
        element.effectiveOpacity = ResolveVisibility(scene_, entity).visible ? group.opacity : 0.0F;
        element.rect = {rect.x * scale, rect.y * scale, rect.width * scale, rect.height * scale};
        element.clipRect = inheritedClip;
        element.clipQuads = inheritedClipQuads;
        const Affine2D resolvedTransform =
            Compose(inheritedTransform, Compose(groupMotion, AuthoredTransform(rect, *transform)));
        const std::array<Vec2, 4U> base{{{rect.x, rect.y},
                                         {rect.x + rect.width, rect.y},
                                         {rect.x + rect.width, rect.y + rect.height},
                                         {rect.x, rect.y + rect.height}}};
        for (std::size_t i = 0U; i < base.size(); ++i) {
            element.corners[i] = TransformPoint(resolvedTransform, base[i]);
            if (pixelPerfect) {
                element.corners[i].x = std::round(element.corners[i].x);
                element.corners[i].y = std::round(element.corners[i].y);
            }
        }
        element.hitCorners = element.corners;
        if (const UISelectable* padded = ui_.TryGet<UISelectable>(entity)) {
            const UIEdges padding = padded->raycastPadding;
            if (padding.left != 0.0F || padding.top != 0.0F || padding.right != 0.0F || padding.bottom != 0.0F) {
                const float left = rect.x - padding.left;
                const float top = rect.y - padding.top;
                const float right = std::max(left, rect.x + rect.width + padding.right);
                const float bottom = std::max(top, rect.y + rect.height + padding.bottom);
                const std::array<Vec2, 4U> grown{{{left, top}, {right, top}, {right, bottom}, {left, bottom}}};
                for (std::size_t i = 0U; i < grown.size(); ++i)
                    element.hitCorners[i] = TransformPoint(resolvedTransform, grown[i]);
            }
        }
        if (pixelPerfect) {
            element.rect.x = std::round(element.rect.x);
            element.rect.y = std::round(element.rect.y);
            element.rect.width = std::round(element.rect.width);
            element.rect.height = std::round(element.rect.height);
        }
#define KB_COPY(Type, Field)                                                                                           \
    if (const Type* value = ui_.TryGet<Type>(entity)) {                                                                \
        if (!IsUIComponentValid(*value)) {                                                                             \
            Refuse(entity, #Type " holds an invalid value");                                                           \
            return;                                                                                                    \
        }                                                                                                              \
        element.Field = *value;                                                                                        \
    }
        KB_COPY(UISprite, sprite)
        KB_COPY(UIImage, image)
        KB_COPY(UIRawImage, rawImage)
        KB_COPY(UIText, text)
        KB_COPY(UIBorder, border)
        KB_COPY(UIMask, mask)
        KB_COPY(UIShadow, shadow)
        KB_COPY(UIOutline, outline)
        KB_COPY(UIBackgroundBlur, backgroundBlur)
        KB_COPY(UIToggle, toggle)
        KB_COPY(UISlider, slider)
        KB_COPY(UIScrollbar, scrollbar)
        KB_COPY(UIScrollView, scrollView)
        KB_COPY(UIDropdown, dropdown)
        KB_COPY(UIProgressBar, progressBar)
        KB_COPY(UIInputField, inputField)
#undef KB_COPY
        if (const auto caption = captionTexts_.find(entity.Id()); caption != captionTexts_.end() && element.text.has_value()) {
            const UIDropdown& dropdown = *caption->second;
            static_cast<void>(SetUITextContent(
                *element.text, dropdown.value < dropdown.optionCount ? UIDropdownOptionText(dropdown.options[dropdown.value])
                                                                     : std::string_view{}));
        }
        if (const auto caption = captionImages_.find(entity.Id()); caption != captionImages_.end() && element.image.has_value()) {
            const UIDropdown& dropdown = *caption->second;
            const std::uint64_t image =
                dropdown.value < dropdown.optionCount ? dropdown.options[dropdown.value].imageAssetId : 0U;
            // An option without an image leaves the caption image out, rather than drawing a blank one.
            if (image == 0U)
                element.image.reset();
            else
                element.image->imageAssetId = image;
        }
        if (element.text.has_value() && !UITextLocalizationKey(*element.text).empty())
            SetTextClipped(*element.text, scene_.Localization().Translate(UITextLocalizationKey(*element.text)));
        if (element.inputField.has_value()) {
            if (state_.uiFocused == entity) {
                element.textCaretByteOffset = static_cast<std::uint32_t>(state_.uiTextCursorByteOffset);
                element.textCaretVisible = IsUITextCaretVisible(state_.uiTextCaretPhase);
            }
            if (element.text.has_value()) {
                const std::string content{UITextContent(*element.text)};
                const UIInputContentType type = element.inputField->contentType;
                if (content.empty() && !UIInputPlaceholder(*element.inputField).empty()) {
                    SetTextClipped(*element.text, UIInputPlaceholder(*element.inputField));
                    element.text->color.a *= 0.5F;
                    element.textCaretByteOffset = 0U;
                } else if (type == UIInputContentType::Password || type == UIInputContentType::Pin) {
                    // One dot per character; the caret keeps its place in character terms.
                    const std::size_t caret = std::min<std::size_t>(element.textCaretByteOffset, content.size());
                    element.textCaretByteOffset =
                        static_cast<std::uint32_t>(Utf8CodePointCount(std::string_view{content}.substr(0U, caret)));
                    SetTextClipped(*element.text, std::string(Utf8CodePointCount(content), '*'));
                }
            }
        }
        const UISelectable* selectable = ui_.TryGet<UISelectable>(entity);
        if (selectable != nullptr && !IsUIComponentValid(*selectable)) {
            Refuse(entity, "Selectable holds an invalid value");
            return;
        }
        if (element.image.has_value() && element.image->alphaHitThreshold > 0.0F) {
            const auto alpha = state_.uiImageAlpha.find(element.image->imageAssetId);
            if (alpha != state_.uiImageAlpha.end())
                element.hitAlpha = alpha->second;
        }
        element.interactionEnabled = selectable != nullptr && selectable->interactable && group.interactable;
        element.hitTestable =
            element.interactionEnabled && selectable->raycastTarget && group.blocksRaycasts && element.effectiveOpacity > 0.0F;
        if (selectable != nullptr)
            element.interactionTint = selectable->normalColor;
        frame_.elements.push_back(std::move(element));
        const std::size_t elementIndex = frame_.elements.size() - 1U;

        Rect childClip = inheritedClip;
        std::vector<std::array<Vec2, 4U>> childClipQuads = inheritedClipQuads;
        if (const UIMask* mask = ui_.TryGet<UIMask>(entity)) {
            childClip = Intersect(childClip, Bounds(frame_.elements[elementIndex].corners));
            childClipQuads.push_back(frame_.elements[elementIndex].corners);
            clipSoftness_ = mask->softness * scale;
        }
        ArrangeChildren(entity, rect, childClip, canvas, scale, sortingOrder, pixelPerfect, group, resolvedTransform,
                        childClipQuads);
        player_ = outerPlayer;
        clipSoftness_ = outerSoftness;
    }

    void ArrangeChildren(SceneEntity parent, Rect rect, Rect clip, SceneEntity canvas, float scale,
                         std::int32_t sortingOrder, bool pixelPerfect, GroupState group,
                         const Affine2D& inheritedTransform,
                         const std::vector<std::array<Vec2, 4U>>& inheritedClipQuads) {
        const std::size_t count = scene_.Hierarchy().ChildCount(parent);
        std::vector<SceneEntity> managed;
        managed.reserve(count);
        const UIWidgetSwitcher* switcher = ui_.TryGet<UIWidgetSwitcher>(parent);
        for (std::size_t i = 0U; i < count; ++i) {
            if (switcher != nullptr && i != switcher->visibleChildIndex)
                continue;
            const SceneEntity child = scene_.Hierarchy().ChildAt(parent, i);
            const UILayoutElement* layout = ui_.TryGet<UILayoutElement>(child);
            if (scene_.Entities().IsActive(child) && ui_.Has<UIRectTransform>(child) &&
                (layout == nullptr || !layout->ignoreLayout))
                managed.push_back(child);
        }
        Rect contentRect = rect;
        if (const UIScrollView* scrollView = ui_.TryGet<UIScrollView>(parent)) {
            if (scrollView->horizontal)
                contentRect.x -= scrollView->scrollX;
            if (scrollView->vertical)
                contentRect.y -= scrollView->scrollY;
        }
        const auto arrangeIgnored = [&] {
            for (std::size_t i = 0U; i < count; ++i) {
                if (switcher != nullptr && i != switcher->visibleChildIndex)
                    continue;
                const SceneEntity child = scene_.Hierarchy().ChildAt(parent, i);
                const UILayoutElement* layout = ui_.TryGet<UILayoutElement>(child);
                if (scene_.Entities().IsActive(child) && ui_.Has<UIRectTransform>(child) && layout != nullptr &&
                    layout->ignoreLayout)
                    Arrange(child, AnchoredRect(Driven(child, *ui_.TryGet<UIRectTransform>(child)), contentRect), clip, canvas, scale,
                            sortingOrder, pixelPerfect, group, inheritedTransform, inheritedClipQuads);
            }
        };
        if (const UIHorizontalLayout* layout = ui_.TryGet<UIHorizontalLayout>(parent)) {
            const Rect inner = Inner(contentRect, layout->padding);
            std::vector<DesiredSize> sizes;
            sizes.reserve(managed.size());
            float used = layout->spacing * static_cast<float>(managed.empty() ? 0U : managed.size() - 1U), flex = 0.0F;
            for (SceneEntity child : managed) {
                DesiredSize size = Measure(child);
                const UILayoutElement* e = ui_.TryGet<UILayoutElement>(child);
                if (!layout->controlChildWidth)
                    size.width = Width(*ui_.TryGet<UIRectTransform>(child));
                if (!layout->controlChildHeight)
                    size.height = Height(*ui_.TryGet<UIRectTransform>(child));
                used += size.width;
                flex += e != nullptr && e->flexibleWidth > 0.0F ? e->flexibleWidth : 0.0F;
                sizes.push_back(size);
            }
            const float extra = std::max(0.0F, inner.width - used);
            const float finalUsed = used + (layout->expandChildWidth && !managed.empty() ? extra : 0.0F);
            const float start = AlignOffset(inner.width, finalUsed, layout->horizontalAlignment);
            float x = inner.x + start;
            for (std::size_t i = 0U; i < managed.size(); ++i) {
                const UILayoutElement* e = ui_.TryGet<UILayoutElement>(managed[i]);
                float width = sizes[i].width;
                if (layout->expandChildWidth) {
                    if (flex > 0.0F)
                        width += e != nullptr && e->flexibleWidth > 0.0F ? extra * e->flexibleWidth / flex : 0.0F;
                    else
                        width += extra / static_cast<float>(managed.size());
                }
                float height = sizes[i].height;
                if (layout->expandChildHeight || layout->verticalAlignment == UIAlignment::Stretch)
                    height = inner.height;
                Arrange(managed[i],
                        {x, inner.y + AlignOffset(inner.height, height, layout->verticalAlignment), width, height},
                        clip, canvas, scale, sortingOrder, pixelPerfect, group, inheritedTransform, inheritedClipQuads);
                x += width + layout->spacing;
            }
            arrangeIgnored();
            return;
        }
        if (const UIVerticalLayout* layout = ui_.TryGet<UIVerticalLayout>(parent)) {
            const Rect inner = Inner(contentRect, layout->padding);
            std::vector<DesiredSize> sizes;
            sizes.reserve(managed.size());
            float used = layout->spacing * static_cast<float>(managed.empty() ? 0U : managed.size() - 1U), flex = 0.0F;
            for (SceneEntity child : managed) {
                DesiredSize size = Measure(child);
                const UILayoutElement* e = ui_.TryGet<UILayoutElement>(child);
                if (!layout->controlChildWidth)
                    size.width = Width(*ui_.TryGet<UIRectTransform>(child));
                if (!layout->controlChildHeight)
                    size.height = Height(*ui_.TryGet<UIRectTransform>(child));
                used += size.height;
                flex += e != nullptr && e->flexibleHeight > 0.0F ? e->flexibleHeight : 0.0F;
                sizes.push_back(size);
            }
            const float extra = std::max(0.0F, inner.height - used);
            const float finalUsed = used + (layout->expandChildHeight && !managed.empty() ? extra : 0.0F);
            const float start = AlignOffset(inner.height, finalUsed, layout->verticalAlignment);
            float y = inner.y + start;
            for (std::size_t i = 0U; i < managed.size(); ++i) {
                const UILayoutElement* e = ui_.TryGet<UILayoutElement>(managed[i]);
                float height = sizes[i].height;
                if (layout->expandChildHeight) {
                    if (flex > 0.0F)
                        height += e != nullptr && e->flexibleHeight > 0.0F ? extra * e->flexibleHeight / flex : 0.0F;
                    else
                        height += extra / static_cast<float>(managed.size());
                }
                float width = sizes[i].width;
                if (layout->expandChildWidth || layout->horizontalAlignment == UIAlignment::Stretch)
                    width = inner.width;
                Arrange(managed[i],
                        {inner.x + AlignOffset(inner.width, width, layout->horizontalAlignment), y, width, height},
                        clip, canvas, scale, sortingOrder, pixelPerfect, group, inheritedTransform, inheritedClipQuads);
                y += height + layout->spacing;
            }
            arrangeIgnored();
            return;
        }
        if (const UIGridLayout* layout = ui_.TryGet<UIGridLayout>(parent)) {
            const Rect inner = Inner(contentRect, layout->padding);
            const std::size_t columns =
                std::min(static_cast<std::size_t>(layout->columns), std::max<std::size_t>(managed.size(), 1U));
            const std::size_t rows = (managed.size() + columns - 1U) / columns;
            const float usedWidth =
                static_cast<float>(columns) * layout->cellSize.x + static_cast<float>(columns - 1U) * layout->spacing.x;
            const float usedHeight =
                static_cast<float>(rows) * layout->cellSize.y + static_cast<float>(rows - 1U) * layout->spacing.y;
            const float startX = inner.x + AlignOffset(inner.width, usedWidth, layout->horizontalAlignment);
            const float startY = inner.y + AlignOffset(inner.height, usedHeight, layout->verticalAlignment);
            for (std::size_t i = 0U; i < managed.size(); ++i) {
                const std::size_t col = i % columns, row = i / columns;
                Arrange(managed[i],
                        {startX + static_cast<float>(col) * (layout->cellSize.x + layout->spacing.x),
                         startY + static_cast<float>(row) * (layout->cellSize.y + layout->spacing.y),
                          layout->cellSize.x, layout->cellSize.y},
                        clip, canvas, scale, sortingOrder, pixelPerfect, group, inheritedTransform, inheritedClipQuads);
            }
            arrangeIgnored();
            return;
        }
        if (const UIWrapLayout* layout = ui_.TryGet<UIWrapLayout>(parent)) {
            const Rect inner = Inner(contentRect, layout->padding);
            struct WrapRow {
                std::size_t begin = 0U;
                std::size_t end = 0U;
                float width = 0.0F;
                float height = 0.0F;
            };
            std::vector<DesiredSize> sizes;
            sizes.reserve(managed.size());
            std::vector<WrapRow> rows;
            for (SceneEntity child : managed) {
                const DesiredSize size = Measure(child);
                sizes.push_back(size);
                if (rows.empty() || (rows.back().begin != rows.back().end &&
                                     rows.back().width + layout->spacing.x + size.width > inner.width)) {
                    rows.push_back({.begin = sizes.size() - 1U, .end = sizes.size()});
                } else {
                    rows.back().width += layout->spacing.x;
                    rows.back().end = sizes.size();
                }
                rows.back().width += size.width;
                rows.back().height = std::max(rows.back().height, size.height);
            }
            float usedHeight = layout->spacing.y * static_cast<float>(rows.empty() ? 0U : rows.size() - 1U);
            for (const WrapRow& row : rows)
                usedHeight += row.height;
            float y = inner.y + AlignOffset(inner.height, usedHeight, layout->verticalAlignment);
            for (const WrapRow& row : rows) {
                float x = inner.x + AlignOffset(inner.width, row.width, layout->horizontalAlignment);
                for (std::size_t index = row.begin; index < row.end; ++index) {
                    Arrange(managed[index], {x, y, sizes[index].width, sizes[index].height}, clip, canvas, scale,
                            sortingOrder, pixelPerfect, group, inheritedTransform, inheritedClipQuads);
                    x += sizes[index].width + layout->spacing.x;
                }
                y += row.height + layout->spacing.y;
            }
            arrangeIgnored();
            return;
        }
        if (const UIOverlayLayout* layout = ui_.TryGet<UIOverlayLayout>(parent)) {
            const Rect inner = Inner(contentRect, layout->padding);
            for (SceneEntity child : managed) {
                const DesiredSize size = Measure(child);
                const float width = layout->horizontalAlignment == UIAlignment::Stretch ? inner.width : size.width,
                            height = layout->verticalAlignment == UIAlignment::Stretch ? inner.height : size.height;
                Arrange(child,
                        {inner.x + AlignOffset(inner.width, width, layout->horizontalAlignment),
                         inner.y + AlignOffset(inner.height, height, layout->verticalAlignment), width, height},
                        clip, canvas, scale, sortingOrder, pixelPerfect, group, inheritedTransform, inheritedClipQuads);
            }
            arrangeIgnored();
            return;
        }
        const UIProgressBar* progress = ui_.TryGet<UIProgressBar>(parent);
        for (std::size_t index = 0U; index < managed.size(); ++index) {
            Rect childRect = AnchoredRect(Driven(managed[index], *ui_.TryGet<UIRectTransform>(managed[index])), contentRect);
            // A progress bar without a fill widget stretches its first child, as it always has.
            if (progress != nullptr && progress->fillRect == 0U && index == 0U) {
                const float fraction = NormalizedRange(progress->minimum, progress->maximum, progress->value);
                childRect.width *= fraction;
            }
            Arrange(managed[index], childRect, clip, canvas, scale, sortingOrder, pixelPerfect, group,
                    inheritedTransform, inheritedClipQuads);
        }
        arrangeIgnored();
    }


    // A slider's or progress bar's fill spans the start of its parent up to the value along the control's
    // direction, and its handle is anchored at the value. Everything else keeps its authored anchors.
    [[nodiscard]] UIRectTransform Driven(SceneEntity entity, UIRectTransform rect) const noexcept {
        const auto part = rangeParts_.find(entity.Id());
        if (part == rangeParts_.end())
            return rect;
        const UIAxisDirection direction = part->second.direction;
        const bool vertical = direction == UIAxisDirection::BottomToTop || direction == UIAxisDirection::TopToBottom;
        // Rows grow downwards, so a bottom-to-top control fills from the bottom edge.
        const bool reverse = direction == UIAxisDirection::RightToLeft || direction == UIAxisDirection::BottomToTop;
        const float at = reverse ? 1.0F - part->second.fraction : part->second.fraction;
        float& low = vertical ? rect.anchorMin.y : rect.anchorMin.x;
        float& high = vertical ? rect.anchorMax.y : rect.anchorMax.x;
        if (part->second.handle) {
            low = at;
            high = at;
        } else if (reverse) {
            low = at;
            high = 1.0F;
        } else {
            low = 0.0F;
            high = at;
        }
        return rect;
    }

    // Lays a World Space canvas out at its own size, then places every element on the canvas plane at the
    // canvas object's transform and projects it through the camera. Elements behind the camera, or with no
    // camera published yet, draw nothing and take no input.
    void ArrangeWorldCanvas(SceneEntity entity, const UICanvas& canvas, const UIRectTransform& rect) {
        const TransformComponent* placement = scene_.Transforms().TryGet(entity);
        if (placement == nullptr) {
            Refuse(entity, "World Space Canvas has no Transform");
            return;
        }
        const float width = Width(rect);
        const float height = Height(rect);
        const std::size_t first = frame_.elements.size();
        Arrange(entity, {0.0F, 0.0F, width, height}, {0.0F, 0.0F, width, height}, entity, 1.0F, canvas.sortingOrder, false,
                {}, {}, {});
        if (!valid_)
            return;
        const auto project = [&](Vec2 point, bool& visible) {
            const kb::math::Vec3 local{(point.x - width * rect.pivot.x) / canvas.pixelsPerUnit * placement->worldScale.x,
                                       -(point.y - height * rect.pivot.y) / canvas.pixelsPerUnit * placement->worldScale.y,
                                       0.0F};
            const SceneRenderScreenPoint screen = SceneRenderFeedback::WorldToScreen(
                scene_, placement->worldPosition + kb::math::Rotate(placement->worldRotation, local));
            visible = visible && screen.valid && screen.viewDepth > 0.0F;
            return Vec2{screen.screenX, screen.screenY};
        };
        const Rect screen{0.0F, 0.0F, viewport_.x, viewport_.y};
        for (std::size_t index = first; index < frame_.elements.size(); ++index) {
            SceneUIFrameElement& element = frame_.elements[index];
            bool visible = true;
            const float logicalWidth = element.rect.width;
            const float logicalHeight = element.rect.height;
            for (Vec2& corner : element.corners)
                corner = project(corner, visible);
            for (Vec2& corner : element.hitCorners)
                corner = project(corner, visible);
            for (auto& quad : element.clipQuads)
                for (Vec2& corner : quad)
                    corner = project(corner, visible);
            const Rect clip = element.clipRect;
            const std::array<Vec2, 4U> clipCorners{{project({clip.x, clip.y}, visible),
                                                    project({clip.x + clip.width, clip.y}, visible),
                                                    project({clip.x + clip.width, clip.y + clip.height}, visible),
                                                    project({clip.x, clip.y + clip.height}, visible)}};
            element.clipRect = Intersect(Bounds(clipCorners), screen);
            const float projectedWidth = std::hypot(element.corners[1].x - element.corners[0].x, element.corners[1].y - element.corners[0].y);
            const float projectedHeight = std::hypot(element.corners[3].x - element.corners[0].x, element.corners[3].y - element.corners[0].y);
            element.rect = {element.corners[0].x, element.corners[0].y, projectedWidth, projectedHeight};
            element.canvasScale = logicalWidth > 0.0F ? projectedWidth / logicalWidth
                                                      : (logicalHeight > 0.0F ? projectedHeight / logicalHeight : 1.0F);
            element.clipSoftness *= element.canvasScale;
            if (!visible || !std::isfinite(element.canvasScale) || element.canvasScale <= 0.0F) {
                element.effectiveOpacity = 0.0F;
                element.hitTestable = false;
                element.canvasScale = 1.0F;
            }
        }
    }

    // The first font in the widget's subtree, else in its canvas - the tooltip bubble reads in the
    // typeface the menu already uses.
    [[nodiscard]] std::uint64_t FontNear(SceneEntity entity, SceneEntity canvas) const {
        for (const SceneEntity root : {entity, canvas}) {
            std::vector<SceneEntity> pending{root};
            while (!pending.empty()) {
                const SceneEntity current = pending.back();
                pending.pop_back();
                if (const UIText* text = ui_.TryGet<UIText>(current); text != nullptr && text->fontAssetId != 0U)
                    return text->fontAssetId;
                for (std::size_t index = 0U; index < scene_.Hierarchy().ChildCount(current); ++index)
                    pending.push_back(scene_.Hierarchy().ChildAt(current, index));
            }
        }
        return 0U;
    }

    // The hovered widget's tooltip, once the pointer has rested on it: a dark rounded bubble with the text,
    // below and to the right of the pointer, kept on screen.
    void AppendTooltip() {
        if (!state_.uiHovered.IsValid() || state_.uiHoverSeconds < kUITooltipDelaySeconds || state_.uiDragging ||
            state_.previousUIInput.primaryDown)
            return;
        const UISelectable* selectable = ui_.TryGet<UISelectable>(state_.uiHovered);
        if (selectable == nullptr || UITooltipText(*selectable).empty())
            return;
        const auto owner = std::ranges::find(frame_.elements, state_.uiHovered, &SceneUIFrameElement::entity);
        if (owner == frame_.elements.end())
            return;
        const std::uint64_t font = FontNear(owner->entity, owner->canvas);
        if (font == 0U)
            return;
        const float scale = owner->canvasScale;
        const std::string_view label = UITooltipText(*selectable);
        const float width = (static_cast<float>(Utf8CodePointCount(label)) * kUITooltipFontSize * 0.55F + kUITooltipPadding * 2.0F) * scale;
        const float height = (kUITooltipFontSize * 1.4F + kUITooltipPadding) * scale;
        const float x = std::clamp(state_.uiTooltipPointer.x + kUITooltipPointerGap.x * scale, 0.0F, std::max(0.0F, viewport_.x - width));
        float y = state_.uiTooltipPointer.y + kUITooltipPointerGap.y * scale;
        if (y + height > viewport_.y)
            y = std::max(0.0F, state_.uiTooltipPointer.y - height - kUITooltipPointerGap.y * 0.5F * scale);
        const auto place = [&](SceneUIFrameElement& element) {
            element.rect = {x, y, width, height};
            element.corners = {{{x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}}};
            element.hitCorners = element.corners;
            element.clipRect = {0.0F, 0.0F, viewport_.x, viewport_.y};
            element.canvasScale = scale;
            element.canvasSortingOrder = kUITooltipSortingOrder;
            element.traversalOrder = traversal_++;
            element.tooltip = true;
        };
        SceneUIFrameElement bubble;
        place(bubble);
        UIBorder border{};
        border.backgroundColor = {0.08F, 0.09F, 0.11F, 0.94F};
        border.borderColor = {1.0F, 1.0F, 1.0F, 0.14F};
        border.borderWidth = {1.0F, 1.0F, 1.0F, 1.0F};
        border.cornerRadius = {4.0F, 4.0F, 4.0F, 4.0F};
        bubble.border = border;
        UIShadow shadow{};
        shadow.offset = {0.0F, 2.0F};
        shadow.color = {0.0F, 0.0F, 0.0F, 0.35F};
        shadow.blur = 6.0F;
        bubble.shadow = shadow;
        frame_.elements.push_back(std::move(bubble));
        SceneUIFrameElement text;
        place(text);
        UIText content{};
        SetTextClipped(content, label);
        content.fontAssetId = font;
        content.fontSize = kUITooltipFontSize;
        content.color = {0.93F, 0.94F, 0.96F, 1.0F};
        content.horizontalAlignment = UITextHorizontalAlignment::Center;
        content.verticalAlignment = UITextVerticalAlignment::Center;
        content.wrapMode = UITextWrapMode::NoWrap;
        content.richText = false;
        text.text = content;
        frame_.elements.push_back(std::move(text));
    }

    // Records the first refusal and stops the build. Only the first is kept: later entities
    // are unreachable consequences of this one, so naming them would bury the cause.
    void Refuse(SceneEntity entity, const char* reason) noexcept {
        if (valid_) {
            refusal_ = SceneUIFrameRefusal{.entity = entity, .reason = reason};
            valid_ = false;
        }
    }

    struct RangePart {
        float fraction = 0.0F;
        UIAxisDirection direction = UIAxisDirection::LeftToRight;
        bool handle = false;
    };

    const Scene& scene_;
    const SceneState& state_;
    SceneUIComponentQueries ui_;
    Vec2 viewport_{};
    // The player owning the canvas being laid out, and the soft edge of the mask around it.
    std::int32_t player_ = -1;
    float clipSoftness_ = 0.0F;
    // Fill and handle widgets driven by a slider or progress bar, by entity id.
    std::unordered_map<std::uint64_t, RangePart> rangeParts_;
    // Widgets whose presentation another component drives: a dropdown's caption text and image show its
    // chosen option, and a toggle's on-state graphic is drawn only while the toggle is on.
    std::unordered_map<std::uint64_t, const UIDropdown*> captionTexts_;
    std::unordered_map<std::uint64_t, const UIDropdown*> captionImages_;
    std::unordered_map<std::uint64_t, bool> toggleGraphics_;
    SceneUIFrame frame_;
    SceneUIFrameRefusal refusal_{};
    std::uint32_t traversal_ = 0U;
    bool valid_ = true;
};

[[nodiscard]] const UISelectable* Selectable(const Scene& scene, SceneEntity entity) noexcept {
    return entity.IsValid() ? scene.Components().UI().TryGet<UISelectable>(entity) : nullptr;
}
void Queue(SceneState& state, const Scene& scene, SceneUIEventType type, SceneEntity entity,
           const SceneUIInput* input = nullptr, float value = 0.0F, float value2 = 0.0F, std::string_view text = {}) {
    SceneUIEvent event = MakeEvent(type, entity, Selectable(scene, entity));
    event.value = value;
    event.value2 = value2;
    if (input != nullptr && input->pointerAvailable) {
        event.pointerAvailable = true;
        event.pointerPosition = input->pointerPosition;
    }
    std::copy(text.begin(), text.end(), event.text.begin());
    state.uiEvents.push_back(std::move(event));
}
[[nodiscard]] const SceneUIFrameElement* Find(const SceneUIFrame& frame, SceneEntity entity) noexcept {
    const auto found = std::ranges::find(frame.elements, entity, &SceneUIFrameElement::entity);
    return found != frame.elements.end() ? &*found : nullptr;
}

[[nodiscard]] Vec2 ScrollLimits(const Scene& scene, const SceneUIFrame& frame, SceneEntity entity,
                                const UIScrollView& scrollView) noexcept {
    const SceneUIFrameElement* viewport = Find(frame, entity);
    if (viewport == nullptr)
        return {};
    const float scale = std::max(viewport->canvasScale, 0.0001F);
    const float framedScrollX = viewport->scrollView.has_value() ? viewport->scrollView->scrollX : scrollView.scrollX;
    const float framedScrollY = viewport->scrollView.has_value() ? viewport->scrollView->scrollY : scrollView.scrollY;
    float contentRight = viewport->rect.x;
    float contentBottom = viewport->rect.y;
    const std::size_t childCount = scene.Hierarchy().ChildCount(entity);
    for (std::size_t index = 0U; index < childCount; ++index) {
        const SceneUIFrameElement* child = Find(frame, scene.Hierarchy().ChildAt(entity, index));
        if (child == nullptr)
            continue;
        contentRight = std::max(contentRight, child->rect.x + child->rect.width + framedScrollX * scale);
        contentBottom = std::max(contentBottom, child->rect.y + child->rect.height + framedScrollY * scale);
    }
    return {std::max(0.0F, (contentRight - viewport->rect.x - viewport->rect.width) / scale),
            std::max(0.0F, (contentBottom - viewport->rect.y - viewport->rect.height) / scale)};
}

[[nodiscard]] bool ClampScrollOffsets(const Scene& scene, const SceneUIFrame& frame, SceneEntity entity,
                                      UIScrollView& scrollView) noexcept {
    const Vec2 limits = ScrollLimits(scene, frame, entity, scrollView);
    const float beforeX = scrollView.scrollX;
    const float beforeY = scrollView.scrollY;
    if (scrollView.horizontal)
        scrollView.scrollX = std::clamp(scrollView.scrollX, 0.0F, limits.x);
    if (scrollView.vertical)
        scrollView.scrollY = std::clamp(scrollView.scrollY, 0.0F, limits.y);
    return beforeX != scrollView.scrollX || beforeY != scrollView.scrollY;
}

// The lowest a scroll offset may go while it is being moved: a clamped view stops at the start, an elastic or
// unrestricted one may be pulled past it.
[[nodiscard]] float ScrollFloor(const UIScrollView& scrollView, float offset) noexcept {
    return scrollView.movementType == UIScrollMovement::Clamped ? std::max(0.0F, offset) : offset;
}

[[nodiscard]] bool IsDescendantOrSelf(const Scene& scene, SceneEntity entity, SceneEntity ancestor) noexcept;

// Keeps every scroll view inside its content. A clamped view is put back at once; an elastic view that is not
// being dragged springs back over its elasticity; an unrestricted view is left where it is.
[[nodiscard]] bool ClampAllScrollOffsets(Scene& scene, SceneState& state, const SceneUIInput& input, float deltaSeconds) {
    bool changed = false;
    SceneUIComponents ui = scene.Components().UI();
    for (const SceneUIFrameElement& element : state.uiFrame.elements) {
        UIScrollView* scrollView = ui.TryGet<UIScrollView>(element.entity);
        if (scrollView == nullptr || scrollView->movementType == UIScrollMovement::Unrestricted)
            continue;
        if (scrollView->movementType == UIScrollMovement::Elastic) {
            if (input.primaryDown && state.uiPressed.IsValid() && IsDescendantOrSelf(scene, state.uiPressed, element.entity))
                continue;
            UIScrollView clamped = *scrollView;
            static_cast<void>(ClampScrollOffsets(scene, state.uiFrame, element.entity, clamped));
            if (clamped.scrollX == scrollView->scrollX && clamped.scrollY == scrollView->scrollY)
                continue;
            const float amount = scrollView->elasticity <= 0.0F ? 1.0F : 1.0F - std::exp(-deltaSeconds / scrollView->elasticity);
            const auto spring = [amount](float& value, float target) {
                value = std::abs(target - value) < 0.5F ? target : std::lerp(value, target, amount);
            };
            spring(scrollView->scrollX, clamped.scrollX);
            spring(scrollView->scrollY, clamped.scrollY);
        } else if (!ClampScrollOffsets(scene, state.uiFrame, element.entity, *scrollView)) {
            continue;
        }
        ui.MarkModified<UIScrollView>(element.entity);
        Queue(state, scene, SceneUIEventType::Changed, element.entity, &input, scrollView->scrollX,
              scrollView->scrollY);
        changed = true;
    }
    return changed;
}

[[nodiscard]] kb::math::Color InteractionColor(const UISelectable& selectable, UIInteractionState state) noexcept {
    switch (state) {
    case UIInteractionState::Hovered:
        return selectable.highlightedColor;
    case UIInteractionState::Pressed:
        return selectable.pressedColor;
    case UIInteractionState::Focused:
        return selectable.selectedColor;
    case UIInteractionState::Disabled:
        return selectable.disabledColor;
    case UIInteractionState::Normal:
        return selectable.normalColor;
    }
    return selectable.normalColor;
}

[[nodiscard]] kb::math::Color LerpColor(kb::math::Color from, kb::math::Color to, float amount) noexcept {
    return {
        std::lerp(from.r, to.r, amount),
        std::lerp(from.g, to.g, amount),
        std::lerp(from.b, to.b, amount),
        std::lerp(from.a, to.a, amount),
    };
}

void ResolveInteractionPresentation(const Scene& scene, const SceneState& state, bool primaryDown, float deltaSeconds,
                                    const SceneUIFrame& previous, SceneUIFrame& current, bool advanceTransition) {
    for (SceneUIFrameElement& element : current.elements) {
        const UISelectable* selectable = Selectable(scene, element.entity);
        if (selectable == nullptr) {
            element.interactionState = UIInteractionState::Normal;
            element.interactionTint = {};
            continue;
        }

        if (!element.interactionEnabled)
            element.interactionState = UIInteractionState::Disabled;
        else if (element.entity == state.uiPressed && primaryDown)
            element.interactionState = UIInteractionState::Pressed;
        else if (element.entity == state.uiHovered)
            element.interactionState = UIInteractionState::Hovered;
        else if (element.entity == state.uiFocused ||
                 std::ranges::any_of(state.uiPlayers, [&](const SceneState::UIPlayerFocus& player) { return player.focused == element.entity; }))
            element.interactionState = UIInteractionState::Focused;
        else
            element.interactionState = UIInteractionState::Normal;

        const UIToggle* toggle = scene.Components().UI().TryGet<UIToggle>(element.entity);
        const kb::math::Color target = toggle != nullptr && toggle->toggled &&
                                               element.interactionState == UIInteractionState::Normal
                                           ? selectable->selectedColor
                                           : InteractionColor(*selectable, element.interactionState);
        const SceneUIFrameElement* prior = Find(previous, element.entity);
        if (prior == nullptr) {
            element.interactionTint = target;
            continue;
        }
        if (!advanceTransition) {
            element.interactionTint = prior->interactionTint;
            continue;
        }
        const float amount = selectable->colorFadeSeconds <= 0.0F
                                 ? 1.0F
                                 : std::clamp(deltaSeconds / selectable->colorFadeSeconds, 0.0F, 1.0F);
        element.interactionTint = LerpColor(prior->interactionTint, target, amount);
    }
    // A selectable shows its state on its target graphic, or on itself without one: tinted with the state
    // colour, with the image swapped for the state image, or not at all.
    std::unordered_map<std::uint64_t, std::size_t> indexByEntity;
    for (std::size_t index = 0U; index < current.elements.size(); ++index) {
        const UISelectable* selectable = Selectable(scene, current.elements[index].entity);
        if (selectable == nullptr)
            continue;
        std::size_t target = index;
        if (selectable->targetGraphic != 0U && selectable->targetGraphic != current.elements[index].entity.Id()) {
            if (indexByEntity.empty())
                for (std::size_t other = 0U; other < current.elements.size(); ++other)
                    indexByEntity.emplace(current.elements[other].entity.Id(), other);
            const auto found = indexByEntity.find(selectable->targetGraphic);
            if (found == indexByEntity.end())
                continue;
            target = found->second;
        }
        SceneUIFrameElement& graphic = current.elements[target];
        const UIInteractionState shownState = current.elements[index].interactionState;
        const kb::math::Color tint = current.elements[index].interactionTint;
        current.elements[index].interactionTint = {};
        graphic.interactionState = shownState;
        graphic.interactionTint = selectable->transition == UISelectableTransition::ColorTint ? tint : kb::math::Color{};
        if (selectable->transition == UISelectableTransition::SpriteSwap && graphic.image.has_value()) {
            const std::uint64_t image = shownState == UIInteractionState::Hovered  ? selectable->highlightedImage
                                        : shownState == UIInteractionState::Pressed ? selectable->pressedImage
                                        : shownState == UIInteractionState::Focused ? selectable->selectedImage
                                        : shownState == UIInteractionState::Disabled ? selectable->disabledImage
                                                                                     : 0U;
            if (image != 0U)
                graphic.image->imageAssetId = image;
        }
    }
}

template <typename T> [[nodiscard]] SceneEntity ClosestAncestorWith(const Scene& scene, SceneEntity entity) noexcept {
    const SceneUIComponentQueries ui = scene.Components().UI();
    while (entity.IsValid()) {
        if (ui.Has<T>(entity))
            return entity;
        entity = scene.Hierarchy().Parent(entity);
    }
    return {};
}
// Whether navigation by `player` may land on the element: players 1-3 stay on their own canvases, and the
// shared controls on the canvases everyone or player 0 uses.
[[nodiscard]] bool NavigableBy(const SceneUIFrameElement& element, std::int32_t player) noexcept {
    return player == 0 ? element.player <= 0 : element.player == player;
}

[[nodiscard]] SceneEntity AutomaticNeighbor(const SceneUIFrame& frame, SceneEntity current, Vec2 direction,
                                            std::int32_t player = 0) noexcept {
    const SceneUIFrameElement* source = Find(frame, current);
    if (source == nullptr) {
        for (const auto& e : frame.elements)
            if (e.hitTestable && NavigableBy(e, player))
                return e.entity;
        return {};
    }
    const Vec2 center{source->rect.x + source->rect.width * 0.5F, source->rect.y + source->rect.height * 0.5F};
    float best = std::numeric_limits<float>::max();
    SceneEntity result{};
    for (const auto& e : frame.elements) {
        if (!e.hitTestable || e.entity == current || !NavigableBy(e, player))
            continue;
        const Vec2 delta{e.rect.x + e.rect.width * 0.5F - center.x, e.rect.y + e.rect.height * 0.5F - center.y};
        const float along = delta.x * direction.x + delta.y * direction.y;
        if (along <= 0.0F)
            continue;
        const float across = std::abs(delta.x * direction.y - delta.y * direction.x);
        const float score = along + across * 4.0F;
        if (score < best) {
            best = score;
            result = e.entity;
        }
    }
    return result;
}

[[nodiscard]] std::size_t PreviousUtf8Boundary(std::string_view text, std::size_t offset) noexcept {
    if (offset == 0U)
        return 0U;
    --offset;
    while (offset > 0U && (static_cast<unsigned char>(text[offset]) & 0xC0U) == 0x80U)
        --offset;
    return offset;
}

[[nodiscard]] std::size_t NextUtf8Boundary(std::string_view text, std::size_t offset) noexcept {
    if (offset >= text.size())
        return text.size();
    ++offset;
    while (offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xC0U) == 0x80U)
        ++offset;
    return offset;
}

[[nodiscard]] std::size_t Utf8CodePointCount(std::string_view text) noexcept {
    std::size_t count = 0U;
    for (std::size_t offset = 0U; offset < text.size(); offset = NextUtf8Boundary(text, offset))
        ++count;
    return count;
}

[[nodiscard]] std::size_t EncodeUtf8(char32_t codePoint, std::array<char, 4U>& output) noexcept {
    if (!kb::input::IsUnicodeScalar(codePoint))
        return 0U;
    if (codePoint <= 0x7FU) {
        output[0] = static_cast<char>(codePoint);
        return 1U;
    }
    if (codePoint <= 0x7FFU) {
        output[0] = static_cast<char>(0xC0U | (codePoint >> 6U));
        output[1] = static_cast<char>(0x80U | (codePoint & 0x3FU));
        return 2U;
    }
    if (codePoint <= 0xFFFFU) {
        output[0] = static_cast<char>(0xE0U | (codePoint >> 12U));
        output[1] = static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU));
        output[2] = static_cast<char>(0x80U | (codePoint & 0x3FU));
        return 3U;
    }
    output[0] = static_cast<char>(0xF0U | (codePoint >> 18U));
    output[1] = static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU));
    output[2] = static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU));
    output[3] = static_cast<char>(0x80U | (codePoint & 0x3FU));
    return 4U;
}

// Whether a typed character belongs in a field of this content type at the caret.
[[nodiscard]] bool AcceptsCharacter(UIInputContentType type, std::string_view text, std::size_t caret, char32_t codePoint) noexcept {
    const bool digit = codePoint >= U'0' && codePoint <= U'9';
    const bool sign = codePoint == U'-' && caret == 0U && !text.starts_with('-');
    switch (type) {
    case UIInputContentType::Standard:
    case UIInputContentType::Password:
        return true;
    case UIInputContentType::IntegerNumber:
        return digit || sign;
    case UIInputContentType::DecimalNumber:
        return digit || sign || (codePoint == U'.' && text.find('.') == std::string_view::npos);
    case UIInputContentType::Alphanumeric:
        return digit || (codePoint >= U'a' && codePoint <= U'z') || (codePoint >= U'A' && codePoint <= U'Z') || codePoint > 0x7FU;
    case UIInputContentType::EmailAddress:
        return codePoint > U' ';
    case UIInputContentType::Pin:
        return digit;
    }
    return false;
}

[[nodiscard]] bool EditFocusedText(Scene& scene, SceneState& state, const SceneUIInput& input, bool backspaceStarted,
                                   bool deleteStarted, bool moveLeft, bool moveRight) {
    if (!state.uiFocused.IsValid())
        return false;
    SceneUIComponents ui = scene.Components().UI();
    UIInputField* field = ui.TryGet<UIInputField>(state.uiFocused);
    UIText* text = ui.TryGet<UIText>(state.uiFocused);
    if (field == nullptr || text == nullptr)
        return false;

    std::string buffer{UITextContent(*text)};
    state.uiTextCursorByteOffset = std::min(state.uiTextCursorByteOffset, buffer.size());
    while (state.uiTextCursorByteOffset > 0U && state.uiTextCursorByteOffset < buffer.size() &&
           (static_cast<unsigned char>(buffer[state.uiTextCursorByteOffset]) & 0xC0U) == 0x80U) {
        --state.uiTextCursorByteOffset;
    }
    if (moveLeft)
        state.uiTextCursorByteOffset = PreviousUtf8Boundary(buffer, state.uiTextCursorByteOffset);
    if (moveRight)
        state.uiTextCursorByteOffset = NextUtf8Boundary(buffer, state.uiTextCursorByteOffset);
    if (field->readOnly)
        return false;

    bool changed = false;
    bool sawBackspace = false;
    bool sawDelete = false;
    const auto eraseBack = [&] {
        if (state.uiTextCursorByteOffset == 0U)
            return;
        const std::size_t previous = PreviousUtf8Boundary(buffer, state.uiTextCursorByteOffset);
        buffer.erase(previous, state.uiTextCursorByteOffset - previous);
        state.uiTextCursorByteOffset = previous;
        changed = true;
    };
    const auto eraseForward = [&] {
        if (state.uiTextCursorByteOffset >= buffer.size())
            return;
        const std::size_t next = NextUtf8Boundary(buffer, state.uiTextCursorByteOffset);
        buffer.erase(state.uiTextCursorByteOffset, next - state.uiTextCursorByteOffset);
        changed = true;
    };
    for (const char32_t codePoint : input.textInput) {
        if (codePoint == U'\b') {
            sawBackspace = true;
            eraseBack();
            continue;
        }
        if (codePoint == 0x7FU) {
            sawDelete = true;
            eraseForward();
            continue;
        }
        if (codePoint == U'\n' && field->multiline && field->contentType == UIInputContentType::Standard) {
            // A line feed is the sole accepted control character for multiline fields.
        } else if (codePoint < U' ' || codePoint == 0x7FU) {
            continue;
        }
        if (codePoint != U'\n' && !AcceptsCharacter(field->contentType, buffer, state.uiTextCursorByteOffset, codePoint))
            continue;
        if (field->characterLimit > 0U && Utf8CodePointCount(buffer) >= field->characterLimit)
            continue;
        std::array<char, 4U> encoded{};
        const std::size_t encodedSize = EncodeUtf8(codePoint, encoded);
        if (encodedSize == 0U || buffer.size() + encodedSize >= UIText::MaxUtf8Bytes)
            continue;
        buffer.insert(state.uiTextCursorByteOffset, encoded.data(), encodedSize);
        state.uiTextCursorByteOffset += encodedSize;
        changed = true;
    }
    if (backspaceStarted && !sawBackspace)
        eraseBack();
    if (deleteStarted && !sawDelete)
        eraseForward();
    if (!changed)
        return false;
    if (!SetUITextContent(*text, buffer))
        return false;
    ui.MarkModified<UIText>(state.uiFocused);
    Queue(state, scene, SceneUIEventType::Changed, state.uiFocused, &input, 0.0F, 0.0F, buffer);
    return true;
}

[[nodiscard]] float PointerFraction(const SceneUIFrameElement& element, UIAxisDirection direction,
                                    Vec2 pointer) noexcept {
    const Vec2 horizontal{element.corners[1].x - element.corners[0].x,
                          element.corners[1].y - element.corners[0].y};
    const Vec2 vertical{element.corners[3].x - element.corners[0].x,
                        element.corners[3].y - element.corners[0].y};
    const Vec2 offset{pointer.x - element.corners[0].x, pointer.y - element.corners[0].y};
    const float determinant = horizontal.x * vertical.y - horizontal.y * vertical.x;
    const float horizontalFraction = std::abs(determinant) > 0.0001F
                                         ? (offset.x * vertical.y - offset.y * vertical.x) / determinant
                                         : 0.0F;
    const float verticalFraction = std::abs(determinant) > 0.0001F
                                       ? (horizontal.x * offset.y - horizontal.y * offset.x) / determinant
                                       : 0.0F;
    float fraction = 0.0F;
    switch (direction) {
    case UIAxisDirection::LeftToRight:
        fraction = horizontalFraction;
        break;
    case UIAxisDirection::RightToLeft:
        fraction = 1.0F - horizontalFraction;
        break;
    case UIAxisDirection::BottomToTop:
        fraction = 1.0F - verticalFraction;
        break;
    case UIAxisDirection::TopToBottom:
        fraction = verticalFraction;
        break;
    }
    return std::clamp(fraction, 0.0F, 1.0F);
}

[[nodiscard]] bool UpdateDraggedRange(Scene& scene, SceneState& state, const SceneUIInput& input) {
    if (!input.primaryDown || !input.pointerAvailable || !state.uiPressed.IsValid())
        return false;
    SceneUIComponents ui = scene.Components().UI();
    const SceneEntity sliderEntity = ClosestAncestorWith<UISlider>(scene, state.uiPressed);
    if (sliderEntity.IsValid()) {
        UISlider* slider = ui.TryGet<UISlider>(sliderEntity);
        const SceneUIFrameElement* element = Find(state.uiFrame, sliderEntity);
        if (slider == nullptr || element == nullptr)
            return false;
        const float fraction = PointerFraction(*element, slider->direction, input.pointerPosition);
        float value = std::lerp(slider->minimum, slider->maximum, fraction);
        if (slider->wholeNumbers)
            value = std::round(value);
        value = std::clamp(value, slider->minimum, slider->maximum);
        if (value == slider->value)
            return false;
        slider->value = value;
        ui.MarkModified<UISlider>(sliderEntity);
        Queue(state, scene, SceneUIEventType::Changed, sliderEntity, &input, value);
        return true;
    }
    const SceneEntity scrollbarEntity = ClosestAncestorWith<UIScrollbar>(scene, state.uiPressed);
    if (!scrollbarEntity.IsValid())
        return false;
    UIScrollbar* scrollbar = ui.TryGet<UIScrollbar>(scrollbarEntity);
    const SceneUIFrameElement* element = Find(state.uiFrame, scrollbarEntity);
    if (scrollbar == nullptr || element == nullptr)
        return false;
    const float pointerFraction = PointerFraction(*element, scrollbar->direction, input.pointerPosition);
    const float travel = 1.0F - scrollbar->size;
    const float value = travel > 0.0001F
                            ? std::clamp((pointerFraction - scrollbar->size * 0.5F) / travel, 0.0F, 1.0F)
                            : 0.0F;
    if (value == scrollbar->value)
        return false;
    scrollbar->value = value;
    ui.MarkModified<UIScrollbar>(scrollbarEntity);
    Queue(state, scene, SceneUIEventType::Changed, scrollbarEntity, &input, value);
    return true;
}

[[nodiscard]] bool UpdateScrollView(Scene& scene, SceneState& state, SceneEntity hovered, const SceneUIInput& input,
                                    bool pressStarted, float deltaSeconds) {
    SceneEntity entity{};
    if (input.scrollDelta != 0.0F)
        entity = ClosestAncestorWith<UIScrollView>(scene, hovered);
    if (!entity.IsValid() && input.primaryDown && !pressStarted && state.previousUIInput.pointerAvailable) {
        entity = ClosestAncestorWith<UIScrollView>(scene, state.uiPressed);
    }
    if (!entity.IsValid())
        return false;
    SceneUIComponents ui = scene.Components().UI();
    UIScrollView* scrollView = ui.TryGet<UIScrollView>(entity);
    const SceneUIFrameElement* element = Find(state.uiFrame, entity);
    if (scrollView == nullptr || element == nullptr)
        return false;
    const float beforeX = scrollView->scrollX;
    const float beforeY = scrollView->scrollY;
    if (input.scrollDelta != 0.0F) {
        state.uiInertialScrollView = {};
        state.uiScrollVelocity = {};
        const float wheel = -input.scrollDelta * scrollView->scrollSensitivity;
        if (scrollView->vertical)
            scrollView->scrollY = std::max(0.0F, scrollView->scrollY + wheel);
        else if (scrollView->horizontal)
            scrollView->scrollX = std::max(0.0F, scrollView->scrollX + wheel);
    }
    // A widget being dragged moves itself, not the scroll view it sits in.
    if (input.primaryDown && !pressStarted && input.pointerAvailable && state.previousUIInput.pointerAvailable &&
        !state.uiDragged.IsValid()) {
        const float scale = std::max(element->canvasScale, 0.0001F);
        // Past an end an elastic view follows the pointer at half speed, so the pull reads as resistance.
        const Vec2 limits = ScrollLimits(scene, state.uiFrame, entity, *scrollView);
        const auto resisted = [&](float offset, float limit, float delta) {
            const bool outside = offset < 0.0F || offset > limit;
            return scrollView->movementType == UIScrollMovement::Elastic && outside ? delta * 0.5F : delta;
        };
        if (scrollView->horizontal)
            scrollView->scrollX = ScrollFloor(*scrollView, scrollView->scrollX + resisted(scrollView->scrollX, limits.x,
                (state.previousUIInput.pointerPosition.x - input.pointerPosition.x) / scale));
        if (scrollView->vertical)
            scrollView->scrollY = ScrollFloor(*scrollView, scrollView->scrollY + resisted(scrollView->scrollY, limits.y,
                (state.previousUIInput.pointerPosition.y - input.pointerPosition.y) / scale));
        if (scrollView->inertia && deltaSeconds > 0.0F) {
            state.uiInertialScrollView = entity;
            state.uiScrollVelocity = {(scrollView->scrollX - beforeX) / deltaSeconds,
                                      (scrollView->scrollY - beforeY) / deltaSeconds};
        } else {
            state.uiInertialScrollView = {};
            state.uiScrollVelocity = {};
        }
    }
    if (scrollView->movementType == UIScrollMovement::Clamped || input.scrollDelta != 0.0F)
        static_cast<void>(ClampScrollOffsets(scene, state.uiFrame, entity, *scrollView));
    if (scrollView->snapToChildren)
        state.uiSnappingScrollView = entity;
    if (beforeX == scrollView->scrollX && beforeY == scrollView->scrollY)
        return false;
    ui.MarkModified<UIScrollView>(entity);
    Queue(state, scene, SceneUIEventType::Changed, entity, &input, scrollView->scrollX, scrollView->scrollY);
    return true;
}

// Eases a snapping scroll view onto the child nearest the start of the view, once nothing moves it any more.
[[nodiscard]] bool UpdateScrollSnap(Scene& scene, SceneState& state, const SceneUIInput& input, float deltaSeconds) {
    const SceneEntity entity = state.uiSnappingScrollView;
    if (!entity.IsValid() || input.primaryDown || state.uiInertialScrollView.IsValid() || deltaSeconds <= 0.0F)
        return false;
    SceneUIComponents ui = scene.Components().UI();
    UIScrollView* scrollView = scene.Entities().IsAlive(entity) ? ui.TryGet<UIScrollView>(entity) : nullptr;
    const SceneUIFrameElement* viewport = Find(state.uiFrame, entity);
    if (scrollView == nullptr || viewport == nullptr || !scrollView->snapToChildren) {
        state.uiSnappingScrollView = {};
        return false;
    }
    // Items are the view's children, or the children of its one content child.
    SceneEntity content = entity;
    if (scene.Hierarchy().ChildCount(entity) == 1U && scene.Hierarchy().ChildCount(scene.Hierarchy().ChildAt(entity, 0U)) > 0U)
        content = scene.Hierarchy().ChildAt(entity, 0U);
    const float scale = std::max(viewport->canvasScale, 0.0001F);
    const bool vertical = scrollView->vertical;
    float& offset = vertical ? scrollView->scrollY : scrollView->scrollX;
    const Vec2 limits = ScrollLimits(scene, state.uiFrame, entity, *scrollView);
    const float limit = vertical ? limits.y : limits.x;
    float target = offset;
    float nearest = std::numeric_limits<float>::max();
    for (std::size_t index = 0U; index < scene.Hierarchy().ChildCount(content); ++index) {
        const SceneUIFrameElement* item = Find(state.uiFrame, scene.Hierarchy().ChildAt(content, index));
        if (item == nullptr)
            continue;
        const float start = std::clamp(offset + (vertical ? item->rect.y - viewport->rect.y : item->rect.x - viewport->rect.x) / scale,
                                       0.0F, limit);
        if (std::abs(start - offset) < nearest) {
            nearest = std::abs(start - offset);
            target = start;
        }
    }
    const float before = offset;
    offset = std::abs(target - offset) < 0.5F ? target : std::lerp(offset, target, 1.0F - std::exp(-kUIScrollSnapRate * deltaSeconds));
    if (offset == target)
        state.uiSnappingScrollView = {};
    if (offset == before)
        return false;
    ui.MarkModified<UIScrollView>(entity);
    Queue(state, scene, SceneUIEventType::Changed, entity, &input, scrollView->scrollX, scrollView->scrollY);
    return true;
}

[[nodiscard]] bool UpdateScrollInertia(Scene& scene, SceneState& state, const SceneUIInput& input,
                                       float deltaSeconds) {
    if (!state.uiInertialScrollView.IsValid() || input.primaryDown || deltaSeconds <= 0.0F)
        return false;
    const SceneEntity entity = state.uiInertialScrollView;
    SceneUIComponents ui = scene.Components().UI();
    UIScrollView* scrollView = ui.TryGet<UIScrollView>(entity);
    if (scrollView == nullptr || !scene.Entities().IsActive(entity) || !scrollView->inertia) {
        state.uiInertialScrollView = {};
        state.uiScrollVelocity = {};
        return false;
    }
    const float beforeX = scrollView->scrollX;
    const float beforeY = scrollView->scrollY;
    if (scrollView->horizontal)
        scrollView->scrollX = ScrollFloor(*scrollView, scrollView->scrollX + state.uiScrollVelocity.x * deltaSeconds);
    if (scrollView->vertical)
        scrollView->scrollY = ScrollFloor(*scrollView, scrollView->scrollY + state.uiScrollVelocity.y * deltaSeconds);
    const Vec2 limits = ScrollLimits(scene, state.uiFrame, entity, *scrollView);
    if (scrollView->movementType == UIScrollMovement::Clamped)
        static_cast<void>(ClampScrollOffsets(scene, state.uiFrame, entity, *scrollView));
    // A flick stops at an end; an elastic view then springs back from wherever it overshot to.
    if (scrollView->movementType != UIScrollMovement::Unrestricted) {
        if ((scrollView->scrollX <= 0.0F && state.uiScrollVelocity.x < 0.0F) ||
            (scrollView->scrollX >= limits.x && state.uiScrollVelocity.x > 0.0F))
            state.uiScrollVelocity.x = 0.0F;
        if ((scrollView->scrollY <= 0.0F && state.uiScrollVelocity.y < 0.0F) ||
            (scrollView->scrollY >= limits.y && state.uiScrollVelocity.y > 0.0F))
            state.uiScrollVelocity.y = 0.0F;
    }
    const float decay = std::exp(-12.0F * deltaSeconds);
    state.uiScrollVelocity.x *= decay;
    state.uiScrollVelocity.y *= decay;
    if (std::hypot(state.uiScrollVelocity.x, state.uiScrollVelocity.y) < 0.01F) {
        state.uiInertialScrollView = {};
        state.uiScrollVelocity = {};
    }
    if (beforeX == scrollView->scrollX && beforeY == scrollView->scrollY)
        return false;
    ui.MarkModified<UIScrollView>(entity);
    Queue(state, scene, SceneUIEventType::Changed, entity, &input, scrollView->scrollX, scrollView->scrollY);
    return true;
}

// Focus is moved from the interaction helpers as well as from the public accessor, so the whole
// rule - refuse an entity the current frame cannot interact with, and announce the change once -
// lives here rather than in SceneUIAccess.
[[nodiscard]] bool SetUIFocus(Scene& scene, SceneState& state, SceneEntity entity) {
    if (entity == state.uiFocused)
        return true;
    const SceneUIFrameElement* target = Find(state.uiFrame, entity);
    if (target == nullptr || !target->interactionEnabled)
        return false;
    if (state.uiFocused.IsValid())
        Queue(state, scene, SceneUIEventType::Blurred, state.uiFocused);
    state.uiFocused = entity;
    if (const UIText* text = scene.Components().UI().TryGet<UIText>(entity);
        text != nullptr && scene.Components().UI().Has<UIInputField>(entity)) {
        state.uiTextCursorByteOffset = UITextContent(*text).size();
    } else {
        state.uiTextCursorByteOffset = 0U;
    }
    Queue(state, scene, SceneUIEventType::Focused, entity);
    return true;
}

void ClearUIFocus(Scene& scene, SceneState& state) noexcept {
    if (state.uiFocused.IsValid())
        Queue(state, scene, SceneUIEventType::Blurred, state.uiFocused);
    state.uiFocused = {};
    state.uiTextCursorByteOffset = 0U;
}

[[nodiscard]] std::vector<SceneEntity> PreorderSubtree(const Scene& scene, SceneEntity root) {
    std::vector<SceneEntity> order;
    std::vector<SceneEntity> pending{root};
    while (!pending.empty()) {
        const SceneEntity entity = pending.back();
        pending.pop_back();
        order.push_back(entity);
        for (std::size_t index = scene.Hierarchy().ChildCount(entity); index > 0U; --index)
            pending.push_back(scene.Hierarchy().ChildAt(entity, index - 1U));
    }
    return order;
}

[[nodiscard]] bool IsDescendantOrSelf(const Scene& scene, SceneEntity entity, SceneEntity ancestor) noexcept {
    for (SceneEntity current = entity; current.IsValid(); current = scene.Hierarchy().Parent(current))
        if (current == ancestor)
            return true;
    return false;
}

// Copies a subtree through the prefab path, so every UI reference inside it follows into the copy.
// `mapping` receives the copy of each original entity. Fails when the copy does not mirror the
// subtree node for node.
[[nodiscard]] SceneEntity CloneSubtree(Scene& scene, SceneEntity root, SceneEntity parent,
                                       std::unordered_map<std::uint64_t, SceneEntity>& mapping) {
    const std::vector<SceneEntity> order = PreorderSubtree(scene, root);
    const ScenePrefab captured = scene.Prefabs().Capture(scene.Entities().Object(root));
    if (captured.NodeCount() != order.size())
        return {};
    const ScenePrefabInstance instance =
        scene.Prefabs().Instantiate(captured, ScenePrefabInstantiationSettings{.parent = scene.Entities().Object(parent)});
    if (instance.ObjectCount() != order.size())
        return {};
    for (std::size_t index = 0U; index < order.size(); ++index)
        mapping[order[index].Id()] = instance.ObjectAt(static_cast<std::uint32_t>(index)).Entity();
    return instance.RootObject().Entity();
}

void SetGroup(Scene& scene, SceneEntity entity, float opacity, bool enabled) {
    UICanvasGroup group{};
    group.opacity = opacity;
    group.interactable = enabled;
    group.blocksRaycasts = enabled;
    scene.Components().UI().Set(entity, group);
}

void DestroyEntity(Scene& scene, SceneEntity entity) noexcept {
    if (entity.IsValid() && scene.Entities().IsAlive(entity))
        scene.Entities().Destroy(entity);
}

// Starts closing the open list: the blocker goes at once, the list fades out and is destroyed when the
// fade ends. Focus returns to the dropdown.
[[nodiscard]] bool HideDropdownList(Scene& scene, SceneState& state) {
    if (!state.uiDropdownList.has_value() || state.uiDropdownList->closing)
        return false;
    SceneState::UIDropdownList& open = *state.uiDropdownList;
    DestroyEntity(scene, open.blocker);
    open.blocker = {};
    open.closing = true;
    state.uiPendingFocus = open.dropdown;
    return true;
}

// Opens the dropdown's list: clones the template under the dropdown into its own canvas sorted above the
// dropdown's canvas, clones the template's item once per option with that option's label and image and
// the chosen option switched on, sizes the list to its items, opens it upwards when it would leave the
// screen, and puts a full-screen blocker under it that closes it. Focus moves to the chosen item.
[[nodiscard]] bool ShowDropdownList(Scene& scene, SceneState& state, SceneEntity dropdownEntity) {
    SceneUIComponents ui = scene.Components().UI();
    const UIDropdown* found = ui.TryGet<UIDropdown>(dropdownEntity);
    if (found == nullptr || found->optionCount == 0U)
        return false;
    const UIDropdown dropdown = *found;
    const SceneEntity templateEntity{dropdown.templateEntity};
    const SceneEntity itemText{dropdown.itemText};
    if (dropdown.templateEntity == 0U || !scene.Entities().IsAlive(templateEntity) ||
        scene.Hierarchy().Parent(templateEntity) != dropdownEntity || !ui.Has<UIRectTransform>(templateEntity))
        return false;
    SceneEntity item{};
    if (dropdown.itemText != 0U && scene.Entities().IsAlive(itemText) && IsDescendantOrSelf(scene, itemText, templateEntity))
        for (SceneEntity current = itemText; current.IsValid() && current != templateEntity; current = scene.Hierarchy().Parent(current))
            if (ui.Has<UIToggle>(current)) {
                item = current;
                break;
            }
    if (!item.IsValid() || !ui.Has<UIRectTransform>(scene.Hierarchy().Parent(item)))
        return false;
    SceneEntity rootCanvas{};
    for (SceneEntity current = dropdownEntity; current.IsValid(); current = scene.Hierarchy().Parent(current))
        if (ui.Has<UICanvas>(current))
            rootCanvas = current;
    if (!rootCanvas.IsValid())
        return false;
    if (state.uiDropdownList.has_value()) {
        DestroyEntity(scene, state.uiDropdownList->list);
        DestroyEntity(scene, state.uiDropdownList->blocker);
        state.uiDropdownList.reset();
    }
    const std::int32_t sortingOrder = ui.TryGet<UICanvas>(rootCanvas)->sortingOrder + kUIDropdownListSortingOffset;

    std::unordered_map<std::uint64_t, SceneEntity> listCopy;
    const SceneEntity list = CloneSubtree(scene, templateEntity, dropdownEntity, listCopy);
    if (!list.IsValid())
        return false;
    scene.Entities().SetName(list, "Dropdown List");
    UICanvas listCanvas{};
    listCanvas.sortingOrder = sortingOrder;
    ui.Set(list, listCanvas);
    SetGroup(scene, list, dropdown.alphaFadeSpeed > 0.0F ? 0.0F : 1.0F, true);

    const SceneEntity itemTemplate = listCopy.at(item.Id());
    const SceneEntity content = scene.Hierarchy().Parent(itemTemplate);
    const UIRectTransform itemRect = *ui.TryGet<UIRectTransform>(itemTemplate);
    const UIRectTransform contentRect = *ui.TryGet<UIRectTransform>(content);
    const float itemHeight = itemRect.offsetMax.y - itemRect.offsetMin.y;
    const float itemTop = itemRect.offsetMin.y;
    const float contentPadding = (contentRect.offsetMax.y - contentRect.offsetMin.y) - (itemTop + itemHeight);
    std::vector<SceneEntity> items;
    items.reserve(dropdown.optionCount);
    for (std::uint32_t index = 0U; index < dropdown.optionCount; ++index) {
        std::unordered_map<std::uint64_t, SceneEntity> itemCopy;
        const SceneEntity clone = CloneSubtree(scene, itemTemplate, content, itemCopy);
        if (!clone.IsValid())
            break;
        const UIDropdownOption& option = dropdown.options[index];
        scene.Entities().SetName(clone, "Item " + std::to_string(index) + ": " + std::string{UIDropdownOptionText(option)});
        UIRectTransform rect = itemRect;
        rect.offsetMin.y = itemTop + itemHeight * static_cast<float>(index);
        rect.offsetMax.y = rect.offsetMin.y + itemHeight;
        ui.Set(clone, rect);
        UIToggle toggle = *ui.TryGet<UIToggle>(clone);
        toggle.toggled = index == dropdown.value;
        ui.Set(clone, toggle);
        if (const auto text = itemCopy.find(listCopy.at(itemText.Id()).Id()); text != itemCopy.end())
            if (UIText* label = ui.TryGet<UIText>(text->second)) {
                static_cast<void>(SetUITextContent(*label, UIDropdownOptionText(option)));
                ui.MarkModified<UIText>(text->second);
            }
        if (dropdown.itemImage != 0U)
            if (const auto templateImage = listCopy.find(dropdown.itemImage); templateImage != listCopy.end())
                if (const auto image = itemCopy.find(templateImage->second.Id()); image != itemCopy.end())
                    if (UIImage* picture = ui.TryGet<UIImage>(image->second)) {
                        picture->imageAssetId = option.imageAssetId;
                        ui.MarkModified<UIImage>(image->second);
                        SetGroup(scene, image->second, option.imageAssetId != 0U ? 1.0F : 0.0F, false);
                    }
        items.push_back(clone);
    }
    // The template's own item stays in the copy as the pattern; it is never shown or chosen.
    SetGroup(scene, itemTemplate, 0.0F, false);
    if (items.size() != dropdown.optionCount) {
        DestroyEntity(scene, list);
        return false;
    }
    // Explicit up/down between items, so navigation walks the list in option order and never leaves it
    // for the blocker or the widgets under the list.
    for (std::size_t index = 0U; index < items.size(); ++index) {
        UISelectable selectable = ui.TryGet<UISelectable>(items[index]) != nullptr ? *ui.TryGet<UISelectable>(items[index]) : UISelectable{};
        selectable.navigationMode = UINavigationMode::Explicit;
        selectable.navigationUp = index > 0U ? items[index - 1U].Id() : 0U;
        selectable.navigationDown = index + 1U < items.size() ? items[index + 1U].Id() : 0U;
        selectable.navigationLeft = selectable.navigationRight = 0U;
        ui.Set(items[index], selectable);
    }
    // The content grows to hold every item; a list authored taller than its items shrinks to fit them.
    UIRectTransform grown = contentRect;
    const float contentHeight = itemTop + itemHeight * static_cast<float>(items.size()) + contentPadding;
    grown.offsetMax.y = grown.offsetMin.y + contentHeight;
    ui.Set(content, grown);
    UIRectTransform listRect = *ui.TryGet<UIRectTransform>(list);
    if (listRect.anchorMin.y == listRect.anchorMax.y) {
        const float listHeight = listRect.offsetMax.y - listRect.offsetMin.y;
        if (listHeight > contentHeight)
            listRect.offsetMax.y -= listHeight - contentHeight;
        // Open upwards when the list would run past the bottom of the screen and there is more room above.
        if (const SceneUIFrameElement* control = Find(state.uiFrame, dropdownEntity)) {
            const float scale = std::max(control->canvasScale, 0.0001F);
            const float height = (listRect.offsetMax.y - listRect.offsetMin.y) * scale;
            const float spaceBelow = state.uiFrame.viewportSize.y - (control->rect.y + control->rect.height);
            if (listRect.anchorMin.y >= 1.0F && height + listRect.offsetMin.y * scale > spaceBelow &&
                control->rect.y > spaceBelow) {
                const float top = listRect.offsetMin.y;
                const float bottom = listRect.offsetMax.y;
                listRect.anchorMin.y = listRect.anchorMax.y = 0.0F;
                listRect.offsetMin.y = -bottom;
                listRect.offsetMax.y = -top;
            }
        }
    }
    ui.Set(list, listRect);

    // The blocker covers the screen just under the list; a press anywhere outside the list lands on it.
    SceneObjectDesc blockerDesc;
    blockerDesc.name = "Blocker";
    blockerDesc.parent = scene.Entities().Object(rootCanvas);
    const SceneEntity blocker = scene.Entities().CreateObject(std::move(blockerDesc)).Entity();
    UIRectTransform blockerRect{};
    blockerRect.anchorMax = {1.0F, 1.0F};
    blockerRect.offsetMax = {};
    ui.Set(blocker, blockerRect);
    UICanvas blockerCanvas{};
    blockerCanvas.sortingOrder = sortingOrder - 1;
    ui.Set(blocker, blockerCanvas);
    UIBorder transparent{};
    transparent.backgroundColor = {0.0F, 0.0F, 0.0F, 0.0F};
    transparent.borderColor = {0.0F, 0.0F, 0.0F, 0.0F};
    ui.Set(blocker, transparent);
    UISelectable blockerSelectable{};
    blockerSelectable.navigationMode = UINavigationMode::None;
    ui.Set(blocker, blockerSelectable);

    state.uiDropdownList = SceneState::UIDropdownList{
        .dropdown = dropdownEntity, .list = list, .blocker = blocker, .items = std::move(items),
        .alpha = dropdown.alphaFadeSpeed > 0.0F ? 0.0F : 1.0F, .closing = false};
    state.uiPendingFocus = state.uiDropdownList->items[dropdown.value];
    return true;
}

// Chooses the option an item stands for, reports the change against the dropdown and closes the list.
[[nodiscard]] bool SelectDropdownItem(Scene& scene, SceneState& state, std::size_t index, const SceneUIInput& input) {
    const SceneEntity dropdownEntity = state.uiDropdownList->dropdown;
    SceneUIComponents ui = scene.Components().UI();
    UIDropdown* dropdown = ui.TryGet<UIDropdown>(dropdownEntity);
    if (dropdown != nullptr && index < dropdown->optionCount && dropdown->value != index) {
        dropdown->value = static_cast<std::uint32_t>(index);
        ui.MarkModified<UIDropdown>(dropdownEntity);
        Queue(state, scene, SceneUIEventType::Changed, dropdownEntity, &input, static_cast<float>(index), 0.0F,
              UIDropdownOptionText(dropdown->options[index]));
    }
    static_cast<void>(HideDropdownList(scene, state));
    return true;
}

// Advances the list's fade, destroys it once it has faded out, and drops it when its dropdown is gone.
[[nodiscard]] bool UpdateDropdownList(Scene& scene, SceneState& state, float deltaSeconds) {
    if (!state.uiDropdownList.has_value())
        return false;
    SceneState::UIDropdownList& open = *state.uiDropdownList;
    const UIDropdown* dropdown = scene.Entities().IsAlive(open.dropdown)
                                     ? scene.Components().UI().TryGet<UIDropdown>(open.dropdown)
                                     : nullptr;
    if (dropdown == nullptr || !scene.Entities().IsAlive(open.list)) {
        DestroyEntity(scene, open.list);
        DestroyEntity(scene, open.blocker);
        state.uiDropdownList.reset();
        return true;
    }
    const float speed = dropdown->alphaFadeSpeed;
    const float target = open.closing ? 0.0F : 1.0F;
    const float before = open.alpha;
    open.alpha = speed <= 0.0F ? target
                               : std::clamp(open.alpha + (open.closing ? -1.0F : 1.0F) * deltaSeconds / speed, 0.0F, 1.0F);
    if (open.closing && open.alpha <= 0.0F) {
        DestroyEntity(scene, open.list);
        state.uiDropdownList.reset();
        return true;
    }
    if (open.alpha == before)
        return false;
    SetGroup(scene, open.list, open.alpha, !open.closing);
    return true;
}

// Keeps each linked scrollbar in step with its scroll view: size is the visible share, value the
// position. While the scrollbar is being dragged it drives the scroll instead. It is hidden while
// everything fits.
[[nodiscard]] bool SyncScrollbars(Scene& scene, SceneState& state) {
    bool changed = false;
    SceneUIComponents ui = scene.Components().UI();
    for (const SceneUIFrameElement& element : state.uiFrame.elements) {
      for (const bool vertical : {true, false}) {
        if (!element.scrollView.has_value())
            continue;
        const std::uint64_t linked = vertical ? element.scrollView->verticalScrollbar : element.scrollView->horizontalScrollbar;
        if (linked == 0U)
            continue;
        const SceneEntity scrollbarEntity{linked};
        UIScrollView* scrollView = ui.TryGet<UIScrollView>(element.entity);
        UIScrollbar* scrollbar = scene.Entities().IsAlive(scrollbarEntity) ? ui.TryGet<UIScrollbar>(scrollbarEntity) : nullptr;
        if (scrollView == nullptr || scrollbar == nullptr)
            continue;
        const Vec2 limits = ScrollLimits(scene, state.uiFrame, element.entity, *scrollView);
        const float limit = vertical ? limits.y : limits.x;
        float& offset = vertical ? scrollView->scrollY : scrollView->scrollX;
        const float viewSize = (vertical ? element.rect.height : element.rect.width) / std::max(element.canvasScale, 0.0001F);
        const bool dragging = state.uiPressed.IsValid() && IsDescendantOrSelf(scene, state.uiPressed, scrollbarEntity);
        if (dragging && limit > 0.0F) {
            const float dragged = scrollbar->value * limit;
            if (dragged != offset) {
                offset = dragged;
                ui.MarkModified<UIScrollView>(element.entity);
                changed = true;
            }
            continue;
        }
        const float size = limit > 0.0F ? std::clamp(viewSize / (viewSize + limit), 0.0F, 1.0F) : 1.0F;
        const float value = limit > 0.0F ? std::clamp(offset / limit, 0.0F, 1.0F) : 0.0F;
        if (size != scrollbar->size || value != scrollbar->value) {
            scrollbar->size = size;
            scrollbar->value = value;
            ui.MarkModified<UIScrollbar>(scrollbarEntity);
            changed = true;
        }
        const UICanvasGroup* shown = ui.TryGet<UICanvasGroup>(scrollbarEntity);
        const bool visible = limit > 0.0F;
        if (shown == nullptr || (shown->opacity > 0.0F) != visible) {
            SetGroup(scene, scrollbarEntity, visible ? 1.0F : 0.0F, visible);
            changed = true;
        }
      }
    }
    return changed;
}

// A focused range control takes navigation along its own axis as a value change, which is how a pad
// or keyboard player adjusts a volume slider at all. Returns true when the step was consumed, so the
// same press does not also move focus away from the control being adjusted.
[[nodiscard]] bool StepFocusedRange(Scene& scene, SceneState& state, Vec2 direction, bool& changed) {
    if (!state.uiFocused.IsValid())
        return false;
    SceneUIComponents ui = scene.Components().UI();
    const auto signedStep = [direction](UIAxisDirection axis) noexcept {
        switch (axis) {
        case UIAxisDirection::LeftToRight:
            return direction.x;
        case UIAxisDirection::RightToLeft:
            return -direction.x;
        case UIAxisDirection::TopToBottom:
            return direction.y;
        case UIAxisDirection::BottomToTop:
            return -direction.y;
        }
        return 0.0F;
    };
    if (UISlider* slider = ui.TryGet<UISlider>(state.uiFocused)) {
        const float sign = signedStep(slider->direction);
        if (sign == 0.0F)
            return false;
        const float step = slider->wholeNumbers ? 1.0F
                                                : (slider->maximum - slider->minimum) * kUISliderNavigationStepFraction;
        float value = std::clamp(slider->value + sign * step, slider->minimum, slider->maximum);
        if (slider->wholeNumbers)
            value = std::clamp(std::round(value), slider->minimum, slider->maximum);
        if (value != slider->value) {
            slider->value = value;
            ui.MarkModified<UISlider>(state.uiFocused);
            Queue(state, scene, SceneUIEventType::Changed, state.uiFocused, nullptr, value);
            changed = true;
        }
        return true;
    }
    if (UIScrollbar* scrollbar = ui.TryGet<UIScrollbar>(state.uiFocused)) {
        const float sign = signedStep(scrollbar->direction);
        if (sign == 0.0F)
            return false;
        const float value = std::clamp(scrollbar->value + sign * kUIScrollbarNavigationStep, 0.0F, 1.0F);
        if (value != scrollbar->value) {
            scrollbar->value = value;
            ui.MarkModified<UIScrollbar>(state.uiFocused);
            Queue(state, scene, SceneUIEventType::Changed, state.uiFocused, nullptr, value);
            changed = true;
        }
        return true;
    }
    return false;
}

// A focused scroll view scrolls under navigation. It gives the step back once it reaches the end in
// that direction, so focus is never trapped inside it.
[[nodiscard]] bool StepFocusedScrollView(Scene& scene, SceneState& state, Vec2 direction, const SceneUIInput& input) {
    if (!state.uiFocused.IsValid())
        return false;
    SceneUIComponents ui = scene.Components().UI();
    UIScrollView* scrollView = ui.TryGet<UIScrollView>(state.uiFocused);
    if (scrollView == nullptr)
        return false;
    const float beforeX = scrollView->scrollX;
    const float beforeY = scrollView->scrollY;
    if (scrollView->horizontal)
        scrollView->scrollX = std::max(0.0F, scrollView->scrollX + direction.x * scrollView->scrollSensitivity);
    if (scrollView->vertical)
        scrollView->scrollY = std::max(0.0F, scrollView->scrollY + direction.y * scrollView->scrollSensitivity);
    static_cast<void>(ClampScrollOffsets(scene, state.uiFrame, state.uiFocused, *scrollView));
    if (beforeX == scrollView->scrollX && beforeY == scrollView->scrollY)
        return false;
    ui.MarkModified<UIScrollView>(state.uiFocused);
    Queue(state, scene, SceneUIEventType::Changed, state.uiFocused, &input, scrollView->scrollX, scrollView->scrollY);
    return true;
}

// Scrolls every scroll view around the focused widget until the widget is inside its viewport.
// Navigation walks a long list row by row; without this the focus would leave the visible part of
// the list and the player would be choosing rows they cannot see.
[[nodiscard]] bool ScrollFocusedIntoView(Scene& scene, SceneState& state, const SceneUIInput& input) {
    const SceneUIFrameElement* focused = Find(state.uiFrame, state.uiFocused);
    if (focused == nullptr)
        return false;
    SceneUIComponents ui = scene.Components().UI();
    bool scrolled = false;
    for (SceneEntity ancestor = scene.Hierarchy().Parent(state.uiFocused); ancestor.IsValid();
         ancestor = scene.Hierarchy().Parent(ancestor)) {
        UIScrollView* scrollView = ui.TryGet<UIScrollView>(ancestor);
        const SceneUIFrameElement* viewport = Find(state.uiFrame, ancestor);
        if (scrollView == nullptr || viewport == nullptr)
            continue;
        const float scale = std::max(viewport->canvasScale, 0.0001F);
        const float beforeX = scrollView->scrollX;
        const float beforeY = scrollView->scrollY;
        // Screen-space overshoot converted back to the scroll view's logical units. An item larger
        // than the viewport aligns its leading edge rather than oscillating between both edges.
        const auto reveal = [scale](float& scroll, float itemStart, float itemSize, float viewStart, float viewSize) {
            if (itemStart < viewStart)
                scroll -= (viewStart - itemStart) / scale;
            else if (itemStart + itemSize > viewStart + viewSize)
                scroll += std::min(itemStart + itemSize - viewStart - viewSize, itemStart - viewStart) / scale;
            scroll = std::max(0.0F, scroll);
        };
        if (scrollView->horizontal)
            reveal(scrollView->scrollX, focused->rect.x, focused->rect.width, viewport->rect.x, viewport->rect.width);
        if (scrollView->vertical)
            reveal(scrollView->scrollY, focused->rect.y, focused->rect.height, viewport->rect.y, viewport->rect.height);
        static_cast<void>(ClampScrollOffsets(scene, state.uiFrame, ancestor, *scrollView));
        if (beforeX == scrollView->scrollX && beforeY == scrollView->scrollY)
            continue;
        ui.MarkModified<UIScrollView>(ancestor);
        Queue(state, scene, SceneUIEventType::Changed, ancestor, &input, scrollView->scrollX, scrollView->scrollY);
        scrolled = true;
    }
    return scrolled;
}

// Moves every animating Canvas Group towards where its `visible` flag puts it.
[[nodiscard]] bool UpdateGroupTransitions(Scene& scene, SceneState& state, float deltaSeconds) {
    bool changed = false;
    const SceneUIComponentQueries ui = std::as_const(scene).Components().UI();
    std::vector<SceneEntity> pending = scene.Hierarchy().RootEntities();
    while (!pending.empty()) {
        const SceneEntity entity = pending.back();
        pending.pop_back();
        for (std::size_t index = 0U; index < scene.Hierarchy().ChildCount(entity); ++index)
            pending.push_back(scene.Hierarchy().ChildAt(entity, index));
        const UICanvasGroup* group = ui.TryGet<UICanvasGroup>(entity);
        const auto entry = state.uiGroupShown.find(entity.Id());
        if (group == nullptr) {
            if (entry != state.uiGroupShown.end())
                state.uiGroupShown.erase(entry);
            continue;
        }
        const float target = group->visible ? 1.0F : 0.0F;
        if (entry == state.uiGroupShown.end()) {
            // A group seen for the first time starts where it is set; only a later change animates.
            state.uiGroupShown.emplace(entity.Id(), target);
            continue;
        }
        float& shown = entry->second;
        if (shown == target)
            continue;
        const float step = group->transitionSeconds <= 0.0F ? 1.0F : deltaSeconds / group->transitionSeconds;
        shown = target > shown ? std::min(target, shown + step) : std::max(target, shown - step);
        changed = true;
    }
    return changed;
}

[[nodiscard]] bool Activate(Scene& scene, SceneState& state, SceneEntity entity, const SceneUIInput& input) {
    SceneUIComponents ui = scene.Components().UI();
    if (state.uiDropdownList.has_value() && !state.uiDropdownList->closing) {
        if (entity == state.uiDropdownList->blocker)
            return HideDropdownList(scene, state);
        const auto& items = state.uiDropdownList->items;
        if (const auto item = std::ranges::find(items, entity); item != items.end())
            return SelectDropdownItem(scene, state, static_cast<std::size_t>(item - items.begin()), input);
    }
    if (UIToggle* toggle = ui.TryGet<UIToggle>(entity)) {
        const std::uint64_t group = toggle->group;
        // The chosen option of a radio group stays chosen until another one is.
        if (group != 0U && toggle->toggled && !toggle->allowSwitchOff)
            return false;
        toggle->toggled = !toggle->toggled;
        const bool switchedOn = toggle->toggled;
        ui.MarkModified<UIToggle>(entity);
        Queue(state, scene, SceneUIEventType::Changed, entity, &input, switchedOn ? 1.0F : 0.0F);
        if (group != 0U && switchedOn) {
            std::vector<SceneEntity> pending = scene.Hierarchy().RootEntities();
            while (!pending.empty()) {
                const SceneEntity other = pending.back();
                pending.pop_back();
                for (std::size_t index = 0U; index < scene.Hierarchy().ChildCount(other); ++index)
                    pending.push_back(scene.Hierarchy().ChildAt(other, index));
                UIToggle* member = other != entity ? ui.TryGet<UIToggle>(other) : nullptr;
                if (member == nullptr || member->group != group || !member->toggled)
                    continue;
                member->toggled = false;
                ui.MarkModified<UIToggle>(other);
                Queue(state, scene, SceneUIEventType::Changed, other, &input, 0.0F);
            }
        }
        return true;
    }
    if (ui.Has<UIDropdown>(entity))
        return ShowDropdownList(scene, state, entity);
    if (UIWidgetSwitcher* switcher = ui.TryGet<UIWidgetSwitcher>(entity)) {
        const std::size_t count = scene.Hierarchy().ChildCount(entity);
        if (count == 0U)
            return false;
        switcher->visibleChildIndex = static_cast<std::uint32_t>((switcher->visibleChildIndex + 1U) % count);
        ui.MarkModified<UIWidgetSwitcher>(entity);
        Queue(state, scene, SceneUIEventType::Changed, entity, &input, static_cast<float>(switcher->visibleChildIndex));
        return true;
    }
    return false;
}

} // namespace

std::span<const SceneUIEventDescriptor> SceneUIEventCatalog() noexcept {
    return kEventDescriptors;
}

const SceneUIEventDescriptor* FindSceneUIEventDescriptor(SceneUIEventType type) noexcept {
    const auto found = std::ranges::find(kEventDescriptors, type, &SceneUIEventDescriptor::type);
    return found == kEventDescriptors.end() ? nullptr : &*found;
}

std::string_view SceneUIEventName(const SceneUIEvent& event) noexcept {
    const auto end = std::find(event.name.begin(), event.name.end(), '\0');
    return {event.name.data(), static_cast<std::size_t>(end - event.name.begin())};
}
std::string_view SceneUIEventText(const SceneUIEvent& event) noexcept {
    const auto end = std::find(event.text.begin(), event.text.end(), '\0');
    return {event.text.data(), static_cast<std::size_t>(end - event.text.begin())};
}
namespace {

// Whether the point lands on an opaque enough part of the element's image. Without published opacity the
// whole rectangle counts, as it did before the image could be tested.
[[nodiscard]] bool OpaqueAt(const SceneUIFrameElement& element, Vec2 point) noexcept {
    if (element.hitAlpha == nullptr || !element.image.has_value() || element.hitAlpha->width == 0U ||
        element.hitAlpha->height == 0U)
        return true;
    const float across = PointerFraction(element, UIAxisDirection::LeftToRight, point);
    const float down = PointerFraction(element, UIAxisDirection::TopToBottom, point);
    const kb::math::Rect uv = element.image->uvRect;
    const float u = std::clamp(uv.x + uv.width * across, 0.0F, 1.0F);
    const float v = std::clamp(uv.y + uv.height * down, 0.0F, 1.0F);
    const SceneUIImageAlpha& alpha = *element.hitAlpha;
    const std::uint32_t x = std::min(alpha.width - 1U, static_cast<std::uint32_t>(u * static_cast<float>(alpha.width)));
    const std::uint32_t y = std::min(alpha.height - 1U, static_cast<std::uint32_t>(v * static_cast<float>(alpha.height)));
    const std::size_t index = static_cast<std::size_t>(y) * alpha.width + x;
    return index < alpha.alpha.size() &&
           static_cast<float>(alpha.alpha[index]) / 255.0F >= element.image->alphaHitThreshold;
}

} // namespace

SceneEntity SceneUIFrame::HitTest(Vec2 point) const noexcept {
    for (auto iterator = elements.rbegin(); iterator != elements.rend(); ++iterator) {
        const bool insideEveryMask = std::ranges::all_of(iterator->clipQuads, [point](const auto& quad) {
            return InQuad(quad, point);
        });
        if (iterator->hitTestable && ContainsRect(iterator->clipRect, point) && insideEveryMask &&
            InQuad(iterator->hitCorners, point) && OpaqueAt(*iterator, point))
            return iterator->entity;
    }
    return {};
}
SceneEntity SceneUIFrame::HitTestExcluding(Vec2 point, SceneEntity excluded, const Scene& scene) const noexcept {
    for (auto iterator = elements.rbegin(); iterator != elements.rend(); ++iterator) {
        if (!iterator->hitTestable || IsDescendantOrSelf(scene, iterator->entity, excluded))
            continue;
        const bool insideEveryMask = std::ranges::all_of(iterator->clipQuads, [point](const auto& quad) {
            return InQuad(quad, point);
        });
        if (ContainsRect(iterator->clipRect, point) && insideEveryMask && InQuad(iterator->hitCorners, point) &&
            OpaqueAt(*iterator, point))
            return iterator->entity;
    }
    return {};
}

SceneUIQueries::SceneUIQueries(const Scene& scene) noexcept : scene_(scene) {}
UIEdges SceneUIQueries::SafeAreaInsets() const noexcept {
    return SceneAccess::State(scene_).uiSafeAreaInsets;
}
SceneEntity SceneUIQueries::PlayerFocused(std::uint32_t player) const noexcept {
    const SceneState& state = SceneAccess::State(scene_);
    if (player == 0U)
        return state.uiFocused;
    return player <= state.uiPlayers.size() ? state.uiPlayers[player - 1U].focused : SceneEntity{};
}
bool SceneUIQueries::HasImageAlpha(std::uint64_t imageAssetId) const noexcept {
    return SceneAccess::State(scene_).uiImageAlpha.contains(imageAssetId);
}
std::vector<std::uint64_t> SceneUIQueries::PendingImageAlpha() const {
    const SceneState& state = SceneAccess::State(scene_);
    std::vector<std::uint64_t> pending;
    for (const SceneUIFrameElement& element : state.uiFrame.elements)
        if (element.image.has_value() && element.image->alphaHitThreshold > 0.0F && element.image->imageAssetId != 0U &&
            !state.uiImageAlpha.contains(element.image->imageAssetId) &&
            std::ranges::find(pending, element.image->imageAssetId) == pending.end())
            pending.push_back(element.image->imageAssetId);
    return pending;
}
SceneEntity SceneUIQueries::Dragged() const noexcept {
    const SceneState& state = SceneAccess::State(scene_);
    return state.uiDragging ? state.uiDragged : SceneEntity{};
}
const SceneUIFrame& SceneUIQueries::Frame() const noexcept {
    return SceneAccess::State(scene_).uiFrame;
}
bool SceneUIQueries::BuildFrame(float viewportWidth, float viewportHeight, SceneUIFrame& output) const {
    if (!std::isfinite(viewportWidth) || !std::isfinite(viewportHeight) || viewportWidth <= 0.0F ||
        viewportHeight <= 0.0F)
        return false;
    const SceneState& state = SceneAccess::State(scene_);
    SceneUIFrame frame;
    if (SceneEntityCounter::CountWithComponent(state.world, state.world.Component<UICanvas>()) == 0U) {
        frame.viewportSize = {viewportWidth, viewportHeight};
    } else if (!FrameBuilder{scene_, {viewportWidth, viewportHeight}}.Build(frame)) {
        // The refusal is the only thing the caller can act on, so it has to survive the
        // discarded frame - the renderer reports it instead of dropping the whole submit.
        output.elements.clear();
        output.refusal = frame.refusal;
        return false;
    }
    ResolveInteractionPresentation(scene_, state, state.previousUIInput.primaryDown, 0.0F, state.uiFrame, frame, false);
    output = std::move(frame);
    return true;
}
SceneEntity SceneUIQueries::HitTest(Vec2 point) const noexcept {
    return Frame().HitTest(point);
}
SceneEntity SceneUIQueries::Hovered() const noexcept {
    return SceneAccess::State(scene_).uiHovered;
}
SceneEntity SceneUIQueries::Pressed() const noexcept {
    return SceneAccess::State(scene_).uiPressed;
}
SceneEntity SceneUIQueries::Focused() const noexcept {
    return SceneAccess::State(scene_).uiFocused;
}
bool SceneUIQueries::HasFocusedTextInput() const noexcept {
    return Focused().IsValid() && scene_.Components().UI().Has<UIInputField>(Focused());
}
std::span<const SceneUIEvent> SceneUIQueries::Events() const noexcept {
    return SceneAccess::State(scene_).uiEvents;
}

SceneUIAccess::SceneUIAccess(Scene& scene) noexcept : scene_(scene) {}
bool SceneUIAccess::SetViewport(float width, float height) noexcept {
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0F || height <= 0.0F)
        return false;
    SceneAccess::State(scene_).uiViewportSize = {width, height};
    return true;
}
bool SceneUIAccess::SetSafeAreaInsets(UIEdges insets) noexcept {
    const auto valid = [](float value) noexcept { return std::isfinite(value) && value >= 0.0F; };
    if (!valid(insets.left) || !valid(insets.top) || !valid(insets.right) || !valid(insets.bottom))
        return false;
    SceneAccess::State(scene_).uiSafeAreaInsets = insets;
    return true;
}
UIEdges SceneUIAccess::SafeAreaInsets() const noexcept {
    return SceneAccess::State(scene_).uiSafeAreaInsets;
}
void SceneUIAccess::PublishImageAlpha(std::uint64_t imageAssetId, SceneUIImageAlpha alpha) {
    SceneAccess::State(scene_).uiImageAlpha[imageAssetId] = std::make_shared<const SceneUIImageAlpha>(std::move(alpha));
}
bool SceneUIAccess::UpdateFromInput(float deltaSeconds) {
    SceneState& state = SceneAccess::State(scene_);
    const kb::input::InputDeviceState& device = scene_.Input().DeviceState();
    if (device.PointerViewportWidth() > 0U && device.PointerViewportHeight() > 0U) {
        state.uiViewportSize = {static_cast<float>(device.PointerViewportWidth()),
                                static_cast<float>(device.PointerViewportHeight())};
    }
    if (state.uiViewportSize.x <= 0.0F || state.uiViewportSize.y <= 0.0F)
        return false;
    SceneUIInput input;
    input.pointerAvailable = device.HasFocus();
    input.pointerPosition = {device.PointerX(), device.PointerY()};
    input.primaryDown = device.IsKeyDown(kb::input::InputKey::MouseLeft);
    for (const kb::input::InputTouchPoint& touch : device.TouchPoints()) {
        if (touch.phase == kb::input::InputTouchPhase::Ended)
            continue;
        input.pointerAvailable = true;
        input.pointerPosition = {touch.x, touch.y};
        input.primaryDown = true;
        break;
    }
    const bool reverseTab =
        device.IsKeyDown(kb::input::InputKey::Tab) &&
        (device.IsKeyDown(kb::input::InputKey::LeftShift) || device.IsKeyDown(kb::input::InputKey::RightShift));
    // The left stick navigates like the D-pad once it leans past the threshold. Its Y axis points up
    // while UI rows grow downwards, hence the sign flip. Only the dominant axis counts, so a diagonal
    // lean does not fire two directions at once.
    const float stickX = device.GetValue(kb::input::InputKey::GamepadLeftStickX);
    const float stickY = device.GetValue(kb::input::InputKey::GamepadLeftStickY);
    const bool stickVertical = std::abs(stickY) >= std::abs(stickX);
    const bool stickUp = stickVertical && stickY >= kUIStickNavigationThreshold;
    const bool stickDown = stickVertical && stickY <= -kUIStickNavigationThreshold;
    const bool stickLeft = !stickVertical && stickX <= -kUIStickNavigationThreshold;
    const bool stickRight = !stickVertical && stickX >= kUIStickNavigationThreshold;
    input.navigateUp = device.IsKeyDown(kb::input::InputKey::ArrowUp) ||
                       device.IsKeyDown(kb::input::InputKey::GamepadDPadUp) || stickUp || reverseTab;
    input.navigateDown = device.IsKeyDown(kb::input::InputKey::ArrowDown) ||
                         device.IsKeyDown(kb::input::InputKey::GamepadDPadDown) || stickDown ||
                         (device.IsKeyDown(kb::input::InputKey::Tab) && !reverseTab);
    input.navigateLeft = device.IsKeyDown(kb::input::InputKey::ArrowLeft) ||
                         device.IsKeyDown(kb::input::InputKey::GamepadDPadLeft) || stickLeft;
    input.navigateRight = device.IsKeyDown(kb::input::InputKey::ArrowRight) ||
                          device.IsKeyDown(kb::input::InputKey::GamepadDPadRight) || stickRight;
    input.submitDown =
        device.IsKeyDown(kb::input::InputKey::Enter) || device.IsKeyDown(kb::input::InputKey::GamepadFaceBottom);
    input.cancelDown =
        device.IsKeyDown(kb::input::InputKey::Escape) || device.IsKeyDown(kb::input::InputKey::GamepadFaceRight);
    input.backspaceDown = device.IsKeyDown(kb::input::InputKey::Backspace);
    input.deleteDown = device.IsKeyDown(kb::input::InputKey::Delete);
    input.scrollDelta = device.GetValue(kb::input::InputKey::MouseWheel);
    input.textInput = device.TextInput();
    for (std::uint8_t pad = 1U; pad <= input.otherPlayers.size(); ++pad) {
        using kb::input::InputKey;
        const float x = device.GetValue(InputKey::GamepadLeftStickX, pad);
        const float y = device.GetValue(InputKey::GamepadLeftStickY, pad);
        const bool vertical = std::abs(y) >= std::abs(x);
        SceneUIPlayerNavigation& player = input.otherPlayers[pad - 1U];
        player.navigateUp = device.IsKeyDown(InputKey::GamepadDPadUp, pad) || (vertical && y >= kUIStickNavigationThreshold);
        player.navigateDown = device.IsKeyDown(InputKey::GamepadDPadDown, pad) || (vertical && y <= -kUIStickNavigationThreshold);
        player.navigateLeft = device.IsKeyDown(InputKey::GamepadDPadLeft, pad) || (!vertical && x <= -kUIStickNavigationThreshold);
        player.navigateRight = device.IsKeyDown(InputKey::GamepadDPadRight, pad) || (!vertical && x >= kUIStickNavigationThreshold);
        player.submitDown = device.IsKeyDown(InputKey::GamepadFaceBottom, pad);
        player.cancelDown = device.IsKeyDown(InputKey::GamepadFaceRight, pad);
    }
    return Update(state.uiViewportSize.x, state.uiViewportSize.y, input, deltaSeconds);
}
const SceneUIFrame& SceneUIAccess::Frame() const noexcept {
    return SceneUIQueries{scene_}.Frame();
}
SceneEntity SceneUIAccess::HitTest(Vec2 point) const noexcept {
    return Frame().HitTest(point);
}
SceneEntity SceneUIAccess::Hovered() const noexcept {
    return SceneAccess::State(scene_).uiHovered;
}
SceneEntity SceneUIAccess::Pressed() const noexcept {
    return SceneAccess::State(scene_).uiPressed;
}
SceneEntity SceneUIAccess::Focused() const noexcept {
    return SceneAccess::State(scene_).uiFocused;
}
bool SceneUIAccess::HasFocusedTextInput() const noexcept {
    return SceneUIQueries{scene_}.HasFocusedTextInput();
}
std::span<const SceneUIEvent> SceneUIAccess::Events() const noexcept {
    return SceneAccess::State(scene_).uiEvents;
}
std::vector<SceneUIEvent> SceneUIAccess::DrainEvents() {
    std::vector<SceneUIEvent> events;
    events.swap(SceneAccess::State(scene_).uiEvents);
    return events;
}

bool SceneUIAccess::SetFocus(SceneEntity entity) {
    return SetUIFocus(scene_, SceneAccess::State(scene_), entity);
}
void SceneUIAccess::ClearFocus() noexcept {
    ClearUIFocus(scene_, SceneAccess::State(scene_));
}

bool SceneUIAccess::Update(float viewportWidth, float viewportHeight, const SceneUIInput& input, float deltaSeconds) {
    if (!std::isfinite(viewportWidth) || !std::isfinite(viewportHeight) || viewportWidth <= 0.0F ||
        viewportHeight <= 0.0F || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0F ||
        !std::isfinite(input.scrollDelta) ||
        (input.pointerAvailable &&
         (!std::isfinite(input.pointerPosition.x) || !std::isfinite(input.pointerPosition.y))))
        return false;
    for (const char32_t codePoint : input.textInput)
        if (!kb::input::IsUnicodeScalar(codePoint))
            return false;
    SceneUIFrame frame;
    if (!FrameBuilder{scene_, {viewportWidth, viewportHeight}}.Build(frame)) {
        // Keep the last good frame so hit testing still answers, but carry the refusal on it
        // so the host can name what to fix. A later successful build replaces the whole frame
        // and clears this by construction.
        SceneAccess::State(scene_).uiFrame.refusal = frame.refusal;
        return false;
    }
    SceneState& state = SceneAccess::State(scene_);
    SceneUIFrame previousFrame = std::move(state.uiFrame);
    state.uiFrame = std::move(frame);
    bool presentationDirty = UpdateGroupTransitions(scene_, state, deltaSeconds);
    presentationDirty = ClampAllScrollOffsets(scene_, state, input, deltaSeconds) || presentationDirty;
    if (presentationDirty) {
        SceneUIFrame clampedFrame;
        if (!FrameBuilder{scene_, {viewportWidth, viewportHeight}}.Build(clampedFrame)) {
            state.uiFrame.refusal = clampedFrame.refusal;
            return false;
        }
        state.uiFrame = std::move(clampedFrame);
        presentationDirty = false;
    }
    if (state.uiFocused.IsValid()) {
        const SceneUIFrameElement* focused = Find(state.uiFrame, state.uiFocused);
        if (focused == nullptr || !focused->interactionEnabled)
            ClearFocus();
    }
    for (SceneState::UIPlayerFocus& player : state.uiPlayers) {
        const SceneUIFrameElement* focused = Find(state.uiFrame, player.focused);
        if (player.focused.IsValid() && (focused == nullptr || !focused->interactionEnabled)) {
            Queue(state, scene_, SceneUIEventType::Blurred, player.focused);
            player.focused = {};
        }
    }
    if (state.uiPressed.IsValid() && Find(state.uiFrame, state.uiPressed) == nullptr)
        state.uiPressed = {};
    const SceneEntity hovered = input.pointerAvailable ? state.uiFrame.HitTest(input.pointerPosition) : SceneEntity{};
    const bool tooltipShown = state.uiHoverSeconds >= kUITooltipDelaySeconds;
    if (hovered != state.uiHovered) {
        if (state.uiHovered.IsValid())
            Queue(state, scene_, SceneUIEventType::HoverExited, state.uiHovered, &input);
        state.uiHovered = hovered;
        state.uiHoverSeconds = 0.0F;
        if (hovered.IsValid())
            Queue(state, scene_, SceneUIEventType::HoverEntered, hovered, &input);
    } else if (hovered.IsValid()) {
        state.uiHoverSeconds += deltaSeconds;
    }
    const bool pressStarted = input.primaryDown && !state.previousUIInput.primaryDown;
    const bool pressReleased = !input.primaryDown && state.previousUIInput.primaryDown;
    if (pressStarted)
        state.uiHoverSeconds = 0.0F;
    if (!tooltipShown && state.uiHoverSeconds >= kUITooltipDelaySeconds)
        state.uiTooltipPointer = input.pointerPosition;
    presentationDirty = tooltipShown != (state.uiHoverSeconds >= kUITooltipDelaySeconds) || presentationDirty;
    if (pressStarted) {
        state.uiInertialScrollView = {};
        state.uiScrollVelocity = {};
        state.uiSnappingScrollView = {};
        state.uiPressed = hovered;
        if (const UISelectable* pressedSelectable = Selectable(scene_, hovered);
            pressedSelectable != nullptr && pressedSelectable->draggable) {
            state.uiDragged = hovered;
            state.uiDragOrigin = input.pointerPosition;
            state.uiDragging = false;
        }
        if (state.uiPressed.IsValid()) {
            Queue(state, scene_, SceneUIEventType::Pressed, state.uiPressed, &input);
            static_cast<void>(SetFocus(state.uiPressed));
            const UIButton* button = scene_.Components().UI().TryGet<UIButton>(state.uiPressed);
            if (button != nullptr && !button->submitOnRelease)
                Queue(state, scene_, SceneUIEventType::Clicked, state.uiPressed, &input);
        }
    }
    // A press on a draggable widget turns into a drag once the pointer travels; a finished drag reports the
    // widget it was dropped on and does not also click.
    bool dropped = false;
    if (state.uiDragged.IsValid() && input.primaryDown && input.pointerAvailable) {
        const Vec2 moved{input.pointerPosition.x - state.uiDragOrigin.x, input.pointerPosition.y - state.uiDragOrigin.y};
        if (!state.uiDragging && std::hypot(moved.x, moved.y) >= kUIDragThresholdPixels) {
            state.uiDragging = true;
            state.uiHoverSeconds = 0.0F;
            Queue(state, scene_, SceneUIEventType::DragBegan, state.uiDragged, &input, moved.x, moved.y);
        } else if (state.uiDragging && (input.pointerPosition.x != state.previousUIInput.pointerPosition.x ||
                                        input.pointerPosition.y != state.previousUIInput.pointerPosition.y)) {
            Queue(state, scene_, SceneUIEventType::Dragged, state.uiDragged, &input, moved.x, moved.y);
        }
    }
    if (state.uiDragged.IsValid() && !input.primaryDown) {
        if (state.uiDragging) {
            const SceneEntity target =
                input.pointerAvailable ? state.uiFrame.HitTestExcluding(input.pointerPosition, state.uiDragged, scene_) : SceneEntity{};
            Queue(state, scene_, SceneUIEventType::DragEnded, state.uiDragged, &input);
            state.uiEvents.back().other = target;
            if (target.IsValid()) {
                Queue(state, scene_, SceneUIEventType::Dropped, target, &input);
                state.uiEvents.back().other = state.uiDragged;
            }
            dropped = true;
        }
        state.uiDragged = {};
        state.uiDragging = false;
    }
    presentationDirty = UpdateDraggedRange(scene_, state, input) || presentationDirty;
    presentationDirty = UpdateScrollView(scene_, state, hovered, input, pressStarted, deltaSeconds) || presentationDirty;
    if (pressReleased && state.uiPressed.IsValid()) {
        const SceneEntity pressed = state.uiPressed;
        Queue(state, scene_, SceneUIEventType::Released, pressed, &input);
        const UIButton* button = scene_.Components().UI().TryGet<UIButton>(pressed);
        if (!dropped && pressed == hovered && (button == nullptr || button->submitOnRelease)) {
            presentationDirty = Activate(scene_, state, pressed, input) || presentationDirty;
            Queue(state, scene_, SceneUIEventType::Clicked, pressed, &input);
        }
        state.uiPressed = {};
    }
    presentationDirty = UpdateScrollInertia(scene_, state, input, deltaSeconds) || presentationDirty;
    presentationDirty = UpdateScrollSnap(scene_, state, input, deltaSeconds) || presentationDirty;
    const auto rising = [&](bool now, bool before) { return now && !before; };
    const bool moveLeft = rising(input.navigateLeft, state.previousUIInput.navigateLeft);
    const bool moveRight = rising(input.navigateRight, state.previousUIInput.navigateRight);
    const bool editingText = state.uiFocused.IsValid() && scene_.Components().UI().Has<UIInputField>(state.uiFocused);
    const bool textEdited =
        EditFocusedText(scene_, state, input, rising(input.backspaceDown, state.previousUIInput.backspaceDown),
                        rising(input.deleteDown, state.previousUIInput.deleteDown), editingText && moveLeft,
                        editingText && moveRight);
    presentationDirty = textEdited || presentationDirty;
    // Blink the caret of the focused field, restarting the cycle on every edit so a keystroke
    // never lands while the caret happens to be in its hidden half. Only a flip in visibility
    // rebuilds the frame - marking every frame dirty would run the whole layout at frame rate
    // for the sake of a caret that changes twice a second.
    const bool caretWasVisible = editingText && IsUITextCaretVisible(state.uiTextCaretPhase);
    if (!editingText || textEdited) {
        state.uiTextCaretPhase = 0.0F;
    } else {
        state.uiTextCaretPhase = std::fmod(state.uiTextCaretPhase + std::max(0.0F, deltaSeconds),
                                           kUITextCaretBlinkPeriodSeconds);
    }
    if (editingText && IsUITextCaretVisible(state.uiTextCaretPhase) != caretWasVisible) {
        presentationDirty = true;
    }
    // Resolve the held direction into this frame's navigation step: once on the press, then again on
    // the repeat schedule for as long as the same direction stays held. Horizontal input belongs to
    // the caret while text is being edited. Players 1-3 run the same steps on their own focus.
    const auto navigate = [&](const SceneUIPlayerNavigation& now, const SceneUIPlayerNavigation& before, std::int32_t player,
                              bool editing) {
        Vec2 held{};
        if (now.navigateUp)
            held = {0.0F, -1.0F};
        else if (now.navigateDown)
            held = {0.0F, 1.0F};
        else if (now.navigateLeft && !editing)
            held = {-1.0F, 0.0F};
        else if (now.navigateRight && !editing)
            held = {1.0F, 0.0F};
        Vec2 direction{};
        if (held.x != state.uiNavigationHeldDirection.x || held.y != state.uiNavigationHeldDirection.y) {
            state.uiNavigationHeldDirection = held;
            state.uiNavigationRepeatSeconds = kUINavigationRepeatDelaySeconds;
            direction = held;
        } else if (held.x != 0.0F || held.y != 0.0F) {
            state.uiNavigationRepeatSeconds -= deltaSeconds;
            if (state.uiNavigationRepeatSeconds <= 0.0F) {
                state.uiNavigationRepeatSeconds += kUINavigationRepeatIntervalSeconds;
                direction = held;
            }
        }
        if (direction.x != 0.0F || direction.y != 0.0F) {
            bool rangeChanged = false;
            if (StepFocusedRange(scene_, state, direction, rangeChanged)) {
                presentationDirty = rangeChanged || presentationDirty;
                direction = {};
            } else if (StepFocusedScrollView(scene_, state, direction, input)) {
                presentationDirty = true;
                direction = {};
            }
        }
        if (direction.x != 0.0F || direction.y != 0.0F) {
            SceneEntity target{};
            const UISelectable* current = Selectable(scene_, state.uiFocused);
            if (current != nullptr && current->navigationMode == UINavigationMode::Explicit) {
                const std::uint64_t id = direction.y < 0   ? current->navigationUp
                                         : direction.y > 0 ? current->navigationDown
                                         : direction.x < 0 ? current->navigationLeft
                                                           : current->navigationRight;
                target = SceneEntity{id};
            } else if (current == nullptr || current->navigationMode == UINavigationMode::Automatic)
                target = AutomaticNeighbor(state.uiFrame, state.uiFocused, direction, player);
            if (target.IsValid() && SetFocus(target))
                presentationDirty = ScrollFocusedIntoView(scene_, state, input) || presentationDirty;
        }
        if (rising(now.submitDown, before.submitDown) && state.uiFocused.IsValid()) {
            if (!scene_.Components().UI().Has<UIInputField>(state.uiFocused)) {
                presentationDirty = Activate(scene_, state, state.uiFocused, input) || presentationDirty;
                Queue(state, scene_, SceneUIEventType::Clicked, state.uiFocused);
            }
            Queue(state, scene_, SceneUIEventType::Submitted, state.uiFocused);
        }
        if (rising(now.cancelDown, before.cancelDown)) {
            // Cancel closes an open dropdown list, from the list or from the dropdown.
            if (state.uiDropdownList.has_value())
                presentationDirty = HideDropdownList(scene_, state) || presentationDirty;
            if (state.uiFocused.IsValid())
                Queue(state, scene_, SceneUIEventType::Canceled, state.uiFocused);
        }
    };
    const auto shared = [](const SceneUIInput& value) {
        return SceneUIPlayerNavigation{value.navigateUp, value.navigateDown, value.navigateLeft, value.navigateRight,
                                       value.submitDown, value.cancelDown};
    };
    navigate(shared(input), shared(state.previousUIInput), 0, editingText);
    for (std::size_t index = 0U; index < state.uiPlayers.size(); ++index) {
        SceneState::UIPlayerFocus& player = state.uiPlayers[index];
        const SceneUIPlayerNavigation& now = input.otherPlayers[index];
        if (!player.focused.IsValid() && now == SceneUIPlayerNavigation{} && player.previous == SceneUIPlayerNavigation{})
            continue;
        // The player's focus stands in for the shared one while their steps run.
        std::swap(state.uiFocused, player.focused);
        std::swap(state.uiNavigationHeldDirection, player.heldDirection);
        std::swap(state.uiNavigationRepeatSeconds, player.repeatSeconds);
        navigate(now, player.previous, static_cast<std::int32_t>(index + 1U), false);
        std::swap(state.uiFocused, player.focused);
        std::swap(state.uiNavigationHeldDirection, player.heldDirection);
        std::swap(state.uiNavigationRepeatSeconds, player.repeatSeconds);
        player.previous = now;
    }
    state.previousUIInput = input;
    state.previousUIInput.textInput = {};
    state.previousUIInput.scrollDelta = 0.0F;
    presentationDirty = UpdateDropdownList(scene_, state, deltaSeconds) || presentationDirty;
    presentationDirty = SyncScrollbars(scene_, state) || presentationDirty;
    if (presentationDirty || state.uiPendingFocus.IsValid()) {
        SceneUIFrame refreshed;
        if (!FrameBuilder{scene_, {viewportWidth, viewportHeight}}.Build(refreshed))
            return false;
        state.uiFrame = std::move(refreshed);
    }
    // Focus handed to a widget created this frame - an opened list's item - lands once it has been laid out.
    if (state.uiPendingFocus.IsValid()) {
        const SceneEntity pending = state.uiPendingFocus;
        state.uiPendingFocus = {};
        if (SetFocus(pending) && ScrollFocusedIntoView(scene_, state, input)) {
            SceneUIFrame scrolled;
            if (!FrameBuilder{scene_, {viewportWidth, viewportHeight}}.Build(scrolled))
                return false;
            state.uiFrame = std::move(scrolled);
        }
    }
    ResolveInteractionPresentation(scene_, state, input.primaryDown, deltaSeconds, previousFrame, state.uiFrame, true);
    return true;
}

SceneUIAccess Scene::UI() noexcept {
    return SceneUIAccess{*this};
}
SceneUIQueries Scene::UI() const noexcept {
    return SceneUIQueries{*this};
}

} // namespace kb::scene
