#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/localization/LocalizationCatalog.hpp"
#include "engine/localization/LocalizationCatalogIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneLocalization.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/scene/SceneTransforms.hpp"
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
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
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

void TestFrameWithoutCanvasTracksDynamicComponents() {
    kb::scene::Scene scene;
    const SceneEntity canvasEntity = scene.Entities().CreateEntity();
    const SceneEntity orphan = scene.Entities().CreateEntity();
    scene.Components().UI().Set(orphan, Rect(0.0F, 0.0F, 80.0F, 20.0F));

    kb::scene::SceneUIFrame frame;
    kb::tests::Require(kb::scene::SceneUIQueries{scene}.BuildFrame(640.0F, 360.0F, frame),
        "UI frame without a canvas must build");
    kb::tests::Require(frame.elements.empty() && frame.viewportSize.x == 640.0F &&
        frame.viewportSize.y == 360.0F && !frame.refusal.HasValue(),
        "Orphan UI components must not create frame elements");

    scene.Components().UI().Set(canvasEntity, Rect(0.0F, 0.0F, 640.0F, 360.0F));
    scene.Components().UI().Set(canvasEntity, kb::scene::UICanvas{});
    kb::tests::Require(kb::scene::SceneUIQueries{scene}.BuildFrame(640.0F, 360.0F, frame) &&
        FindElement(frame, canvasEntity) != nullptr,
        "Adding a canvas after an empty frame must create visible UI");

    scene.Components().UI().Remove<kb::scene::UICanvas>(canvasEntity);
    kb::tests::Require(kb::scene::SceneUIQueries{scene}.BuildFrame(640.0F, 360.0F, frame) &&
        frame.elements.empty() && !frame.refusal.HasValue(),
        "Removing the last canvas must clear a previously populated frame");
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

// Drives the UI a frame at a time with the pointer at a place, pressed or not.
struct PointerDriver {
    kb::scene::Scene& scene;
    float width = 400.0F;
    float height = 400.0F;
    kb::scene::SceneUIInput input{.pointerAvailable = true};

    void At(float x, float y, bool down, float deltaSeconds = 0.016F) {
        input.pointerPosition = {x, y};
        input.primaryDown = down;
        kb::tests::Require(scene.UI().Update(width, height, input, deltaSeconds), "A driven UI frame must build");
    }
    void Click(float x, float y) {
        At(x, y, false);
        At(x, y, true);
        At(x, y, false);
    }
};

// Radio buttons: toggles naming one group switch each other off and the chosen one stays chosen. A slider or
// progress bar positions the fill and handle widgets it names along its direction.
void TestRadioGroupsAndDrivenWidgets() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject canvas = AddUI(scene, {}, CanvasComponents());
    kb::scene::UIComponentSet groupComponents;
    groupComponents.rectTransform = Rect(0.0F, 0.0F, 300.0F, 40.0F);
    const kb::scene::SceneObject group = AddUI(scene, canvas, groupComponents);
    std::array<kb::scene::SceneObject, 3U> options{};
    for (std::size_t index = 0U; index < options.size(); ++index) {
        kb::scene::UIComponentSet toggle = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Toggle);
        toggle.rectTransform = Rect(100.0F * static_cast<float>(index), 0.0F, 90.0F, 40.0F);
        toggle.toggle->toggled = index == 0U;
        toggle.toggle->group = group.Entity().Id();
        options[index] = AddUI(scene, group, toggle);
    }
    PointerDriver pointer{scene};
    const auto on = [&](std::size_t index) {
        return scene.Components().UI().TryGet<kb::scene::UIToggle>(options[index].Entity())->toggled;
    };
    pointer.Click(150.0F, 20.0F);
    kb::tests::Require(!on(0U) && on(1U) && !on(2U), "Choosing a radio option must switch the rest of its group off");
    pointer.Click(150.0F, 20.0F);
    kb::tests::Require(on(1U), "Clicking the chosen radio option again must keep it chosen");
    scene.Components().UI().TryGet<kb::scene::UIToggle>(options[1U].Entity())->allowSwitchOff = true;
    pointer.Click(150.0F, 20.0F);
    kb::tests::Require(!on(1U), "A group that allows switching off must let the chosen option be cleared");

    kb::scene::UIComponentSet sliderComponents = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Slider);
    sliderComponents.rectTransform = Rect(0.0F, 100.0F, 200.0F, 20.0F);
    sliderComponents.slider->value = 0.25F;
    const kb::scene::SceneObject slider = AddUI(scene, canvas, sliderComponents);
    kb::scene::UIComponentSet part;
    part.rectTransform.emplace();
    part.rectTransform->anchorMax = {1.0F, 1.0F};
    part.rectTransform->offsetMin = {};
    part.rectTransform->offsetMax = {};
    part.border.emplace();
    const kb::scene::SceneObject fill = AddUI(scene, slider, part);
    part.rectTransform->offsetMin = {-5.0F, 0.0F};
    part.rectTransform->offsetMax = {5.0F, 0.0F};
    const kb::scene::SceneObject handle = AddUI(scene, slider, part);
    scene.Components().UI().TryGet<kb::scene::UISlider>(slider.Entity())->fillRect = fill.Entity().Id();
    scene.Components().UI().TryGet<kb::scene::UISlider>(slider.Entity())->handleRect = handle.Entity().Id();

    kb::scene::UIComponentSet progressComponents = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::ProgressBar);
    progressComponents.rectTransform = Rect(300.0F, 100.0F, 20.0F, 100.0F);
    progressComponents.progressBar->value = 0.75F;
    progressComponents.progressBar->direction = kb::scene::UIAxisDirection::BottomToTop;
    const kb::scene::SceneObject progress = AddUI(scene, canvas, progressComponents);
    part.rectTransform->offsetMin = {};
    part.rectTransform->offsetMax = {};
    const kb::scene::SceneObject progressFill = AddUI(scene, progress, part);
    scene.Components().UI().TryGet<kb::scene::UIProgressBar>(progress.Entity())->fillRect = progressFill.Entity().Id();
    pointer.At(390.0F, 390.0F, false);
    const kb::scene::SceneUIFrameElement* fillFrame = FindElement(scene.UI().Frame(), fill.Entity());
    const kb::scene::SceneUIFrameElement* handleFrame = FindElement(scene.UI().Frame(), handle.Entity());
    kb::tests::Require(fillFrame != nullptr && kb::tests::NearlyEqual(fillFrame->rect.x, 0.0F) &&
        kb::tests::NearlyEqual(fillFrame->rect.width, 50.0F), "A slider's fill must span from its start to the value");
    kb::tests::Require(handleFrame != nullptr && kb::tests::NearlyEqual(handleFrame->rect.x + handleFrame->rect.width * 0.5F, 50.0F),
        "A slider's handle must sit at the value");
    const kb::scene::SceneUIFrameElement* progressFrame = FindElement(scene.UI().Frame(), progressFill.Entity());
    kb::tests::Require(progressFrame != nullptr && kb::tests::NearlyEqual(progressFrame->rect.y, 125.0F) &&
        kb::tests::NearlyEqual(progressFrame->rect.height, 75.0F), "A bottom-to-top progress bar must fill up from its bottom edge");

    for (const kb::scene::UIComponentPreset preset :
         {kb::scene::UIComponentPreset::Slider, kb::scene::UIComponentPreset::ProgressBar, kb::scene::UIComponentPreset::ScrollView}) {
        std::vector<SceneEntity> parts;
        const SceneEntity root = kb::scene::CreateUIHierarchy(scene, preset, canvas,
            preset == kb::scene::UIComponentPreset::Slider ? "Created Slider" : "Created", &parts);
        kb::tests::Require(root.IsValid() && kb::scene::IsUIHierarchyPreset(preset) && parts.size() >= 2U && parts.front() == root,
            "A hierarchy preset must build its parts under a root");
    }
    const SceneEntity createdSlider = FindNamed(scene, canvas.Entity(), "Created Slider");
    const kb::scene::UISlider* wired = scene.Components().UI().TryGet<kb::scene::UISlider>(createdSlider);
    kb::tests::Require(wired != nullptr && wired->fillRect == FindNamed(scene, createdSlider, "Fill").Id() &&
        wired->handleRect == FindNamed(scene, createdSlider, "Handle").Id(), "A created slider must drive its own fill and handle");
    kb::tests::Require(!kb::scene::CreateUIHierarchy(scene, kb::scene::UIComponentPreset::Button, canvas, "Single").IsValid(),
        "A single-object preset must not build a hierarchy");
}

