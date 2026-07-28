#include "TestSupport.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneUIDocuments.hpp"
#include "engine/scene/UIAssetIO.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace kb::tests {

void RunUIDocumentRuntimeTests() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb-ui-document-runtime-tests";
    std::filesystem::remove_all(root);
    const std::filesystem::path stylePath = root / "Assets" / "UI" / "Default.kbuistyle";
    const std::filesystem::path documentPath = root / "Assets" / "UI" / "Hud.kbui";
    std::filesystem::create_directories(stylePath.parent_path());
    Require(kb::scene::UIAssetIO::SaveStyle(stylePath, kb::scene::UIStyleAsset{
        .name = "Default",
        .classes = { "hud", "label" },
    }), "UIStyle production asset could not be saved");

    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(root), "UI document project mount failed");
    Require(scene.Assets().Discover() == 1U, "UI style discovery failed");
    const auto* styleMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/UI/Default.kbuistyle");
    Require(styleMetadata != nullptr && styleMetadata->type == kb::scene::kUIStyleAssetType,
        "UI style asset was not classified by the production registry");
    const kb::assets::AssetId styleAssetId = styleMetadata->id;
    const kb::scene::UIDocument document{
        .styleAssetId = styleAssetId.value,
        .elements = {
            { .id = 1U, .parentId = 0U, .name = "HUD", .styleClass = "hud", .visible = true },
            { .id = 2U, .parentId = 1U, .name = "Score", .styleClass = "label", .visible = true,
              .control = { .kind = kb::scene::UIControlKind::Text, .text = "Score: 0" } },
        },
        .bindings = {
            { .elementId = 2U, .property = "text", .sourcePath = "player.score",
              .valueType = kb::scene::UIDataValueType::Number,
              .direction = kb::scene::UIBindingDirection::OneWay },
        },
    };
    Require(kb::scene::UIAssetIO::SaveDocument(documentPath, document), "UIDocument production asset could not be saved");
    Require(scene.Assets().Discover() == 2U, "UIDocument discovery failed");
    const auto* documentMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/UI/Hud.kbui");
    Require(documentMetadata != nullptr && documentMetadata->type == kb::scene::kUIDocumentAssetType &&
            documentMetadata->dependencies.size() == 1U && documentMetadata->dependencies.front() == styleAssetId,
        "UIDocument did not register its UIStyle dependency");

    const kb::scene::SceneObject owner = scene.Entities().CreateObject({ .name = "HUD Owner" });
    scene.Components().UIDocuments().Set(owner.Entity(), kb::scene::UIDocumentComponent{
        .documentAssetId = documentMetadata->id.value,
        .enabled = true,
    });
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().Exists(owner.Entity()) &&
            scene.UIDocuments().Asset(owner.Entity()) == documentMetadata->id.value &&
            scene.UIDocuments().Root(owner.Entity()) == 1U &&
            scene.UIDocuments().ElementCount(owner.Entity()) == 2U &&
            scene.UIDocuments().HasElement(owner.Entity(), 2U) &&
            scene.UIDocuments().StyleIsResolved(owner.Entity()),
        "Scene runtime did not derive the UI tree from UIDocument and UIStyle assets");

    const auto transientPanel = scene.UIDocuments().QueueCreate(owner.Entity(), kb::scene::UIRuntimeElementDesc{
        .parentId = 2U,
        .name = "TransientPanel",
        .styleClass = "hud",
        .visible = true,
    });
    Require(transientPanel.has_value() && !scene.UIDocuments().HasElement(owner.Entity(), *transientPanel) &&
            scene.UIDocuments().QueueHide(owner.Entity(), 2U),
        "UI mutations were not accepted through the deferred runtime queue");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().HasElement(owner.Entity(), *transientPanel) &&
            scene.UIDocuments().ElementCount(owner.Entity()) == 3U &&
            !scene.UIDocuments().Visible(owner.Entity(), 2U),
        "UI command FIFO did not apply create/hide at its frame boundary");
    Require(scene.UIDocuments().QueueDestroy(owner.Entity(), *transientPanel) &&
            scene.UIDocuments().QueueShow(owner.Entity(), 2U),
        "UI destroy/show commands were not accepted for a live runtime element");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(!scene.UIDocuments().HasElement(owner.Entity(), *transientPanel) &&
            scene.UIDocuments().ElementCount(owner.Entity()) == 2U &&
            scene.UIDocuments().Visible(owner.Entity(), 2U),
        "UI command FIFO did not apply destroy/show at its frame boundary");

    const std::filesystem::path luaPath = root / "Assets" / "Logic" / "UIQueue.lua";
    std::filesystem::create_directories(luaPath.parent_path());
    std::ofstream luaFile{ luaPath, std::ios::binary | std::ios::trunc };
    luaFile << R"(
