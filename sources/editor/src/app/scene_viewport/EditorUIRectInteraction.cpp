#include "app/scene_viewport/EditorUIRectInteraction.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/EditorSceneContext.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>

namespace kb::editor {
namespace {
using kb::math::Vec2;
using kb::scene::SceneUIFrameElement;
constexpr std::array<Vec2, 8> kHandles{{{0, 0}, {0.5F, 0}, {1, 0}, {1, 0.5F}, {1, 1}, {0.5F, 1}, {0, 1}, {0, 0.5F}}};
// Editor picking is not bounded by the game screen; it is bounded only by authored masks.
// Far wider than any usable layout coordinate, and small enough that x + width stays finite.
constexpr kb::math::Rect kUnclippedPickArea{-1.0e9F, -1.0e9F, 2.0e9F, 2.0e9F};
Vec2 Point(const SceneUIFrameElement& e, Vec2 uv) {
    return {e.corners[0].x + (e.corners[1].x - e.corners[0].x) * uv.x + (e.corners[3].x - e.corners[0].x) * uv.y,
            e.corners[0].y + (e.corners[1].y - e.corners[0].y) * uv.x + (e.corners[3].y - e.corners[0].y) * uv.y};
}
Vec2 RotateScale(Vec2 v, const kb::scene::UIRectTransform& r) {
    const float angle = kb::math::ToRadians(kb::math::Degrees{r.rotationDegrees}).Value();
    const float c = std::cos(angle), s = std::sin(angle);
    return {c * v.x * r.scale.x - s * v.y * r.scale.y, s * v.x * r.scale.x + c * v.y * r.scale.y};
}
constexpr float kSnapPixels = 6.0F;

// The lines a widget offers to a dragged one: its edges and its centre.
void AddLines(std::vector<float>& lines, float low, float size) {
    lines.push_back(low);
    lines.push_back(low + size * 0.5F);
    lines.push_back(low + size);
}

[[nodiscard]] bool AxisAligned(const SceneUIFrameElement& e) {
    return std::abs(e.corners[0].y - e.corners[1].y) < 0.01F && std::abs(e.corners[0].x - e.corners[3].x) < 0.01F;
}

[[nodiscard]] bool IsWithin(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, kb::scene::SceneEntity ancestor) {
    for (auto at = entity; at.IsValid(); at = scene.Hierarchy().Parent(at))
        if (at == ancestor) return true;
    return false;
}

// The offset that brings the nearest of `edges` onto the nearest of `lines`, if one is within `distance`.
[[nodiscard]] std::optional<float> NearestOffset(std::span<const float> edges, const std::vector<float>& lines, float distance) {
    std::optional<float> best;
    for (const float edge : edges)
        for (const float line : lines) {
            const float offset = line - edge;
            if (std::abs(offset) <= distance && (!best || std::abs(offset) < std::abs(*best))) best = offset;
        }
    return best;
}

// The lines that the edges, once moved by `offset`, lie on.
[[nodiscard]] std::vector<float> LinesTouched(std::span<const float> edges, const std::vector<float>& lines, float offset) {
    std::vector<float> touched;
    for (const float edge : edges)
        for (const float line : lines)
            if (std::abs(line - (edge + offset)) < 0.01F && std::ranges::none_of(touched, [line](float t) { return std::abs(t - line) < 0.01F; }))
                touched.push_back(line);
    return touched;
}
} // namespace
std::vector<SceneUIFrameElement> EditorUIRectInteraction::Overlays(const EditorSceneContext& context, float width,
                                                                   float height, float zoom) {
    kb::scene::SceneUIFrame frame;
    if (!kb::scene::SceneUIQueries{context.Scene()}.BuildFrame(width, height, frame))
        return {};
    const float radius = 4.0F / zoom;
    const float stroke = 1.0F / zoom;
    std::vector<SceneUIFrameElement> result;
    for (const auto& e : frame.elements) {
        const bool canvas = context.Scene().Components().UI().TryGet<kb::scene::UICanvas>(e.entity) != nullptr;
        const bool selected = e.entity == context.SelectedEntity();
        if (!canvas && !selected)
            continue;
        SceneUIFrameElement outline;
        outline.rect = e.rect;
        outline.corners = e.corners;
        outline.clipRect = {0, 0, width, height};
        outline.border = kb::scene::UIBorder{};
        outline.border->borderColor =
            selected ? kb::math::Color{0.85F, 0.85F, 0.85F, 1} : kb::math::Color{0.5F, 0.6F, 0.7F, 0.8F};
        outline.border->borderWidth = {stroke, stroke, stroke, stroke};
        result.push_back(outline);
        if (!selected || canvas)
            continue;
        if (const auto& drag = context.UIRectDrag(); drag && drag->entity == e.entity) {
            const auto guide = [&](kb::math::Rect line) {
                SceneUIFrameElement element;
                element.rect = line;
                element.corners = {{{line.x, line.y}, {line.x + line.width, line.y},
                                    {line.x + line.width, line.y + line.height}, {line.x, line.y + line.height}}};
                element.clipRect = {0, 0, width, height};
                element.border = kb::scene::UIBorder{};
                element.border->backgroundColor = {1.0F, 0.2F, 0.75F, 1};
                result.push_back(element);
            };
            for (const float x : drag->guideX) guide({x - stroke * 0.5F, 0.0F, stroke, drag->extent.y});
            for (const float y : drag->guideY) guide({0.0F, y - stroke * 0.5F, drag->extent.x, stroke});
        }
        for (const Vec2 uv : kHandles) {
            const Vec2 p = Point(e, uv);
            SceneUIFrameElement handle;
            handle.rect = {p.x - radius, p.y - radius, radius * 2, radius * 2};
            handle.corners = {{{p.x - radius, p.y - radius},
                               {p.x + radius, p.y - radius},
                               {p.x + radius, p.y + radius},
                               {p.x - radius, p.y + radius}}};
            handle.clipRect = {0, 0, width, height};
            handle.border = kb::scene::UIBorder{};
            handle.border->backgroundColor = {0.95F, 0.95F, 0.95F, 1};
            handle.border->borderColor = {0.45F, 0.45F, 0.45F, 1};
            handle.border->borderWidth = {stroke, stroke, stroke, stroke};
            result.push_back(handle);
        }
    }
    return result;
}
bool EditorUIRectInteraction::Begin(EditorSceneContext& context, float width, float height, float x, float y,
                                    float zoom) {
    if (context.HasPlayModeSceneSession() || context.UIRectDrag())
        return false;
    kb::scene::SceneUIFrame frame;
    if (!kb::scene::SceneUIQueries{context.Scene()}.BuildFrame(width, height, frame))
        return false;
    int handle = -1;
    kb::scene::SceneEntity entity{};
    for (const auto& e : frame.elements) {
        if (e.entity != context.SelectedEntity() ||
            context.Scene().Components().UI().TryGet<kb::scene::UICanvas>(e.entity))
            continue;
        for (std::size_t i = 0; i < kHandles.size(); ++i) {
            const Vec2 p = Point(e, kHandles[i]);
            if (std::abs(x - p.x) <= 7 / zoom && std::abs(y - p.y) <= 7 / zoom) {
                entity = e.entity;
                handle = static_cast<int>(i);
                break;
            }
        }
    }
    if (!entity.IsValid()) {
        for (auto& e : frame.elements) {
            // HitTest still checks every authored mask quad, so a Mask clips picking exactly
            // as it clips pixels. Only the game-screen clip is lifted: authoring routinely
            // parks a widget outside the canvas, and the editor must still let you grab it.
            e.clipRect = kUnclippedPickArea;
            e.hitTestable =
                e.effectiveOpacity > 0 && !context.Scene().Components().UI().TryGet<kb::scene::UICanvas>(e.entity);
        }
        entity = frame.HitTest({x, y});
        // A pointer that missed every widget is not a UI interaction. Reporting that back
        // lets the caller keep routing the click to the gizmo, rectangle selection and 3D
        // picking instead of having it swallowed by any scene that contains a canvas.
        if (!entity.IsValid())
            return false;
        context.SelectEntity(entity);
    }
    const auto found = std::ranges::find(frame.elements, entity, &SceneUIFrameElement::entity);
    const auto* rect = context.Scene().Components().UI().TryGet<kb::scene::UIRectTransform>(entity);
    if (found == frame.elements.end() || rect == nullptr || found->rect.width <= 0 || found->rect.height <= 0)
        return true;
    const auto parent = context.Scene().Hierarchy().Parent(entity);
    const auto& ui = context.Scene().Components().UI();
    const auto* layout = ui.TryGet<kb::scene::UILayoutElement>(entity);
    const bool driven = parent.IsValid() && (layout == nullptr || !layout->ignoreLayout) &&
                        (ui.TryGet<kb::scene::UIHorizontalLayout>(parent) ||
                         ui.TryGet<kb::scene::UIVerticalLayout>(parent) || ui.TryGet<kb::scene::UIGridLayout>(parent) ||
                         ui.TryGet<kb::scene::UIWrapLayout>(parent) || ui.TryGet<kb::scene::UIOverlayLayout>(parent));
    if (driven || (handle >= 0 && (ui.TryGet<kb::scene::UIContentSizeFitter>(entity) ||
                                   ui.TryGet<kb::scene::UIAspectRatioFitter>(entity)))) {
        context.Console().Warning("UI", "This rectangle is controlled by a layout component. Edit its layout "
                                        "properties or enable Ignore Layout before dragging.");
        return true;
    }
    const float w = found->rect.width / found->canvasScale, h = found->rect.height / found->canvasScale;
    EditorUIRectDragState drag{
        .entity = entity,
        .original = *rect,
        .pointerStart = {x, y},
        .basisX = {(found->corners[1].x - found->corners[0].x) / w, (found->corners[1].y - found->corners[0].y) / w},
        .basisY = {(found->corners[3].x - found->corners[0].x) / h, (found->corners[3].y - found->corners[0].y) / h},
        .size = {w, h},
        .handle = handle};
    if (std::abs(drag.basisX.x * drag.basisY.y - drag.basisX.y * drag.basisY.x) < 0.000001F) {
        context.Console().Warning("UI", "Cannot resize a UI object with a collapsed transform.");
        return true;
    }
    drag.grabbed = found->rect;
    drag.snapDistance = kSnapPixels / zoom;
    drag.extent = {width, height};
    if (AxisAligned(*found)) {
        for (const auto& other : frame.elements) {
            if (IsWithin(context.Scene(), other.entity, entity) || other.rect.width <= 0 || other.rect.height <= 0 ||
                other.effectiveOpacity <= 0 || !AxisAligned(other))
                continue;
            AddLines(drag.snapX, other.rect.x, other.rect.width);
            AddLines(drag.snapY, other.rect.y, other.rect.height);
        }
    }
    context.UIRectDrag() = drag;
    return true;
}
bool EditorUIRectInteraction::Update(EditorSceneContext& context, float x, float y, bool preserveAspect,
                                     bool fromCenter, bool snap) {
    if (!context.UIRectDrag())
        return false;
    auto& drag = *context.UIRectDrag();
    const float det = drag.basisX.x * drag.basisY.y - drag.basisX.y * drag.basisY.x;
    float dx = x - drag.pointerStart.x, dy = y - drag.pointerStart.y;
    drag.guideX.clear();
    drag.guideY.clear();
    if (snap && !preserveAspect && !fromCenter && !drag.snapX.empty()) {
        // The edges the pointer is moving: all three lines of a moved widget, the grabbed edge of a resized one.
        const auto& r = drag.grabbed;
        const Vec2 uv = drag.handle < 0 ? Vec2{0.5F, 0.5F} : kHandles[static_cast<std::size_t>(drag.handle)];
        const std::array<float, 3> xs{r.x, r.x + r.width * 0.5F, r.x + r.width};
        const std::array<float, 3> ys{r.y, r.y + r.height * 0.5F, r.y + r.height};
        const std::span<const float> xe = drag.handle < 0 ? std::span<const float>{xs}
            : uv.x == 0.5F ? std::span<const float>{} : std::span<const float>{xs.data() + (uv.x == 0 ? 0 : 2), 1};
        const std::span<const float> ye = drag.handle < 0 ? std::span<const float>{ys}
            : uv.y == 0.5F ? std::span<const float>{} : std::span<const float>{ys.data() + (uv.y == 0 ? 0 : 2), 1};
        const auto shifted = [](std::span<const float> edges, float by) {
            std::vector<float> moved(edges.begin(), edges.end());
            for (float& edge : moved) edge += by;
            return moved;
        };
        if (const auto offset = NearestOffset(shifted(xe, dx), drag.snapX, drag.snapDistance)) dx += *offset;
        if (const auto offset = NearestOffset(shifted(ye, dy), drag.snapY, drag.snapDistance)) dy += *offset;
        drag.guideX = LinesTouched(xe, drag.snapX, dx);
        drag.guideY = LinesTouched(ye, drag.snapY, dy);
    }
    if (!drag.changed) {
        if (std::abs(dx) < 0.01F && std::abs(dy) < 0.01F)
            return true;
        if (!context.BeginSceneEditTransaction(drag.handle < 0 ? "Move UI object" : "Resize UI object"))
            return false;
    }
    Vec2 delta{(dx * drag.basisY.y - dy * drag.basisY.x) / det, (dy * drag.basisX.x - dx * drag.basisX.y) / det};
    auto rect = drag.original;
    Vec2 shift{}, sizeDelta{};
    if (drag.handle < 0)
        shift = RotateScale(delta, rect);
    else {
        const Vec2 uv = kHandles[static_cast<std::size_t>(drag.handle)];
        if (uv.x != 0.5F)
            sizeDelta.x = delta.x * (uv.x == 0 ? -1.0F : 1.0F) * (fromCenter ? 2.0F : 1.0F);
        if (uv.y != 0.5F)
            sizeDelta.y = delta.y * (uv.y == 0 ? -1.0F : 1.0F) * (fromCenter ? 2.0F : 1.0F);
        if (preserveAspect && uv.x != 0.5F && uv.y != 0.5F) {
            float ratio = std::abs(sizeDelta.x / drag.size.x) > std::abs(sizeDelta.y / drag.size.y)
                              ? sizeDelta.x / drag.size.x
                              : sizeDelta.y / drag.size.y;
            ratio = std::max(ratio, std::max(1.0F / drag.size.x, 1.0F / drag.size.y) - 1.0F);
            sizeDelta = {ratio * drag.size.x, ratio * drag.size.y};
        }
        sizeDelta.x = std::max(1.0F - drag.size.x, sizeDelta.x);
        sizeDelta.y = std::max(1.0F - drag.size.y, sizeDelta.y);
        const Vec2 low{fromCenter ? -sizeDelta.x * 0.5F : (uv.x == 0 ? -sizeDelta.x : 0),
                       fromCenter ? -sizeDelta.y * 0.5F : (uv.y == 0 ? -sizeDelta.y : 0)};
        shift = RotateScale({low.x + rect.pivot.x * sizeDelta.x, low.y + rect.pivot.y * sizeDelta.y}, rect);
        shift.x -= rect.pivot.x * sizeDelta.x;
        shift.y -= rect.pivot.y * sizeDelta.y;
    }
    rect.offsetMin.x += shift.x;
    rect.offsetMin.y += shift.y;
    rect.offsetMax.x += shift.x + sizeDelta.x;
    rect.offsetMax.y += shift.y + sizeDelta.y;
    context.Scene().Components().UI().Set(drag.entity, rect);
    drag.changed = drag.changed || std::abs(dx) > 0.01F || std::abs(dy) > 0.01F;
    context.MarkSceneRenderDirty();
    return true;
}
bool EditorUIRectInteraction::End(EditorSceneContext& context, bool cancel) {
    if (!context.UIRectDrag())
        return false;
    const bool changed = context.UIRectDrag()->changed;
    context.UIRectDrag().reset();
    if (!changed)
        return true;
    if (cancel) {
        EditorPlayModeSelectionSnapshot selection;
        EditorHierarchySelectionState selected;
        selected.SelectEntity(context.SelectedEntity());
        selection.CaptureAuthoredHierarchy(context.Scene());
        selection.CaptureSelection(selected);
        context.CancelSceneEditTransaction();
        selection.Restore(context.Scene(), selected);
        context.SelectEntity(selected.Primary());
    } else
        static_cast<void>(context.CommitSceneEditTransaction());
    return true;
}
} // namespace kb::editor