// How a selectable shows and takes presses: raycast padding, sprite swap, no transition, a tooltip after the
// pointer rests, the event target of its action, and presses on transparent image pixels.
void TestSelectablePresentationAndHits() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject canvas = AddUI(scene, {}, CanvasComponents());
    kb::scene::UIComponentSet buttonComponents = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Button);
    buttonComponents.rectTransform = Rect(0.0F, 0.0F, 100.0F, 40.0F);
    buttonComponents.image.emplace();
    buttonComponents.image->imageAssetId = 11U;
    buttonComponents.selectable->transition = kb::scene::UISelectableTransition::SpriteSwap;
    buttonComponents.selectable->highlightedImage = 12U;
    buttonComponents.selectable->raycastPadding = {0.0F, 0.0F, 20.0F, 0.0F};
    static_cast<void>(kb::scene::SetUITooltipText(*buttonComponents.selectable, "Save the game"));
    static_cast<void>(kb::scene::SetUIEventName(*buttonComponents.selectable, "SaveGame"));
    const kb::scene::SceneObject target = AddUI(scene, canvas, {});
    buttonComponents.selectable->eventTarget = target.Entity().Id();
    const kb::scene::SceneObject button = AddUI(scene, canvas, buttonComponents);
    kb::scene::UIComponentSet label;
    label.rectTransform = Rect(0.0F, 0.0F, 100.0F, 40.0F);
    label.text.emplace();
    label.text->fontAssetId = 9U;
    static_cast<void>(AddUI(scene, button, label));

    PointerDriver pointer{scene};
    pointer.At(110.0F, 20.0F, false);
    kb::tests::Require(scene.UI().Hovered() == button.Entity(), "Raycast padding must grow where a press lands");
    const kb::scene::SceneUIFrameElement* hovered = FindElement(scene.UI().Frame(), button.Entity());
    kb::tests::Require(hovered->image->imageAssetId == 12U && hovered->interactionTint.r == 1.0F && hovered->interactionTint.g == 1.0F,
        "Sprite swap must show the highlighted image untinted");
    for (int step = 0; step < 4; ++step)
        pointer.At(110.0F, 20.0F, false, 0.2F);
    const auto bubble = std::ranges::find_if(scene.UI().Frame().elements, [](const kb::scene::SceneUIFrameElement& element) {
        return element.tooltip && element.text.has_value();
    });
    kb::tests::Require(bubble != scene.UI().Frame().elements.end() &&
        kb::scene::UITextContent(*bubble->text) == "Save the game" && bubble->text->fontAssetId == 9U &&
        bubble->canvasSortingOrder == std::numeric_limits<std::int32_t>::max(),
        "A widget the pointer rests on must show its tooltip over everything, in the menu's font");
    static_cast<void>(scene.UI().DrainEvents());
    pointer.At(50.0F, 20.0F, true);
    kb::tests::Require(std::ranges::none_of(scene.UI().Frame().elements, &kb::scene::SceneUIFrameElement::tooltip),
        "Pressing must hide the tooltip");
    pointer.At(50.0F, 20.0F, false);
    const kb::scene::SceneUIEvent* clicked = FindEvent(scene.UI().Events(), kb::scene::SceneUIEventType::Clicked, button.Entity());
    kb::tests::Require(clicked != nullptr && clicked->actionTarget == target.Entity(),
        "A click must address its action to the selectable's event target");

    scene.Components().UI().TryGet<kb::scene::UISelectable>(button.Entity())->transition = kb::scene::UISelectableTransition::None;
    scene.Components().UI().TryGet<kb::scene::UIImage>(button.Entity())->alphaHitThreshold = 0.5F;
    scene.UI().PublishImageAlpha(11U, kb::scene::SceneUIImageAlpha{.width = 2U, .height = 1U, .alpha = {0U, 255U}});
    pointer.At(25.0F, 20.0F, true);
    kb::tests::Require(FindElement(scene.UI().Frame(), button.Entity())->interactionTint.r == 1.0F,
        "Without a transition a widget must not be tinted by its state");
    pointer.At(25.0F, 20.0F, false);
    kb::tests::Require(scene.UI().HitTest({25.0F, 20.0F}) != button.Entity() && scene.UI().HitTest({75.0F, 20.0F}) == button.Entity(),
        "A press must only land where the image is opaque enough");
}

