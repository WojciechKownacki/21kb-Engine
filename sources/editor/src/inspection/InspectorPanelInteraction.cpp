#include "inspection/InspectorPanelInteraction.hpp"
#include "inspection/TerrainMaterialLayerMenuState.hpp"

#if defined(_WIN32)
#include "app/EditorTextInputShortcuts.hpp"
#include "platform/win32/EditorTagNameDialog.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/ContentInstanceComponent.hpp"
#include "engine/scene/StreamFocusComponent.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/DetailSwitchComponent.hpp"
#include "engine/scene/VisibilityBlockerComponent.hpp"
#include "engine/scene/VisibilityCellComponent.hpp"
#include "engine/scene/RegionPortalComponent.hpp"
#include "engine/scene/AuxFrameComponent.hpp"
#include "engine/scene/GeometrySwarmComponent.hpp"
#include "engine/scene/SurfaceCastComponent.hpp"
#include "engine/scene/FacingPanelComponent.hpp"
#include "engine/scene/SpaceStrokeComponent.hpp"
#include "engine/scene/HistoryRibbonComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/RegionShapeComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "inspection/InspectorAddComponentBrowserModel.hpp"
#include "inspection/InspectorAudioComponentModel.hpp"
#include "inspection/InspectorAudioScrubController.hpp"
#include "inspection/InspectorInputInteraction.hpp"
#include "inspection/InspectorPhysicsModel.hpp"
#include "kb/render/resources/RenderMaterialNumericParsing.hpp"
#include "scene/transform_edit/EditorTransformProperty.hpp"
#include "scene/EditorTerrainService.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::editor {
namespace {

[[nodiscard]] std::string FormatCompactFloat(float value);

// The value text seeded into the edit box when a non-bool exposed-variable row
// is clicked (bool rows toggle their checkbox instead).
[[nodiscard]] std::string FormatScriptVariableEditText(const EditorSceneContext::EntityScriptVariable& variable) {
    switch (variable.type) {
    case kb::script::ScriptValueType::String:
    case kb::script::ScriptValueType::Name:
    case kb::script::ScriptValueType::Guid:
        return variable.value.AsString();
    case kb::script::ScriptValueType::Int:
        return std::to_string(variable.value.AsInt());
    case kb::script::ScriptValueType::Int64:
        return std::to_string(variable.value.AsInt64());
    case kb::script::ScriptValueType::UInt32:
        return std::to_string(variable.value.AsUInt32());
    case kb::script::ScriptValueType::Entity:
    case kb::script::ScriptValueType::Component:
    case kb::script::ScriptValueType::Hash:
        return std::to_string(variable.value.AsUInt64());
    case kb::script::ScriptValueType::Double: {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%g", variable.value.AsDouble());
        return std::string{ buffer };
    }
    default: {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(variable.value.AsFloat()));
        return std::string{ buffer };
    }
    }
}

// Parses the edited text into a ScriptValue of the variable's declared type.
// Returns nullopt when a numeric field's text is not a number (edit is rejected,
// the previous value stands).
[[nodiscard]] std::optional<kb::script::ScriptValue> ParseScriptVariableEditText(
    std::string_view text, const EditorSceneContext::EntityScriptVariable& variable) {
    using kb::script::ScriptValue;
    using kb::script::ScriptValueType;
    switch (variable.type) {
    case ScriptValueType::String:
        return ScriptValue{ std::string{ text } };
    case ScriptValueType::Name:
    case ScriptValueType::Guid:
        return ScriptValue{ std::string{ text }, variable.type };
    case ScriptValueType::Bool:
        return ScriptValue{ text == "true" || text == "1" || text == "yes" };
    default:
        break;
    }
    const std::string buffer{ text };
    char* end = nullptr;
    const double parsed = std::strtod(buffer.c_str(), &end);
    if (end == buffer.c_str()) {
        return std::nullopt;
    }
    switch (variable.type) {
    case ScriptValueType::Double:
        return ScriptValue{ parsed };
    case ScriptValueType::Int:
        return ScriptValue{ static_cast<int>(std::llround(parsed)) };
    case ScriptValueType::Int64:
        return ScriptValue{ static_cast<std::int64_t>(std::llround(parsed)) };
    case ScriptValueType::UInt32:
        return ScriptValue{ static_cast<std::uint32_t>(parsed < 0.0 ? 0LL : std::llround(parsed)) };
    case ScriptValueType::Entity:
    case ScriptValueType::Component:
    case ScriptValueType::Hash:
        return ScriptValue{ static_cast<std::uint64_t>(parsed < 0.0 ? 0LL : std::llround(parsed)), variable.type };
    default:
        return ScriptValue{ static_cast<float>(parsed) };
    }
}

void SelectAssetInProjectFiles(EditorSceneContext& sceneContext, kb::assets::AssetId assetId) {
    if (!assetId.IsValid()) {
        return;
    }
    if (sceneContext.AssetBrowser().SelectAsset(assetId, sceneContext.Scene().Assets().Manager())) {
        sceneContext.AssetBrowser().FocusSelection(true);
    }
}

[[nodiscard]] bool HandleScriptClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    sceneContext.Inspector().EndTextEdit();
    switch (hit.property) {
    case InspectorPropertyId::ScriptEnabled:
        static_cast<void>(sceneContext.ToggleEntityScriptEnabled(entity));
        return true;
    case InspectorPropertyId::ScriptName:
    case InspectorPropertyId::ScriptPicker:
        // Reveal the bound Lua asset in Project Files (same affordance as the
        // Mesh/Material asset-field pickers).
        SelectAssetInProjectFiles(sceneContext, sceneContext.EntityScriptAssetId(entity));
        return true;
    case InspectorPropertyId::ComponentRemove:
        static_cast<void>(sceneContext.RemoveScriptFromEntity(entity));
        sceneContext.Inspector().CloseComponentMenus();
        return true;
    case InspectorPropertyId::ScriptVariable: {
        const std::vector<EditorSceneContext::EntityScriptVariable> variables = sceneContext.EntityScriptExposedVariables(entity);
        if (hit.index < 0 || static_cast<std::size_t>(hit.index) >= variables.size()) {
            return true;
        }
        const EditorSceneContext::EntityScriptVariable& variable = variables[static_cast<std::size_t>(hit.index)];
        if (variable.type == kb::script::ScriptValueType::Bool) {
            // A checkbox click flips the value immediately (undoable).
            static_cast<void>(sceneContext.SetEntityScriptVariable(entity, variable.name, kb::script::ScriptValue{ !variable.value.AsBool() }));
        } else {
            // Other types open the inline text editor seeded with the current value.
            // BeginTextEdit resets the edit index, so set it afterwards — the commit
            // path reads EditIndex() to know which variable row is being edited.
            sceneContext.Inspector().BeginTextEdit(InspectorPropertyId::ScriptVariable, FormatScriptVariableEditText(variable));
            sceneContext.Inspector().SetEditIndex(hit.index);
        }
        return true;
    }
    default:
        return true;
    }
}

[[nodiscard]] bool HandleAddComponentClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const std::string query = sceneContext.Inspector().EditedProperty() == InspectorPropertyId::AddComponentSearch
        ? sceneContext.Inspector().EditBuffer()
        : std::string{};
    sceneContext.Inspector().CloseComponentMenus();
    if (hit.property == InspectorPropertyId::AddComponentButton) {
        sceneContext.Inspector().EndTextEdit();
        sceneContext.Inspector().ToggleAddComponentBrowser();
        return true;
    }
    if (hit.property == InspectorPropertyId::AddComponentSearch) {
        sceneContext.Inspector().BeginTextEdit(InspectorPropertyId::AddComponentSearch, query);
        return true;
    }
    if (hit.property == InspectorPropertyId::AddComponentBack) {
        // Slide back to the category list, keeping the search box active + empty.
        sceneContext.Inspector().CloseAddComponentCategory();
        sceneContext.Inspector().BeginTextEdit(InspectorPropertyId::AddComponentSearch, {});
        return true;
    }
    if (hit.property == InspectorPropertyId::AddComponentOption) {
        const std::string& category = sceneContext.Inspector().AddComponentBrowserCategory();
        const std::vector<AddComponentRow> rows = InspectorAddComponentBrowserModel::Rows(
            query.empty() ? std::string_view{ category } : std::string_view{}, query,
            [&sceneContext](std::string_view pluginId) { return sceneContext.IsProjectPluginEnabled(pluginId); });
        if (hit.index >= 0 && static_cast<std::size_t>(hit.index) < rows.size()) {
            const AddComponentRow& row = rows[static_cast<std::size_t>(hit.index)];
            if (row.kind == AddComponentRowKind::Category) {
                // Enter the category (animated slide), keep search active + empty.
                sceneContext.Inspector().OpenAddComponentCategory(row.id);
                sceneContext.Inspector().BeginTextEdit(InspectorPropertyId::AddComponentSearch, {});
            } else {
                static_cast<void>(sceneContext.AddComponentToEntity(entity, row.id));
                sceneContext.Inspector().CloseAddComponentBrowser();
            }
        }
        return true;
    }
    return true;
}

[[nodiscard]] float StepFor(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::RotationX:
    case InspectorPropertyId::RotationY:
    case InspectorPropertyId::RotationZ:
        return 0.05F;
    case InspectorPropertyId::ScaleX:
    case InspectorPropertyId::ScaleY:
    case InspectorPropertyId::ScaleZ:
        return 0.1F;
    default:
        return 0.1F;
    }
}

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] bool ParseFloat(std::string_view text, float& value) noexcept {
    text = Trim(text);
    return kb::render::ParseFiniteMaterialFloatToken(text, value);
}

