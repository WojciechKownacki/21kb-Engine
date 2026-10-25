#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/localization/LocalizationCatalog.hpp"
#include "engine/localization/LocalizationCatalogIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocument.hpp"
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
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/ui/SceneUIGroupTransitions.hpp"

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
    kb::tests::Require(scene.UI().Update(640.0F, 360.0F, {}, 0.016F) && scene.UI().Frame().elements.empty(),
        "UI update without a canvas must publish an empty frame");

    scene.Components().UI().Set(canvasEntity, Rect(0.0F, 0.0F, 640.0F, 360.0F));
    scene.Components().UI().Set(canvasEntity, kb::scene::UICanvas{});
    kb::tests::Require(kb::scene::SceneUIQueries{scene}.BuildFrame(640.0F, 360.0F, frame) &&
        FindElement(frame, canvasEntity) != nullptr,
        "Adding a canvas after an empty frame must create visible UI");
    kb::tests::Require(scene.UI().Update(640.0F, 360.0F, {}, 0.016F) &&
        FindElement(scene.UI().Frame(), canvasEntity) != nullptr,
        "UI update must observe a canvas added after an empty frame");

    scene.Components().UI().Remove<kb::scene::UICanvas>(canvasEntity);
    kb::tests::Require(kb::scene::SceneUIQueries{scene}.BuildFrame(640.0F, 360.0F, frame) &&
        frame.elements.empty() && !frame.refusal.HasValue(),
        "Removing the last canvas must clear a previously populated frame");
    kb::tests::Require(scene.UI().Update(640.0F, 360.0F, {}, 0.016F) && scene.UI().Frame().elements.empty(),
        "UI update must discard elements after removal of the last canvas");
}

