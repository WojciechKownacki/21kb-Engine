#include "scene/ui/UIPresentationBuilder.hpp"

#include "engine/ui/layout/UIPresentationLayout.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace kb::scene {
namespace {

struct LayoutEntry {
    const UIDocumentElement* element = nullptr;
    UIPresentationRect rect{};
};

[[nodiscard]] float FiniteOr(float value, float fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] float NonNegative(float value) noexcept {
    return std::max(0.0F, FiniteOr(value, 0.0F));
}

[[nodiscard]] UIPresentationRect Intersect(const UIPresentationRect& lhs, const UIPresentationRect& rhs) noexcept {
    return UIPresentationRect{
        .left = std::max(lhs.left, rhs.left),
        .top = std::max(lhs.top, rhs.top),
        .right = std::min(lhs.right, rhs.right),
        .bottom = std::min(lhs.bottom, rhs.bottom),
    };
}

[[nodiscard]] float ResolveCanvasScale(const UIDocumentElement& root, std::uint32_t viewportWidth,
                                       std::uint32_t viewportHeight) noexcept {
    if (!root.canvas.has_value())
        return 1.0F;
    const UICanvas& canvas = *root.canvas;
    const float explicitScale = std::max(0.0001F, FiniteOr(canvas.scaleFactor, 1.0F));
    if (canvas.scaleMode == UICanvasScaleMode::ConstantPixelSize)
        return explicitScale;

    const float referenceWidth = std::max(1.0F, FiniteOr(canvas.referenceResolution.x, 1920.0F));
    const float referenceHeight = std::max(1.0F, FiniteOr(canvas.referenceResolution.y, 1080.0F));
    const float widthRatio = std::max(0.0001F, static_cast<float>(viewportWidth) / referenceWidth);
    const float heightRatio = std::max(0.0001F, static_cast<float>(viewportHeight) / referenceHeight);
    const float match = std::clamp(FiniteOr(canvas.matchWidthOrHeight, 0.5F), 0.0F, 1.0F);
    return std::exp2(std::log2(widthRatio) * (1.0F - match) + std::log2(heightRatio) * match) * explicitScale;
}

[[nodiscard]] UIPresentationRect ResolveAnchoredRect(const UIDocumentElement& element, const UIPresentationRect& parent,
                                                     float canvasScale) noexcept {
    const UIRectTransform& transform = element.rect;
    const float parentWidth = parent.Width();
    const float parentHeight = parent.Height();
    return UIPresentationRect{
        .left = parent.left + parentWidth * transform.anchorMin.x + transform.offsetMin.x * canvasScale,
        .top = parent.top + parentHeight * transform.anchorMin.y + transform.offsetMin.y * canvasScale,
        .right = parent.left + parentWidth * transform.anchorMax.x + transform.offsetMax.x * canvasScale,
        .bottom = parent.top + parentHeight * transform.anchorMax.y + transform.offsetMax.y * canvasScale,
    };
}

[[nodiscard]] float NaturalWidth(const UIDocumentElement& element, float canvasScale) noexcept {
    return NonNegative(element.rect.offsetMax.x - element.rect.offsetMin.x) * canvasScale;
}

[[nodiscard]] float NaturalHeight(const UIDocumentElement& element, float canvasScale) noexcept {
    return NonNegative(element.rect.offsetMax.y - element.rect.offsetMin.y) * canvasScale;
}

[[nodiscard]] std::vector<const UIDocumentElement*> SortedChildren(std::span<const UIDocumentElement* const> elements,
                                                                   UIElementId parentId) {
    std::vector<const UIDocumentElement*> children;
    for (const UIDocumentElement* candidate : elements) {
        if (candidate != nullptr && candidate->parentId == parentId) {
            children.push_back(candidate);
        }
    }
    std::ranges::sort(children, [](const UIDocumentElement* lhs, const UIDocumentElement* rhs) {
        if (lhs->rect.zOrder != rhs->rect.zOrder)
            return lhs->rect.zOrder < rhs->rect.zOrder;
        if (lhs->siblingOrder != rhs->siblingOrder)
            return lhs->siblingOrder < rhs->siblingOrder;
        return lhs->id < rhs->id;
    });
    return children;
}

[[nodiscard]] UIPresentationRect CrossAlignedVertical(float top, float bottom, float naturalHeight,
                                                      UIAlignment alignment) noexcept {
    const float available = NonNegative(bottom - top);
    const float height = std::min(available, naturalHeight);
    switch (alignment) {
    case UIAlignment::Center: {
        const float offset = (available - height) * 0.5F;
        return {0.0F, top + offset, 0.0F, top + offset + height};
    }
    case UIAlignment::End:
        return {0.0F, bottom - height, 0.0F, bottom};
    case UIAlignment::Stretch:
        return {0.0F, top, 0.0F, bottom};
    case UIAlignment::Start:
    default:
        return {0.0F, top, 0.0F, top + height};
    }
}

[[nodiscard]] UIPresentationRect CrossAlignedHorizontal(float left, float right, float naturalWidth,
                                                        UIAlignment alignment) noexcept {
    const float available = NonNegative(right - left);
    const float width = std::min(available, naturalWidth);
    switch (alignment) {
    case UIAlignment::Center: {
        const float offset = (available - width) * 0.5F;
        return {left + offset, 0.0F, left + offset + width, 0.0F};
    }
    case UIAlignment::End:
        return {right - width, 0.0F, right, 0.0F};
    case UIAlignment::Stretch:
        return {left, 0.0F, right, 0.0F};
    case UIAlignment::Start:
    default:
        return {left, 0.0F, left + width, 0.0F};
    }
}

[[nodiscard]] std::vector<LayoutEntry> ResolveChildren(std::span<const UIDocumentElement* const> elements,
                                                       const UIDocumentElement& parentElement,
                                                       const UIPresentationRect& parentRect, float canvasScale) {
    std::vector<const UIDocumentElement*> children = SortedChildren(elements, parentElement.id);
    if (parentElement.control.kind == UIControlKind::WidgetSwitcher && !children.empty()) {
        const std::size_t selected = std::min<std::size_t>(parentElement.control.selectedIndex, children.size() - 1U);
        const UIDocumentElement* visibleChild = children[selected];
        children.assign(1U, visibleChild);
    }
    std::vector<LayoutEntry> result;
    result.reserve(children.size());
    if (children.empty())
        return result;

    const UIContainerLayout layout = parentElement.layout.value_or(UIContainerLayout{});
    const float left = parentRect.left + NonNegative(layout.padding.left) * canvasScale;
    const float top = parentRect.top + NonNegative(layout.padding.top) * canvasScale;
    const float right = parentRect.right - NonNegative(layout.padding.right) * canvasScale;
    const float bottom = parentRect.bottom - NonNegative(layout.padding.bottom) * canvasScale;
    const float spacingX = NonNegative(layout.spacing.x) * canvasScale;
    const float spacingY = NonNegative(layout.spacing.y) * canvasScale;
    const float scrollOffset = parentElement.control.kind == UIControlKind::ScrollView
                                   ? NonNegative(parentElement.control.scrollOffset) * canvasScale
                                   : 0.0F;

    if (layout.mode == UIContainerLayoutMode::None || layout.mode == UIContainerLayoutMode::Overlay) {
        for (const UIDocumentElement* child : children) {
            UIPresentationRect rect = ResolveAnchoredRect(*child, parentRect, canvasScale);
            rect.top -= scrollOffset;
            rect.bottom -= scrollOffset;
            result.push_back({child, rect});
        }
        return result;
    }

    if (layout.mode == UIContainerLayoutMode::Horizontal) {
        float totalWidth = spacingX * static_cast<float>(children.size() - 1U);
        for (const UIDocumentElement* child : children)
            totalWidth += NaturalWidth(*child, canvasScale);
        float cursor = left;
        const float remaining = NonNegative(right - left - totalWidth);
        if (layout.horizontalAlignment == UIAlignment::Center)
            cursor += remaining * 0.5F;
        else if (layout.horizontalAlignment == UIAlignment::End)
            cursor += remaining;
        const float stretchWidth =
            layout.horizontalAlignment == UIAlignment::Stretch
                ? NonNegative(right - left - spacingX * static_cast<float>(children.size() - 1U)) /
                      static_cast<float>(children.size())
                : 0.0F;
        for (const UIDocumentElement* child : children) {
            const float width =
                layout.horizontalAlignment == UIAlignment::Stretch ? stretchWidth : NaturalWidth(*child, canvasScale);
            const UIPresentationRect cross =
                CrossAlignedVertical(top - scrollOffset, bottom - scrollOffset, NaturalHeight(*child, canvasScale),
                                     layout.verticalAlignment);
            result.push_back({child, {cursor, cross.top, cursor + width, cross.bottom}});
            cursor += width + spacingX;
        }
        return result;
    }

    if (layout.mode == UIContainerLayoutMode::Vertical) {
        float totalHeight = spacingY * static_cast<float>(children.size() - 1U);
        for (const UIDocumentElement* child : children)
            totalHeight += NaturalHeight(*child, canvasScale);
        float cursor = top - scrollOffset;
        const float remaining = NonNegative(bottom - top - totalHeight);
        if (layout.verticalAlignment == UIAlignment::Center)
            cursor += remaining * 0.5F;
        else if (layout.verticalAlignment == UIAlignment::End)
            cursor += remaining;
        const float stretchHeight =
            layout.verticalAlignment == UIAlignment::Stretch
                ? NonNegative(bottom - top - spacingY * static_cast<float>(children.size() - 1U)) /
                      static_cast<float>(children.size())
                : 0.0F;
        for (const UIDocumentElement* child : children) {
            const float height =
                layout.verticalAlignment == UIAlignment::Stretch ? stretchHeight : NaturalHeight(*child, canvasScale);
            const UIPresentationRect cross =
                CrossAlignedHorizontal(left, right, NaturalWidth(*child, canvasScale), layout.horizontalAlignment);
            result.push_back({child, {cross.left, cursor, cross.right, cursor + height}});
            cursor += height + spacingY;
        }
        return result;
    }

    if (layout.mode == UIContainerLayoutMode::Grid) {
        const std::uint32_t columns = std::max(1U, layout.columns);
        const float cellWidth = NonNegative(layout.cellSize.x) * canvasScale;
        const float cellHeight = NonNegative(layout.cellSize.y) * canvasScale;
        for (std::size_t index = 0U; index < children.size(); ++index) {
            const std::uint32_t column = static_cast<std::uint32_t>(index) % columns;
            const std::uint32_t row = static_cast<std::uint32_t>(index) / columns;
            const float x = left + static_cast<float>(column) * (cellWidth + spacingX);
            const float y = top - scrollOffset + static_cast<float>(row) * (cellHeight + spacingY);
            result.push_back({children[index], {x, y, x + cellWidth, y + cellHeight}});
        }
        return result;
    }

    float cursorX = left;
    float cursorY = top - scrollOffset;
    float rowHeight = 0.0F;
    for (const UIDocumentElement* child : children) {
        const float width = NaturalWidth(*child, canvasScale);
        const float height = NaturalHeight(*child, canvasScale);
        if (cursorX > left && cursorX + width > right) {
            cursorX = left;
            cursorY += rowHeight + spacingY;
            rowHeight = 0.0F;
        }
        result.push_back({child, {cursorX, cursorY, cursorX + width, cursorY + height}});
        cursorX += width + spacingX;
        rowHeight = std::max(rowHeight, height);
    }
    return result;
}

[[nodiscard]] bool IsVisibleControl(UIControlKind kind) noexcept {
    switch (kind) {
    case UIControlKind::Container:
    case UIControlKind::Canvas:
    case UIControlKind::Overlay:
    case UIControlKind::HorizontalBox:
    case UIControlKind::VerticalBox:
    case UIControlKind::Grid:
    case UIControlKind::Wrap:
    case UIControlKind::Spacer:
    case UIControlKind::SizeBox:
    case UIControlKind::ScaleBox:
    case UIControlKind::WidgetSwitcher:
        return false;
    default:
        return true;
    }
}

[[nodiscard]] std::string_view PresentedText(const UIControlState& control) noexcept {
    if (control.kind == UIControlKind::Dropdown && !control.listItems.empty() &&
        control.selectedIndex < control.listItems.size()) {
        return control.listItems[control.selectedIndex];
    }
    return control.text;
}

void AppendElement(std::span<const UIDocumentElement* const> elements, SceneEntity owner,
                   const UIDocumentElement& element, const UIPresentationRect& rect,
                   const UIPresentationRect& incomingClip, float canvasScale, UIPresentationSnapshot& snapshot,
                   std::uint64_t& painterOrder) {
    if (!element.visible || (element.control.kind == UIControlKind::ModalDialog && !element.control.modalOpen))
        return;

    const std::uint64_t order = painterOrder++;
    const bool hasPresentation = element.paint.has_value() || element.image.has_value() ||
                                 element.textStyle.has_value() || element.effects.has_value() ||
                                 IsVisibleControl(element.control.kind);
    if (hasPresentation && rect.IsValid() && incomingClip.IsValid()) {
        snapshot.items.push_back(UIPresentationItem{
            .owner = owner,
            .elementId = element.id,
            .rect = rect,
            .clipRect = incomingClip,
            .pivot = element.rect.pivot,
            .scale = element.rect.scale,
            .rotationDegrees = element.rect.rotationDegrees,
            .canvasScale = canvasScale,
            .painterOrder = order,
            .controlKind = element.control.kind,
            .text = PresentedText(element.control),
            .toggleValue = element.control.toggleValue,
            .value = element.control.sliderValue,
            .minimum = element.control.sliderMinimum,
            .maximum = element.control.sliderMaximum,
            .selectedIndex = element.control.selectedIndex,
            .scrollOffset = element.control.scrollOffset,
            .paint = element.paint,
            .image = element.image,
            .textStyle = element.textStyle,
            .effects = element.effects,
        });
    }
    if (element.interaction.has_value() && element.interaction->raycastTarget && element.interaction->interactable &&
        rect.IsValid() && incomingClip.IsValid()) {
        snapshot.hitTargets.push_back(UIPresentationHitTarget{
            .owner = owner,
            .elementId = element.id,
            .rect = rect,
            .clipRect = incomingClip,
            .pivot = element.rect.pivot,
            .scale = element.rect.scale,
            .rotationDegrees = element.rect.rotationDegrees,
            .painterOrder = order,
            .controlKind = element.control.kind,
            .eventName = element.interaction->eventName,
        });
    }

    UIPresentationRect childClip = incomingClip;
    if ((element.effects.has_value() && (element.effects->clipChildren || element.effects->mask)) ||
        element.control.kind == UIControlKind::ScrollView) {
        childClip = Intersect(childClip, rect);
    }
    if (!childClip.IsValid())
        return;
    for (const LayoutEntry& child : ResolveChildren(elements, element, rect, canvasScale)) {
        AppendElement(elements, owner, *child.element, child.rect, childClip, canvasScale, snapshot, painterOrder);
    }
}

[[nodiscard]] const UIDocumentElement* FindRoot(std::span<const UIDocumentElement* const> elements) noexcept {
    const UIDocumentElement* root = nullptr;
    for (const UIDocumentElement* element : elements) {
        if (element == nullptr || element->parentId != 0U)
            continue;
        if (root != nullptr)
            return nullptr;
        root = element;
    }
    return root;
}

} // namespace