local phase = 0
local element = 0
local clickSubscription = 0

function Tick(self, dt)
    if phase == 0 then
        element = UI.Create(1, "LuaPanel", { styleClass = "hud", visible = true, kind = "ModalDialog", text = "Pause", modal = true })
        UI.Hide(2)
        phase = 1
    elseif phase == 1 then
        UI.Destroy(element)
        UI.Show(2)
        phase = 2
    elseif phase == 2 then
        clickSubscription = Events.Subscribe("UI.Click", function(event)
            SetShared("uiClickElement", event.args.element)
            SetShared("uiClickX", event.args.x)
            UI.SetText(2, "Clicked")
        end)
        UI.EmitClick(2, 12.5, -4.0)
        phase = 3
    elseif phase == 3 then
        SetShared("uiClickUnsubscribed", Events.Unsubscribe(clickSubscription))
        UI.EmitClick(2, 0.0, 0.0)
        phase = 4
    end
end
)";
    luaFile.close();
    Require(luaFile.good() && scene.Assets().Discover() == 3U, "UI queue Lua behaviour was not discovered as a production script asset");
    const auto* luaMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/UIQueue.lua");
    Require(luaMetadata != nullptr, "UI queue Lua behaviour was not registered by the project asset registry");
    scene.Components().Behaviours().Set(owner.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = luaMetadata->id.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });
    kb::script::ScriptRuntimeHost scriptHost{ scene };
    Require(scriptHost.Succeeded() && scriptHost.InstallSceneSystem(), "UI script runtime host could not install the UI library module");
    for (const char* const function : { "UI.SetText", "UI.SetImage", "UI.SetToggle", "UI.SetSlider", "UI.ListAppend", "UI.ListClear", "UI.SetScrollOffset", "UI.SetModalOpen",
             "UI.EmitClick", "UI.EmitPointer", "UI.EmitSubmit", "UI.EmitChanged", "UI.EmitFocus", "UI.EmitNavigation" }) {
        Require(scriptHost.Functions().FindSignature(function) != nullptr,
            "UI control script API was not registered in the production runtime host");
    }
    static_cast<void>(scene.Runtime().Update(0.0F));
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().ElementCount(owner.Entity()) == 3U && !scene.UIDocuments().Visible(owner.Entity(), 2U) &&
            scene.UIDocuments().Control(owner.Entity(), 4U)->kind == kb::scene::UIControlKind::ModalDialog &&
            scene.UIDocuments().Control(owner.Entity(), 4U)->modalOpen,
        "Lua UI.Create/UI.Hide did not reach the queued scene runtime tree");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().ElementCount(owner.Entity()) == 2U && scene.UIDocuments().Visible(owner.Entity(), 2U),
        "Lua UI.Destroy/UI.Show did not reach the queued scene runtime tree");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().Control(owner.Entity(), 2U)->text == "Clicked",
        "Lua Events.Subscribe did not receive the queued UI.Click callback");
    Require(scriptHost.SharedState().Get("uiClickElement").value_or(kb::script::ScriptValue{ 0 }).AsInt() == 2,
        "Lua UI.Click did not preserve the UI element argument");
    Require(scriptHost.SharedState().Get("uiClickX").value_or(kb::script::ScriptValue{ 0.0F }).AsFloat() == 12.5F,
        "Lua UI.Click did not preserve the pointer coordinate");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scriptHost.SharedState().Get("uiClickUnsubscribed").value_or(kb::script::ScriptValue{ false }).AsBool() &&
            scene.UIDocuments().Control(owner.Entity(), 2U)->text == "Clicked",
        "Lua Events.Unsubscribe did not prevent a later UI.Click callback");

    const std::array<kb::scene::UIControlState, 9U> controls{
        kb::scene::UIControlState{ .kind = kb::scene::UIControlKind::Text, .text = "Score: 42" },
        kb::scene::UIControlState{ .kind = kb::scene::UIControlKind::Image, .imageAssetId = 17U },
        kb::scene::UIControlState{ .kind = kb::scene::UIControlKind::Button, .text = "Continue" },
        kb::scene::UIControlState{ .kind = kb::scene::UIControlKind::Toggle, .toggleValue = true },
        kb::scene::UIControlState{ .kind = kb::scene::UIControlKind::Slider, .sliderValue = 5.0F, .sliderMinimum = 1.0F, .sliderMaximum = 10.0F },
        kb::scene::UIControlState{ .kind = kb::scene::UIControlKind::List, .listItems = { "One", "Two" } },
        kb::scene::UIControlState{ .kind = kb::scene::UIControlKind::InputField, .text = "Player" },
        kb::scene::UIControlState{ .kind = kb::scene::UIControlKind::ScrollView, .scrollOffset = 3.0F },
        kb::scene::UIControlState{ .kind = kb::scene::UIControlKind::ModalDialog, .modalOpen = true },
    };
    std::array<kb::scene::UIElementId, controls.size()> controlElements{};
    for (std::size_t index = 0U; index < controls.size(); ++index) {
        const auto created = scene.UIDocuments().QueueCreate(owner.Entity(), kb::scene::UIRuntimeElementDesc{
            .parentId = 1U,
            .name = "Control" + std::to_string(index),
            .control = controls[index],
        });
        Require(created.has_value(), "Runtime UI control creation was rejected");
        controlElements[index] = *created;
    }
    static_cast<void>(scene.Runtime().Update(0.0F));
    for (std::size_t index = 0U; index < controls.size(); ++index) {
        const auto state = scene.UIDocuments().Control(owner.Entity(), controlElements[index]);
        Require(state.has_value() && state->kind == controls[index].kind,
            "Queued runtime UI control did not preserve its typed control state");
    }
    Require(scene.UIDocuments().Control(owner.Entity(), controlElements[0U])->text == "Score: 42" &&
            scene.UIDocuments().Control(owner.Entity(), controlElements[1U])->imageAssetId == 17U &&
            scene.UIDocuments().Control(owner.Entity(), controlElements[3U])->toggleValue &&
            scene.UIDocuments().Control(owner.Entity(), controlElements[4U])->sliderValue == 5.0F &&
            scene.UIDocuments().Control(owner.Entity(), controlElements[5U])->listItems.size() == 2U &&
            scene.UIDocuments().Control(owner.Entity(), controlElements[7U])->scrollOffset == 3.0F &&
            scene.UIDocuments().Control(owner.Entity(), controlElements[8U])->modalOpen,
        "Runtime UI controls lost their type-specific values");
    kb::scene::UIControlState changedText = *scene.UIDocuments().Control(owner.Entity(), controlElements[0U]);
    changedText.text = "Score: 43";
    Require(scene.UIDocuments().QueueSetControl(owner.Entity(), controlElements[0U], changedText),
        "UI control mutation command was rejected");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().Control(owner.Entity(), controlElements[0U])->text == "Score: 43",
        "UI control mutation command did not update the canonical runtime tree");

    const std::array<kb::scene::UIRuntimeEvent, 6U> events{
        kb::scene::UIRuntimeEvent{ .kind = kb::scene::UIRuntimeEventKind::Click, .elementId = 2U, .pointerX = 1.0F, .pointerY = 2.0F },
        kb::scene::UIRuntimeEvent{ .kind = kb::scene::UIRuntimeEventKind::Pointer, .elementId = 2U, .pointerX = 3.0F, .pointerY = 4.0F },
        kb::scene::UIRuntimeEvent{ .kind = kb::scene::UIRuntimeEventKind::Submit, .elementId = 2U, .text = "Ready" },
        kb::scene::UIRuntimeEvent{ .kind = kb::scene::UIRuntimeEventKind::Changed, .elementId = 2U, .value = 0.75F },
        kb::scene::UIRuntimeEvent{ .kind = kb::scene::UIRuntimeEventKind::Focus, .elementId = 2U, .focused = true },
        kb::scene::UIRuntimeEvent{ .kind = kb::scene::UIRuntimeEventKind::Navigation, .elementId = 2U, .navigation = kb::scene::UINavigationDirection::Next },
    };
    const std::array<const char*, events.size()> eventNames{ "UI.Click", "UI.Pointer", "UI.Submit", "UI.Changed", "UI.Focus", "UI.Navigation" };
    std::array<std::size_t, events.size()> deliveries{};
    std::array<bool, events.size()> payloadsValid{};
    std::vector<kb::script::EventSubscriptionHandle> subscriptions;
    subscriptions.reserve(events.size());
    for (std::size_t index = 0U; index < events.size(); ++index) {
        subscriptions.push_back(scriptHost.Runtime().Events().Subscribe(eventNames[index], [&, index](const kb::script::ScriptEvent& event) {
            ++deliveries[index];
            payloadsValid[index] = event.sender == owner.Entity() && event.target == owner.Entity() &&
                event.arguments.size() >= 3U && event.arguments[0U].name == "owner" &&
                event.arguments[0U].value.AsUInt64() == owner.Entity().Id() && event.arguments[1U].name == "element" &&
                event.arguments[1U].value.AsUInt64() == 2U;
        }, owner.Entity()));
        Require(subscriptions.back() != kb::script::kInvalidEventSubscriptionHandle,
            "UI event subscription was rejected by ScriptEventBus");
        Require(scene.UIDocuments().QueueEvent(owner.Entity(), events[index]),
            "A valid typed UI interaction was rejected by the scene event queue");
    }
    static_cast<void>(scene.Runtime().Update(0.0F));
    for (std::size_t index = 0U; index < events.size(); ++index) {
        Require(deliveries[index] == 1U && payloadsValid[index],
            "A typed UI event was not delivered exactly once through ScriptEventBus with its document owner");
        Require(scriptHost.Runtime().Events().Unsubscribe(subscriptions[index]),
            "A UI event listener could not be explicitly unsubscribed");
    }
    Require(scene.UIDocuments().QueueEvent(owner.Entity(), events[0U]), "UI event setup for explicit unsubscribe verification was rejected");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(deliveries[0U] == 1U, "An explicitly unsubscribed UI listener received a later event");
    const kb::scene::SceneObject deactivatedListener = scene.Entities().CreateObject({ .name = "Deactivated UI Listener" });
    std::size_t deactivatedDeliveries = 0U;
    const kb::script::EventSubscriptionHandle deactivatedHandle = scriptHost.Runtime().Events().Subscribe("UI.Click",
        [&deactivatedDeliveries](const kb::script::ScriptEvent&) { ++deactivatedDeliveries; }, deactivatedListener.Entity());
    scene.Entities().SetActive(deactivatedListener.Entity(), false);
    Require(deactivatedHandle != kb::script::kInvalidEventSubscriptionHandle &&
            scene.UIDocuments().QueueEvent(owner.Entity(), events[0U]),
        "UI event owner-lifetime setup was rejected");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(deactivatedDeliveries == 0U && !scriptHost.Runtime().Events().Unsubscribe(deactivatedHandle),
        "A deactivated UI event subscription owner was not automatically released");
    scene.Entities().Destroy(deactivatedListener.Entity());

    kb::scene::UIControlState restoredText = *scene.UIDocuments().Control(owner.Entity(), 2U);
    restoredText.text = "Score: 0";
    Require(scene.UIDocuments().QueueSetControl(owner.Entity(), 2U, restoredText),
        "UI text restore before scene serialization was rejected");
    static_cast<void>(scene.Runtime().Update(0.0F));

    const std::filesystem::path scenePath = root / "UIDocument.21kbscene";
    Require(kb::scene::SceneDocumentService::Save(scene, scenePath, "UIDocument"),
        "Scene document with UIDocument component could not be saved");
    kb::scene::Scene loaded;
    Require(loaded.Assets().MountProject(root) && loaded.Assets().Discover() == 3U &&
            kb::scene::SceneDocumentService::LoadFileIntoScene(loaded, scenePath),
        "Scene document with UIDocument component could not be reloaded");
    static_cast<void>(loaded.Runtime().Update(0.0F));
    const auto roots = loaded.Hierarchy().RootEntities();
    Require(roots.size() == 1U && loaded.Components().UIDocuments().TryGet(roots.front()) != nullptr &&
            loaded.UIDocuments().Exists(roots.front()) && loaded.UIDocuments().Root(roots.front()) == 1U &&
            loaded.UIDocuments().StyleIsResolved(roots.front()) &&
            loaded.UIDocuments().Control(roots.front(), 2U)->kind == kb::scene::UIControlKind::Text &&
            loaded.UIDocuments().Control(roots.front(), 2U)->text == "Score: 0",
        "Project -> scene reload -> UIDocument component -> UI runtime lost its canonical document tree");
    std::filesystem::remove_all(root);
}

} // namespace kb::tests