void TestFrameTracksWidgetComponentLifetimes() {
    for (const bool mirrored : {true, false}) {
        kb::ecs::WorldConfig config;
        config.mirrorEntitiesToBackend = mirrored;
        config.mirrorNativeComponentChangesToBackend = mirrored;
        kb::scene::Scene scene{config};
        const auto canvas = AddUI(scene, {}, CanvasComponents());
        kb::scene::UIComponentSet components;
        components.rectTransform = Rect(0.0F, 0.0F, 80.0F, 20.0F);
        const auto first = AddUI(scene, canvas, components);
        static_cast<void>(kb::scene::SetUITextContent(components.text.emplace(), "Sibling"));
        const auto sibling = AddUI(scene, canvas, components);
        auto ui = scene.Components().UI();
        const kb::scene::SceneUIQueries queries{scene};
        kb::scene::SceneUIFrame frame;
        const auto build = [&] {
            kb::tests::Require(queries.BuildFrame(640.0F, 360.0F, frame),
                "UI frames must build after widget component changes");
        };
        build();
        kb::tests::Require(FindElement(frame, first.Entity()) != nullptr &&
            !FindElement(frame, first.Entity())->text &&
            FindElement(frame, sibling.Entity()) != nullptr &&
            kb::scene::UITextContent(*FindElement(frame, sibling.Entity())->text) == "Sibling",
            "Missing widget components must not reuse a sibling's payload");

        kb::scene::UIText text;
        static_cast<void>(kb::scene::SetUITextContent(text, "Added"));
        ui.Set(first.Entity(), text);
        build();
        kb::tests::Require(FindElement(frame, first.Entity())->text &&
            kb::scene::UITextContent(*FindElement(frame, first.Entity())->text) == "Added",
            "A frame must observe a component added after an earlier missing lookup");
        const auto previous = frame;
        static_cast<void>(kb::scene::SetUITextContent(*ui.TryGet<kb::scene::UIText>(first.Entity()), "Edited"));
        ui.Set(first.Entity(), Rect(10.0F, 0.0F, 120.0F, 20.0F));
        build();
        kb::tests::Require(kb::scene::UITextContent(*FindElement(frame, first.Entity())->text) == "Edited" &&
            kb::tests::NearlyEqual(FindElement(frame, first.Entity())->rect.width, 120.0F) &&
            kb::scene::UITextContent(*FindElement(previous, first.Entity())->text) == "Added",
            "Frame payloads must reflect current values while earlier snapshots remain owned");

        ui.Remove<kb::scene::UIText>(first.Entity());
        build();
        kb::tests::Require(!FindElement(frame, first.Entity())->text &&
            kb::scene::UITextContent(*FindElement(frame, sibling.Entity())->text) == "Sibling",
            "Removing a component must clear only its widget's frame payload");
        const auto retired = first.Entity();
        scene.Entities().Destroy(first);
        static_cast<void>(kb::scene::SetUITextContent(*components.text, "Replacement"));
        const auto replacement = AddUI(scene, canvas, components);
        build();
        kb::tests::Require(replacement.Entity() != retired && FindElement(frame, retired) == nullptr &&
            FindElement(frame, replacement.Entity()) != nullptr &&
            kb::scene::UITextContent(*FindElement(frame, replacement.Entity())->text) == "Replacement" &&
            kb::scene::UITextContent(*FindElement(frame, sibling.Entity())->text) == "Sibling",
            "Destroyed widgets must leave the frame without transferring payloads to new entities");
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

// Masks inside masks: a descendant is clipped by every mask above it, so only the overlap of the two takes presses,
// and the overlap follows the canvas when the screen it is scaled to changes size.
void TestNestedMasksClipToTheirOverlap() {
    kb::scene::Scene scene{kb::scene::SceneMode::PrefabPrivate};
    kb::scene::UIComponentSet canvasComponents = CanvasComponents();
    canvasComponents.canvasScaler->scaleMode = kb::scene::UICanvasScaleMode::ScaleWithScreenSize;
    canvasComponents.canvasScaler->referenceResolution = {200.0F, 200.0F};
    const kb::scene::SceneObject canvas = AddUI(scene, {}, canvasComponents);
    kb::scene::UIComponentSet outerComponents;
    outerComponents.rectTransform = Rect(20.0F, 20.0F, 100.0F, 100.0F);
    outerComponents.mask.emplace();
    const kb::scene::SceneObject outer = AddUI(scene, canvas, outerComponents);
    kb::scene::UIComponentSet innerComponents;
    innerComponents.rectTransform = Rect(60.0F, 60.0F, 100.0F, 100.0F);
    innerComponents.mask.emplace();
    const kb::scene::SceneObject inner = AddUI(scene, outer, innerComponents);
    kb::scene::UIComponentSet leafComponents;
    leafComponents.rectTransform = Rect(-100.0F, -100.0F, 400.0F, 400.0F);
    leafComponents.selectable.emplace();
    const kb::scene::SceneObject leaf = AddUI(scene, inner, leafComponents);

    // The masks overlap in the square from 80 to 120 in canvas units.
    for (const float scale : {1.0F, 2.0F, 0.5F}) {
        const float side = 200.0F * scale;
        kb::tests::Require(scene.UI().Update(side, side, {}, 0.016F), "The nested mask frame must build");
        const kb::scene::SceneUIFrameElement* leafFrame = FindElement(scene.UI().Frame(), leaf.Entity());
        kb::tests::Require(leafFrame != nullptr && leafFrame->clipQuads.size() == 2U,
            "A descendant of two masks must carry both mask quads");
        kb::tests::Require(scene.UI().HitTest({100.0F * scale, 100.0F * scale}) == leaf.Entity(),
            "A press inside both masks must reach the descendant");
        kb::tests::Require(!scene.UI().HitTest({50.0F * scale, 50.0F * scale}).IsValid(),
            "A press inside the outer mask only must not reach the descendant");
        kb::tests::Require(!scene.UI().HitTest({150.0F * scale, 150.0F * scale}).IsValid(),
            "A press inside the inner mask only must not reach the descendant");
        kb::tests::Require(!scene.UI().HitTest({121.0F * scale, 100.0F * scale}).IsValid() && scene.UI().HitTest({119.0F * scale, 100.0F * scale}) == leaf.Entity(),
            "The overlap must end exactly where the outer mask ends, at every scale");
    }

    // A turned mask inside a straight one: the corners of the turned mask's bounding box are inside the outer mask
    // and inside the box, but outside the turned quad, and take no press; its middle does.
    kb::scene::Scene turned{kb::scene::SceneMode::PrefabPrivate};
    const kb::scene::SceneObject turnedCanvas = AddUI(turned, {}, CanvasComponents());
    kb::scene::UIComponentSet straightComponents;
    straightComponents.rectTransform = Rect(0.0F, 0.0F, 200.0F, 200.0F);
    straightComponents.mask.emplace();
    const kb::scene::SceneObject straight = AddUI(turned, turnedCanvas, straightComponents);
    kb::scene::UIComponentSet diamondComponents;
    diamondComponents.rectTransform = Rect(50.0F, 50.0F, 100.0F, 100.0F);
    diamondComponents.rectTransform->rotationDegrees = 45.0F;
    diamondComponents.mask.emplace();
    const kb::scene::SceneObject diamond = AddUI(turned, straight, diamondComponents);
    kb::scene::UIComponentSet coverComponents;
    coverComponents.rectTransform = Rect(-100.0F, -100.0F, 400.0F, 400.0F);
    coverComponents.selectable.emplace();
    const kb::scene::SceneObject cover = AddUI(turned, diamond, coverComponents);
    kb::tests::Require(turned.UI().Update(200.0F, 200.0F, {}, 0.016F), "The turned mask frame must build");
    kb::tests::Require(turned.UI().HitTest({100.0F, 100.0F}) == cover.Entity(), "The middle of a turned mask takes presses");
    kb::tests::Require(!turned.UI().HitTest({160.0F, 160.0F}).IsValid() && !turned.UI().HitTest({40.0F, 160.0F}).IsValid(),
        "The corners of a turned mask's bounding box take no press");
    kb::tests::Require(turned.UI().HitTest({100.0F, 40.0F}) == cover.Entity() || turned.UI().HitTest({100.0F, 45.0F}) == cover.Entity(),
        "The tip of a turned mask takes presses");
}

template <typename Component, typename Mutation>
void RequireInvalidFramePayload(Mutation mutate, std::string_view componentName) {
    kb::scene::Scene scene{kb::scene::SceneMode::PrefabPrivate};
    const auto canvas = AddUI(scene, {}, CanvasComponents());
    kb::scene::UIComponentSet components;
    components.rectTransform = Rect(0.0F, 0.0F, 40.0F, 30.0F);
    const auto widget = AddUI(scene, canvas, components);
    auto ui = scene.Components().UI();
    ui.Set(widget.Entity(), Component{});
    const kb::scene::SceneUIQueries query{scene};
    kb::scene::SceneUIFrame frame;
    kb::tests::Require(query.BuildFrame(100.0F, 100.0F, frame), "Valid UI payload control must build");
    auto* payload = ui.TryGet<Component>(widget.Entity());
    kb::tests::Require(payload != nullptr, "UI payload mutation fixture must resolve its authored component");
    mutate(*payload);
    kb::tests::Require(!query.BuildFrame(100.0F, 100.0F, frame) && frame.refusal.entity == widget.Entity() &&
        frame.refusal.reason != nullptr && std::string_view{frame.refusal.reason}.starts_with(componentName),
        "Invalid UI payload must refuse the frame with the component and entity before layout uses it");
}

void TestInvalidFramePayloads() {
    RequireInvalidFramePayload<kb::scene::UIText>([](auto& value) { value.fontSize = std::numeric_limits<float>::quiet_NaN(); }, "UIText");
    RequireInvalidFramePayload<kb::scene::UIText>([](auto& value) { value.content.fill('x'); }, "UIText");
    RequireInvalidFramePayload<kb::scene::UIImage>([](auto& value) { value.alphaHitThreshold = std::numeric_limits<float>::quiet_NaN(); }, "UIImage");
    RequireInvalidFramePayload<kb::scene::UIDropdown>([](auto& value) { value.optionCount = kb::scene::UIDropdown::MaxOptions + 1U; }, "UIDropdown");
    RequireInvalidFramePayload<kb::scene::UIDropdown>([](auto& value) { value.optionCount = 1U; value.options[0].text.fill('x'); }, "UIDropdown");
    RequireInvalidFramePayload<kb::scene::UIInputField>([](auto& value) { value.contentType = static_cast<kb::scene::UIInputContentType>(255U); }, "UIInputField");
}

void TestContentSizeFitterModes() {
    kb::scene::Scene scene;
    const auto canvas = AddUI(scene, {}, CanvasComponents());
    kb::scene::UIComponentSet components;
    components.rectTransform = Rect(0.0F, 0.0F, 20.0F, 25.0F);
    components.layoutElement = kb::scene::UILayoutElement{.minimumWidth = 10.0F, .minimumHeight = 15.0F,
        .preferredWidth = 60.0F, .preferredHeight = 70.0F};
    const auto widget = AddUI(scene, canvas, components);
    struct Fit { kb::scene::UIFitMode mode; float width; float height; };
    constexpr std::array modes{Fit{kb::scene::UIFitMode::Unconstrained, 20.0F, 25.0F},
        Fit{kb::scene::UIFitMode::MinimumSize, 10.0F, 15.0F}, Fit{kb::scene::UIFitMode::PreferredSize, 60.0F, 70.0F}};
    for (const auto horizontal : modes) {
        for (const auto vertical : modes) {
            scene.Components().UI().Set(widget.Entity(), kb::scene::UIContentSizeFitter{
                .horizontalFit = horizontal.mode, .verticalFit = vertical.mode});
            kb::scene::SceneUIFrame frame;
            kb::tests::Require(kb::scene::SceneUIQueries{scene}.BuildFrame(100.0F, 100.0F, frame), "Every content size fitter mode must build");
            const auto* element = FindElement(frame, widget.Entity());
            kb::tests::Require(element != nullptr && kb::tests::NearlyEqual(element->rect.width, horizontal.width) &&
                kb::tests::NearlyEqual(element->rect.height, vertical.height),
                "Content size fitting must resolve minimum, preferred and unconstrained axes independently");
        }
    }
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

// Editing text by the characters a player sees: an accent typed after a letter joins it, Backspace and Delete remove a
// whole letter with its accents or a whole emoji with its skin tone or family, the arrow keys step over it, and the
// character limit counts them, not code points or bytes.
void TestInputFieldEditsGraphemeClusters() {
    kb::scene::Scene scene{kb::scene::SceneMode::PrefabPrivate};
    const kb::scene::SceneObject canvas = AddUI(scene, {}, CanvasComponents());
    kb::scene::UIComponentSet components = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::InputField);
    components.rectTransform = Rect(0.0F, 0.0F, 100.0F, 20.0F);
    const kb::scene::SceneObject field = AddUI(scene, canvas, components);
    kb::tests::Require(scene.UI().Update(200.0F, 100.0F, {}, 0.016F) && scene.UI().SetFocus(field.Entity()), "The input field must take focus");
    const auto content = [&] {
        return std::string{kb::scene::UITextContent(*scene.Components().UI().TryGet<kb::scene::UIText>(field.Entity()))};
    };
    const auto type = [&](std::u32string_view text) {
        kb::scene::SceneUIInput input;
        input.textInput = std::span<const char32_t>{text.data(), text.size()};
        static_cast<void>(scene.UI().Update(200.0F, 100.0F, input, 0.016F));
        static_cast<void>(scene.UI().Update(200.0F, 100.0F, {}, 0.016F));
    };
    const auto press = [&](auto setKey) {
        kb::scene::SceneUIInput input;
        setKey(input);
        static_cast<void>(scene.UI().Update(200.0F, 100.0F, input, 0.016F));
        static_cast<void>(scene.UI().Update(200.0F, 100.0F, {}, 0.016F));
    };
    const auto backspace = [&] { press([](kb::scene::SceneUIInput& input) { input.backspaceDown = true; }); };
    const auto left = [&] { press([](kb::scene::SceneUIInput& input) { input.navigateLeft = true; }); };
    const auto remove = [&] { press([](kb::scene::SceneUIInput& input) { input.deleteDown = true; }); };
    // UTF-8 of: e with an acute accent, a with a diaeresis, the diaeresis and a circumflex, a thumb with a skin tone,
    // a flag of two regional indicators.
    const std::string acuteE = "e\xCC\x81";
    const std::string diaeresisA = "a\xCC\x88";
    const std::string circumflex = "\xCC\x82";
    const std::string thumb = "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD";
    const std::string flag = "\xF0\x9F\x87\xB5\xF0\x9F\x87\xB1";

    type(U"e\u0301");
    kb::tests::Require(content() == acuteE, "A combining accent typed after a letter must join the text");
    backspace();
    kb::tests::Require(content().empty(), "Backspace must remove a letter together with its combining accent");

    type(U"x\U0001F468\u200D\U0001F469\u200D\U0001F467");
    backspace();
    kb::tests::Require(content() == "x", "Backspace must remove a whole joined family, not one of its members");

    type(U"\U0001F44D\U0001F3FD");
    left();
    type(U"y");
    kb::tests::Require(content() == "xy" + thumb, "An arrow key must step over an emoji and its skin tone together");
    remove();
    kb::tests::Require(content() == "xy", "Delete before an emoji must remove it with its skin tone");
    backspace();
    backspace();
    backspace();
    kb::tests::Require(content().empty(), "Backspace on empty text must leave it empty");

    // The limit counts characters as seen: two letters with accents fit a limit of two and a third does not.
    kb::scene::UIInputField limited = *scene.Components().UI().TryGet<kb::scene::UIInputField>(field.Entity());
    limited.characterLimit = 2U;
    scene.Components().UI().Set(field.Entity(), limited);
    type(U"e\u0301");
    type(U"a\u0308");
    kb::tests::Require(content() == acuteE + diaeresisA, "Accents must not use up places of the character limit");
    type(U"z");
    kb::tests::Require(content() == acuteE + diaeresisA, "A third character must be refused at a limit of two");
    type(U"\u0302");
    kb::tests::Require(content() == acuteE + diaeresisA + circumflex, "An accent typed at the limit must still join the last character");
    // A flag is one character: two regional indicators fit a limit of one.
    for (int guard = 0; guard < 8 && !content().empty(); ++guard) backspace();
    kb::tests::Require(content().empty(), "The field must be emptied before the flag check");
    limited.characterLimit = 1U;
    scene.Components().UI().Set(field.Entity(), limited);
    type(U"\U0001F1F5\U0001F1F1");
    kb::tests::Require(content() == flag, "A flag must count as one character");
    type(U"q");
    kb::tests::Require(content() == flag, "A second character must be refused at a limit of one");
}

// A menu laid out for a reference screen keeps every control on screen, whole, apart and clickable across screens of
// every shape and size: phone and tablet, portrait and landscape, ultra wide and 4K, with the scaler matching the
// width, the height or both, with and without a safe area. A text far too long for its place is cut to the text
// limit and the element keeps a finite, bounded size.
void TestResponsiveMenuKeepsEveryControlReachable() {
    struct Screen {
        float width;
        float height;
    };
    constexpr std::array<Screen, 8U> screens{ Screen{ 1920.0F, 1080.0F }, Screen{ 1280.0F, 720.0F }, Screen{ 800.0F, 600.0F },
        Screen{ 360.0F, 640.0F }, Screen{ 640.0F, 360.0F }, Screen{ 3840.0F, 2160.0F }, Screen{ 2560.0F, 1080.0F }, Screen{ 1024.0F, 1366.0F } };
    constexpr std::array<float, 3U> matches{ 0.0F, 0.5F, 1.0F };
    for (const bool safeArea : { false, true }) {
        for (const float match : matches) {
            kb::scene::Scene scene{ kb::scene::SceneMode::PrefabPrivate };
            kb::scene::UIComponentSet canvasComponents = CanvasComponents();
            canvasComponents.canvasScaler->scaleMode = kb::scene::UICanvasScaleMode::ScaleWithScreenSize;
            canvasComponents.canvasScaler->referenceResolution = { 1920.0F, 1080.0F };
            canvasComponents.canvasScaler->matchWidthOrHeight = match;
            const kb::scene::SceneObject canvas = AddUI(scene, {}, canvasComponents);
            kb::scene::UIComponentSet menuComponents;
            menuComponents.rectTransform = kb::scene::UIRectTransform{};
            menuComponents.rectTransform->anchorMin = { 0.1F, 0.1F };
            menuComponents.rectTransform->anchorMax = { 0.9F, 0.9F };
            menuComponents.rectTransform->offsetMin = { 0.0F, 0.0F };
            menuComponents.rectTransform->offsetMax = { 0.0F, 0.0F };
            menuComponents.verticalLayout = kb::scene::UIVerticalLayout{ .spacing = 10.0F, .controlChildWidth = true, .expandChildWidth = true };
            const kb::scene::SceneObject menu = AddUI(scene, canvas, menuComponents);
            std::vector<kb::scene::SceneEntity> buttons;
            for (int index = 0; index < 5; ++index) {
                kb::scene::UIComponentSet button = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Button);
                button.rectTransform = Rect(0.0F, 0.0F, 400.0F, 80.0F);
                button.layoutElement = kb::scene::UILayoutElement{ .preferredWidth = 400.0F, .preferredHeight = 80.0F };
                buttons.push_back(AddUI(scene, menu, button).Entity());
            }
            // A label with a text far longer than anything fits: the limit cuts it and the size stays sane.
            kb::scene::UIComponentSet longLabel = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Text);
            longLabel.rectTransform = Rect(0.0F, 0.0F, 400.0F, 40.0F);
            longLabel.layoutElement = kb::scene::UILayoutElement{ .preferredWidth = 400.0F, .preferredHeight = 40.0F };
            const kb::scene::SceneObject label = AddUI(scene, menu, longLabel);
            kb::tests::Require(kb::scene::SetUITextContent(*scene.Components().UI().TryGet<kb::scene::UIText>(label.Entity()), std::string(1000U, 'W')),
                "A text of 1000 letters must be accepted by the text limit");
            for (const Screen& screen : screens) {
                if (safeArea) {
                    static_cast<void>(scene.UI().SetSafeAreaInsets(kb::scene::UIEdges{ .left = 20.0F, .top = 40.0F, .right = 20.0F, .bottom = 30.0F }));
                }
                kb::tests::Require(scene.UI().Update(screen.width, screen.height, {}, 0.016F), "The responsive menu must build a frame");
                const kb::scene::SceneUIFrame& frame = scene.UI().Frame();
                float previousBottom = -1.0e9F;
                for (const kb::scene::SceneEntity entity : buttons) {
                    const kb::scene::SceneUIFrameElement* element = FindElement(frame, entity);
                    kb::tests::Require(element != nullptr && element->interactionEnabled, "Every control of the menu must be in the frame and usable");
                    const kb::math::Rect& rect = element->rect;
                    kb::tests::Require(std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) && std::isfinite(rect.height) &&
                            rect.width > 1.0F && rect.height > 1.0F,
                        "Every control must have a finite, visible size");
                    kb::tests::Require(rect.x >= -0.5F && rect.y >= -0.5F && rect.x + rect.width <= screen.width + 0.5F &&
                            rect.y + rect.height <= screen.height + 0.5F,
                        "Every control must lie inside the screen");
                    kb::tests::Require(rect.y >= previousBottom - 0.5F, "Controls of a vertical menu must not overlap");
                    previousBottom = rect.y + rect.height;
                    kb::tests::Require(scene.UI().HitTest({ rect.x + rect.width * 0.5F, rect.y + rect.height * 0.5F }) == entity,
                        "The middle of a control must hit that control");
                }
                const kb::scene::SceneUIFrameElement* labelElement = FindElement(frame, label.Entity());
                kb::tests::Require(labelElement != nullptr && std::isfinite(labelElement->rect.width) && std::isfinite(labelElement->rect.height) &&
                        labelElement->rect.width <= screen.width * 4.0F && labelElement->rect.height <= screen.height * 4.0F,
                    "A very long text must not give its element an unbounded size");
                if (labelElement != nullptr && labelElement->text.has_value()) {
                    kb::tests::Require(kb::scene::UITextContent(*labelElement->text).size() < kb::scene::UIText::MaxUtf8Bytes,
                        "A text longer than the text limit must be cut to it");
                }
            }
        }
    }
}