[[nodiscard]] bool IsMaterialFloatProperty(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::MaterialBaseColorR:
    case InspectorPropertyId::MaterialBaseColorG:
    case InspectorPropertyId::MaterialBaseColorB:
    case InspectorPropertyId::MaterialBaseColorA:
    case InspectorPropertyId::MaterialMetallicFactor:
    case InspectorPropertyId::MaterialRoughnessFactor:
    case InspectorPropertyId::MaterialNormalScale:
    case InspectorPropertyId::MaterialOcclusionStrength:
    case InspectorPropertyId::MaterialEmissiveColorR:
    case InspectorPropertyId::MaterialEmissiveColorG:
    case InspectorPropertyId::MaterialEmissiveColorB:
    case InspectorPropertyId::MaterialEmissiveStrength:
    case InspectorPropertyId::MaterialAlphaCutoff:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool IsLightFloatProperty(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::LightColorR:
    case InspectorPropertyId::LightColorG:
    case InspectorPropertyId::LightColorB:
    case InspectorPropertyId::LightIntensity:
    case InspectorPropertyId::LightRange:
    case InspectorPropertyId::LightInnerCone:
    case InspectorPropertyId::LightOuterCone:
    case InspectorPropertyId::LightAreaWidth:
    case InspectorPropertyId::LightAreaHeight:
    case InspectorPropertyId::LightContactShadowLength:
    case InspectorPropertyId::LightVolumetricScattering:
    case InspectorPropertyId::LightColorTemperatureKelvin:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool IsCameraFloatProperty(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::CameraVerticalFov:
    case InspectorPropertyId::CameraOrthographicHeight:
    case InspectorPropertyId::CameraNearClip:
    case InspectorPropertyId::CameraFarClip:
    case InspectorPropertyId::CameraClearColorR:
    case InspectorPropertyId::CameraClearColorG:
    case InspectorPropertyId::CameraClearColorB:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool ReadCameraFloat(
    const kb::scene::CameraComponent& camera,
    InspectorPropertyId property,
    float& value) noexcept {
    switch (property) {
    case InspectorPropertyId::CameraVerticalFov:
        value = camera.verticalFovDegrees;
        return true;
    case InspectorPropertyId::CameraOrthographicHeight:
        value = camera.orthographicHeight;
        return true;
    case InspectorPropertyId::CameraNearClip:
        value = camera.nearClip;
        return true;
    case InspectorPropertyId::CameraFarClip:
        value = camera.farClip;
        return true;
    case InspectorPropertyId::CameraClearColorR:
        value = camera.clearColor.x;
        return true;
    case InspectorPropertyId::CameraClearColorG:
        value = camera.clearColor.y;
        return true;
    case InspectorPropertyId::CameraClearColorB:
        value = camera.clearColor.z;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool WriteCameraFloat(
    kb::scene::CameraComponent& camera,
    InspectorPropertyId property,
    float value) noexcept {
    if (!std::isfinite(value)) {
        return false;
    }
    float* destination = nullptr;
    switch (property) {
    case InspectorPropertyId::CameraVerticalFov:
        if (value < 1.0F || value > 179.0F) {
            return false;
        }
        destination = &camera.verticalFovDegrees;
        break;
    case InspectorPropertyId::CameraOrthographicHeight:
        if (value <= 0.0F) {
            return false;
        }
        destination = &camera.orthographicHeight;
        break;
    case InspectorPropertyId::CameraNearClip:
        if (value <= 0.0F || value >= camera.farClip) {
            return false;
        }
        destination = &camera.nearClip;
        break;
    case InspectorPropertyId::CameraFarClip:
        if (value <= camera.nearClip) {
            return false;
        }
        destination = &camera.farClip;
        break;
    case InspectorPropertyId::CameraClearColorR:
        destination = &camera.clearColor.x;
        break;
    case InspectorPropertyId::CameraClearColorG:
        destination = &camera.clearColor.y;
        break;
    case InspectorPropertyId::CameraClearColorB:
        destination = &camera.clearColor.z;
        break;
    default:
        return false;
    }
    if (*destination == value) {
        return false;
    }
    *destination = value;
    return true;
}

[[nodiscard]] bool ReadLightFloat(const kb::scene::LightComponent& light, InspectorPropertyId property, float& value) noexcept {
    switch (property) {
    case InspectorPropertyId::LightColorR:
        value = light.color.x;
        return true;
    case InspectorPropertyId::LightColorG:
        value = light.color.y;
        return true;
    case InspectorPropertyId::LightColorB:
        value = light.color.z;
        return true;
    case InspectorPropertyId::LightIntensity:
        value = light.intensity;
        return true;
    case InspectorPropertyId::LightRange:
        value = light.range;
        return true;
    case InspectorPropertyId::LightInnerCone:
        value = light.innerConeDegrees;
        return true;
    case InspectorPropertyId::LightOuterCone:
        value = light.outerConeDegrees;
        return true;
    case InspectorPropertyId::LightAreaWidth:
        value = light.areaWidth;
        return true;
    case InspectorPropertyId::LightAreaHeight:
        value = light.areaHeight;
        return true;
    case InspectorPropertyId::LightContactShadowLength:
        value = light.contactShadowLength;
        return true;
    case InspectorPropertyId::LightVolumetricScattering:
        value = light.volumetricScattering;
        return true;
    case InspectorPropertyId::LightColorTemperatureKelvin:
        value = light.colorTemperatureKelvin;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] float ClampLightCone(float degrees) noexcept {
    return std::clamp(degrees, 0.0F, 179.0F);
}

[[nodiscard]] bool WriteLightFloat(kb::scene::LightComponent& light, InspectorPropertyId property, float value) noexcept {
    switch (property) {
    case InspectorPropertyId::LightColorR:
        light.color.x = std::clamp(value, 0.0F, 1.0F);
        return true;
    case InspectorPropertyId::LightColorG:
        light.color.y = std::clamp(value, 0.0F, 1.0F);
        return true;
    case InspectorPropertyId::LightColorB:
        light.color.z = std::clamp(value, 0.0F, 1.0F);
        return true;
    case InspectorPropertyId::LightIntensity:
        light.intensity = std::max(0.0F, value);
        return true;
    case InspectorPropertyId::LightRange:
        light.range = std::max(0.0F, value);
        return true;
    case InspectorPropertyId::LightInnerCone:
        light.innerConeDegrees = std::min(ClampLightCone(value), ClampLightCone(light.outerConeDegrees));
        return true;
    case InspectorPropertyId::LightOuterCone:
        light.outerConeDegrees = std::max(ClampLightCone(value), ClampLightCone(light.innerConeDegrees));
        return true;
    case InspectorPropertyId::LightAreaWidth:
        light.areaWidth = std::max(0.0F, value);
        return true;
    case InspectorPropertyId::LightAreaHeight:
        light.areaHeight = std::max(0.0F, value);
        return true;
    case InspectorPropertyId::LightContactShadowLength:
        light.contactShadowLength = std::max(0.0F, value);
        return true;
    case InspectorPropertyId::LightVolumetricScattering:
        light.volumetricScattering = std::max(0.0F, value);
        return true;
    case InspectorPropertyId::LightColorTemperatureKelvin:
        if (!std::isfinite(value)) {
            return false;
        }
        light.colorTemperatureKelvin = std::clamp(value, 1000.0F, 40000.0F);
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool ReadMaterialFloat(const kb::render::RenderMaterialAssetData& material, InspectorPropertyId property, float& value) noexcept {
    switch (property) {
    case InspectorPropertyId::MaterialBaseColorR:
        value = material.desc.baseColor[0];
        return true;
    case InspectorPropertyId::MaterialBaseColorG:
        value = material.desc.baseColor[1];
        return true;
    case InspectorPropertyId::MaterialBaseColorB:
        value = material.desc.baseColor[2];
        return true;
    case InspectorPropertyId::MaterialBaseColorA:
        value = material.desc.baseColor[3];
        return true;
    case InspectorPropertyId::MaterialMetallicFactor:
        value = material.desc.metallicFactor;
        return true;
    case InspectorPropertyId::MaterialRoughnessFactor:
        value = material.desc.roughnessFactor;
        return true;
    case InspectorPropertyId::MaterialNormalScale:
        value = material.desc.normalScale;
        return true;
    case InspectorPropertyId::MaterialOcclusionStrength:
        value = material.desc.occlusionStrength;
        return true;
    case InspectorPropertyId::MaterialEmissiveColorR:
        value = material.desc.emissiveColor[0];
        return true;
    case InspectorPropertyId::MaterialEmissiveColorG:
        value = material.desc.emissiveColor[1];
        return true;
    case InspectorPropertyId::MaterialEmissiveColorB:
        value = material.desc.emissiveColor[2];
        return true;
    case InspectorPropertyId::MaterialEmissiveStrength:
        value = material.desc.emissiveStrength;
        return true;
    case InspectorPropertyId::MaterialAlphaCutoff:
        value = material.desc.alphaCutoff;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool WriteMaterialFloat(EditorSceneContext& sceneContext, kb::assets::AssetId asset, InspectorPropertyId property, float value) {
    switch (property) {
    case InspectorPropertyId::MaterialBaseColorR:
        return sceneContext.SetMaterialBaseColor(asset, 0, value);
    case InspectorPropertyId::MaterialBaseColorG:
        return sceneContext.SetMaterialBaseColor(asset, 1, value);
    case InspectorPropertyId::MaterialBaseColorB:
        return sceneContext.SetMaterialBaseColor(asset, 2, value);
    case InspectorPropertyId::MaterialBaseColorA:
        return sceneContext.SetMaterialBaseColor(asset, 3, value);
    case InspectorPropertyId::MaterialMetallicFactor:
        return sceneContext.SetMaterialMetallicFactor(asset, value);
    case InspectorPropertyId::MaterialRoughnessFactor:
        return sceneContext.SetMaterialRoughnessFactor(asset, value);
    case InspectorPropertyId::MaterialNormalScale:
        return sceneContext.SetMaterialNormalScale(asset, value);
    case InspectorPropertyId::MaterialOcclusionStrength:
        return sceneContext.SetMaterialOcclusionStrength(asset, value);
    case InspectorPropertyId::MaterialEmissiveColorR:
        return sceneContext.SetMaterialEmissiveColor(asset, 0, value);
    case InspectorPropertyId::MaterialEmissiveColorG:
        return sceneContext.SetMaterialEmissiveColor(asset, 1, value);
    case InspectorPropertyId::MaterialEmissiveColorB:
        return sceneContext.SetMaterialEmissiveColor(asset, 2, value);
    case InspectorPropertyId::MaterialEmissiveStrength:
        return sceneContext.SetMaterialEmissiveStrength(asset, value);
    case InspectorPropertyId::MaterialAlphaCutoff:
        return sceneContext.SetMaterialAlphaCutoff(asset, value);
    default:
        return false;
    }
}

[[nodiscard]] std::optional<EditorMaterialTextureSlot> MaterialTextureSlotForProperty(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::MaterialAlbedoTexture:
        return EditorMaterialTextureSlot::Albedo;
    case InspectorPropertyId::MaterialNormalTexture:
        return EditorMaterialTextureSlot::Normal;
    case InspectorPropertyId::MaterialMetallicRoughnessTexture:
        return EditorMaterialTextureSlot::MetallicRoughness;
    case InspectorPropertyId::MaterialOcclusionTexture:
        return EditorMaterialTextureSlot::Occlusion;
    case InspectorPropertyId::MaterialEmissiveTexture:
        return EditorMaterialTextureSlot::Emissive;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::uint32_t> MeshRendererMaterialSlotForProperty(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::MeshRendererMaterialSlot0:
        return 0U;
    case InspectorPropertyId::MeshRendererMaterialSlot1:
        return 1U;
    case InspectorPropertyId::MeshRendererMaterialSlot2:
        return 2U;
    case InspectorPropertyId::MeshRendererMaterialSlot3:
        return 3U;
    case InspectorPropertyId::MeshRendererMaterialSlot4:
        return 4U;
    case InspectorPropertyId::MeshRendererMaterialSlot5:
        return 5U;
    case InspectorPropertyId::MeshRendererMaterialSlot6:
        return 6U;
    case InspectorPropertyId::MeshRendererMaterialSlot7:
        return 7U;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] bool HandleMeshRendererClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    sceneContext.Inspector().EndTextEdit();
    if (hit.kind == InspectorHitKind::BoolField &&
        (hit.property == InspectorPropertyId::MeshRendererCastsShadow ||
            hit.property == InspectorPropertyId::MeshRendererReceivesShadow)) {
        if (!sceneContext.BeginSceneEditTransaction(
                hit.property == InspectorPropertyId::MeshRendererCastsShadow
                ? "Edit Mesh Cast Shadows"
                : "Edit Mesh Receive Shadows")) {
            return true;
        }
        kb::scene::MeshRendererComponent* renderer =
            sceneContext.Scene().Components().MeshRenderers().TryGet(entity);
        if (renderer == nullptr) {
            sceneContext.CancelSceneEditTransaction();
            return true;
        }
        if (hit.property == InspectorPropertyId::MeshRendererCastsShadow) {
            renderer->castsShadow = !renderer->castsShadow;
        } else {
            renderer->receivesShadow = !renderer->receivesShadow;
        }
        sceneContext.Scene().Components().MeshRenderers().MarkModified(entity);
        static_cast<void>(sceneContext.CommitSceneEditTransaction());
        return true;
    }
    if (hit.kind != InspectorHitKind::TextField) {
        return true;
    }
    if (hit.property == InspectorPropertyId::MeshRendererMaterial) {
        const kb::scene::MeshRendererComponent* renderer = sceneContext.Scene().Components().MeshRenderers().TryGet(entity);
        if (renderer != nullptr) {
            SelectAssetInProjectFiles(sceneContext, kb::assets::AssetId{ renderer->materialAssetId });
        }
        return true;
    }
    if (const std::optional<std::uint32_t> slot = MeshRendererMaterialSlotForProperty(hit.property)) {
        const kb::scene::MeshRendererComponent* renderer = sceneContext.Scene().Components().MeshRenderers().TryGet(entity);
        if (renderer != nullptr && *slot < renderer->materialSlotOverrideCount) {
            SelectAssetInProjectFiles(sceneContext, kb::assets::AssetId{ renderer->materialSlotAssetIds[*slot] });
        }
        return true;
    }
    return true;
}

[[nodiscard]] bool EvaluateMath(std::string_view text, float currentValue, float& output) noexcept {
    text = Trim(text);
    if (text.empty() || !std::isfinite(currentValue)) {
        return false;
    }

    float value = 0.0F;
    std::size_t index = 0;
    if (text.front() == '+' || text.front() == '-' || text.front() == '*' || text.front() == '/') {
        value = currentValue;
    } else {
        std::size_t op = text.find_first_of("+-*/", 1);
        const std::string_view first = op == std::string_view::npos ? text : text.substr(0, op);
        if (!ParseFloat(first, value)) {
            return false;
        }
        index = op == std::string_view::npos ? text.size() : op;
    }

    while (index < text.size()) {
        const char operation = text[index++];
        while (index < text.size() && text[index] == ' ') {
            ++index;
        }
        const std::size_t next = text.find_first_of("+-*/", index);
        const std::string_view term = next == std::string_view::npos ? text.substr(index) : text.substr(index, next - index);
        float rhs = 0.0F;
        if (!ParseFloat(term, rhs)) {
            return false;
        }
        switch (operation) {
        case '+':
            value += rhs;
            break;
        case '-':
            value -= rhs;
            break;
        case '*':
            value *= rhs;
            break;
        case '/':
            if (rhs == 0.0F) {
                return false;
            }
            value /= rhs;
            break;
        default:
            return false;
        }
        if (!std::isfinite(value)) {
            return false;
        }
        index = next == std::string_view::npos ? text.size() : next;
    }

    if (!std::isfinite(value)) {
        return false;
    }
    output = value;
    return true;
}

[[nodiscard]] bool HandleMaterialClick(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit, int x, int y) {
    const kb::assets::AssetId asset = sceneContext.AssetBrowser().InspectorAsset();
    if (hit.kind == InspectorHitKind::BoolField && hit.property == InspectorPropertyId::MaterialDoubleSided) {
        sceneContext.Inspector().EndTextEdit();
        static_cast<void>(sceneContext.ToggleMaterialDoubleSided(asset));
        return true;
    }
    if (hit.kind == InspectorHitKind::TextField && hit.property == InspectorPropertyId::MaterialAlphaMode) {
        sceneContext.Inspector().EndTextEdit();
        static_cast<void>(sceneContext.CycleMaterialAlphaMode(asset));
        return true;
    }
    if (hit.kind == InspectorHitKind::TextField) {
        const std::optional<EditorMaterialTextureSlot> slot = MaterialTextureSlotForProperty(hit.property);
        if (slot.has_value()) {
            sceneContext.Inspector().EndTextEdit();
            static_cast<void>(sceneContext.CycleMaterialTextureAsset(asset, *slot));
            return true;
        }
    }
    if (hit.kind == InspectorHitKind::FloatField && IsMaterialFloatProperty(hit.property)) {
        const std::optional<kb::render::RenderMaterialAssetData> material = sceneContext.ReadMaterialAsset(asset);
        float value = 0.0F;
        if (material.has_value() && ReadMaterialFloat(*material, hit.property, value)) {
            if (sceneContext.BeginMaterialAssetFloatEdit(asset, hit.property)) {
                sceneContext.Inspector().BeginFloatDrag(hit.property, value, x, y);
            } else {
                sceneContext.Inspector().BeginTextEdit(hit.property, FormatCompactFloat(value));
            }
        }
        return true;
    }
    sceneContext.Inspector().EndTextEdit();
    return true;
}

[[nodiscard]] std::string FormatCompactFloat(float value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(value));
    std::string text = buffer;
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    return text == "-0" ? "0" : text;
}

[[nodiscard]] kb::scene::LightKind NextLightKind(kb::scene::LightKind kind) noexcept {
    switch (kind) {
    case kb::scene::LightKind::Directional:
        return kb::scene::LightKind::Point;
    case kb::scene::LightKind::Point:
        return kb::scene::LightKind::Spot;
    case kb::scene::LightKind::Spot:
        return kb::scene::LightKind::AreaRect;
    case kb::scene::LightKind::AreaRect:
        return kb::scene::LightKind::AreaDisk;
    case kb::scene::LightKind::AreaDisk:
        return kb::scene::LightKind::Tube;
    case kb::scene::LightKind::Tube:
        return kb::scene::LightKind::Directional;
    }
    return kb::scene::LightKind::Point;
}

template <typename Mutator>
[[nodiscard]] bool MutateLightComponent(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(label)) {
        return true;
    }
    kb::scene::LightComponent* light = sceneContext.Scene().Components().Lights().TryGet(entity);
    if (light == nullptr) {
        sceneContext.CancelSceneEditTransaction();
        return true;
    }
    mutator(*light);
    sceneContext.Scene().Components().Lights().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

[[nodiscard]] bool IsTerrainBrushFloatProperty(InspectorPropertyId property) noexcept {
    return property == InspectorPropertyId::TerrainBrushRadius ||
        property == InspectorPropertyId::TerrainBrushStrength ||
        property == InspectorPropertyId::TerrainBrushFalloff ||
        property == InspectorPropertyId::TerrainFlattenHeight ||
        property == InspectorPropertyId::TerrainTerraceStep;
}

[[nodiscard]] bool IsTerrainImportFloatProperty(InspectorPropertyId property) noexcept {
    return property == InspectorPropertyId::TerrainImportMinimumHeight ||
        property == InspectorPropertyId::TerrainImportMaximumHeight;
}

[[nodiscard]] bool IsTerrainTextProperty(InspectorPropertyId property) noexcept {
    return IsTerrainBrushFloatProperty(property) ||
        IsTerrainImportFloatProperty(property) ||
        property == InspectorPropertyId::TerrainNoiseSeed ||
        property == InspectorPropertyId::TerrainResolutionX ||
        property == InspectorPropertyId::TerrainResolutionZ ||
        property == InspectorPropertyId::TerrainWorldSizeX ||
        property == InspectorPropertyId::TerrainWorldSizeZ ||
        property == InspectorPropertyId::TerrainChunkQuads ||
        property == InspectorPropertyId::TerrainLodCount;
}

[[nodiscard]] std::optional<std::uint8_t> TerrainLayerIndexForProperty(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::TerrainMaterialLayer0:
        return 0U;
    case InspectorPropertyId::TerrainMaterialLayer1:
        return 1U;
    case InspectorPropertyId::TerrainMaterialLayer2:
        return 2U;
    case InspectorPropertyId::TerrainMaterialLayer3:
        return 3U;
    default: return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::uint8_t> TerrainLayerRevealIndexForProperty(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::TerrainMaterialLayerPicker0: return 0U;
    case InspectorPropertyId::TerrainMaterialLayerPicker1: return 1U;
    case InspectorPropertyId::TerrainMaterialLayerPicker2: return 2U;
    case InspectorPropertyId::TerrainMaterialLayerPicker3: return 3U;
    default: return std::nullopt;
    }
}

[[nodiscard]] float TerrainBrushFloatValue(const kb::terrain_editor::TerrainBrushSettings& brush, InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::TerrainBrushRadius: return brush.radius;
    case InspectorPropertyId::TerrainBrushStrength: return brush.strength;
    case InspectorPropertyId::TerrainBrushFalloff: return brush.falloff;
    case InspectorPropertyId::TerrainFlattenHeight: return brush.targetHeight;
    case InspectorPropertyId::TerrainTerraceStep: return brush.terraceStep;
    default: return 0.0F;
    }
}

[[nodiscard]] bool HandleTerrainClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    if (hit.property == terrain_material_layer_menu::kProperty) {
        sceneContext.Inspector().EndTextEdit();
        sceneContext.Inspector().CloseAddComponentBrowser();
        sceneContext.Inspector().CloseComponentMenus();
        terrain_material_layer_menu::Toggle();
        return true;
    }
    if (const std::optional<std::uint8_t> layerIndex = TerrainLayerRevealIndexForProperty(hit.property)) {
        const kb::assets::TerrainAsset* terrain = sceneContext.TerrainForEditing(entity);
        if (terrain != nullptr && *layerIndex < terrain->materialLayers.size()) {
            SelectAssetInProjectFiles(
                sceneContext,
                kb::assets::AssetId{ terrain->materialLayers[*layerIndex].materialAssetId });
        }
        return true;
    }
    if (const std::optional<std::uint8_t> layerIndex = TerrainLayerIndexForProperty(hit.property)) {
        terrain_material_layer_menu::Close();
        EditorTerrainToolState& tool = EditorTerrainService::ToolState();
        tool.selectedMaterialLayer = *layerIndex;
        tool.mode = EditorTerrainToolMode::Paint;
        tool.editingEnabled = true;
        tool.brush.strength = std::min(tool.brush.strength, 1.0F);
        tool.brushMenuOpen = false;
        return true;
    }
    if (hit.property == InspectorPropertyId::TerrainMaterialLayerRemove) {
        terrain_material_layer_menu::Close();
        const std::uint8_t layerIndex = hit.index >= 0
            ? static_cast<std::uint8_t>(hit.index)
            : EditorTerrainService::ToolState().selectedMaterialLayer;
        std::string error;
        if (!sceneContext.RemoveTerrainMaterialLayer(
                entity, layerIndex, &error)) {
            sceneContext.Console().Warning("Terrain", error.empty() ? "Material layer could not be removed." : error);
        }
        return true;
    }
    if (hit.property == InspectorPropertyId::TerrainBrushMode) {
        sceneContext.Inspector().EndTextEdit();
        auto& mode = EditorTerrainService::ToolState().brush.mode;
        mode = static_cast<kb::terrain_editor::TerrainBrushMode>((static_cast<std::uint32_t>(mode) + 1U) % 8U);
        EditorTerrainService::ToolState().mode =
            mode == kb::terrain_editor::TerrainBrushMode::CutHole || mode == kb::terrain_editor::TerrainBrushMode::FillHole
                ? EditorTerrainToolMode::Holes
                : EditorTerrainToolMode::Sculpt;
        EditorTerrainService::ToolState().editingEnabled = true;
        return true;
    }
    if (IsTerrainBrushFloatProperty(hit.property)) {
        sceneContext.Inspector().BeginTextEdit(hit.property, FormatCompactFloat(TerrainBrushFloatValue(EditorTerrainService::ToolState().brush, hit.property)));
        return true;
    }
    if (IsTerrainImportFloatProperty(hit.property)) {
        const kb::terrain_editor::TerrainHeightmapImportSettings& import =
            EditorTerrainService::ToolState().heightmapImport;
        sceneContext.Inspector().BeginTextEdit(
            hit.property,
            FormatCompactFloat(
                hit.property == InspectorPropertyId::TerrainImportMinimumHeight
                    ? import.minimumHeight
                    : import.maximumHeight));
        return true;
    }
    if (hit.property == InspectorPropertyId::TerrainNoiseSeed) {
        sceneContext.Inspector().BeginTextEdit(
            hit.property,
            std::to_string(EditorTerrainService::ToolState().brush.noiseSeed));
        return true;
    }
    if (hit.property == InspectorPropertyId::TerrainResolutionX ||
        hit.property == InspectorPropertyId::TerrainResolutionZ ||
        hit.property == InspectorPropertyId::TerrainWorldSizeX ||
        hit.property == InspectorPropertyId::TerrainWorldSizeZ ||
        hit.property == InspectorPropertyId::TerrainChunkQuads ||
        hit.property == InspectorPropertyId::TerrainLodCount) {
        const kb::assets::TerrainAsset* terrain = sceneContext.TerrainForEditing(entity);
        if (terrain == nullptr) return true;
        std::string value;
        switch (hit.property) {
        case InspectorPropertyId::TerrainResolutionX: value = std::to_string(terrain->width); break;
        case InspectorPropertyId::TerrainResolutionZ: value = std::to_string(terrain->height); break;
        case InspectorPropertyId::TerrainWorldSizeX: value = FormatCompactFloat(terrain->worldSizeX); break;
        case InspectorPropertyId::TerrainWorldSizeZ: value = FormatCompactFloat(terrain->worldSizeZ); break;
        case InspectorPropertyId::TerrainChunkQuads: value = std::to_string(terrain->chunkQuads); break;
        case InspectorPropertyId::TerrainLodCount: value = std::to_string(terrain->lodCount); break;
        default: break;
        }
        sceneContext.Inspector().BeginTextEdit(hit.property, std::move(value));
        return true;
    }
    if (hit.property == InspectorPropertyId::TerrainImportFlipVertically) {
        sceneContext.Inspector().EndTextEdit();
        auto& flip = EditorTerrainService::ToolState().heightmapImport.flipVertically;
        flip = !flip;
        return true;
    }
    if (hit.property == InspectorPropertyId::MeshRendererMaterial ||
        hit.property == InspectorPropertyId::MeshRendererCastsShadow ||
        hit.property == InspectorPropertyId::MeshRendererReceivesShadow) {
        return HandleMeshRendererClick(sceneContext, entity, hit);
    }
    return true;
}

[[nodiscard]] bool ApplyTerrainText(
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity,
    InspectorPropertyId property,
    std::string_view text) {
    if (property == InspectorPropertyId::TerrainNoiseSeed) {
        text = Trim(text);
        std::uint32_t value = 0U;
        const std::from_chars_result parsed =
            std::from_chars(text.data(), text.data() + text.size(), value);
        if (text.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != text.data() + text.size()) {
            return false;
        }
        EditorTerrainService::ToolState().brush.noiseSeed = value;
        return true;
    }
    float value = 0.0F;
    if (!ParseFloat(text, value) || !std::isfinite(value)) return false;
    if (IsTerrainImportFloatProperty(property)) {
        kb::terrain_editor::TerrainHeightmapImportSettings candidate =
            EditorTerrainService::ToolState().heightmapImport;
        if (property == InspectorPropertyId::TerrainImportMinimumHeight) {
            candidate.minimumHeight = value;
        } else {
            candidate.maximumHeight = value;
        }
        if (!(candidate.minimumHeight < candidate.maximumHeight)) return false;
        EditorTerrainService::ToolState().heightmapImport = candidate;
        return true;
    }
    if (property == InspectorPropertyId::TerrainResolutionX ||
        property == InspectorPropertyId::TerrainResolutionZ ||
        property == InspectorPropertyId::TerrainWorldSizeX ||
        property == InspectorPropertyId::TerrainWorldSizeZ ||
        property == InspectorPropertyId::TerrainChunkQuads ||
        property == InspectorPropertyId::TerrainLodCount) {
        const kb::assets::TerrainAsset* terrain = sceneContext.TerrainForEditing(entity);
        if (terrain == nullptr) return false;
        EditorTerrainConfiguration configuration{
            .width = terrain->width,
            .height = terrain->height,
            .chunkQuads = terrain->chunkQuads,
            .lodCount = terrain->lodCount,
            .worldSizeX = terrain->worldSizeX,
            .worldSizeZ = terrain->worldSizeZ,
        };
        if (property == InspectorPropertyId::TerrainWorldSizeX ||
            property == InspectorPropertyId::TerrainWorldSizeZ) {
            if (!ParseFloat(text, value) || !std::isfinite(value) || value <= 0.0F) {
                return false;
            }
            if (property == InspectorPropertyId::TerrainWorldSizeX) {
                configuration.worldSizeX = value;
            } else {
                configuration.worldSizeZ = value;
            }
        } else {
            text = Trim(text);
            std::uint32_t integer = 0U;
            const std::from_chars_result parsed =
                std::from_chars(text.data(), text.data() + text.size(), integer);
            if (text.empty() || parsed.ec != std::errc{} ||
                parsed.ptr != text.data() + text.size()) {
                return false;
            }
            switch (property) {
            case InspectorPropertyId::TerrainResolutionX: configuration.width = integer; break;
            case InspectorPropertyId::TerrainResolutionZ: configuration.height = integer; break;
            case InspectorPropertyId::TerrainChunkQuads: configuration.chunkQuads = integer; break;
            case InspectorPropertyId::TerrainLodCount: configuration.lodCount = integer; break;
            default: return false;
            }
        }
        std::string error;
        if (!sceneContext.ConfigureTerrain(entity, configuration, &error)) {
            sceneContext.Console().Warning(
                "Terrain", error.empty()
                    ? "Terrain configuration is invalid."
                    : error);
            return false;
        }
        return true;
    }
    kb::terrain_editor::TerrainBrushSettings candidate = EditorTerrainService::ToolState().brush;
    switch (property) {
    case InspectorPropertyId::TerrainBrushRadius: candidate.radius = value; break;
    case InspectorPropertyId::TerrainBrushStrength: candidate.strength = value; break;
    case InspectorPropertyId::TerrainBrushFalloff: candidate.falloff = value; break;
    case InspectorPropertyId::TerrainFlattenHeight: candidate.targetHeight = value; break;
    case InspectorPropertyId::TerrainTerraceStep: candidate.terraceStep = value; break;
    default: return false;
    }
    if (!kb::terrain_editor::IsTerrainBrushSettingsValid(candidate)) return false;
    EditorTerrainService::ToolState().brush = candidate;
    return true;
}

[[nodiscard]] bool ApplyLightLayerMask(
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity,
    std::string_view text) {
    text = Trim(text);
    std::uint32_t value = 0U;
    const std::from_chars_result parsed =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size()) {
        return false;
    }
    return MutateLightComponent(
        sceneContext,
        entity,
        "Edit Light Layer Mask",
        [value](kb::scene::LightComponent& light) {
            if (light.layerMask == value) {
                return false;
            }
            light.layerMask = value;
            return true;
        });
}

[[nodiscard]] bool HandleLightClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    if (hit.kind == InspectorHitKind::TextField && hit.property == InspectorPropertyId::LightKind) {
        sceneContext.Inspector().EndTextEdit();
        return MutateLightComponent(sceneContext, entity, "Edit Light Type", [](kb::scene::LightComponent& light) {
            light.kind = NextLightKind(light.kind);
        });
    }
    if (hit.kind == InspectorHitKind::BoolField && hit.property == InspectorPropertyId::LightCastsShadow) {
        sceneContext.Inspector().EndTextEdit();
        return MutateLightComponent(sceneContext, entity, "Edit Light Shadows", [](kb::scene::LightComponent& light) {
            light.castsShadow = !light.castsShadow;
        });
    }
    if (hit.kind == InspectorHitKind::BoolField && hit.property == InspectorPropertyId::LightUseColorTemperature) {
        sceneContext.Inspector().EndTextEdit();
        return MutateLightComponent(sceneContext, entity, "Edit Light Color Temperature", [](kb::scene::LightComponent& light) {
            light.useColorTemperature = !light.useColorTemperature;
        });
    }
    if (hit.kind == InspectorHitKind::FloatField && IsLightFloatProperty(hit.property)) {
        if (const kb::scene::LightComponent* light = sceneContext.Scene().Components().Lights().TryGet(entity); light != nullptr) {
            float value = 0.0F;
            if (ReadLightFloat(*light, hit.property, value)) {
                sceneContext.Inspector().BeginTextEdit(hit.property, FormatCompactFloat(value));
            }
        }
        return true;
    }
    if (hit.kind == InspectorHitKind::TextField && hit.property == InspectorPropertyId::LightLayerMask) {
        if (const kb::scene::LightComponent* light = sceneContext.Scene().Components().Lights().TryGet(entity); light != nullptr) {
            sceneContext.Inspector().BeginTextEdit(hit.property, std::to_string(light->layerMask));
        }
        return true;
    }
    sceneContext.Inspector().EndTextEdit();
    return true;
}

template <typename Mutator>
[[nodiscard]] bool MutateCameraComponent(
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity,
    std::string label,
    Mutator mutator) {
    const kb::scene::CameraComponent* current =
        sceneContext.Scene().Components().Cameras().TryGet(entity);
    if (current == nullptr) {
        return false;
    }
    kb::scene::CameraComponent candidate = *current;
    if (!mutator(candidate)) {
        return false;
    }
    if (!sceneContext.BeginSceneEditTransaction(label)) {
        return false;
    }
    if (!sceneContext.Scene().Entities().IsAlive(entity) ||
        !sceneContext.Scene().Components().Cameras().Has(entity)) {
        sceneContext.CancelSceneEditTransaction();
        return false;
    }
    sceneContext.Scene().Components().Cameras().Set(entity, candidate);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

[[nodiscard]] bool RemoveCameraComponent(
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity) {
    if (!sceneContext.Scene().Components().Cameras().Has(entity) ||
        !sceneContext.BeginSceneEditTransaction("Remove Camera")) {
        return false;
    }
    sceneContext.Scene().Components().Cameras().Remove(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

[[nodiscard]] bool ApplyCameraFloat(
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity,
    InspectorPropertyId property,
    float value) {
    return MutateCameraComponent(
        sceneContext,
        entity,
        "Edit Camera",
        [property, value](kb::scene::CameraComponent& camera) {
            return WriteCameraFloat(camera, property, value);
        });
}

template <typename Integer>
[[nodiscard]] std::optional<Integer> ParseInteger(std::string_view text) noexcept {
    text = Trim(text);
    if (text.empty()) {
        return std::nullopt;
    }
    Integer value{};
    const std::from_chars_result parsed =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] bool ApplyCameraInteger(
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity,
    InspectorPropertyId property,
    std::string_view text) {
    if (property == InspectorPropertyId::CameraViewportId) {
        const std::optional<std::uint32_t> value =
            ParseInteger<std::uint32_t>(text);
        return value.has_value() &&
            MutateCameraComponent(
                sceneContext,
                entity,
                "Edit Camera Viewport",
                [value](kb::scene::CameraComponent& camera) {
                    if (camera.viewportId == *value) {
                        return false;
                    }
                    camera.viewportId = *value;
                    return true;
                });
    }
    if (property == InspectorPropertyId::CameraPriority) {
        const std::optional<std::int32_t> value =
            ParseInteger<std::int32_t>(text);
        return value.has_value() &&
            MutateCameraComponent(
                sceneContext,
                entity,
                "Edit Camera Priority",
                [value](kb::scene::CameraComponent& camera) {
                    if (camera.priority == *value) {
                        return false;
                    }
                    camera.priority = *value;
                    return true;
                });
    }
    if (property == InspectorPropertyId::CameraCullingMask) {
        const std::optional<std::uint32_t> value =
            ParseInteger<std::uint32_t>(text);
        return value.has_value() &&
            MutateCameraComponent(
                sceneContext,
                entity,
                "Edit Camera Culling Mask",
                [value](kb::scene::CameraComponent& camera) {
                    if (camera.cullingMask == *value) {
                        return false;
                    }
                    camera.cullingMask = *value;
                    return true;
                });
    }
    return false;
}

[[nodiscard]] bool HandleCameraClick(
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity,
    const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::CameraComponent* camera =
        sceneContext.Scene().Components().Cameras().TryGet(entity);
    if (camera == nullptr) {
        sceneContext.Inspector().EndTextEdit();
        return true;
    }
    if (hit.kind == InspectorHitKind::TextField &&
        hit.property == InspectorPropertyId::CameraProjection) {
        sceneContext.Inspector().EndTextEdit();
        static_cast<void>(MutateCameraComponent(
            sceneContext,
            entity,
            "Edit Camera Projection",
            [](kb::scene::CameraComponent& mutableCamera) {
                mutableCamera.projection =
                    mutableCamera.projection ==
                        kb::scene::CameraProjection::Perspective
                    ? kb::scene::CameraProjection::Orthographic
                    : kb::scene::CameraProjection::Perspective;
                return true;
            }));
        return true;
    }
    if (hit.kind == InspectorHitKind::TextField &&
        hit.property == InspectorPropertyId::CameraClearMode) {
        sceneContext.Inspector().EndTextEdit();
        static_cast<void>(MutateCameraComponent(
            sceneContext,
            entity,
            "Edit Camera Clear Mode",
            [](kb::scene::CameraComponent& mutableCamera) {
                switch (mutableCamera.clearMode) {
                case kb::scene::CameraClearMode::SolidColor:
                    mutableCamera.clearMode = kb::scene::CameraClearMode::DepthOnly;
                    break;
                case kb::scene::CameraClearMode::DepthOnly:
                    mutableCamera.clearMode = kb::scene::CameraClearMode::DontClear;
                    break;
                case kb::scene::CameraClearMode::DontClear:
                    mutableCamera.clearMode = kb::scene::CameraClearMode::SolidColor;
                    break;
                }
                return true;
            }));
        return true;
    }
    if (hit.kind == InspectorHitKind::BoolField &&
        hit.property == InspectorPropertyId::CameraPrimary) {
        sceneContext.Inspector().EndTextEdit();
        static_cast<void>(MutateCameraComponent(
            sceneContext,
            entity,
            "Edit Camera Primary",
            [](kb::scene::CameraComponent& mutableCamera) {
                mutableCamera.primary = !mutableCamera.primary;
                return true;
            }));
        return true;
    }
    if (hit.kind == InspectorHitKind::FloatField &&
        IsCameraFloatProperty(hit.property)) {
        float value = 0.0F;
        if (ReadCameraFloat(*camera, hit.property, value)) {
            sceneContext.Inspector().BeginTextEdit(
                hit.property, FormatCompactFloat(value));
        }
        return true;
    }
    if (hit.kind == InspectorHitKind::TextField &&
        (hit.property == InspectorPropertyId::CameraViewportId ||
            hit.property == InspectorPropertyId::CameraCullingMask)) {
        sceneContext.Inspector().BeginTextEdit(
            hit.property,
            hit.property == InspectorPropertyId::CameraViewportId
                ? std::to_string(camera->viewportId)
                : std::to_string(camera->cullingMask));
        return true;
    }
    if (hit.kind == InspectorHitKind::TextField &&
        hit.property == InspectorPropertyId::CameraPriority) {
        sceneContext.Inspector().BeginTextEdit(
            hit.property, std::to_string(camera->priority));
        return true;
    }
    sceneContext.Inspector().EndTextEdit();
    return true;
}

[[nodiscard]] bool HandleAnimatorClick(
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity,
    const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::Animator* animator = sceneContext.Scene().Components().Animators().TryGet(entity);
    if (animator == nullptr) return false;
    if (hit.property == InspectorPropertyId::AnimatorEnabled) {
        sceneContext.Inspector().EndTextEdit();
        return sceneContext.ToggleAnimatorEnabled(entity);
    }
    if (hit.property == InspectorPropertyId::AnimatorController ||
        hit.property == InspectorPropertyId::AnimatorControllerPicker) {
        sceneContext.Inspector().EndTextEdit();
        return true;
    }
    if (hit.property == InspectorPropertyId::AnimatorSpeed) {
        sceneContext.Inspector().BeginTextEdit(hit.property, FormatCompactFloat(animator->speed));
        return true;
    }
    if (hit.property == InspectorPropertyId::AnimatorRootMotionOwner) {
        sceneContext.Inspector().EndTextEdit();
        return sceneContext.CycleAnimatorRootMotionOwner(entity);
    }
    return true;
}

[[nodiscard]] bool HandleUIDocumentClick(
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity,
    const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::UIDocumentComponent* document = sceneContext.Scene().Components().UIDocuments().TryGet(entity);
    if (document == nullptr) return false;
    if (hit.property == InspectorPropertyId::UIDocumentEnabled) {
        sceneContext.Inspector().EndTextEdit();
        return sceneContext.ToggleUIDocumentEnabled(entity);
    }
    if (hit.property == InspectorPropertyId::UIDocumentAsset) {
        sceneContext.Inspector().BeginTextEdit(hit.property, std::to_string(document->documentAssetId));
        return true;
    }
    return true;
}

[[nodiscard]] bool HandleSkeletonBindingClick(
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity,
    const InspectorPanelRenderer::Hit& hit) {
    if (sceneContext.Scene().Components().SkeletonBindings().TryGet(entity) == nullptr) return false;
    sceneContext.Inspector().EndTextEdit();
    if (hit.property == InspectorPropertyId::SkeletonBindingEnabled) {
        return sceneContext.ToggleSkeletonBindingEnabled(entity);
    }
    return true;
}

[[nodiscard]] bool HandleDeformedGeometryClick(
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity,
    const InspectorPanelRenderer::Hit& hit) {
    if (sceneContext.Scene().Components().DeformedGeometries().TryGet(entity) == nullptr) return false;
    sceneContext.Inspector().EndTextEdit();
    switch (hit.property) {
    case InspectorPropertyId::DeformedGeometryCastsShadow:
        return sceneContext.ToggleDeformedGeometryCastsShadow(entity);
    case InspectorPropertyId::DeformedGeometryReceivesShadow:
        return sceneContext.ToggleDeformedGeometryReceivesShadow(entity);
    case InspectorPropertyId::DeformedGeometryEnabled:
        return sceneContext.ToggleDeformedGeometryEnabled(entity);
    default:
        return true;
    }
}

[[nodiscard]] bool IsNavAgentProperty(InspectorPropertyId property) noexcept {
    return property >= InspectorPropertyId::NavAgentRadius && property <= InspectorPropertyId::NavAgentEnabled;
}

[[nodiscard]] bool IsNavObstacleProperty(InspectorPropertyId property) noexcept {
    return property >= InspectorPropertyId::NavObstacleShape && property <= InspectorPropertyId::NavObstacleEnabled;
}

[[nodiscard]] bool IsRegionShapeProperty(InspectorPropertyId property) noexcept {
    return property >= InspectorPropertyId::RegionShapeKind && property <= InspectorPropertyId::RegionShapeEnabled;
}

[[nodiscard]] bool IsContentInstanceProperty(InspectorPropertyId property) noexcept {
    return property >= InspectorPropertyId::ContentInstanceAssetId && property <= InspectorPropertyId::ContentInstanceActive;
}

[[nodiscard]] bool IsStreamFocusProperty(InspectorPropertyId property) noexcept {
    return property >= InspectorPropertyId::StreamFocusInnerRadius && property <= InspectorPropertyId::StreamFocusEnabled;
}

[[nodiscard]] bool IsWorldBackdropProperty(InspectorPropertyId property) noexcept {
    return property >= InspectorPropertyId::WorldBackdropMode && property <= InspectorPropertyId::WorldBackdropEnabled;
}

[[nodiscard]] bool IsAmbientRadianceProperty(InspectorPropertyId property) noexcept {
    return property >= InspectorPropertyId::AmbientRadianceMode && property <= InspectorPropertyId::AmbientRadianceEnabled;
}

[[nodiscard]] bool IsDetailSwitchProperty(InspectorPropertyId property) noexcept {
    return property >= InspectorPropertyId::DetailSwitchGroupId && property <= InspectorPropertyId::DetailSwitchEnabled;
}

[[nodiscard]] bool IsVisibilityBlockerProperty(InspectorPropertyId property) noexcept {
    return property >= InspectorPropertyId::VisibilityBlockerCenterX && property <= InspectorPropertyId::VisibilityBlockerEnabled;
}

[[nodiscard]] bool IsVisibilityCellProperty(InspectorPropertyId property) noexcept {
    return property >= InspectorPropertyId::VisibilityCellMembershipMask && property <= InspectorPropertyId::VisibilityCellEnabled;
}

[[nodiscard]] bool IsRegionPortalProperty(InspectorPropertyId property) noexcept {
    return property >= InspectorPropertyId::RegionPortalSourceCell && property <= InspectorPropertyId::RegionPortalEnabled;
}

[[nodiscard]] bool IsSecondaryFrameProperty(InspectorPropertyId property) noexcept {
    return property >= InspectorPropertyId::SecondaryFrameMode && property <= InspectorPropertyId::SecondaryFrameEnabled;
}

[[nodiscard]] bool IsGeometrySwarmProperty(InspectorPropertyId property) noexcept {
    return property >= InspectorPropertyId::GeometrySwarmMeshAssetId && property <= InspectorPropertyId::GeometrySwarmEnabled;
}
[[nodiscard]] bool IsSurfaceCastProperty(InspectorPropertyId property) noexcept {
    return property >= InspectorPropertyId::SurfaceCastMaterialAssetId && property <= InspectorPropertyId::SurfaceCastEnabled;
}
[[nodiscard]] bool IsFacingPanelProperty(InspectorPropertyId property) noexcept {
    return property >= InspectorPropertyId::FacingPanelMode && property <= InspectorPropertyId::FacingPanelEnabled;
}
[[nodiscard]] bool IsSpaceStrokeProperty(InspectorPropertyId property) noexcept {
    return property >= InspectorPropertyId::SpaceStrokeMeshAssetId && property <= InspectorPropertyId::SpaceStrokeEnabled;
}
[[nodiscard]] bool IsHistoryRibbonProperty(InspectorPropertyId property) noexcept {
    return property >= InspectorPropertyId::HistoryRibbonMeshAssetId && property <= InspectorPropertyId::HistoryRibbonEnabled;
}

template <typename Mutator>
[[nodiscard]] bool EditNavAgent(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string_view label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) return false;
    kb::scene::NavAgent* agent = sceneContext.Scene().Components().NavAgents().TryGet(entity);
    if (agent == nullptr || !mutator(*agent)) {
        sceneContext.CancelSceneEditTransaction();
        return false;
    }
    sceneContext.Scene().Components().NavAgents().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

template <typename Mutator>
[[nodiscard]] bool EditNavObstacle(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string_view label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) return false;
    kb::scene::NavObstacle* obstacle = sceneContext.Scene().Components().NavObstacles().TryGet(entity);
    if (obstacle == nullptr || !mutator(*obstacle)) {
        sceneContext.CancelSceneEditTransaction();
        return false;
    }
    sceneContext.Scene().Components().NavObstacles().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

template <typename Mutator>
[[nodiscard]] bool EditRegionShape(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string_view label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) return false;
    kb::scene::RegionShapeComponent* shape = sceneContext.Scene().Components().RegionShapes().TryGet(entity);
    if (shape == nullptr || !mutator(*shape)) {
        sceneContext.CancelSceneEditTransaction();
        return false;
    }
    sceneContext.Scene().Components().RegionShapes().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

template <typename Mutator>
[[nodiscard]] bool EditContentInstance(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string_view label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) return false;
    kb::scene::ContentInstanceComponent* instance = sceneContext.Scene().Components().ContentInstances().TryGet(entity);
    if (instance == nullptr || !mutator(*instance)) {
        sceneContext.CancelSceneEditTransaction();
        return false;
    }
    sceneContext.Scene().Components().ContentInstances().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

template <typename Mutator>
[[nodiscard]] bool EditStreamFocus(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string_view label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) return false;
    kb::scene::StreamFocusComponent* focus = sceneContext.Scene().Components().StreamFocuses().TryGet(entity);
    if (focus == nullptr || !mutator(*focus)) {
        sceneContext.CancelSceneEditTransaction();
        return false;
    }
    sceneContext.Scene().Components().StreamFocuses().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

template <typename Mutator>
[[nodiscard]] bool EditWorldBackdrop(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string_view label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) return false;
    kb::scene::WorldBackdropComponent* backdrop = sceneContext.Scene().Components().WorldBackdrops().TryGet(entity);
    if (backdrop == nullptr || !mutator(*backdrop)) {
        sceneContext.CancelSceneEditTransaction();
        return false;
    }
    sceneContext.Scene().Components().WorldBackdrops().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

template <typename Mutator>
[[nodiscard]] bool EditAmbientRadiance(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string_view label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) return false;
    kb::scene::AmbientRadianceComponent* ambient = sceneContext.Scene().Components().AmbientRadiances().TryGet(entity);
    if (ambient == nullptr || !mutator(*ambient)) { sceneContext.CancelSceneEditTransaction(); return false; }
    sceneContext.Scene().Components().AmbientRadiances().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

template <typename Mutator>
[[nodiscard]] bool EditDetailSwitch(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string_view label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) return false;
    kb::scene::SceneDetailSwitchComponent* detailSwitch = sceneContext.Scene().Components().DetailSwitches().TryGet(entity);
    if (detailSwitch == nullptr || !mutator(*detailSwitch)) { sceneContext.CancelSceneEditTransaction(); return false; }
    sceneContext.Scene().Components().DetailSwitches().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

template <typename Mutator>
[[nodiscard]] bool EditVisibilityBlocker(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string_view label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) return false;
    kb::scene::SceneVisibilityBlockerComponent* blocker = sceneContext.Scene().Components().VisibilityBlockers().TryGet(entity);
    if (blocker == nullptr || !mutator(*blocker)) { sceneContext.CancelSceneEditTransaction(); return false; }
    sceneContext.Scene().Components().VisibilityBlockers().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

template <typename Mutator>
[[nodiscard]] bool EditVisibilityCell(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string_view label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) return false;
    kb::scene::VisibilityCellComponent* cell = sceneContext.Scene().Components().VisibilityCells().TryGet(entity);
    if (cell == nullptr || !mutator(*cell)) { sceneContext.CancelSceneEditTransaction(); return false; }
    sceneContext.Scene().Components().VisibilityCells().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

template <typename Mutator>
[[nodiscard]] bool EditRegionPortal(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string_view label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) return false;
    kb::scene::SceneRegionPortalComponent* portal = sceneContext.Scene().Components().RegionPortals().TryGet(entity);
    if (portal == nullptr || !mutator(*portal)) { sceneContext.CancelSceneEditTransaction(); return false; }
    sceneContext.Scene().Components().RegionPortals().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

template <typename Mutator>
[[nodiscard]] bool EditSecondaryFrame(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string_view label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) return false;
    kb::scene::AuxFrameComponent* frame = sceneContext.Scene().Components().AuxFrames().TryGet(entity);
    if (frame == nullptr || !mutator(*frame)) { sceneContext.CancelSceneEditTransaction(); return false; }
    sceneContext.Scene().Components().AuxFrames().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

template <typename Mutator>
[[nodiscard]] bool EditGeometrySwarm(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string_view label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) return false;
    kb::scene::GeometrySwarmComponent* swarm = sceneContext.Scene().Components().GeometrySwarms().TryGet(entity);
    if (swarm == nullptr || !mutator(*swarm)) { sceneContext.CancelSceneEditTransaction(); return false; }
    sceneContext.Scene().Components().GeometrySwarms().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}
template <typename Mutator>
[[nodiscard]] bool EditSurfaceCast(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string_view label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) return false;
    kb::scene::SurfaceCastComponent* surfaceCast = sceneContext.Scene().Components().SurfaceCasts().TryGet(entity);
    if (surfaceCast == nullptr || !mutator(*surfaceCast)) { sceneContext.CancelSceneEditTransaction(); return false; }
    sceneContext.Scene().Components().SurfaceCasts().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}
template <typename Mutator>
[[nodiscard]] bool EditFacingPanel(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string_view label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) return false;
    kb::scene::FacingPanelComponent* panel = sceneContext.Scene().Components().FacingPanels().TryGet(entity);
    if (panel == nullptr || !mutator(*panel)) { sceneContext.CancelSceneEditTransaction(); return false; }
    sceneContext.Scene().Components().FacingPanels().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}
template <typename Mutator>
[[nodiscard]] bool EditSpaceStroke(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string_view label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) return false;
    kb::scene::SpaceStrokeComponent* stroke = sceneContext.Scene().Components().SpaceStrokes().TryGet(entity);
    if (stroke == nullptr || !mutator(*stroke)) { sceneContext.CancelSceneEditTransaction(); return false; }
    sceneContext.Scene().Components().SpaceStrokes().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}
template <typename Mutator>
[[nodiscard]] bool EditHistoryRibbon(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, std::string_view label, Mutator mutator) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) return false;
    kb::scene::HistoryRibbonComponent* ribbon = sceneContext.Scene().Components().HistoryRibbons().TryGet(entity);
    if (ribbon == nullptr || !mutator(*ribbon)) { sceneContext.CancelSceneEditTransaction(); return false; }
    sceneContext.Scene().Components().HistoryRibbons().MarkModified(entity);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

[[nodiscard]] std::string NavAgentFieldValue(const kb::scene::NavAgent& agent, InspectorPropertyId property) {
    switch (property) {
    case InspectorPropertyId::NavAgentRadius: return FormatCompactFloat(agent.radius);
    case InspectorPropertyId::NavAgentHeight: return FormatCompactFloat(agent.height);
    case InspectorPropertyId::NavAgentMaxSpeed: return FormatCompactFloat(agent.maxSpeed);
    case InspectorPropertyId::NavAgentAcceleration: return FormatCompactFloat(agent.acceleration);
    case InspectorPropertyId::NavAgentAngularSpeed: return FormatCompactFloat(agent.angularSpeedDegrees);
    case InspectorPropertyId::NavAgentStoppingDistance: return FormatCompactFloat(agent.stoppingDistance);
    case InspectorPropertyId::NavAgentAreaMask: return std::to_string(agent.areaMask);
    case InspectorPropertyId::NavAgentDestinationX: return FormatCompactFloat(agent.destination.x);
    case InspectorPropertyId::NavAgentDestinationY: return FormatCompactFloat(agent.destination.y);
    case InspectorPropertyId::NavAgentDestinationZ: return FormatCompactFloat(agent.destination.z);
    default: return {};
    }
}

[[nodiscard]] std::string NavObstacleFieldValue(const kb::scene::NavObstacle& obstacle, InspectorPropertyId property) {
    switch (property) {
    case InspectorPropertyId::NavObstacleRadius: return FormatCompactFloat(obstacle.radius);
    case InspectorPropertyId::NavObstacleHeight: return FormatCompactFloat(obstacle.height);
    case InspectorPropertyId::NavObstacleArea: return std::to_string(obstacle.area);
    case InspectorPropertyId::NavObstacleCenterX: return FormatCompactFloat(obstacle.center.x);
    case InspectorPropertyId::NavObstacleCenterY: return FormatCompactFloat(obstacle.center.y);
    case InspectorPropertyId::NavObstacleCenterZ: return FormatCompactFloat(obstacle.center.z);
    case InspectorPropertyId::NavObstacleSizeX: return FormatCompactFloat(obstacle.size.x);
    case InspectorPropertyId::NavObstacleSizeY: return FormatCompactFloat(obstacle.size.y);
    case InspectorPropertyId::NavObstacleSizeZ: return FormatCompactFloat(obstacle.size.z);
    default: return {};
    }
}

[[nodiscard]] std::string RegionShapeFieldValue(const kb::scene::RegionShapeComponent& shape, InspectorPropertyId property) {
    switch (property) {
    case InspectorPropertyId::RegionShapeCenterX: return FormatCompactFloat(shape.center.x);
    case InspectorPropertyId::RegionShapeCenterY: return FormatCompactFloat(shape.center.y);
    case InspectorPropertyId::RegionShapeCenterZ: return FormatCompactFloat(shape.center.z);
    case InspectorPropertyId::RegionShapeSizeX: return FormatCompactFloat(shape.size.x);
    case InspectorPropertyId::RegionShapeSizeY: return FormatCompactFloat(shape.size.y);
    case InspectorPropertyId::RegionShapeSizeZ: return FormatCompactFloat(shape.size.z);
    case InspectorPropertyId::RegionShapeRadius: return FormatCompactFloat(shape.radius);
    case InspectorPropertyId::RegionShapeHeight: return FormatCompactFloat(shape.height);
    default: return {};
    }
}

[[nodiscard]] bool HandleNavAgentClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::NavAgent* agent = sceneContext.Scene().Components().NavAgents().TryGet(entity);
    if (agent == nullptr) return false;
    if (hit.property == InspectorPropertyId::NavAgentEnabled) {
        return EditNavAgent(sceneContext, entity, "Toggle Nav Agent", [](kb::scene::NavAgent& value) { value.enabled = !value.enabled; return true; });
    }
    if (IsNavAgentProperty(hit.property)) {
        sceneContext.Inspector().BeginTextEdit(hit.property, NavAgentFieldValue(*agent, hit.property));
    }
    return true;
}

[[nodiscard]] bool HandleNavObstacleClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::NavObstacle* obstacle = sceneContext.Scene().Components().NavObstacles().TryGet(entity);
    if (obstacle == nullptr) return false;
    if (hit.property == InspectorPropertyId::NavObstacleCarve) {
        return EditNavObstacle(sceneContext, entity, "Toggle Nav Obstacle Carve", [](kb::scene::NavObstacle& value) { value.carve = !value.carve; return true; });
    }
    if (hit.property == InspectorPropertyId::NavObstacleEnabled) {
        return EditNavObstacle(sceneContext, entity, "Toggle Nav Obstacle", [](kb::scene::NavObstacle& value) { value.enabled = !value.enabled; return true; });
    }
    if (hit.property == InspectorPropertyId::NavObstacleShape) {
        return EditNavObstacle(sceneContext, entity, "Change Nav Obstacle Shape", [](kb::scene::NavObstacle& value) {
            value.shape = value.shape == kb::scene::NavObstacleShape::Box ? kb::scene::NavObstacleShape::Cylinder : kb::scene::NavObstacleShape::Box;
            return true;
        });
    }
    if (IsNavObstacleProperty(hit.property)) {
        sceneContext.Inspector().BeginTextEdit(hit.property, NavObstacleFieldValue(*obstacle, hit.property));
    }
    return true;
}

[[nodiscard]] bool HandleRegionShapeClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::RegionShapeComponent* shape = sceneContext.Scene().Components().RegionShapes().TryGet(entity);
    if (shape == nullptr) return false;
    if (hit.property == InspectorPropertyId::RegionShapeEnabled) {
        return EditRegionShape(sceneContext, entity, "Toggle Region Shape", [](kb::scene::RegionShapeComponent& value) { value.enabled = !value.enabled; return true; });
    }
    if (hit.property == InspectorPropertyId::RegionShapeKind) {
        return EditRegionShape(sceneContext, entity, "Change Region Shape", [](kb::scene::RegionShapeComponent& value) {
            const auto next = static_cast<std::uint8_t>(value.kind) + 1U;
            value.kind = next > static_cast<std::uint8_t>(kb::scene::RegionShapeKind::Capsule)
                ? kb::scene::RegionShapeKind::Circle2D : static_cast<kb::scene::RegionShapeKind>(next);
            return true;
        });
    }
    if (IsRegionShapeProperty(hit.property)) {
        sceneContext.Inspector().BeginTextEdit(hit.property, RegionShapeFieldValue(*shape, hit.property));
    }
    return true;
}