// A draggable widget reports its drag and the widget it lands on, and a finished drag does not click.
void TestDragAndDrop() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject canvas = AddUI(scene, {}, CanvasComponents());
    kb::scene::UIComponentSet card = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Button);
    card.rectTransform = Rect(0.0F, 0.0F, 50.0F, 50.0F);
    card.selectable->draggable = true;
    const kb::scene::SceneObject item = AddUI(scene, canvas, card);
    card.rectTransform = Rect(200.0F, 0.0F, 50.0F, 50.0F);
    card.selectable->draggable = false;
    const kb::scene::SceneObject slot = AddUI(scene, canvas, card);
    PointerDriver pointer{scene};
    pointer.At(25.0F, 25.0F, false);
    pointer.At(25.0F, 25.0F, true);
    pointer.At(120.0F, 25.0F, true);
    kb::tests::Require(kb::scene::SceneUIQueries{scene}.Dragged() == item.Entity(), "Moving a pressed draggable widget must start a drag");
    pointer.At(225.0F, 25.0F, true);
    pointer.At(225.0F, 25.0F, false);
    const auto events = scene.UI().Events();
    const kb::scene::SceneUIEvent* ended = FindEvent(events, kb::scene::SceneUIEventType::DragEnded, item.Entity());
    const kb::scene::SceneUIEvent* dropped = FindEvent(events, kb::scene::SceneUIEventType::Dropped, slot.Entity());
    kb::tests::Require(HasEvent(events, kb::scene::SceneUIEventType::DragBegan, item.Entity()) &&
        HasEvent(events, kb::scene::SceneUIEventType::Dragged, item.Entity()) && ended != nullptr && ended->other == slot.Entity() &&
        dropped != nullptr && dropped->other == item.Entity(), "A drag must report its start, moves, end and drop target");
    kb::tests::Require(!HasEvent(events, kb::scene::SceneUIEventType::Clicked, item.Entity()) && !kb::scene::SceneUIQueries{scene}.Dragged().IsValid(),
        "A finished drag must not click the dragged widget");
}