// Controls anchored to the four corners of a screen stay inside the safe area and reachable for every orientation, notch,
// gesture bar and on-screen keyboard (a bottom inset), and insets larger than the screen collapse without negative
// sizes or a crash.
void TestSafeAreaMatrixKeepsCornerControlsReachable() {
    struct Screen { float width; float height; };
    struct Insets { float left; float top; float right; float bottom; };
    constexpr std::array<Screen, 2U> screens{ Screen{ 360.0F, 780.0F }, Screen{ 780.0F, 360.0F } };
    constexpr std::array<Insets, 5U> insets{ Insets{ 0.0F, 0.0F, 0.0F, 0.0F }, Insets{ 0.0F, 44.0F, 0.0F, 34.0F },
        Insets{ 44.0F, 0.0F, 44.0F, 21.0F }, Insets{ 0.0F, 24.0F, 0.0F, 300.0F }, Insets{ 400.0F, 400.0F, 400.0F, 400.0F } };
    for (const Screen& screen : screens) {
        for (const Insets& edge : insets) {
            kb::scene::Scene scene{ kb::scene::SceneMode::PrefabPrivate };
            kb::scene::UIComponentSet canvasComponents = CanvasComponents();
            canvasComponents.canvas->respectSafeArea = true;
            const kb::scene::SceneObject canvas = AddUI(scene, {}, canvasComponents);
            std::array<kb::scene::SceneEntity, 4U> corners{};
            for (std::size_t index = 0U; index < corners.size(); ++index) {
                kb::scene::UIComponentSet button = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Button);
                const float x = (index % 2U == 0U) ? 0.0F : 1.0F;
                const float y = (index < 2U) ? 0.0F : 1.0F;
                button.rectTransform = kb::scene::UIRectTransform{};
                button.rectTransform->anchorMin = { x, y };
                button.rectTransform->anchorMax = { x, y };
                button.rectTransform->pivot = { x, y };
                button.rectTransform->offsetMin = { x > 0.5F ? -80.0F : 0.0F, y > 0.5F ? -40.0F : 0.0F };
                button.rectTransform->offsetMax = { x > 0.5F ? 0.0F : 80.0F, y > 0.5F ? 0.0F : 40.0F };
                corners[index] = AddUI(scene, canvas, button).Entity();
            }
            static_cast<void>(scene.UI().SetSafeAreaInsets(kb::scene::UIEdges{ .left = edge.left, .top = edge.top, .right = edge.right, .bottom = edge.bottom }));
            kb::tests::Require(scene.UI().Update(screen.width, screen.height, {}, 0.016F), "The safe area frame must build");
            // With less room than two buttons side by side and two on top of each other, nothing is promised except sane sizes.
            const bool collapsed = screen.width - edge.left - edge.right < 160.0F || screen.height - edge.top - edge.bottom < 80.0F;
            for (const kb::scene::SceneEntity entity : corners) {
                const kb::scene::SceneUIFrameElement* element = FindElement(scene.UI().Frame(), entity);
                if (element == nullptr) {
                    kb::tests::Require(collapsed, "A corner control may only vanish when the insets leave no room");
                    continue;
                }
                const kb::math::Rect& rect = element->rect;
                kb::tests::Require(std::isfinite(rect.x) && std::isfinite(rect.y) && rect.width >= 0.0F && rect.height >= 0.0F,
                    "A corner control must keep a finite, non-negative size under any inset");
                if (!collapsed) {
                    kb::tests::Require(rect.x >= edge.left - 0.5F && rect.y >= edge.top - 0.5F &&
                            rect.x + rect.width <= screen.width - edge.right + 0.5F && rect.y + rect.height <= screen.height - edge.bottom + 0.5F,
                        "A corner control must lie inside the safe area");
                    kb::tests::Require(scene.UI().HitTest({ rect.x + rect.width * 0.5F, rect.y + rect.height * 0.5F }) == entity,
                        "The middle of a corner control must take presses");
                }
            }
        }
    }
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