void UIPresentationLayout::Build(std::span<const UIPresentationDocumentView> documents, std::uint32_t viewportWidth,
                                 std::uint32_t viewportHeight, UIPresentationSnapshot& output) {
    output.Clear();
    output.viewportWidth = viewportWidth;
    output.viewportHeight = viewportHeight;
    if (viewportWidth == 0U || viewportHeight == 0U)
        return;

    const UIPresentationRect viewport{0.0F, 0.0F, static_cast<float>(viewportWidth),
                                      static_cast<float>(viewportHeight)};
    std::uint64_t painterOrder = 0U;
    for (const UIPresentationDocumentView& document : documents) {
        const UIDocumentElement* root = FindRoot(document.elements);
        if (root == nullptr)
            continue;
        const float canvasScale = ResolveCanvasScale(*root, viewportWidth, viewportHeight);
        AppendElement(document.elements, document.owner, *root, viewport, viewport, canvasScale, output, painterOrder);
    }
}

void UIPresentationLayout::Build(const UIDocument& document, std::uint32_t viewportWidth, std::uint32_t viewportHeight,
                                 UIPresentationSnapshot& output) {
    std::vector<const UIDocumentElement*> elements;
    elements.reserve(document.elements.size());
    for (const UIDocumentElement& element : document.elements) {
        elements.push_back(&element);
    }
    const UIPresentationDocumentView view{.elements = elements};
    Build(std::span<const UIPresentationDocumentView>{&view, 1U}, viewportWidth, viewportHeight, output);
}

void UIPresentationBuilder::Build(SceneState& state, std::uint32_t viewportWidth, std::uint32_t viewportHeight) {
    UIPresentationSnapshot& snapshot = state.uiPresentation;
    snapshot.revision = ++state.uiPresentationRevision;
    std::vector<std::vector<const UIDocumentElement*>> elementStorage;
    std::vector<UIPresentationDocumentView> documents;
    elementStorage.reserve(state.uiDocuments.size());
    documents.reserve(state.uiDocuments.size());
    for (const auto& [id, record] : state.uiDocuments) {
        static_cast<void>(id);
        std::vector<const UIDocumentElement*>& elements = elementStorage.emplace_back();
        elements.reserve(record.elements.size());
        for (const auto& [elementId, element] : record.elements) {
            static_cast<void>(elementId);
            elements.push_back(&element);
        }
        documents.push_back(UIPresentationDocumentView{
            .owner = record.entity,
            .elements = elements,
        });
    }
    UIPresentationLayout::Build(documents, viewportWidth, viewportHeight, snapshot);
}

} // namespace kb::scene