// Content types filter typing, a password shows dots, and an empty field shows its placeholder.
void TestInputFieldContentTypes() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject canvas = AddUI(scene, {}, CanvasComponents());
    kb::scene::UIComponentSet fieldComponents = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::InputField);
    fieldComponents.rectTransform = Rect(0.0F, 0.0F, 200.0F, 30.0F);
    fieldComponents.inputField->contentType = kb::scene::UIInputContentType::IntegerNumber;
    static_cast<void>(kb::scene::SetUIInputPlaceholder(*fieldComponents.inputField, "Enter a number"));
    static_cast<void>(kb::scene::SetUITextContent(*fieldComponents.text, ""));
    const kb::scene::SceneObject field = AddUI(scene, canvas, fieldComponents);
    kb::scene::SceneUIInput input{};
    kb::tests::Require(scene.UI().Update(400.0F, 400.0F, input, 0.016F), "Input field frame must build");
    const kb::scene::SceneUIFrameElement* empty = FindElement(scene.UI().Frame(), field.Entity());
    kb::tests::Require(kb::scene::UITextContent(*empty->text) == "Enter a number" &&
        empty->text->color.a < fieldComponents.text->color.a, "An empty field must show its placeholder dimmed");
    kb::tests::Require(scene.UI().SetFocus(field.Entity()), "The field must take focus");
    const std::u32string typed = U"-1a2.3";
    input.textInput = typed;
    kb::tests::Require(scene.UI().Update(400.0F, 400.0F, input, 0.016F), "Typing frame must build");
    kb::tests::Require(kb::scene::UITextContent(*scene.Components().UI().TryGet<kb::scene::UIText>(field.Entity())) == "-123",
        "An integer field must keep only a leading sign and digits");
    scene.Components().UI().TryGet<kb::scene::UIInputField>(field.Entity())->contentType = kb::scene::UIInputContentType::Password;
    input.textInput = {};
    kb::tests::Require(scene.UI().Update(400.0F, 400.0F, input, 0.016F), "Password frame must build");
    const kb::scene::SceneUIFrameElement* password = FindElement(scene.UI().Frame(), field.Entity());
    kb::tests::Require(kb::scene::UITextContent(*password->text) == "****" && password->textCaretByteOffset == 4U,
        "A password must show one dot per character with the caret after them");
}