void TestIndependentTouchCapture() {
    kb::scene::Scene scene;
    const auto canvas = AddUI(scene, {}, CanvasComponents());
    auto components = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Button);
    components.rectTransform = Rect(200.0F, 0.0F, 100.0F, 100.0F);
    const auto button = AddUI(scene, canvas, components);
    auto& device = scene.Input().MutableDeviceState();
    device.SetHasFocus(true);
    device.SetPointerViewportExtent(400U, 400U);
    const auto frame = [&](std::span<const kb::input::InputTouchPoint> points) {
        device.SetTouchPoints(points);
        static_cast<void>(scene.Runtime().Update(0.016F));
    };
    using Phase = kb::input::InputTouchPhase;
    std::array points{kb::input::InputTouchPoint{7U, 50.0F, 50.0F, Phase::Began},
        kb::input::InputTouchPoint{23U, 250.0F, 50.0F, Phase::Began}};
    frame(std::span{points}.first(1U));
    kb::tests::Require(!scene.UI().Pressed().IsValid(), "Gameplay touch outside UI must not press a widget");
    points[0].phase = Phase::Moved;
    frame(points);
    kb::tests::Require(scene.UI().Pressed() == button.Entity(),
        "A second contact must press UI independently of the first gameplay contact");
    std::swap(points[0], points[1]);
    points[0].phase = Phase::Moved;
    points[1].x = 350.0F;
    frame(points);
    kb::tests::Require(scene.UI().Pressed() == button.Entity(), "Contact order changes must preserve the captured UI press");
    points[0].phase = Phase::Ended;
    frame(points);
    kb::tests::Require(!scene.UI().Pressed().IsValid() &&
        HasEvent(scene.UI().Events(), kb::scene::SceneUIEventType::Clicked, button.Entity()),
        "Captured contact release must activate its button while the gameplay finger remains down");
    static_cast<void>(scene.UI().DrainEvents());
    frame(std::span{points}.subspan(1U));
    kb::tests::Require(!scene.UI().Pressed().IsValid() &&
        !HasEvent(scene.UI().Events(), kb::scene::SceneUIEventType::Pressed, button.Entity()) &&
        !HasEvent(scene.UI().Events(), kb::scene::SceneUIEventType::Clicked, button.Entity()),
        "The remaining gameplay move must not become a new UI press");
    points[0].phase = Phase::Began;
    frame(points);
    kb::tests::Require(scene.UI().Pressed() == button.Entity(), "A fresh contact must press UI again");
    static_cast<void>(scene.UI().DrainEvents());
    device.SetHasFocus(false);
    points[0].phase = Phase::Moved;
    frame(points);
    kb::tests::Require(!scene.UI().Pressed().IsValid() &&
        !HasEvent(scene.UI().Events(), kb::scene::SceneUIEventType::Clicked, button.Entity()),
        "Focus loss must release a captured UI contact without activating it");
    device.SetHasFocus(true);
    frame(points);
    kb::tests::Require(!scene.UI().Pressed().IsValid(), "A move after focus restoration must not revive a UI press");
    points[0].phase = Phase::Began;
    frame(points);
    kb::tests::Require(scene.UI().Pressed() == button.Entity(), "Cancellation fixture must capture the button");
    static_cast<void>(scene.UI().DrainEvents());
    points[0].phase = Phase::Cancelled;
    frame(points);
    kb::tests::Require(!scene.UI().Pressed().IsValid() &&
        HasEvent(scene.UI().Events(), kb::scene::SceneUIEventType::Canceled, button.Entity()) &&
        !HasEvent(scene.UI().Events(), kb::scene::SceneUIEventType::Clicked, button.Entity()),
        "Explicit touch cancellation must release and cancel the button without activation");
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

