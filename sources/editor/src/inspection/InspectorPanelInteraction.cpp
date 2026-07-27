#include "inspection/InspectorPanelInteraction.hpp"

#if defined(_WIN32)
#include "app/EditorTextInputShortcuts.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "inspection/InspectorAddComponentBrowserModel.hpp"
#include "inspection/InspectorComponentCatalog.hpp"
#include "inspection/InspectorInputInteraction.hpp"
#include "inspection/InspectorPhysicsModel.hpp"
#include "kb/render/resources/RenderMaterialNumericParsing.hpp"
#include "scene/transform_edit/EditorTransformProperty.hpp"

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
        const std::vector<AddComponentRow> rows = InspectorAddComponentBrowserModel::Rows(query.empty() ? std::string_view{ category } : std::string_view{}, query);
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

[[nodiscard]] bool ToggleAudioSourceProperty(kb::scene::AudioSourceComponent& source, InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::AudioSourceEnabled:
        source.enabled = !source.enabled;
        return true;
    case InspectorPropertyId::AudioSourceAutoplay:
        source.autoplay = !source.autoplay;
        return true;
    case InspectorPropertyId::AudioSourceLoop:
        source.loop = !source.loop;
        return true;
    case InspectorPropertyId::AudioSourceMute:
        source.mute = !source.mute;
        return true;
    case InspectorPropertyId::AudioSourceSpatial:
        source.spatial = !source.spatial;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool ToggleAudioListenerProperty(kb::scene::AudioListenerComponent& listener, InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::AudioListenerEnabled:
        listener.enabled = !listener.enabled;
        return true;
    case InspectorPropertyId::AudioListenerPrimary:
        listener.primary = !listener.primary;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] kb::scene::LightKind NextLightKind(kb::scene::LightKind kind) noexcept {
    switch (kind) {
    case kb::scene::LightKind::Directional:
        return kb::scene::LightKind::Point;
    case kb::scene::LightKind::Point:
        return kb::scene::LightKind::Spot;
    case kb::scene::LightKind::Spot:
    case kb::scene::LightKind::AreaRect:
    case kb::scene::LightKind::AreaDisk:
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
    if (hit.kind == InspectorHitKind::FloatField && IsLightFloatProperty(hit.property)) {
        if (const kb::scene::LightComponent* light = sceneContext.Scene().Components().Lights().TryGet(entity); light != nullptr) {
            float value = 0.0F;
            if (ReadLightFloat(*light, hit.property, value)) {
                sceneContext.Inspector().BeginTextEdit(hit.property, FormatCompactFloat(value));
            }
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
        hit.property == InspectorPropertyId::CameraViewportId) {
        sceneContext.Inspector().BeginTextEdit(
            hit.property, std::to_string(camera->viewportId));
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

[[nodiscard]] bool ToggleAudioProperty(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property) {
    if (!sceneContext.BeginSceneEditTransaction("Edit Audio Component")) {
        return true;
    }

    bool changed = false;
    if (kb::scene::AudioSourceComponent* source = sceneContext.Scene().Components().AudioSources().TryGet(entity); source != nullptr) {
        changed = ToggleAudioSourceProperty(*source, property);
        if (changed) {
            sceneContext.Scene().Components().AudioSources().MarkModified(entity);
        }
    }

    if (!changed) {
        if (kb::scene::AudioListenerComponent* listener = sceneContext.Scene().Components().AudioListeners().TryGet(entity); listener != nullptr) {
            changed = ToggleAudioListenerProperty(*listener, property);
            if (changed) {
                sceneContext.Scene().Components().AudioListeners().MarkModified(entity);
            }
        }
    }

    if (changed) {
        static_cast<void>(sceneContext.CommitSceneEditTransaction());
    } else {
        sceneContext.CancelSceneEditTransaction();
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
        IsCameraFloatProperty(property);
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
    return false;
}

// Writes the value through the field's own undoable path (each is a self-contained
// begin+commit, so no held transaction conflicts with the click/commit handlers).
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
        IsCameraFloatProperty(hit.property);
    if (!isPhysicsFloat && !isComponentFloat) {
        return false;
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
        return false;
    }

    kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
    if (const std::optional<InspectorDisclosureId> disclosure = DisclosureForProperty(hit.property);
        hit.kind == InspectorHitKind::Row && disclosure.has_value()) {
        sceneContext.Inspector().EndTextEdit();
        sceneContext.Inspector().CloseAddComponentBrowser();
        sceneContext.Inspector().CloseComponentMenus();
        sceneContext.Inspector().ToggleDisclosure(*disclosure);
        return true;
    }
    if (hit.kind == InspectorHitKind::SectionHeader) {
        sceneContext.Inspector().EndTextEdit();
        sceneContext.Inspector().CloseAddComponentBrowser();
        sceneContext.Inspector().CloseComponentMenus();
        sceneContext.Inspector().ToggleCollapsed(hit.section);
        return true;
    }
    if (hit.kind == InspectorHitKind::ComponentMenuButton) {
        sceneContext.Inspector().EndTextEdit();
        if (hit.property == InspectorPropertyId::ComponentRemove && sceneContext.Scene().Entities().IsAlive(entity)) {
            if (hit.section == InspectorSectionId::Script) {
                static_cast<void>(sceneContext.RemoveScriptFromEntity(entity));
            } else if (hit.section == InspectorSectionId::MeshRenderer) {
                static_cast<void>(sceneContext.RemoveMeshRendererFromEntity(entity));
            } else if (hit.section == InspectorSectionId::Camera) {
                static_cast<void>(RemoveCameraComponent(sceneContext, entity));
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

    // Drag-to-scrub: pressing a numeric entity field (physics/light float) starts a
    // horizontal scrub; a click that does not move opens the inline editor on
    // pointer-up (handled below in HandlePointerUp). Transform/material fields have
    // their own scrub paths and are not caught here.
    if ((hit.kind == InspectorHitKind::TextField || hit.kind == InspectorHitKind::FloatField) &&
        IsGenericEntityFloatProperty(hit.property) &&
        BeginEntityFloatDrag(sceneContext, entity, hit, x, y)) {
        return true;
    }

    if (hit.section == InspectorSectionId::Script) {
        return HandleScriptClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::MeshRenderer) {
        return HandleMeshRendererClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::Camera) {
        return HandleCameraClick(sceneContext, entity, hit);
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
    const kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
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
    // Physics/light floats: scrub in 0.1 steps (~6 px per step) as requested.
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
    const int dragIndex = inspector.EditIndex(); // captured before EndFloatDrag clears it
    inspector.EndFloatDrag();
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
            inspector.BeginTextEdit(property, FormatCompactFloat(startValue));
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
                const std::vector<const InspectorComponentTile*> tiles = InspectorComponentCatalog::Search(inspector.EditBuffer());
                const kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
                if (!tiles.empty() && sceneContext.Scene().Entities().IsAlive(entity)) {
                    static_cast<void>(sceneContext.AddComponentToEntity(entity, tiles.front()->id));
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
                    InspectorPropertyId::CameraPriority)) {
            static_cast<void>(ApplyCameraInteger(
                sceneContext,
                entity,
                inspector.EditedProperty(),
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
    const bool hoverChanged = sceneContext.Inspector().SetHover(hit.kind, hit.section, hit.property, hit.index);
    return hoverChanged || previousDropdownHover != dropdownHover;
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