// Show/Hide animation on a Canvas Group, safe-area layout, a world-space canvas seen through the camera,
// and a second player's focus kept on their own canvas.
void TestCanvasPlacementAnimationAndPlayers() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject canvas = AddUI(scene, {}, CanvasComponents());
    kb::scene::UIComponentSet panelComponents = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Button);
    panelComponents.rectTransform = Rect(0.0F, 0.0F, 100.0F, 50.0F);
    panelComponents.canvasGroup.emplace();
    panelComponents.canvasGroup->transitionSeconds = 1.0F;
    panelComponents.canvasGroup->hiddenOffset = {0.0F, 100.0F};
    const kb::scene::SceneObject panel = AddUI(scene, canvas, panelComponents);
    kb::scene::SceneUIInput idle{};
    kb::tests::Require(scene.UI().Update(400.0F, 400.0F, idle, 0.016F), "Animated panel frame must build");
    scene.Components().UI().TryGet<kb::scene::UICanvasGroup>(panel.Entity())->visible = false;
    kb::tests::Require(scene.UI().Update(400.0F, 400.0F, idle, 0.5F), "Half-hidden frame must build");
    const kb::scene::SceneUIFrameElement* half = FindElement(scene.UI().Frame(), panel.Entity());
    kb::tests::Require(half != nullptr && kb::tests::NearlyEqual(half->effectiveOpacity, 0.5F) &&
        kb::tests::NearlyEqual(half->corners[0].y, 50.0F) && !half->hitTestable,
        "Hiding a group must fade and slide it over its transition and stop taking input at once");
    kb::tests::Require(scene.UI().Update(400.0F, 400.0F, idle, 0.6F), "Hidden frame must build");
    kb::tests::Require(FindElement(scene.UI().Frame(), panel.Entity())->effectiveOpacity == 0.0F, "A hidden group must end invisible");

    kb::tests::Require(scene.UI().SetSafeAreaInsets({40.0F, 10.0F, 0.0F, 0.0F}) && !scene.UI().SetSafeAreaInsets({-1.0F, 0.0F, 0.0F, 0.0F}),
        "Safe-area insets must be accepted when finite and non-negative only");
    kb::tests::Require(scene.UI().Update(400.0F, 400.0F, idle, 0.016F), "Safe-area frame must build");
    kb::tests::Require(kb::tests::NearlyEqual(FindElement(scene.UI().Frame(), panel.Entity())->rect.x, 40.0F),
        "A canvas fitting the safe area must lay out clear of the insets");
    scene.Components().UI().TryGet<kb::scene::UICanvas>(canvas.Entity())->respectSafeArea = false;
    kb::tests::Require(scene.UI().Update(400.0F, 400.0F, idle, 0.016F), "Full-screen frame must build");
    kb::tests::Require(kb::tests::NearlyEqual(FindElement(scene.UI().Frame(), panel.Entity())->rect.x, 0.0F),
        "A canvas not fitting the safe area must use the whole screen");

    kb::scene::UIComponentSet worldComponents = CanvasComponents();
    worldComponents.rectTransform = Rect(0.0F, 0.0F, 200.0F, 100.0F);
    worldComponents.canvas->renderMode = kb::scene::UICanvasRenderMode::WorldSpace;
    const kb::scene::SceneObject world = AddUI(scene, {}, worldComponents);
    scene.Transforms().TryGet(world.Entity())->localPosition = {0.0F, 0.0F, 1.0F};
    scene.Transforms().TryGet(world.Entity())->worldPosition = {0.0F, 0.0F, 1.0F};
    kb::scene::UIComponentSet sign = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Button);
    sign.rectTransform = Rect(0.0F, 0.0F, 100.0F, 50.0F);
    const kb::scene::SceneObject signButton = AddUI(scene, world, sign);
    kb::tests::Require(scene.UI().Update(400.0F, 400.0F, idle, 0.016F), "World canvas frame without a camera must build");
    kb::tests::Require(!FindElement(scene.UI().Frame(), signButton.Entity())->hitTestable,
        "A world-space canvas must not take input before a camera has been seen");
    kb::scene::SceneRenderVisibilityFrame camera{.frustumValid = true, .cameraValid = true, .viewportWidth = 400U, .viewportHeight = 400U};
    camera.view = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    camera.projection = camera.view;
    kb::scene::SceneRenderFeedback::Publish(scene, camera);
    kb::tests::Require(scene.UI().Update(400.0F, 400.0F, idle, 0.016F), "World canvas frame must build");
    const kb::scene::SceneUIFrameElement* projected = FindElement(scene.UI().Frame(), signButton.Entity());
    kb::tests::Require(projected != nullptr && kb::tests::NearlyEqual(projected->corners[0].x, 0.0F) &&
        kb::tests::NearlyEqual(projected->corners[0].y, 100.0F) && kb::tests::NearlyEqual(projected->corners[2].x, 200.0F) &&
        kb::tests::NearlyEqual(projected->canvasScale, 2.0F) && scene.UI().HitTest({100.0F, 150.0F}) == signButton.Entity(),
        "A world-space canvas must be placed on screen through the camera and take presses where it is drawn");

    kb::scene::UIComponentSet playerCanvas = CanvasComponents();
    playerCanvas.canvas->player = 1;
    playerCanvas.canvas->sortingOrder = 5;
    const kb::scene::SceneObject second = AddUI(scene, {}, playerCanvas);
    std::array<kb::scene::SceneObject, 2U> choices{};
    for (std::size_t index = 0U; index < choices.size(); ++index) {
        kb::scene::UIComponentSet choice = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Button);
        choice.rectTransform = Rect(300.0F, 200.0F + 40.0F * static_cast<float>(index), 80.0F, 30.0F);
        choices[index] = AddUI(scene, second, choice);
    }
    kb::scene::SceneUIInput pad{};
    const auto step = [&](bool down) {
        pad.otherPlayers[0].navigateDown = down;
        kb::tests::Require(scene.UI().Update(400.0F, 400.0F, pad, 0.016F), "Player navigation frame must build");
    };
    const SceneEntity sharedFocus = scene.UI().Focused();
    step(true);
    step(false);
    kb::tests::Require(kb::scene::SceneUIQueries{scene}.PlayerFocused(1U) == choices[0].Entity(),
        "A second player's first step must focus the first widget on their own canvas");
    step(true);
    step(false);
    kb::tests::Require(kb::scene::SceneUIQueries{scene}.PlayerFocused(1U) == choices[1].Entity() && scene.UI().Focused() == sharedFocus,
        "A second player's navigation must stay on their canvas and leave the shared focus alone");
}

