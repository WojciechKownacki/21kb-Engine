#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/ScenePrefabInstance.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/scene/SceneUIHierarchyPresets.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "engine/ui/UIComponentCatalog.hpp"
#include "engine/ui/UIComponentPropertyCatalog.hpp"
#include "engine/ui/UIComponentValidation.hpp"
#include "scene/asset/io/SceneAssetReader.hpp"
#include "scene/asset/io/components/SceneAssetUIComponentCodec.hpp"
#include "scene/ui/SceneUIComponentTextCodec.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using kb::scene::SceneEntity;

[[nodiscard]] kb::scene::UIRectTransform Rect(float x, float y, float width, float height, std::int32_t zOrder = 0) {
    kb::scene::UIRectTransform value;
    value.offsetMin = {x, y};
    value.offsetMax = {x + width, y + height};
    value.zOrder = zOrder;
    return value;
}

[[nodiscard]] kb::scene::UIComponentSet CanvasComponents() {
    kb::scene::UIComponentSet components = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Canvas);
    components.canvasScaler->scaleMode = kb::scene::UICanvasScaleMode::ConstantPixelSize;
    return components;
}

[[nodiscard]] kb::scene::SceneObject AddUI(kb::scene::Scene& scene, kb::scene::SceneObject parent,
    const kb::scene::UIComponentSet& components) {
    kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.parent = parent});
    kb::scene::ApplySceneUIComponents(scene.Components().UI(), object.Entity(), components);
    return object;
}

[[nodiscard]] const kb::scene::SceneUIFrameElement* FindElement(
    const kb::scene::SceneUIFrame& frame, SceneEntity entity) {
    const auto found = std::ranges::find(frame.elements, entity, &kb::scene::SceneUIFrameElement::entity);
    return found != frame.elements.end() ? &*found : nullptr;
}

[[nodiscard]] bool HasEvent(std::span<const kb::scene::SceneUIEvent> events,
    kb::scene::SceneUIEventType type, SceneEntity entity) {
    return std::ranges::any_of(events, [=](const kb::scene::SceneUIEvent& event) {
        return event.type == type && event.entity == entity;
    });
}

[[nodiscard]] const kb::scene::SceneUIEvent* FindEvent(std::span<const kb::scene::SceneUIEvent> events,
    kb::scene::SceneUIEventType type, SceneEntity entity) {
    const auto found = std::ranges::find_if(events, [=](const kb::scene::SceneUIEvent& event) {
        return event.type == type && event.entity == entity;
    });
    return found != events.end() ? &*found : nullptr;
}

void TestCatalogAndPresets() {
    const std::span<const kb::scene::UIComponentDescriptor> catalog = kb::scene::UIComponentCatalog();
    kb::tests::Require(catalog.size() == 31U, "UI component catalog must expose every canonical ECS component");
    std::array<bool, 31U> seen{};
    for (const kb::scene::UIComponentDescriptor& descriptor : catalog) {
        const std::size_t index = static_cast<std::size_t>(descriptor.type);
        kb::tests::Require(index < seen.size() && !seen[index], "UI component catalog types must be unique and contiguous");
        seen[index] = true;
        kb::tests::Require(!descriptor.stableId.empty() && !descriptor.displayName.empty(), "UI component catalog names must be non-empty");
        kb::tests::Require(kb::scene::FindUIComponentDescriptor(descriptor.type) == &descriptor,
            "UI component catalog type lookup must return its canonical descriptor");
        kb::tests::Require(kb::scene::FindUIComponentDescriptor(descriptor.stableId) == &descriptor,
            "UI component catalog stable-id lookup must return its canonical descriptor");
        kb::tests::Require(kb::scene::FindUIComponentDescriptor(descriptor.displayName) == &descriptor,
            "UI component catalog display-name lookup must use the same resolver");
        kb::tests::Require(!kb::scene::UIComponentPropertyCatalog(descriptor.type).empty(),
            "Every UI component must expose properties through the canonical property catalog");
    }

    constexpr std::array visiblePresets{
        kb::scene::UIComponentPreset::Button,
        kb::scene::UIComponentPreset::Toggle,
        kb::scene::UIComponentPreset::Slider,
        kb::scene::UIComponentPreset::Scrollbar,
        kb::scene::UIComponentPreset::ScrollView,
        kb::scene::UIComponentPreset::InputField,
        kb::scene::UIComponentPreset::Dropdown,
        kb::scene::UIComponentPreset::ProgressBar,
    };
    for (const kb::scene::UIComponentPresetDescriptor& descriptor : kb::scene::UIComponentPresetCatalog()) {
        kb::tests::Require(!descriptor.name.empty() && !descriptor.components.empty(), "UI presets must have a name and composition");
        kb::tests::Require(kb::scene::FindUIComponentPreset(descriptor.name) == &descriptor,
            "UI preset name lookup must return its canonical descriptor");
        const kb::scene::UIComponentSet built = kb::scene::BuildUIComponentPreset(descriptor.preset);
        kb::tests::Require(kb::scene::IsUIComponentSetValid(built), "Every UI preset must build a valid component set");
        for (const kb::scene::UIComponentDescriptor& component : catalog) {
            const bool expected = std::ranges::find(descriptor.components, component.type) != descriptor.components.end();
            kb::tests::Require(kb::scene::HasUIComponent(built, component.type) == expected,
                "UI preset composition must exactly match the canonical preset catalog");
        }
        if (std::ranges::find(visiblePresets, descriptor.preset) != visiblePresets.end()) {
            kb::tests::Require(built.border.has_value() && built.border->backgroundColor.a > 0.0F,
                "Interactive and progress presets must have a visible authored surface");
        }

        // A widget must arrive at a size that suits it. Leaving UIRectTransform's default
        // 100x100 square made every authored menu start as a stack of identical squares on
        // one point, so the author's first act was always to retype width and height.
        kb::tests::Require(built.rectTransform.has_value(), "Every UI preset must author a Rect Transform");
        if (descriptor.preset != kb::scene::UIComponentPreset::Canvas) {
            const kb::math::Vec2 size = built.rectTransform->offsetMax;
            kb::tests::Require(size.x > 0.0F && size.y > 0.0F, "Every non-canvas UI preset must author a positive size");
            kb::tests::Require(size.x != 100.0F || size.y != 100.0F,
                "A UI preset must not ship the placeholder 100x100 square as its authored size");
        }
    }

    // The proportions are the point, not just "not 100x100": a button reads as a button
    // because it is wide and short, and a toggle because it is small and square.
    const kb::scene::UIComponentSet button = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Button);
    kb::tests::Require(button.rectTransform->offsetMax.x > button.rectTransform->offsetMax.y * 2.0F,
        "A Button preset must arrive wider than it is tall");
    const kb::scene::UIComponentSet toggle = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Toggle);
    kb::tests::Require(toggle.rectTransform->offsetMax.x == toggle.rectTransform->offsetMax.y,
        "A Toggle preset must arrive square");
    kb::tests::Require(toggle.rectTransform->offsetMax.x < button.rectTransform->offsetMax.x,
        "A Toggle preset must arrive smaller than a Button");
}

