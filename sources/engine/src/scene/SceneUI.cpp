#include "engine/scene/SceneUI.hpp"

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputText.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneVisibilityResolution.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/ui/UIComponentValidation.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <string>

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
[[nodiscard]] SceneUIEvent MakeEvent(SceneUIEventType type, SceneEntity entity, const UISelectable* selectable) {
    SceneUIEvent event{.type = type, .entity = entity};
    if (selectable != nullptr) {
        const std::string_view name = UIEventName(*selectable);
        std::copy(name.begin(), name.end(), event.name.begin());
    }
    return event;
}

class FrameBuilder {
  public:
    FrameBuilder(const Scene& scene, Vec2 viewport) : scene_(scene), ui_(scene.Components().UI()), viewport_(viewport) {
        frame_.viewportSize = viewport;
    }

    [[nodiscard]] bool Build(SceneUIFrame& output) {
        for (const SceneEntity root : scene_.Hierarchy().RootEntities())
            SearchForCanvas(root);
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
        const UIDropdown* dropdown = ui_.TryGet<UIDropdown>(entity);
        if (dropdown != nullptr && directChildCount != 0U && dropdown->selectedIndex >= directChildCount)
            return "Dropdown selectedIndex is past its last child";
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
            const float scale = CanvasScale(viewport_, scaler);
            if (!std::isfinite(scale) || scale <= 0.0F) {
                Refuse(entity, "Canvas Scaler resolved to a non-positive scale");
                return;
            }
            const Rect logicalViewport{0.0F, 0.0F, viewport_.x / scale, viewport_.y / scale};
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
            }
        }
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
        if (const UICanvasGroup* authored = ui_.TryGet<UICanvasGroup>(entity)) {
            if (!IsUIComponentValid(*authored)) {
                Refuse(entity, "Canvas Group holds an invalid value");
                return;
            }
            if (authored->ignoreParentGroups)
                group = {};
            group.opacity *= authored->opacity;
            group.interactable = group.interactable && authored->interactable;
            group.blocksRaycasts = group.blocksRaycasts && authored->blocksRaycasts;
        }

        SceneUIFrameElement element;
        element.entity = entity;
        element.canvas = canvas;
        element.canvasScale = scale;
        element.canvasSortingOrder = sortingOrder;
        element.zOrder = transform->zOrder;
        element.traversalOrder = traversal_++;
        element.effectiveOpacity = ResolveVisibility(scene_, entity).visible ? group.opacity : 0.0F;
        element.rect = {rect.x * scale, rect.y * scale, rect.width * scale, rect.height * scale};
        element.clipRect = inheritedClip;
        element.clipQuads = inheritedClipQuads;
        const Affine2D resolvedTransform = Compose(inheritedTransform, AuthoredTransform(rect, *transform));
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
        if (element.inputField.has_value()) {
            const SceneState& sceneState = SceneAccess::State(scene_);
            if (sceneState.uiFocused == entity) {
                element.textCaretByteOffset = static_cast<std::uint32_t>(sceneState.uiTextCursorByteOffset);
                element.textCaretVisible = IsUITextCaretVisible(sceneState.uiTextCaretPhase);
            }
        }
        const UISelectable* selectable = ui_.TryGet<UISelectable>(entity);
        if (selectable != nullptr && !IsUIComponentValid(*selectable)) {
            Refuse(entity, "Selectable holds an invalid value");
            return;
        }
        element.interactionEnabled = selectable != nullptr && selectable->interactable && group.interactable;
        element.hitTestable =
            element.interactionEnabled && selectable->raycastTarget && group.blocksRaycasts && element.effectiveOpacity > 0.0F;
        if (selectable != nullptr)
            element.interactionTint = selectable->normalColor;
        frame_.elements.push_back(std::move(element));

        Rect childClip = inheritedClip;
        std::vector<std::array<Vec2, 4U>> childClipQuads = inheritedClipQuads;
        if (ui_.Has<UIMask>(entity)) {
            childClip = Intersect(childClip, Bounds(frame_.elements.back().corners));
            childClipQuads.push_back(frame_.elements.back().corners);
        }
        ArrangeChildren(entity, rect, childClip, canvas, scale, sortingOrder, pixelPerfect, group, resolvedTransform,
                        childClipQuads);
    }

    void ArrangeChildren(SceneEntity parent, Rect rect, Rect clip, SceneEntity canvas, float scale,
                         std::int32_t sortingOrder, bool pixelPerfect, GroupState group,
                         const Affine2D& inheritedTransform,
                         const std::vector<std::array<Vec2, 4U>>& inheritedClipQuads) {
        const std::size_t count = scene_.Hierarchy().ChildCount(parent);
        std::vector<SceneEntity> managed;
        managed.reserve(count);
        const UIWidgetSwitcher* switcher = ui_.TryGet<UIWidgetSwitcher>(parent);
        const UIDropdown* dropdown = ui_.TryGet<UIDropdown>(parent);
        for (std::size_t i = 0U; i < count; ++i) {
            if (switcher != nullptr && i != switcher->visibleChildIndex)
                continue;
            if (dropdown != nullptr && i != dropdown->selectedIndex)
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
                    Arrange(child, AnchoredRect(*ui_.TryGet<UIRectTransform>(child), contentRect), clip, canvas, scale,
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
            Rect childRect = AnchoredRect(*ui_.TryGet<UIRectTransform>(managed[index]), contentRect);
            if (progress != nullptr && index == 0U) {
                const float fraction = NormalizedRange(progress->minimum, progress->maximum, progress->value);
                childRect.width *= fraction;
            }
            Arrange(managed[index], childRect, clip, canvas, scale, sortingOrder, pixelPerfect, group,
                    inheritedTransform, inheritedClipQuads);
        }
        arrangeIgnored();
    }

    // Records the first refusal and stops the build. Only the first is kept: later entities
    // are unreachable consequences of this one, so naming them would bury the cause.
    void Refuse(SceneEntity entity, const char* reason) noexcept {
        if (valid_) {
            refusal_ = SceneUIFrameRefusal{.entity = entity, .reason = reason};
            valid_ = false;
        }
    }

    const Scene& scene_;
    SceneUIComponentQueries ui_;
    Vec2 viewport_{};
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

[[nodiscard]] bool ClampAllScrollOffsets(Scene& scene, SceneState& state, const SceneUIInput& input) {
    bool changed = false;
    SceneUIComponents ui = scene.Components().UI();
    for (const SceneUIFrameElement& element : state.uiFrame.elements) {
        UIScrollView* scrollView = ui.TryGet<UIScrollView>(element.entity);
        if (scrollView == nullptr || !ClampScrollOffsets(scene, state.uiFrame, element.entity, *scrollView))
            continue;
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
        else if (element.entity == state.uiFocused)
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
[[nodiscard]] SceneEntity AutomaticNeighbor(const SceneUIFrame& frame, SceneEntity current, Vec2 direction) noexcept {
    const SceneUIFrameElement* source = Find(frame, current);
    if (source == nullptr) {
        for (const auto& e : frame.elements)
            if (e.hitTestable)
                return e.entity;
        return {};
    }
    const Vec2 center{source->rect.x + source->rect.width * 0.5F, source->rect.y + source->rect.height * 0.5F};
    float best = std::numeric_limits<float>::max();
    SceneEntity result{};
    for (const auto& e : frame.elements) {
        if (!e.hitTestable || e.entity == current)
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
        if (codePoint == U'\n' && field->multiline) {
            // A line feed is the sole accepted control character for multiline fields.
        } else if (codePoint < U' ' || codePoint == 0x7FU) {
            continue;
        }
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
    if (input.primaryDown && !pressStarted && input.pointerAvailable && state.previousUIInput.pointerAvailable) {
        const float scale = std::max(element->canvasScale, 0.0001F);
        if (scrollView->horizontal)
            scrollView->scrollX =
                std::max(0.0F, scrollView->scrollX +
                                   (state.previousUIInput.pointerPosition.x - input.pointerPosition.x) / scale);
        if (scrollView->vertical)
            scrollView->scrollY =
                std::max(0.0F, scrollView->scrollY +
                                   (state.previousUIInput.pointerPosition.y - input.pointerPosition.y) / scale);
        if (scrollView->inertia && deltaSeconds > 0.0F) {
            state.uiInertialScrollView = entity;
            state.uiScrollVelocity = {(scrollView->scrollX - beforeX) / deltaSeconds,
                                      (scrollView->scrollY - beforeY) / deltaSeconds};
        } else {
            state.uiInertialScrollView = {};
            state.uiScrollVelocity = {};
        }
    }
    static_cast<void>(ClampScrollOffsets(scene, state.uiFrame, entity, *scrollView));
    if (beforeX == scrollView->scrollX && beforeY == scrollView->scrollY)
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
        scrollView->scrollX = std::max(0.0F, scrollView->scrollX + state.uiScrollVelocity.x * deltaSeconds);
    if (scrollView->vertical)
        scrollView->scrollY = std::max(0.0F, scrollView->scrollY + state.uiScrollVelocity.y * deltaSeconds);
    const Vec2 limits = ScrollLimits(scene, state.uiFrame, entity, *scrollView);
    static_cast<void>(ClampScrollOffsets(scene, state.uiFrame, entity, *scrollView));
    if ((scrollView->scrollX == 0.0F && state.uiScrollVelocity.x < 0.0F) ||
        (scrollView->scrollX == limits.x && state.uiScrollVelocity.x > 0.0F))
        state.uiScrollVelocity.x = 0.0F;
    if ((scrollView->scrollY == 0.0F && state.uiScrollVelocity.y < 0.0F) ||
        (scrollView->scrollY == limits.y && state.uiScrollVelocity.y > 0.0F))
        state.uiScrollVelocity.y = 0.0F;
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

[[nodiscard]] bool Activate(Scene& scene, SceneState& state, SceneEntity entity, const SceneUIInput& input) {
    SceneUIComponents ui = scene.Components().UI();
    if (UIToggle* toggle = ui.TryGet<UIToggle>(entity)) {
        toggle->toggled = !toggle->toggled;
        ui.MarkModified<UIToggle>(entity);
        Queue(state, scene, SceneUIEventType::Changed, entity, &input, toggle->toggled ? 1.0F : 0.0F);
        return true;
    }
    if (UIDropdown* dropdown = ui.TryGet<UIDropdown>(entity)) {
        const std::size_t count = scene.Hierarchy().ChildCount(entity);
        if (count == 0U)
            return false;
        dropdown->selectedIndex = static_cast<std::uint32_t>((dropdown->selectedIndex + 1U) % count);
        ui.MarkModified<UIDropdown>(entity);
        Queue(state, scene, SceneUIEventType::Changed, entity, &input, static_cast<float>(dropdown->selectedIndex));
        return true;
    }
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
SceneEntity SceneUIFrame::HitTest(Vec2 point) const noexcept {
    for (auto iterator = elements.rbegin(); iterator != elements.rend(); ++iterator) {
        const bool insideEveryMask = std::ranges::all_of(iterator->clipQuads, [point](const auto& quad) {
            return InQuad(quad, point);
        });
        if (iterator->hitTestable && ContainsRect(iterator->clipRect, point) && insideEveryMask &&
            InQuad(iterator->corners, point))
            return iterator->entity;
    }
    return {};
}

SceneUIQueries::SceneUIQueries(const Scene& scene) noexcept : scene_(scene) {}
const SceneUIFrame& SceneUIQueries::Frame() const noexcept {
    return SceneAccess::State(scene_).uiFrame;
}
bool SceneUIQueries::BuildFrame(float viewportWidth, float viewportHeight, SceneUIFrame& output) const {
    if (!std::isfinite(viewportWidth) || !std::isfinite(viewportHeight) || viewportWidth <= 0.0F ||
        viewportHeight <= 0.0F)
        return false;
    SceneUIFrame frame;
    if (!FrameBuilder{scene_, {viewportWidth, viewportHeight}}.Build(frame)) {
        // The refusal is the only thing the caller can act on, so it has to survive the
        // discarded frame - the renderer reports it instead of dropping the whole submit.
        output.elements.clear();
        output.refusal = frame.refusal;
        return false;
    }
    const SceneState& state = SceneAccess::State(scene_);
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
    input.navigateUp = device.IsKeyDown(kb::input::InputKey::ArrowUp) ||
                       device.IsKeyDown(kb::input::InputKey::GamepadDPadUp) || reverseTab;
    input.navigateDown = device.IsKeyDown(kb::input::InputKey::ArrowDown) ||
                         device.IsKeyDown(kb::input::InputKey::GamepadDPadDown) ||
                         (device.IsKeyDown(kb::input::InputKey::Tab) && !reverseTab);
    input.navigateLeft =
        device.IsKeyDown(kb::input::InputKey::ArrowLeft) || device.IsKeyDown(kb::input::InputKey::GamepadDPadLeft);
    input.navigateRight =
        device.IsKeyDown(kb::input::InputKey::ArrowRight) || device.IsKeyDown(kb::input::InputKey::GamepadDPadRight);
    input.submitDown =
        device.IsKeyDown(kb::input::InputKey::Enter) || device.IsKeyDown(kb::input::InputKey::GamepadFaceBottom);
    input.cancelDown =
        device.IsKeyDown(kb::input::InputKey::Escape) || device.IsKeyDown(kb::input::InputKey::GamepadFaceRight);
    input.backspaceDown = device.IsKeyDown(kb::input::InputKey::Backspace);
    input.deleteDown = device.IsKeyDown(kb::input::InputKey::Delete);
    input.scrollDelta = device.GetValue(kb::input::InputKey::MouseWheel);
    input.textInput = device.TextInput();
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
    SceneState& state = SceneAccess::State(scene_);
    if (entity == state.uiFocused)
        return true;
    const SceneUIFrameElement* target = Find(state.uiFrame, entity);
    if (target == nullptr || !target->interactionEnabled)
        return false;
    if (state.uiFocused.IsValid())
        Queue(state, scene_, SceneUIEventType::Blurred, state.uiFocused);
    state.uiFocused = entity;
    if (const UIText* text = scene_.Components().UI().TryGet<UIText>(entity);
        text != nullptr && scene_.Components().UI().Has<UIInputField>(entity)) {
        state.uiTextCursorByteOffset = UITextContent(*text).size();
    } else {
        state.uiTextCursorByteOffset = 0U;
    }
    Queue(state, scene_, SceneUIEventType::Focused, entity);
    return true;
}
void SceneUIAccess::ClearFocus() noexcept {
    SceneState& state = SceneAccess::State(scene_);
    if (state.uiFocused.IsValid())
        Queue(state, scene_, SceneUIEventType::Blurred, state.uiFocused);
    state.uiFocused = {};
    state.uiTextCursorByteOffset = 0U;
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
    bool presentationDirty = ClampAllScrollOffsets(scene_, state, input);
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
    if (state.uiPressed.IsValid() && Find(state.uiFrame, state.uiPressed) == nullptr)
        state.uiPressed = {};
    const SceneEntity hovered = input.pointerAvailable ? state.uiFrame.HitTest(input.pointerPosition) : SceneEntity{};
    if (hovered != state.uiHovered) {
        if (state.uiHovered.IsValid())
            Queue(state, scene_, SceneUIEventType::HoverExited, state.uiHovered, &input);
        state.uiHovered = hovered;
        if (hovered.IsValid())
            Queue(state, scene_, SceneUIEventType::HoverEntered, hovered, &input);
    }
    const bool pressStarted = input.primaryDown && !state.previousUIInput.primaryDown;
    const bool pressReleased = !input.primaryDown && state.previousUIInput.primaryDown;
    if (pressStarted) {
        state.uiInertialScrollView = {};
        state.uiScrollVelocity = {};
        state.uiPressed = hovered;
        if (state.uiPressed.IsValid()) {
            Queue(state, scene_, SceneUIEventType::Pressed, state.uiPressed, &input);
            static_cast<void>(SetFocus(state.uiPressed));
            const UIButton* button = scene_.Components().UI().TryGet<UIButton>(state.uiPressed);
            if (button != nullptr && !button->submitOnRelease)
                Queue(state, scene_, SceneUIEventType::Clicked, state.uiPressed, &input);
        }
    }
    presentationDirty = UpdateDraggedRange(scene_, state, input) || presentationDirty;
    presentationDirty = UpdateScrollView(scene_, state, hovered, input, pressStarted, deltaSeconds) || presentationDirty;
    if (pressReleased && state.uiPressed.IsValid()) {
        const SceneEntity pressed = state.uiPressed;
        Queue(state, scene_, SceneUIEventType::Released, pressed, &input);
        const UIButton* button = scene_.Components().UI().TryGet<UIButton>(pressed);
        if (pressed == hovered && (button == nullptr || button->submitOnRelease)) {
            presentationDirty = Activate(scene_, state, pressed, input) || presentationDirty;
            Queue(state, scene_, SceneUIEventType::Clicked, pressed, &input);
        }
        state.uiPressed = {};
    }
    presentationDirty = UpdateScrollInertia(scene_, state, input, deltaSeconds) || presentationDirty;
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
    Vec2 direction{};
    if (rising(input.navigateUp, state.previousUIInput.navigateUp))
        direction = {0.0F, -1.0F};
    else if (rising(input.navigateDown, state.previousUIInput.navigateDown))
        direction = {0.0F, 1.0F};
    else if (moveLeft && !editingText)
        direction = {-1.0F, 0.0F};
    else if (moveRight && !editingText)
        direction = {1.0F, 0.0F};
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
            target = AutomaticNeighbor(state.uiFrame, state.uiFocused, direction);
        if (target.IsValid())
            static_cast<void>(SetFocus(target));
    }
    if (rising(input.submitDown, state.previousUIInput.submitDown) && state.uiFocused.IsValid()) {
        if (!scene_.Components().UI().Has<UIInputField>(state.uiFocused)) {
            presentationDirty = Activate(scene_, state, state.uiFocused, input) || presentationDirty;
            Queue(state, scene_, SceneUIEventType::Clicked, state.uiFocused);
        }
        Queue(state, scene_, SceneUIEventType::Submitted, state.uiFocused);
    }
    if (rising(input.cancelDown, state.previousUIInput.cancelDown) && state.uiFocused.IsValid())
        Queue(state, scene_, SceneUIEventType::Canceled, state.uiFocused);
    state.previousUIInput = input;
    state.previousUIInput.textInput = {};
    state.previousUIInput.scrollDelta = 0.0F;
    if (presentationDirty) {
        SceneUIFrame refreshed;
        if (!FrameBuilder{scene_, {viewportWidth, viewportHeight}}.Build(refreshed))
            return false;
        state.uiFrame = std::move(refreshed);
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
