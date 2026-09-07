#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "engine/ui/UIComponentCatalog.hpp"
#include "engine/ui/UIComponentPropertyCatalog.hpp"
#include "engine/ui/UIComponentValidation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string_view>

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
    }
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
    const kb::scene::SceneObject dropdown = addPreset(kb::scene::UIComponentPreset::Dropdown, Rect(0.0F, 130.0F, 100.0F, 20.0F));
    const auto firstOption = AddUI(scene, dropdown, content);
    const auto secondOption = AddUI(scene, dropdown, content);
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

    pointer.pointerPosition = {10.0F, 135.0F};
    pointer.primaryDown = true;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    pointer.primaryDown = false;
    static_cast<void>(scene.UI().Update(200.0F, 220.0F, pointer, 0.016F));
    kb::tests::Require(scene.Components().UI().TryGet<kb::scene::UIDropdown>(dropdown.Entity())->selectedIndex == 1U,
        "Dropdown activation must advance the selected child index");
    kb::tests::Require(FindElement(scene.UI().Frame(), dropdown.Entity())->dropdown->selectedIndex == 1U,
        "The derived frame must expose the canonical dropdown selection to presentation consumers");
    kb::tests::Require(FindElement(scene.UI().Frame(), firstOption.Entity()) == nullptr &&
        FindElement(scene.UI().Frame(), secondOption.Entity()) != nullptr,
        "A dropdown must show only its selected option instead of overlapping all option labels");

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

} // namespace

namespace kb::tests {

void RunSceneUITests() {
    TestAuthoredDimensionsAndVisibility();
    TestCatalogAndPresets();
    TestLayoutsAndFitters();
    TestHierarchicalTransformsAndMaskHitTesting();
    TestClippingGroupsSortingAndScaling();
    TestInteractionAndEditing();
    TestProgressAndEventQueue();
    TestAutomaticRuntimeInput();
}

} // namespace kb::tests