[[nodiscard]] bool HandleContentInstanceClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::ContentInstanceComponent* instance = sceneContext.Scene().Components().ContentInstances().TryGet(entity);
    if (instance == nullptr) return false;
    if (hit.property == InspectorPropertyId::ContentInstanceActive) {
        return EditContentInstance(sceneContext, entity, "Toggle Content Instance", [](kb::scene::ContentInstanceComponent& value) { value.active = !value.active; return true; });
    }
    if (hit.property == InspectorPropertyId::ContentInstanceKind) {
        return EditContentInstance(sceneContext, entity, "Change Content Instance Source Type", [](kb::scene::ContentInstanceComponent& value) {
            const auto next = static_cast<std::uint8_t>(value.kind) + 1U;
            value.kind = next > static_cast<std::uint8_t>(kb::scene::ContentInstanceKind::WorldFragment)
                ? kb::scene::ContentInstanceKind::Prefab : static_cast<kb::scene::ContentInstanceKind>(next);
            return true;
        });
    }
    if (hit.property == InspectorPropertyId::ContentInstanceLifetime) {
        return EditContentInstance(sceneContext, entity, "Change Content Instance Lifetime", [](kb::scene::ContentInstanceComponent& value) {
            value.lifetime = value.lifetime == kb::scene::ContentInstanceLifetime::Owner
                ? kb::scene::ContentInstanceLifetime::Persistent : kb::scene::ContentInstanceLifetime::Owner;
            return true;
        });
    }
    if (hit.property == InspectorPropertyId::ContentInstanceAssetId) {
        sceneContext.Inspector().BeginTextEdit(hit.property, std::to_string(instance->assetId));
    }
    return true;
}