void TestFocusedTextAfterViewportResize() {
    kb::scene::Scene scene;
    const auto canvas = AddUI(scene, {}, CanvasComponents());
    kb::scene::UIComponentSet formComponents;
    formComponents.rectTransform = Rect(0.0F, 0.0F, 0.0F, 0.0F);
    formComponents.rectTransform->anchorMax = {1.0F, 1.0F};
    formComponents.mask.emplace();
    formComponents.scrollView.emplace();
    formComponents.scrollView->horizontal = false;
    formComponents.scrollView->inertia = false;
    const auto form = AddUI(scene, canvas, formComponents);
    auto fieldComponents = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::InputField);
    fieldComponents.rectTransform = Rect(40.0F, 320.0F, 160.0F, 80.0F);
    const auto field = AddUI(scene, form, fieldComponents);
    kb::scene::SceneUIInput input;
    input.pointerAvailable = true;
    input.pointerPosition = {100.0F, 350.0F};
    kb::tests::Require(scene.UI().Update(400.0F, 600.0F, input, 0.016F), "Form must build before focus");
    input.primaryDown = true;
    kb::tests::Require(scene.UI().Update(400.0F, 600.0F, input, 0.016F), "Pointer must focus the field");
    input.primaryDown = false;
    const std::array<char32_t, 1U> typed{U'x'};
    input.textInput = typed;
    kb::tests::Require(scene.UI().Update(400.0F, 600.0F, input, 0.016F), "Focused field must accept text");
    input.textInput = {};
    kb::tests::Require(scene.UI().Focused() == field.Entity(), "The form field must hold actual pointer focus");
    const auto resize = [&](float height) {
        kb::tests::Require(scene.UI().Update(400.0F, height, input, 0.016F), "Focused form must resize");
    };
    const auto requireVisible = [&] {
        const auto* item = FindElement(scene.UI().Frame(), field.Entity());
        const auto* viewport = FindElement(scene.UI().Frame(), form.Entity());
        kb::tests::Require(item != nullptr && viewport != nullptr && item->rect.y >= viewport->rect.y - 0.01F &&
            item->rect.y + item->rect.height <= viewport->rect.y + viewport->rect.height + 0.01F,
            "Viewport changes must reveal the focused text field inside its scroll view");
        kb::tests::Require(scene.UI().Focused() == field.Entity() &&
            kb::scene::UITextContent(*scene.Components().UI().TryGet<kb::scene::UIText>(field.Entity())) == "x",
            "Revealing a field must preserve focus and committed text");
        kb::tests::Require(scene.UI().HitTest({100.0F, item->rect.y + 40.0F}) == field.Entity(),
            "The revealed field must hit-test where it is drawn");
    };
    resize(280.0F);
    requireVisible();
    input.pointerPosition = {100.0F, 240.0F};
    input.scrollDelta = 10.0F;
    resize(280.0F);
    input.scrollDelta = 0.0F;
    resize(280.0F);
    kb::tests::Require(kb::tests::NearlyEqual(scene.Components().UI().TryGet<kb::scene::UIScrollView>(form.Entity())->scrollY, 0.0F),
        "Ordinary frames must let the user scroll away without forcing the focus back into view");
    resize(600.0F);
    requireVisible();
    resize(280.0F);
    requireVisible();
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
void TestSparseGroupTransitionLifetime() {
    using kb::tests::Require;
    using kb::tests::NearlyEqual;
    kb::scene::Scene scene;
    const auto unrelated = scene.Runtime().EcsWorld().CreateEntities(10'000U, {});
    const auto owner = scene.Entities().CreateEntity();
    auto ui = scene.Components().UI();
    ui.Set(owner, kb::scene::UICanvasGroup{.transitionSeconds = 1.0F});
    Require(!kb::scene::SceneUIGroupTransitions::Update(scene, 0.1F), "A new group starts at its authored visibility");
    auto& shown = kb::scene::SceneAccess::State(scene).uiGroupShown;
    Require(shown.size() == 1U && shown.at(owner.Id()) == 1.0F, "Only actual groups own animation state");
    ui.TryGet<kb::scene::UICanvasGroup>(owner)->visible = false;
    Require(kb::scene::SceneUIGroupTransitions::Update(scene, 0.25F) && NearlyEqual(shown.at(owner.Id()), 0.75F), "Existing groups animate after a direct component edit");
    ui.Remove<kb::scene::UICanvasGroup>(owner);
    Require(!kb::scene::SceneUIGroupTransitions::Update(scene, 0.1F) && shown.empty(), "Removing a component releases derived animation state");
    ui.Set(owner, kb::scene::UICanvasGroup{.visible = false, .transitionSeconds = 1.0F});
    Require(!kb::scene::SceneUIGroupTransitions::Update(scene, 0.1F) && shown.at(owner.Id()) == 0.0F, "Readding a group starts from current authored visibility");
    scene.Entities().Destroy(owner);
    Require(!kb::scene::SceneUIGroupTransitions::Update(scene, 0.1F) && shown.empty(), "Destroying a group releases its state");
    Require(unrelated.size() == 10'000U, "Unrelated entities remain present");
}

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

    // Edges: a language that is not in the catalog (a regional variant too) is refused and changes nothing, a message
    // missing from the selected language falls back to the catalog language, a message missing everywhere shows its key,
    // and a translation longer than the text limit is cut on a character boundary instead of overflowing.
    kb::tests::Require(!scene.Localization().SetLanguage("pl-PL") && !scene.Localization().SetLanguage("xx") && scene.Localization().Language() == "pl",
        "A language the catalog does not have must be refused and leave the language alone");
    const std::string accent = "\xE2\x82\xAC"; // the euro sign: three bytes, so a cut at 1022 would split it
    std::string tooLong;
    for (int index = 0; index < 600; ++index) tooLong += accent;
    const kb::localization::LocalizationCatalog edgeCatalog{
        .fallbackLanguage = "en",
        .languages = {{"en", {{"long.text", {.text = tooLong}}, {"short.text", {.text = "ok"}}}}, {"pl", {{"short.text", {.text = "OK!"}}}}},
    };
    kb::tests::Require(kb::localization::LocalizationCatalogIO::Save(root / "Assets" / "Localization" / "Edge.kbloc", edgeCatalog), "The edge catalog must save");
    static_cast<void>(scene.Assets().Discover());
    const auto* edgeMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Localization/Edge.kbloc");
    kb::tests::Require(edgeMetadata != nullptr && scene.Localization().SetCatalog(edgeMetadata->id.value), "The edge catalog must load");
    const auto addLocalized = [&](const char* key, float y) {
        kb::scene::UIComponentSet components;
        components.rectTransform = Rect(0.0F, y, 100.0F, 20.0F);
        components.text.emplace();
        static_cast<void>(kb::scene::SetUITextLocalizationKey(*components.text, key));
        return AddUI(scene, canvas, components);
    };
    const kb::scene::SceneObject longText = addLocalized("long.text", 130.0F);
    const kb::scene::SceneObject shortText = addLocalized("short.text", 150.0F);
    const kb::scene::SceneObject missingText = addLocalized("no.such.key", 170.0F);
    for (const char* language : {"pl", "en"}) {
        kb::tests::Require(scene.Localization().SetLanguage(language), "The edge catalog language must switch");
        pointer.At(390.0F, 390.0F, false);
        const std::string shownLong{ShownText(scene.UI().Frame(), longText.Entity())};
        kb::tests::Require(!shownLong.empty() && shownLong.size() < kb::scene::UIText::MaxUtf8Bytes && shownLong.size() % 3U == 0U &&
                shownLong.compare(0U, 3U, accent) == 0 && shownLong.compare(shownLong.size() - 3U, 3U, accent) == 0,
            "A translation longer than the text limit must be cut at a character boundary, also when it comes from the fallback language");
        kb::tests::Require(ShownText(scene.UI().Frame(), shortText.Entity()) == (std::string{language} == "pl" ? "OK!" : "ok"),
            "A short message must follow the language");
        kb::tests::Require(ShownText(scene.UI().Frame(), missingText.Entity()) == "no.such.key", "A message missing everywhere must show its key");
    }
    std::filesystem::remove_all(root);
}

// Every v39 widget setting survives a save, and an older canvas keeps filling the whole screen.
// A modal canvas takes the input of every canvas drawn below it while it is shown: no hit, no focus, no navigation into
// them. Focus moves into the modal canvas when it appears and returns to where it was when the canvas goes.
void TestModalCanvasBlocksCanvasesBelow() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject hud = AddUI(scene, {}, CanvasComponents());
    kb::scene::UIComponentSet buttonComponents = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Button);
    buttonComponents.rectTransform = Rect(0.0F, 0.0F, 100.0F, 20.0F);
    const kb::scene::SceneObject hudFirst = AddUI(scene, hud, buttonComponents);
    buttonComponents.rectTransform = Rect(0.0F, 40.0F, 100.0F, 20.0F);
    const kb::scene::SceneObject hudSecond = AddUI(scene, hud, buttonComponents);

    kb::scene::UIComponentSet dialogComponents = CanvasComponents();
    dialogComponents.canvas->sortingOrder = 10;
    dialogComponents.canvas->modal = true;
    dialogComponents.canvasGroup.emplace();
    dialogComponents.canvasGroup->visible = false;
    dialogComponents.canvasGroup->transitionSeconds = 0.0F;
    const kb::scene::SceneObject dialog = AddUI(scene, {}, dialogComponents);
    buttonComponents.rectTransform = Rect(200.0F, 0.0F, 100.0F, 20.0F);
    const kb::scene::SceneObject yes = AddUI(scene, dialog, buttonComponents);
    buttonComponents.rectTransform = Rect(200.0F, 40.0F, 100.0F, 20.0F);
    const kb::scene::SceneObject no = AddUI(scene, dialog, buttonComponents);

    kb::input::InputDeviceState& device = scene.Input().MutableDeviceState();
    device.SetHasFocus(true);
    device.SetPointerViewportExtent(400U, 400U);
    device.SetPointerPosition(390.0F, 390.0F);
    const auto frame = [&] { static_cast<void>(scene.Runtime().Update(0.016F)); };
    const auto tap = [&](kb::input::InputKey key) {
        device.SetKeyDown(key, true);
        frame();
        device.SetKeyDown(key, false);
        frame();
    };
    frame();
    kb::tests::Require(!scene.UI().ModalCanvasActive() && scene.UI().SetFocus(hudFirst.Entity()), "A hidden modal canvas must not block anything");
    kb::tests::Require(scene.UI().HitTest({50.0F, 10.0F}) == hudFirst.Entity(), "The HUD is hit while the dialog is hidden");

    // The dialog appears.
    scene.Components().UI().TryGet<kb::scene::UICanvasGroup>(dialog.Entity())->visible = true;
    scene.Components().UI().MarkModified<kb::scene::UICanvasGroup>(dialog.Entity());
    frame();
    frame();
    kb::tests::Require(scene.UI().ModalCanvasActive(), "A shown modal canvas must report itself");
    kb::tests::Require(scene.UI().Focused() == yes.Entity(), "Focus must move into the modal canvas when it appears");
    kb::tests::Require(scene.UI().HitTest({50.0F, 10.0F}) != hudFirst.Entity() && scene.UI().HitTest({50.0F, 50.0F}) != hudSecond.Entity(),
        "The canvas below a modal canvas must not be hit");
    kb::tests::Require(scene.UI().HitTest({250.0F, 10.0F}) == yes.Entity(), "The modal canvas itself is still hit");
    kb::tests::Require(!scene.UI().SetFocus(hudSecond.Entity()), "The canvas below a modal canvas must refuse focus");
    tap(kb::input::InputKey::GamepadDPadDown);
    kb::tests::Require(scene.UI().Focused() == no.Entity(), "Navigation moves inside the modal canvas");
    tap(kb::input::InputKey::GamepadDPadDown);
    tap(kb::input::InputKey::GamepadDPadLeft);
    kb::tests::Require(scene.UI().Focused() == no.Entity(), "Navigation must not leave the modal canvas for the one below");

    // The dialog goes: focus returns to the widget that had it.
    scene.Components().UI().TryGet<kb::scene::UICanvasGroup>(dialog.Entity())->visible = false;
    scene.Components().UI().MarkModified<kb::scene::UICanvasGroup>(dialog.Entity());
    frame();
    frame();
    kb::tests::Require(!scene.UI().ModalCanvasActive(), "A hidden modal canvas must let go");
    kb::tests::Require(scene.UI().Focused() == hudFirst.Entity(), "Focus must return to where it was before the modal canvas");
    kb::tests::Require(scene.UI().HitTest({50.0F, 50.0F}) == hudSecond.Entity() && scene.UI().SetFocus(hudSecond.Entity()),
        "The canvas below is reachable again once the modal canvas is gone");
}