void TestLayoutsAndFitters() {
    kb::scene::Scene scene{kb::scene::SceneMode::PrefabPrivate};
    const kb::scene::SceneObject canvas = AddUI(scene, {}, CanvasComponents());
    kb::scene::UIComponentSet childComponents;
    childComponents.rectTransform = Rect(0.0F, 0.0F, 20.0F, 10.0F);
    const kb::scene::SceneObject first = AddUI(scene, canvas, childComponents);
    const kb::scene::SceneObject second = AddUI(scene, canvas, childComponents);
    kb::scene::SceneUIComponents ui = scene.Components().UI();

    kb::scene::UIHorizontalLayout horizontal;
    horizontal.spacing = 5.0F;
    horizontal.controlChildWidth = true;
    horizontal.controlChildHeight = true;
    ui.Set(canvas.Entity(), horizontal);
    kb::tests::Require(scene.UI().Update(100.0F, 100.0F, {}, 0.016F), "Horizontal layout frame must build");
    const kb::scene::SceneUIFrameElement* firstFrame = FindElement(scene.UI().Frame(), first.Entity());
    const kb::scene::SceneUIFrameElement* secondFrame = FindElement(scene.UI().Frame(), second.Entity());
    kb::tests::Require(firstFrame != nullptr && secondFrame != nullptr, "Horizontal layout children must be present");
    kb::tests::Require(kb::tests::NearlyEqual(firstFrame->rect.width, 20.0F) && kb::tests::NearlyEqual(secondFrame->rect.x, 25.0F),
        "Horizontal layout must use preferred sizes and spacing in authored order");

    horizontal.expandChildWidth = true;
    horizontal.horizontalAlignment = kb::scene::UIAlignment::Center;
    ui.Set(canvas.Entity(), horizontal);
    kb::tests::Require(scene.UI().Update(100.0F, 100.0F, {}, 0.016F), "Expanded horizontal layout frame must build");
    firstFrame = FindElement(scene.UI().Frame(), first.Entity());
    secondFrame = FindElement(scene.UI().Frame(), second.Entity());
    kb::tests::Require(firstFrame != nullptr && secondFrame != nullptr &&
            kb::tests::NearlyEqual(firstFrame->rect.x, 0.0F) && kb::tests::NearlyEqual(firstFrame->rect.width, 47.5F) &&
            kb::tests::NearlyEqual(secondFrame->rect.x + secondFrame->rect.width, 100.0F),
        "Horizontal expansion must align the final expanded span without overflowing the container");

    ui.Remove<kb::scene::UIHorizontalLayout>(canvas.Entity());
    kb::scene::UIVerticalLayout vertical;
    vertical.spacing = 3.0F;
    vertical.controlChildWidth = true;
    vertical.controlChildHeight = true;
    ui.Set(canvas.Entity(), vertical);
    kb::tests::Require(scene.UI().Update(100.0F, 100.0F, {}, 0.016F), "Vertical layout frame must build");
    secondFrame = FindElement(scene.UI().Frame(), second.Entity());
    kb::tests::Require(secondFrame != nullptr && kb::tests::NearlyEqual(secondFrame->rect.y, 13.0F),
        "Vertical layout must use preferred sizes and spacing in authored order");

    vertical.expandChildHeight = true;
    vertical.verticalAlignment = kb::scene::UIAlignment::End;
    ui.Set(canvas.Entity(), vertical);
    kb::tests::Require(scene.UI().Update(100.0F, 100.0F, {}, 0.016F), "Expanded vertical layout frame must build");
    firstFrame = FindElement(scene.UI().Frame(), first.Entity());
    secondFrame = FindElement(scene.UI().Frame(), second.Entity());
    kb::tests::Require(firstFrame != nullptr && secondFrame != nullptr &&
            kb::tests::NearlyEqual(firstFrame->rect.y, 0.0F) &&
            kb::tests::NearlyEqual(secondFrame->rect.y + secondFrame->rect.height, 100.0F),
        "Vertical expansion must align the final expanded span without overflowing the container");

    ui.Remove<kb::scene::UIVerticalLayout>(canvas.Entity());
    kb::scene::UIGridLayout grid;
    grid.columns = 2U;
    grid.cellSize = {15.0F, 12.0F};
    grid.spacing = {2.0F, 4.0F};
    grid.horizontalAlignment = kb::scene::UIAlignment::Center;
    ui.Set(canvas.Entity(), grid);
    kb::tests::Require(scene.UI().Update(100.0F, 100.0F, {}, 0.016F), "Grid layout frame must build");
    firstFrame = FindElement(scene.UI().Frame(), first.Entity());
    secondFrame = FindElement(scene.UI().Frame(), second.Entity());
    kb::tests::Require(firstFrame != nullptr && secondFrame != nullptr && kb::tests::NearlyEqual(firstFrame->rect.x, 34.0F) &&
        kb::tests::NearlyEqual(secondFrame->rect.x, 51.0F), "Grid layout must honor cell size, spacing, columns, and alignment");

    ui.Remove<kb::scene::UIGridLayout>(canvas.Entity());
    ui.Set(first.Entity(), Rect(0.0F, 0.0F, 60.0F, 10.0F));
    ui.Set(second.Entity(), Rect(0.0F, 0.0F, 60.0F, 20.0F));
    kb::scene::UIWrapLayout wrap;
    wrap.spacing = {2.0F, 3.0F};
    ui.Set(canvas.Entity(), wrap);
    kb::tests::Require(scene.UI().Update(100.0F, 100.0F, {}, 0.016F), "Wrap layout frame must build");
    secondFrame = FindElement(scene.UI().Frame(), second.Entity());
    kb::tests::Require(secondFrame != nullptr && kb::tests::NearlyEqual(secondFrame->rect.x, 0.0F) &&
        kb::tests::NearlyEqual(secondFrame->rect.y, 13.0F), "Wrap layout must advance to a new row when the next child does not fit");

    wrap.horizontalAlignment = kb::scene::UIAlignment::Center;
    wrap.verticalAlignment = kb::scene::UIAlignment::End;
    ui.Set(canvas.Entity(), wrap);
    kb::tests::Require(scene.UI().Update(100.0F, 100.0F, {}, 0.016F), "Aligned wrap layout frame must build");
    firstFrame = FindElement(scene.UI().Frame(), first.Entity());
    secondFrame = FindElement(scene.UI().Frame(), second.Entity());
    kb::tests::Require(firstFrame != nullptr && secondFrame != nullptr &&
            kb::tests::NearlyEqual(firstFrame->rect.x, 20.0F) && kb::tests::NearlyEqual(firstFrame->rect.y, 67.0F) &&
            kb::tests::NearlyEqual(secondFrame->rect.x, 20.0F) && kb::tests::NearlyEqual(secondFrame->rect.y, 80.0F),
        "Wrap layout must align each row horizontally and the complete row block vertically");

    ui.Remove<kb::scene::UIWrapLayout>(canvas.Entity());
    ui.Set(canvas.Entity(), kb::scene::UIOverlayLayout{});
    kb::tests::Require(scene.UI().Update(100.0F, 100.0F, {}, 0.016F), "Overlay layout frame must build");
    firstFrame = FindElement(scene.UI().Frame(), first.Entity());
    kb::tests::Require(firstFrame != nullptr && kb::tests::NearlyEqual(firstFrame->rect.width, 100.0F) &&
        kb::tests::NearlyEqual(firstFrame->rect.height, 100.0F), "Overlay stretch must fill the available rect");

    ui.Remove<kb::scene::UIOverlayLayout>(canvas.Entity());
    ui.Set(first.Entity(), Rect(0.0F, 0.0F, 80.0F, 80.0F));
    ui.Set(first.Entity(), kb::scene::UILayoutElement{.minimumWidth = 10.0F, .minimumHeight = 8.0F,
        .preferredWidth = 30.0F, .preferredHeight = 20.0F});
    ui.Set(first.Entity(), kb::scene::UIContentSizeFitter{.horizontalFit = kb::scene::UIFitMode::PreferredSize,
        .verticalFit = kb::scene::UIFitMode::PreferredSize});
    ui.Set(first.Entity(), kb::scene::UIAspectRatioFitter{.mode = kb::scene::UIAspectFitMode::WidthControlsHeight,
        .aspectRatio = 2.0F});
    kb::tests::Require(scene.UI().Update(100.0F, 100.0F, {}, 0.016F), "Fitted frame must build");
    firstFrame = FindElement(scene.UI().Frame(), first.Entity());
    kb::tests::Require(firstFrame != nullptr && kb::tests::NearlyEqual(firstFrame->rect.width, 30.0F) &&
        kb::tests::NearlyEqual(firstFrame->rect.height, 15.0F), "Content and aspect fitters must resolve one final rect");

    ui.Remove<kb::scene::UILayoutElement>(first.Entity());
    ui.Remove<kb::scene::UIAspectRatioFitter>(first.Entity());
    ui.Set(first.Entity(), Rect(0.0F, 0.0F, 0.0F, 20.0F));
    kb::scene::UIText unicodeText;
    unicodeText.fontSize = 10.0F;
    kb::tests::Require(kb::scene::SetUITextContent(unicodeText, "\xC4\x85\xF0\x9F\x99\x82"),
        "Unicode content-size fixture must fit the text component");
    ui.Set(first.Entity(), unicodeText);
    ui.Set(first.Entity(), kb::scene::UIContentSizeFitter{.horizontalFit = kb::scene::UIFitMode::PreferredSize});
    kb::tests::Require(scene.UI().Update(100.0F, 100.0F, {}, 0.016F), "Unicode content-size frame must build");
    firstFrame = FindElement(scene.UI().Frame(), first.Entity());
    kb::tests::Require(firstFrame != nullptr && kb::tests::NearlyEqual(firstFrame->rect.width, 10.0F),
        "Text measurement must count UTF-8 scalars instead of encoded bytes");

    ui.Set(canvas.Entity(), kb::scene::UIHorizontalLayout{});
    ui.Set(canvas.Entity(), kb::scene::UIVerticalLayout{});
    const std::size_t previousCount = scene.UI().Frame().elements.size();
    kb::tests::Require(!scene.UI().Update(100.0F, 100.0F, {}, 0.016F), "Conflicting layout components must fail closed");
    kb::tests::Require(scene.UI().Frame().elements.size() == previousCount, "A rejected frame must not replace the last valid frame");

    // Failing closed is only half the contract: a refusal nobody can attribute reads as a
    // dead renderer. It has to name the widget to fix and why.
    const kb::scene::SceneUIFrameRefusal& refusal = scene.UI().Frame().refusal;
    kb::tests::Require(refusal.HasValue(), "A rejected frame must publish a refusal reason");
    kb::tests::Require(refusal.entity == canvas.Entity(), "A refusal must name the entity that caused it");
    kb::tests::Require(std::string_view{refusal.reason}.find("layout") != std::string_view::npos,
        "A refusal reason must describe the conflict it found");

    ui.Remove<kb::scene::UIVerticalLayout>(canvas.Entity());
    kb::tests::Require(scene.UI().Update(100.0F, 100.0F, {}, 0.016F), "Removing the conflict must let the frame build again");
    kb::tests::Require(!scene.UI().Frame().refusal.HasValue(), "A frame that builds must clear the previous refusal");
}

