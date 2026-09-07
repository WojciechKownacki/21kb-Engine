#include "TestSupport.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneUIDocuments.hpp"
#include "engine/scene/UIAssetIO.hpp"
#include "engine/scene/UIAssetLoaders.hpp"
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
            { .id = 1U, .parentId = 0U, .name = "HUD", .styleClass = "hud", .visible = true,
              .canvas = kb::scene::UICanvas{}, .control = { .kind = kb::scene::UIControlKind::Canvas } },
            { .id = 2U, .parentId = 1U, .name = "Score", .styleClass = "label", .visible = true,
              .textStyle = kb::scene::UIText{},
              .control = { .kind = kb::scene::UIControlKind::Text, .text = "Score: 0" } },
        },
        .bindings = {
            { .elementId = 2U, .property = "text", .sourcePath = "player.score",
              .valueType = kb::scene::UIDataValueType::Number,
              .direction = kb::scene::UIBindingDirection::OneWay },
        },
    };
    Require(kb::scene::UIAssetIO::SaveDocument(documentPath, document), "UIDocument production asset could not be saved");

    const std::string v1Text =
        "schema 1\n"
        "style 0\n"
        "element 1 0 \"LegacyRoot\" \"\" true\n"
        "control 1 Container \"\" 0 false 0 0 1 0 false 0\n"
        "element 2 1 \"LegacyLabel\" \"\" true\n"
        "control 2 Text \"Legacy\" 0 false 0 0 1 0 false 0\n"
        "element 3 1 \"LegacyImage\" \"\" true\n"
        "control 3 Image \"\" 17 false 0 0 1 0 false 0\n";
    const auto migrated = kb::scene::UIAssetIO::LoadDocument(std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(v1Text.data()), v1Text.size() });
    Require(migrated.has_value() && migrated->schemaVersion == kb::scene::UIDocument::kSchemaVersion &&
            migrated->elements.size() == 3U && migrated->elements[0U].control.kind == kb::scene::UIControlKind::Canvas &&
            migrated->elements[0U].canvas.has_value() && migrated->elements[1U].siblingOrder == 0U &&
            migrated->elements[2U].siblingOrder == 1U && migrated->elements[1U].textStyle.has_value() &&
            migrated->elements[2U].image.has_value() && migrated->elements[2U].image->imageAssetId == 17U,
        "Schema 1 UI document was not migrated into the canonical schema 2 component model");

    const std::filesystem::path componentDocumentPath = root / "ComponentModel.kbui";
    const std::filesystem::path componentRoundTripPath = root / "ComponentModelRoundTrip.kbui";
    constexpr std::uint64_t imageAssetId = 7001U;
    constexpr std::uint64_t fontAssetId = 7002U;
    const kb::scene::UIDocument componentDocument{
        .styleAssetId = styleAssetId.value,
        .elements = {
            { .id = 10U, .parentId = 0U, .siblingOrder = 0U, .name = "Canvas", .visible = true,
              .rect = { .anchorMin = { 0.0F, 0.0F }, .anchorMax = { 1.0F, 1.0F }, .offsetMin = {}, .offsetMax = {},
                  .pivot = { 0.5F, 0.5F }, .scale = { 1.0F, 1.0F }, .rotationDegrees = 0.0F, .zOrder = -2 },
              .canvas = kb::scene::UICanvas{ .scaleMode = kb::scene::UICanvasScaleMode::ScaleWithScreenSize,
                  .referenceResolution = { 2560.0F, 1440.0F }, .scaleFactor = 1.25F, .matchWidthOrHeight = 0.6F },
              .layout = kb::scene::UIContainerLayout{ .mode = kb::scene::UIContainerLayoutMode::Vertical,
                  .padding = { 8.0F, 9.0F, 10.0F, 11.0F }, .spacing = { 3.0F, 4.0F },
                  .horizontalAlignment = kb::scene::UIAlignment::Stretch,
                  .verticalAlignment = kb::scene::UIAlignment::Center, .cellSize = { 120.0F, 36.0F }, .columns = 2U },
              .control = { .kind = kb::scene::UIControlKind::Canvas } },
            { .id = 11U, .parentId = 10U, .siblingOrder = 0U, .name = "Portrait", .visible = true,
              .rect = { .anchorMin = { 0.1F, 0.2F }, .anchorMax = { 0.3F, 0.5F }, .offsetMin = { 1.0F, 2.0F },
                  .offsetMax = { 101.0F, 202.0F }, .pivot = { 0.25F, 0.75F }, .scale = { 1.5F, 0.75F },
                  .rotationDegrees = 12.0F, .zOrder = 3 },
              .paint = kb::scene::UIPaint{ .backgroundColor = { 0.1F, 0.2F, 0.3F, 0.4F },
                  .borderColor = { 0.5F, 0.6F, 0.7F, 0.8F }, .borderWidth = { 1.0F, 2.0F, 3.0F, 4.0F },
                  .cornerRadius = { 5.0F, 6.0F, 7.0F, 8.0F }, .opacity = 0.9F },
              .image = kb::scene::UIImage{ .imageAssetId = imageAssetId, .uvRect = { 0.1F, 0.2F, 0.7F, 0.6F },
                  .scaleMode = kb::scene::UIImageScaleMode::Contain, .preserveAspect = true,
                  .nineSlice = { 2.0F, 3.0F, 4.0F, 5.0F } },
              .effects = kb::scene::UIEffects{ .clipChildren = true, .mask = true, .shadowEnabled = true,
                  .shadowOffset = { 4.0F, 5.0F }, .shadowColor = { 0.1F, 0.1F, 0.1F, 0.1F }, .shadowBlur = 6.0F,
                  .outlineEnabled = true, .outlineColor = { 0.9F, 0.8F, 0.7F, 1.0F }, .outlineWidth = 2.0F,
                  .backgroundBlur = 8.0F },
              .control = { .kind = kb::scene::UIControlKind::Image } },
            { .id = 12U, .parentId = 10U, .siblingOrder = 1U, .name = "Caption", .visible = true,
              .textStyle = kb::scene::UIText{ .fontAssetId = fontAssetId, .fontSize = 28.0F,
                  .color = { 0.8F, 0.7F, 0.6F, 1.0F },
                  .horizontalAlignment = kb::scene::UITextHorizontalAlignment::Center,
                  .verticalAlignment = kb::scene::UITextVerticalAlignment::Bottom,
                  .wrapMode = kb::scene::UITextWrapMode::Character },
              .interaction = kb::scene::UIInteraction{ .raycastTarget = true, .interactable = true,
                  .navigationMode = kb::scene::UINavigationMode::Explicit, .navigationLeft = 11U,
                  .eventName = "Caption.Activate" },
              .control = { .kind = kb::scene::UIControlKind::Text, .text = "Ready" } },
            { .id = 13U, .parentId = 10U, .siblingOrder = 2U, .name = "Actions", .visible = true,
              .layout = kb::scene::UIContainerLayout{ .mode = kb::scene::UIContainerLayoutMode::Horizontal },
              .control = { .kind = kb::scene::UIControlKind::HorizontalBox } },
            { .id = 14U, .parentId = 13U, .siblingOrder = 0U, .name = "Continue", .visible = true,
              .textStyle = kb::scene::UIText{ .fontAssetId = fontAssetId },
              .interaction = kb::scene::UIInteraction{ .eventName = "Menu.Continue" },
              .control = { .kind = kb::scene::UIControlKind::Button, .text = "Continue" } },
            { .id = 15U, .parentId = 13U, .siblingOrder = 1U, .name = "Quality", .visible = true,
              .textStyle = kb::scene::UIText{}, .interaction = kb::scene::UIInteraction{},
              .control = { .kind = kb::scene::UIControlKind::Dropdown,
                  .listItems = { "Low", "High" }, .selectedIndex = 1U } },
            { .id = 16U, .parentId = 10U, .siblingOrder = 3U, .name = "Pages", .visible = true,
              .control = { .kind = kb::scene::UIControlKind::WidgetSwitcher, .selectedIndex = 1U } },
            { .id = 17U, .parentId = 16U, .siblingOrder = 0U, .name = "FirstPage", .visible = true },
            { .id = 18U, .parentId = 16U, .siblingOrder = 1U, .name = "SecondPage", .visible = true },
            { .id = 19U, .parentId = 16U, .siblingOrder = 2U, .name = "RepeatedPortrait", .visible = true,
              .image = kb::scene::UIImage{ .imageAssetId = imageAssetId },
              .control = { .kind = kb::scene::UIControlKind::Image } },
        },
    };
    Require(kb::scene::UIAssetIO::SaveDocument(componentDocumentPath, componentDocument),
        "Schema 2 UI component document could not be saved");
    const auto componentRoundTrip = kb::scene::UIAssetIO::LoadDocument(componentDocumentPath);
    Require(componentRoundTrip.has_value() && componentRoundTrip->elements.size() == componentDocument.elements.size() &&
            componentRoundTrip->elements[0U].canvas->referenceResolution.x == 2560.0F &&
            componentRoundTrip->elements[0U].layout->padding.bottom == 11.0F &&
            componentRoundTrip->elements[1U].paint->cornerRadius.w == 8.0F &&
            componentRoundTrip->elements[1U].image->preserveAspect &&
            componentRoundTrip->elements[1U].effects->backgroundBlur == 8.0F &&
            componentRoundTrip->elements[2U].textStyle->fontAssetId == fontAssetId &&
            componentRoundTrip->elements[2U].interaction->eventName == "Caption.Activate" &&
            componentRoundTrip->elements[3U].layout->mode == kb::scene::UIContainerLayoutMode::Horizontal &&
            componentRoundTrip->elements[5U].control.selectedIndex == 1U &&
            componentRoundTrip->elements[6U].control.selectedIndex == 1U,
        "Schema 2 UI component document lost authored fields during load");
    Require(kb::scene::UIAssetIO::SaveDocument(componentRoundTripPath, *componentRoundTrip) &&
            kb::scene::UIAssetIO::LoadDocument(componentRoundTripPath).has_value(),
        "Loaded schema 2 UI component document could not be saved and loaded again");

    kb::scene::UIDocument invalidComponentDocument = componentDocument;
    invalidComponentDocument.elements[1U].rect.anchorMin.x = 1.1F;
    Require(!kb::scene::UIAssetIO::SaveDocument(root / "InvalidRect.kbui", invalidComponentDocument),
        "UI document writer accepted an anchor outside the normalized range");
    invalidComponentDocument = componentDocument;
    invalidComponentDocument.elements[2U].siblingOrder = 0U;
    Require(!kb::scene::UIAssetIO::SaveDocument(root / "InvalidOrder.kbui", invalidComponentDocument),
        "UI document writer accepted duplicate sibling order");
    invalidComponentDocument = componentDocument;
    invalidComponentDocument.elements[3U].layout->mode = kb::scene::UIContainerLayoutMode::Grid;
    Require(!kb::scene::UIAssetIO::SaveDocument(root / "InvalidComposition.kbui", invalidComponentDocument),
        "UI document writer accepted a layout component that contradicts its widget kind");
    invalidComponentDocument = componentDocument;
    invalidComponentDocument.elements[2U].control.text.assign(kb::scene::kMaxUITextBytes + 1U, 'x');
    Require(!kb::scene::UIAssetIO::SaveDocument(root / "InvalidTextSize.kbui", invalidComponentDocument),
        "UI document writer accepted control text above the canonical UI text limit");
    invalidComponentDocument = componentDocument;
    invalidComponentDocument.elements[5U].control.listItems[0U].assign(kb::scene::kMaxUITextBytes + 1U, 'x');
    Require(!kb::scene::UIAssetIO::SaveDocument(root / "InvalidItemSize.kbui", invalidComponentDocument),
        "UI document writer accepted a list item above the canonical UI text limit");

    kb::assets::AssetRegistry dependencyRegistry;
    Require(dependencyRegistry.Upsert(kb::assets::AssetMetadata{ .id = styleAssetId, .type = kb::scene::kUIStyleAssetType,
                .virtualPath = "/Game/UI/Default.kbuistyle" }) &&
            dependencyRegistry.Upsert(kb::assets::AssetMetadata{ .id = kb::assets::AssetId{ imageAssetId },
                .type = "ImportedAsset", .importCategory = "Texture", .virtualPath = "/Game/UI/Portrait.png" }) &&
            dependencyRegistry.Upsert(kb::assets::AssetMetadata{ .id = kb::assets::AssetId{ fontAssetId },
                .type = "ImportedAsset", .importCategory = "Font", .virtualPath = "/Game/UI/Body.ttf" }),
        "UI dependency fixture assets could not be registered");
    const kb::scene::UIDocumentAssetLoader documentLoader;
    const std::vector<kb::assets::AssetId> componentDependencies = documentLoader.DiscoverDependencies(
        kb::assets::AssetMetadata{ .id = kb::assets::AssetId{ 7999U }, .type = kb::scene::kUIDocumentAssetType,
            .virtualPath = "/Game/UI/ComponentModel.kbui", .physicalPath = componentDocumentPath }, dependencyRegistry);
    Require(componentDependencies == std::vector<kb::assets::AssetId>{ styleAssetId,
                kb::assets::AssetId{ imageAssetId }, kb::assets::AssetId{ fontAssetId } },
        "UI document dependency discovery did not retain unique style, image, and font assets");
    kb::assets::AssetRegistry wrongTypeDependencyRegistry;
    Require(wrongTypeDependencyRegistry.Upsert(kb::assets::AssetMetadata{
                .id = kb::assets::AssetId{ imageAssetId }, .type = "AudioClip",
                .importCategory = "Texture", .virtualPath = "/Game/UI/Portrait.wav" }),
        "Wrong-type UI dependency fixture could not be registered");
    Require(documentLoader.DiscoverDependencies(
                kb::assets::AssetMetadata{ .id = kb::assets::AssetId{ 7999U },
                    .type = kb::scene::kUIDocumentAssetType,
                    .virtualPath = "/Game/UI/ComponentModel.kbui", .physicalPath = componentDocumentPath },
                wrongTypeDependencyRegistry) == componentDependencies,
        "UI dependency discovery dropped a wrong-type reference from the cook graph");
    const std::optional<std::string> wrongTypeDiagnostic = documentLoader.ValidateDependencies(
        kb::assets::AssetMetadata{ .id = kb::assets::AssetId{ 7999U },
            .type = kb::scene::kUIDocumentAssetType,
            .virtualPath = "/Game/UI/ComponentModel.kbui", .physicalPath = componentDocumentPath },
        wrongTypeDependencyRegistry);
    Require(wrongTypeDiagnostic.has_value() &&
            wrongTypeDiagnostic->find("as an image") != std::string::npos &&
            wrongTypeDiagnostic->find("AudioClip") != std::string::npos,
        "UI dependency validation accepted an image reference with the wrong registered type");
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
            scene.UIDocuments().Find(owner.Entity(), "HUD") == std::optional<kb::scene::UIElementId>{ 1U } &&
            scene.UIDocuments().Find(owner.Entity(), "Score") == std::optional<kb::scene::UIElementId>{ 2U } &&
            !scene.UIDocuments().Find(owner.Entity(), "Missing").has_value() &&
            scene.UIDocuments().StyleIsResolved(owner.Entity()),
        "Scene runtime did not derive the UI tree from UIDocument and UIStyle assets");

    const auto transientPanel = scene.UIDocuments().QueueCreate(owner.Entity(), kb::scene::UIRuntimeElementDesc{
        .parentId = 2U,
        .name = "TransientPanel",
        .styleClass = "hud",
        .visible = true,
    });
    Require(transientPanel.has_value() && !scene.UIDocuments().HasElement(owner.Entity(), *transientPanel) &&
            !scene.UIDocuments().Find(owner.Entity(), "TransientPanel").has_value() &&
            scene.UIDocuments().QueueHide(owner.Entity(), 2U),
        "UI mutations were not accepted through the deferred runtime queue");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().HasElement(owner.Entity(), *transientPanel) &&
            scene.UIDocuments().ElementCount(owner.Entity()) == 3U &&
            scene.UIDocuments().Find(owner.Entity(), "TransientPanel") == transientPanel &&
            !scene.UIDocuments().Visible(owner.Entity(), 2U),
        "UI command FIFO did not apply create/hide at its frame boundary");
    Require(scene.UIDocuments().QueueDestroy(owner.Entity(), *transientPanel) &&
            scene.UIDocuments().QueueShow(owner.Entity(), 2U),
        "UI destroy/show commands were not accepted for a live runtime element");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(!scene.UIDocuments().HasElement(owner.Entity(), *transientPanel) &&
            scene.UIDocuments().ElementCount(owner.Entity()) == 2U &&
            !scene.UIDocuments().Find(owner.Entity(), "TransientPanel").has_value() &&
            scene.UIDocuments().Visible(owner.Entity(), 2U),
        "UI command FIFO did not apply destroy/show at its frame boundary");

    const std::filesystem::path luaPath = root / "Assets" / "Logic" / "UIQueue.lua";
    std::filesystem::create_directories(luaPath.parent_path());
    std::ofstream luaFile{ luaPath, std::ios::binary | std::ios::trunc };
    luaFile << R"(