void TestWidgetSettingsPersistence() {
    kb::scene::UIComponentSet authored;
    authored.rectTransform.emplace();
    authored.canvas.emplace();
    authored.canvas->renderMode = kb::scene::UICanvasRenderMode::WorldSpace;
    authored.canvas->pixelsPerUnit = 50.0F;
    authored.canvas->respectSafeArea = false;
    authored.canvas->player = 2;
    authored.canvas->modal = true;
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
    authored.image->materialAssetId = 81U;
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
    kb::tests::Require(kb::scene::SceneAssetUIComponentCodec::Read(reader, kb::scene::SceneDocument::CurrentFileVersion, decoded) && reader.Exhausted(), "The current widget set must load");
    kb::tests::Require(decoded.canvas->renderMode == kb::scene::UICanvasRenderMode::WorldSpace && decoded.canvas->pixelsPerUnit == 50.0F &&
        !decoded.canvas->respectSafeArea && decoded.canvas->player == 2 && decoded.canvas->modal && !decoded.canvasGroup->visible &&
        decoded.canvasGroup->transitionSeconds == 0.3F && decoded.canvasGroup->hiddenOffset.y == 5.0F && decoded.canvasGroup->hiddenScale == 0.8F &&
        decoded.image->fillMethod == kb::scene::UIImageFillMethod::Radial360 && decoded.image->fillOrigin == kb::scene::UIImageFillOrigin::End &&
        decoded.image->fillAmount == 0.4F && !decoded.image->fillClockwise && decoded.image->alphaHitThreshold == 0.2F &&
        decoded.image->materialAssetId == 81U &&
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
        "Every v45 widget setting must round-trip");

    // A canvas saved before the modal flag existed (v51) ends one byte earlier and never blocks the canvases below.
    {
        kb::scene::UIComponentSet onlyCanvas;
        onlyCanvas.canvas.emplace();
        onlyCanvas.canvas->modal = true;
        std::vector<std::uint8_t> current;
        kb::scene::SceneAssetUIComponentCodec::Write(current, onlyCanvas);
        current.pop_back();
        kb::scene::SceneAssetBinaryIO::ByteReader olderReader{current};
        kb::scene::UIComponentSet older;
        kb::tests::Require(kb::scene::SceneAssetUIComponentCodec::Read(olderReader, 51U, older) && olderReader.Exhausted() && !older.canvas->modal,
            "A v51 canvas must load without a modal flag");
    }

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

struct UIFrameHash {
    std::uint64_t value = 1469598103934665603ULL;

    void Bytes(const void* data, std::size_t size) noexcept {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0U; index < size; ++index) {
            value ^= bytes[index];
            value *= 1099511628211ULL;
        }
    }
    void U64(std::uint64_t number) noexcept { Bytes(&number, sizeof(number)); }
    void Float(float number) noexcept { Bytes(&number, sizeof(number)); }
    void Rect(const kb::math::Rect& rect) noexcept { Float(rect.x); Float(rect.y); Float(rect.width); Float(rect.height); }
    void Quad(const std::array<kb::math::Vec2, 4U>& quad) noexcept { for (const kb::math::Vec2 point : quad) { Float(point.x); Float(point.y); } }
};