[[nodiscard]] bool HandleStreamFocusClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::StreamFocusComponent* focus = sceneContext.Scene().Components().StreamFocuses().TryGet(entity);
    if (focus == nullptr) return false;
    if (hit.property == InspectorPropertyId::StreamFocusEnabled) {
        return EditStreamFocus(sceneContext, entity, "Toggle Stream Focus", [](kb::scene::StreamFocusComponent& value) { value.enabled = !value.enabled; return true; });
    }
    if (IsStreamFocusProperty(hit.property)) {
        switch (hit.property) {
        case InspectorPropertyId::StreamFocusInnerRadius: sceneContext.Inspector().BeginTextEdit(hit.property, FormatCompactFloat(focus->innerRadius)); break;
        case InspectorPropertyId::StreamFocusOuterRadius: sceneContext.Inspector().BeginTextEdit(hit.property, FormatCompactFloat(focus->outerRadius)); break;
        case InspectorPropertyId::StreamFocusPriority: sceneContext.Inspector().BeginTextEdit(hit.property, std::to_string(focus->priority)); break;
        case InspectorPropertyId::StreamFocusLoadMask: sceneContext.Inspector().BeginTextEdit(hit.property, std::to_string(static_cast<std::uint32_t>(focus->loadMask))); break;
        default: break;
        }
    }
    return true;
}

[[nodiscard]] std::string WorldBackdropFieldText(const kb::scene::WorldBackdropComponent& value, InspectorPropertyId property) {
    switch (property) {
    case InspectorPropertyId::WorldBackdropMode: return std::to_string(static_cast<int>(value.mode));
    case InspectorPropertyId::WorldBackdropColorR: return FormatCompactFloat(value.color.x);
    case InspectorPropertyId::WorldBackdropColorG: return FormatCompactFloat(value.color.y);
    case InspectorPropertyId::WorldBackdropColorB: return FormatCompactFloat(value.color.z);
    case InspectorPropertyId::WorldBackdropHorizonColorR: return FormatCompactFloat(value.horizonColor.x);
    case InspectorPropertyId::WorldBackdropHorizonColorG: return FormatCompactFloat(value.horizonColor.y);
    case InspectorPropertyId::WorldBackdropHorizonColorB: return FormatCompactFloat(value.horizonColor.z);
    case InspectorPropertyId::WorldBackdropZenithColorR: return FormatCompactFloat(value.zenithColor.x);
    case InspectorPropertyId::WorldBackdropZenithColorG: return FormatCompactFloat(value.zenithColor.y);
    case InspectorPropertyId::WorldBackdropZenithColorB: return FormatCompactFloat(value.zenithColor.z);
    case InspectorPropertyId::WorldBackdropEnvironmentAssetId: return std::to_string(value.environmentAssetId);
    case InspectorPropertyId::WorldBackdropHorizonHeight: return FormatCompactFloat(value.horizonHeight);
    case InspectorPropertyId::WorldBackdropGradientExponent: return FormatCompactFloat(value.gradientExponent);
    case InspectorPropertyId::WorldBackdropPriority: return std::to_string(value.priority);
    default: return {};
    }
}

[[nodiscard]] kb::scene::WorldBackdropMode NextWorldBackdropMode(kb::scene::WorldBackdropMode mode) noexcept {
    switch (mode) {
    case kb::scene::WorldBackdropMode::SolidColor: return kb::scene::WorldBackdropMode::VerticalGradient;
    case kb::scene::WorldBackdropMode::VerticalGradient: return kb::scene::WorldBackdropMode::EnvironmentMap;
    case kb::scene::WorldBackdropMode::EnvironmentMap: return kb::scene::WorldBackdropMode::ProceduralSky;
    case kb::scene::WorldBackdropMode::ProceduralSky: return kb::scene::WorldBackdropMode::SolidColor;
    }
    return kb::scene::WorldBackdropMode::SolidColor;
}

[[nodiscard]] bool HandleWorldBackdropClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::WorldBackdropComponent* backdrop = sceneContext.Scene().Components().WorldBackdrops().TryGet(entity);
    if (backdrop == nullptr) return false;
    if (hit.property == InspectorPropertyId::WorldBackdropEnabled) {
        return EditWorldBackdrop(sceneContext, entity, "Toggle World Backdrop", [](kb::scene::WorldBackdropComponent& value) { value.enabled = !value.enabled; return true; });
    }
    if (hit.property == InspectorPropertyId::WorldBackdropMode) {
        return EditWorldBackdrop(sceneContext, entity, "Set World Backdrop Mode", [](kb::scene::WorldBackdropComponent& value) {
            value.mode = NextWorldBackdropMode(value.mode);
            return true;
        });
    }
    if (IsWorldBackdropProperty(hit.property)) {
        sceneContext.Inspector().BeginTextEdit(hit.property, WorldBackdropFieldText(*backdrop, hit.property));
    }
    return true;
}

[[nodiscard]] std::string AmbientRadianceFieldText(const kb::scene::AmbientRadianceComponent& value, InspectorPropertyId property) {
    switch (property) {
    case InspectorPropertyId::AmbientRadianceMode: return std::to_string(static_cast<int>(value.mode));
    case InspectorPropertyId::AmbientRadianceColorR: return FormatCompactFloat(value.color.x);
    case InspectorPropertyId::AmbientRadianceColorG: return FormatCompactFloat(value.color.y);
    case InspectorPropertyId::AmbientRadianceColorB: return FormatCompactFloat(value.color.z);
    case InspectorPropertyId::AmbientRadianceHorizonColorR: return FormatCompactFloat(value.horizonColor.x);
    case InspectorPropertyId::AmbientRadianceHorizonColorG: return FormatCompactFloat(value.horizonColor.y);
    case InspectorPropertyId::AmbientRadianceHorizonColorB: return FormatCompactFloat(value.horizonColor.z);
    case InspectorPropertyId::AmbientRadianceZenithColorR: return FormatCompactFloat(value.zenithColor.x);
    case InspectorPropertyId::AmbientRadianceZenithColorG: return FormatCompactFloat(value.zenithColor.y);
    case InspectorPropertyId::AmbientRadianceZenithColorB: return FormatCompactFloat(value.zenithColor.z);
    case InspectorPropertyId::AmbientRadianceEnvironmentAssetId: return std::to_string(value.environmentAssetId);
    case InspectorPropertyId::AmbientRadianceIntensity: return FormatCompactFloat(value.intensity);
    case InspectorPropertyId::AmbientRadianceDiffuseIntensity: return FormatCompactFloat(value.diffuseIntensity);
    case InspectorPropertyId::AmbientRadianceSpecularIntensity: return FormatCompactFloat(value.specularIntensity);
    case InspectorPropertyId::AmbientRadiancePriority: return std::to_string(value.priority);
    default: return {};
    }
}

[[nodiscard]] bool HandleAmbientRadianceClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::AmbientRadianceComponent* ambient = sceneContext.Scene().Components().AmbientRadiances().TryGet(entity);
    if (ambient == nullptr) return false;
    if (hit.property == InspectorPropertyId::AmbientRadianceEnabled) {
        return EditAmbientRadiance(sceneContext, entity, "Toggle Ambient Radiance", [](kb::scene::AmbientRadianceComponent& value) { value.enabled = !value.enabled; return true; });
    }
    if (hit.property == InspectorPropertyId::AmbientRadianceMode) {
        return EditAmbientRadiance(sceneContext, entity, "Set Ambient Radiance Mode", [](kb::scene::AmbientRadianceComponent& value) {
            const auto next = static_cast<std::uint8_t>(value.mode) + 1U;
            value.mode = next > static_cast<std::uint8_t>(kb::scene::AmbientRadianceMode::EstimatedEnvironment)
                ? kb::scene::AmbientRadianceMode::Constant : static_cast<kb::scene::AmbientRadianceMode>(next);
            return true;
        });
    }
    if (IsAmbientRadianceProperty(hit.property)) sceneContext.Inspector().BeginTextEdit(hit.property, AmbientRadianceFieldText(*ambient, hit.property));
    return true;
}

[[nodiscard]] std::string DetailSwitchFieldText(const kb::scene::SceneDetailSwitchComponent& value, InspectorPropertyId property) {
    switch (property) {
    case InspectorPropertyId::DetailSwitchGroupId: return std::to_string(value.groupId);
    case InspectorPropertyId::DetailSwitchMinimumLod: return std::to_string(value.minimumLod);
    case InspectorPropertyId::DetailSwitchMaximumLod: return std::to_string(value.maximumLod);
    case InspectorPropertyId::DetailSwitchPromoteCoverage: return FormatCompactFloat(value.promoteCoverage);
    case InspectorPropertyId::DetailSwitchDemoteCoverage: return FormatCompactFloat(value.demoteCoverage);
    default: return {};
    }
}

[[nodiscard]] bool HandleDetailSwitchClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::SceneDetailSwitchComponent* detailSwitch = sceneContext.Scene().Components().DetailSwitches().TryGet(entity);
    if (detailSwitch == nullptr) return false;
    if (hit.property == InspectorPropertyId::DetailSwitchEnabled) {
        return EditDetailSwitch(sceneContext, entity, "Toggle Detail Switch", [](kb::scene::SceneDetailSwitchComponent& value) { value.enabled = !value.enabled; return true; });
    }
    if (IsDetailSwitchProperty(hit.property)) sceneContext.Inspector().BeginTextEdit(hit.property, DetailSwitchFieldText(*detailSwitch, hit.property));
    return true;
}

[[nodiscard]] std::string VisibilityBlockerFieldText(const kb::scene::SceneVisibilityBlockerComponent& value, InspectorPropertyId property) {
    switch (property) {
    case InspectorPropertyId::VisibilityBlockerCenterX: return FormatCompactFloat(value.localCenter.x);
    case InspectorPropertyId::VisibilityBlockerCenterY: return FormatCompactFloat(value.localCenter.y);
    case InspectorPropertyId::VisibilityBlockerCenterZ: return FormatCompactFloat(value.localCenter.z);
    case InspectorPropertyId::VisibilityBlockerSizeX: return FormatCompactFloat(value.size.x);
    case InspectorPropertyId::VisibilityBlockerSizeY: return FormatCompactFloat(value.size.y);
    case InspectorPropertyId::VisibilityBlockerSizeZ: return FormatCompactFloat(value.size.z);
    default: return {};
    }
}

[[nodiscard]] bool HandleVisibilityBlockerClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::SceneVisibilityBlockerComponent* blocker = sceneContext.Scene().Components().VisibilityBlockers().TryGet(entity);
    if (blocker == nullptr) return false;
    if (hit.property == InspectorPropertyId::VisibilityBlockerEnabled) return EditVisibilityBlocker(sceneContext, entity, "Toggle Visibility Blocker", [](kb::scene::SceneVisibilityBlockerComponent& value) { value.enabled = !value.enabled; return true; });
    if (IsVisibilityBlockerProperty(hit.property)) sceneContext.Inspector().BeginTextEdit(hit.property, VisibilityBlockerFieldText(*blocker, hit.property));
    return true;
}

[[nodiscard]] std::string VisibilityCellFieldText(const kb::scene::VisibilityCellComponent& value, InspectorPropertyId property) {
    switch (property) {
    case InspectorPropertyId::VisibilityCellMembershipMask: return std::to_string(value.membershipMask);
    case InspectorPropertyId::VisibilityCellMembership: return std::to_string(static_cast<int>(value.membership));
    case InspectorPropertyId::VisibilityCellOverride: return std::to_string(static_cast<int>(value.visibilityOverride));
    default: return {};
    }
}

[[nodiscard]] bool HandleVisibilityCellClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::VisibilityCellComponent* cell = sceneContext.Scene().Components().VisibilityCells().TryGet(entity);
    if (cell == nullptr) return false;
    if (hit.property == InspectorPropertyId::VisibilityCellEnabled) return EditVisibilityCell(sceneContext, entity, "Toggle Visibility Cell", [](kb::scene::VisibilityCellComponent& value) { value.enabled = !value.enabled; return true; });
    if (IsVisibilityCellProperty(hit.property)) sceneContext.Inspector().BeginTextEdit(hit.property, VisibilityCellFieldText(*cell, hit.property));
    return true;
}

[[nodiscard]] bool HandleRegionPortalClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::SceneRegionPortalComponent* portal = sceneContext.Scene().Components().RegionPortals().TryGet(entity);
    if (portal == nullptr) return false;
    if (hit.property == InspectorPropertyId::RegionPortalEnabled) return EditRegionPortal(sceneContext, entity, "Toggle Region Portal", [](kb::scene::SceneRegionPortalComponent& value) { value.enabled = !value.enabled; return true; });
    switch (hit.property) {
    case InspectorPropertyId::RegionPortalSourceCell:
        sceneContext.Inspector().BeginTextEdit(hit.property, std::to_string(portal->sourceCell.Id()));
        break;
    case InspectorPropertyId::RegionPortalTargetCell:
        sceneContext.Inspector().BeginTextEdit(hit.property, std::to_string(portal->targetCell.Id()));
        break;
    case InspectorPropertyId::RegionPortalPurposes:
        sceneContext.Inspector().BeginTextEdit(hit.property, std::to_string(portal->purposes));
        break;
    default:
        break;
    }
    return true;
}

[[nodiscard]] std::string SecondaryFrameFieldText(const kb::scene::AuxFrameComponent& value, InspectorPropertyId property) {
    switch (property) {
    case InspectorPropertyId::SecondaryFrameMode: return std::to_string(static_cast<int>(value.mode));
    case InspectorPropertyId::SecondaryFrameImageTargetId: return std::to_string(value.imageTargetId);
    case InspectorPropertyId::SecondaryFrameWidth: return std::to_string(value.width);
    case InspectorPropertyId::SecondaryFrameHeight: return std::to_string(value.height);
    case InspectorPropertyId::SecondaryFramePlaneNormalX: return FormatCompactFloat(value.mirrorPlaneNormal.x);
    case InspectorPropertyId::SecondaryFramePlaneNormalY: return FormatCompactFloat(value.mirrorPlaneNormal.y);
    case InspectorPropertyId::SecondaryFramePlaneNormalZ: return FormatCompactFloat(value.mirrorPlaneNormal.z);
    case InspectorPropertyId::SecondaryFramePlaneOffset: return FormatCompactFloat(value.mirrorPlaneOffset);
    default: return {};
    }
}

[[nodiscard]] bool HandleSecondaryFrameClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::AuxFrameComponent* frame = sceneContext.Scene().Components().AuxFrames().TryGet(entity);
    if (frame == nullptr) return false;
    if (hit.property == InspectorPropertyId::SecondaryFrameEnabled) {
        return EditSecondaryFrame(sceneContext, entity, "Toggle Secondary Frame", [](kb::scene::AuxFrameComponent& value) {
            kb::scene::AuxFrameComponent candidate = value;
            candidate.enabled = !candidate.enabled;
            if (!kb::scene::IsAuxFrameComponentPersistable(candidate)) return false;
            value = candidate;
            return true;
        });
    }
    if (IsSecondaryFrameProperty(hit.property)) sceneContext.Inspector().BeginTextEdit(hit.property, SecondaryFrameFieldText(*frame, hit.property));
    return true;
}