void TestHierarchicalTransformsAndMaskHitTesting() {
    kb::scene::Scene scene{kb::scene::SceneMode::PrefabPrivate};
    const kb::scene::SceneObject canvas = AddUI(scene, {}, CanvasComponents());
    kb::scene::UIComponentSet parentComponents;
    parentComponents.rectTransform = Rect(50.0F, 50.0F, 100.0F, 50.0F);
    parentComponents.rectTransform->scale = {2.0F, 2.0F};
    parentComponents.rectTransform->rotationDegrees = 90.0F;
    const kb::scene::SceneObject parent = AddUI(scene, canvas, parentComponents);
    kb::scene::UIComponentSet childComponents;
    childComponents.rectTransform = Rect(-20.0F, -20.0F, 140.0F, 90.0F);
    const kb::scene::SceneObject child = AddUI(scene, parent, childComponents);
    kb::scene::UIComponentSet grandchildComponents;
    grandchildComponents.rectTransform = Rect(10.0F, 10.0F, 20.0F, 10.0F);
    const kb::scene::SceneObject grandchild = AddUI(scene, child, grandchildComponents);

    kb::scene::UIComponentSet maskComponents;
    maskComponents.rectTransform = Rect(50.0F, 50.0F, 100.0F, 50.0F);
    maskComponents.rectTransform->rotationDegrees = 45.0F;
    maskComponents.mask.emplace();
    const kb::scene::SceneObject mask = AddUI(scene, canvas, maskComponents);
    kb::scene::UIComponentSet clippedComponents;
    clippedComponents.rectTransform = Rect(-25.0F, -25.0F, 150.0F, 100.0F);
    clippedComponents.selectable.emplace();
    const kb::scene::SceneObject clipped = AddUI(scene, mask, clippedComponents);

    kb::tests::Require(scene.UI().Update(220.0F, 180.0F, {}, 0.016F), "Hierarchical transform frame must build");
    const kb::scene::SceneUIFrameElement* grandchildFrame = FindElement(scene.UI().Frame(), grandchild.Entity());
    kb::tests::Require(grandchildFrame != nullptr && kb::tests::NearlyEqual(grandchildFrame->corners[0].x, 170.0F) &&
            kb::tests::NearlyEqual(grandchildFrame->corners[0].y, -45.0F) &&
            kb::tests::NearlyEqual(grandchildFrame->corners[2].x, 150.0F) &&
            kb::tests::NearlyEqual(grandchildFrame->corners[2].y, -5.0F),
        "Descendants must inherit every parent translation, scale, and rotation");
    const kb::scene::SceneUIFrameElement* clippedFrame = FindElement(scene.UI().Frame(), clipped.Entity());
    kb::tests::Require(clippedFrame != nullptr && clippedFrame->clipQuads.size() == 1U,
        "A masked descendant must retain the exact transformed mask geometry");
    kb::tests::Require(scene.UI().HitTest({100.0F, 75.0F}) == clipped.Entity(),
        "Hit testing must accept a point inside the transformed mask");
    kb::tests::Require(!scene.UI().HitTest({50.0F, 25.0F}).IsValid(),
        "Hit testing must reject a point inside the mask bounds but outside its transformed quad");
}

void TestClippingGroupsSortingAndScaling() {
    kb::scene::Scene scene{kb::scene::SceneMode::PrefabPrivate};
    kb::scene::UIComponentSet canvasComponents = CanvasComponents();
    canvasComponents.canvasScaler->scaleMode = kb::scene::UICanvasScaleMode::ScaleWithScreenSize;
    canvasComponents.canvasScaler->referenceResolution = {100.0F, 100.0F};
    const kb::scene::SceneObject canvas = AddUI(scene, {}, canvasComponents);

    kb::scene::UIComponentSet maskComponents;
    maskComponents.rectTransform = Rect(0.0F, 0.0F, 50.0F, 50.0F);
    maskComponents.mask.emplace();
    maskComponents.canvasGroup = kb::scene::UICanvasGroup{.opacity = 0.5F};
    const kb::scene::SceneObject mask = AddUI(scene, canvas, maskComponents);
    kb::scene::UIComponentSet clippedComponents;
    clippedComponents.rectTransform = Rect(40.0F, 0.0F, 40.0F, 30.0F);
    clippedComponents.selectable.emplace();
    const kb::scene::SceneObject clipped = AddUI(scene, mask, clippedComponents);

    kb::scene::UIComponentSet lowerComponents;
    lowerComponents.rectTransform = Rect(0.0F, 60.0F, 30.0F, 30.0F, 1);
    lowerComponents.selectable.emplace();
    const kb::scene::SceneObject lower = AddUI(scene, canvas, lowerComponents);
    kb::scene::UIComponentSet upperComponents = lowerComponents;
    upperComponents.rectTransform->zOrder = 2;
    const kb::scene::SceneObject upper = AddUI(scene, canvas, upperComponents);

    kb::tests::Require(scene.UI().Update(200.0F, 200.0F, {}, 0.016F), "Scaled and clipped frame must build");
    const kb::scene::SceneUIFrameElement* clippedFrame = FindElement(scene.UI().Frame(), clipped.Entity());
    kb::tests::Require(clippedFrame != nullptr && kb::tests::NearlyEqual(clippedFrame->canvasScale, 2.0F) &&
        kb::tests::NearlyEqual(clippedFrame->effectiveOpacity, 0.5F), "Canvas scaling and group opacity must be resolved in the frame");
    kb::tests::Require(scene.UI().HitTest({90.0F, 10.0F}) == clipped.Entity(), "Hit test must accept the visible part of a clipped element");
    kb::tests::Require(!scene.UI().HitTest({120.0F, 10.0F}).IsValid(), "Hit test must reject pixels outside the inherited clip");
    kb::tests::Require(scene.UI().HitTest({20.0F, 140.0F}) == upper.Entity(), "Higher z-order must win hit testing independent of insertion order");
    kb::tests::Require(FindElement(scene.UI().Frame(), lower.Entity())->traversalOrder <
        FindElement(scene.UI().Frame(), upper.Entity())->traversalOrder, "Sibling traversal order must remain authored hierarchy order");
}

