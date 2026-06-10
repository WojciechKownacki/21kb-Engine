#include "inspection/InspectorInputInteraction.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "platform/win32/Win32InputKeyMap.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] std::string FormatScale(float value) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.3f", static_cast<double>(value));
    std::string text = buffer.data();
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    return text;
}

} // namespace

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
    if (hit.property == InspectorPropertyId::InputMappingScale) {
        const std::optional<kb::input::InputMappingContextAsset> context = sceneContext.ReadInputMappingContextAsset(imc);
        const float scale = (context.has_value() && index < context->mappings.size()) ? context->mappings[index].scale : 1.0F;
        sceneContext.Inspector().BeginTextEdit(InspectorPropertyId::InputMappingScale, FormatScale(scale));
        sceneContext.Inspector().SetEditIndex(static_cast<int>(index));
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

bool InspectorInputInteraction::HandleKeyCapture(EditorSceneContext& sceneContext, WPARAM virtualKey) {
    InspectorPanelState& inspector = sceneContext.Inspector();
    if (!inspector.IsListeningForKey()) {
        return false;
    }
    if (virtualKey == VK_ESCAPE) {
        inspector.EndKeyCapture();
        return true;
    }
    const kb::input::InputKey key = Win32InputKeyMap::InputKeyForVirtualKey(static_cast<int>(virtualKey));
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