[[nodiscard]] bool HandleGeometrySwarmClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::GeometrySwarmComponent* swarm = sceneContext.Scene().Components().GeometrySwarms().TryGet(entity);
    if (swarm == nullptr) return false;
    if (hit.property == InspectorPropertyId::GeometrySwarmCastsShadow) return EditGeometrySwarm(sceneContext, entity, "Toggle Geometry Swarm Shadows", [](auto& value) { value.castsShadow = !value.castsShadow; return true; });
    if (hit.property == InspectorPropertyId::GeometrySwarmReceivesShadow) return EditGeometrySwarm(sceneContext, entity, "Toggle Geometry Swarm Shadow Reception", [](auto& value) { value.receivesShadow = !value.receivesShadow; return true; });
    if (hit.property == InspectorPropertyId::GeometrySwarmEnabled) return EditGeometrySwarm(sceneContext, entity, "Toggle Geometry Swarm", [](auto& value) { value.enabled = !value.enabled; return kb::scene::IsGeometrySwarmComponentPersistable(value); });
    if (IsGeometrySwarmProperty(hit.property)) {
        std::string text;
        switch (hit.property) {
        case InspectorPropertyId::GeometrySwarmMeshAssetId: text = std::to_string(swarm->meshAssetId); break;
        case InspectorPropertyId::GeometrySwarmMaterialAssetId: text = std::to_string(swarm->materialAssetId); break;
        case InspectorPropertyId::GeometrySwarmInstanceCount: text = std::to_string(swarm->instanceCount); break;
        case InspectorPropertyId::GeometrySwarmColumns: text = std::to_string(swarm->columns); break;
        case InspectorPropertyId::GeometrySwarmRows: text = std::to_string(swarm->rows); break;
        case InspectorPropertyId::GeometrySwarmLayers: text = std::to_string(swarm->layers); break;
        case InspectorPropertyId::GeometrySwarmLayer: text = std::to_string(swarm->layer); break;
        case InspectorPropertyId::GeometrySwarmSpacingX: text = FormatCompactFloat(swarm->spacing.x); break;
        case InspectorPropertyId::GeometrySwarmSpacingY: text = FormatCompactFloat(swarm->spacing.y); break;
        case InspectorPropertyId::GeometrySwarmSpacingZ: text = FormatCompactFloat(swarm->spacing.z); break;
        case InspectorPropertyId::GeometrySwarmInstanceScale: text = FormatCompactFloat(swarm->instanceScale); break;
        default: return false;
        }
        sceneContext.Inspector().BeginTextEdit(hit.property, std::move(text));
    }
    return true;
}
[[nodiscard]] bool HandleSurfaceCastClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::SurfaceCastComponent* surfaceCast = sceneContext.Scene().Components().SurfaceCasts().TryGet(entity);
    if (surfaceCast == nullptr) return false;
    if (hit.property == InspectorPropertyId::SurfaceCastEnabled) return EditSurfaceCast(sceneContext, entity, "Toggle Surface Cast", [](auto& value) { value.enabled = !value.enabled; return kb::scene::IsSurfaceCastComponentPersistable(value); });
    if (hit.property == InspectorPropertyId::SurfaceCastContent) return EditSurfaceCast(sceneContext, entity, "Change Surface Cast Content", [](auto& value) { value.content = value.content == kb::scene::SurfaceCastContent::Material ? kb::scene::SurfaceCastContent::Detail : kb::scene::SurfaceCastContent::Material; return true; });
    if (IsSurfaceCastProperty(hit.property)) {
        std::string text;
        switch (hit.property) {
        case InspectorPropertyId::SurfaceCastMaterialAssetId: text = std::to_string(surfaceCast->materialAssetId); break;
        case InspectorPropertyId::SurfaceCastReceiverLayerMask: text = std::to_string(surfaceCast->receiverLayerMask); break;
        case InspectorPropertyId::SurfaceCastOrder: text = std::to_string(surfaceCast->order); break;
        default: return false;
        }
        sceneContext.Inspector().BeginTextEdit(hit.property, std::move(text));
        return true;
    }
    return false;
}
[[nodiscard]] bool HandleFacingPanelClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::FacingPanelComponent* panel = sceneContext.Scene().Components().FacingPanels().TryGet(entity);
    if (panel == nullptr) return false;
    if (hit.property == InspectorPropertyId::FacingPanelEnabled) return EditFacingPanel(sceneContext, entity, "Toggle Facing Panel", [](auto& value) { value.enabled = !value.enabled; return kb::scene::IsFacingPanelComponentPersistable(value); });
    if (hit.property == InspectorPropertyId::FacingPanelMode) return EditFacingPanel(sceneContext, entity, "Change Facing Panel Mode", [](auto& value) { value.mode = static_cast<kb::scene::FacingPanelMode>((static_cast<std::uint32_t>(value.mode) + 1U) % 4U); return true; });
    if (!IsFacingPanelProperty(hit.property)) return false;
    float value = 0.0F;
    switch (hit.property) {
    case InspectorPropertyId::FacingPanelTargetX: value = panel->targetPoint.x; break;
    case InspectorPropertyId::FacingPanelTargetY: value = panel->targetPoint.y; break;
    case InspectorPropertyId::FacingPanelTargetZ: value = panel->targetPoint.z; break;
    case InspectorPropertyId::FacingPanelAxisX: value = panel->axis.x; break;
    case InspectorPropertyId::FacingPanelAxisY: value = panel->axis.y; break;
    case InspectorPropertyId::FacingPanelAxisZ: value = panel->axis.z; break;
    case InspectorPropertyId::FacingPanelUpX: value = panel->up.x; break;
    case InspectorPropertyId::FacingPanelUpY: value = panel->up.y; break;
    case InspectorPropertyId::FacingPanelUpZ: value = panel->up.z; break;
    default: return false;
    }
    sceneContext.Inspector().BeginTextEdit(hit.property, FormatCompactFloat(value));
    return true;
}
[[nodiscard]] bool HandleSpaceStrokeClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::SpaceStrokeComponent* stroke = sceneContext.Scene().Components().SpaceStrokes().TryGet(entity);
    if (stroke == nullptr) return false;
    if (hit.property == InspectorPropertyId::SpaceStrokeCastsShadow) return EditSpaceStroke(sceneContext, entity, "Toggle Kreska przestrzenna Shadows", [](auto& value) { value.castsShadow = !value.castsShadow; return true; });
    if (hit.property == InspectorPropertyId::SpaceStrokeReceivesShadow) return EditSpaceStroke(sceneContext, entity, "Toggle Kreska przestrzenna Shadow Reception", [](auto& value) { value.receivesShadow = !value.receivesShadow; return true; });
    if (hit.property == InspectorPropertyId::SpaceStrokeEnabled) return EditSpaceStroke(sceneContext, entity, "Toggle Kreska przestrzenna", [](auto& value) { value.enabled = !value.enabled; return kb::scene::IsSpaceStrokeComponentPersistable(value); });
    if (hit.property == InspectorPropertyId::SpaceStrokeMode) return EditSpaceStroke(sceneContext, entity, "Change Kreska przestrzenna Mode", [](auto& value) { value.mode = static_cast<kb::scene::SpaceStrokeMode>((static_cast<std::uint32_t>(value.mode) + 1U) % 4U); return true; });
    if (!IsSpaceStrokeProperty(hit.property)) return false;
    switch (hit.property) {
    case InspectorPropertyId::SpaceStrokeMeshAssetId: sceneContext.Inspector().BeginTextEdit(hit.property, std::to_string(stroke->meshAssetId)); break;
    case InspectorPropertyId::SpaceStrokeMaterialAssetId: sceneContext.Inspector().BeginTextEdit(hit.property, std::to_string(stroke->materialAssetId)); break;
    case InspectorPropertyId::SpaceStrokeWidth: sceneContext.Inspector().BeginTextEdit(hit.property, FormatCompactFloat(stroke->width)); break;
    case InspectorPropertyId::SpaceStrokeCableSag: sceneContext.Inspector().BeginTextEdit(hit.property, FormatCompactFloat(stroke->cableSag)); break;
    case InspectorPropertyId::SpaceStrokeSplineSegments: sceneContext.Inspector().BeginTextEdit(hit.property, std::to_string(stroke->splineSegments)); break;
    case InspectorPropertyId::SpaceStrokeLayer: sceneContext.Inspector().BeginTextEdit(hit.property, std::to_string(stroke->layer)); break;
    default: return false;
    }
    return true;
}
[[nodiscard]] bool HandleHistoryRibbonClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const kb::scene::HistoryRibbonComponent* ribbon = sceneContext.Scene().Components().HistoryRibbons().TryGet(entity);
    if (ribbon == nullptr) return false;
    if (hit.property == InspectorPropertyId::HistoryRibbonCastsShadow) return EditHistoryRibbon(sceneContext, entity, "Toggle Wst\xC4\x99" "ga historii Shadows", [](auto& value) { value.castsShadow = !value.castsShadow; return true; });
    if (hit.property == InspectorPropertyId::HistoryRibbonReceivesShadow) return EditHistoryRibbon(sceneContext, entity, "Toggle Wst\xC4\x99" "ga historii Shadow Reception", [](auto& value) { value.receivesShadow = !value.receivesShadow; return true; });
    if (hit.property == InspectorPropertyId::HistoryRibbonEnabled) return EditHistoryRibbon(sceneContext, entity, "Toggle Wst\xC4\x99" "ga historii", [](auto& value) { value.enabled = !value.enabled; return kb::scene::IsHistoryRibbonComponentPersistable(value); });
    if (!IsHistoryRibbonProperty(hit.property)) return false;
    switch (hit.property) {
    case InspectorPropertyId::HistoryRibbonMeshAssetId: sceneContext.Inspector().BeginTextEdit(hit.property, std::to_string(ribbon->meshAssetId)); break;
    case InspectorPropertyId::HistoryRibbonMaterialAssetId: sceneContext.Inspector().BeginTextEdit(hit.property, std::to_string(ribbon->materialAssetId)); break;
    case InspectorPropertyId::HistoryRibbonLifetimeSeconds: sceneContext.Inspector().BeginTextEdit(hit.property, FormatCompactFloat(ribbon->lifetimeSeconds)); break;
    case InspectorPropertyId::HistoryRibbonWidth: sceneContext.Inspector().BeginTextEdit(hit.property, FormatCompactFloat(ribbon->width)); break;
    case InspectorPropertyId::HistoryRibbonSampleIntervalSeconds: sceneContext.Inspector().BeginTextEdit(hit.property, FormatCompactFloat(ribbon->sampleIntervalSeconds)); break;
    case InspectorPropertyId::HistoryRibbonLayer: sceneContext.Inspector().BeginTextEdit(hit.property, std::to_string(ribbon->layer)); break;
    default: return false;
    }
    return true;
}

[[nodiscard]] bool ApplyNavAgentText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) {
    float value = 0.0F;
    if (property == InspectorPropertyId::NavAgentAreaMask) {
        std::uint32_t mask = 0U;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), mask);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
        return EditNavAgent(sceneContext, entity, "Edit Nav Agent Area Mask", [mask](kb::scene::NavAgent& agent) { agent.areaMask = static_cast<kb::scene::NavAreaMask>(mask); return true; });
    }
    if (!ParseFloat(text, value) || !std::isfinite(value)) return false;
    return EditNavAgent(sceneContext, entity, "Edit Nav Agent", [property, value](kb::scene::NavAgent& agent) {
        switch (property) {
        case InspectorPropertyId::NavAgentRadius: if (value <= 0.0F) return false; agent.radius = value; return true;
        case InspectorPropertyId::NavAgentHeight: if (value <= 0.0F) return false; agent.height = value; return true;
        case InspectorPropertyId::NavAgentMaxSpeed: if (value < 0.0F) return false; agent.maxSpeed = value; return true;
        case InspectorPropertyId::NavAgentAcceleration: if (value < 0.0F) return false; agent.acceleration = value; return true;
        case InspectorPropertyId::NavAgentAngularSpeed: if (value < 0.0F) return false; agent.angularSpeedDegrees = value; return true;
        case InspectorPropertyId::NavAgentStoppingDistance: if (value < 0.0F) return false; agent.stoppingDistance = value; return true;
        case InspectorPropertyId::NavAgentDestinationX: agent.destination.x = value; return true;
        case InspectorPropertyId::NavAgentDestinationY: agent.destination.y = value; return true;
        case InspectorPropertyId::NavAgentDestinationZ: agent.destination.z = value; return true;
        default: return false;
        }
    });
}

[[nodiscard]] bool ApplyNavObstacleText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) {
    if (property == InspectorPropertyId::NavObstacleArea) {
        unsigned int area = 0U;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), area);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || area > 255U) return false;
        return EditNavObstacle(sceneContext, entity, "Edit Nav Obstacle Area", [area](kb::scene::NavObstacle& obstacle) { obstacle.area = static_cast<kb::scene::NavAreaId>(area); return true; });
    }
    float value = 0.0F;
    if (!ParseFloat(text, value) || !std::isfinite(value)) return false;
    return EditNavObstacle(sceneContext, entity, "Edit Nav Obstacle", [property, value](kb::scene::NavObstacle& obstacle) {
        switch (property) {
        case InspectorPropertyId::NavObstacleRadius: if (value <= 0.0F) return false; obstacle.radius = value; return true;
        case InspectorPropertyId::NavObstacleHeight: if (value <= 0.0F) return false; obstacle.height = value; return true;
        case InspectorPropertyId::NavObstacleCenterX: obstacle.center.x = value; return true;
        case InspectorPropertyId::NavObstacleCenterY: obstacle.center.y = value; return true;
        case InspectorPropertyId::NavObstacleCenterZ: obstacle.center.z = value; return true;
        case InspectorPropertyId::NavObstacleSizeX: if (value <= 0.0F) return false; obstacle.size.x = value; return true;
        case InspectorPropertyId::NavObstacleSizeY: if (value <= 0.0F) return false; obstacle.size.y = value; return true;
        case InspectorPropertyId::NavObstacleSizeZ: if (value <= 0.0F) return false; obstacle.size.z = value; return true;
        default: return false;
        }
    });
}

[[nodiscard]] bool ApplyRegionShapeText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) {
    float value = 0.0F;
    if (!ParseFloat(text, value) || !std::isfinite(value)) return false;
    return EditRegionShape(sceneContext, entity, "Edit Region Shape", [property, value](kb::scene::RegionShapeComponent& shape) {
        switch (property) {
        case InspectorPropertyId::RegionShapeCenterX: shape.center.x = value; return true;
        case InspectorPropertyId::RegionShapeCenterY: shape.center.y = value; return true;
        case InspectorPropertyId::RegionShapeCenterZ: shape.center.z = value; return true;
        case InspectorPropertyId::RegionShapeSizeX: if (value <= 0.0F) return false; shape.size.x = value; return true;
        case InspectorPropertyId::RegionShapeSizeY: if (value <= 0.0F) return false; shape.size.y = value; return true;
        case InspectorPropertyId::RegionShapeSizeZ: if (value <= 0.0F) return false; shape.size.z = value; return true;
        case InspectorPropertyId::RegionShapeRadius: if (value <= 0.0F) return false; shape.radius = value; return true;
        case InspectorPropertyId::RegionShapeHeight: if (value <= 0.0F) return false; shape.height = value; return true;
        default: return false;
        }
    });
}

[[nodiscard]] bool ApplyContentInstanceText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) {
    if (property != InspectorPropertyId::ContentInstanceAssetId) return false;
    std::uint64_t assetId = 0U;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), assetId);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
    return EditContentInstance(sceneContext, entity, "Edit Content Instance Asset", [assetId](kb::scene::ContentInstanceComponent& value) {
        value.assetId = assetId;
        return true;
    });
}

[[nodiscard]] bool ApplyStreamFocusText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) {
    if (property == InspectorPropertyId::StreamFocusPriority) {
        std::int32_t priority = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), priority);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
        return EditStreamFocus(sceneContext, entity, "Edit Stream Focus Priority", [priority](kb::scene::StreamFocusComponent& value) { value.priority = priority; return true; });
    }
    if (property == InspectorPropertyId::StreamFocusLoadMask) {
        std::uint32_t mask = 0U;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), mask);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !kb::scene::IsStreamLoadMaskValid(static_cast<kb::scene::StreamLoadMask>(mask))) return false;
        return EditStreamFocus(sceneContext, entity, "Edit Stream Focus Load Mask", [mask](kb::scene::StreamFocusComponent& value) { value.loadMask = static_cast<kb::scene::StreamLoadMask>(mask); return true; });
    }
    float radius = 0.0F;
    if (!ParseFloat(text, radius) || !std::isfinite(radius)) return false;
    return EditStreamFocus(sceneContext, entity, "Edit Stream Focus Radius", [property, radius](kb::scene::StreamFocusComponent& value) {
        if (property == InspectorPropertyId::StreamFocusInnerRadius && radius >= 0.0F && radius <= value.outerRadius) { value.innerRadius = radius; return true; }
        if (property == InspectorPropertyId::StreamFocusOuterRadius && radius >= value.innerRadius) { value.outerRadius = radius; return true; }
        return false;
    });
}

[[nodiscard]] bool ApplyWorldBackdropText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) {
    if (property == InspectorPropertyId::WorldBackdropMode || property == InspectorPropertyId::WorldBackdropPriority) {
        std::int32_t integer = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), integer);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
        return EditWorldBackdrop(sceneContext, entity, "Edit World Backdrop", [property, integer](kb::scene::WorldBackdropComponent& value) {
            kb::scene::WorldBackdropComponent candidate = value;
            if (property == InspectorPropertyId::WorldBackdropMode) candidate.mode = static_cast<kb::scene::WorldBackdropMode>(integer);
            else candidate.priority = integer;
            if (!kb::scene::IsWorldBackdropComponentValid(candidate)) return false;
            value = candidate;
            return true;
        });
    }
    if (property == InspectorPropertyId::WorldBackdropEnvironmentAssetId) {
        std::uint64_t assetId = 0U;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), assetId);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
        return EditWorldBackdrop(sceneContext, entity, "Edit World Backdrop Environment", [assetId](kb::scene::WorldBackdropComponent& value) { value.environmentAssetId = assetId; return true; });
    }
    float number = 0.0F;
    if (!ParseFloat(text, number) || !std::isfinite(number)) return false;
    return EditWorldBackdrop(sceneContext, entity, "Edit World Backdrop", [property, number](kb::scene::WorldBackdropComponent& value) {
        kb::scene::WorldBackdropComponent candidate = value;
        switch (property) {
        case InspectorPropertyId::WorldBackdropColorR: candidate.color.x = number; break;
        case InspectorPropertyId::WorldBackdropColorG: candidate.color.y = number; break;
        case InspectorPropertyId::WorldBackdropColorB: candidate.color.z = number; break;
        case InspectorPropertyId::WorldBackdropHorizonColorR: candidate.horizonColor.x = number; break;
        case InspectorPropertyId::WorldBackdropHorizonColorG: candidate.horizonColor.y = number; break;
        case InspectorPropertyId::WorldBackdropHorizonColorB: candidate.horizonColor.z = number; break;
        case InspectorPropertyId::WorldBackdropZenithColorR: candidate.zenithColor.x = number; break;
        case InspectorPropertyId::WorldBackdropZenithColorG: candidate.zenithColor.y = number; break;
        case InspectorPropertyId::WorldBackdropZenithColorB: candidate.zenithColor.z = number; break;
        case InspectorPropertyId::WorldBackdropHorizonHeight: candidate.horizonHeight = number; break;
        case InspectorPropertyId::WorldBackdropGradientExponent: candidate.gradientExponent = number; break;
        default: return false;
        }
        if (!kb::scene::IsWorldBackdropComponentValid(candidate)) return false;
        value = candidate;
        return true;
    });
}

[[nodiscard]] bool ApplyAmbientRadianceText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) {
    if (property == InspectorPropertyId::AmbientRadianceMode || property == InspectorPropertyId::AmbientRadiancePriority) {
        std::int32_t integer = 0; const auto parsed = std::from_chars(text.data(), text.data() + text.size(), integer);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
        return EditAmbientRadiance(sceneContext, entity, "Edit Ambient Radiance", [property, integer](kb::scene::AmbientRadianceComponent& value) {
            auto candidate = value;
            if (property == InspectorPropertyId::AmbientRadianceMode) candidate.mode = static_cast<kb::scene::AmbientRadianceMode>(integer); else candidate.priority = integer;
            if (!kb::scene::IsAmbientRadianceComponentValid(candidate)) return false;
            value = candidate; return true;
        });
    }
    if (property == InspectorPropertyId::AmbientRadianceEnvironmentAssetId) {
        std::uint64_t assetId = 0U; const auto parsed = std::from_chars(text.data(), text.data() + text.size(), assetId);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
        return EditAmbientRadiance(sceneContext, entity, "Edit Ambient Radiance Environment", [assetId](kb::scene::AmbientRadianceComponent& value) { value.environmentAssetId = assetId; return true; });
    }
    float number = 0.0F;
    if (!ParseFloat(text, number) || !std::isfinite(number)) return false;
    return EditAmbientRadiance(sceneContext, entity, "Edit Ambient Radiance", [property, number](kb::scene::AmbientRadianceComponent& value) {
        auto candidate = value;
        switch (property) {
        case InspectorPropertyId::AmbientRadianceColorR: candidate.color.x = number; break;
        case InspectorPropertyId::AmbientRadianceColorG: candidate.color.y = number; break;
        case InspectorPropertyId::AmbientRadianceColorB: candidate.color.z = number; break;
        case InspectorPropertyId::AmbientRadianceHorizonColorR: candidate.horizonColor.x = number; break;
        case InspectorPropertyId::AmbientRadianceHorizonColorG: candidate.horizonColor.y = number; break;
        case InspectorPropertyId::AmbientRadianceHorizonColorB: candidate.horizonColor.z = number; break;
        case InspectorPropertyId::AmbientRadianceZenithColorR: candidate.zenithColor.x = number; break;
        case InspectorPropertyId::AmbientRadianceZenithColorG: candidate.zenithColor.y = number; break;
        case InspectorPropertyId::AmbientRadianceZenithColorB: candidate.zenithColor.z = number; break;
        case InspectorPropertyId::AmbientRadianceIntensity: candidate.intensity = number; break;
        case InspectorPropertyId::AmbientRadianceDiffuseIntensity: candidate.diffuseIntensity = number; break;
        case InspectorPropertyId::AmbientRadianceSpecularIntensity: candidate.specularIntensity = number; break;
        default: return false;
        }
        if (!kb::scene::IsAmbientRadianceComponentValid(candidate)) return false;
        value = candidate; return true;
    });
}

[[nodiscard]] bool ApplyDetailSwitchText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) {
    return EditDetailSwitch(sceneContext, entity, "Edit Detail Switch", [property, text](kb::scene::SceneDetailSwitchComponent& value) {
        kb::scene::SceneDetailSwitchComponent candidate = value;
        if (property == InspectorPropertyId::DetailSwitchGroupId) {
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), candidate.groupId);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
        } else if (property == InspectorPropertyId::DetailSwitchMinimumLod || property == InspectorPropertyId::DetailSwitchMaximumLod) {
            std::uint32_t lod = 0U;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), lod);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
            if (property == InspectorPropertyId::DetailSwitchMinimumLod) candidate.minimumLod = lod; else candidate.maximumLod = lod;
        } else {
            float coverage = 0.0F;
            if (!ParseFloat(text, coverage) || !std::isfinite(coverage)) return false;
            if (property == InspectorPropertyId::DetailSwitchPromoteCoverage) candidate.promoteCoverage = coverage;
            else if (property == InspectorPropertyId::DetailSwitchDemoteCoverage) candidate.demoteCoverage = coverage;
            else return false;
        }
        if (!kb::scene::IsSceneDetailSwitchComponentValid(candidate)) return false;
        value = candidate;
        return true;
    });
}

[[nodiscard]] bool ApplyVisibilityBlockerText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) {
    float parsed = 0.0F;
    if (!ParseFloat(text, parsed) || !std::isfinite(parsed)) return false;
    return EditVisibilityBlocker(sceneContext, entity, "Edit Visibility Blocker", [property, parsed](kb::scene::SceneVisibilityBlockerComponent& value) {
        kb::scene::SceneVisibilityBlockerComponent candidate = value;
        switch (property) {
        case InspectorPropertyId::VisibilityBlockerCenterX: candidate.localCenter.x = parsed; break;
        case InspectorPropertyId::VisibilityBlockerCenterY: candidate.localCenter.y = parsed; break;
        case InspectorPropertyId::VisibilityBlockerCenterZ: candidate.localCenter.z = parsed; break;
        case InspectorPropertyId::VisibilityBlockerSizeX: candidate.size.x = parsed; break;
        case InspectorPropertyId::VisibilityBlockerSizeY: candidate.size.y = parsed; break;
        case InspectorPropertyId::VisibilityBlockerSizeZ: candidate.size.z = parsed; break;
        default: return false;
        }
        if (!kb::scene::IsSceneVisibilityBlockerComponentValid(candidate)) return false;
        value = candidate;
        return true;
    });
}

[[nodiscard]] bool ApplyVisibilityCellText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) {
    std::uint32_t parsed = 0U;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
    return EditVisibilityCell(sceneContext, entity, "Edit Visibility Cell", [property, parsed](kb::scene::VisibilityCellComponent& value) {
        kb::scene::VisibilityCellComponent candidate = value;
        switch (property) {
        case InspectorPropertyId::VisibilityCellMembershipMask: candidate.membershipMask = parsed; break;
        case InspectorPropertyId::VisibilityCellMembership:
            if (parsed > static_cast<std::uint32_t>(kb::scene::VisibilityCellMembership::Exclude)) return false;
            candidate.membership = static_cast<kb::scene::VisibilityCellMembership>(parsed); break;
        case InspectorPropertyId::VisibilityCellOverride:
            if (parsed > static_cast<std::uint32_t>(kb::scene::VisibilityCellOverride::ForceHidden)) return false;
            candidate.visibilityOverride = static_cast<kb::scene::VisibilityCellOverride>(parsed); break;
        default: return false;
        }
        if (!kb::scene::IsVisibilityCellComponentValid(candidate)) return false;
        value = candidate;
        return true;
    });
}