[[nodiscard]] std::uint64_t HashUIFrame(const kb::scene::SceneUIFrame& frame) {
    UIFrameHash hash;
    hash.U64(frame.elements.size());
    hash.U64(frame.refusal.entity.Id());
    for (const kb::scene::SceneUIFrameElement& element : frame.elements) {
        hash.U64(element.entity.Id());
        hash.U64(element.canvas.Id());
        hash.Rect(element.rect);
        hash.Quad(element.corners);
        hash.Quad(element.hitCorners);
        hash.Rect(element.clipRect);
        hash.Float(element.clipSoftness);
        hash.U64(element.clipQuads.size());
        for (const auto& quad : element.clipQuads) hash.Quad(quad);
        hash.Float(element.canvasScale);
        hash.Float(element.effectiveOpacity);
        hash.U64(static_cast<std::uint64_t>(static_cast<std::uint32_t>(element.canvasSortingOrder)));
        hash.U64(static_cast<std::uint64_t>(static_cast<std::uint32_t>(element.player)));
        hash.U64(static_cast<std::uint64_t>(static_cast<std::uint32_t>(element.zOrder)));
        hash.U64(element.traversalOrder);
        hash.U64(static_cast<std::uint64_t>(element.interactionState));
        hash.Float(element.interactionTint.r); hash.Float(element.interactionTint.g); hash.Float(element.interactionTint.b); hash.Float(element.interactionTint.a);
        hash.U64((element.interactionEnabled ? 1U : 0U) | (element.hitTestable ? 2U : 0U) | (element.tooltip ? 4U : 0U) | (element.textCaretVisible ? 8U : 0U));
        hash.U64((element.sprite ? 1U : 0U) | (element.image ? 2U : 0U) | (element.rawImage ? 4U : 0U) | (element.text ? 8U : 0U) |
            (element.border ? 16U : 0U) | (element.mask ? 32U : 0U) | (element.shadow ? 64U : 0U) | (element.outline ? 128U : 0U) |
            (element.backgroundBlur ? 256U : 0U) | (element.toggle ? 512U : 0U) | (element.slider ? 1024U : 0U) | (element.scrollbar ? 2048U : 0U) |
            (element.scrollView ? 4096U : 0U) | (element.dropdown ? 8192U : 0U) | (element.progressBar ? 16384U : 0U) | (element.inputField ? 32768U : 0U));
        if (element.text) {
            const std::string_view content = kb::scene::UITextContent(*element.text);
            hash.Bytes(content.data(), content.size());
        }
        if (element.toggle) hash.U64(element.toggle->toggled ? 1U : 0U);
        if (element.slider) hash.Float(element.slider->value);
        if (element.dropdown) hash.U64(element.dropdown->value);
        hash.U64(element.textCaretByteOffset);
    }
    return hash.value;
}

// Canvases among many plain roots, with root order changed by reparenting and destruction, equal sorting orders
// and widgets that drive the same caption, graphic and fill: the frame was recorded before the frame builder
// stopped walking every root, and must not depend on how the canvases are found.
void TestFrameAmongManyRootsMatchesRecordedFrame() {
    kb::scene::Scene scene;
    std::uint32_t seed = 12345U;
    const auto next = [&seed](std::uint32_t range) {
        seed = seed * 1664525U + 1013904223U;
        return (seed >> 8U) % range;
    };
    std::vector<kb::scene::SceneObject> plain;
    const auto addPlain = [&](std::size_t count) {
        for (std::size_t index = 0U; index < count; ++index) plain.push_back(scene.Entities().CreateObject());
    };
    addPlain(100U);
    std::vector<kb::scene::SceneObject> canvases;
    kb::scene::SceneObject shared;
    kb::scene::SceneObject sharedFill;
    constexpr std::array<kb::scene::UIComponentPreset, 5U> presets{ kb::scene::UIComponentPreset::Button, kb::scene::UIComponentPreset::Text,
        kb::scene::UIComponentPreset::Toggle, kb::scene::UIComponentPreset::Slider, kb::scene::UIComponentPreset::ProgressBar };
    for (std::uint32_t canvasIndex = 0U; canvasIndex < 6U; ++canvasIndex) {
        addPlain(50U + next(200U));
        kb::scene::UIComponentSet canvasComponents = CanvasComponents();
        canvasComponents.canvas->sortingOrder = static_cast<std::int32_t>(next(3U));
        const kb::scene::SceneObject parent = canvasIndex == 4U ? plain[next(static_cast<std::uint32_t>(plain.size()))] : kb::scene::SceneObject{};
        const kb::scene::SceneObject canvas = AddUI(scene, parent, canvasComponents);
        canvases.push_back(canvas);
        if (canvasIndex == 0U) {
            kb::scene::UIComponentSet text = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Text);
            text.rectTransform = Rect(10.0F, 10.0F, 100.0F, 20.0F);
            shared = AddUI(scene, canvas, text);
            kb::scene::UIComponentSet fill;
            fill.rectTransform = Rect(10.0F, 40.0F, 100.0F, 10.0F);
            fill.border.emplace();
            sharedFill = AddUI(scene, canvas, fill);
        }
        for (std::uint32_t widgetIndex = 0U; widgetIndex < 5U; ++widgetIndex) {
            kb::scene::UIComponentSet widget = kb::scene::BuildUIComponentPreset(presets[next(static_cast<std::uint32_t>(presets.size()))]);
            widget.rectTransform = Rect(static_cast<float>(next(500U)), static_cast<float>(next(300U)), 40.0F + static_cast<float>(next(80U)),
                20.0F + static_cast<float>(next(40U)), static_cast<std::int32_t>(next(3U)));
            if (widget.toggle) {
                widget.toggle->toggled = next(2U) == 1U;
                widget.toggle->graphic = shared.Entity().Id();
            }
            if (widget.slider) {
                widget.slider->value = static_cast<float>(next(100U)) * 0.01F;
                widget.slider->fillRect = sharedFill.Entity().Id();
            }
            if (widget.progressBar) {
                widget.progressBar->value = static_cast<float>(next(100U)) * 0.01F;
                widget.progressBar->fillRect = sharedFill.Entity().Id();
            }
            static_cast<void>(AddUI(scene, canvas, widget));
        }
    }
    addPlain(100U);
    kb::tests::Require(scene.Hierarchy().SetParent(canvases[1], plain[3]) && scene.Hierarchy().SetParent(canvases[1], kb::scene::SceneObject{}),
        "Recorded UI frame scene could not move a canvas to the end of the roots");
    scene.Entities().Destroy(plain[7]);
    scene.Entities().Destroy(plain[120]);

    kb::scene::SceneUIFrame frame;
    kb::tests::Require(kb::scene::SceneUIQueries{scene}.BuildFrame(640.0F, 360.0F, frame) && frame.elements.size() > 30U,
        "Recorded UI frame scene must build");
    const std::uint64_t hash = HashUIFrame(frame);
    constexpr std::uint64_t expected = 0x21362496b433b984ULL;
    if (hash != expected) std::cerr << "recorded UI frame hash 0x" << std::hex << hash << std::dec << '\n';
    kb::tests::Require(hash == expected, "UI frame among many roots differs from the recorded frame");
}