// Elastic and snapping scroll views, a horizontal scrollbar, and a text showing its localization key.
void TestScrollMovementAndLocalizedText() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject canvas = AddUI(scene, {}, CanvasComponents());
    kb::scene::UIComponentSet viewComponents;
    viewComponents.rectTransform = Rect(0.0F, 0.0F, 100.0F, 50.0F);
    viewComponents.mask.emplace();
    viewComponents.scrollView.emplace();
    viewComponents.scrollView->horizontal = false;
    viewComponents.scrollView->inertia = false;
    viewComponents.scrollView->movementType = kb::scene::UIScrollMovement::Elastic;
    viewComponents.scrollView->snapToChildren = true;
    const kb::scene::SceneObject view = AddUI(scene, canvas, viewComponents);
    for (int row = 0; row < 5; ++row) {
        kb::scene::UIComponentSet rowComponents;
        rowComponents.rectTransform = Rect(0.0F, 20.0F * static_cast<float>(row), 100.0F, 20.0F);
        rowComponents.selectable.emplace();
        static_cast<void>(AddUI(scene, view, rowComponents));
    }
    const auto scrollY = [&] { return scene.Components().UI().TryGet<kb::scene::UIScrollView>(view.Entity())->scrollY; };
    PointerDriver pointer{scene};
    pointer.At(50.0F, 10.0F, false);
    pointer.At(50.0F, 10.0F, true);
    pointer.At(50.0F, 30.0F, true);
    kb::tests::Require(scrollY() < 0.0F, "An elastic view must follow a pull past its start");
    pointer.At(50.0F, 30.0F, false);
    for (int frame = 0; frame < 60; ++frame)
        pointer.At(50.0F, 30.0F, false, 0.05F);
    kb::tests::Require(scrollY() == 0.0F, "A released elastic view must spring back to its content");
    scene.Components().UI().TryGet<kb::scene::UIScrollView>(view.Entity())->scrollY = 10.0F;
    pointer.At(50.0F, 30.0F, false);
    pointer.At(50.0F, 30.0F, true);
    pointer.At(50.0F, 27.0F, true);
    pointer.At(50.0F, 27.0F, false);
    for (int frame = 0; frame < 60; ++frame)
        pointer.At(50.0F, 27.0F, false, 0.05F);
    kb::tests::Require(scrollY() == 20.0F, "A snapping view must settle with the nearest row at its top");

    kb::scene::UIComponentSet wideComponents;
    wideComponents.rectTransform = Rect(200.0F, 0.0F, 100.0F, 50.0F);
    wideComponents.scrollView.emplace();
    wideComponents.scrollView->vertical = false;
    wideComponents.scrollView->scrollX = 50.0F;
    const kb::scene::SceneObject wide = AddUI(scene, canvas, wideComponents);
    kb::scene::UIComponentSet contentComponents;
    contentComponents.rectTransform = Rect(0.0F, 0.0F, 200.0F, 50.0F);
    static_cast<void>(AddUI(scene, wide, contentComponents));
    kb::scene::UIComponentSet barComponents = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Scrollbar);
    barComponents.rectTransform = Rect(200.0F, 60.0F, 100.0F, 10.0F);
    const kb::scene::SceneObject bar = AddUI(scene, canvas, barComponents);
    scene.Components().UI().TryGet<kb::scene::UIScrollView>(wide.Entity())->horizontalScrollbar = bar.Entity().Id();
    pointer.At(390.0F, 390.0F, false);
    const kb::scene::UIScrollbar* synced = scene.Components().UI().TryGet<kb::scene::UIScrollbar>(bar.Entity());
    kb::tests::Require(kb::tests::NearlyEqual(synced->value, 0.5F) && kb::tests::NearlyEqual(synced->size, 0.5F),
        "A horizontal scrollbar must show the visible share and position of its view");

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb-ui-localized-text";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "Assets" / "Localization");
    const kb::localization::LocalizationCatalog catalog{
        .fallbackLanguage = "en",
        .languages = {{"en", {{"menu.play", {.text = "Play"}}}}, {"pl", {{"menu.play", {.text = "Graj"}}}}},
    };
    kb::tests::Require(kb::localization::LocalizationCatalogIO::Save(root / "Assets" / "Localization" / "Menu.kbloc", catalog) &&
        scene.Assets().MountProject(root) && scene.Assets().Discover() == 1U, "The menu catalog must be discovered");
    const auto* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Localization/Menu.kbloc");
    kb::tests::Require(metadata != nullptr && scene.Localization().SetCatalog(metadata->id.value), "The menu catalog must load");
    kb::scene::UIComponentSet textComponents;
    textComponents.rectTransform = Rect(0.0F, 100.0F, 100.0F, 20.0F);
    textComponents.text.emplace();
    static_cast<void>(kb::scene::SetUITextContent(*textComponents.text, "PLAY"));
    static_cast<void>(kb::scene::SetUITextLocalizationKey(*textComponents.text, "menu.play"));
    const kb::scene::SceneObject text = AddUI(scene, canvas, textComponents);
    pointer.At(390.0F, 390.0F, false);
    kb::tests::Require(ShownText(scene.UI().Frame(), text.Entity()) == "Play", "A localized text must show its key's translation");
    kb::tests::Require(scene.Localization().SetLanguage("pl"), "The menu language must switch");
    pointer.At(390.0F, 390.0F, false);
    kb::tests::Require(ShownText(scene.UI().Frame(), text.Entity()) == "Graj", "A localized text must follow a language change");
    std::filesystem::remove_all(root);
}