void TestInteractionAndEditing() {
    kb::scene::Scene scene{kb::scene::SceneMode::PrefabPrivate};
    const kb::scene::SceneObject canvas = AddUI(scene, {}, CanvasComponents());

    const auto addPreset = [&](kb::scene::UIComponentPreset preset, kb::scene::UIRectTransform rect) {
        kb::scene::UIComponentSet components = kb::scene::BuildUIComponentPreset(preset);
        components.rectTransform = rect;
        return AddUI(scene, canvas, components);
    };
    const kb::scene::SceneObject toggle = addPreset(kb::scene::UIComponentPreset::Toggle, Rect(0.0F, 0.0F, 20.0F, 20.0F));
    const kb::scene::SceneObject slider = addPreset(kb::scene::UIComponentPreset::Slider, Rect(0.0F, 30.0F, 100.0F, 10.0F));
    const kb::scene::SceneObject scrollbar = addPreset(kb::scene::UIComponentPreset::Scrollbar, Rect(0.0F, 50.0F, 100.0F, 10.0F));
    scene.Components().UI().TryGet<kb::scene::UIScrollbar>(scrollbar.Entity())->size = 0.5F;
    scene.Components().UI().MarkModified<kb::scene::UIScrollbar>(scrollbar.Entity());
    const kb::scene::SceneObject inputField = addPreset(kb::scene::UIComponentPreset::InputField, Rect(0.0F, 70.0F, 100.0F, 20.0F));
    const kb::scene::SceneObject scrollView = addPreset(kb::scene::UIComponentPreset::ScrollView, Rect(0.0F, 100.0F, 100.0F, 20.0F));
    kb::scene::UIComponentSet content;
    content.rectTransform = Rect(0.0F, 0.0F, 20.0F, 10.0F);
    kb::scene::UIComponentSet scrollContentComponents = content;
    scrollContentComponents.rectTransform = Rect(0.0F, 0.0F, 100.0F, 100.0F);
    const kb::scene::SceneObject scrollContent = AddUI(scene, scrollView, scrollContentComponents);
    kb::scene::UIComponentSet switcherComponents = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::WidgetSwitcher);
    switcherComponents.rectTransform = Rect(0.0F, 160.0F, 100.0F, 20.0F);
    switcherComponents.selectable.emplace();
    const kb::scene::SceneObject switcher = AddUI(scene, canvas, switcherComponents);
    const kb::scene::SceneObject firstPage = AddUI(scene, switcher, content);
    const kb::scene::SceneObject secondPage = AddUI(scene, switcher, content);

    kb::scene::SceneUIInput pointer;
    pointer.pointerAvailable = true;
    pointer.pointerPosition = {10.0F, 10.0F};
    kb::tests::Require(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F), "Initial interaction frame must build");
    pointer.primaryDown = true;
    kb::tests::Require(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F), "Toggle press must update");
    pointer.primaryDown = false;
    kb::tests::Require(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F), "Toggle release must update");
    kb::tests::Require(scene.Components().UI().TryGet<kb::scene::UIToggle>(toggle.Entity())->toggled,
        "A clicked toggle must mutate its canonical ECS component");
    kb::tests::Require(FindElement(scene.UI().Frame(), toggle.Entity())->toggle->toggled,
        "The derived frame must expose the canonical toggle state to presentation consumers");
    kb::tests::Require(HasEvent(scene.UI().Events(), kb::scene::SceneUIEventType::Changed, toggle.Entity()) &&
        HasEvent(scene.UI().Events(), kb::scene::SceneUIEventType::Clicked, toggle.Entity()),
        "A clicked toggle must emit changed and clicked events");

    pointer.pointerPosition = {50.0F, 35.0F};
    pointer.primaryDown = true;
    kb::tests::Require(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F), "Slider drag must update");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Components().UI().TryGet<kb::scene::UISlider>(slider.Entity())->value, 0.5F),
        "Slider drag must resolve its value from the pointer");
    kb::tests::Require(kb::tests::NearlyEqual(FindElement(scene.UI().Frame(), slider.Entity())->slider->value, 0.5F),
        "The derived frame must expose the canonical slider value to presentation consumers");
    pointer.primaryDown = false;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));

    pointer.pointerPosition = {75.0F, 55.0F};
    pointer.primaryDown = true;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    kb::tests::Require(kb::tests::NearlyEqual(scene.Components().UI().TryGet<kb::scene::UIScrollbar>(scrollbar.Entity())->value, 1.0F),
        "Scrollbar drag must keep its authored thumb size inside the track");
    kb::tests::Require(kb::tests::NearlyEqual(FindElement(scene.UI().Frame(), scrollbar.Entity())->scrollbar->size, 0.5F) &&
            kb::tests::NearlyEqual(FindElement(scene.UI().Frame(), scrollbar.Entity())->scrollbar->value, 1.0F),
        "The derived frame must expose the canonical scrollbar value and thumb size to presentation consumers");
    pointer.primaryDown = false;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));

    pointer.pointerPosition = {10.0F, 165.0F};
    pointer.primaryDown = true;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    pointer.primaryDown = false;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    kb::tests::Require(scene.Components().UI().TryGet<kb::scene::UIWidgetSwitcher>(switcher.Entity())->visibleChildIndex == 1U &&
        FindElement(scene.UI().Frame(), firstPage.Entity()) == nullptr && FindElement(scene.UI().Frame(), secondPage.Entity()) != nullptr,
        "Widget switcher activation must select exactly one authored child in the same frame");

    pointer.pointerPosition = {10.0F, 75.0F};
    pointer.primaryDown = true;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    pointer.primaryDown = false;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    kb::tests::Require(scene.UI().HasFocusedTextInput() && scene.UI().Focused() == inputField.Entity(),
        "Clicking an input field must expose focused text-input state");

    const std::array<char32_t, 3U> typed{U'a', U'b', U'\U0001F642'};
    kb::scene::SceneUIInput textInput;
    textInput.textInput = typed;
    kb::tests::Require(scene.UI().Update(200.0F, 220.0F, textInput, 0.016F), "Unicode text input must update");
    kb::tests::Require(kb::scene::UITextContent(*scene.Components().UI().TryGet<kb::scene::UIText>(inputField.Entity())) ==
        std::string_view{"ab\xF0\x9F\x99\x82"}, "Text input must encode Unicode scalars into canonical UTF-8 text");
    const kb::scene::SceneUIEvent* textChanged = FindEvent(scene.UI().Events(), kb::scene::SceneUIEventType::Changed,
        inputField.Entity());
    kb::tests::Require(textChanged != nullptr && kb::scene::SceneUIEventText(*textChanged) ==
        std::string_view{"ab\xF0\x9F\x99\x82"}, "Changed events must carry the current canonical text payload");

    const std::array<char32_t, 1U> backspace{U'\b'};
    textInput.textInput = backspace;
    textInput.backspaceDown = true;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, textInput, 0.016F));
    kb::tests::Require(kb::scene::UITextContent(*scene.Components().UI().TryGet<kb::scene::UIText>(inputField.Entity())) == "ab",
        "Backspace control text and key state must erase exactly one Unicode scalar");
    textInput = {};
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, textInput, 0.016F));
    const std::array<char32_t, 1U> appendC{U'c'};
    textInput.textInput = appendC;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, textInput, 0.016F));
    textInput = {};
    textInput.navigateLeft = true;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, textInput, 0.016F));
    textInput = {};
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, textInput, 0.016F));
    textInput.deleteDown = true;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, textInput, 0.016F));
    kb::tests::Require(kb::scene::UITextContent(*scene.Components().UI().TryGet<kb::scene::UIText>(inputField.Entity())) == "ab",
        "Delete must erase the Unicode scalar at the transient caret");

    const std::array<char32_t, 2U> controls{U'\t', U'\r'};
    textInput = {};
    textInput.textInput = controls;
    textInput.submitDown = true;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, textInput, 0.016F));
    kb::tests::Require(kb::scene::UITextContent(*scene.Components().UI().TryGet<kb::scene::UIText>(inputField.Entity())) == "ab" &&
        HasEvent(scene.UI().Events(), kb::scene::SceneUIEventType::Submitted, inputField.Entity()),
        "Tab and carriage return must not enter a single-line field, while Enter submits it");

    pointer = {};
    pointer.pointerAvailable = true;
    pointer.pointerPosition = {10.0F, 105.0F};
    pointer.scrollDelta = -1.0F;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    kb::tests::Require(kb::tests::NearlyEqual(scene.Components().UI().TryGet<kb::scene::UIScrollView>(scrollView.Entity())->scrollY, 24.0F),
        "Scroll view wheel input must mutate its canonical offset");
    kb::tests::Require(FindElement(scene.UI().Frame(), scrollContent.Entity())->rect.y < 100.0F,
        "Scroll view offset must move its content in the refreshed frame");

    pointer.scrollDelta = 0.0F;
    pointer.primaryDown = true;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    pointer.pointerPosition.y = 95.0F;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    const float draggedScroll = scene.Components().UI().TryGet<kb::scene::UIScrollView>(scrollView.Entity())->scrollY;
    pointer.primaryDown = false;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    kb::tests::Require(scene.Components().UI().TryGet<kb::scene::UIScrollView>(scrollView.Entity())->scrollY > draggedScroll,
        "An inertial scroll view must continue the canonical offset after a drag is released");

    kb::scene::UIScrollView* authoredScrollView =
        scene.Components().UI().TryGet<kb::scene::UIScrollView>(scrollView.Entity());
    authoredScrollView->scrollY = 75.0F;
    scene.Components().UI().MarkModified<kb::scene::UIScrollView>(scrollView.Entity());
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, {}, 0.016F));
    pointer.pointerPosition.y = 105.0F;
    pointer.primaryDown = true;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    pointer.pointerPosition.y = 95.0F;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    pointer.primaryDown = false;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    kb::tests::Require(kb::tests::NearlyEqual(authoredScrollView->scrollY, 80.0F),
        "Drag and inertia must stop at the measured content boundary");

    authoredScrollView->inertia = false;
    authoredScrollView->scrollY = 24.0F;
    scene.Components().UI().MarkModified<kb::scene::UIScrollView>(scrollView.Entity());
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, {}, 0.016F));
    pointer.pointerPosition.y = 105.0F;
    pointer.primaryDown = true;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    pointer.pointerPosition.y = 95.0F;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    const float nonInertialScroll = authoredScrollView->scrollY;
    pointer.primaryDown = false;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    kb::tests::Require(kb::tests::NearlyEqual(authoredScrollView->scrollY, nonInertialScroll),
        "A scroll view with inertia disabled must stop when the drag is released");

    pointer = {};
    pointer.pointerAvailable = true;
    pointer.pointerPosition = {10.0F, 105.0F};
    pointer.scrollDelta = -100.0F;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    kb::tests::Require(kb::tests::NearlyEqual(authoredScrollView->scrollY, 80.0F),
        "Wheel scrolling must clamp to the measured content boundary");
}