[[nodiscard]] bool ApplyRegionPortalText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) {
    if (property == InspectorPropertyId::RegionPortalPurposes) {
        std::uint32_t purposes = 0U;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), purposes);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || !kb::scene::IsRegionPortalPurposeMaskValid(purposes)) return false;
        return EditRegionPortal(sceneContext, entity, "Edit Region Portal", [purposes](kb::scene::SceneRegionPortalComponent& value) { value.purposes = purposes; return true; });
    }

    if (property != InspectorPropertyId::RegionPortalSourceCell && property != InspectorPropertyId::RegionPortalTargetCell) return false;
    std::uint64_t cellId = 0U;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), cellId);
    const kb::scene::SceneEntity cell{ cellId };
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || cell == entity ||
        !sceneContext.Scene().Entities().IsAlive(cell) || !sceneContext.Scene().Components().VisibilityCells().Has(cell)) return false;
    const kb::scene::SceneRegionPortalComponent* portal = sceneContext.Scene().Components().RegionPortals().TryGet(entity);
    if (portal == nullptr) return false;
    return property == InspectorPropertyId::RegionPortalSourceCell
        ? sceneContext.SetRegionPortalCells(entity, cell, portal->targetCell)
        : sceneContext.SetRegionPortalCells(entity, portal->sourceCell, cell);
}

[[nodiscard]] bool ApplySecondaryFrameText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) {
    return EditSecondaryFrame(sceneContext, entity, "Edit Secondary Frame", [property, text](kb::scene::AuxFrameComponent& value) {
        kb::scene::AuxFrameComponent candidate = value;
        if (property == InspectorPropertyId::SecondaryFrameMode) {
            std::uint32_t mode = 0U;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), mode);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || mode > static_cast<std::uint32_t>(kb::scene::AuxFrameMode::Panoramic)) return false;
            candidate.mode = static_cast<kb::scene::AuxFrameMode>(mode);
        } else if (property == InspectorPropertyId::SecondaryFrameImageTargetId) {
            const auto result = std::from_chars(text.data(), text.data() + text.size(), candidate.imageTargetId);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
        } else if (property == InspectorPropertyId::SecondaryFrameWidth || property == InspectorPropertyId::SecondaryFrameHeight) {
            std::uint32_t dimension = 0U;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), dimension);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || dimension == 0U || dimension > UINT16_MAX) return false;
            if (property == InspectorPropertyId::SecondaryFrameWidth) candidate.width = static_cast<std::uint16_t>(dimension); else candidate.height = static_cast<std::uint16_t>(dimension);
        } else {
            float number = 0.0F;
            if (!ParseFloat(text, number) || !std::isfinite(number)) return false;
            switch (property) {
            case InspectorPropertyId::SecondaryFramePlaneNormalX: candidate.mirrorPlaneNormal.x = number; break;
            case InspectorPropertyId::SecondaryFramePlaneNormalY: candidate.mirrorPlaneNormal.y = number; break;
            case InspectorPropertyId::SecondaryFramePlaneNormalZ: candidate.mirrorPlaneNormal.z = number; break;
            case InspectorPropertyId::SecondaryFramePlaneOffset: candidate.mirrorPlaneOffset = number; break;
            default: return false;
            }
        }
        if (!kb::scene::IsAuxFrameComponentPersistable(candidate)) return false;
        value = candidate;
        return true;
    });
}

[[nodiscard]] bool ApplyGeometrySwarmText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) {
    return EditGeometrySwarm(sceneContext, entity, "Edit Geometry Swarm", [property, text](kb::scene::GeometrySwarmComponent& value) {
        kb::scene::GeometrySwarmComponent candidate = value;
        auto parseUnsigned = [text](auto& target) { const auto result = std::from_chars(text.data(), text.data() + text.size(), target); return result.ec == std::errc{} && result.ptr == text.data() + text.size(); };
        switch (property) {
        case InspectorPropertyId::GeometrySwarmMeshAssetId: if (!parseUnsigned(candidate.meshAssetId)) return false; break;
        case InspectorPropertyId::GeometrySwarmMaterialAssetId: if (!parseUnsigned(candidate.materialAssetId)) return false; break;
        case InspectorPropertyId::GeometrySwarmInstanceCount: if (!parseUnsigned(candidate.instanceCount)) return false; break;
        case InspectorPropertyId::GeometrySwarmColumns: if (!parseUnsigned(candidate.columns)) return false; break;
        case InspectorPropertyId::GeometrySwarmRows: if (!parseUnsigned(candidate.rows)) return false; break;
        case InspectorPropertyId::GeometrySwarmLayers: if (!parseUnsigned(candidate.layers)) return false; break;
        case InspectorPropertyId::GeometrySwarmLayer: if (!parseUnsigned(candidate.layer)) return false; break;
        case InspectorPropertyId::GeometrySwarmSpacingX: if (!ParseFloat(text, candidate.spacing.x)) return false; break;
        case InspectorPropertyId::GeometrySwarmSpacingY: if (!ParseFloat(text, candidate.spacing.y)) return false; break;
        case InspectorPropertyId::GeometrySwarmSpacingZ: if (!ParseFloat(text, candidate.spacing.z)) return false; break;
        case InspectorPropertyId::GeometrySwarmInstanceScale: if (!ParseFloat(text, candidate.instanceScale)) return false; break;
        default: return false;
        }
        if (!kb::scene::IsGeometrySwarmComponentPersistable(candidate)) return false;
        value = candidate;
        return true;
    });
}
[[nodiscard]] bool ApplySurfaceCastText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) {
    return EditSurfaceCast(sceneContext, entity, "Edit Surface Cast", [property, text](kb::scene::SurfaceCastComponent& value) {
        kb::scene::SurfaceCastComponent candidate = value;
        switch (property) {
        case InspectorPropertyId::SurfaceCastMaterialAssetId: { const auto result = std::from_chars(text.data(), text.data() + text.size(), candidate.materialAssetId); if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false; break; }
        case InspectorPropertyId::SurfaceCastReceiverLayerMask: { const auto result = std::from_chars(text.data(), text.data() + text.size(), candidate.receiverLayerMask); if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false; break; }
        case InspectorPropertyId::SurfaceCastOrder: { const auto result = std::from_chars(text.data(), text.data() + text.size(), candidate.order); if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false; break; }
        default: return false;
        }
        if (!kb::scene::IsSurfaceCastComponentPersistable(candidate)) return false;
        value = candidate;
        return true;
    });
}
[[nodiscard]] bool ApplyFacingPanelText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) {
    float parsed = 0.0F;
    if (!ParseFloat(text, parsed) || !std::isfinite(parsed)) return false;
    return EditFacingPanel(sceneContext, entity, "Edit Facing Panel", [property, parsed](kb::scene::FacingPanelComponent& value) {
        kb::scene::FacingPanelComponent candidate = value;
        switch (property) {
        case InspectorPropertyId::FacingPanelTargetX: candidate.targetPoint.x = parsed; break;
        case InspectorPropertyId::FacingPanelTargetY: candidate.targetPoint.y = parsed; break;
        case InspectorPropertyId::FacingPanelTargetZ: candidate.targetPoint.z = parsed; break;
        case InspectorPropertyId::FacingPanelAxisX: candidate.axis.x = parsed; break;
        case InspectorPropertyId::FacingPanelAxisY: candidate.axis.y = parsed; break;
        case InspectorPropertyId::FacingPanelAxisZ: candidate.axis.z = parsed; break;
        case InspectorPropertyId::FacingPanelUpX: candidate.up.x = parsed; break;
        case InspectorPropertyId::FacingPanelUpY: candidate.up.y = parsed; break;
        case InspectorPropertyId::FacingPanelUpZ: candidate.up.z = parsed; break;
        default: return false;
        }
        if (!kb::scene::IsFacingPanelComponentPersistable(candidate)) return false;
        value = candidate;
        return true;
    });
}
[[nodiscard]] bool ApplySpaceStrokeText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) {
    return EditSpaceStroke(sceneContext, entity, "Edit Kreska przestrzenna", [property, text](kb::scene::SpaceStrokeComponent& value) {
        kb::scene::SpaceStrokeComponent candidate = value;
        switch (property) {
        case InspectorPropertyId::SpaceStrokeMeshAssetId: { const auto result = std::from_chars(text.data(), text.data() + text.size(), candidate.meshAssetId); if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false; break; }
        case InspectorPropertyId::SpaceStrokeMaterialAssetId: { const auto result = std::from_chars(text.data(), text.data() + text.size(), candidate.materialAssetId); if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false; break; }
        case InspectorPropertyId::SpaceStrokeSplineSegments: { std::uint32_t segments = 0U; const auto result = std::from_chars(text.data(), text.data() + text.size(), segments); if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || segments > UINT8_MAX) return false; candidate.splineSegments = static_cast<std::uint8_t>(segments); break; }
        case InspectorPropertyId::SpaceStrokeLayer: { const auto result = std::from_chars(text.data(), text.data() + text.size(), candidate.layer); if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false; break; }
        case InspectorPropertyId::SpaceStrokeWidth: if (!ParseFloat(text, candidate.width)) return false; break;
        case InspectorPropertyId::SpaceStrokeCableSag: if (!ParseFloat(text, candidate.cableSag)) return false; break;
        default: return false;
        }
        if (!kb::scene::IsSpaceStrokeComponentPersistable(candidate)) return false;
        value = candidate;
        return true;
    });
}
[[nodiscard]] bool ApplyHistoryRibbonText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) {
    return EditHistoryRibbon(sceneContext, entity, "Edit Wst\xC4\x99" "ga historii", [property, text](kb::scene::HistoryRibbonComponent& value) {
        kb::scene::HistoryRibbonComponent candidate = value;
        switch (property) {
        case InspectorPropertyId::HistoryRibbonMeshAssetId: { const auto result = std::from_chars(text.data(), text.data() + text.size(), candidate.meshAssetId); if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false; break; }
        case InspectorPropertyId::HistoryRibbonMaterialAssetId: { const auto result = std::from_chars(text.data(), text.data() + text.size(), candidate.materialAssetId); if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false; break; }
        case InspectorPropertyId::HistoryRibbonLayer: { const auto result = std::from_chars(text.data(), text.data() + text.size(), candidate.layer); if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false; break; }
        case InspectorPropertyId::HistoryRibbonLifetimeSeconds: if (!ParseFloat(text, candidate.lifetimeSeconds)) return false; break;
        case InspectorPropertyId::HistoryRibbonWidth: if (!ParseFloat(text, candidate.width)) return false; break;
        case InspectorPropertyId::HistoryRibbonSampleIntervalSeconds: if (!ParseFloat(text, candidate.sampleIntervalSeconds)) return false; break;
        default: return false;
        }
        if (!kb::scene::IsHistoryRibbonComponentPersistable(candidate)) return false;
        value = candidate;
        return true;
    });
}

[[nodiscard]] bool ToggleAudioProperty(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property) {
    if (!sceneContext.BeginSceneEditTransaction("Edit Audio Component")) {
        return true;
    }

    const bool changed = InspectorAudioComponentModel::Toggle(sceneContext.Scene(), entity, property);

    if (changed) {
        static_cast<void>(sceneContext.CommitSceneEditTransaction());
    } else {
        sceneContext.CancelSceneEditTransaction();
    }
    return true;
}

template <typename Mutation>
[[nodiscard]] bool ApplyAudioMutation(EditorSceneContext& sceneContext, std::string_view label, Mutation mutation) {
    if (!sceneContext.BeginSceneEditTransaction(std::string{ label })) {
        return false;
    }
    if (!mutation()) {
        sceneContext.CancelSceneEditTransaction();
        return false;
    }
    return sceneContext.CommitSceneEditTransaction();
}

[[nodiscard]] bool ApplyAudioFloat(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, float value) {
    return ApplyAudioMutation(sceneContext, "Edit Audio Component", [&]() {
        return InspectorAudioComponentModel::ApplyFloat(sceneContext.Scene(), entity, property, value);
    });
}

class EditorAudioScrubTransactions {
public:
    explicit EditorAudioScrubTransactions(EditorSceneContext& sceneContext) noexcept
        : sceneContext_(sceneContext) {}

    [[nodiscard]] bool Begin(std::string label) {
        return sceneContext_.BeginSceneEditTransaction(std::move(label));
    }

    [[nodiscard]] bool Commit() {
        return sceneContext_.CommitSceneEditTransaction();
    }

    void Cancel() {
        sceneContext_.CancelSceneEditTransaction();
    }

private:
    EditorSceneContext& sceneContext_;
};

void CancelAudioScrub(EditorSceneContext& sceneContext) noexcept {
    InspectorPanelState& inspector = sceneContext.Inspector();
    EditorAudioScrubTransactions transactions{ sceneContext };
    InspectorAudioScrubController::Cancel(transactions, inspector.AudioScrub());
    inspector.EndFloatDrag();
}

[[nodiscard]] bool HandleAudioClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    sceneContext.Inspector().EndTextEdit();
    if (hit.kind == InspectorHitKind::BoolField) {
        return ToggleAudioProperty(sceneContext, entity, hit.property);
    }
    if (hit.kind != InspectorHitKind::TextField) {
        return true;
    }
    if (hit.property == InspectorPropertyId::AudioSourceClipClear) {
        static_cast<void>(sceneContext.SetAudioSourceClipAsset(entity, {}));
        return true;
    }
    if (hit.property == InspectorPropertyId::AudioSourceClip || hit.property == InspectorPropertyId::AudioSourceClipPicker) {
        if (const kb::scene::AudioSourceComponent* source = sceneContext.Scene().Components().AudioSources().TryGet(entity)) {
            SelectAssetInProjectFiles(sceneContext, kb::assets::AssetId{ source->clipAssetId });
        }
        return true;
    }
    if (hit.property == InspectorPropertyId::AudioSourceAttenuation) {
        static_cast<void>(ApplyAudioMutation(sceneContext, "Edit Audio Attenuation", [&]() {
            return InspectorAudioComponentModel::CycleAttenuation(sceneContext.Scene(), entity);
        }));
        return true;
    }
    if (hit.property == InspectorPropertyId::AudioSourceOutputBus) {
        static_cast<void>(ApplyAudioMutation(sceneContext, "Edit Audio Output", [&]() {
            return InspectorAudioComponentModel::CycleOutputBus(sceneContext.Scene(), entity);
        }));
        return true;
    }
    return true;
}

// --- Physics component editing (index-addressed, via InspectorPhysicsModel) ----

[[nodiscard]] std::optional<PhysicsComponentKind> PhysicsKindForSection(InspectorSectionId section) noexcept {
    switch (section) {
    case InspectorSectionId::Rigidbody: return PhysicsComponentKind::Rigidbody;
    case InspectorSectionId::Collider: return PhysicsComponentKind::Collider;
    case InspectorSectionId::CharacterController: return PhysicsComponentKind::CharacterController;
    case InspectorSectionId::Joint: return PhysicsComponentKind::Joint;
    default: return std::nullopt;
    }
}

[[nodiscard]] std::optional<PhysicsComponentKind> PhysicsKindForProperty(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::RigidbodyField: return PhysicsComponentKind::Rigidbody;
    case InspectorPropertyId::ColliderField: return PhysicsComponentKind::Collider;
    case InspectorPropertyId::CharacterControllerField: return PhysicsComponentKind::CharacterController;
    case InspectorPropertyId::JointField: return PhysicsComponentKind::Joint;
    default: return std::nullopt;
    }
}

[[nodiscard]] InspectorPropertyId PhysicsFieldProperty(PhysicsComponentKind kind) noexcept {
    switch (kind) {
    case PhysicsComponentKind::Rigidbody: return InspectorPropertyId::RigidbodyField;
    case PhysicsComponentKind::Collider: return InspectorPropertyId::ColliderField;
    case PhysicsComponentKind::CharacterController: return InspectorPropertyId::CharacterControllerField;
    case PhysicsComponentKind::Joint: return InspectorPropertyId::JointField;
    }
    return InspectorPropertyId::RigidbodyField;
}

// Applies op(component) inside an undoable transaction, marking the component
// modified and committing only when op reports a change.
template <typename Store, typename Op>
[[nodiscard]] bool EditPhysicsComponent(EditorSceneContext& sceneContext, Store store, kb::scene::SceneEntity entity, const char* label, Op op) {
    if (!sceneContext.BeginSceneEditTransaction(label)) {
        return false;
    }
    auto* component = store.TryGet(entity);
    if (component == nullptr) {
        sceneContext.CancelSceneEditTransaction();
        return false;
    }
    const bool changed = op(*component);
    if (changed) {
        store.MarkModified(entity);
        static_cast<void>(sceneContext.CommitSceneEditTransaction());
    } else {
        sceneContext.CancelSceneEditTransaction();
    }
    return changed;
}

[[nodiscard]] bool CyclePhysicsEnum(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, PhysicsComponentKind kind, int index) {
    switch (kind) {
    case PhysicsComponentKind::Rigidbody:
        return EditPhysicsComponent(sceneContext, sceneContext.Scene().Components().Rigidbodies(), entity, "Edit Rigidbody",
            [index](kb::scene::RigidbodyComponent& c) { return InspectorPhysicsModel::CycleEnum(c, index); });
    case PhysicsComponentKind::Collider:
        return EditPhysicsComponent(sceneContext, sceneContext.Scene().Components().Colliders(), entity, "Edit Collider",
            [index](kb::scene::ColliderComponent& c) { return InspectorPhysicsModel::CycleEnum(c, index); });
    case PhysicsComponentKind::CharacterController:
        return EditPhysicsComponent(sceneContext, sceneContext.Scene().Components().CharacterControllers(), entity, "Edit Character Controller",
            [index](kb::scene::CharacterControllerComponent& c) { return InspectorPhysicsModel::CycleEnum(c, index); });
    case PhysicsComponentKind::Joint:
        return EditPhysicsComponent(sceneContext, sceneContext.Scene().Components().Joints(), entity, "Edit Joint",
            [index](kb::scene::JointComponent& c) { return InspectorPhysicsModel::CycleEnum(c, index); });
    }
    return false;
}

[[nodiscard]] bool TogglePhysicsBool(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, PhysicsComponentKind kind, int index) {
    switch (kind) {
    case PhysicsComponentKind::Rigidbody:
        return EditPhysicsComponent(sceneContext, sceneContext.Scene().Components().Rigidbodies(), entity, "Edit Rigidbody",
            [index](kb::scene::RigidbodyComponent& c) { return InspectorPhysicsModel::ToggleBool(c, index); });
    case PhysicsComponentKind::Collider:
        return EditPhysicsComponent(sceneContext, sceneContext.Scene().Components().Colliders(), entity, "Edit Collider",
            [index](kb::scene::ColliderComponent& c) { return InspectorPhysicsModel::ToggleBool(c, index); });
    case PhysicsComponentKind::CharacterController:
        return EditPhysicsComponent(sceneContext, sceneContext.Scene().Components().CharacterControllers(), entity, "Edit Character Controller",
            [index](kb::scene::CharacterControllerComponent& c) { return InspectorPhysicsModel::ToggleBool(c, index); });
    case PhysicsComponentKind::Joint:
        return EditPhysicsComponent(sceneContext, sceneContext.Scene().Components().Joints(), entity, "Edit Joint",
            [index](kb::scene::JointComponent& c) { return InspectorPhysicsModel::ToggleBool(c, index); });
    }
    return false;
}

[[nodiscard]] bool ReadPhysicsFloat(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, PhysicsComponentKind kind, int index, float& out) {
    switch (kind) {
    case PhysicsComponentKind::Rigidbody:
        if (const auto* c = sceneContext.Scene().Components().Rigidbodies().TryGet(entity)) { return InspectorPhysicsModel::ReadFloat(*c, index, out); }
        return false;
    case PhysicsComponentKind::Collider:
        if (const auto* c = sceneContext.Scene().Components().Colliders().TryGet(entity)) { return InspectorPhysicsModel::ReadFloat(*c, index, out); }
        return false;
    case PhysicsComponentKind::CharacterController:
        if (const auto* c = sceneContext.Scene().Components().CharacterControllers().TryGet(entity)) { return InspectorPhysicsModel::ReadFloat(*c, index, out); }
        return false;
    case PhysicsComponentKind::Joint:
        if (const auto* c = sceneContext.Scene().Components().Joints().TryGet(entity)) { return InspectorPhysicsModel::ReadFloat(*c, index, out); }
        return false;
    }
    return false;
}

[[nodiscard]] bool ApplyPhysicsFloat(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, PhysicsComponentKind kind, int index, float value) {
    switch (kind) {
    case PhysicsComponentKind::Rigidbody:
        return EditPhysicsComponent(sceneContext, sceneContext.Scene().Components().Rigidbodies(), entity, "Edit Rigidbody",
            [index, value](kb::scene::RigidbodyComponent& c) { return InspectorPhysicsModel::ApplyFloat(c, index, value); });
    case PhysicsComponentKind::Collider:
        return EditPhysicsComponent(sceneContext, sceneContext.Scene().Components().Colliders(), entity, "Edit Collider",
            [index, value](kb::scene::ColliderComponent& c) { return InspectorPhysicsModel::ApplyFloat(c, index, value); });
    case PhysicsComponentKind::CharacterController:
        return EditPhysicsComponent(sceneContext, sceneContext.Scene().Components().CharacterControllers(), entity, "Edit Character Controller",
            [index, value](kb::scene::CharacterControllerComponent& c) { return InspectorPhysicsModel::ApplyFloat(c, index, value); });
    case PhysicsComponentKind::Joint:
        return EditPhysicsComponent(sceneContext, sceneContext.Scene().Components().Joints(), entity, "Edit Joint",
            [index, value](kb::scene::JointComponent& c) { return InspectorPhysicsModel::ApplyFloat(c, index, value); });
    }
    return false;
}