// Every v39 widget setting survives a save, and an older canvas keeps filling the whole screen.
void TestWidgetSettingsPersistence() {
    kb::scene::UIComponentSet authored;
    authored.rectTransform.emplace();
    authored.canvas.emplace();
    authored.canvas->renderMode = kb::scene::UICanvasRenderMode::WorldSpace;
    authored.canvas->pixelsPerUnit = 50.0F;
    authored.canvas->respectSafeArea = false;
    authored.canvas->player = 2;
    authored.canvasGroup.emplace();
    authored.canvasGroup->visible = false;
    authored.canvasGroup->transitionSeconds = 0.3F;
    authored.canvasGroup->hiddenOffset = {4.0F, 5.0F};
    authored.canvasGroup->hiddenScale = 0.8F;
    authored.image.emplace();
    authored.image->fillMethod = kb::scene::UIImageFillMethod::Radial360;
    authored.image->fillOrigin = kb::scene::UIImageFillOrigin::End;
    authored.image->fillAmount = 0.4F;
    authored.image->fillClockwise = false;
    authored.image->alphaHitThreshold = 0.2F;
    authored.text.emplace();
    static_cast<void>(kb::scene::SetUITextLocalizationKey(*authored.text, "hud.title"));
    authored.text->overflow = kb::scene::UITextOverflow::Ellipsis;
    authored.text->autoSize = true;
    authored.text->minFontSize = 9.0F;
    authored.text->maxLines = 2U;
    authored.text->characterSpacing = 1.5F;
    authored.mask.emplace();
    authored.mask->softness = 6.0F;
    authored.selectable.emplace();
    authored.selectable->transition = kb::scene::UISelectableTransition::SpriteSwap;
    authored.selectable->pressedImage = 21U;
    authored.selectable->raycastPadding = {1.0F, 2.0F, 3.0F, 4.0F};
    static_cast<void>(kb::scene::SetUITooltipText(*authored.selectable, "Hint"));
    authored.selectable->draggable = true;
    authored.selectable->eventTarget = 31U;
    authored.toggle.emplace();
    authored.toggle->group = 41U;
    authored.toggle->allowSwitchOff = true;
    authored.slider.emplace();
    authored.slider->fillRect = 51U;
    authored.slider->handleRect = 52U;
    authored.scrollView.emplace();
    authored.scrollView->horizontalScrollbar = 61U;
    authored.scrollView->movementType = kb::scene::UIScrollMovement::Elastic;
    authored.scrollView->elasticity = 0.25F;
    authored.scrollView->snapToChildren = true;
    authored.inputField.emplace();
    authored.inputField->contentType = kb::scene::UIInputContentType::Pin;
    static_cast<void>(kb::scene::SetUIInputPlaceholder(*authored.inputField, "PIN"));
    authored.progressBar.emplace();
    authored.progressBar->direction = kb::scene::UIAxisDirection::TopToBottom;
    authored.progressBar->fillRect = 71U;
    std::vector<std::uint8_t> bytes;
    kb::scene::SceneAssetUIComponentCodec::Write(bytes, authored);
    kb::scene::SceneAssetBinaryIO::ByteReader reader{bytes};
    kb::scene::UIComponentSet decoded;
    kb::tests::Require(kb::scene::SceneAssetUIComponentCodec::Read(reader, 39U, decoded) && reader.Exhausted(), "A v39 widget set must load");
    kb::tests::Require(decoded.canvas->renderMode == kb::scene::UICanvasRenderMode::WorldSpace && decoded.canvas->pixelsPerUnit == 50.0F &&
        !decoded.canvas->respectSafeArea && decoded.canvas->player == 2 && !decoded.canvasGroup->visible &&
        decoded.canvasGroup->transitionSeconds == 0.3F && decoded.canvasGroup->hiddenOffset.y == 5.0F && decoded.canvasGroup->hiddenScale == 0.8F &&
        decoded.image->fillMethod == kb::scene::UIImageFillMethod::Radial360 && decoded.image->fillOrigin == kb::scene::UIImageFillOrigin::End &&
        decoded.image->fillAmount == 0.4F && !decoded.image->fillClockwise && decoded.image->alphaHitThreshold == 0.2F &&
        kb::scene::UITextLocalizationKey(*decoded.text) == "hud.title" && decoded.text->overflow == kb::scene::UITextOverflow::Ellipsis &&
        decoded.text->autoSize && decoded.text->minFontSize == 9.0F && decoded.text->maxLines == 2U && decoded.text->characterSpacing == 1.5F &&
        decoded.mask->softness == 6.0F && decoded.selectable->transition == kb::scene::UISelectableTransition::SpriteSwap &&
        decoded.selectable->pressedImage == 21U && decoded.selectable->raycastPadding.bottom == 4.0F &&
        kb::scene::UITooltipText(*decoded.selectable) == "Hint" && decoded.selectable->draggable && decoded.selectable->eventTarget == 31U &&
        decoded.toggle->group == 41U && decoded.toggle->allowSwitchOff && decoded.slider->fillRect == 51U && decoded.slider->handleRect == 52U &&
        decoded.scrollView->horizontalScrollbar == 61U && decoded.scrollView->movementType == kb::scene::UIScrollMovement::Elastic &&
        decoded.scrollView->elasticity == 0.25F && decoded.scrollView->snapToChildren &&
        decoded.inputField->contentType == kb::scene::UIInputContentType::Pin && kb::scene::UIInputPlaceholder(*decoded.inputField) == "PIN" &&
        decoded.progressBar->direction == kb::scene::UIAxisDirection::TopToBottom && decoded.progressBar->fillRect == 71U,
        "Every v39 widget setting must round-trip");

    std::vector<std::uint8_t> legacy;
    kb::scene::SceneAssetBinaryIO::WriteUInt64(legacy, 1ULL << static_cast<unsigned>(kb::scene::UIComponentType::Canvas));
    kb::scene::SceneAssetBinaryIO::WriteInt32(legacy, 3);
    kb::scene::SceneAssetBinaryIO::WriteBool(legacy, false);
    kb::scene::SceneAssetBinaryIO::ByteReader legacyReader{legacy};
    kb::scene::UIComponentSet legacyDecoded;
    kb::tests::Require(kb::scene::SceneAssetUIComponentCodec::Read(legacyReader, 38U, legacyDecoded) && legacyReader.Exhausted() &&
        legacyDecoded.canvas->sortingOrder == 3 && !legacyDecoded.canvas->respectSafeArea &&
        legacyDecoded.canvas->renderMode == kb::scene::UICanvasRenderMode::ScreenSpace && legacyDecoded.canvas->player == -1,
        "A v38 canvas must keep filling the whole screen as a shared screen-space canvas");

    kb::tests::Require(!kb::scene::IsUIComponentValid(kb::scene::UIImage{.fillAmount = 1.5F}) &&
        !kb::scene::IsUIComponentValid(kb::scene::UICanvas{.player = 4}) &&
        !kb::scene::IsUIComponentValid(kb::scene::UIMask{.softness = -1.0F}),
        "Out-of-range widget settings must be rejected");
}

} // namespace