void TestProgressAndEventQueue() {
    kb::scene::Scene scene{kb::scene::SceneMode::PrefabPrivate};
    const kb::scene::SceneObject canvas = AddUI(scene, {}, CanvasComponents());
    kb::scene::UIComponentSet progressComponents =
        kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::ProgressBar);
    progressComponents.rectTransform = Rect(0.0F, 0.0F, 100.0F, 10.0F);
    progressComponents.progressBar->value = 0.25F;
    const kb::scene::SceneObject progress = AddUI(scene, canvas, progressComponents);
    kb::scene::UIComponentSet fillComponents;
    fillComponents.rectTransform = Rect(0.0F, 0.0F, 100.0F, 10.0F);
    fillComponents.border.emplace();
    fillComponents.border->backgroundColor = {0.2F, 0.8F, 0.3F, 1.0F};
    const kb::scene::SceneObject fill = AddUI(scene, progress, fillComponents);

    kb::scene::UIComponentSet buttonComponents =
        kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Button);
    buttonComponents.rectTransform = Rect(0.0F, 20.0F, 30.0F, 20.0F);
    const kb::scene::SceneObject button = AddUI(scene, canvas, buttonComponents);
    kb::tests::Require(scene.UI().Update(120.0F, 80.0F, {}, 0.016F), "Progress and event queue frame must build");
    const kb::scene::SceneUIFrameElement* fillFrame = FindElement(scene.UI().Frame(), fill.Entity());
    kb::tests::Require(fillFrame != nullptr && kb::tests::NearlyEqual(fillFrame->rect.width, 25.0F),
        "Progress value must resolve the first child into a visible fill width");
    kb::tests::Require(FindElement(scene.UI().Frame(), progress.Entity())->progressBar->value == 0.25F,
        "The derived frame must expose the canonical progress value to presentation consumers");

    kb::scene::UIProgressBar zeroRange{.minimum = 1.0F, .maximum = 1.0F, .value = 1.0F};
    scene.Components().UI().Set(progress.Entity(), zeroRange);
    kb::tests::Require(scene.UI().Update(120.0F, 80.0F, {}, 0.016F), "Zero-range progress frame must build");
    fillFrame = FindElement(scene.UI().Frame(), fill.Entity());
    kb::tests::Require(fillFrame != nullptr && kb::tests::NearlyEqual(fillFrame->rect.width, 100.0F),
        "A progress value at a zero-width range must deterministically resolve to complete");

    kb::tests::Require(scene.UI().SetFocus(button.Entity()), "Event queue fixture must focus its button");
    kb::tests::Require(scene.UI().Update(120.0F, 80.0F, {}, 0.016F),
        "A frame update after an external focus request must succeed");
    std::vector<kb::scene::SceneUIEvent> events = scene.UI().DrainEvents();
    const std::size_t focusedCount = static_cast<std::size_t>(std::ranges::count_if(events,
        [&](const kb::scene::SceneUIEvent& event) {
            return event.type == kb::scene::SceneUIEventType::Focused && event.entity == button.Entity();
        }));
    kb::tests::Require(focusedCount == 1U && scene.UI().DrainEvents().empty(),
        "UI events must survive frame rebuilds and drain exactly once");
}

void TestAutomaticRuntimeInput() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject canvas = AddUI(scene, {}, CanvasComponents());
    kb::scene::UIComponentSet button = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Button);
    button.rectTransform = Rect(0.0F, 0.0F, 30.0F, 30.0F);
    const kb::scene::SceneObject target = AddUI(scene, canvas, button);
    kb::tests::Require(scene.UI().SetViewport(50.0F, 50.0F), "A valid fallback viewport must be accepted");
    kb::input::InputDeviceState& device = scene.Input().MutableDeviceState();
    device.SetHasFocus(true);
    device.SetPointerViewportExtent(100U, 100U);
    device.SetPointerPosition(10.0F, 10.0F);
    device.SetKeyDown(kb::input::InputKey::MouseLeft, true);
    static_cast<void>(scene.Runtime().Update(0.016F));
    kb::tests::Require(scene.UI().Pressed() == target.Entity() && kb::tests::NearlyEqual(scene.UI().Frame().viewportSize.x, 100.0F),
        "The runtime UI system must consume physical input and the device's canonical viewport extent after polling");
    device.SetKeyDown(kb::input::InputKey::MouseLeft, false);
    static_cast<void>(scene.Runtime().Update(0.016F));
    kb::tests::Require(HasEvent(scene.UI().Events(), kb::scene::SceneUIEventType::Clicked, target.Entity()),
        "The runtime UI system must emit release activation without a manual UI update call");
    kb::tests::Require(!scene.UI().SetViewport(std::nanf(""), 100.0F), "Invalid viewport geometry must fail closed");
}

void TestAuthoredDimensionsAndVisibility() {
    kb::scene::Scene scene;
    const auto canvas = AddUI(scene, {}, CanvasComponents());
    auto values = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Button);
    values.rectTransform->offsetMin = {12.0F, 15.0F};
    kb::tests::Require(kb::scene::WriteUIComponentProperty(values, kb::scene::UIComponentType::RectTransform,
        "sizeDelta.x", kb::scene::UIComponentPropertyValue{240.0F}) == kb::scene::UIComponentPropertyWriteResult::Succeeded &&
        values.rectTransform->offsetMax.x == 252.0F,
        "Editing UI width must preserve position and update the existing rectangle");
    const auto button = AddUI(scene, canvas, values);
    scene.Components().Visibility().Set(button.Entity(), {.mode = kb::scene::VisibilityMode::Hidden});
    kb::scene::SceneUIFrame frame;
    kb::tests::Require(kb::scene::SceneUIQueries{scene}.BuildFrame(640.0F, 360.0F, frame), "Hidden UI frame must build");
    const auto* hidden = FindElement(frame, button.Entity());
    kb::tests::Require(hidden != nullptr && hidden->effectiveOpacity == 0.0F && !hidden->hitTestable,
        "Hidden UI must neither render nor intercept input");
    scene.Components().Visibility().Set(button.Entity(), {.mode = kb::scene::VisibilityMode::Visible});
    kb::tests::Require(kb::scene::SceneUIQueries{scene}.BuildFrame(640.0F, 360.0F, frame), "Visible UI frame must build");
    const auto* visible = FindElement(frame, button.Entity());
    kb::tests::Require(visible != nullptr && visible->effectiveOpacity == 1.0F && visible->hitTestable &&
        kb::tests::NearlyEqual(visible->rect.width, 240.0F), "Restored UI must render at its authored width and accept input");
}