void PopulateMillionRootOverlay(kb::scene::Scene& scene) {
    const std::vector<kb::scene::SceneObjectDesc> descs(1'000'000U);
    static_cast<void>(scene.Entities().CreateObjects(descs));
    const kb::scene::SceneObject canvas = AddUI(scene, {}, CanvasComponents());
    for (std::uint32_t index = 0U; index < 8U; ++index) {
        kb::scene::UIComponentSet text = kb::scene::BuildUIComponentPreset(kb::scene::UIComponentPreset::Text);
        text.rectTransform = Rect(10.0F, 10.0F + 30.0F * static_cast<float>(index), 200.0F, 24.0F);
        static_cast<void>(AddUI(scene, canvas, text));
    }
    kb::tests::Require(scene.Entities().Count() == 1'000'009U,
        "Overlay profiling scene must contain a million plain roots and nine UI objects");
}

// The frame builder walks the roots above UI objects only: a small overlay next to a million plain roots must
// build in well under a millisecond and without large allocations.
void TestFrameNextToMillionRootsIsCheap() {
    kb::scene::Scene scene;
    PopulateMillionRootOverlay(scene);
    const kb::scene::SceneUIQueries ui{scene};
    kb::scene::SceneUIFrame frame;
    std::array<double, 5U> samples{};
    std::size_t largeAllocations = 0U;
    for (int warmup = 0; warmup < 2; ++warmup) {
        kb::tests::Require(ui.BuildFrame(1920.0F, 1080.0F, frame) && frame.elements.size() == 9U, "Overlay frame next to a million roots must build");
    }
    for (double& sample : samples) {
        kb::tests::BeginAllocationTally();
        const auto start = std::chrono::steady_clock::now();
        const bool built = ui.BuildFrame(1920.0F, 1080.0F, frame);
        sample = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        largeAllocations += kb::tests::EndAllocationTally().largeCount;
        kb::tests::Require(built && frame.elements.size() == 9U, "Overlay frame next to a million roots must build");
    }
    std::ranges::sort(samples);
    std::cout << "ui_buildframe_million_roots median_ms=" << samples[2] << " large_allocations=" << largeAllocations
              << " element_bytes=" << sizeof(kb::scene::SceneUIFrameElement) << '\n';
    kb::tests::Require(samples[2] < 0.5 * kb::tests::kSanitizerTimeScale && largeAllocations == 0U,
        "A small overlay next to a million plain roots must build in under 0.5 ms without allocations of 16 KB or more");
}

void RunSceneUITests() {
    TestAuthoredDimensionsAndVisibility();
    TestCatalogAndPresets();
    TestFrameWithoutCanvasTracksDynamicComponents();
    TestFrameTracksWidgetComponentLifetimes();
    TestLayoutsAndFitters();
    TestInvalidFramePayloads();
    TestContentSizeFitterModes();
    TestHierarchicalTransformsAndMaskHitTesting();
    TestClippingGroupsSortingAndScaling();
    TestInteractionAndEditing();
    TestDropdownListFromTemplate();
    TestDropdownScrollingAndPlacement();
    TestDropdownPersistenceAndOptionEdits();
    TestNavigationLinksSurviveHierarchyCopies();
    TestProgressAndEventQueue();
    TestAutomaticRuntimeInput();
    TestFocusedTextAfterViewportResize();
    TestIndependentTouchCapture();
    TestControllerNavigation();
    TestRadioGroupsAndDrivenWidgets();
    TestSelectablePresentationAndHits();
    TestDragAndDrop();
    TestInputFieldContentTypes();
    TestCanvasPlacementAnimationAndPlayers();
    TestSparseGroupTransitionLifetime();
    TestScrollMovementAndLocalizedText();
    TestWidgetSettingsPersistence();
    TestModalCanvasBlocksCanvasesBelow();
    TestFrameAmongManyRootsMatchesRecordedFrame();
    TestFrameNextToMillionRootsIsCheap();
    TestInputFieldEditsGraphemeClusters();
    TestResponsiveMenuKeepsEveryControlReachable();
    TestNestedMasksClipToTheirOverlap();
    TestSafeAreaMatrixKeepsCornerControlsReachable();
    RunUITextLineBreakingTests();
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

void RunSceneUIBuildFrameProfile() {
    kb::scene::Scene scene;
    PopulateMillionRootOverlay(scene);
    const kb::scene::SceneUIQueries ui{scene};
    kb::scene::SceneUIFrame frame;
    for (int warmup = 0; warmup < 2; ++warmup) {
        kb::tests::Require(ui.BuildFrame(1920.0F, 1080.0F, frame) && frame.elements.size() == 9U,
            "UI profiling workload must build the complete overlay");
    }
    const auto expectedHash = HashUIFrame(frame);
    constexpr std::size_t frameCount = 10'000U;
    std::vector<double> samples(frameCount);
    std::cout << "{\"schema\":\"21kb.scene-ui-profile/v1\",\"phase\":\"begin\",\"frames\":"
              << frameCount << ",\"budgetEvaluated\":false}" << std::endl;
    kb::tests::BeginAllocationTally();
    for (double& sample : samples) {
        const auto start = std::chrono::steady_clock::now();
        const bool built = ui.BuildFrame(1920.0F, 1080.0F, frame);
        sample = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        kb::tests::Require(built && frame.elements.size() == 9U,
            "UI profiling workload must build the complete overlay on every frame");
    }
    const auto allocations = kb::tests::EndAllocationTally();
    kb::tests::Require(HashUIFrame(frame) == expectedHash,
        "UI profiling workload must preserve the complete frame contents");
    std::ranges::sort(samples);
    std::cout << "{\"schema\":\"21kb.scene-ui-profile/v1\",\"phase\":\"complete\",\"roots\":1000000,"
                 "\"elements\":9,\"frames\":" << frameCount
              << ",\"warmupFrames\":2,\"clock\":\"steady_wall_ms\",\"p50\":" << samples[(frameCount - 1U) * 50U / 100U]
              << ",\"p95\":" << samples[(frameCount - 1U) * 95U / 100U]
              << ",\"p99\":" << samples[(frameCount - 1U) * 99U / 100U]
              << ",\"allocations\":" << allocations.count << ",\"allocationBytes\":" << allocations.bytes
              << ",\"largeAllocations\":" << allocations.largeCount
              << ",\"budgetEvaluated\":false}" << std::endl;
}

} // namespace kb::tests
