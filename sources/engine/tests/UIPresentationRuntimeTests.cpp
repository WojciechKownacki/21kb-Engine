#include "TestSupport.hpp"

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneUIDocuments.hpp"
#include "engine/scene/UIAssetIO.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/ui/layout/UIPresentationLayout.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace kb::tests {
namespace {

const kb::scene::UIPresentationItem* FindItem(const kb::scene::UIPresentationSnapshot& snapshot,
                                              kb::scene::UIElementId element) {
    for (const kb::scene::UIPresentationItem& item : snapshot.items) {
        if (item.elementId == element)
            return &item;
    }
    return nullptr;
}

bool SameRect(const kb::scene::UIPresentationRect& lhs, const kb::scene::UIPresentationRect& rhs) noexcept {
    return lhs.left == rhs.left && lhs.top == rhs.top && lhs.right == rhs.right && lhs.bottom == rhs.bottom;
}

kb::script::ScriptFunctionArgument Argument(std::string name, kb::script::ScriptValue value) {
    return {.name = std::move(name), .value = std::move(value)};
}

} // namespace

void RunUIPresentationRuntimeTests() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb-ui-presentation-runtime-tests";
    std::filesystem::remove_all(root);
    const std::filesystem::path documentPath = root / "Assets" / "UI" / "Menu.kbui";
    std::filesystem::create_directories(documentPath.parent_path());

    const kb::scene::UIDocument document{
        .elements =
            {
                {.id = 1U,
                 .parentId = 0U,
                 .siblingOrder = 0U,
                 .name = "Canvas",
                 .rect = {.anchorMax = {1.0F, 1.0F}, .offsetMax = {}},
                 .canvas =
                     kb::scene::UICanvas{
                         .referenceResolution = {800.0F, 600.0F},
                         .matchWidthOrHeight = 0.0F,
                     },
                 .control = {.kind = kb::scene::UIControlKind::Canvas}},
                {.id = 2U,
                 .parentId = 1U,
                 .siblingOrder = 0U,
                 .name = "Actions",
                 .rect = {.offsetMin = {100.0F, 100.0F}, .offsetMax = {400.0F, 400.0F}},
                 .layout =
                     kb::scene::UIContainerLayout{
                         .mode = kb::scene::UIContainerLayoutMode::Vertical,
                         .padding = {10.0F, 10.0F, 10.0F, 10.0F},
                         .spacing = {0.0F, 5.0F},
                     },
                 .effects = kb::scene::UIEffects{.clipChildren = true},
                 .control = {.kind = kb::scene::UIControlKind::VerticalBox}},
                {.id = 3U,
                 .parentId = 2U,
                 .siblingOrder = 0U,
                 .name = "Play",
                 .rect = {.offsetMax = {200.0F, 50.0F}},
                 .paint = kb::scene::UIPaint{.backgroundColor = {0.1F, 0.2F, 0.3F, 1.0F}},
                 .textStyle = kb::scene::UIText{},
                 .interaction = kb::scene::UIInteraction{.eventName = "Menu.Play"},
                 .control = {.kind = kb::scene::UIControlKind::Button, .text = "Play"}},
                {.id = 4U,
                 .parentId = 2U,
                 .siblingOrder = 1U,
                 .name = "Volume",
                 .rect = {.offsetMax = {200.0F, 40.0F}},
                 .interaction = kb::scene::UIInteraction{},
                 .control = {.kind = kb::scene::UIControlKind::Slider,
                             .sliderValue = 0.5F,
                             .sliderMinimum = 0.0F,
                             .sliderMaximum = 1.0F}},
                {.id = 50U,
                 .parentId = 1U,
                 .siblingOrder = 1U,
                 .name = "Toggle",
                 .rect = {.offsetMin = {120.0F, 120.0F}, .offsetMax = {220.0F, 180.0F}, .zOrder = 10},
                 .interaction = kb::scene::UIInteraction{.eventName = "Menu.Toggle"},
                 .control = {.kind = kb::scene::UIControlKind::Toggle}},
                {.id = 6U,
                 .parentId = 1U,
                 .siblingOrder = 2U,
                 .name = "PlayerName",
                 .rect = {.offsetMin = {100.0F, 450.0F}, .offsetMax = {300.0F, 500.0F}},
                 .textStyle = kb::scene::UIText{},
                 .interaction = kb::scene::UIInteraction{},
                 .control = {.kind = kb::scene::UIControlKind::InputField}},
            },
    };
    Require(kb::scene::UIAssetIO::SaveDocument(documentPath, document), "UI presentation fixture could not be saved");

    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(root) && scene.Assets().Discover() == 1U,
            "UI presentation fixture project could not be mounted");
    const auto* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/UI/Menu.kbui");
    Require(metadata != nullptr, "UI presentation fixture was not discovered");
    const kb::scene::SceneObject owner = scene.Entities().CreateObject({.name = "Menu"});
    scene.Components().UIDocuments().Set(owner.Entity(), {
                                                             .documentAssetId = metadata->id.value,
                                                             .enabled = true,
                                                         });
    static_cast<void>(scene.Runtime().Update(0.0F));

    const kb::scene::UIPresentationSnapshot& initial = scene.UIDocuments().BuildPresentation(800U, 600U);
    kb::scene::UIPresentationSnapshot publicLayout;
    kb::scene::UIPresentationLayout::Build(document, 800U, 600U, publicLayout);
    Require(publicLayout.items.size() == initial.items.size() &&
                publicLayout.hitTargets.size() == initial.hitTargets.size(),
            "Public and runtime UI layout paths did not produce the same element sets");
    for (std::size_t index = 0U; index < initial.items.size(); ++index) {
        const kb::scene::UIPresentationItem& runtimeItem = initial.items[index];
        const kb::scene::UIPresentationItem& publicItem = publicLayout.items[index];
        Require(runtimeItem.elementId == publicItem.elementId && SameRect(runtimeItem.rect, publicItem.rect) &&
                    SameRect(runtimeItem.clipRect, publicItem.clipRect) &&
                    runtimeItem.painterOrder == publicItem.painterOrder &&
                    runtimeItem.canvasScale == publicItem.canvasScale,
                "Public UI layout geometry diverged from the runtime presentation snapshot");
    }
    const kb::scene::UIPresentationItem* button = FindItem(initial, 3U);
    const kb::scene::UIPresentationItem* slider = FindItem(initial, 4U);
    Require(button != nullptr && button->rect.left == 110.0F && button->rect.top == 110.0F &&
                button->rect.right == 310.0F && button->rect.bottom == 160.0F && slider != nullptr &&
                slider->rect.top == 165.0F && initial.hitTargets.size() == 4U,
            "Canvas scaling or vertical layout did not produce deterministic shared geometry");

    kb::input::InputDeviceState& input = scene.Input().MutableDeviceState();
    input.SetHasFocus(true);
    input.SetPointerViewportExtent(400U, 300U);
    static_cast<void>(scene.UIDocuments().BuildPresentation(800U, 600U));
    input.SetPointerPosition(140.0F, 70.0F);
    input.SetKeyDown(kb::input::InputKey::MouseLeft, true);
    static_cast<void>(scene.Runtime().Update(0.0F));
    input.SetKeyDown(kb::input::InputKey::MouseLeft, false);
    static_cast<void>(scene.Runtime().Update(0.0F));
    bool smallViewportClick = false;
    for (const kb::scene::UIRuntimeEventRecord& event : scene.UIDocuments().DrainEvents()) {
        smallViewportClick = smallViewportClick ||
                             (event.event.kind == kb::scene::UIRuntimeEventKind::Click && event.event.elementId == 3U);
    }
    const kb::scene::UIPresentationSnapshot& smallInputPresentation =
        static_cast<const kb::scene::Scene&>(scene).UIDocuments().Presentation();
    Require(smallViewportClick && smallInputPresentation.viewportWidth == 400U &&
                smallInputPresentation.viewportHeight == 300U,
            "UI input used the last rendered viewport instead of its active pointer viewport");

    input.SetPointerViewportExtent(1600U, 1200U);
    static_cast<void>(scene.UIDocuments().BuildPresentation(400U, 300U));
    input.SetPointerPosition(500.0F, 260.0F);
    input.SetKeyDown(kb::input::InputKey::MouseLeft, true);
    static_cast<void>(scene.Runtime().Update(0.0F));
    input.SetKeyDown(kb::input::InputKey::MouseLeft, false);
    static_cast<void>(scene.Runtime().Update(0.0F));
    bool largeViewportClick = false;
    for (const kb::scene::UIRuntimeEventRecord& event : scene.UIDocuments().DrainEvents()) {
        largeViewportClick = largeViewportClick ||
                             (event.event.kind == kb::scene::UIRuntimeEventKind::Click && event.event.elementId == 3U);
    }
    const kb::scene::UIPresentationSnapshot& largeInputPresentation =
        static_cast<const kb::scene::Scene&>(scene).UIDocuments().Presentation();
    Require(largeViewportClick && largeInputPresentation.viewportWidth == 1600U &&
                largeInputPresentation.viewportHeight == 1200U,
            "UI input did not rebuild canonical geometry after switching pointer viewports");
    input.SetPointerViewportExtent(800U, 600U);

    Require(scene.UIDocuments().QueueFocus(owner.Entity(), 4U),
            "Authored-order navigation fixture could not focus its starting element");
    input.SetKeyDown(kb::input::InputKey::Tab, true);
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().Focused(owner.Entity()) == 50U,
            "Keyboard navigation followed element ids instead of authored sibling order");
    input.SetKeyDown(kb::input::InputKey::Tab, false);
    static_cast<void>(scene.Runtime().Update(0.0F));
    input.SetPointerPosition(130.0F, 130.0F);
    input.SetKeyDown(kb::input::InputKey::MouseLeft, true);
    static_cast<void>(scene.Runtime().Update(0.0F));
    input.SetKeyDown(kb::input::InputKey::MouseLeft, false);
    static_cast<void>(scene.Runtime().Update(0.0F));
    const auto toggle = scene.UIDocuments().Control(owner.Entity(), 50U);
    const std::vector<kb::scene::UIRuntimeEventRecord> pointerEvents = scene.UIDocuments().DrainEvents();
    bool namedToggleClick = false;
    for (const kb::scene::UIRuntimeEventRecord& event : pointerEvents) {
        namedToggleClick = namedToggleClick || (event.event.kind == kb::scene::UIRuntimeEventKind::Click &&
                                                event.event.elementId == 50U && event.event.eventName == "Menu.Toggle");
    }
    Require(toggle.has_value() && toggle->toggleValue && namedToggleClick,
            "Topmost hit testing did not activate the rendered Toggle or preserve its script action");

    Require(scene.UIDocuments().QueueFocus(owner.Entity(), 6U), "InputField could not receive text focus");
    Require(scene.UIDocuments().HasFocusedTextInput(),
            "Platform host could not observe the active InputField IME request");
    const std::array<char32_t, 3U> text{U'\u017B', U'\u00F3', U'\u0142'};
    static_cast<void>(input.SetTextInput(text));
    static_cast<void>(scene.Runtime().Update(0.0F));
    input.ClearTextInput();
    const auto inputField = scene.UIDocuments().Control(owner.Entity(), 6U);
    Require(inputField.has_value() && inputField->text == "\xC5\xBB\xC3\xB3\xC5\x82",
            "Unicode text input did not update the focused InputField");

    const std::array<char32_t, 1U> backspaceText{U'\b'};
    static_cast<void>(input.SetTextInput(backspaceText));
    input.SetKeyDown(kb::input::InputKey::Backspace, true);
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().Control(owner.Entity(), 6U)->text == "\xC5\xBB\xC3\xB3",
            "InputField Backspace retained its control character or failed to remove the previous glyph");
    input.ClearTextInput();
    input.SetKeyDown(kb::input::InputKey::Backspace, false);
    static_cast<void>(scene.Runtime().Update(0.0F));

    const std::array<char32_t, 1U> enterText{U'\r'};
    static_cast<void>(input.SetTextInput(enterText));
    input.SetKeyDown(kb::input::InputKey::Enter, true);
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().Control(owner.Entity(), 6U)->text == "\xC5\xBB\xC3\xB3",
            "Single-line InputField inserted the Enter control character");
    input.ClearTextInput();
    input.SetKeyDown(kb::input::InputKey::Enter, false);
    static_cast<void>(scene.Runtime().Update(0.0F));

    const std::array<char32_t, 1U> tabText{U'\t'};
    static_cast<void>(input.SetTextInput(tabText));
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().Control(owner.Entity(), 6U)->text == "\xC5\xBB\xC3\xB3",
            "Single-line InputField inserted the Tab control character");
    input.ClearTextInput();

    kb::script::ScriptRuntimeHost scripts{scene};
    std::size_t namedActionDeliveries = 0U;
    const kb::script::EventSubscriptionHandle actionSubscription = scripts.Runtime().Events().Subscribe(
        "Menu.Play", [&namedActionDeliveries](const kb::script::ScriptEvent&) { ++namedActionDeliveries; },
        owner.Entity());
    Require(scripts.Succeeded() && actionSubscription != kb::script::kInvalidEventSubscriptionHandle &&
                scripts.InstallSceneSystem(),
            "UI script action test could not initialize");
    input.SetPointerPosition(250.0F, 130.0F);
    input.SetKeyDown(kb::input::InputKey::MouseLeft, true);
    static_cast<void>(scene.Runtime().Update(0.0F));
    input.SetKeyDown(kb::input::InputKey::MouseLeft, false);
    static_cast<void>(scene.Runtime().Update(0.0F));
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(namedActionDeliveries == 1U,
            "Physical UI input did not dispatch its authored action to the script event bus");
    const kb::script::ScriptFunctionCallContext context{
        .scene = &scene,
        .caller = owner.Entity(),
    };
    const std::vector<kb::script::ScriptFunctionArgument> setRectArguments{
        Argument("element", kb::script::ScriptValue{std::uint64_t{6U}, kb::script::ScriptValueType::Hash}),
        Argument("offsetMinX", kb::script::ScriptValue{200.0F}),
        Argument("offsetMaxX", kb::script::ScriptValue{500.0F}),
        Argument("rotation", kb::script::ScriptValue{5.0F}),
    };
    Require(scripts.Functions().Call("UI.SetRect", setRectArguments, context).Succeeded(),
            "UI.SetRect did not reach the production runtime command queue");
    static_cast<void>(scene.Runtime().Update(0.0F));
    const kb::scene::UIPresentationItem* movedInput = FindItem(scene.UIDocuments().BuildPresentation(800U, 600U), 6U);
    Require(movedInput != nullptr && movedInput->rect.left == 200.0F && movedInput->rect.right == 500.0F &&
                movedInput->rotationDegrees == 5.0F,
            "Scripted RectTransform did not affect the shared runtime presentation");

    std::vector<kb::script::ScriptFunctionArgument> invalidRect = setRectArguments;
    invalidRect[1U] = Argument("anchorMinX", kb::script::ScriptValue{2.0F});
    Require(!scripts.Functions().Call("UI.SetRect", invalidRect, context).Succeeded(),
            "UI.SetRect accepted an anchor outside the canonical component contract");

    std::filesystem::remove_all(root);
}

} // namespace kb::tests