// A pad player has no pointer: the left stick and the D-pad have to walk focus, a focused slider has
// to take left/right as its value, and holding a direction on a long list has to keep stepping and
// keep the focused row scrolled into view. Driven through the device state the platform collector
// fills, so the stick threshold and the axis sign are part of what is checked.
void TestControllerNavigation() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject canvas = AddUI(scene, {}, CanvasComponents());
    kb::scene::UIComponentSet buttonComponents = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Button);
    buttonComponents.rectTransform = Rect(0.0F, 0.0F, 100.0F, 20.0F);
    const kb::scene::SceneObject button = AddUI(scene, canvas, buttonComponents);
    kb::scene::UIComponentSet sliderComponents = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Slider);
    sliderComponents.rectTransform = Rect(0.0F, 40.0F, 100.0F, 20.0F);
    const kb::scene::SceneObject slider = AddUI(scene, canvas, sliderComponents);

    // The list itself is not focusable; its rows are, which is how a settings list is authored.
    kb::scene::UIComponentSet listComponents;
    listComponents.rectTransform = Rect(0.0F, 80.0F, 100.0F, 60.0F);
    listComponents.mask.emplace();
    listComponents.scrollView.emplace();
    listComponents.scrollView->horizontal = false;
    listComponents.scrollView->inertia = false;
    const kb::scene::SceneObject list = AddUI(scene, canvas, listComponents);
    constexpr std::size_t kRowCount = 10U;
    std::array<kb::scene::SceneObject, kRowCount> rows{};
    for (std::size_t index = 0U; index < kRowCount; ++index) {
        kb::scene::UIComponentSet row;
        row.rectTransform = Rect(0.0F, 20.0F * static_cast<float>(index), 100.0F, 20.0F);
        row.selectable.emplace();
        rows[index] = AddUI(scene, list, row);
    }

    kb::input::InputDeviceState& device = scene.Input().MutableDeviceState();
    device.SetHasFocus(true);
    device.SetPointerViewportExtent(400U, 400U);
    device.SetPointerPosition(390.0F, 390.0F);
    const auto frame = [&](float deltaSeconds = 0.016F) {
        static_cast<void>(scene.Runtime().Update(deltaSeconds));
    };
    frame();
    kb::tests::Require(scene.UI().SetFocus(button.Entity()), "The button must accept focus");

    device.SetAnalog(kb::input::InputKey::GamepadLeftStickY, -0.3F);
    frame();
    kb::tests::Require(scene.UI().Focused() == button.Entity(),
        "A stick resting below the navigation threshold must not move focus");
    device.SetAnalog(kb::input::InputKey::GamepadLeftStickY, -0.9F);
    frame();
    device.SetAnalog(kb::input::InputKey::GamepadLeftStickY, 0.0F);
    frame();
    kb::tests::Require(scene.UI().Focused() == slider.Entity(),
        "Pulling the left stick down must move focus to the widget below");

    device.SetKeyDown(kb::input::InputKey::GamepadDPadRight, true);
    frame();
    device.SetKeyDown(kb::input::InputKey::GamepadDPadRight, false);
    frame();
    kb::tests::Require(scene.UI().Focused() == slider.Entity() &&
        kb::tests::NearlyEqual(scene.Components().UI().TryGet<kb::scene::UISlider>(slider.Entity())->value, 0.05F),
        "D-pad right on a focused slider must raise its value instead of moving focus");
    device.SetAnalog(kb::input::InputKey::GamepadLeftStickX, 1.0F);
    frame();
    device.SetAnalog(kb::input::InputKey::GamepadLeftStickX, 0.0F);
    frame();
    kb::tests::Require(kb::tests::NearlyEqual(scene.Components().UI().TryGet<kb::scene::UISlider>(slider.Entity())->value, 0.1F),
        "Leaning the stick right on a focused slider must raise its value too");
    kb::tests::Require(HasEvent(scene.UI().Events(), kb::scene::SceneUIEventType::Changed, slider.Entity()),
        "Adjusting a slider from the pad must report the change to game logic");

    // Hold D-pad down for 1.5 s: one step on the press, then the repeat schedule carries focus from
    // the slider through every row of the list.
    device.SetKeyDown(kb::input::InputKey::GamepadDPadDown, true);
    for (int step = 0; step < 30; ++step)
        frame(0.05F);
    device.SetKeyDown(kb::input::InputKey::GamepadDPadDown, false);
    frame();
    kb::tests::Require(scene.UI().Focused() == rows[kRowCount - 1U].Entity(),
        "Holding a navigation direction must keep stepping through a long list");
    kb::tests::Require(scene.Components().UI().TryGet<kb::scene::UIScrollView>(list.Entity())->scrollY > 0.0F,
        "Walking past the visible rows must scroll the list");
    const kb::scene::SceneUIFrameElement* lastRow = FindElement(scene.UI().Frame(), rows[kRowCount - 1U].Entity());
    const kb::scene::SceneUIFrameElement* viewport = FindElement(scene.UI().Frame(), list.Entity());
    kb::tests::Require(lastRow != nullptr && viewport != nullptr && lastRow->rect.y >= viewport->rect.y - 0.01F &&
        lastRow->rect.y + lastRow->rect.height <= viewport->rect.y + viewport->rect.height + 0.01F,
        "The focused row must end up inside the list's visible area");
    kb::tests::Require(scene.UI().HitTest({50.0F, lastRow->rect.y + 10.0F}) == rows[kRowCount - 1U].Entity(),
        "The scrolled-to row must be targetable where it is drawn");
}

// Explicit navigation links name other widgets. Every path that recreates a hierarchy - saving and
// loading a scene, an editor undo snapshot, a packaged game, a prefab placed twice - hands out new
// entity ids, so a link has to follow its target into the new copy rather than keep the old number.
void TestNavigationLinksSurviveHierarchyCopies() {
    kb::scene::Scene scene{kb::scene::SceneMode::PrefabPrivate};
    const kb::scene::SceneObject canvas = AddUI(scene, {}, CanvasComponents());
    kb::scene::UIComponentSet play = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Button);
    play.rectTransform = Rect(0.0F, 0.0F, 100.0F, 20.0F);
    const kb::scene::SceneObject playButton = AddUI(scene, canvas, play);
    play.rectTransform = Rect(0.0F, 100.0F, 100.0F, 20.0F);
    const kb::scene::SceneObject quitButton = AddUI(scene, canvas, play);
    kb::scene::UISelectable* playSelectable = scene.Components().UI().TryGet<kb::scene::UISelectable>(playButton.Entity());
    playSelectable->navigationMode = kb::scene::UINavigationMode::Explicit;
    playSelectable->navigationDown = quitButton.Entity().Id();
    scene.Components().UI().MarkModified<kb::scene::UISelectable>(playButton.Entity());

    const kb::scene::ScenePrefab captured = scene.Prefabs().Capture(canvas);
    kb::scene::ScenePrefabInstance first = scene.Prefabs().Instantiate(captured);
    kb::scene::ScenePrefabInstance second = scene.Prefabs().Instantiate(captured);
    kb::tests::Require(first.ObjectCount() == 3U && second.ObjectCount() == 3U, "The menu prefab must instantiate twice");
    for (const kb::scene::ScenePrefabInstance* instance : {&first, &second}) {
        const kb::scene::SceneEntity copiedPlay = instance->ObjectAt(1U).Entity();
        const kb::scene::SceneEntity copiedQuit = instance->ObjectAt(2U).Entity();
        const kb::scene::UISelectable* copied = scene.Components().UI().TryGet<kb::scene::UISelectable>(copiedPlay);
        kb::tests::Require(copied != nullptr && copied->navigationDown == copiedQuit.Id(),
            "A copied navigation link must point at the copy of its target in the same instance");
    }

    // Driven through the runtime: the explicit link, not geometry, decides where focus goes.
    kb::scene::Scene runtime{kb::scene::SceneMode::PrefabPrivate};
    kb::scene::ScenePrefabInstance menu = runtime.Prefabs().Instantiate(captured);
    kb::scene::SceneUIInput input;
    kb::tests::Require(runtime.UI().Update(400.0F, 400.0F, input, 0.016F), "Copied menu must build");
    kb::tests::Require(runtime.UI().SetFocus(menu.ObjectAt(1U).Entity()), "Copied play button must take focus");
    input.navigateDown = true;
    static_cast<void>(runtime.UI().Update(400.0F, 400.0F, input, 0.016F));
    kb::tests::Require(runtime.UI().Focused() == menu.ObjectAt(2U).Entity(),
        "Navigating from a copied widget must follow its explicit link to the copied target");

    // A link whose target was deleted is kept as data but never followed: explicit means explicit, so
    // focus stays put instead of guessing a neighbour.
    input.navigateDown = false;
    static_cast<void>(runtime.UI().Update(400.0F, 400.0F, input, 0.016F));
    kb::tests::Require(runtime.UI().SetFocus(menu.ObjectAt(1U).Entity()), "Play button must take focus again");
    runtime.Entities().Destroy(menu.ObjectAt(2U));
    input.navigateDown = true;
    static_cast<void>(runtime.UI().Update(400.0F, 400.0F, input, 0.016F));
    kb::tests::Require(runtime.UI().Focused() == menu.ObjectAt(1U).Entity(),
        "An explicit link to a deleted widget must leave focus where it is");

    // v34 files wrote live ids; they are not reinterpreted as stable node ids.
    std::vector<std::uint8_t> bytes;
    kb::scene::UIComponentSet legacy;
    legacy.rectTransform.emplace();
    legacy.selectable.emplace();
    legacy.selectable->navigationDown = 12345U;
    kb::scene::SceneAssetUIComponentCodec::Write(bytes, legacy);
    kb::scene::SceneAssetBinaryIO::ByteReader reader{bytes};
    kb::scene::UIComponentSet decoded;
    kb::tests::Require(kb::scene::SceneAssetUIComponentCodec::Read(reader, 34U, decoded) &&
        decoded.selectable->navigationDown == 0U,
        "A v34 navigation link written as a live entity id must load as no link");
}

[[nodiscard]] SceneEntity FindNamed(const kb::scene::Scene& scene, SceneEntity root, std::string_view name) {
    std::vector<SceneEntity> pending{root};
    while (!pending.empty()) {
        const SceneEntity entity = pending.back();
        pending.pop_back();
        if (scene.Entities().Name(entity) == name) return entity;
        for (std::size_t index = 0U; index < scene.Hierarchy().ChildCount(entity); ++index)
            pending.push_back(scene.Hierarchy().ChildAt(entity, index));
    }
    return {};
}

[[nodiscard]] std::string_view ShownText(const kb::scene::SceneUIFrame& frame, SceneEntity entity) {
    const auto* element = FindElement(frame, entity);
    return element != nullptr && element->text.has_value() ? kb::scene::UITextContent(*element->text) : std::string_view{};
}