local phase = 0
local element = 0
local clickSubscription = 0
local score = nil
local list = 0

function Tick(self, dt)
    if phase == 0 then
        score = UI.Find("Score")
        if score == nil then error("UI.Find setup lookup failed") end
        local kinds = {
            "Container", "Text", "Image", "Button", "Toggle", "Slider", "List",
            "InputField", "ScrollView", "ModalDialog", "Border", "Overlay",
            "HorizontalBox", "VerticalBox", "Grid", "Wrap", "Spacer", "SizeBox",
            "ScaleBox", "ProgressBar", "Dropdown", "Scrollbar", "WidgetSwitcher"
        }
        local button = 0
        local image = 0
        local layout = 0
        local dropdown = 0
        local switcher = 0
        local toggle = 0
        local slider = 0
        local scroll = 0
        local progress = 0
        local scrollbar = 0
        for _, kind in ipairs(kinds) do
            local created = UI.Create(1, "LuaKind" .. kind, {
                styleClass = "hud", visible = true, kind = kind,
                text = kind == "ModalDialog" and "Pause" or "",
                modal = kind == "ModalDialog"
            })
            if created == 0 then error("UI.Create rejected " .. kind) end
            if kind == "Button" then button = created end
            if kind == "Image" then image = created end
            if kind == "VerticalBox" then layout = created end
            if kind == "Dropdown" then dropdown = created end
            if kind == "WidgetSwitcher" then switcher = created end
            if kind == "Toggle" then toggle = created end
            if kind == "Slider" then slider = created end
            if kind == "List" then list = created end
            if kind == "ScrollView" then scroll = created end
            if kind == "ProgressBar" then progress = created end
            if kind == "Scrollbar" then scrollbar = created end
            if kind == "ModalDialog" then element = created end
        end
        UI.SetCanvas(1, { scaleMode = "ConstantPixelSize", referenceWidth = 1024,
            referenceHeight = 768, scaleFactor = 1.5, match = 0.25 })
        UI.SetRect(button, { anchorMinX = 0.1, anchorMinY = 0.2,
            anchorMaxX = 0.7, anchorMaxY = 0.8, offsetMinX = 3, offsetMinY = 4,
            offsetMaxX = 203, offsetMaxY = 84, pivotX = 0.25, pivotY = 0.75,
            scaleX = 1.25, scaleY = 0.75, rotation = 12, zOrder = 9 })
        UI.SetPaint(button, { red = 0.1, green = 0.2, blue = 0.3, alpha = 0.4,
            borderRed = 0.5, borderGreen = 0.6, borderBlue = 0.7, borderAlpha = 0.8,
            borderLeft = 1, borderTop = 2, borderRight = 3, borderBottom = 4,
            radiusTopLeft = 5, radiusTopRight = 6, radiusBottomRight = 7,
            radiusBottomLeft = 8, opacity = 0.9 })
        UI.SetImageStyle(button, { image = 4294967311 })
        UI.SetTextStyle(button, { font = 4294967312, fontSize = 24, red = 0.8, green = 0.7,
            blue = 0.6, alpha = 1, horizontalAlignment = "Center",
            verticalAlignment = "Bottom", wrap = "Character" })
        UI.SetInteraction(button, { raycastTarget = true, interactable = true,
            navigationMode = "Explicit", navigationUp = 2, navigationDown = 2,
            navigationLeft = 2, navigationRight = 2, eventName = "Lua.Button" })
        UI.SetEffects(button, { clipChildren = true, mask = true, shadowEnabled = true,
            shadowX = 2, shadowY = 3, shadowRed = 0.1, shadowGreen = 0.2,
            shadowBlue = 0.3, shadowAlpha = 0.4, shadowBlur = 5,
            outlineEnabled = true, outlineRed = 0.6, outlineGreen = 0.7,
            outlineBlue = 0.8, outlineAlpha = 0.9, outlineWidth = 2,
            backgroundBlur = 6 })
        UI.SetImage(image, 41)
        UI.SetImageStyle(image, { image = 42, uvX = 0.1, uvY = 0.2,
            uvWidth = 0.6, uvHeight = 0.7, scaleMode = "Contain",
            preserveAspect = true, sliceLeft = 1, sliceTop = 2,
            sliceRight = 3, sliceBottom = 4 })
        UI.SetImage(image, 43)
        UI.SetLayout(layout, { mode = "Vertical", paddingLeft = 1, paddingTop = 2,
            paddingRight = 3, paddingBottom = 4, spacingX = 5, spacingY = 6,
            horizontalAlignment = "Stretch", verticalAlignment = "Center",
            cellWidth = 120, cellHeight = 40, columns = 3 })
        UI.ListAppend(dropdown, "temporary")
        UI.ListClear(dropdown)
        UI.ListAppend(dropdown, "Low")
        UI.ListAppend(dropdown, "High")
        UI.SetSelected(dropdown, 1)
        UI.ListAppend(list, "One")
        UI.ListAppend(list, "Two")
        UI.SetToggle(toggle, true)
        UI.SetSlider(slider, 4, { minimum = 1, maximum = 5 })
        UI.SetSlider(progress, 0.75, { minimum = 0, maximum = 1 })
        UI.SetSlider(scrollbar, 0.5, { minimum = 0, maximum = 1 })
        UI.SetScrollOffset(scroll, 32)
        local pageA = UI.Create(switcher, "LuaPageA", { kind = "Container" })
        local pageB = UI.Create(switcher, "LuaPageB", { kind = "Container" })
        UI.SetSelected(switcher, 1)
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
            UI.SetText(score, "Clicked")
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
    const std::string scriptHostDiagnostic = scriptHost.Diagnostics().empty()
        ? "UI script runtime host could not install the UI library module"
        : "UI script runtime host could not install the UI library module: " + scriptHost.Diagnostics().front();
    Require(scriptHost.Succeeded() && scriptHost.InstallSceneSystem(), scriptHostDiagnostic.c_str());
    for (const char* const function : { "UI.Find", "UI.Focus", "UI.SetText", "UI.SetImage", "UI.SetToggle", "UI.SetSlider", "UI.SetSelected", "UI.ListAppend", "UI.ListClear", "UI.SetScrollOffset", "UI.SetModalOpen",
             "UI.SetRect", "UI.SetCanvas", "UI.SetLayout", "UI.SetPaint", "UI.SetImageStyle", "UI.SetTextStyle", "UI.SetInteraction", "UI.SetEffects",
             "UI.EmitClick", "UI.EmitPointer", "UI.EmitSubmit", "UI.EmitChanged", "UI.EmitFocus", "UI.EmitNavigation" }) {
        Require(scriptHost.Functions().FindSignature(function) != nullptr,
            "UI control script API was not registered in the production runtime host");
    }
    static_cast<void>(scene.Runtime().Update(0.0F));
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().ElementCount(owner.Entity()) == 27U &&
            !scene.UIDocuments().Visible(owner.Entity(), 2U),
        "Lua UI.Create/UI.Hide did not reach the queued scene runtime tree");
    for (const auto& [kind, name] : kb::scene::kUIControlKindNames) {
        if (kind == kb::scene::UIControlKind::Canvas) {
            Require(scene.UIDocuments().Control(owner.Entity(), 1U)->kind == kind,
                "The authored UI root did not retain the canonical Canvas kind");
            continue;
        }
        const auto created = scene.UIDocuments().Find(owner.Entity(), "LuaKind" + std::string{ name });
        Require(created.has_value() && scene.UIDocuments().Control(owner.Entity(), *created)->kind == kind,
            "A canonical UI control kind could not be created through the real Lua wrapper");
    }
    const auto luaButton = scene.UIDocuments().Find(owner.Entity(), "LuaKindButton");
    const auto luaImage = scene.UIDocuments().Find(owner.Entity(), "LuaKindImage");
    const auto luaLayout = scene.UIDocuments().Find(owner.Entity(), "LuaKindVerticalBox");
    const auto luaDropdown = scene.UIDocuments().Find(owner.Entity(), "LuaKindDropdown");
    const auto luaSwitcher = scene.UIDocuments().Find(owner.Entity(), "LuaKindWidgetSwitcher");
    const auto rootComponents = scene.UIDocuments().ElementComponents(owner.Entity(), 1U);
    const auto buttonComponents = scene.UIDocuments().ElementComponents(owner.Entity(), *luaButton);
    const auto imageComponents = scene.UIDocuments().ElementComponents(owner.Entity(), *luaImage);
    const auto layoutComponents = scene.UIDocuments().ElementComponents(owner.Entity(), *luaLayout);
    const kb::scene::UIRectTransform& luaRect = buttonComponents->rect;
    const kb::scene::UIPaint& luaPaint = *buttonComponents->paint;
    const kb::scene::UIText& luaText = *buttonComponents->textStyle;
    const kb::scene::UIInteraction& luaInteraction = *buttonComponents->interaction;
    const kb::scene::UIEffects& luaEffects = *buttonComponents->effects;
    const kb::scene::UIImage& luaImageStyle = *imageComponents->image;
    const kb::scene::UIContainerLayout& luaContainerLayout = *layoutComponents->layout;
    Require(rootComponents->canvas->scaleMode == kb::scene::UICanvasScaleMode::ConstantPixelSize &&
            rootComponents->canvas->referenceResolution.x == 1024.0F &&
            rootComponents->canvas->referenceResolution.y == 768.0F &&
            rootComponents->canvas->scaleFactor == 1.5F &&
            rootComponents->canvas->matchWidthOrHeight == 0.25F,
        "Real Lua Canvas setter did not mutate every canonical field");
    Require(luaRect.anchorMin.x == 0.1F && luaRect.anchorMin.y == 0.2F &&
            luaRect.anchorMax.x == 0.7F && luaRect.anchorMax.y == 0.8F &&
            luaRect.offsetMin.x == 3.0F && luaRect.offsetMin.y == 4.0F &&
            luaRect.offsetMax.x == 203.0F && luaRect.offsetMax.y == 84.0F &&
            luaRect.pivot.x == 0.25F && luaRect.pivot.y == 0.75F &&
            luaRect.scale.x == 1.25F && luaRect.scale.y == 0.75F &&
            luaRect.rotationDegrees == 12.0F && luaRect.zOrder == 9,
        "Real Lua RectTransform setter did not mutate every canonical field");
    Require(luaPaint.backgroundColor.r == 0.1F && luaPaint.backgroundColor.g == 0.2F &&
            luaPaint.backgroundColor.b == 0.3F && luaPaint.backgroundColor.a == 0.4F &&
            luaPaint.borderColor.r == 0.5F && luaPaint.borderColor.g == 0.6F &&
            luaPaint.borderColor.b == 0.7F && luaPaint.borderColor.a == 0.8F &&
            luaPaint.borderWidth.left == 1.0F && luaPaint.borderWidth.top == 2.0F &&
            luaPaint.borderWidth.right == 3.0F && luaPaint.borderWidth.bottom == 4.0F &&
            luaPaint.cornerRadius.x == 5.0F && luaPaint.cornerRadius.y == 6.0F &&
            luaPaint.cornerRadius.z == 7.0F && luaPaint.cornerRadius.w == 8.0F &&
            luaPaint.opacity == 0.9F,
        "Real Lua paint setter did not mutate every canonical field");
    Require(buttonComponents->image.has_value() &&
            buttonComponents->image->imageAssetId == 4294967311ULL,
        "A Lua-authored Button could not compose its own optional image component");
    Require(luaText.fontAssetId == 4294967312ULL && luaText.fontSize == 24.0F &&
            luaText.color.r == 0.8F && luaText.color.g == 0.7F &&
            luaText.color.b == 0.6F && luaText.color.a == 1.0F &&
            luaText.horizontalAlignment == kb::scene::UITextHorizontalAlignment::Center &&
            luaText.verticalAlignment == kb::scene::UITextVerticalAlignment::Bottom &&
            luaText.wrapMode == kb::scene::UITextWrapMode::Character,
        "Real Lua text-style setter did not mutate every canonical field");
    Require(luaInteraction.raycastTarget && luaInteraction.interactable &&
            luaInteraction.navigationMode == kb::scene::UINavigationMode::Explicit &&
            luaInteraction.navigationUp == 2U && luaInteraction.navigationDown == 2U &&
            luaInteraction.navigationLeft == 2U && luaInteraction.navigationRight == 2U &&
            luaInteraction.eventName == "Lua.Button",
        "Real Lua interaction setter did not mutate every canonical field");
    Require(luaEffects.clipChildren && luaEffects.mask && luaEffects.shadowEnabled &&
            luaEffects.shadowOffset.x == 2.0F && luaEffects.shadowOffset.y == 3.0F &&
            luaEffects.shadowColor.r == 0.1F && luaEffects.shadowColor.g == 0.2F &&
            luaEffects.shadowColor.b == 0.3F && luaEffects.shadowColor.a == 0.4F &&
            luaEffects.shadowBlur == 5.0F && luaEffects.outlineEnabled &&
            luaEffects.outlineColor.r == 0.6F && luaEffects.outlineColor.g == 0.7F &&
            luaEffects.outlineColor.b == 0.8F && luaEffects.outlineColor.a == 0.9F &&
            luaEffects.outlineWidth == 2.0F && luaEffects.backgroundBlur == 6.0F,
        "Real Lua effects setter did not mutate every canonical field");
    Require(luaImageStyle.imageAssetId == 43U && luaImageStyle.uvRect.x == 0.1F &&
            luaImageStyle.uvRect.y == 0.2F && luaImageStyle.uvRect.width == 0.6F &&
            luaImageStyle.uvRect.height == 0.7F &&
            luaImageStyle.scaleMode == kb::scene::UIImageScaleMode::Contain &&
            luaImageStyle.preserveAspect && luaImageStyle.nineSlice.left == 1.0F &&
            luaImageStyle.nineSlice.top == 2.0F && luaImageStyle.nineSlice.right == 3.0F &&
            luaImageStyle.nineSlice.bottom == 4.0F,
        "Real Lua image setters did not mutate the canonical image component");
    Require(luaContainerLayout.mode == kb::scene::UIContainerLayoutMode::Vertical &&
            luaContainerLayout.padding.left == 1.0F && luaContainerLayout.padding.top == 2.0F &&
            luaContainerLayout.padding.right == 3.0F && luaContainerLayout.padding.bottom == 4.0F &&
            luaContainerLayout.spacing.x == 5.0F && luaContainerLayout.spacing.y == 6.0F &&
            luaContainerLayout.horizontalAlignment == kb::scene::UIAlignment::Stretch &&
            luaContainerLayout.verticalAlignment == kb::scene::UIAlignment::Center &&
            luaContainerLayout.cellSize.x == 120.0F && luaContainerLayout.cellSize.y == 40.0F &&
            luaContainerLayout.columns == 3U,
        "Real Lua container-layout setter did not mutate every canonical field");
    Require(
            scene.UIDocuments().Control(owner.Entity(), *luaDropdown)->listItems ==
                std::vector<std::string>{ "Low", "High" } &&
            scene.UIDocuments().Control(owner.Entity(), *luaDropdown)->selectedIndex == 1U &&
            scene.UIDocuments().Control(owner.Entity(), *luaSwitcher)->selectedIndex == 1U,
        "Real Lua component setters did not mutate the canonical runtime UI tree end to end");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().ElementCount(owner.Entity()) == 26U && scene.UIDocuments().Visible(owner.Entity(), 2U),
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
        kb::scene::UIControlState{ .kind = kb::scene::UIControlKind::Image },
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
        kb::scene::UIElementComponents components{};
        if (controls[index].kind == kb::scene::UIControlKind::Image) {
            components.image = kb::scene::UIImage{ .imageAssetId = 17U };
        }
        const auto created = scene.UIDocuments().QueueCreate(owner.Entity(), kb::scene::UIRuntimeElementDesc{
            .parentId = 1U,
            .name = "Control" + std::to_string(index),
            .components = std::move(components),
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
            scene.UIDocuments().ElementComponents(owner.Entity(), controlElements[1U])->image->imageAssetId == 17U &&
            scene.UIDocuments().Control(owner.Entity(), controlElements[3U])->toggleValue &&
            scene.UIDocuments().Control(owner.Entity(), controlElements[4U])->sliderValue == 5.0F &&
            scene.UIDocuments().Control(owner.Entity(), controlElements[5U])->listItems.size() == 2U &&
            scene.UIDocuments().Control(owner.Entity(), controlElements[7U])->scrollOffset == 3.0F &&
            scene.UIDocuments().Control(owner.Entity(), controlElements[8U])->modalOpen,
        "Runtime UI controls lost their type-specific values");
    kb::scene::UIControlState virtualizedList = *scene.UIDocuments().Control(owner.Entity(), controlElements[5U]);
    virtualizedList.listItems.clear();
    virtualizedList.listItems.reserve(100U);
    for (std::uint32_t index = 0U; index < 100U; ++index) {
        virtualizedList.listItems.push_back("Item " + std::to_string(index));
    }
    const std::size_t treeCountBeforeVirtualization = scene.UIDocuments().ElementCount(owner.Entity());
    Require(scene.UIDocuments().QueueSetControl(owner.Entity(), controlElements[5U], virtualizedList) &&
            scene.UIDocuments().QueueConfigureVirtualList(owner.Entity(), controlElements[5U], 3U, 1U),
        "Virtual List setup was rejected by the canonical UI command queue");
    static_cast<void>(scene.Runtime().Update(0.0F));
    const auto initialVirtualList = scene.UIDocuments().VirtualList(owner.Entity(), controlElements[5U]);
    Require(initialVirtualList.has_value() && initialVirtualList->totalItemCount == 100U &&
            initialVirtualList->firstVisibleIndex == 0U && initialVirtualList->pooledItems.size() == 4U &&
            initialVirtualList->pooledItems[0U].index == 0U && initialVirtualList->pooledItems[3U].index == 3U &&
            scene.UIDocuments().ElementCount(owner.Entity()) == treeCountBeforeVirtualization,
        "Virtual List created UI tree elements instead of a bounded initial slot pool");
    Require(scene.UIDocuments().QueueScrollVirtualListTo(owner.Entity(), controlElements[5U], 50U),
        "Virtual List viewport scroll command was rejected");
    static_cast<void>(scene.Runtime().Update(0.0F));
    const auto scrolledVirtualList = scene.UIDocuments().VirtualList(owner.Entity(), controlElements[5U]);
    Require(scrolledVirtualList.has_value() && scrolledVirtualList->firstVisibleIndex == 50U &&
            scrolledVirtualList->pooledItems.size() == 5U && scrolledVirtualList->pooledItems[0U].index == 49U &&
            scrolledVirtualList->pooledItems[4U].index == 53U && scrolledVirtualList->pooledItems[0U].text == "Item 49",
        "Virtual List did not rebind the expected visible range after scrolling");
    const kb::scene::UIVirtualListItem* const pooledSlots = scrolledVirtualList->pooledItems.data();
    for (std::uint32_t index = 51U; index < 90U; ++index) {
        Require(scene.UIDocuments().QueueScrollVirtualListTo(owner.Entity(), controlElements[5U], index),
            "Repeated Virtual List scroll was rejected");
        static_cast<void>(scene.Runtime().Update(0.0F));
        const auto view = scene.UIDocuments().VirtualList(owner.Entity(), controlElements[5U]);
        Require(view.has_value() && view->pooledItems.data() == pooledSlots &&
                scene.UIDocuments().ElementCount(owner.Entity()) == treeCountBeforeVirtualization,
            "Virtual List scroll allocated a new pool or created UI elements in the hot path");
    }
    kb::scene::UIControlState changedText = *scene.UIDocuments().Control(owner.Entity(), controlElements[0U]);
    changedText.text = "Score: 43";
    Require(scene.UIDocuments().QueueSetControl(owner.Entity(), controlElements[0U], changedText),
        "UI control mutation command was rejected");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().Control(owner.Entity(), controlElements[0U])->text == "Score: 43",
        "UI control mutation command did not update the canonical runtime tree");

    const auto duplicateScore = scene.UIDocuments().QueueCreate(owner.Entity(), kb::scene::UIRuntimeElementDesc{
        .parentId = 1U,
        .name = "Score",
        .control = { .kind = kb::scene::UIControlKind::Text, .text = "Duplicate" },
    });
    Require(duplicateScore.has_value(), "Duplicate-name UI setup fixture could not be queued");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(!scene.UIDocuments().Find(owner.Entity(), "Score").has_value(),
        "UI.Find must reject an ambiguous document-local name rather than choose an arbitrary element");
    Require(scene.UIDocuments().QueueDestroy(owner.Entity(), *duplicateScore),
        "Duplicate-name UI setup fixture could not be destroyed");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().Find(owner.Entity(), "Score") == std::optional<kb::scene::UIElementId>{ 2U },
        "UI.Find did not recover the original cached handle after the duplicate was removed");

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

    // Physical device state is routed by UIDocumentSceneSystem into
    // the existing UI event queue. There is no geometry in UIDocument yet, so
    // pointer activation intentionally targets the canonical focused element.
    std::size_t routedFocus = 0U;
    std::size_t routedNavigation = 0U;
    std::size_t routedClick = 0U;
    std::size_t routedSubmit = 0U;
    const kb::script::EventSubscriptionHandle focusRoute = scriptHost.Runtime().Events().Subscribe("UI.Focus",
        [&routedFocus](const kb::script::ScriptEvent&) { ++routedFocus; }, owner.Entity());
    const kb::script::EventSubscriptionHandle navigationRoute = scriptHost.Runtime().Events().Subscribe("UI.Navigation",
        [&routedNavigation](const kb::script::ScriptEvent&) { ++routedNavigation; }, owner.Entity());
    const kb::script::EventSubscriptionHandle clickRoute = scriptHost.Runtime().Events().Subscribe("UI.Click",
        [&routedClick](const kb::script::ScriptEvent&) { ++routedClick; }, owner.Entity());
    const kb::script::EventSubscriptionHandle submitRoute = scriptHost.Runtime().Events().Subscribe("UI.Submit",
        [&routedSubmit](const kb::script::ScriptEvent&) { ++routedSubmit; }, owner.Entity());
    Require(focusRoute != kb::script::kInvalidEventSubscriptionHandle && navigationRoute != kb::script::kInvalidEventSubscriptionHandle &&
            clickRoute != kb::script::kInvalidEventSubscriptionHandle && submitRoute != kb::script::kInvalidEventSubscriptionHandle,
        "UI input-route subscriptions were rejected");
    auto& device = scene.Input().MutableDeviceState();
    device.SetHasFocus(true);
    Require(scene.UIDocuments().QueueFocus(owner.Entity(), 2U), "Explicit UI focus was rejected");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().Focused(owner.Entity()) == 2U && routedFocus == 1U,
        "UI focus did not become canonical or emit its accessibility-visible event");
    device.SetKeyDown(kb::input::InputKey::GamepadDPadDown, true);
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.Input().GameplayInputConsumed(),
        "UI navigation did not claim the physical input for gameplay routing");
    device.SetKeyDown(kb::input::InputKey::GamepadDPadDown, false);
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(!scene.Input().GameplayInputConsumed(),
        "UI input consumption leaked beyond the routed interaction frame");
    Require(scene.UIDocuments().Focused(owner.Entity()) != 0U && routedNavigation == 1U && routedFocus >= 2U,
        "Gamepad D-pad did not route deterministic UI navigation and focus");
    device.SetPointerPosition(17.0F, 29.0F);
    device.SetKeyDown(kb::input::InputKey::MouseLeft, true);
    static_cast<void>(scene.Runtime().Update(0.0F));
    device.SetKeyDown(kb::input::InputKey::MouseLeft, false);
    static_cast<void>(scene.Runtime().Update(0.0F));
    device.SetKeyDown(kb::input::InputKey::GamepadFaceBottom, true);
    static_cast<void>(scene.Runtime().Update(0.0F));
    device.SetKeyDown(kb::input::InputKey::GamepadFaceBottom, false);
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(routedClick == 1U && routedSubmit == 1U,
        "Pointer and gamepad submit were not routed through the shared UI event queue");
    device.SetHasFocus(false);
    static_cast<void>(scene.Runtime().Update(0.0F));
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().Focused(owner.Entity()) == 0U && routedFocus >= 3U,
        "Losing window focus did not clear focused UI state");
    static_cast<void>(scriptHost.Runtime().Events().Unsubscribe(focusRoute));
    static_cast<void>(scriptHost.Runtime().Events().Unsubscribe(navigationRoute));
    static_cast<void>(scriptHost.Runtime().Events().Unsubscribe(clickRoute));
    static_cast<void>(scriptHost.Runtime().Events().Unsubscribe(submitRoute));

    // A second retained document exercises the production bridge
    // ScriptSharedState -> ScriptRuntimeSceneSystem -> SceneUIDocuments. The
    // source-to-control direction remains queued, while control-to-source
    // writes are observed after the next UI frame boundary.
    const std::filesystem::path bindingPath = root / "Assets" / "UI" / "Bindings.kbui";
    const kb::scene::UIDocument bindingDocument{
        .elements = {
            { .id = 1U, .parentId = 0U, .name = "BindingRoot", .visible = true,
              .canvas = kb::scene::UICanvas{}, .control = { .kind = kb::scene::UIControlKind::Canvas } },
            { .id = 2U, .parentId = 1U, .siblingOrder = 0U, .name = "BoundScore", .visible = true,
              .textStyle = kb::scene::UIText{},
              .control = { .kind = kb::scene::UIControlKind::Text, .text = "0" } },
            { .id = 3U, .parentId = 1U, .siblingOrder = 1U, .name = "BoundName", .visible = true,
              .textStyle = kb::scene::UIText{},
              .control = { .kind = kb::scene::UIControlKind::InputField, .text = "guest" } },
            { .id = 4U, .parentId = 1U, .siblingOrder = 2U, .name = "BoundEnabled", .visible = true,
              .control = { .kind = kb::scene::UIControlKind::Toggle, .toggleValue = false } },
            { .id = 5U, .parentId = 1U, .siblingOrder = 3U, .name = "BoundVolume", .visible = true,
              .control = { .kind = kb::scene::UIControlKind::Slider, .sliderValue = 0.0F, .sliderMinimum = 0.0F, .sliderMaximum = 10.0F } },
            { .id = 6U, .parentId = 1U, .siblingOrder = 4U, .name = "BoundScroll", .visible = true,
              .control = { .kind = kb::scene::UIControlKind::ScrollView, .scrollOffset = 0.0F } },
            { .id = 7U, .parentId = 1U, .siblingOrder = 5U, .name = "BoundModal", .visible = true,
              .control = { .kind = kb::scene::UIControlKind::ModalDialog, .modalOpen = false } },
        },
        .bindings = {
            { .elementId = 2U, .property = "text", .sourcePath = "bind.score", .valueType = kb::scene::UIDataValueType::Number,
              .direction = kb::scene::UIBindingDirection::OneWay },
            { .elementId = 3U, .property = "text", .sourcePath = "bind.name", .valueType = kb::scene::UIDataValueType::String,
              .direction = kb::scene::UIBindingDirection::TwoWay },
            { .elementId = 4U, .property = "toggle", .sourcePath = "bind.enabled", .valueType = kb::scene::UIDataValueType::Boolean,
              .direction = kb::scene::UIBindingDirection::TwoWay },
            { .elementId = 5U, .property = "value", .sourcePath = "bind.volume", .valueType = kb::scene::UIDataValueType::Number,
              .direction = kb::scene::UIBindingDirection::OneWay },
            { .elementId = 6U, .property = "scroll", .sourcePath = "bind.scroll", .valueType = kb::scene::UIDataValueType::Number,
              .direction = kb::scene::UIBindingDirection::TwoWay },
            { .elementId = 7U, .property = "modal", .sourcePath = "bind.modal", .valueType = kb::scene::UIDataValueType::Boolean,
              .direction = kb::scene::UIBindingDirection::TwoWay },
        },
    };
    Require(kb::scene::UIAssetIO::SaveDocument(bindingPath, bindingDocument),
        "Typed one-way/two-way UI binding document was rejected by the production asset writer");
    const kb::scene::UIDocument invalidBindingDocument{
        .elements = {
            { .id = 1U, .parentId = 0U, .name = "Root", .visible = true,
              .canvas = kb::scene::UICanvas{}, .control = { .kind = kb::scene::UIControlKind::Canvas } },
            { .id = 2U, .parentId = 1U, .name = "Text", .visible = true,
              .textStyle = kb::scene::UIText{},
              .control = { .kind = kb::scene::UIControlKind::Text } },
        },
        .bindings = {
            { .elementId = 2U, .property = "toggle", .sourcePath = "bad", .valueType = kb::scene::UIDataValueType::Boolean,
              .direction = kb::scene::UIBindingDirection::TwoWay },
        },
    };
    Require(!kb::scene::UIAssetIO::SaveDocument(root / "Assets" / "UI" / "InvalidBindings.kbui", invalidBindingDocument),
        "UI asset writer accepted a binding whose property does not match its typed control");
    Require(scene.Assets().Discover() == 4U, "UI binding document was not discovered by the production asset registry");
    const auto* bindingMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/UI/Bindings.kbui");
    Require(bindingMetadata != nullptr && bindingMetadata->type == kb::scene::kUIDocumentAssetType,
        "UI binding document was not classified as a production UIDocument asset");
    Require(scriptHost.SharedState().Set("bind.score", kb::script::ScriptValue{ 12 }) &&
            scriptHost.SharedState().Set("bind.name", kb::script::ScriptValue{ std::string{ "model" } }) &&
            scriptHost.SharedState().Set("bind.enabled", kb::script::ScriptValue{ true }) &&
            scriptHost.SharedState().Set("bind.volume", kb::script::ScriptValue{ 7.5F }) &&
            scriptHost.SharedState().Set("bind.scroll", kb::script::ScriptValue{ 3.0F }) &&
            scriptHost.SharedState().Set("bind.modal", kb::script::ScriptValue{ true }),
        "UI binding setup could not seed the canonical shared state");
    const kb::scene::SceneObject bindingOwner = scene.Entities().CreateObject({ .name = "Binding Owner" });
    scene.Components().UIDocuments().Set(bindingOwner.Entity(), kb::scene::UIDocumentComponent{
        .documentAssetId = bindingMetadata->id.value,
        .enabled = true,
    });
    static_cast<void>(scene.Runtime().Update(0.0F));
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().Control(bindingOwner.Entity(), 2U)->text == "12" &&
            scene.UIDocuments().Control(bindingOwner.Entity(), 3U)->text == "model" &&
            scene.UIDocuments().Control(bindingOwner.Entity(), 4U)->toggleValue &&
            scene.UIDocuments().Control(bindingOwner.Entity(), 5U)->sliderValue == 7.5F &&
            scene.UIDocuments().Control(bindingOwner.Entity(), 6U)->scrollOffset == 3.0F &&
            scene.UIDocuments().Control(bindingOwner.Entity(), 7U)->modalOpen,
        "Shared-state source values did not reach typed UI bindings through the queued runtime boundary");

    kb::scene::UIControlState editedName = *scene.UIDocuments().Control(bindingOwner.Entity(), 3U);
    kb::scene::UIControlState editedToggle = *scene.UIDocuments().Control(bindingOwner.Entity(), 4U);
    editedName.text = "player";
    editedToggle.toggleValue = false;
    Require(scene.UIDocuments().QueueSetControl(bindingOwner.Entity(), 3U, editedName) &&
            scene.UIDocuments().QueueSetControl(bindingOwner.Entity(), 4U, editedToggle),
        "Two-way UI binding edit was rejected by the canonical runtime queue");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scriptHost.SharedState().Get("bind.name").value_or(kb::script::ScriptValue{ std::string{} }).AsString() == "player" &&
            !scriptHost.SharedState().Get("bind.enabled").value_or(kb::script::ScriptValue{ true }).AsBool(),
        "Two-way UI bindings did not write typed control edits back to shared state");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().Control(bindingOwner.Entity(), 3U)->text == "player" &&
            !scene.UIDocuments().Control(bindingOwner.Entity(), 4U)->toggleValue,
        "A two-way UI binding reflected its own control write back as a feedback loop");

    editedName = *scene.UIDocuments().Control(bindingOwner.Entity(), 3U);
    editedName.text = "stale-control";
    Require(scene.UIDocuments().QueueSetControl(bindingOwner.Entity(), 3U, editedName) &&
            scriptHost.SharedState().Set("bind.name", kb::script::ScriptValue{ std::string{ "authoritative-model" } }),
        "Concurrent UI/model binding conflict setup failed");
    static_cast<void>(scene.Runtime().Update(0.0F));
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().Control(bindingOwner.Entity(), 3U)->text == "authoritative-model" &&
            scriptHost.SharedState().Get("bind.name").value_or(kb::script::ScriptValue{ std::string{} }).AsString() == "authoritative-model",
        "Two-way UI binding did not give the concurrently changed model deterministic precedence");
    Require(scriptHost.SharedState().Set("bind.score", kb::script::ScriptValue{ 99 }),
        "One-way UI binding source update was rejected");
    static_cast<void>(scene.Runtime().Update(0.0F));
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().Control(bindingOwner.Entity(), 2U)->text == "99",
        "One-way UI binding did not refresh after a typed source change");
    Require(scriptHost.SharedState().Set("bind.score", kb::script::ScriptValue{ std::string{ "wrong-type" } }),
        "Invalid-source binding setup was rejected before the typed binding boundary");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().Control(bindingOwner.Entity(), 2U)->text == "99",
        "A UI binding silently coerced a mismatched shared-state type");
    scene.Entities().Destroy(bindingOwner.Entity());
    static_cast<void>(scene.Runtime().Update(0.0F));

    kb::scene::UIControlState restoredText = *scene.UIDocuments().Control(owner.Entity(), 2U);
    restoredText.text = "Score: 0";
    Require(scene.UIDocuments().QueueSetControl(owner.Entity(), 2U, restoredText),
        "UI text restore before scene serialization was rejected");
    static_cast<void>(scene.Runtime().Update(0.0F));

    const std::filesystem::path scenePath = root / "UIDocument.21kbscene";
    Require(kb::scene::SceneDocumentService::Save(scene, scenePath, "UIDocument"),
        "Scene document with UIDocument component could not be saved");
    kb::scene::Scene loaded;
    Require(loaded.Assets().MountProject(root) && loaded.Assets().Discover() == 4U &&
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

    // LIB-182: an event may be queued while its element is alive, but it
    // must not be delivered after that element is removed at the next UI
    // command boundary. This uses a reloaded production document so it also
    // covers runtime state reconstructed from a scene asset.
    const auto eventTarget = loaded.UIDocuments().QueueCreate(roots.front(), kb::scene::UIRuntimeElementDesc{
        .parentId = 1U,
        .name = "EventTarget",
        .styleClass = "hud",
        .visible = true,
    });
    Require(eventTarget.has_value(), "UI event/destroy regression fixture target could not be queued");
    static_cast<void>(loaded.Runtime().Update(0.0F));
    Require(loaded.UIDocuments().QueueEvent(roots.front(), kb::scene::UIRuntimeEvent{
                .kind = kb::scene::UIRuntimeEventKind::Click,
                .elementId = *eventTarget,
                .pointerX = 12.5F,
                .pointerY = -4.0F,
            }) &&
            loaded.UIDocuments().QueueDestroy(roots.front(), *eventTarget),
        "UI event/destroy regression fixture was not accepted for a live element");
    static_cast<void>(loaded.Runtime().Update(0.0F));
    Require(!loaded.UIDocuments().HasElement(roots.front(), *eventTarget) &&
            loaded.UIDocuments().DrainEvents().empty(),
        "A UI event queued before its element was destroyed escaped the frame boundary");
    std::filesystem::remove_all(root);
}

} // namespace kb::tests
