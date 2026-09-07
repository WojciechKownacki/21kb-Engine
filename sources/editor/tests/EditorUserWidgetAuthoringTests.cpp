#include "EditorTestSuites.hpp"
#include "EditorTestSupport.hpp"

#include "engine/scene/UIAssetIO.hpp"
#include "engine/ui/layout/UIPresentationLayout.hpp"
#include "scene/user_widget/UserWidgetEditorDocument.hpp"
#include "scene/user_widget/UserWidgetEditorProperty.hpp"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <system_error>

namespace kb::editor::tests {
namespace {

[[nodiscard]] const kb::scene::UIDocumentElement* FindElement(const kb::scene::UIDocument& document,
                                                              kb::scene::UIElementId id) {
    for (const kb::scene::UIDocumentElement& element : document.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

void RunCanonicalDocumentAuthoringTest() {
    const std::filesystem::path path = std::filesystem::current_path() / "editor-user-widget-authoring.kbui";
    std::error_code error;
    static_cast<void>(std::filesystem::remove(path, error));

    const kb::scene::UIDocument created = UserWidgetEditorDocument::CreateCanvasDocument();
    Require(created.schemaVersion == kb::scene::UIDocument::kSchemaVersion && created.elements.size() == 1U &&
                created.elements.front().control.kind == kb::scene::UIControlKind::Canvas &&
                created.elements.front().canvas.has_value(),
            "New User Widget should contain one canonical root Canvas");
    Require(kb::scene::UIAssetIO::SaveDocument(path, created), "Canonical root Canvas should save through UIAssetIO");

    UserWidgetEditorDocument editor;
    Require(editor.Open(kb::assets::AssetId{7001U}, path), "User Widget editor should open a canonical .kbui document");
    const kb::scene::UIElementId root = editor.SelectedElementId();
    const auto container = editor.AddElement(kb::scene::UIControlKind::VerticalBox, root);
    Require(container.has_value(), "User Widget editor should add a canonical layout control");
    const auto button = editor.AddElement(kb::scene::UIControlKind::Button, *container);
    Require(button.has_value(), "User Widget editor should add a Button as a document child");

    const kb::scene::UIDocumentElement* current = FindElement(*editor.Document(), *button);
    Require(current != nullptr, "Added Button should exist in the document");
    kb::scene::UIDocumentElement edited = *current;
    edited.name = "PlayButton";
    edited.control.text = "Play";
    edited.rect.offsetMin = {48.0F, 64.0F};
    edited.rect.offsetMax = {288.0F, 128.0F};
    edited.paint = kb::scene::UIPaint{};
    edited.effects = kb::scene::UIEffects{};
    edited.interaction->eventName = "Menu.Play";
    Require(editor.EditElement(*button, edited), "Inspector edit should update the existing canonical components");
    Require(editor.IsDirty() && editor.CanUndo(), "User Widget mutation should be undoable and mark the asset dirty");
    Require(editor.Undo(), "User Widget property edit should undo");
    current = FindElement(*editor.Document(), *button);
    Require(current != nullptr && current->name == "Button" && current->control.text.empty(),
            "Undo should restore the exact prior UIDocument state");
    Require(editor.Redo(), "User Widget property edit should redo");
    current = FindElement(*editor.Document(), *button);
    Require(current != nullptr && current->name == "PlayButton" && current->control.text == "Play",
            "Redo should restore the edited UIDocument state");

    Require(editor.ReparentElement(*button, root),
            "User Widget hierarchy should support reparenting without scene entities");
    current = FindElement(*editor.Document(), *button);
    Require(current != nullptr && current->parentId == root, "Reparent should update the canonical parent id");
    Require(editor.Undo(), "Reparent should be undoable");
    current = FindElement(*editor.Document(), *button);
    Require(current != nullptr && current->parentId == *container, "Undo should restore the previous widget parent");

    const auto switcher = editor.AddElement(kb::scene::UIControlKind::WidgetSwitcher, root);
    const auto firstPage = switcher ? editor.AddElement(kb::scene::UIControlKind::Container, *switcher) : std::nullopt;
    const auto secondPage =
        firstPage ? editor.AddElement(kb::scene::UIControlKind::Container, *switcher) : std::nullopt;
    const auto thirdPage =
        secondPage ? editor.AddElement(kb::scene::UIControlKind::Container, *switcher) : std::nullopt;
    const auto pageDestination =
        thirdPage ? editor.AddElement(kb::scene::UIControlKind::Container, root) : std::nullopt;
    Require(switcher && firstPage && secondPage && thirdPage && pageDestination,
            "WidgetSwitcher fixture should contain three pages");
    kb::scene::UIDocumentElement switcherEdit = *FindElement(*editor.Document(), *switcher);
    switcherEdit.control.selectedIndex = 2U;
    Require(editor.EditElement(*switcher, switcherEdit) && editor.RemoveElement(*thirdPage),
            "Removing the selected WidgetSwitcher page should succeed");
    current = FindElement(*editor.Document(), *switcher);
    Require(current != nullptr && current->control.selectedIndex == 1U,
            "Removing the selected page should clamp WidgetSwitcher selection");

    Require(editor.ReparentElement(*secondPage, *pageDestination),
            "Reparenting the selected WidgetSwitcher page should succeed");
    current = FindElement(*editor.Document(), *switcher);
    Require(current != nullptr && current->control.selectedIndex == 0U,
            "Reparenting the selected page should clamp WidgetSwitcher selection");

    for (const auto& [kind, name] : kb::scene::kUIControlKindNames) {
        static_cast<void>(name);
        if (kind == kb::scene::UIControlKind::Canvas || kind == kb::scene::UIControlKind::Button ||
            kind == kb::scene::UIControlKind::VerticalBox) {
            continue;
        }
        Require(editor.AddElement(kind, root).has_value(), "Every canonical UIControlKind should be authorable");
    }
    Require(editor.Save(), "All canonical control compositions should save through UIAssetIO");
    Require(!editor.IsDirty(), "Successful save should establish a clean revision");

    UserWidgetEditorDocument reopened;
    Require(reopened.Open(kb::assets::AssetId{7001U}, path), "Saved User Widget should reopen through UIAssetIO");
    current = FindElement(*reopened.Document(), *button);
    Require(current != nullptr && current->name == "PlayButton" && current->control.text == "Play" &&
                current->interaction && current->interaction->eventName == "Menu.Play",
            "Reopen should preserve Button properties and Lua event binding");
    current = FindElement(*reopened.Document(), *switcher);
    const kb::scene::UIDocumentElement* movedPage = FindElement(*reopened.Document(), *secondPage);
    Require(current != nullptr && current->control.selectedIndex == 0U && movedPage != nullptr &&
                movedPage->parentId == *pageDestination && FindElement(*reopened.Document(), *thirdPage) == nullptr,
            "Save and reopen should preserve normalized WidgetSwitcher pages");

    error.clear();
    static_cast<void>(std::filesystem::remove(path, error));
}

void RunInspectorAndPreviewTest() {
    const std::filesystem::path path = std::filesystem::current_path() / "editor-user-widget-inspector.kbui";
    std::error_code error;
    static_cast<void>(std::filesystem::remove(path, error));
    Require(kb::scene::UIAssetIO::SaveDocument(path, UserWidgetEditorDocument::CreateCanvasDocument()),
            "Inspector fixture should save through UIAssetIO");

    UserWidgetEditorDocument editor;
    Require(editor.Open(kb::assets::AssetId{7002U}, path), "Inspector fixture should open");
    const kb::scene::UIElementId rootId = editor.SelectedElementId();
    kb::scene::UIDocumentElement root = *editor.SelectedElement();
    Require(UserWidgetEditorPropertyAdapter::Apply(UserWidgetEditorProperty::Canvas, "0 1280 720 1 0.5", root) &&
                editor.EditElement(rootId, root),
            "Canvas property row should edit the canonical canvas component");

    const auto buttonId = editor.AddElement(kb::scene::UIControlKind::Button, rootId);
    Require(buttonId.has_value(), "Inspector fixture should add Button");
    kb::scene::UIDocumentElement button = *editor.SelectedElement();
    const auto apply = [&button](UserWidgetEditorProperty property, std::string_view value) {
        Require(UserWidgetEditorPropertyAdapter::Apply(property, value, button),
                "Inspector property parser rejected a valid canonical value");
    };
    apply(UserWidgetEditorProperty::Name, "ActionButton");
    apply(UserWidgetEditorProperty::Visible, "true");
    apply(UserWidgetEditorProperty::RectAnchors, "0 0 0 0");
    apply(UserWidgetEditorProperty::RectOffsets, "10 20 210 84");
    apply(UserWidgetEditorProperty::RectPivot, "0.5 0.5");
    apply(UserWidgetEditorProperty::RectScale, "1 1");
    apply(UserWidgetEditorProperty::RectRotation, "5");
    apply(UserWidgetEditorProperty::RectZOrder, "3");
    apply(UserWidgetEditorProperty::Layout, "0 0 0 0 0 0 0 0 0 100 100 1");
    apply(UserWidgetEditorProperty::Paint, "1 1 1 1 4 4 4 4 0.9");
    Require(UserWidgetEditorPropertyAdapter::ApplyColor(UserWidgetEditorProperty::PaintBackgroundColor,
                                                        {0.1F, 0.2F, 0.3F, 1.0F}, button) &&
                UserWidgetEditorPropertyAdapter::ApplyColor(UserWidgetEditorProperty::PaintBorderColor,
                                                            {0.8F, 0.9F, 1.0F, 1.0F}, button),
            "Inspector color picker adapter should edit paint colors");
    apply(UserWidgetEditorProperty::Image, "71 0 0 1 1 1 true 0 0 0 0");
    apply(UserWidgetEditorProperty::Text, "Launch");
    apply(UserWidgetEditorProperty::TextStyle, "81 22 1 1 0");
    Require(UserWidgetEditorPropertyAdapter::ApplyColor(UserWidgetEditorProperty::TextColor, {1.0F, 1.0F, 1.0F, 1.0F},
                                                        button),
            "Inspector color picker adapter should edit text color");
    Require(!UserWidgetEditorPropertyAdapter::ApplyColor(UserWidgetEditorProperty::TextColor, {2.0F, 1.0F, 1.0F, 1.0F},
                                                         button),
            "Inspector color adapter should reject out-of-range channels");
    apply(UserWidgetEditorProperty::Interaction, "true true 1 0 0 0 0");
    apply(UserWidgetEditorProperty::InteractionAction, "Menu.Launch");
    apply(UserWidgetEditorProperty::Effects, "true false true 2 3 4 true 2 6");
    Require(UserWidgetEditorPropertyAdapter::ApplyColor(UserWidgetEditorProperty::EffectsShadowColor,
                                                        {0.0F, 0.0F, 0.0F, 0.5F}, button) &&
                UserWidgetEditorPropertyAdapter::ApplyColor(UserWidgetEditorProperty::EffectsOutlineColor,
                                                            {1.0F, 1.0F, 1.0F, 1.0F}, button),
            "Inspector color picker adapter should edit effect colors");
    apply(UserWidgetEditorProperty::ControlState, "false 0.5 0 1 0 0 false");
    Require(editor.EditElement(*buttonId, button),
            "Inspector should commit all canonical component fields as one edit");
    const kb::scene::UIDocumentElement* committed = editor.SelectedElement();
    Require(committed != nullptr && committed->name == "ActionButton" && committed->image &&
                committed->image->imageAssetId == 71U && committed->textStyle &&
                committed->textStyle->fontAssetId == 81U && committed->effects &&
                committed->effects->backgroundBlur == 6.0F && committed->interaction &&
                committed->interaction->eventName == "Menu.Launch",
            "Inspector should retain image/font IDs, effects and Lua action");

    kb::scene::UIDocumentElement invalid = *committed;
    Require(UserWidgetEditorPropertyAdapter::Apply(UserWidgetEditorProperty::TextStyle, "none", invalid) &&
                !editor.EditElement(*buttonId, invalid),
            "Canonical validation should reject removal of a required component");
    invalid = *committed;
    Require(!UserWidgetEditorPropertyAdapter::Apply(UserWidgetEditorProperty::RectAnchors, "not a rect", invalid),
            "Inspector should reject malformed values before opening an undo step");

    kb::scene::UIPresentationSnapshot snapshot;
    kb::scene::UIPresentationLayout::Build(*editor.Document(), 640U, 360U, snapshot);
    const auto item = std::ranges::find(snapshot.items, *buttonId, &kb::scene::UIPresentationItem::elementId);
    Require(item != snapshot.items.end(), "Editor preview should use the canonical presentation snapshot");
    const kb::math::Vec2 previousOffset = committed->rect.offsetMin;
    Require(editor.BeginPreviewDrag(*buttonId, 20.0F, 30.0F, item->canvasScale) &&
                editor.UpdatePreviewDrag(40.0F, 40.0F) && editor.EndPreviewDrag(),
            "Preview selection drag should edit canonical rect offsets");
    committed = editor.SelectedElement();
    Require(committed != nullptr && committed->rect.offsetMin.x > previousOffset.x &&
                committed->rect.offsetMin.y > previousOffset.y && editor.Undo(),
            "Preview drag should be finite and undo as one mutation");
    Require(editor.BeginPreviewDrag(*buttonId, -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                                    1.0F) &&
                !editor.UpdatePreviewDrag(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()) &&
                !editor.EndPreviewDrag(),
            "Preview drag should reject a non-finite offset result");

    const auto listId = editor.AddElement(kb::scene::UIControlKind::Dropdown, rootId);
    Require(listId.has_value(), "Inspector fixture should add Dropdown");
    kb::scene::UIDocumentElement list = *editor.SelectedElement();
    Require(UserWidgetEditorPropertyAdapter::Apply(UserWidgetEditorProperty::ListItems, "Low|Medium|High", list) &&
                editor.EditElement(*listId, list) && editor.SelectedElement()->control.listItems.size() == 3U,
            "Inspector should edit canonical control list state");

    const auto containerId = editor.AddElement(kb::scene::UIControlKind::Container, rootId);
    Require(containerId.has_value(), "Inspector fixture should add Container");
    kb::scene::UIDocumentElement container = *editor.SelectedElement();
    Require(UserWidgetEditorPropertyAdapter::Apply(UserWidgetEditorProperty::Interaction, "true true 1 0 0 0 0",
                                                   container) &&
                UserWidgetEditorPropertyAdapter::Apply(UserWidgetEditorProperty::TextStyle, "0 16 0 0 1", container) &&
                editor.EditElement(*containerId, container) && editor.SelectedElement()->interaction.has_value() &&
                editor.SelectedElement()->textStyle.has_value(),
            "Optional component rows should compose interaction and text on a Container");

    kb::scene::UIDocument exhausted = UserWidgetEditorDocument::CreateCanvasDocument();
    exhausted.elements.front().id = std::numeric_limits<kb::scene::UIElementId>::max();
    Require(kb::scene::UIAssetIO::SaveDocument(path, exhausted), "Maximum element ID fixture should save");
    UserWidgetEditorDocument exhaustedEditor;
    Require(exhaustedEditor.Open(kb::assets::AssetId{7003U}, path) &&
                !exhaustedEditor.AddElement(kb::scene::UIControlKind::Button).has_value(),
            "Element ID allocation should reject overflow");

    error.clear();
    static_cast<void>(std::filesystem::remove(path, error));
}

} // namespace

void RunEditorUserWidgetAuthoringTests() {
    RunCanonicalDocumentAuthoringTest();
    RunInspectorAndPreviewTest();
}

} // namespace kb::editor::tests