[[nodiscard]] kb::math::Vec2 Center(const kb::scene::SceneUIFrame& frame, SceneEntity entity) {
    const auto* element = FindElement(frame, entity);
    return element != nullptr ? kb::math::Vec2{element->rect.x + element->rect.width * 0.5F, element->rect.y + element->rect.height * 0.5F}
                              : kb::math::Vec2{-1.0F, -1.0F};
}

struct DropdownFixture {
    kb::scene::Scene scene{kb::scene::SceneMode::PrefabPrivate};
    kb::scene::SceneObject canvas;
    SceneEntity dropdown;
    kb::scene::SceneUIInput input;

    explicit DropdownFixture(std::span<const std::string_view> labels, float top = 0.0F) {
        canvas = AddUI(scene, {}, CanvasComponents());
        dropdown = kb::scene::CreateUIDropdownHierarchy(scene, canvas, "Quality");
        kb::scene::SceneUIComponents ui = scene.Components().UI();
        ui.Set(dropdown, Rect(0.0F, top, 160.0F, 30.0F));
        kb::scene::UIDropdown value = *ui.TryGet<kb::scene::UIDropdown>(dropdown);
        value.optionCount = static_cast<std::uint32_t>(labels.size());
        for (std::size_t index = 0U; index < labels.size(); ++index)
            kb::tests::Require(kb::scene::SetUIDropdownOptionText(value.options[index], labels[index]), "Label must fit");
        ui.Set(dropdown, value);
        input.pointerAvailable = true;
        input.pointerPosition = {390.0F, 390.0F};
        Step();
    }
    void Step(float deltaSeconds = 0.016F) {
        kb::tests::Require(scene.UI().Update(400.0F, 400.0F, input, deltaSeconds), "Dropdown frame must build");
    }
    void Click(kb::math::Vec2 position) {
        input.pointerAvailable = true;
        input.pointerPosition = position;
        input.primaryDown = true;
        Step();
        input.primaryDown = false;
        Step();
    }
    void Pulse(bool kb::scene::SceneUIInput::*field) {
        input.*field = true;
        Step();
        input.*field = false;
        Step();
    }
    [[nodiscard]] SceneEntity List() const { return FindNamed(scene, dropdown, "Dropdown List"); }
    [[nodiscard]] SceneEntity Item(std::size_t index, std::string_view label) const {
        return FindNamed(scene, dropdown, "Item " + std::to_string(index) + ": " + std::string{label});
    }
    [[nodiscard]] std::uint32_t Value() const { return scene.Components().UI().TryGet<kb::scene::UIDropdown>(dropdown)->value; }
    void FinishFade() { for (int step = 0; step < 20; ++step) Step(0.05F); }
};

// The dropdown opens a list cloned from its template: one item per option cloned from the template's
// item, with the option's label, the chosen one switched on and focused; a blocker under the list closes
// it. Choosing an item sets the value, reports it with the label, fades the list out and destroys it.
void TestDropdownListFromTemplate() {
    constexpr std::array<std::string_view, 5U> kLabels{"Low", "Medium", "High", "Ultra", "Custom"};
    DropdownFixture fixture{kLabels};
    auto& scene = fixture.scene;
    const SceneEntity label = FindNamed(scene, fixture.dropdown, "Label");
    const SceneEntity templateEntity = FindNamed(scene, fixture.dropdown, "Template");
    kb::tests::Require(ShownText(scene.UI().Frame(), label) == "Low", "The caption must show the chosen option");
    const auto* templateElement = FindElement(scene.UI().Frame(), templateEntity);
    kb::tests::Require(templateElement != nullptr && templateElement->effectiveOpacity == 0.0F && !templateElement->hitTestable,
        "The template must stay hidden and untouchable");
    kb::tests::Require(!fixture.List().IsValid(), "No list exists before the dropdown is opened");

    fixture.Click(Center(scene.UI().Frame(), fixture.dropdown));
    const SceneEntity list = fixture.List();
    kb::tests::Require(list.IsValid() && scene.Components().UI().Has<kb::scene::UICanvas>(list),
        "Opening must clone the template as a list in its own canvas");
    for (std::size_t index = 0U; index < kLabels.size(); ++index) {
        const SceneEntity item = fixture.Item(index, kLabels[index]);
        kb::tests::Require(item.IsValid() && ShownText(scene.UI().Frame(), FindNamed(scene, item, "Item Label")) == kLabels[index],
            "Every option must become an item cloned from the template with its label");
        kb::tests::Require(scene.Components().UI().TryGet<kb::scene::UIToggle>(item)->toggled == (index == 0U),
            "Only the chosen option's item may be switched on");
        const auto* checkmark = FindElement(scene.UI().Frame(), FindNamed(scene, item, "Item Checkmark"));
        kb::tests::Require(checkmark != nullptr, "Item checkmark must be laid out");
    }
    kb::tests::Require(scene.UI().Focused() == fixture.Item(0U, "Low"), "Opening must focus the chosen item");
    kb::tests::Require(FindNamed(scene, fixture.canvas.Entity(), "Blocker").IsValid(), "Opening must place a blocker under the list");
    const auto* listElement = FindElement(scene.UI().Frame(), list);
    const auto* controlElement = FindElement(scene.UI().Frame(), fixture.dropdown);
    kb::tests::Require(listElement->rect.y >= controlElement->rect.y + controlElement->rect.height - 0.01F,
        "A list with room below opens below the dropdown");
    kb::tests::Require(listElement->rect.height < 150.0F - 0.01F, "A list taller than its items shrinks to fit them");
    fixture.FinishFade();
    kb::tests::Require(FindElement(scene.UI().Frame(), list)->effectiveOpacity > 0.99F, "The list must fade in");

    // Hovering an item tints its background, the item's target graphic.
    const SceneEntity medium = fixture.Item(1U, "Medium");
    fixture.input.pointerPosition = Center(scene.UI().Frame(), medium);
    fixture.Step();
    fixture.Step(0.2F);
    const auto* background = FindElement(scene.UI().Frame(), FindNamed(scene, medium, "Item Background"));
    kb::tests::Require(background != nullptr && background->interactionTint.b > background->interactionTint.r,
        "A hovered item must tint its background with the highlight colour");
    const auto* offCheck = FindElement(scene.UI().Frame(), FindNamed(scene, medium, "Item Checkmark"));
    kb::tests::Require(offCheck->effectiveOpacity == 0.0F, "An item that is off must hide its checkmark");

    fixture.Click(Center(scene.UI().Frame(), medium));
    kb::tests::Require(fixture.Value() == 1U, "Clicking an item must choose its option");
    const kb::scene::SceneUIEvent* changed = FindEvent(scene.UI().Events(), kb::scene::SceneUIEventType::Changed, fixture.dropdown);
    kb::tests::Require(changed != nullptr && kb::tests::NearlyEqual(changed->value, 1.0F) &&
        kb::scene::SceneUIEventText(*changed) == "Medium", "Choosing must report the value and the label on the dropdown");
    kb::tests::Require(!FindNamed(scene, fixture.canvas.Entity(), "Blocker").IsValid(), "Choosing must remove the blocker at once");
    fixture.FinishFade();
    kb::tests::Require(!fixture.List().IsValid(), "The list must be destroyed once it has faded out");
    kb::tests::Require(ShownText(scene.UI().Frame(), label) == "Medium" && scene.UI().Focused() == fixture.dropdown,
        "The caption must show the new choice and focus must return to the dropdown");

    // Keyboard and pad: submit opens, down walks the items in order, submit chooses, cancel closes.
    fixture.input.pointerAvailable = false;
    fixture.Pulse(&kb::scene::SceneUIInput::submitDown);
    kb::tests::Require(scene.UI().Focused() == fixture.Item(1U, "Medium"), "Submit must open the list onto the chosen item");
    fixture.Pulse(&kb::scene::SceneUIInput::navigateDown);
    fixture.Pulse(&kb::scene::SceneUIInput::navigateDown);
    kb::tests::Require(scene.UI().Focused() == fixture.Item(3U, "Ultra"), "Navigation must walk the items in option order");
    fixture.Pulse(&kb::scene::SceneUIInput::submitDown);
    fixture.FinishFade();
    kb::tests::Require(fixture.Value() == 3U && !fixture.List().IsValid(), "Submit on an item must choose it and close the list");
    fixture.Pulse(&kb::scene::SceneUIInput::submitDown);
    fixture.Pulse(&kb::scene::SceneUIInput::navigateUp);
    fixture.Pulse(&kb::scene::SceneUIInput::cancelDown);
    fixture.FinishFade();
    kb::tests::Require(fixture.Value() == 3U && !fixture.List().IsValid() && scene.UI().Focused() == fixture.dropdown,
        "Cancel must close the list without changing the value");

    // A press outside the list lands on the blocker and closes it without choosing.
    fixture.Click(Center(scene.UI().Frame(), fixture.dropdown));
    fixture.Click({390.0F, 390.0F});
    fixture.FinishFade();
    kb::tests::Require(fixture.Value() == 3U && !fixture.List().IsValid(), "A press outside must close the list without choosing");
}