namespace kb::tests {

void RunSceneUITests() {
    TestAuthoredDimensionsAndVisibility();
    TestCatalogAndPresets();
    TestFrameWithoutCanvasTracksDynamicComponents();
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
    TestRadioGroupsAndDrivenWidgets();
    TestSelectablePresentationAndHits();
    TestDragAndDrop();
    TestInputFieldContentTypes();
    TestCanvasPlacementAnimationAndPlayers();
    TestScrollMovementAndLocalizedText();
    TestWidgetSettingsPersistence();
}

void RunSceneUIBuildFrameBenchmark() {
    constexpr std::size_t rootCount = 100'000U;
    kb::scene::Scene scene;
    for (std::size_t index = 0U; index < rootCount; ++index) {
        kb::tests::Require(scene.Entities().CreateEntity().IsValid(),
            "UI frame benchmark could not create a real scene root");
    }
    kb::tests::Require(scene.Entities().Count() == rootCount,
        "UI frame benchmark must count live scene entities");

    const kb::scene::SceneUIQueries ui{scene};
    kb::scene::SceneUIFrame frame;
    for (int warmup = 0; warmup < 2; ++warmup) {
        kb::tests::Require(ui.BuildFrame(640.0F, 360.0F, frame) && frame.elements.empty(),
            "UI frame benchmark expected an empty frame");
    }
    std::array<double, 5U> samples{};
    for (double& sample : samples) {
        const auto start = std::chrono::steady_clock::now();
        const bool built = ui.BuildFrame(640.0F, 360.0F, frame);
        sample = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        kb::tests::Require(built && frame.elements.empty(),
            "UI frame benchmark expected an empty frame");
    }
    std::ranges::sort(samples);
    std::cout << "ui_buildframe_roots=" << rootCount << " median_ms=" << samples[2] << '\n';
}

} // namespace kb::tests
