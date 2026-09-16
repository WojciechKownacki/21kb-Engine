#include "app/scene_viewport/EditorUIRectInteraction.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/EditorSceneContext.hpp"
#include <algorithm>
#include <cmath>

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
            selected ? kb::math::Color{0.55F, 0.4F, 1, 1} : kb::math::Color{0.5F, 0.6F, 0.7F, 0.8F};
        outline.border->borderWidth = {stroke, stroke, stroke, stroke};
        result.push_back(outline);
        if (!selected || canvas)
            continue;
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
            handle.border->backgroundColor = {0.85F, 0.8F, 1, 1};
            handle.border->borderColor = {0.45F, 0.25F, 0.9F, 1};
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
    context.UIRectDrag() = drag;
    return true;
}
bool EditorUIRectInteraction::Update(EditorSceneContext& context, float x, float y, bool preserveAspect,
                                     bool fromCenter) {
    if (!context.UIRectDrag())
        return false;
    auto& drag = *context.UIRectDrag();
    const float det = drag.basisX.x * drag.basisY.y - drag.basisX.y * drag.basisY.x;
    const float dx = x - drag.pointerStart.x, dy = y - drag.pointerStart.y;
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