// A long list scrolls inside the template's viewport: the linked scrollbar appears, and walking to the last
// item scrolls it into view. A dropdown near the bottom opens its list upwards.
void TestDropdownScrollingAndPlacement() {
    constexpr std::array<std::string_view, 12U> kLabels{"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"};
    DropdownFixture fixture{kLabels};
    auto& scene = fixture.scene;
    fixture.input.pointerAvailable = false;
    const SceneEntity focusStart = fixture.dropdown;
    kb::tests::Require(scene.UI().SetFocus(focusStart), "Dropdown must take focus");
    fixture.Pulse(&kb::scene::SceneUIInput::submitDown);
    for (int step = 0; step < 11; ++step) fixture.Pulse(&kb::scene::SceneUIInput::navigateDown);
    const SceneEntity last = fixture.Item(11U, "12");
    kb::tests::Require(scene.UI().Focused() == last, "Navigation must reach the last item of a long list");
    const SceneEntity list = fixture.List();
    const SceneEntity viewport = FindNamed(scene, list, "Viewport");
    const SceneEntity scrollbar = FindNamed(scene, list, "Scrollbar");
    kb::tests::Require(scene.Components().UI().TryGet<kb::scene::UIScrollView>(viewport)->scrollY > 0.0F,
        "Reaching an item below the viewport must scroll the list");
    const auto* lastElement = FindElement(scene.UI().Frame(), last);
    const auto* viewportElement = FindElement(scene.UI().Frame(), viewport);
    kb::tests::Require(lastElement->rect.y + lastElement->rect.height <= viewportElement->rect.y + viewportElement->rect.height + 0.01F,
        "The focused item must end up inside the viewport");
    const auto* bar = FindElement(scene.UI().Frame(), scrollbar);
    kb::tests::Require(bar != nullptr && bar->effectiveOpacity > 0.0F && bar->scrollbar->value > 0.9F && bar->scrollbar->size < 1.0F,
        "A list that does not fit must show its scrollbar at the scrolled position");

    constexpr std::array<std::string_view, 3U> kShort{"A", "B", "C"};
    DropdownFixture bottom{kShort, 360.0F};
    bottom.Click(Center(bottom.scene.UI().Frame(), bottom.dropdown));
    const auto* upward = FindElement(bottom.scene.UI().Frame(), bottom.List());
    const auto* control = FindElement(bottom.scene.UI().Frame(), bottom.dropdown);
    kb::tests::Require(upward != nullptr && upward->rect.y + upward->rect.height <= control->rect.y + 0.01F && upward->rect.y >= 0.0F,
        "A list with no room below must open upwards and stay on screen");
    const auto* shortScrollbar = FindElement(bottom.scene.UI().Frame(), FindNamed(bottom.scene, bottom.List(), "Scrollbar"));
    kb::tests::Require(shortScrollbar != nullptr && shortScrollbar->effectiveOpacity == 0.0F,
        "A list whose items fit must hide its scrollbar");
}

// A dropdown keeps its widgets and options across a save and a prefab copy: its references follow into
// the copy. Payloads written by v35-v37 still load, keeping labels, images and the chosen index.
void TestDropdownPersistenceAndOptionEdits() {
    constexpr std::array<std::string_view, 2U> kLabels{"Windowed", "Fullscreen"};
    DropdownFixture fixture{kLabels};
    auto& scene = fixture.scene;
    const kb::scene::ScenePrefab captured = scene.Prefabs().Capture(scene.Entities().Object(fixture.dropdown));
    kb::scene::ScenePrefabInstance copy = scene.Prefabs().Instantiate(captured);
    const SceneEntity copied = copy.RootObject().Entity();
    const kb::scene::UIDropdown* copiedDropdown = scene.Components().UI().TryGet<kb::scene::UIDropdown>(copied);
    kb::tests::Require(copiedDropdown != nullptr && copiedDropdown->templateEntity == FindNamed(scene, copied, "Template").Id() &&
        copiedDropdown->captionText == FindNamed(scene, copied, "Label").Id() &&
        copiedDropdown->itemText == FindNamed(scene, copied, "Item Label").Id(),
        "A copied dropdown must point at its own template, caption and item label");
    const kb::scene::UIToggle* copiedToggle = scene.Components().UI().TryGet<kb::scene::UIToggle>(FindNamed(scene, copied, "Item"));
    kb::tests::Require(copiedToggle->graphic == FindNamed(scene, copied, "Item Checkmark").Id(),
        "A copied item must point at its own checkmark");

    kb::scene::UIComponentSet authored;
    authored.rectTransform.emplace();
    authored.dropdown = *copiedDropdown;
    authored.dropdown->options[1].imageAssetId = 77U;
    authored.dropdown->value = 1U;
    std::vector<std::uint8_t> bytes;
    kb::scene::SceneAssetUIComponentCodec::Write(bytes, authored);
    kb::scene::SceneAssetBinaryIO::ByteReader reader{bytes};
    kb::scene::UIComponentSet decoded;
    kb::tests::Require(kb::scene::SceneAssetUIComponentCodec::Read(reader, 38U, decoded) && reader.Exhausted() &&
        decoded.dropdown->templateEntity == copiedDropdown->templateEntity && decoded.dropdown->value == 1U &&
        decoded.dropdown->options[1].imageAssetId == 77U &&
        kb::scene::UIDropdownOptionText(decoded.dropdown->options[1]) == "Fullscreen",
        "A v38 dropdown must round-trip its widgets, value and options");

    // v37 layout: index, visible rows, options with image and content, then a font and four colours.
    std::vector<std::uint8_t> legacy;
    kb::scene::SceneAssetBinaryIO::WriteUInt64(legacy, 1ULL << static_cast<unsigned>(kb::scene::UIComponentType::Dropdown));
    kb::scene::SceneAssetBinaryIO::WriteUInt32(legacy, 1U);
    kb::scene::SceneAssetBinaryIO::WriteUInt32(legacy, 0U);
    kb::scene::SceneAssetBinaryIO::WriteUInt32(legacy, 2U);
    for (const std::string_view text : kLabels) {
        kb::scene::SceneAssetBinaryIO::WriteString(legacy, text);
        kb::scene::SceneAssetBinaryIO::WriteUInt64(legacy, 5U);
        kb::scene::SceneAssetBinaryIO::WriteUInt64(legacy, 0U);
    }
    kb::scene::SceneAssetBinaryIO::WriteUInt64(legacy, 0U);
    kb::scene::SceneAssetBinaryIO::WriteFloat(legacy, 16.0F);
    for (int color = 0; color < 16; ++color) kb::scene::SceneAssetBinaryIO::WriteFloat(legacy, 1.0F);
    kb::scene::SceneAssetBinaryIO::ByteReader legacyReader{legacy};
    kb::scene::UIComponentSet legacyDecoded;
    kb::tests::Require(kb::scene::SceneAssetUIComponentCodec::Read(legacyReader, 37U, legacyDecoded) && legacyReader.Exhausted() &&
        legacyDecoded.dropdown->value == 1U && legacyDecoded.dropdown->optionCount == 2U &&
        legacyDecoded.dropdown->options[0].imageAssetId == 5U && legacyDecoded.dropdown->templateEntity == 0U,
        "A v37 dropdown must load its options and chosen index, without widgets to drive");

    kb::scene::UIDropdown edited = *copiedDropdown;
    edited.optionCount = 3U;
    static_cast<void>(kb::scene::SetUIDropdownOptionText(edited.options[2], "Borderless"));
    edited.value = 2U;
    kb::tests::Require(kb::scene::MoveUIDropdownOption(edited, 2U, 0U) && edited.value == 0U &&
        kb::scene::UIDropdownOptionText(edited.options[0]) == "Borderless", "Moving the chosen option must keep it chosen");
    kb::tests::Require(kb::scene::RemoveUIDropdownOption(edited, 1U) && edited.optionCount == 2U && edited.value == 0U,
        "Removing another option must keep the value");
    kb::tests::Require(kb::scene::RemoveUIDropdownOption(edited, 0U) && edited.optionCount == 1U && edited.value == 0U,
        "Removing the chosen option must choose a remaining one");
}

} // namespace

namespace kb::tests {

void RunSceneUITests() {
    TestAuthoredDimensionsAndVisibility();
    TestCatalogAndPresets();
    TestLayoutsAndFitters();
    TestHierarchicalTransformsAndMaskHitTesting();
    TestClippingGroupsSortingAndScaling();
    TestInteractionAndEditing();
    TestDropdownListFromTemplate();
    TestDropdownScrollingAndPlacement();
    TestDropdownPersistenceAndOptionEdits();
    TestNavigationLinksSurviveHierarchyCopies();
    TestProgressAndEventQueue();
    TestAutomaticRuntimeInput();
    TestControllerNavigation();
}

} // namespace kb::tests