[[nodiscard]] bool HandlePhysicsClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, PhysicsComponentKind kind, const InspectorPanelRenderer::Hit& hit) {
    if (hit.property == InspectorPropertyId::ComponentRemove) {
        sceneContext.Inspector().EndTextEdit();
        static_cast<void>(sceneContext.RemovePhysicsComponent(entity, kind));
        sceneContext.Inspector().CloseComponentMenus();
        return true;
    }
    if (hit.property == InspectorPropertyId::ColliderFitToMesh) {
        sceneContext.Inspector().EndTextEdit();
        static_cast<void>(sceneContext.FitColliderToMesh(entity));
        return true;
    }
    if (PhysicsKindForProperty(hit.property) != kind || hit.index < 0) {
        sceneContext.Inspector().EndTextEdit();
        return true;
    }
    const int index = hit.index;
    switch (InspectorPhysicsModel::KindOf(kind, index)) {
    case PhysicsFieldKind::Bool:
        sceneContext.Inspector().EndTextEdit();
        static_cast<void>(TogglePhysicsBool(sceneContext, entity, kind, index));
        return true;
    case PhysicsFieldKind::Enum:
        sceneContext.Inspector().EndTextEdit();
        static_cast<void>(CyclePhysicsEnum(sceneContext, entity, kind, index));
        return true;
    case PhysicsFieldKind::ReadOnly:
        sceneContext.Inspector().EndTextEdit();
        return true;
    case PhysicsFieldKind::Float: {
        float value = 0.0F;
        if (ReadPhysicsFloat(sceneContext, entity, kind, index, value)) {
            // BeginTextEdit resets the edit index — set it afterwards so the commit
            // path knows which field row is being edited.
            sceneContext.Inspector().BeginTextEdit(PhysicsFieldProperty(kind), FormatCompactFloat(value));
            sceneContext.Inspector().SetEditIndex(index);
        }
        return true;
    }
    }
    return true;
}

// --- Drag-to-scrub for entity numeric fields (physics + light floats) ---------
// Transform and material fields already scrub; these give every other numeric
// entity field the same "hold LMB + move left/right" behaviour.

[[nodiscard]] bool IsGenericEntityFloatProperty(InspectorPropertyId property) noexcept {
    return PhysicsKindForProperty(property).has_value() ||
        IsLightFloatProperty(property) ||
        IsCameraFloatProperty(property) ||
        InspectorAudioComponentModel::IsFloatProperty(property) ||
        InspectorAudioComponentModel::IsIntegerProperty(property) ||
        property == InspectorPropertyId::AnimatorSpeed;
}

[[nodiscard]] bool ReadEntityFloatField(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, int index, float& out) {
    if (const std::optional<PhysicsComponentKind> kind = PhysicsKindForProperty(property); kind.has_value()) {
        return ReadPhysicsFloat(sceneContext, entity, *kind, index, out);
    }
    if (IsLightFloatProperty(property)) {
        if (const kb::scene::LightComponent* light = sceneContext.Scene().Components().Lights().TryGet(entity)) {
            return ReadLightFloat(*light, property, out);
        }
    }
    if (IsCameraFloatProperty(property)) {
        if (const kb::scene::CameraComponent* camera =
                sceneContext.Scene().Components().Cameras().TryGet(entity)) {
            return ReadCameraFloat(*camera, property, out);
        }
    }
    if (InspectorAudioComponentModel::IsFloatProperty(property)) {
        return InspectorAudioComponentModel::ReadFloat(sceneContext.Scene(), entity, property, out);
    }
    if (InspectorAudioComponentModel::IsIntegerProperty(property)) {
        std::int64_t value = 0;
        if (InspectorAudioComponentModel::ReadInteger(sceneContext.Scene(), entity, property, value)) {
            out = static_cast<float>(value);
            return true;
        }
    }
    if (property == InspectorPropertyId::AnimatorSpeed) {
        if (const kb::scene::Animator* animator =
                sceneContext.Scene().Components().Animators().TryGet(entity)) {
            out = animator->speed;
            return true;
        }
    }
    return false;
}

// Writes non-audio fields through their existing edit paths. Audio fields use the
// gesture controller below so all pointer moves share one scene transaction.
void ApplyEntityFloatField(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property, int index, float value) {
    if (const std::optional<PhysicsComponentKind> kind = PhysicsKindForProperty(property); kind.has_value()) {
        static_cast<void>(ApplyPhysicsFloat(sceneContext, entity, *kind, index, value));
        return;
    }
    if (IsLightFloatProperty(property)) {
        static_cast<void>(MutateLightComponent(sceneContext, entity, "Edit Light", [property, value](kb::scene::LightComponent& light) {
            static_cast<void>(WriteLightFloat(light, property, value));
        }));
        return;
    }
    if (IsCameraFloatProperty(property)) {
        static_cast<void>(ApplyCameraFloat(
            sceneContext, entity, property, value));
        return;
    }
    if (property == InspectorPropertyId::AnimatorSpeed) {
        static_cast<void>(sceneContext.SetAnimatorSpeed(entity, value));
    }
}

// Begins a scrub drag on a numeric entity field. Only Float fields scrub — Enum/
// Bool physics rows keep their click behaviour. Returns true when the press was
// consumed as a drag start.
[[nodiscard]] bool BeginEntityFloatDrag(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit, int x, int y) {
    const std::optional<PhysicsComponentKind> physicsKind = PhysicsKindForProperty(hit.property);
    const bool isPhysicsFloat = physicsKind.has_value() && hit.index >= 0 && InspectorPhysicsModel::KindOf(*physicsKind, hit.index) == PhysicsFieldKind::Float;
    const bool isComponentFloat =
        IsLightFloatProperty(hit.property) ||
        IsCameraFloatProperty(hit.property) ||
        InspectorAudioComponentModel::IsFloatProperty(hit.property) ||
        InspectorAudioComponentModel::IsIntegerProperty(hit.property) ||
        hit.property == InspectorPropertyId::AnimatorSpeed;
    if (!isPhysicsFloat && !isComponentFloat) {
        return false;
    }
    if (InspectorAudioComponentModel::IsFloatProperty(hit.property)
        || InspectorAudioComponentModel::IsIntegerProperty(hit.property)) {
        InspectorPanelState& inspector = sceneContext.Inspector();
        EditorAudioScrubTransactions transactions{ sceneContext };
        if (!InspectorAudioScrubController::Begin(sceneContext.Scene(), entity, hit.property, transactions, inspector.AudioScrub())) {
            return false;
        }
        const InspectorAudioScrubState& scrub = inspector.AudioScrub();
        if (scrub.integer) {
            inspector.BeginIntegerDrag(hit.property, x, y);
        } else {
            inspector.BeginFloatDrag(hit.property, scrub.startFloat, x, y);
        }
        inspector.SetEditIndex(hit.index);
        return true;
    }
    float value = 0.0F;
    if (!ReadEntityFloatField(sceneContext, entity, hit.property, hit.index, value)) {
        return false;
    }
    sceneContext.Inspector().BeginFloatDrag(hit.property, value, x, y);
    sceneContext.Inspector().SetEditIndex(hit.index); // BeginFloatDrag ends text edit (resets index) first
    return true;
}

[[nodiscard]] std::optional<InspectorDisclosureId> DisclosureForProperty(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::MeshRendererAdvanced:
        return InspectorDisclosureId::MeshRendererAdvanced;
    case InspectorPropertyId::TerrainAdvanced:
        return InspectorDisclosureId::TerrainAdvanced;
    default:
        return std::nullopt;
    }
}

} // namespace

bool InspectorPanelInteraction::HandlePointerDown(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit, int x, int y) noexcept {
    if (hit.kind == InspectorHitKind::None) {
        sceneContext.Inspector().EndTextEdit();
        sceneContext.Inspector().CloseAddComponentBrowser();
        sceneContext.Inspector().CloseComponentMenus();
        sceneContext.Inspector().CloseTagsDropdown();
        terrain_material_layer_menu::Close();
        return false;
    }

    const bool tagsMenuHit = hit.kind == InspectorHitKind::TagOption ||
        (hit.section == InspectorSectionId::General &&
         hit.kind == InspectorHitKind::TextField &&
         hit.property == InspectorPropertyId::TagsText);
    if (!tagsMenuHit) {
        sceneContext.Inspector().CloseTagsDropdown();
    }

    const bool terrainLayerMenuHit = hit.section == InspectorSectionId::Terrain &&
        (hit.property == terrain_material_layer_menu::kProperty ||
         hit.property == InspectorPropertyId::TerrainMaterialLayerCreate ||
         hit.property == InspectorPropertyId::TerrainMaterialLayerAdd ||
         hit.property == InspectorPropertyId::TerrainMaterialLayerRemove);
    if (!terrainLayerMenuHit) {
        terrain_material_layer_menu::Close();
    }

    kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
    if (const std::optional<InspectorDisclosureId> disclosure = DisclosureForProperty(hit.property);
        hit.kind == InspectorHitKind::Row && disclosure.has_value()) {
        sceneContext.Inspector().EndTextEdit();
        sceneContext.Inspector().CloseAddComponentBrowser();
        sceneContext.Inspector().CloseComponentMenus();
        sceneContext.Inspector().CloseTagsDropdown();
        sceneContext.Inspector().ToggleDisclosure(*disclosure);
        return true;
    }
    if (hit.kind == InspectorHitKind::SectionHeader) {
        sceneContext.Inspector().EndTextEdit();
        sceneContext.Inspector().CloseAddComponentBrowser();
        sceneContext.Inspector().CloseComponentMenus();
        sceneContext.Inspector().CloseTagsDropdown();
        sceneContext.Inspector().ToggleCollapsed(hit.section);
        return true;
    }
    if (hit.kind == InspectorHitKind::ComponentMenuButton) {
        sceneContext.Inspector().EndTextEdit();
        if (hit.property == InspectorPropertyId::ComponentRemove && sceneContext.Scene().Entities().IsAlive(entity)) {
            if (hit.section == InspectorSectionId::Script) {
                static_cast<void>(sceneContext.RemoveScriptFromEntity(entity));
            } else if (hit.section == InspectorSectionId::Terrain) {
                EditorTerrainToolState& tool = EditorTerrainService::ToolState();
                tool.editingEnabled = false;
                tool.mode = EditorTerrainToolMode::Select;
                tool.hoverVisible = false;
                tool.brushMenuOpen = false;
                tool.brushShapeMenuOpen = false;
                static_cast<void>(sceneContext.RemoveMeshRendererFromEntity(entity));
            } else if (hit.section == InspectorSectionId::MeshRenderer) {
                static_cast<void>(sceneContext.RemoveMeshRendererFromEntity(entity));
            } else if (hit.section == InspectorSectionId::Camera) {
                static_cast<void>(RemoveCameraComponent(sceneContext, entity));
            } else if (InspectorAudioComponentModel::HasRemoveControl(hit.section)) {
                const std::string_view label = hit.section == InspectorSectionId::AudioSource
                    ? "Remove Audio Source"
                    : "Remove Audio Listener";
                static_cast<void>(ApplyAudioMutation(sceneContext, label, [&]() {
                    return InspectorAudioComponentModel::RemoveComponent(sceneContext.Scene(), entity, hit.section);
                }));
            } else if (hit.section == InspectorSectionId::Animator) {
                static_cast<void>(sceneContext.RemoveAnimatorFromEntity(entity));
            } else if (hit.section == InspectorSectionId::SkeletonBinding) {
                static_cast<void>(sceneContext.RemoveSkeletonBindingFromEntity(entity));
            } else if (hit.section == InspectorSectionId::DeformedGeometry) {
                static_cast<void>(sceneContext.RemoveDeformedGeometryFromEntity(entity));
            } else if (hit.section == InspectorSectionId::UIDocument) {
                static_cast<void>(sceneContext.RemoveUIDocumentFromEntity(entity));
            } else if (hit.section == InspectorSectionId::Tags) {
                static_cast<void>(sceneContext.RemoveTagsFromEntity(entity));
            } else if (hit.section == InspectorSectionId::NavAgent && sceneContext.Scene().Components().NavAgents().Has(entity)) {
                if (sceneContext.BeginSceneEditTransaction("Remove Nav Agent")) {
                    sceneContext.Scene().Components().NavAgents().Remove(entity);
                    static_cast<void>(sceneContext.CommitSceneEditTransaction());
                }
            } else if (hit.section == InspectorSectionId::NavObstacle && sceneContext.Scene().Components().NavObstacles().Has(entity)) {
                if (sceneContext.BeginSceneEditTransaction("Remove Nav Obstacle")) {
                    sceneContext.Scene().Components().NavObstacles().Remove(entity);
                    static_cast<void>(sceneContext.CommitSceneEditTransaction());
                }
            } else if (hit.section == InspectorSectionId::WorldBackdrop && sceneContext.Scene().Components().WorldBackdrops().Has(entity)) {
                if (sceneContext.BeginSceneEditTransaction("Remove World Backdrop")) {
                    sceneContext.Scene().Components().WorldBackdrops().Remove(entity);
                    static_cast<void>(sceneContext.CommitSceneEditTransaction());
                }
            } else if (hit.section == InspectorSectionId::AmbientRadiance && sceneContext.Scene().Components().AmbientRadiances().Has(entity)) {
                if (sceneContext.BeginSceneEditTransaction("Remove Ambient Radiance")) {
                    sceneContext.Scene().Components().AmbientRadiances().Remove(entity);
                    static_cast<void>(sceneContext.CommitSceneEditTransaction());
                }
            } else if (hit.section == InspectorSectionId::DetailSwitch && sceneContext.Scene().Components().DetailSwitches().Has(entity)) {
                if (sceneContext.BeginSceneEditTransaction("Remove Detail Switch")) {
                    sceneContext.Scene().Components().DetailSwitches().Remove(entity);
                    static_cast<void>(sceneContext.CommitSceneEditTransaction());
                }
            } else if (hit.section == InspectorSectionId::VisibilityBlocker && sceneContext.Scene().Components().VisibilityBlockers().Has(entity)) {
                if (sceneContext.BeginSceneEditTransaction("Remove Visibility Blocker")) {
                    sceneContext.Scene().Components().VisibilityBlockers().Remove(entity);
                    static_cast<void>(sceneContext.CommitSceneEditTransaction());
                }
            } else if (hit.section == InspectorSectionId::VisibilityCell && sceneContext.Scene().Components().VisibilityCells().Has(entity)) {
                if (sceneContext.BeginSceneEditTransaction("Remove Visibility Cell")) {
                    sceneContext.Scene().Components().VisibilityCells().Remove(entity);
                    static_cast<void>(sceneContext.CommitSceneEditTransaction());
                }
            } else if (hit.section == InspectorSectionId::RegionPortal && sceneContext.Scene().Components().RegionPortals().Has(entity)) {
                if (sceneContext.BeginSceneEditTransaction("Remove Region Portal")) {
                    sceneContext.Scene().Components().RegionPortals().Remove(entity);
                    static_cast<void>(sceneContext.CommitSceneEditTransaction());
                }
            } else if (hit.section == InspectorSectionId::SecondaryFrame && sceneContext.Scene().Components().AuxFrames().Has(entity)) {
                if (sceneContext.BeginSceneEditTransaction("Remove Secondary Frame")) {
                    sceneContext.Scene().Components().AuxFrames().Remove(entity);
                    static_cast<void>(sceneContext.CommitSceneEditTransaction());
                }
            } else if (hit.section == InspectorSectionId::GeometrySwarm && sceneContext.Scene().Components().GeometrySwarms().Has(entity)) {
                if (sceneContext.BeginSceneEditTransaction("Remove Geometry Swarm")) {
                    sceneContext.Scene().Components().GeometrySwarms().Remove(entity);
                    static_cast<void>(sceneContext.CommitSceneEditTransaction());
                }
            } else if (hit.section == InspectorSectionId::SurfaceCast && sceneContext.Scene().Components().SurfaceCasts().Has(entity)) {
                if (sceneContext.BeginSceneEditTransaction("Remove Surface Cast")) {
                    sceneContext.Scene().Components().SurfaceCasts().Remove(entity);
                    static_cast<void>(sceneContext.CommitSceneEditTransaction());
                }
            } else if (hit.section == InspectorSectionId::FacingPanel && sceneContext.Scene().Components().FacingPanels().Has(entity)) {
                if (sceneContext.BeginSceneEditTransaction("Remove Facing Panel")) {
                    sceneContext.Scene().Components().FacingPanels().Remove(entity);
                    static_cast<void>(sceneContext.CommitSceneEditTransaction());
                }
            } else if (hit.section == InspectorSectionId::SpaceStroke && sceneContext.Scene().Components().SpaceStrokes().Has(entity)) {
                if (sceneContext.BeginSceneEditTransaction("Remove Kreska przestrzenna")) {
                    sceneContext.Scene().Components().SpaceStrokes().Remove(entity);
                    static_cast<void>(sceneContext.CommitSceneEditTransaction());
                }
            } else if (hit.section == InspectorSectionId::HistoryRibbon && sceneContext.Scene().Components().HistoryRibbons().Has(entity)) {
                if (sceneContext.BeginSceneEditTransaction("Remove Wst\xC4\x99" "ga historii")) {
                    sceneContext.Scene().Components().HistoryRibbons().Remove(entity);
                    static_cast<void>(sceneContext.CommitSceneEditTransaction());
                }
            } else if (const std::optional<PhysicsComponentKind> kind = PhysicsKindForSection(hit.section); kind.has_value()) {
                static_cast<void>(sceneContext.RemovePhysicsComponent(entity, *kind));
            }
        }
        return true;
    }
    if (hit.kind == InspectorHitKind::MeshPreviewToolbarButton) {
        sceneContext.Inspector().EndTextEdit();
        if (hit.property == InspectorPropertyId::MeshPreviewReset) {
            sceneContext.Inspector().ResetMeshPreview();
        } else if (hit.property == InspectorPropertyId::MeshPreviewFit) {
            sceneContext.Inspector().FitMeshPreview();
        } else if (hit.property == InspectorPropertyId::MeshPreviewRenderMode) {
            sceneContext.Inspector().CycleMeshPreviewRenderMode();
        } else if (hit.property == InspectorPropertyId::MeshPreviewLightPreset) {
            sceneContext.Inspector().CycleMeshPreviewLightPreset();
        }
        return true;
    }
    if (hit.kind == InspectorHitKind::MeshPreview) {
        sceneContext.Inspector().BeginMeshPreviewDrag(x, y);
        return true;
    }
    const bool assetSelected = sceneContext.AssetBrowser().SelectionKind() == EditorAssetBrowserSelectionKind::Asset;
    if (assetSelected && hit.section == InspectorSectionId::InputAction) {
        return InspectorInputInteraction::HandleActionAssetClick(sceneContext, hit);
    }
    if (assetSelected && hit.section == InspectorSectionId::InputMappings) {
        return InspectorInputInteraction::HandleMappingClick(sceneContext, hit);
    }
    if (assetSelected && hit.section == InspectorSectionId::Material) {
        return HandleMaterialClick(sceneContext, hit, x, y);
    }
    if (!sceneContext.Scene().Entities().IsAlive(entity)) {
        return true;
    }

    if (hit.kind == InspectorHitKind::TagOption) {
        sceneContext.Inspector().EndTextEdit();
        const std::vector<std::string> known = sceneContext.KnownSceneTags();
        if (hit.index >= 0 && hit.index < static_cast<int>(known.size())) {
            const std::string& tag = known[static_cast<std::size_t>(hit.index)];
            if (hit.property == InspectorPropertyId::TagsRemove) {
                static_cast<void>(sceneContext.DeleteSceneTag(tag));
                sceneContext.Inspector().SetTagsDropdownHover(-1);
            } else {
                static_cast<void>(sceneContext.SetEntityTagSelected(entity, tag, true));
                sceneContext.Inspector().CloseTagsDropdown();
            }
        } else if (hit.index == static_cast<int>(known.size())) {
            static_cast<void>(sceneContext.RemoveTagsFromEntity(entity));
            sceneContext.Inspector().CloseTagsDropdown();
        } else if (hit.index == static_cast<int>(known.size()) + 1) {
            sceneContext.Inspector().CloseTagsDropdown();
            if (const std::optional<std::string> tag = EditorTagNameDialog::Show(GetActiveWindow()); tag.has_value()) {
                static_cast<void>(sceneContext.SetEntityTagSelected(entity, *tag, true));
            }
        }
        return true;
    }

    // Drag-to-scrub: pressing a numeric entity field (physics/light float) starts a
    // horizontal scrub; a click that does not move opens the inline editor on
    // pointer-up (handled below in HandlePointerUp). Transform/material fields have
    // their own scrub paths and are not caught here.
    if ((hit.kind == InspectorHitKind::TextField || hit.kind == InspectorHitKind::FloatField) &&
        IsGenericEntityFloatProperty(hit.property) &&
        BeginEntityFloatDrag(sceneContext, entity, hit, x, y)) {
        return true;
    }

    if (hit.section == InspectorSectionId::AudioSource || hit.section == InspectorSectionId::AudioListener) {
        return HandleAudioClick(sceneContext, entity, hit);
    }

    if (hit.section == InspectorSectionId::Script) {
        return HandleScriptClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::Terrain) {
        return HandleTerrainClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::MeshRenderer) {
        return HandleMeshRendererClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::Camera) {
        return HandleCameraClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::Animator) {
        return HandleAnimatorClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::SkeletonBinding) {
        return HandleSkeletonBindingClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::DeformedGeometry) {
        return HandleDeformedGeometryClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::UIDocument) {
        return HandleUIDocumentClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::NavAgent) {
        return HandleNavAgentClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::NavObstacle) {
        return HandleNavObstacleClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::RegionShape) {
        return HandleRegionShapeClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::ContentInstance) {
        return HandleContentInstanceClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::StreamFocus) {
        return HandleStreamFocusClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::WorldBackdrop) {
        return HandleWorldBackdropClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::AmbientRadiance) {
        return HandleAmbientRadianceClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::DetailSwitch) {
        return HandleDetailSwitchClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::VisibilityBlocker) {
        return HandleVisibilityBlockerClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::VisibilityCell) return HandleVisibilityCellClick(sceneContext, entity, hit);
    if (hit.section == InspectorSectionId::RegionPortal) return HandleRegionPortalClick(sceneContext, entity, hit);
    if (hit.section == InspectorSectionId::SecondaryFrame) return HandleSecondaryFrameClick(sceneContext, entity, hit);
    if (hit.section == InspectorSectionId::GeometrySwarm) return HandleGeometrySwarmClick(sceneContext, entity, hit);
    if (hit.section == InspectorSectionId::SurfaceCast) return HandleSurfaceCastClick(sceneContext, entity, hit);
    if (hit.section == InspectorSectionId::FacingPanel) return HandleFacingPanelClick(sceneContext, entity, hit);
    if (hit.section == InspectorSectionId::SpaceStroke) return HandleSpaceStrokeClick(sceneContext, entity, hit);
    if (hit.section == InspectorSectionId::HistoryRibbon) return HandleHistoryRibbonClick(sceneContext, entity, hit);
    if (hit.section == InspectorSectionId::General &&
        hit.kind == InspectorHitKind::TextField &&
        hit.property == InspectorPropertyId::TagsText) {
        sceneContext.Inspector().EndTextEdit();
        sceneContext.Inspector().ToggleTagsDropdown();
        return true;
    }
    if (hit.section == InspectorSectionId::Light) {
        return HandleLightClick(sceneContext, entity, hit);
    }
    if (const std::optional<PhysicsComponentKind> physicsKind = PhysicsKindForSection(hit.section); physicsKind.has_value()) {
        return HandlePhysicsClick(sceneContext, entity, *physicsKind, hit);
    }
    if (hit.section == InspectorSectionId::AddComponent) {
        return HandleAddComponentClick(sceneContext, entity, hit);
    }

    if (hit.kind == InspectorHitKind::TextField && hit.property == InspectorPropertyId::EntityName) {
        sceneContext.Inspector().BeginTextEdit(InspectorPropertyId::EntityName, sceneContext.Scene().Entities().Name(entity));
        return true;
    }
    if (hit.kind == InspectorHitKind::BoolField) {
        sceneContext.Inspector().EndTextEdit();
        if (hit.property == InspectorPropertyId::EntityVisible) {
            static_cast<void>(sceneContext.ToggleEntityVisibility(entity));
        } else if (hit.section == InspectorSectionId::AudioSource || hit.section == InspectorSectionId::AudioListener) {
            return ToggleAudioProperty(sceneContext, entity, hit.property);
        }
        return true;
    }
    if (hit.kind == InspectorHitKind::FloatField) {
        if (!sceneContext.BeginSelectedTransformEdit("Edit Transform")) {
            return true;
        }
        sceneContext.Inspector().BeginFloatDrag(hit.property, sceneContext.ActiveTransformEditPropertyStart(hit.property), x, y);
        return true;
    }
    return true;
}

