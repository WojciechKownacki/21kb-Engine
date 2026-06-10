#include "inspection/InspectorInputInteraction.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/scene/InputComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "platform/win32/Win32InputCollector.hpp"

#include <cstddef>
#include <optional>
#include <string>

namespace kb::editor {

bool InspectorInputInteraction::HandleActionAssetClick(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit) {
    const kb::assets::AssetId asset = sceneContext.AssetBrowser().SelectedAsset();
    if (hit.kind == InspectorHitKind::TextField && hit.property == InspectorPropertyId::InputActionName) {
        const std::optional<kb::input::InputActionAsset> current = sceneContext.ReadInputActionAsset(asset);
        sceneContext.Inspector().BeginTextEdit(InspectorPropertyId::InputActionName, current.has_value() ? current->name : std::string{});
        return true;
    }
    sceneContext.Inspector().EndTextEdit();
    if (hit.kind == InspectorHitKind::TextField && hit.property == InspectorPropertyId::InputActionValueType) {
        static_cast<void>(sceneContext.CycleInputActionValueType(asset));
    } else if (hit.kind == InspectorHitKind::BoolField && hit.property == InspectorPropertyId::InputActionConsume) {
        static_cast<void>(sceneContext.ToggleInputActionConsume(asset));
    }
    return true;
}

bool InspectorInputInteraction::HandleMappingClick(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit) {
    const kb::assets::AssetId imc = sceneContext.AssetBrowser().SelectedAsset();
    const auto index = static_cast<std::size_t>(hit.index < 0 ? 0 : hit.index);
    if (hit.property == InspectorPropertyId::InputMappingKey) {
        sceneContext.Inspector().BeginKeyCapture(hit.index);
        return true;
    }
    sceneContext.Inspector().EndTextEdit();
    switch (hit.property) {
    case InspectorPropertyId::InputMappingAction:
        static_cast<void>(sceneContext.CycleInputMappingAction(imc, index));
        break;
    case InspectorPropertyId::InputMappingTrigger:
        static_cast<void>(sceneContext.CycleInputMappingTrigger(imc, index));
        break;
    case InspectorPropertyId::InputMappingRemove:
        static_cast<void>(sceneContext.RemoveInputMapping(imc, index));
        break;
    case InspectorPropertyId::InputMappingAdd:
        static_cast<void>(sceneContext.AddInputMapping(imc));
        break;
    default:
        break;
    }
    return true;
}

bool InspectorInputInteraction::HandleComponentClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    if (hit.kind == InspectorHitKind::TextField && hit.property == InspectorPropertyId::InputComponentPriority) {
        const kb::scene::InputComponent* component = sceneContext.Scene().Components().Inputs().TryGet(entity);
        sceneContext.Inspector().BeginTextEdit(InspectorPropertyId::InputComponentPriority, std::to_string(component != nullptr ? component->priority : 0));
        return true;
    }
    sceneContext.Inspector().EndTextEdit();
    switch (hit.property) {
    case InspectorPropertyId::InputComponentEnabled:
        static_cast<void>(sceneContext.ToggleInputComponentEnabled(entity));
        break;
    case InspectorPropertyId::InputComponentMappingContext:
        static_cast<void>(sceneContext.CycleInputComponentMappingContext(entity));
        break;
    case InspectorPropertyId::InputComponentRemove:
        static_cast<void>(sceneContext.RemoveInputComponent(entity));
        break;
    case InspectorPropertyId::InputComponentAdd:
        static_cast<void>(sceneContext.AddInputComponent(entity));
        break;
    default:
        break;
    }
    return true;
}

bool InspectorInputInteraction::HandleKeyCapture(EditorSceneContext& sceneContext, WPARAM virtualKey) {
    InspectorPanelState& inspector = sceneContext.Inspector();
    if (!inspector.IsListeningForKey()) {
        return false;
    }
    if (virtualKey == VK_ESCAPE) {
        inspector.EndKeyCapture();
        return true;
    }
    const kb::input::InputKey key = Win32InputKeyFromVirtualKey(static_cast<int>(virtualKey));
    const int index = inspector.KeyCaptureMappingIndex();
    if (key != kb::input::InputKey::None && index >= 0 &&
        sceneContext.AssetBrowser().SelectionKind() == EditorAssetBrowserSelectionKind::Asset) {
        static_cast<void>(sceneContext.SetInputMappingKey(sceneContext.AssetBrowser().SelectedAsset(), static_cast<std::size_t>(index), key));
    }
    inspector.EndKeyCapture();
    return true;
}

} // namespace kb::editor

#endif