bool InspectorPanelInteraction::HandlePointerDrag(EditorSceneContext& sceneContext, int x, int y) noexcept {
    if (sceneContext.Inspector().IsDraggingMeshPreview()) {
        sceneContext.Inspector().DragMeshPreview(x, y);
        return true;
    }
    if (!sceneContext.Inspector().IsDraggingFloat()) {
        return false;
    }
    const InspectorPropertyId property = sceneContext.Inspector().DraggedProperty();
    if (sceneContext.AssetBrowser().SelectionKind() == EditorAssetBrowserSelectionKind::Asset && IsMaterialFloatProperty(property)) {
        if (!sceneContext.HasActiveMaterialAssetEdit()) {
            sceneContext.Inspector().EndFloatDrag();
            return true;
        }
        const int dx = x - sceneContext.Inspector().DragStartX();
        const int dy = y - sceneContext.Inspector().DragStartY();
        if (std::abs(dx) + std::abs(dy) < 2) {
            return true;
        }
        sceneContext.Inspector().MarkFloatDragMoved();
        const float delta = static_cast<float>(dx - dy) * StepFor(property) * 0.08F;
        static_cast<void>(sceneContext.ApplyActiveMaterialAssetFloatEdit(sceneContext.Inspector().DragStartValue() + delta));
        return true;
    }
    InspectorPanelState& inspector = sceneContext.Inspector();
    const kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
    if (inspector.AudioScrub().active) {
        if (!sceneContext.Scene().Entities().IsAlive(entity) || entity != inspector.AudioScrub().entity) {
            CancelAudioScrub(sceneContext);
            return true;
        }
        const int dx = x - inspector.DragStartX();
        const int dy = y - inspector.DragStartY();
        if (std::abs(dx) + std::abs(dy) < 2) {
            return true;
        }
        inspector.MarkFloatDragMoved();
        const std::int64_t pixelDelta = (static_cast<std::int64_t>(x) - inspector.DragStartX())
            - (static_cast<std::int64_t>(y) - inspector.DragStartY());
        const InspectorAudioScrubUpdate update = InspectorAudioScrubController::Update(
            sceneContext.Scene(), entity, pixelDelta, inspector.AudioScrub());
        if (update == InspectorAudioScrubUpdate::LostTarget) {
            CancelAudioScrub(sceneContext);
        }
        return true;
    }
    if (!sceneContext.Scene().Entities().IsAlive(entity)) {
        if (!IsGenericEntityFloatProperty(property)) {
            sceneContext.CancelActiveTransformEdit();
        }
        sceneContext.Inspector().EndFloatDrag();
        return true;
    }
    const int dx = x - sceneContext.Inspector().DragStartX();
    const int dy = y - sceneContext.Inspector().DragStartY();
    if (std::abs(dx) + std::abs(dy) < 2) {
        return true;
    }
    sceneContext.Inspector().MarkFloatDragMoved();
    // Entity numeric fields scrub in stable increments without entering a Transform edit.
    if (IsGenericEntityFloatProperty(property)) {
        const float delta = std::round(static_cast<float>(dx - dy) / 6.0F) * 0.1F;
        ApplyEntityFloatField(sceneContext, entity, property, sceneContext.Inspector().EditIndex(), sceneContext.Inspector().DragStartValue() + delta);
        return true;
    }
    const float delta = static_cast<float>(dx - dy) * StepFor(property) * 0.08F;
    static_cast<void>(sceneContext.ApplyActiveTransformEditProperty(property, sceneContext.Inspector().DragStartValue() + delta));
    return true;
}

bool InspectorPanelInteraction::HandlePointerUp(EditorSceneContext& sceneContext) noexcept {
    if (sceneContext.Inspector().IsDraggingMeshPreview()) {
        sceneContext.Inspector().EndMeshPreviewDrag();
        return true;
    }
    if (!sceneContext.Inspector().IsDraggingFloat()) {
        return false;
    }
    InspectorPanelState& inspector = sceneContext.Inspector();
    const InspectorPropertyId property = inspector.DraggedProperty();
    const bool moved = inspector.FloatDragMoved();
    const float startValue = inspector.DragStartValue();
    const bool audioScrubActive = inspector.AudioScrub().active;
    const bool audioInteger = inspector.AudioScrub().integer;
    const std::int64_t audioStartInteger = inspector.AudioScrub().startInteger;
    const float audioStartFloat = inspector.AudioScrub().startFloat;
    const int dragIndex = inspector.EditIndex(); // captured before EndFloatDrag clears it
    inspector.EndFloatDrag();
    if (audioScrubActive) {
        EditorAudioScrubTransactions transactions{ sceneContext };
        static_cast<void>(InspectorAudioScrubController::Finish(
            sceneContext.Scene(), sceneContext.SelectedEntity(), moved, transactions, inspector.AudioScrub()));
        if (!moved && property != InspectorPropertyId::None) {
            inspector.BeginTextEdit(property, audioInteger ? std::to_string(audioStartInteger) : FormatCompactFloat(audioStartFloat));
            inspector.SetEditIndex(dragIndex);
        }
        return true;
    }
    if (IsMaterialFloatProperty(property)) {
        if (moved) {
            static_cast<void>(sceneContext.CommitActiveMaterialAssetEdit());
        } else {
            sceneContext.CancelActiveMaterialAssetEdit();
        }
        if (!moved && property != InspectorPropertyId::None) {
            inspector.BeginTextEdit(property, FormatCompactFloat(startValue));
        }
        return true;
    }
    if (IsGenericEntityFloatProperty(property)) {
        // Each scrub step already applied via its own undoable command. A click
        // without movement opens the inline editor (restoring the row index that
        // BeginTextEdit clears).
        if (!moved && property != InspectorPropertyId::None) {
            if (InspectorAudioComponentModel::IsIntegerProperty(property)) {
                std::int64_t value = 0;
                inspector.BeginTextEdit(
                    property,
                    InspectorAudioComponentModel::ReadInteger(sceneContext.Scene(), sceneContext.SelectedEntity(), property, value)
                        ? std::to_string(value)
                        : std::string{});
            } else {
                inspector.BeginTextEdit(property, FormatCompactFloat(startValue));
            }
            inspector.SetEditIndex(dragIndex);
        }
        return true;
    }
    if (moved) {
        static_cast<void>(sceneContext.CommitActiveTransformEdit());
    } else {
        sceneContext.CancelActiveTransformEdit();
    }
    if (!moved && property != InspectorPropertyId::None) {
        inspector.BeginTextEdit(property, FormatCompactFloat(startValue));
    }
    return true;
}

void InspectorPanelInteraction::CancelPointerDrag(EditorSceneContext& sceneContext) noexcept {
    InspectorPanelState& inspector = sceneContext.Inspector();
    if (!inspector.IsDraggingFloat()) {
        return;
    }
    if (inspector.AudioScrub().active) {
        CancelAudioScrub(sceneContext);
        return;
    }
    if (IsMaterialFloatProperty(inspector.DraggedProperty())) {
        sceneContext.CancelActiveMaterialAssetEdit();
    } else if (!IsGenericEntityFloatProperty(inspector.DraggedProperty())) {
        sceneContext.CancelActiveTransformEdit();
    }
    inspector.EndFloatDrag();
}

bool InspectorPanelInteraction::HandleChar(EditorSceneContext& sceneContext, wchar_t character) {
    if (!sceneContext.Inspector().IsTextEditing()) {
        return false;
    }
    if (character == VK_BACK || character == VK_ESCAPE || character == VK_RETURN) {
        return false;
    }
    sceneContext.Inspector().AppendText(character);
    return true;
}

bool InspectorPanelInteraction::HandleKeyDown(HWND owner, EditorSceneContext& sceneContext, WPARAM key) {
    if (!sceneContext.Inspector().IsTextEditing()) {
        return false;
    }

    InspectorPanelState& inspector = sceneContext.Inspector();
    switch (EditorTextInputShortcuts::Resolve(key)) {
    case EditorTextInputShortcut::SelectAll:
        inspector.SelectAllText();
        return true;
    case EditorTextInputShortcut::Copy:
        static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(owner, inspector.EditBuffer()));
        return true;
    case EditorTextInputShortcut::Cut:
        static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(owner, inspector.EditBuffer()));
        inspector.ClearText();
        return true;
    case EditorTextInputShortcut::Paste:
        if (const std::optional<std::string> text = EditorTextInputShortcuts::PasteFromClipboard(owner); text.has_value()) {
            inspector.InsertText(*text);
        }
        return true;
    case EditorTextInputShortcut::None:
        break;
    }

    switch (key) {
    case VK_BACK:
        inspector.BackspaceText();
        return true;
    case VK_ESCAPE:
        if (inspector.EditedProperty() == InspectorPropertyId::AddComponentSearch) {
            inspector.CloseAddComponentBrowser();
            return true;
        }
        inspector.EndTextEdit();
        return true;
    case VK_RETURN: {
        if (inspector.EditedProperty() == InspectorPropertyId::AddComponentSearch) {
            // Enter adds the top search match — but only while actually searching
            // (an empty query is the category list, where Enter must do nothing).
            if (!inspector.EditBuffer().empty()) {
                const std::vector<AddComponentRow> rows =
                    InspectorAddComponentBrowserModel::Rows(
                        {}, inspector.EditBuffer(),
                        [&sceneContext](std::string_view pluginId) {
                            return sceneContext.IsProjectPluginEnabled(pluginId);
                        });
                const kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
                if (!rows.empty() && sceneContext.Scene().Entities().IsAlive(entity)) {
                    static_cast<void>(sceneContext.AddComponentToEntity(entity, rows.front().id));
                }
                inspector.CloseAddComponentBrowser();
            }
            return true;
        }
        if (inspector.EditedProperty() == InspectorPropertyId::InputActionName) {
            static_cast<void>(sceneContext.SetInputActionName(sceneContext.AssetBrowser().InspectorAsset(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (inspector.EditedProperty() == InspectorPropertyId::InputMappingScale) {
            float scale = 0.0F;
            const int index = inspector.EditIndex();
            if (index >= 0 && ParseFloat(inspector.EditBuffer(), scale)) {
                static_cast<void>(sceneContext.SetInputMappingScale(sceneContext.AssetBrowser().InspectorAsset(), static_cast<std::size_t>(index), scale));
            }
            inspector.EndTextEdit();
            return true;
        }
        if (inspector.EditedProperty() == InspectorPropertyId::ScriptVariable) {
            const kb::scene::SceneEntity variableEntity = sceneContext.SelectedEntity();
            const int index = inspector.EditIndex();
            const std::vector<EditorSceneContext::EntityScriptVariable> variables = sceneContext.EntityScriptExposedVariables(variableEntity);
            if (sceneContext.Scene().Entities().IsAlive(variableEntity) && index >= 0 && static_cast<std::size_t>(index) < variables.size()) {
                const EditorSceneContext::EntityScriptVariable& variable = variables[static_cast<std::size_t>(index)];
                if (const std::optional<kb::script::ScriptValue> parsed = ParseScriptVariableEditText(inspector.EditBuffer(), variable); parsed.has_value()) {
                    static_cast<void>(sceneContext.SetEntityScriptVariable(variableEntity, variable.name, *parsed));
                }
            }
            inspector.EndTextEdit();
            return true;
        }
        if (IsMaterialFloatProperty(inspector.EditedProperty())) {
            const kb::assets::AssetId asset = sceneContext.AssetBrowser().InspectorAsset();
            const std::optional<kb::render::RenderMaterialAssetData> material = sceneContext.ReadMaterialAsset(asset);
            float currentValue = 0.0F;
            float value = 0.0F;
            if (material.has_value() &&
                ReadMaterialFloat(*material, inspector.EditedProperty(), currentValue) &&
                EvaluateMath(inspector.EditBuffer(), currentValue, value)) {
                static_cast<void>(WriteMaterialFloat(sceneContext, asset, inspector.EditedProperty(), value));
            }
            inspector.EndTextEdit();
            return true;
        }
        const kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
        if (sceneContext.Scene().Entities().IsAlive(entity) &&
            IsNavAgentProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplyNavAgentText(sceneContext, entity, inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) &&
            IsNavObstacleProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplyNavObstacleText(sceneContext, entity, inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) &&
            IsRegionShapeProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplyRegionShapeText(sceneContext, entity, inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) &&
            IsContentInstanceProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplyContentInstanceText(sceneContext, entity, inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) &&
            IsStreamFocusProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplyStreamFocusText(sceneContext, entity, inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) &&
            IsWorldBackdropProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplyWorldBackdropText(sceneContext, entity, inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) &&
            IsAmbientRadianceProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplyAmbientRadianceText(sceneContext, entity, inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) &&
            IsDetailSwitchProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplyDetailSwitchText(sceneContext, entity, inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) &&
            IsVisibilityBlockerProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplyVisibilityBlockerText(sceneContext, entity, inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) && IsVisibilityCellProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplyVisibilityCellText(sceneContext, entity, inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) && IsRegionPortalProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplyRegionPortalText(sceneContext, entity, inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) && IsSecondaryFrameProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplySecondaryFrameText(sceneContext, entity, inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) && IsGeometrySwarmProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplyGeometrySwarmText(sceneContext, entity, inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) && IsSurfaceCastProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplySurfaceCastText(sceneContext, entity, inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) && IsFacingPanelProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplyFacingPanelText(sceneContext, entity, inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) && IsSpaceStrokeProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplySpaceStrokeText(sceneContext, entity, inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) && IsHistoryRibbonProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplyHistoryRibbonText(sceneContext, entity, inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) &&
            (InspectorAudioComponentModel::IsFloatProperty(inspector.EditedProperty()) ||
                InspectorAudioComponentModel::IsIntegerProperty(inspector.EditedProperty()))) {
            const InspectorPropertyId property = inspector.EditedProperty();
            if (InspectorAudioComponentModel::IsFloatProperty(property)) {
                float currentValue = 0.0F;
                float value = 0.0F;
                if (InspectorAudioComponentModel::ReadFloat(sceneContext.Scene(), entity, property, currentValue) &&
                    EvaluateMath(inspector.EditBuffer(), currentValue, value)) {
                    static_cast<void>(ApplyAudioFloat(sceneContext, entity, property, value));
                }
            } else {
                static_cast<void>(ApplyAudioMutation(sceneContext, "Edit Audio Listener", [&]() {
                    return InspectorAudioComponentModel::ApplyText(sceneContext.Scene(), entity, property, inspector.EditBuffer());
                }));
            }
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) &&
            inspector.EditedProperty() == InspectorPropertyId::AnimatorSpeed) {
            float value = 0.0F;
            if (ParseFloat(inspector.EditBuffer(), value)) {
                static_cast<void>(sceneContext.SetAnimatorSpeed(entity, value));
            }
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) &&
            inspector.EditedProperty() == InspectorPropertyId::UIDocumentAsset) {
            std::uint64_t value = 0U;
            const std::string_view text = inspector.EditBuffer();
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()) {
                static_cast<void>(sceneContext.SetUIDocumentAsset(entity, kb::assets::AssetId{ value }));
            }
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) &&
            IsCameraFloatProperty(inspector.EditedProperty())) {
            const InspectorPropertyId property = inspector.EditedProperty();
            const kb::scene::CameraComponent* current =
                sceneContext.Scene().Components().Cameras().TryGet(entity);
            float currentValue = 0.0F;
            float value = 0.0F;
            if (current != nullptr &&
                ReadCameraFloat(*current, property, currentValue) &&
                EvaluateMath(inspector.EditBuffer(), currentValue, value)) {
                static_cast<void>(ApplyCameraFloat(
                    sceneContext, entity, property, value));
            }
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) &&
            (inspector.EditedProperty() ==
                    InspectorPropertyId::CameraViewportId ||
                inspector.EditedProperty() ==
                    InspectorPropertyId::CameraPriority ||
                inspector.EditedProperty() ==
                    InspectorPropertyId::CameraCullingMask)) {
            static_cast<void>(ApplyCameraInteger(
                sceneContext,
                entity,
                inspector.EditedProperty(),
                inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (IsTerrainTextProperty(inspector.EditedProperty())) {
            static_cast<void>(ApplyTerrainText(
                sceneContext, entity,
                inspector.EditedProperty(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) &&
            inspector.EditedProperty() == InspectorPropertyId::LightLayerMask) {
            static_cast<void>(ApplyLightLayerMask(
                sceneContext,
                entity,
                inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity) && IsLightFloatProperty(inspector.EditedProperty())) {
            const InspectorPropertyId property = inspector.EditedProperty();
            const kb::scene::LightComponent* current = sceneContext.Scene().Components().Lights().TryGet(entity);
            float currentValue = 0.0F;
            float value = 0.0F;
            if (current != nullptr &&
                ReadLightFloat(*current, property, currentValue) &&
                EvaluateMath(inspector.EditBuffer(), currentValue, value)) {
                static_cast<void>(MutateLightComponent(sceneContext, entity, "Edit Light Component", [property, value](kb::scene::LightComponent& light) {
                    static_cast<void>(WriteLightFloat(light, property, value));
                }));
            }
            inspector.EndTextEdit();
            return true;
        }
        if (const std::optional<PhysicsComponentKind> physicsKind = PhysicsKindForProperty(inspector.EditedProperty());
            physicsKind.has_value() && sceneContext.Scene().Entities().IsAlive(entity)) {
            const int index = inspector.EditIndex();
            float currentValue = 0.0F;
            float value = 0.0F;
            if (index >= 0 &&
                ReadPhysicsFloat(sceneContext, entity, *physicsKind, index, currentValue) &&
                EvaluateMath(inspector.EditBuffer(), currentValue, value)) {
                static_cast<void>(ApplyPhysicsFloat(sceneContext, entity, *physicsKind, index, value));
            }
            inspector.EndTextEdit();
            return true;
        }
        if (sceneContext.Scene().Entities().IsAlive(entity)) {
            const InspectorPropertyId property = inspector.EditedProperty();
            if (property == InspectorPropertyId::EntityName) {
                if (sceneContext.BeginSceneEditTransaction("Rename Entity")) {
                    sceneContext.Scene().Entities().SetName(entity, inspector.EditBuffer().empty() ? "Entity" : inspector.EditBuffer());
                    static_cast<void>(sceneContext.CommitSceneEditTransaction());
                }
            } else if (EditorTransformProperty::IsTransform(property)) {
                if (sceneContext.BeginSelectedTransformEdit("Edit Transform")) {
                    float value = 0.0F;
                    if (EvaluateMath(inspector.EditBuffer(), sceneContext.ActiveTransformEditPropertyStart(property), value)) {
                        static_cast<void>(sceneContext.ApplyActiveTransformEditProperty(property, value));
                        static_cast<void>(sceneContext.CommitActiveTransformEdit());
                    } else {
                        sceneContext.CancelActiveTransformEdit();
                    }
                }
            }
        }
        inspector.EndTextEdit();
        return true;
    }
    default:
        return false;
    }
}

bool InspectorPanelInteraction::HandleKeyCapture(EditorSceneContext& sceneContext, WPARAM virtualKey) {
    return InspectorInputInteraction::HandleKeyCapture(sceneContext, virtualKey);
}

bool InspectorPanelInteraction::UpdateHover(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit) noexcept {
    const int dropdownHover = hit.kind == InspectorHitKind::ValueTypeOption ? hit.index : -1;
    const int previousDropdownHover = sceneContext.Inspector().ValueTypeDropdownHover();
    sceneContext.Inspector().SetValueTypeDropdownHover(dropdownHover);
    const int tagsDropdownHover = hit.kind == InspectorHitKind::TagOption ? hit.index : -1;
    const int previousTagsDropdownHover = sceneContext.Inspector().TagsDropdownHover();
    sceneContext.Inspector().SetTagsDropdownHover(tagsDropdownHover);
    const bool hoverChanged = sceneContext.Inspector().SetHover(hit.kind, hit.section, hit.property, hit.index);
    return hoverChanged || previousDropdownHover != dropdownHover || previousTagsDropdownHover != tagsDropdownHover;
}

bool InspectorPanelInteraction::HandleMouseWheel(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit, int wheelDelta) noexcept {
    if (hit.kind != InspectorHitKind::MeshPreview) {
        return false;
    }
    const float direction = wheelDelta > 0 ? 0.10F : -0.10F;
    return sceneContext.Inspector().ZoomMeshPreview(direction);
}

bool InspectorPanelInteraction::HandleDoubleClick(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit) noexcept {
    if (hit.kind != InspectorHitKind::MeshPreview) {
        return false;
    }
    sceneContext.Inspector().ResetMeshPreview();
    return true;
}

} // namespace kb::editor

#endif
