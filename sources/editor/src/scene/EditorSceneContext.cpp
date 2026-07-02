#include "scene/EditorSceneContext.hpp"

#include "engine/audio/AudioPlayback.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneVisitors.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/script/ScriptBehaviourAsset.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneInputActivation.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/modules/EngineModuleHost.hpp"
#include "engine/project/ProjectDescriptorWriter.hpp"
#include "engine/script/ScriptModule.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "kb/render/resources/RenderMaterialTypeAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include "scene/EditorScriptAssetGateway.hpp"
#include "scene/input/EditorInputActionAuthoring.hpp"
#include "scene/input/EditorInputAssetGateway.hpp"
#include "scene/input/EditorInputMappingContextAuthoring.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include "scene/EditorDefaultSceneFactory.hpp"
#include "scene/EditorHierarchyRowBuilder.hpp"
#include "scene/EditorPluginCatalog.hpp"
#include "scene/EditorSceneAssetBrowserCommands.hpp"
#include "scene/EditorSceneCommandController.hpp"
#include "scene/EditorSceneAudioAssetActions.hpp"
#include "scene/EditorSceneHierarchyActions.hpp"
#include "scene/EditorSceneMaterialAssetActions.hpp"
#include "scene/EditorSceneMeshAssetActions.hpp"
#include "scene/EditorSceneObjectEditCommands.hpp"
#include "scene/EditorScenePrefabActions.hpp"
#include "scene/EditorSceneSelectionPivot.hpp"
#include "scene/material/EditorMaterialAssetAuthoring.hpp"
#include "scene/material/EditorMaterialAssetEditCommand.hpp"
#include "scene/material/EditorMaterialAssetGateway.hpp"
#include "scene/material/EditorMaterialReferenceFinder.hpp"
#include "scene/material/EditorMaterialTextureSlotValidation.hpp"
#include "scene/material/EditorEmbeddedMaterialExtractor.hpp"
#include "scene/material_preview/EditorMaterialGraphCookService.hpp"
#include "scene/material_preview/EditorMaterialPreviewScene.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "scene/transform_edit/EditorSceneTransformCommitBuilder.hpp"
#include "scene/transform_edit/EditorSceneTransformEditApplier.hpp"
#include "scene/transform_edit/EditorSceneTransformEditController.hpp"
#include "scene/transform_edit/EditorSceneTransformSnapshotBuilder.hpp"
#include "project/EditorProjectBootstrap.hpp"
#include "project/EditorProjectPaths.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace kb::editor {
namespace {

constexpr std::string_view kSceneDocumentExtension = ".21kbscene";
constexpr std::string_view kEditorLiveAssetOverrideCategory = "EditorLiveOverride";


[[nodiscard]] bool ContainsEntity(std::span<const kb::scene::SceneEntity> entities, kb::scene::SceneEntity entity) noexcept {
    return std::ranges::find(entities, entity) != entities.end();
}

[[nodiscard]] std::vector<kb::assets::AssetId> MaterialAssetIds(const kb::assets::AssetManager& manager) {
    std::vector<kb::assets::AssetId> materials;
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (EditorSceneMaterialAssetActions::IsMaterialAsset(metadata)) {
            materials.push_back(metadata.id);
        }
    }
    std::ranges::sort(materials, [](kb::assets::AssetId lhs, kb::assets::AssetId rhs) {
        return lhs.value < rhs.value;
    });
    return materials;
}

[[nodiscard]] kb::assets::AssetId NextMaterialAssetId(std::span<const kb::assets::AssetId> materials, std::uint64_t current) {
    if (materials.empty()) {
        return {};
    }
    if (current == 0U) {
        return materials.front();
    }
    const auto currentIt = std::ranges::find_if(materials, [current](kb::assets::AssetId candidate) {
        return candidate.value == current;
    });
    if (currentIt == materials.end()) {
        return {};
    }
    const auto nextIt = std::next(currentIt);
    return nextIt == materials.end() ? kb::assets::AssetId{} : *nextIt;
}

[[nodiscard]] kb::render::RenderMaterialAlphaMode NextAlphaMode(kb::render::RenderMaterialAlphaMode mode) noexcept {
    switch (mode) {
    case kb::render::RenderMaterialAlphaMode::Opaque:
        return kb::render::RenderMaterialAlphaMode::Mask;
    case kb::render::RenderMaterialAlphaMode::Mask:
        return kb::render::RenderMaterialAlphaMode::Blend;
    case kb::render::RenderMaterialAlphaMode::Blend:
        return kb::render::RenderMaterialAlphaMode::Opaque;
    }
    return kb::render::RenderMaterialAlphaMode::Opaque;
}

[[nodiscard]] bool IsTextureAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderTexture" || metadata.type == "Texture" || metadata.importCategory == "Texture";
}

[[nodiscard]] std::filesystem::path ResolveAssetPath(const kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata) {
    if (!metadata.physicalPath.empty()) {
        return metadata.physicalPath;
    }
    return manager.Mounts().Resolve(metadata.virtualPath).value_or(std::filesystem::path{});
}

[[nodiscard]] std::uint64_t HashBytes(std::string_view text) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char ch : text) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 1099511628211ULL;
    }
    return hash == 0U ? 1U : hash;
}

[[nodiscard]] std::uint64_t MaterialWorkingCopyRuntimeContentHash(const kb::render::RenderMaterialAssetData& material) {
    kb::render::RenderMaterialAssetData runtimeRelevant = material;
    for (kb::render::RenderMaterialGraphNode& node : runtimeRelevant.graph.nodes) {
        node.positionX = 0;
        node.positionY = 0;
    }

    std::ostringstream output;
    kb::render::RenderMaterialAssetWriter::Write(output, runtimeRelevant);
    return HashBytes(output.str());
}

[[nodiscard]] std::filesystem::path SceneMaterialWorkingCopyRuntimePath(kb::assets::AssetId materialAssetId) {
    return std::filesystem::temp_directory_path() / ("21kb_scene_material_working_" + std::to_string(materialAssetId.value) + ".kbmat");
}

[[nodiscard]] std::optional<kb::render::RenderMaterialTypeDocument> LoadMaterialTypeDocumentForMaterial(
    const kb::assets::AssetManager& manager,
    const kb::render::RenderMaterialAssetData& material) {
    if (material.materialTypeAssetId != 0U) {
        if (const kb::assets::AssetMetadata* metadata = manager.Registry().Find(kb::assets::AssetId{ material.materialTypeAssetId });
            metadata != nullptr && metadata->type == kb::render::kRenderMaterialTypeAssetType) {
            const std::filesystem::path path = ResolveAssetPath(manager, *metadata);
            if (!path.empty()) {
                return kb::render::RenderMaterialTypeAssetLoader::LoadType(path);
            }
        }
    }

    if (!material.materialTypeAssetPath.empty()) {
        const std::filesystem::path authoredPath{ material.materialTypeAssetPath };
        if (const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(authoredPath);
            metadata != nullptr && metadata->type == kb::render::kRenderMaterialTypeAssetType) {
            const std::filesystem::path path = ResolveAssetPath(manager, *metadata);
            if (!path.empty()) {
                return kb::render::RenderMaterialTypeAssetLoader::LoadType(path);
            }
        }
        if (const std::optional<std::filesystem::path> resolved = manager.Mounts().Resolve(authoredPath)) {
            return kb::render::RenderMaterialTypeAssetLoader::LoadType(*resolved);
        }
    }

    return std::nullopt;
}

[[nodiscard]] const kb::assets::AssetMetadata* ResolveTypedAssetReference(
    const kb::assets::AssetManager& manager,
    std::uint64_t assetId,
    std::string_view assetPath,
    std::string_view expectedType) {
    if (assetId != 0U) {
        if (const kb::assets::AssetMetadata* metadata = manager.Registry().Find(kb::assets::AssetId{ assetId });
            metadata != nullptr && metadata->type == expectedType) {
            return metadata;
        }
    }

    if (!assetPath.empty()) {
        const std::filesystem::path authoredPath{ std::string{ assetPath } };
        if (const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(authoredPath);
            metadata != nullptr && metadata->type == expectedType) {
            return metadata;
        }
    }

    return nullptr;
}

[[nodiscard]] kb::render::RenderMaterialTypeSchema MaterialEditorSchemaForMaterial(
    const kb::assets::AssetManager& manager,
    const kb::render::RenderMaterialAssetData& material) {
    if (const std::optional<kb::render::RenderMaterialTypeDocument> type = LoadMaterialTypeDocumentForMaterial(manager, material);
        type.has_value() && type->stableTypeId == material.materialType && type->version == material.materialTypeVersion) {
        return type->schema;
    }
    return kb::render::GetBuiltInPbrMaterialTypeSchema();
}

[[nodiscard]] std::string MaterialSchemaRefreshDiagnosticLine(const kb::render::RenderMaterialSchemaRefreshDiagnostic& diagnostic) {
    return "schema_refresh: " + diagnostic.message;
}

[[nodiscard]] std::uint64_t MaterialTextureSlotValue(const kb::render::RenderMaterialAssetData& asset, EditorMaterialTextureSlot slot) noexcept {
    switch (slot) {
    case EditorMaterialTextureSlot::Albedo:
        return asset.desc.albedoTextureAssetId;
    case EditorMaterialTextureSlot::Normal:
        return asset.desc.normalTextureAssetId;
    case EditorMaterialTextureSlot::MetallicRoughness:
        return asset.desc.metallicRoughnessTextureAssetId;
    case EditorMaterialTextureSlot::Occlusion:
        return asset.desc.occlusionTextureAssetId;
    case EditorMaterialTextureSlot::Emissive:
        return asset.desc.emissiveTextureAssetId;
    }
    return 0U;
}

[[nodiscard]] std::vector<kb::assets::AssetId> TextureAssetIds(const kb::assets::AssetManager& manager) {
    std::vector<kb::assets::AssetId> textures;
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (IsTextureAsset(metadata)) {
            textures.push_back(metadata.id);
        }
    }
    std::ranges::sort(textures, [](kb::assets::AssetId lhs, kb::assets::AssetId rhs) {
        return lhs.value < rhs.value;
    });
    return textures;
}

[[nodiscard]] std::optional<EditorMaterialTextureSlot> TextureSlotForMaterialField(std::string_view field) noexcept {
    if (field == "albedoTextureAssetId") return EditorMaterialTextureSlot::Albedo;
    if (field == "normalTextureAssetId") return EditorMaterialTextureSlot::Normal;
    if (field == "metallicRoughnessTextureAssetId") return EditorMaterialTextureSlot::MetallicRoughness;
    if (field == "occlusionTextureAssetId") return EditorMaterialTextureSlot::Occlusion;
    if (field == "emissiveTextureAssetId") return EditorMaterialTextureSlot::Emissive;
    return std::nullopt;
}

[[nodiscard]] std::string MaterialTextureValidationSlotName(EditorMaterialTextureSlot slot) {
    return std::string{ EditorMaterialTextureSlotValidation::SemanticName(EditorMaterialTextureSlotValidation::ExpectedSemantic(slot)) };
}

void AppendMaterialTextureValidationDiagnostics(
    const kb::assets::AssetManager& manager,
    const kb::render::RenderMaterialAssetData& material,
    const kb::render::RenderMaterialTypeSchema& schema,
    std::vector<std::string>& diagnostics,
    bool& hasError) {
    for (const kb::render::RenderMaterialTextureSlotSchema& textureSlot : schema.textureSlots) {
        const std::optional<EditorMaterialTextureSlot> mappedSlot = TextureSlotForMaterialField(textureSlot.assetIdFieldName);
        if (!mappedSlot.has_value()) {
            continue;
        }
        const EditorMaterialTextureSlot slot = *mappedSlot;
        const std::uint64_t textureId = MaterialTextureSlotValue(material, slot);
        if (textureId == 0U) {
            continue;
        }

        const kb::assets::AssetMetadata* texture = manager.Registry().Find(kb::assets::AssetId{ textureId });
        if (texture == nullptr) {
            diagnostics.push_back("texture_missing: " + MaterialTextureValidationSlotName(slot) + " texture asset id " + std::to_string(textureId) + " could not be resolved; runtime fallback will be used.");
            continue;
        }
        if (!IsTextureAsset(*texture)) {
            hasError = true;
            diagnostics.push_back("texture_invalid_asset: " + MaterialTextureValidationSlotName(slot) + " references non-texture asset " + texture->virtualPath.generic_string() + ".");
            continue;
        }

        const EditorMaterialTextureSlotValidationResult validation = EditorMaterialTextureSlotValidation::Validate(*texture, slot);
        if (!validation.accepted) {
            diagnostics.push_back(
                "texture_color_space_mismatch: " + MaterialTextureValidationSlotName(slot)
                + " " + EditorMaterialTextureSlotValidation::RejectionMessage(*texture, validation));
        }
    }
}

void AppendMaterialTypeReferenceValidationDiagnostics(
    const kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& metadata,
    const kb::render::RenderMaterialAssetData& material,
    std::vector<std::string>& diagnostics,
    bool& hasError) {
    const kb::render::RenderMaterialTypeReferenceValidationResult validation =
        kb::render::ValidateRenderMaterialTypeReference(material, metadata, manager);
    for (const kb::render::RenderMaterialTypeReferenceDiagnostic& diagnostic : validation.diagnostics) {
        hasError = true;
        std::string message = "material_type_reference: ";
        message += kb::render::RenderMaterialTypeReferenceDiagnosticCodeName(diagnostic.code);
        message += ": ";
        message += diagnostic.message;
        diagnostics.push_back(std::move(message));
    }
}

[[nodiscard]] kb::assets::AssetId NextTextureAssetId(std::span<const kb::assets::AssetId> textures, std::uint64_t current) {
    if (textures.empty()) {
        return {};
    }
    if (current == 0U) {
        return textures.front();
    }
    const auto currentIt = std::ranges::find_if(textures, [current](kb::assets::AssetId candidate) {
        return candidate.value == current;
    });
    if (currentIt == textures.end()) {
        return {};
    }
    const auto nextIt = std::next(currentIt);
    return nextIt == textures.end() ? kb::assets::AssetId{} : *nextIt;
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

[[nodiscard]] std::unique_ptr<IEditorMaterialAssetPropertyEdit> MaterialFloatEditForProperty(InspectorPropertyId property, float value) {
    switch (property) {
    case InspectorPropertyId::MaterialBaseColorR:
        return std::make_unique<EditorMaterialBaseColorChannelEdit>(0, value);
    case InspectorPropertyId::MaterialBaseColorG:
        return std::make_unique<EditorMaterialBaseColorChannelEdit>(1, value);
    case InspectorPropertyId::MaterialBaseColorB:
        return std::make_unique<EditorMaterialBaseColorChannelEdit>(2, value);
    case InspectorPropertyId::MaterialBaseColorA:
        return std::make_unique<EditorMaterialBaseColorChannelEdit>(3, value);
    case InspectorPropertyId::MaterialMetallicFactor:
        return std::make_unique<EditorMaterialMetallicFactorEdit>(value);
    case InspectorPropertyId::MaterialRoughnessFactor:
        return std::make_unique<EditorMaterialRoughnessFactorEdit>(value);
    case InspectorPropertyId::MaterialNormalScale:
        return std::make_unique<EditorMaterialNormalScaleEdit>(value);
    case InspectorPropertyId::MaterialOcclusionStrength:
        return std::make_unique<EditorMaterialOcclusionStrengthEdit>(value);
    case InspectorPropertyId::MaterialEmissiveColorR:
        return std::make_unique<EditorMaterialEmissiveColorChannelEdit>(0, value);
    case InspectorPropertyId::MaterialEmissiveColorG:
        return std::make_unique<EditorMaterialEmissiveColorChannelEdit>(1, value);
    case InspectorPropertyId::MaterialEmissiveColorB:
        return std::make_unique<EditorMaterialEmissiveColorChannelEdit>(2, value);
    case InspectorPropertyId::MaterialEmissiveStrength:
        return std::make_unique<EditorMaterialEmissiveStrengthEdit>(value);
    case InspectorPropertyId::MaterialAlphaCutoff:
        return std::make_unique<EditorMaterialAlphaCutoffEdit>(value);
    default:
        return {};
    }
}

[[nodiscard]] std::optional<float> ParsePlainFloat(std::string_view text) {
    std::string copy{ text };
    char* end = nullptr;
    const float value = std::strtof(copy.c_str(), &end);
    if (end == copy.c_str()) {
        return std::nullopt;
    }
    while (end != nullptr && *end != '\0') {
        if (!std::isspace(static_cast<unsigned char>(*end))) {
            return std::nullopt;
        }
        ++end;
    }
    return value;
}

[[nodiscard]] std::optional<kb::render::RenderMaterialGraphParameterValue> ParseMaterialGraphParameterValue(
    std::string_view stableId,
    kb::render::RenderMaterialParameterType type,
    std::string_view text) {
    kb::render::RenderMaterialGraphParameterValue value{};
    value.stableId = std::string{ stableId };
    value.type = type;

    std::string normalized{ text };
    std::ranges::replace(normalized, ',', ' ');
    std::istringstream input{ normalized };
    switch (type) {
    case kb::render::RenderMaterialParameterType::Scalar:
        if (input >> value.numbers[0]) {
            return value;
        }
        return std::nullopt;
    case kb::render::RenderMaterialParameterType::Vec3:
        if (input >> value.numbers[0] >> value.numbers[1] >> value.numbers[2]) {
            return value;
        }
        return std::nullopt;
    case kb::render::RenderMaterialParameterType::Vec4:
    case kb::render::RenderMaterialParameterType::Color:
        if (input >> value.numbers[0] >> value.numbers[1] >> value.numbers[2]) {
            if (!(input >> value.numbers[3])) {
                value.numbers[3] = 1.0F;
            }
            return value;
        }
        return std::nullopt;
    case kb::render::RenderMaterialParameterType::Bool: {
        std::string boolText;
        if (!(input >> boolText)) {
            return std::nullopt;
        }
        std::ranges::transform(boolText, boolText.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (boolText == "true" || boolText == "1" || boolText == "yes" || boolText == "on") {
            value.boolValue = true;
            return value;
        }
        if (boolText == "false" || boolText == "0" || boolText == "no" || boolText == "off") {
            value.boolValue = false;
            return value;
        }
        return std::nullopt;
    }
    case kb::render::RenderMaterialParameterType::Enum:
        value.text = std::string{ text };
        return value.text.empty() ? std::nullopt : std::optional<kb::render::RenderMaterialGraphParameterValue>{ value };
    case kb::render::RenderMaterialParameterType::Texture:
        return std::nullopt;
    }
    return std::nullopt;
}

void UpsertGraphParameterValue(
    kb::render::RenderMaterialAssetData& material,
    kb::render::RenderMaterialGraphParameterValue value) {
    for (kb::render::RenderMaterialGraphParameterValue& existing : material.graphParameterValues) {
        if (existing.stableId == value.stableId) {
            existing = std::move(value);
            return;
        }
    }
    material.graphParameterValues.push_back(std::move(value));
}

void RemoveGraphParameterValue(
    kb::render::RenderMaterialAssetData& material,
    std::string_view stableId) {
    const auto oldEnd = std::remove_if(material.graphParameterValues.begin(), material.graphParameterValues.end(), [stableId](const kb::render::RenderMaterialGraphParameterValue& value) {
        return value.stableId == stableId;
    });
    material.graphParameterValues.erase(oldEnd, material.graphParameterValues.end());
}

void EnsureMaterialInstanceOverrideDocument(
    kb::render::RenderMaterialInstanceAssetData& instance,
    const kb::render::RenderMaterialAssetData& parentMaterial) {
    if (!instance.hasOverrides) {
        instance.hasOverrides = true;
        instance.overrides = kb::render::RenderMaterialAssetData{};
    }
    instance.overrides.materialType = parentMaterial.materialType;
    instance.overrides.materialTypeVersion = parentMaterial.materialTypeVersion;
    instance.overrides.hasExplicitMaterialType = true;
    instance.overrides.hasExplicitMaterialTypeVersion = true;
    if (instance.overrides.materialTypeAssetId == 0U) {
        instance.overrides.materialTypeAssetId = parentMaterial.materialTypeAssetId;
    }
    if (instance.overrides.materialTypeAssetPath.empty()) {
        instance.overrides.materialTypeAssetPath = parentMaterial.materialTypeAssetPath;
    }
    if (instance.overrides.graphSourceAssetId == 0U) {
        instance.overrides.graphSourceAssetId = parentMaterial.graphSourceAssetId;
    }
    if (instance.overrides.graphSourceAssetPath.empty()) {
        instance.overrides.graphSourceAssetPath = parentMaterial.graphSourceAssetPath;
    }
}

std::vector<std::string> MaterialInstanceValidationDiagnosticLines(
    const kb::render::RenderMaterialInstanceValidationResult& validation) {
    std::vector<std::string> diagnostics;
    diagnostics.reserve(validation.diagnostics.size());
    for (const kb::render::RenderMaterialInstanceValidationDiagnostic& diagnostic : validation.diagnostics) {
        diagnostics.push_back(std::string{ kb::render::RenderMaterialInstanceValidationDiagnosticCodeName(diagnostic.code) } + ": " + diagnostic.message);
    }
    return diagnostics;
}

[[nodiscard]] std::string MaterialGraphPinConnectionDiagnostic(
    const kb::render::RenderMaterialAssetData& material,
    std::uint32_t fromNodeId,
    std::string_view fromPin,
    std::uint32_t toNodeId,
    std::string_view toPin) {
    if (fromNodeId == toNodeId) {
        return "Material graph pins are not compatible: node " + std::to_string(fromNodeId) + " cannot connect to itself.";
    }
    const kb::render::RenderMaterialGraphNode* fromNode = kb::render::FindRenderMaterialGraphNode(material.graph, fromNodeId);
    const kb::render::RenderMaterialGraphNode* toNode = kb::render::FindRenderMaterialGraphNode(material.graph, toNodeId);
    if (fromNode == nullptr || toNode == nullptr) {
        return "Material graph pins are not compatible: one endpoint no longer exists.";
    }
    const kb::render::RenderMaterialGraphPinType fromType = kb::render::RenderMaterialGraphPinDataType(*fromNode, fromPin, true);
    const kb::render::RenderMaterialGraphPinType toType = kb::render::RenderMaterialGraphPinDataType(*toNode, toPin, false);
    return "Material graph pins are not compatible: node " + std::to_string(fromNodeId) +
        " output '" + std::string{ fromPin } + "' (" + std::string{ kb::render::RenderMaterialGraphPinTypeName(fromType) } +
        ") cannot connect to node " + std::to_string(toNodeId) +
        " input '" + std::string{ toPin } + "' (" + std::string{ kb::render::RenderMaterialGraphPinTypeName(toType) } + ").";
}

[[nodiscard]] bool HasSelectedAncestor(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::span<const kb::scene::SceneEntity> selected) noexcept {
    kb::scene::SceneEntity parent = scene.Hierarchy().Parent(entity);
    while (parent.IsValid()) {
        if (ContainsEntity(selected, parent)) {
            return true;
        }
        parent = scene.Hierarchy().Parent(parent);
    }
    return false;
}

[[nodiscard]] std::vector<kb::scene::SceneEntity> TopLevelSelectedEntities(const kb::scene::Scene& scene, std::span<const kb::scene::SceneEntity> entities) {
    std::vector<kb::scene::SceneEntity> filtered;
    filtered.reserve(entities.size());
    for (const kb::scene::SceneEntity entity : entities) {
        if (!entity.IsValid() || ContainsEntity(filtered, entity) || HasSelectedAncestor(scene, entity, entities)) {
            continue;
        }
        filtered.push_back(entity);
    }
    return filtered;
}

[[nodiscard]] bool AnyAlive(const kb::scene::Scene& scene, std::span<const kb::scene::SceneEntity> entities) noexcept {
    for (const kb::scene::SceneEntity entity : entities) {
        if (scene.Entities().IsAlive(entity)) {
            return true;
        }
    }
    return false;
}

void AppendEntityBranchRenderDirty(
    const kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    std::vector<std::uint64_t>& dirtyEntityIds) {
    if (!entity.IsValid()) {
        return;
    }
    if (std::ranges::find(dirtyEntityIds, entity.Id()) == dirtyEntityIds.end()) {
        dirtyEntityIds.push_back(entity.Id());
    }
    if (!scene.Entities().IsAlive(entity)) {
        return;
    }
    for (const kb::scene::SceneEntity child : scene.Hierarchy().ChildEntities(entity)) {
        AppendEntityBranchRenderDirty(scene, child, dirtyEntityIds);
    }
}

[[nodiscard]] std::string AssetErrorOr(const kb::assets::AssetManager& manager, const char* fallback) {
    const std::string error = manager.LastError();
    return error.empty() ? std::string{ fallback } : error;
}

[[nodiscard]] std::size_t ImportStatusCount(const kb::assets::AssetImportResult& result, kb::assets::AssetImportItemStatus status) noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(result.items, [status](const kb::assets::AssetImportItemResult& item) {
        return item.status == status;
    }));
}

[[nodiscard]] std::string ImportSourceLabel(const kb::assets::AssetImportItemResult& item) {
    const std::filesystem::path filename = item.sourcePath.filename();
    return filename.empty() ? item.sourcePath.generic_string() : filename.generic_string();
}

[[nodiscard]] std::string ImportReportLine(const kb::assets::AssetImportItemResult& item) {
    std::string message = std::string{ kb::assets::ToString(item.category) } + " " + std::string{ kb::assets::ToString(item.status) } + ": " + ImportSourceLabel(item);
    if (!item.virtualPath.empty()) {
        message += " -> " + item.virtualPath.generic_string();
    }
    if (!item.error.empty()) {
        message += " (" + item.error + ")";
    }
    return message;
}

void LogAssetImportReport(EditorConsoleState& console, const kb::assets::AssetImportResult& result, const std::filesystem::path& destinationVirtualFolder) {
    if (result.items.empty()) {
        return;
    }

    const std::size_t failed = ImportStatusCount(result, kb::assets::AssetImportItemStatus::Failed);
    console.Info("Assets",
        "Import report for " + destinationVirtualFolder.generic_string() +
        ": created=" + std::to_string(result.CreatedCount()) +
        ", reused=" + std::to_string(result.ReusedCount()) +
        ", missing=" + std::to_string(result.MissingCount()) +
        ", unsupported=" + std::to_string(result.UnsupportedCount()) +
        ", failed=" + std::to_string(failed));

    for (const kb::assets::AssetImportItemResult& item : result.items) {
        const std::string line = ImportReportLine(item);
        switch (item.status) {
        case kb::assets::AssetImportItemStatus::Created:
        case kb::assets::AssetImportItemStatus::Reused:
            console.Info("Assets", line);
            break;
        case kb::assets::AssetImportItemStatus::Missing:
        case kb::assets::AssetImportItemStatus::Unsupported:
            console.Warning("Assets", line);
            break;
        case kb::assets::AssetImportItemStatus::Failed:
        case kb::assets::AssetImportItemStatus::None:
        default:
            console.Error("Assets", line);
            break;
        }
    }
}

[[nodiscard]] std::filesystem::path EnsureSceneDocumentExtension(std::filesystem::path path) {
    if (path.extension() != kSceneDocumentExtension) {
        path.replace_extension(kSceneDocumentExtension);
    }
    return path;
}

void RegisterEditorRenderAssetLoaders(kb::scene::Scene& scene) {
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialGraphAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialInstanceAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialParameterCollectionAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialTypeAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderTextureAssetLoader>()));
}

} // namespace

EditorSceneContext::EditorSceneContext()
    // Load the project descriptor first, then construct the scene from it so the
    // scene's engine module host honours the project's enabled/disabled module set.
    : projectBootstrap_(EditorProjectBootstrap::BootstrapDefaultProject())
    , project_(projectBootstrap_.succeeded ? projectBootstrap_.descriptor : kb::project::ProjectDescriptor{})
    , projectFile_(projectBootstrap_.succeeded ? projectBootstrap_.projectFile : EditorProjectPaths::ProjectFile())
    , scene_(std::make_unique<kb::scene::Scene>(project_))
    , materialPreviewScene_(std::make_unique<EditorMaterialPreviewScene>())
    , graphShaderCacheRoot_((EditorProjectPaths::ProjectRoot() / ".cache" / "graph_shaders").generic_string())
    , materialGraphCookService_(std::make_unique<EditorMaterialGraphCookService>(EditorMaterialGraphCookConfig::Resolve(graphShaderCacheRoot_))) {
    if (projectBootstrap_.succeeded) {
        console_.Info("Project", projectBootstrap_.created ? "Created project descriptor." : "Loaded project descriptor.");
    } else {
        console_.Error("Project", projectBootstrap_.error.empty() ? "Project descriptor bootstrap failed." : projectBootstrap_.error);
    }

    if (scene_->Assets().MountProject(EditorProjectPaths::ProjectRoot())) {
        console_.Info("Project", "Mounted project assets.");
    } else {
        console_.Error("Project", AssetErrorOr(scene_->Assets().Manager(), "Project assets could not be mounted."));
    }
    RegisterEditorRenderAssetLoaders(*scene_);
    const std::size_t discovered = scene_->Assets().Discover();
    console_.Info("Assets", "Asset discovery completed. Found " + std::to_string(discovered) + " asset(s).");
    currentScenePath_ = ResolveDefaultScenePath();
    std::error_code error;
    if (!currentScenePath_.empty() && std::filesystem::is_regular_file(currentScenePath_, error) && !error && kb::scene::SceneDocumentService::LoadFileIntoScene(*scene_, currentScenePath_)) {
        SelectFirstSceneEntityOrClear();
        console_.Info("Project", "Opened default scene: " + currentScenePath_.generic_string());
    } else {
        hierarchySelection_.SelectEntity(EditorDefaultSceneFactory::Seed(*scene_));
        if (SaveCurrentScene()) {
            console_.Info("Project", "Created default scene: " + currentScenePath_.generic_string());
        }
    }
    console_.Info("Editor", "Editor scene initialized.");
}

EditorSceneContext::~EditorSceneContext() = default;

void EditorSceneContext::EnsureScriptRuntime() {
    if (scriptModuleHost_ != nullptr) {
        return;
    }
    EditorConsoleState* console = &console_;

    kb::script::ScriptModuleOptions scriptOptions;
    scriptOptions.configureHost = [console](kb::script::ScriptRuntimeHost& host) {
        // Log("...") in any script prints to the editor Console. console_ outlives
        // scriptModuleHost_ (declared last, destroyed first), so capturing it is safe.
        kb::script::ScriptFunctionDesc logDesc;
        logDesc.signature.name = "Log";
        logDesc.signature.inputs = { kb::script::ScriptFunctionPin{ "message", kb::script::ScriptValueType::String, true } };
        logDesc.signature.outputs = {};
        logDesc.callback = [console](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument> arguments) {
            std::string message;
            for (const kb::script::ScriptFunctionArgument& argument : arguments) {
                if (argument.name == "message") {
                    message = argument.value.AsString();
                    break;
                }
            }
            console->Info("Script", message);
            return kb::script::ScriptFunctionCallResult{ .executed = true, .outputs = {}, .errors = {} };
        };
        static_cast<void>(host.RegisterFunction(std::move(logDesc)));
    };

    auto scriptModule = std::make_unique<kb::script::ScriptModule>(std::move(scriptOptions));
    kb::script::ScriptModule* scriptModuleView = scriptModule.get();
    scriptModule_ = scriptModuleView;
    scriptModuleHost_ = std::make_unique<kb::modules::EngineModuleHost>(project_);
    scriptModuleHost_->Add(std::move(scriptModule));
    scriptModuleHost_->Load(scene_->Runtime().EcsWorld());
    scriptModuleHost_->AttachScene(*scene_);

    if (!scriptModuleHost_->IsActive("Script")) {
        console_.Warning("Scripts", "Script module is disabled for this project; behaviours will not run.");
        return;
    }

    if (!scriptModuleView->Succeeded()) {
        for (const std::string& diagnostic : scriptModuleView->Diagnostics()) {
            console_.Error("Scripts", diagnostic);
        }
        console_.Error("Scripts", "Script runtime could not be fully initialized; behaviours may not run.");
    } else {
        console_.Info("Scripts", "Script runtime ready for play mode.");
    }
}

void EditorSceneContext::ResetScriptRuntimeStateForPlayMode() {
    if (scriptModule_ == nullptr || scriptModule_->Host() == nullptr) {
        return;
    }
    kb::script::ScriptRuntimeHost& host = *scriptModule_->Host();
    host.LuaRuntime().Clear();
    host.VisualGraphInstances().Clear();
    host.SharedState().Clear();
}

kb::scene::Scene& EditorSceneContext::Scene() noexcept {
    return *scene_;
}

const kb::scene::Scene& EditorSceneContext::Scene() const noexcept {
    return *scene_;
}

EditorAssetBrowserState& EditorSceneContext::AssetBrowser() noexcept {
    return assetBrowser_;
}

const EditorAssetBrowserState& EditorSceneContext::AssetBrowser() const noexcept {
    return assetBrowser_;
}

EditorViewportPreviewState& EditorSceneContext::ViewportPreview() noexcept {
    return viewportState_.Preview();
}

const EditorViewportPreviewState& EditorSceneContext::ViewportPreview() const noexcept {
    return viewportState_.Preview();
}

EditorViewportPreviewState& EditorSceneContext::ViewportPreview(std::uint64_t viewportKey) noexcept {
    return viewportState_.Preview(viewportKey);
}

const EditorViewportPreviewState& EditorSceneContext::ViewportPreview(std::uint64_t viewportKey) const noexcept {
    return viewportState_.Preview(viewportKey);
}

EditorViewportCameraState& EditorSceneContext::ViewportCamera() noexcept {
    return viewportState_.Camera();
}

const EditorViewportCameraState& EditorSceneContext::ViewportCamera() const noexcept {
    return viewportState_.Camera();
}

EditorViewportCameraState& EditorSceneContext::ViewportCamera(std::uint64_t viewportKey) noexcept {
    return viewportState_.Camera(viewportKey);
}

const EditorViewportCameraState& EditorSceneContext::ViewportCamera(std::uint64_t viewportKey) const noexcept {
    return viewportState_.Camera(viewportKey);
}

void EditorSceneContext::BeginViewportCameraNavigation(std::uint64_t viewportKey, EditorViewportCameraNavigationMode mode, int x, int y) noexcept {
    viewportState_.BeginCameraNavigation(viewportKey, mode, x, y);
}

bool EditorSceneContext::HasActiveViewportCameraNavigation() const noexcept {
    return viewportState_.HasActiveCameraNavigation();
}

std::uint64_t EditorSceneContext::ActiveViewportCameraKey() const noexcept {
    return viewportState_.ActiveCameraKey();
}

EditorViewportCameraState* EditorSceneContext::ActiveViewportCamera() noexcept {
    return viewportState_.ActiveCamera();
}

const EditorViewportCameraState* EditorSceneContext::ActiveViewportCamera() const noexcept {
    return viewportState_.ActiveCamera();
}

void EditorSceneContext::EndViewportCameraNavigation() noexcept {
    viewportState_.EndCameraNavigation();
}

bool EditorSceneContext::CloseViewportToolbarDropdowns() noexcept {
    return viewportState_.CloseToolbarDropdowns();
}

InspectorPanelState& EditorSceneContext::Inspector() noexcept {
    return inspector_;
}

const InspectorPanelState& EditorSceneContext::Inspector() const noexcept {
    return inspector_;
}

MaterialEditorState& EditorSceneContext::MaterialEditor() noexcept {
    return materialEditor_;
}

const MaterialEditorState& EditorSceneContext::MaterialEditor() const noexcept {
    return materialEditor_;
}

EditorProjectSettingsState& EditorSceneContext::ProjectSettings() noexcept {
    return projectSettings_;
}

const EditorProjectSettingsState& EditorSceneContext::ProjectSettings() const noexcept {
    return projectSettings_;
}

EditorPluginsState& EditorSceneContext::Plugins() noexcept {
    return plugins_;
}

const EditorPluginsState& EditorSceneContext::Plugins() const noexcept {
    return plugins_;
}

EditorScriptEditorState& EditorSceneContext::ScriptEditor() noexcept {
    return scriptEditor_;
}

const EditorScriptEditorState& EditorSceneContext::ScriptEditor() const noexcept {
    return scriptEditor_;
}

EditorConsoleState& EditorSceneContext::Console() noexcept {
    return console_;
}

const EditorConsoleState& EditorSceneContext::Console() const noexcept {
    return console_;
}

EditorSceneGizmoState& EditorSceneContext::Gizmo() noexcept {
    return viewportState_.Gizmo();
}

const EditorSceneGizmoState& EditorSceneContext::Gizmo() const noexcept {
    return viewportState_.Gizmo();
}

const kb::project::ProjectDescriptor& EditorSceneContext::Project() const noexcept {
    return project_;
}

const std::filesystem::path& EditorSceneContext::ProjectFile() const noexcept {
    return projectFile_;
}

const std::filesystem::path& EditorSceneContext::CurrentScenePath() const noexcept {
    return currentScenePath_;
}

std::uint64_t EditorSceneContext::SceneRenderRevision() const noexcept {
    return sceneRenderRevision_;
}

std::uint64_t EditorSceneContext::SceneRenderDirtyBaseRevision() const noexcept {
    return sceneRenderDirtyBaseRevision_;
}

bool EditorSceneContext::SceneRenderFullDirty() const noexcept {
    return sceneRenderFullDirty_;
}

const std::vector<std::uint64_t>& EditorSceneContext::SceneRenderDirtyEntityIds() const noexcept {
    return sceneRenderDirtyEntityIds_;
}

bool EditorSceneContext::SceneDocumentDirty() const noexcept {
    return sceneDocumentDirty_;
}

void EditorSceneContext::MarkSceneRenderDirty() noexcept {
    ++sceneRenderRevision_;
    if (sceneRenderRevision_ == 0U) {
        sceneRenderRevision_ = 1U;
    }
    InvalidateHierarchyRows();
    sceneRenderFullDirty_ = true;
    sceneRenderDirtyBaseRevision_ = sceneRenderRevision_;
    sceneRenderDirtyEntityIds_.clear();
}

void EditorSceneContext::MarkSceneEntitiesRenderDirty(std::span<const kb::scene::SceneEntity> entities) {
    if (entities.empty()) {
        return;
    }
    if (!sceneRenderFullDirty_ && sceneRenderDirtyEntityIds_.empty()) {
        sceneRenderDirtyBaseRevision_ = sceneRenderRevision_;
    }

    ++sceneRenderRevision_;
    if (sceneRenderRevision_ == 0U) {
        sceneRenderRevision_ = 1U;
    }

    if (sceneRenderFullDirty_) {
        return;
    }
    for (const kb::scene::SceneEntity entity : entities) {
        AppendEntityBranchRenderDirty(*scene_, entity, sceneRenderDirtyEntityIds_);
    }
}

void EditorSceneContext::AcknowledgeSceneRenderSubmitted() noexcept {
    sceneRenderFullDirty_ = false;
    sceneRenderDirtyEntityIds_.clear();
    sceneRenderDirtyBaseRevision_ = sceneRenderRevision_;
}

void EditorSceneContext::MarkSceneDocumentDirty() noexcept {
    sceneDocumentDirty_ = true;
}

bool EditorSceneContext::SaveDirtySceneDocument(std::string_view reason) {
    if (!sceneDocumentDirty_) {
        return true;
    }
    if (!SaveCurrentScene()) {
        console_.Error("Project", "Dirty scene save failed before " + std::string{ reason } + ".");
        return false;
    }
    return true;
}

void EditorSceneContext::DiscardDirtySceneDocument(std::string_view reason) {
    if (!sceneDocumentDirty_) {
        return;
    }
    sceneDocumentDirty_ = false;
    console_.Warning("Project", "Unsaved scene changes discarded before " + std::string{ reason } + ".");
}

bool EditorSceneContext::PrepareDirtySceneTransition(std::string_view reason, EditorDirtySceneResolution resolution) {
    if (!sceneDocumentDirty_) {
        return true;
    }

    if (resolution == EditorDirtySceneResolution::Discard) {
        console_.Warning("Project", "Unsaved scene changes discarded before " + std::string{ reason } + ".");
        return true;
    }

    return SaveDirtySceneDocument(reason);
}

bool EditorSceneContext::BeginPlayModeSceneSession() {
    if (playModeSceneSession_.Active()) {
        return true;
    }
    if (plugins_.HasPendingReload() && !ReloadSceneFromProject()) {
        return false;
    }
    if (!SaveDirtySceneDocument("entering play mode")) {
        return false;
    }
    kb::audio::AudioPlayback::StopAll(*scene_);

    const std::string name = currentScenePath_.stem().string().empty() ? std::string{ "Main" } : currentScenePath_.stem().string();
    if (!playModeSceneSession_.Begin(*scene_, name)) {
        console_.Error("Play Mode", "Scene snapshot could not be captured.");
        return false;
    }
    EnsureScriptRuntime();
    ResetScriptRuntimeStateForPlayMode();
    kb::scene::SceneInputActivation::Apply(*scene_);
    ActivateProjectInput();
    console_.Info("Play Mode", "Captured editor scene snapshot.");
    return true;
}

bool EditorSceneContext::RestorePlayModeSceneSession() {
    if (!playModeSceneSession_.Active()) {
        return true;
    }
    kb::audio::AudioPlayback::StopAll(*scene_);
    kb::scene::SceneInputActivation::Clear(*scene_);
    if (!playModeSceneSession_.Restore(*scene_)) {
        console_.Error("Play Mode", "Editor scene snapshot could not be restored.");
        return false;
    }

    SelectFirstSceneEntityOrClear();
    ResetSceneEditState();
    ClearSceneDocumentDirty();
    console_.Info("Play Mode", "Restored editor scene snapshot.");
    return true;
}

bool EditorSceneContext::HasPlayModeSceneSession() const noexcept {
    return playModeSceneSession_.Active();
}

bool EditorSceneContext::ReloadSceneFromProject() {
    if (!RestorePlayModeSceneSession()) {
        return false;
    }
    if (!SaveDirtySceneDocument("reloading project plugins")) {
        return false;
    }
    if (currentScenePath_.empty() && !SaveCurrentScene()) {
        console_.Error("Project", "Scene could not be saved before reloading project plugins.");
        return false;
    }

    if (scriptModuleHost_ != nullptr) {
        scriptModuleHost_->DetachScene(*scene_);
        scriptModuleHost_->Unload();
        scriptModuleHost_.reset();
        scriptModule_ = nullptr;
    }

    auto nextScene = std::make_unique<kb::scene::Scene>(project_);
    if (nextScene->Assets().MountProject(EditorProjectPaths::ProjectRoot())) {
        console_.Info("Project", "Mounted project assets.");
    } else {
        console_.Error("Project", AssetErrorOr(nextScene->Assets().Manager(), "Project assets could not be mounted."));
        return false;
    }
    RegisterEditorRenderAssetLoaders(*nextScene);
    const std::size_t discovered = nextScene->Assets().Discover();
    console_.Info("Assets", "Asset discovery completed. Found " + std::to_string(discovered) + " asset(s).");

    if (!currentScenePath_.empty() && !kb::scene::SceneDocumentService::LoadFileIntoScene(*nextScene, currentScenePath_)) {
        console_.Error("Project", "Scene could not be reloaded: " + currentScenePath_.generic_string());
        return false;
    }

    scene_ = std::move(nextScene);
    plugins_.ClearPendingReload();
    SelectFirstSceneEntityOrClear();
    ResetSceneEditState();
    ClearSceneDocumentDirty();
    console_.Info("Project", "Reloaded scene with current project plugin settings.");
    return true;
}

bool EditorSceneContext::NewScene(EditorDirtySceneResolution dirtyResolution) {
    if (!RestorePlayModeSceneSession()) {
        return false;
    }
    if (!PrepareDirtySceneTransition("creating a new scene", dirtyResolution)) {
        return false;
    }

    const std::vector<kb::scene::SceneEntity> roots = scene_->Hierarchy().RootEntities();
    for (const kb::scene::SceneEntity root : roots) {
        scene_->Entities().Destroy(root);
    }

    hierarchySelection_.SelectEntity(EditorDefaultSceneFactory::Seed(*scene_));
    currentScenePath_ = EditorProjectPaths::UniqueScenePath("Untitled");
    ResetSceneEditState();
    MarkSceneDocumentDirty();
    console_.Info("Project", "New scene created: " + currentScenePath_.generic_string());
    return true;
}

bool EditorSceneContext::OpenDefaultScene() {
    return OpenScene(ResolveDefaultScenePath());
}

bool EditorSceneContext::OpenScene(const std::filesystem::path& path, EditorDirtySceneResolution dirtyResolution) {
    if (!RestorePlayModeSceneSession()) {
        return false;
    }
    if (!PrepareDirtySceneTransition("opening a scene", dirtyResolution)) {
        return false;
    }

    const std::filesystem::path scenePath = EnsureSceneDocumentExtension(path);

    if (!kb::scene::SceneDocumentService::LoadFileIntoScene(*scene_, scenePath)) {
        console_.Error("Project", "Scene could not be opened: " + scenePath.generic_string());
        return false;
    }

    currentScenePath_ = scenePath;
    SelectFirstSceneEntityOrClear();
    ResetSceneEditState();
    ClearSceneDocumentDirty();
    console_.Info("Project", "Opened scene: " + currentScenePath_.generic_string());
    return true;
}

bool EditorSceneContext::SaveCurrentScene() {
    if (playModeSceneSession_.Active()) {
        console_.Warning("Project", "Scene save ignored while play mode is active. Stop play mode before saving.");
        return false;
    }
    if (currentScenePath_.empty()) {
        currentScenePath_ = EditorProjectPaths::DefaultScenePath();
    }

    return SaveSceneToPath(currentScenePath_);
}

bool EditorSceneContext::SaveCurrentSceneAs(const std::filesystem::path& path) {
    if (playModeSceneSession_.Active()) {
        console_.Warning("Project", "Save As ignored while play mode is active. Stop play mode before saving.");
        return false;
    }
    return SaveSceneToPath(path);
}

bool EditorSceneContext::SaveSceneToPath(const std::filesystem::path& path) {
    const std::filesystem::path scenePath = EnsureSceneDocumentExtension(path.empty() ? EditorProjectPaths::DefaultScenePath() : path);
    std::error_code error;
    if (!scenePath.parent_path().empty()) {
        std::filesystem::create_directories(scenePath.parent_path(), error);
        if (error) {
            console_.Error("Project", "Scene directory could not be created: " + scenePath.parent_path().generic_string());
            return false;
        }
    }

    const std::string name = scenePath.stem().string().empty() ? std::string{ "Main" } : scenePath.stem().string();
    if (!kb::scene::SceneDocumentService::Save(*scene_, scenePath, name)) {
        console_.Error("Project", "Scene could not be saved: " + scenePath.generic_string());
        return false;
    }

    currentScenePath_ = scenePath;
    static_cast<void>(scene_->Assets().Discover());
    ClearSceneDocumentDirty();
    console_.Info("Project", "Saved scene: " + currentScenePath_.generic_string());
    return true;
}

bool EditorSceneContext::CanUndoSceneCommand() const noexcept {
    return commandStack_.CanUndo();
}

bool EditorSceneContext::CanRedoSceneCommand() const noexcept {
    return commandStack_.CanRedo();
}

bool EditorSceneContext::UndoSceneCommand() {
    static_cast<void>(CommitHierarchyRename());
    inspector_.EndTextEdit();
    const bool undone = SceneCommands().Undo();
    if (undone && commandStack_.LastCompletedCommandAffectsOpenMaterialSource()) {
        RefreshOpenMaterialEditorFromSource();
    } else if (undone && materialEditor_.OpenAssetId().IsValid()) {
        SyncMaterialEditorWorkingCopyRuntimePreview();
        MarkSceneRenderDirty();
    }
    return undone;
}

bool EditorSceneContext::RedoSceneCommand() {
    static_cast<void>(CommitHierarchyRename());
    inspector_.EndTextEdit();
    const bool redone = SceneCommands().Redo();
    if (redone && commandStack_.LastCompletedCommandAffectsOpenMaterialSource()) {
        RefreshOpenMaterialEditorFromSource();
    } else if (redone && materialEditor_.OpenAssetId().IsValid()) {
        SyncMaterialEditorWorkingCopyRuntimePreview();
        MarkSceneRenderDirty();
    }
    return redone;
}

bool EditorSceneContext::BeginSceneEditTransaction(std::string label) {
    return SceneCommands().BeginTransaction(std::move(label));
}

bool EditorSceneContext::CommitSceneEditTransaction() {
    return SceneCommands().CommitTransaction();
}

void EditorSceneContext::CancelSceneEditTransaction() {
    SceneCommands().CancelTransaction();
}

bool EditorSceneContext::HasPendingSceneEditTransaction() const noexcept {
    return pendingSceneTransactionLabel_.has_value();
}

kb::scene::SceneEntity EditorSceneContext::SelectedEntity() const noexcept {
    return hierarchySelection_.Primary();
}

const std::vector<kb::scene::SceneEntity>& EditorSceneContext::SelectedHierarchyEntities() const noexcept {
    return hierarchySelection_.SelectedEntities();
}

bool EditorSceneContext::IsHierarchyEntitySelected(kb::scene::SceneEntity entity) const noexcept {
    return hierarchySelection_.IsSelected(entity);
}

void EditorSceneContext::SelectEntity(kb::scene::SceneEntity entity) noexcept {
    const kb::scene::SceneEntity selected = scene_->Entities().IsAlive(entity) ? entity : kb::scene::SceneEntity{};
    if (hierarchyRenameEntity_.IsValid() && hierarchyRenameEntity_ != selected) {
        static_cast<void>(CommitHierarchyRename());
    }
    materialGraphFocused_ = false;
    hierarchySelection_.SelectEntity(selected);
    assetBrowser_.ClearSelection();
}

void EditorSceneContext::SelectHierarchyEntities(std::span<const kb::scene::SceneEntity> entities) noexcept {
    std::vector<kb::scene::SceneEntity> alive;
    alive.reserve(entities.size());
    for (const kb::scene::SceneEntity entity : entities) {
        if (scene_->Entities().IsAlive(entity) && !ContainsEntity(alive, entity)) {
            alive.push_back(entity);
        }
    }

    if (alive.empty()) {
        ClearHierarchySelection();
        return;
    }

    if (hierarchyRenameEntity_.IsValid() && !ContainsEntity(alive, hierarchyRenameEntity_)) {
        static_cast<void>(CommitHierarchyRename());
    }
    materialGraphFocused_ = false;
    hierarchySelection_.SelectEntities(alive);
    assetBrowser_.ClearSelection();
}

void EditorSceneContext::ClearHierarchySelection() noexcept {
    static_cast<void>(CommitHierarchyRename());
    materialGraphFocused_ = false;
    hierarchySelection_.Clear();
}

bool EditorSceneContext::SelectHierarchyRow(std::size_t rowIndex) noexcept {
    return SelectHierarchyRow(rowIndex, false, false);
}

bool EditorSceneContext::SelectHierarchyRow(std::size_t rowIndex, bool additive, bool range) noexcept {
    const std::vector<EditorHierarchyRow>& rows = HierarchyRows();
    if (IsHierarchyRenaming()) {
        static_cast<void>(CommitHierarchyRename());
    }
    const bool selected = hierarchySelection_.SelectRow(rows, rowIndex, additive, range);
    if (selected) {
        assetBrowser_.ClearSelection();
    }
    return selected;
}

const EditorSceneViewportBoxSelectionState& EditorSceneContext::ViewportBoxSelection() const noexcept {
    return viewportBoxSelection_;
}

void EditorSceneContext::BeginViewportBoxSelection(const EditorSceneViewportBoxSelectionState& selection) noexcept {
    viewportBoxSelection_ = selection;
}

void EditorSceneContext::UpdateViewportBoxSelection(POINT current, bool active) noexcept {
    viewportBoxSelection_.current = current;
    viewportBoxSelection_.active = active;
}

void EditorSceneContext::ClearViewportBoxSelection() noexcept {
    viewportBoxSelection_ = {};
}

const std::vector<EditorHierarchyRow>& EditorSceneContext::HierarchyRows() const {
    RebuildHierarchyRowsIfNeeded();
    return hierarchyRowsCache_;
}

std::size_t EditorSceneContext::HierarchyRowCount() const {
    return HierarchyRows().size();
}

const EditorHierarchyRow* EditorSceneContext::HierarchyRowAt(std::size_t rowIndex) const {
    const std::vector<EditorHierarchyRow>& rows = HierarchyRows();
    return rowIndex < rows.size() ? &rows[rowIndex] : nullptr;
}

int EditorSceneContext::HierarchyScrollOffset() const noexcept {
    return hierarchyScrollOffset_;
}

bool EditorSceneContext::IsHierarchyScrollbarDragging() const noexcept {
    return hierarchyScrollbarDragging_;
}

bool EditorSceneContext::SetHierarchyScrollOffset(int offset, int maxOffset) noexcept {
    const int clamped = std::clamp(offset, 0, std::max(0, maxOffset));
    if (hierarchyScrollOffset_ == clamped) {
        return false;
    }
    hierarchyScrollOffset_ = clamped;
    return true;
}

void EditorSceneContext::BeginHierarchyScrollbarDrag(int y) noexcept {
    hierarchyScrollbarDragging_ = true;
    hierarchyScrollbarDragY_ = y;
    hierarchyScrollbarDragStartOffset_ = hierarchyScrollOffset_;
}

void EditorSceneContext::DragHierarchyScrollbar(int y, int trackTravel, int maxOffset) noexcept {
    if (!hierarchyScrollbarDragging_) {
        return;
    }

    const int delta = y - hierarchyScrollbarDragY_;
    const int offsetDelta = trackTravel <= 0 || maxOffset <= 0 ? 0 : (delta * maxOffset) / trackTravel;
    static_cast<void>(SetHierarchyScrollOffset(hierarchyScrollbarDragStartOffset_ + offsetDelta, maxOffset));
}

void EditorSceneContext::EndHierarchyScrollbarDrag() noexcept {
    hierarchyScrollbarDragging_ = false;
}

std::string_view EditorSceneContext::HierarchySearchQuery() const noexcept {
    return hierarchySearch_.Query();
}

bool EditorSceneContext::IsHierarchySearchFocused() const noexcept {
    return hierarchySearch_.IsFocused();
}

bool EditorSceneContext::IsHierarchyRenaming() const noexcept {
    return hierarchyRenameEntity_.IsValid() && scene_->Entities().IsAlive(hierarchyRenameEntity_);
}

bool EditorSceneContext::IsHierarchyRenaming(kb::scene::SceneEntity entity) const noexcept {
    return IsHierarchyRenaming() && hierarchyRenameEntity_ == entity;
}

bool EditorSceneContext::IsHierarchyRenameSelectingAll() const noexcept {
    return IsHierarchyRenaming() && hierarchyRenameSelectingAll_;
}

std::string_view EditorSceneContext::HierarchyRenameBuffer() const noexcept {
    return hierarchyRenameBuffer_;
}

void EditorSceneContext::FocusHierarchySearch(bool focused) noexcept {
    if (focused) {
        static_cast<void>(CommitHierarchyRename());
    }
    hierarchySearch_.Focus(focused);
}

void EditorSceneContext::SetHierarchySearchQuery(std::string query) {
    hierarchySearch_.SetQuery(std::move(query));
    InvalidateHierarchyRows();
}

void EditorSceneContext::AppendHierarchySearchText(wchar_t character) {
    hierarchySearch_.AppendAscii(character);
    InvalidateHierarchyRows();
}

void EditorSceneContext::InsertHierarchySearchText(std::string_view text) {
    hierarchySearch_.Insert(text);
    InvalidateHierarchyRows();
}

void EditorSceneContext::BackspaceHierarchySearch() {
    hierarchySearch_.Backspace();
    InvalidateHierarchyRows();
}

void EditorSceneContext::SelectAllHierarchySearch() noexcept {
    hierarchySearch_.SelectAll();
}

void EditorSceneContext::ClearHierarchySearch() {
    hierarchySearch_.Clear();
    InvalidateHierarchyRows();
}

bool EditorSceneContext::BeginHierarchyRename() {
    const kb::scene::SceneEntity entity = SelectedEntity();
    if (!scene_->Entities().IsAlive(entity)) {
        CancelHierarchyRename();
        return false;
    }

    hierarchySearch_.Focus(false);
    assetBrowser_.CancelTextEdit();
    inspector_.EndTextEdit();
    hierarchyRenameEntity_ = entity;
    hierarchyRenameBuffer_ = scene_->Entities().Name(entity);
    hierarchyRenameSelectingAll_ = true;
    InvalidateHierarchyRows();
    return true;
}

void EditorSceneContext::AppendHierarchyRenameText(wchar_t character) {
    if (!IsHierarchyRenaming()) {
        return;
    }
    if (character >= 32 && character <= 126) {
        if (hierarchyRenameSelectingAll_) {
            hierarchyRenameBuffer_.clear();
            hierarchyRenameSelectingAll_ = false;
        }
        hierarchyRenameBuffer_.push_back(static_cast<char>(character));
        InvalidateHierarchyRows();
    }
}

void EditorSceneContext::InsertHierarchyRenameText(std::string_view text) {
    if (!IsHierarchyRenaming()) {
        return;
    }
    if (hierarchyRenameSelectingAll_) {
        hierarchyRenameBuffer_.clear();
        hierarchyRenameSelectingAll_ = false;
    }
    for (const char character : text) {
        if (character >= 32 && character <= 126) {
            hierarchyRenameBuffer_.push_back(character);
        }
    }
    InvalidateHierarchyRows();
}

void EditorSceneContext::SetHierarchyRenameText(std::string text) {
    if (!IsHierarchyRenaming()) {
        return;
    }
    hierarchyRenameBuffer_ = std::move(text);
    hierarchyRenameSelectingAll_ = false;
    InvalidateHierarchyRows();
}

void EditorSceneContext::BackspaceHierarchyRename() {
    if (!IsHierarchyRenaming()) {
        return;
    }
    if (hierarchyRenameSelectingAll_) {
        hierarchyRenameBuffer_.clear();
        hierarchyRenameSelectingAll_ = false;
        InvalidateHierarchyRows();
        return;
    }
    if (!hierarchyRenameBuffer_.empty()) {
        hierarchyRenameBuffer_.pop_back();
    }
    InvalidateHierarchyRows();
}

void EditorSceneContext::SelectAllHierarchyRename() noexcept {
    if (IsHierarchyRenaming()) {
        hierarchyRenameSelectingAll_ = true;
        InvalidateHierarchyRows();
    }
}

void EditorSceneContext::ClearHierarchyRename() noexcept {
    if (!IsHierarchyRenaming()) {
        return;
    }
    hierarchyRenameBuffer_.clear();
    hierarchyRenameSelectingAll_ = false;
    InvalidateHierarchyRows();
}

bool EditorSceneContext::CommitHierarchyRename() {
    if (!IsHierarchyRenaming()) {
        CancelHierarchyRename();
        return false;
    }

    const kb::scene::SceneEntity entity = hierarchyRenameEntity_;
    const std::string name = hierarchyRenameBuffer_.empty() ? "Entity" : hierarchyRenameBuffer_;
    if (scene_->Entities().Name(entity) == name) {
        CancelHierarchyRename();
        return false;
    }

    const bool renamed = ExecuteSceneCommand("Rename Entity", [this, entity, name]() {
        if (!scene_->Entities().IsAlive(entity)) {
            return false;
        }
        scene_->Entities().SetName(entity, name);
        return true;
    });
    if (renamed) {
        console_.Info("Hierarchy", "Entity renamed.");
    }
    CancelHierarchyRename();
    return renamed;
}

void EditorSceneContext::CancelHierarchyRename() noexcept {
    const bool changed = hierarchyRenameEntity_.IsValid() || !hierarchyRenameBuffer_.empty() || hierarchyRenameSelectingAll_;
    hierarchyRenameEntity_ = {};
    hierarchyRenameBuffer_.clear();
    hierarchyRenameSelectingAll_ = false;
    if (changed) {
        InvalidateHierarchyRows();
    }
}

bool EditorSceneContext::BeginAssetFolderCreation() {
    static_cast<void>(CommitHierarchyRename());
    assetBrowser_.BeginNewFolder();
    return true;
}

bool EditorSceneContext::BeginAssetRename() {
    static_cast<void>(CommitHierarchyRename());
    return assetBrowser_.BeginRenameSelection(scene_->Assets().Manager());
}

bool EditorSceneContext::BeginAssetRename(kb::assets::AssetId id) {
    static_cast<void>(CommitHierarchyRename());
    return assetBrowser_.BeginRenameAsset(id, scene_->Assets().Manager());
}

bool EditorSceneContext::BeginAssetFolderRename(const std::filesystem::path& virtualFolder) {
    static_cast<void>(CommitHierarchyRename());
    return assetBrowser_.BeginRenameFolder(virtualFolder, scene_->Assets().Manager());
}

bool EditorSceneContext::CommitAssetTextEdit() {
    const bool committed = EditorSceneAssetBrowserCommands::CommitTextEdit(*scene_, assetBrowser_);
    if (committed) {
        console_.Info("Assets", "Asset browser text edit committed.");
    } else {
        console_.Error("Assets", AssetErrorOr(scene_->Assets().Manager(), "Asset browser text edit failed."));
    }
    return committed;
}

void EditorSceneContext::CancelAssetTextEdit() noexcept {
    assetBrowser_.CancelTextEdit();
}

bool EditorSceneContext::DeleteSelectedAssetBrowserItem() {
    const bool deleted = EditorSceneAssetBrowserCommands::DeleteSelected(*scene_, assetBrowser_);
    if (deleted) {
        console_.Info("Assets", "Selected asset browser item deleted.");
    } else {
        console_.Warning("Assets", "No asset browser item was deleted.");
    }
    return deleted;
}

bool EditorSceneContext::DeleteSelectedHierarchyEntity() noexcept {
    if (hierarchySelection_.SelectedEntities().empty()) {
        ClearHierarchySelection();
        return false;
    }

    const std::vector<kb::scene::SceneEntity> deleting = TopLevelSelectedEntities(*scene_, hierarchySelection_.SelectedEntities());
    if (!AnyAlive(*scene_, deleting)) {
        ClearHierarchySelection();
        console_.Warning("Hierarchy", "No hierarchy entity was deleted.");
        return false;
    }

    const std::vector<EditorSceneObjectPrefabPayload> payloads = EditorSceneObjectPayloadBuilder::Capture(
        *this,
        std::span<const kb::scene::SceneEntity>{ deleting.data(), deleting.size() });
    if (payloads.empty()) {
        console_.Warning("Hierarchy", "No hierarchy entity was deleted.");
        return false;
    }

    auto command = std::make_unique<EditorScenePrefabRemoveCommand>(*this, "Delete Entity", deleting, payloads);
    const bool deleted = commandStack_.Execute(std::move(command));
    if (deleted) {
        MarkSceneDocumentDirty();
        console_.Info("Hierarchy", "Selected hierarchy entity deleted.");
    } else {
        console_.Warning("Hierarchy", "No hierarchy entity was deleted.");
    }
    return deleted;
}

bool EditorSceneContext::DuplicateSelectedHierarchyEntities() {
    const std::vector<kb::scene::SceneEntity> selected = hierarchySelection_.SelectedEntities();
    const std::vector<kb::scene::SceneEntity> duplicating = TopLevelSelectedEntities(*scene_, selected);
    if (duplicating.empty() || !AnyAlive(*scene_, duplicating)) {
        console_.Warning("Hierarchy", "No hierarchy entity was duplicated.");
        return false;
    }

    const std::vector<EditorSceneObjectPrefabPayload> payloads = EditorSceneObjectPayloadBuilder::Capture(
        *this,
        std::span<const kb::scene::SceneEntity>{ duplicating.data(), duplicating.size() });
    if (payloads.empty()) {
        console_.Warning("Hierarchy", "No hierarchy entity was duplicated.");
        return false;
    }

    auto command = std::make_unique<EditorScenePrefabSpawnCommand>(*this, "Duplicate Entity", payloads);
    EditorScenePrefabSpawnCommand* duplicateCommand = command.get();
    const bool duplicated = commandStack_.Execute(std::move(command));
    if (duplicated) {
        SelectHierarchyEntities(duplicateCommand->CreatedEntities());
        MarkSceneDocumentDirty();
    }

    if (duplicated) {
        console_.Info("Hierarchy", "Selected hierarchy entity duplicated.");
    } else {
        console_.Warning("Hierarchy", "No hierarchy entity was duplicated.");
    }
    return duplicated;
}

bool EditorSceneContext::AdoptCreatedHierarchyEntities(std::string label, std::span<const kb::scene::SceneEntity> entities) {
    const std::vector<EditorSceneObjectPrefabPayload> payloads = EditorSceneObjectPayloadBuilder::Capture(*this, entities);
    if (payloads.empty() || !AnyAlive(*scene_, entities)) {
        return false;
    }

    std::vector<kb::scene::SceneEntity> alive;
    alive.reserve(entities.size());
    for (const kb::scene::SceneEntity entity : entities) {
        if (scene_->Entities().IsAlive(entity) && !ContainsEntity(alive, entity)) {
            alive.push_back(entity);
        }
    }
    if (alive.empty()) {
        return false;
    }

    auto command = std::make_unique<EditorScenePrefabSpawnCommand>(*this, std::move(label), payloads, alive);
    commandStack_.PushExecuted(std::move(command));
    SelectHierarchyEntities(alive);
    MarkSceneRenderDirty();
    MarkSceneDocumentDirty();
    scene_->Runtime().SynchronizeTransforms();
    return true;
}

bool EditorSceneContext::DeleteAssetBrowserItem(kb::assets::AssetId id) {
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (metadata != nullptr && EditorSceneMaterialAssetActions::IsMaterialAsset(*metadata)) {
        const std::vector<std::string> references = EditorMaterialReferenceFinder::FindSceneReferences(*scene_, id);
        if (!references.empty()) {
            console_.Warning(
                "Materials",
                "Material delete blocked; " + std::to_string(references.size()) + " scene reference(s) found. First: " + references.front());
            return false;
        }
    }

    const bool deleted = EditorSceneAssetBrowserCommands::DeleteAsset(*scene_, assetBrowser_, id);
    if (deleted) {
        console_.Info("Assets", "Asset deleted.");
    } else {
        console_.Error("Assets", AssetErrorOr(scene_->Assets().Manager(), "Asset delete failed."));
    }
    return deleted;
}

bool EditorSceneContext::DeleteAssetBrowserFolder(const std::filesystem::path& virtualFolder) {
    const bool deleted = EditorSceneAssetBrowserCommands::DeleteFolder(*scene_, assetBrowser_, virtualFolder);
    if (deleted) {
        console_.Info("Assets", "Folder deleted: " + virtualFolder.generic_string());
    } else {
        console_.Error("Assets", AssetErrorOr(scene_->Assets().Manager(), "Folder delete failed."));
    }
    return deleted;
}

bool EditorSceneContext::MoveAssetToFolder(kb::assets::AssetId id, const std::filesystem::path& destinationVirtualFolder) {
    const bool moved = EditorSceneAssetBrowserCommands::MoveAssetToFolder(*scene_, assetBrowser_, id, destinationVirtualFolder);
    if (moved) {
        console_.Info("Assets", "Asset moved to " + destinationVirtualFolder.generic_string());
    } else {
        console_.Error("Assets", AssetErrorOr(scene_->Assets().Manager(), "Asset move failed."));
    }
    return moved;
}

bool EditorSceneContext::MoveAssetFolderToFolder(const std::filesystem::path& sourceVirtualFolder, const std::filesystem::path& destinationVirtualFolder) {
    const bool moved = EditorSceneAssetBrowserCommands::MoveFolderToFolder(*scene_, assetBrowser_, sourceVirtualFolder, destinationVirtualFolder);
    if (moved) {
        console_.Info("Assets", "Folder moved to " + destinationVirtualFolder.generic_string());
    } else {
        console_.Error("Assets", AssetErrorOr(scene_->Assets().Manager(), "Folder move failed."));
    }
    return moved;
}

bool EditorSceneContext::CopyAssetToFolder(kb::assets::AssetId id, const std::filesystem::path& destinationVirtualFolder) {
    const bool copied = EditorSceneAssetBrowserCommands::CopyAssetToFolder(*scene_, assetBrowser_, id, destinationVirtualFolder);
    if (copied) {
        console_.Info("Assets", "Asset copied to " + destinationVirtualFolder.generic_string());
    } else {
        console_.Error("Assets", AssetErrorOr(scene_->Assets().Manager(), "Asset copy failed."));
    }
    return copied;
}

bool EditorSceneContext::CopyAssetFolderToFolder(const std::filesystem::path& sourceVirtualFolder, const std::filesystem::path& destinationVirtualFolder) {
    const bool copied = EditorSceneAssetBrowserCommands::CopyFolderToFolder(*scene_, assetBrowser_, sourceVirtualFolder, destinationVirtualFolder);
    if (copied) {
        console_.Info("Assets", "Folder copied to " + destinationVirtualFolder.generic_string());
    } else {
        console_.Error("Assets", AssetErrorOr(scene_->Assets().Manager(), "Folder copy failed."));
    }
    return copied;
}

bool EditorSceneContext::ImportAssetFiles(std::span<const std::filesystem::path> sourceFiles) {
    return ImportAssetFiles(sourceFiles, assetBrowser_.SelectedFolder());
}

bool EditorSceneContext::ImportAssetFiles(std::span<const std::filesystem::path> sourceFiles, const std::filesystem::path& destinationVirtualFolder) {
    const kb::assets::AssetImportResult report = EditorSceneAssetBrowserCommands::ImportFilesWithReport(*scene_, assetBrowser_, sourceFiles, destinationVirtualFolder);
    LogAssetImportReport(console_, report, destinationVirtualFolder);
    if (report.ImportedCount() == 0U) {
        console_.Error("Assets", AssetErrorOr(scene_->Assets().Manager(), "Asset import failed."));
    }
    return report.ImportedCount() > 0U;
}

bool EditorSceneContext::ToggleHierarchyRowExpanded(std::size_t rowIndex) {
    const std::vector<EditorHierarchyRow>& rows = HierarchyRows();
    if (rowIndex >= rows.size() || !rows[rowIndex].hasChildren) {
        return false;
    }

    hierarchyExpansion_.SetExpanded(rows[rowIndex].entity, !rows[rowIndex].expanded);
    InvalidateHierarchyRows();
    return true;
}

bool EditorSceneContext::ToggleEntityVisibility(kb::scene::SceneEntity entity) {
    if (!ExecuteSceneCommand("Toggle Visibility", [this, entity]() {
            return EditorSceneHierarchyActions::ToggleVisibility(*scene_, entity);
        })) {
        console_.Warning("Hierarchy", "Visibility toggle ignored for invalid entity.");
        return false;
    }
    SelectEntity(entity);
    return true;
}

kb::scene::SceneEntity EditorSceneContext::CreateHierarchyObject() {
    kb::scene::SceneEntity created{};
    if (ExecuteSceneCommand("Create Entity", [this, &created]() {
            created = EditorSceneHierarchyActions::CreateObject(*scene_);
            if (!created.IsValid()) {
                return false;
            }
            SelectEntity(created);
            return true;
        })) {
        console_.Info("Hierarchy", "Entity created.");
    }
    return created;
}

kb::scene::SceneEntity EditorSceneContext::CreateLightObject(kb::scene::LightKind kind) {
    const char* name = "Point Light";
    const char* label = "Create Point Light";
    switch (kind) {
    case kb::scene::LightKind::Directional:
        name = "Directional Light";
        label = "Create Directional Light";
        break;
    case kb::scene::LightKind::Spot:
        name = "Spot Light";
        label = "Create Spot Light";
        break;
    case kb::scene::LightKind::Point:
    default:
        break;
    }

    kb::scene::SceneEntity created{};
    if (ExecuteSceneCommand(label, [this, &created, kind, name]() {
            kb::scene::SceneObjectDesc desc{};
            desc.name = name;
            created = scene_->Entities().CreateEntity(std::move(desc));
            if (!created.IsValid()) {
                return false;
            }
            kb::scene::LightComponent light{};
            light.kind = kind;
            scene_->Components().Lights().Set(created, light);
            SelectEntity(created);
            return true;
        })) {
        console_.Info("Hierarchy", std::string{ name } + " created.");
    }
    return created;
}

bool EditorSceneContext::ReparentEntity(kb::scene::SceneEntity child, kb::scene::SceneEntity parent) {
    if (!child.IsValid() || !scene_->Entities().IsAlive(child)) {
        console_.Warning("Hierarchy", "Entity reparent ignored.");
        return false;
    }

    const bool moved = ExecuteSceneCommand("Reparent Entity", [this, child, parent]() {
        return EditorSceneHierarchyActions::Reparent(*scene_, child, parent);
    });
    if (moved) {
        SelectEntity(child);
        console_.Info("Hierarchy", "Entity reparented.");
    } else {
        console_.Warning("Hierarchy", "Entity reparent ignored.");
    }
    return moved;
}

bool EditorSceneContext::ReparentEntities(std::span<const kb::scene::SceneEntity> children, kb::scene::SceneEntity parent) {
    const std::vector<kb::scene::SceneEntity> moving = TopLevelSelectedEntities(*scene_, children);
    if (moving.empty()) {
        console_.Warning("Hierarchy", "Hierarchy reparent did not move any entity.");
        return false;
    }

    const std::span<const kb::scene::SceneEntity> movingSpan{ moving.data(), moving.size() };
    if (parent.IsValid() && (ContainsEntity(movingSpan, parent) || HasSelectedAncestor(*scene_, parent, movingSpan))) {
        console_.Warning("Hierarchy", "Cannot reparent an entity below itself or a selected descendant.");
        return false;
    }

    const bool moved = ExecuteSceneCommand("Reparent Entities", [this, moving, parent]() {
        bool anyMoved = false;
        for (const kb::scene::SceneEntity child : moving) {
            if (child == parent) {
                continue;
            }
            anyMoved = EditorSceneHierarchyActions::Reparent(*scene_, child, parent) || anyMoved;
        }
        return anyMoved;
    });
    if (moved) {
        const std::vector<EditorHierarchyRow>& rows = HierarchyRows();
        hierarchySelection_.Clear();
        bool first = true;
        for (const kb::scene::SceneEntity entity : children) {
            if (!scene_->Entities().IsAlive(entity)) {
                continue;
            }
            for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
                if (rows[rowIndex].entity != entity) {
                    continue;
                }
                static_cast<void>(hierarchySelection_.SelectRow(rows, rowIndex, !first, false));
                first = false;
                break;
            }
        }
        if (hierarchySelection_.SelectedEntities().empty() && !moving.empty()) {
            SelectEntity(moving.front());
        }
        assetBrowser_.ClearSelection();
        console_.Info("Hierarchy", "Hierarchy selection reparented.");
    } else {
        console_.Warning("Hierarchy", "Hierarchy reparent did not move any entity.");
    }
    return moved;
}

bool EditorSceneContext::CreatePrefabAsset(kb::scene::SceneEntity entity, const std::filesystem::path& path) {
    const bool created = EditorScenePrefabActions::CreateAsset(*scene_, entity, path);
    if (created) {
        InvalidateHierarchyRows();
        static_cast<void>(scene_->Assets().Discover());
        if (const std::optional<std::filesystem::path> virtualPath = scene_->Assets().Manager().Mounts().ToVirtual(path)) {
            if (const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().FindByPath(*virtualPath); metadata != nullptr) {
                static_cast<void>(assetBrowser_.SelectAsset(metadata->id, scene_->Assets().Manager()));
            }
        }
        console_.Info("Prefabs", "Prefab asset created: " + path.generic_string());
    } else {
        console_.Error("Prefabs", AssetErrorOr(scene_->Assets().Manager(), "Prefab asset creation failed."));
    }
    return created;
}

EditorInputActionAuthoring EditorSceneContext::InputActionAuthoring() noexcept {
    return EditorInputActionAuthoring{ *scene_, assetBrowser_, console_ };
}

EditorInputMappingContextAuthoring EditorSceneContext::InputMappingContextAuthoring() noexcept {
    return EditorInputMappingContextAuthoring{ *scene_, assetBrowser_, console_ };
}

EditorMaterialAssetAuthoring EditorSceneContext::MaterialAssetAuthoring() noexcept {
    return EditorMaterialAssetAuthoring{ *scene_, assetBrowser_, console_ };
}

bool EditorSceneContext::CreateInputActionAsset(const std::filesystem::path& virtualFolder) {
    return InputActionAuthoring().Create(virtualFolder);
}

bool EditorSceneContext::CreateInputAxisAsset(const std::filesystem::path& virtualFolder) {
    return InputActionAuthoring().CreateAxis(virtualFolder);
}

bool EditorSceneContext::CreateInputMappingContextAsset(const std::filesystem::path& virtualFolder) {
    return InputMappingContextAuthoring().Create(virtualFolder);
}

bool EditorSceneContext::CreateMaterialAsset(const std::filesystem::path& virtualFolder) {
    return MaterialAssetAuthoring().Create(virtualFolder);
}

bool EditorSceneContext::CreateMaterialFunctionAsset(const std::filesystem::path& virtualFolder) {
    return MaterialAssetAuthoring().CreateFunction(virtualFolder);
}

bool EditorSceneContext::CreateMaterialGraphAsset(const std::filesystem::path& virtualFolder) {
    return MaterialAssetAuthoring().CreateGraph(virtualFolder);
}

bool EditorSceneContext::CreateMaterialInstanceAsset(kb::assets::AssetId parentMaterial) {
    return MaterialAssetAuthoring().CreateInstance(parentMaterial);
}

bool EditorSceneContext::CreateMaterialTypeAsset(const std::filesystem::path& virtualFolder) {
    return MaterialAssetAuthoring().CreateMaterialType(virtualFolder);
}

bool EditorSceneContext::CreateMaterialFromGraphAsset(kb::assets::AssetId graphAssetId) {
    return MaterialAssetAuthoring().CreateMaterialFromGraph(graphAssetId);
}

bool EditorSceneContext::CreateMaterialFromMaterialTypeAsset(kb::assets::AssetId materialTypeAssetId) {
    return MaterialAssetAuthoring().CreateMaterialFromMaterialType(materialTypeAssetId);
}

bool EditorSceneContext::DuplicateMaterialAsset(kb::assets::AssetId materialAssetId) {
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(materialAssetId);
    if (metadata == nullptr || metadata->type != "RenderMaterial") {
        console_.Error("Materials", "Only Material assets can be duplicated to .kbmat.");
        return false;
    }

    EditorMaterialAssetGateway gateway{ *scene_, assetBrowser_ };
    const std::optional<std::filesystem::path> duplicatePath = gateway.DuplicateMaterial(materialAssetId);
    if (!duplicatePath.has_value()) {
        console_.Error("Materials", "Material duplicate could not be written: " + metadata->virtualPath.generic_string());
        return false;
    }

    console_.Info("Materials", "Material duplicated: " + duplicatePath->generic_string());
    return true;
}

bool EditorSceneContext::FindMaterialReferences(kb::assets::AssetId materialAssetId) {
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(materialAssetId);
    if (metadata == nullptr || (metadata->type != "RenderMaterial" && metadata->type != "RenderMaterialInstance")) {
        console_.Error("Materials", "Find References requires a material asset.");
        return false;
    }

    const std::vector<std::string> references = EditorMaterialReferenceFinder::FindSceneReferences(*scene_, materialAssetId);
    if (references.empty()) {
        console_.Info("Materials", "No scene references found for: " + metadata->virtualPath.generic_string());
        return true;
    }

    console_.Info("Materials", "References for " + metadata->virtualPath.generic_string() + ": " + std::to_string(references.size()));
    for (const std::string& reference : references) {
        console_.Info("Materials", "  " + reference);
    }
    return true;
}

bool EditorSceneContext::ExtractEmbeddedMaterials(kb::assets::AssetId meshAssetId) {
    EditorEmbeddedMaterialExtractor extractor{ *scene_, assetBrowser_, console_ };
    return extractor.Extract(meshAssetId).Succeeded();
}

bool EditorSceneContext::CreateLuaScriptAsset(const std::filesystem::path& virtualFolder) {
    EditorScriptAssetGateway gateway{ *scene_, assetBrowser_ };
    const std::optional<std::filesystem::path> path = gateway.CreateLuaScript(virtualFolder);
    if (!path.has_value()) {
        console_.Error("Scripts", "Lua script could not be created in folder: " + virtualFolder.generic_string());
        return false;
    }
    console_.Info("Scripts", "Lua script created: " + path->generic_string());
    return true;
}

bool EditorSceneContext::OpenLuaScript(kb::assets::AssetId id) {
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (metadata == nullptr) {
        console_.Error("Scripts", "Lua script metadata was not found.");
        return false;
    }
    std::filesystem::path path = metadata->physicalPath;
    if (const std::optional<std::filesystem::path> mounted = scene_->Assets().Manager().Mounts().Resolve(metadata->virtualPath)) {
        path = *mounted;
    }
    scriptEditor_.Open(path, id, metadata->virtualPath.filename().string());
    console_.Info("Scripts", "Opened script: " + metadata->virtualPath.generic_string());
    return true;
}

bool EditorSceneContext::OpenMaterialEditorAsset(kb::assets::AssetId id) {
    if (!id.IsValid()) {
        console_.Error("Materials", "No material asset was provided for the Material Editor.");
        return false;
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (metadata == nullptr || (metadata->type != "RenderMaterial" && metadata->type != "RenderMaterialInstance")) {
        console_.Error("Materials", "Selected asset is not a material document.");
        return false;
    }
    if (!PrepareMaterialAssetSelectionChange(id)) {
        return false;
    }
    ClearMaterialEditorWorkingCopyRuntimePreview();
    std::optional<kb::render::RenderMaterialInstanceAssetData> instanceDocument =
        metadata->type == "RenderMaterialInstance" ? ReadMaterialInstanceAsset(id) : std::nullopt;
    std::optional<kb::render::RenderMaterialAssetData> materialDocument =
        instanceDocument.has_value() && instanceDocument->parentMaterialAssetId.IsValid()
            ? ReadEffectiveMaterialAsset(instanceDocument->parentMaterialAssetId)
            : ReadMaterialAsset(id);
    std::optional<kb::render::RenderMaterialAssetData> refreshedMaterialDocument;
    std::optional<kb::render::RenderMaterialTypeSchema> schema;
    std::vector<std::string> refreshDiagnostics;
    std::vector<std::string> materialTypeDiagnostics;
    bool materialTypeDiagnosticsHaveError = false;
    if (materialDocument.has_value()) {
        const std::optional<kb::render::RenderMaterialTypeDocument> materialType =
            LoadMaterialTypeDocumentForMaterial(scene_->Assets().Manager(), *materialDocument);
        if (materialType.has_value() && materialType->stableTypeId == materialDocument->materialType) {
            if (materialType->version != materialDocument->materialTypeVersion) {
                kb::render::RenderMaterialSchemaRefreshResult refreshed =
                    kb::render::RefreshRenderMaterialGraphBackedMaterialSchema(*materialDocument, *materialType);
                refreshDiagnostics.reserve(refreshed.diagnostics.size());
                for (const kb::render::RenderMaterialSchemaRefreshDiagnostic& diagnostic : refreshed.diagnostics) {
                    refreshDiagnostics.push_back(MaterialSchemaRefreshDiagnosticLine(diagnostic));
                    console_.Warning("Materials", refreshDiagnostics.back());
                }
                refreshedMaterialDocument = std::move(refreshed.material);
            }
            schema = materialType->schema;
        } else {
            schema = MaterialEditorSchemaForMaterial(scene_->Assets().Manager(), *materialDocument);
        }
        AppendMaterialTypeReferenceValidationDiagnostics(
            scene_->Assets().Manager(),
            *metadata,
            refreshedMaterialDocument.has_value() ? *refreshedMaterialDocument : *materialDocument,
            materialTypeDiagnostics,
            materialTypeDiagnosticsHaveError);
    }
    materialEditor_.Open(id, std::move(materialDocument), std::move(schema), std::move(instanceDocument));
    if (refreshedMaterialDocument.has_value()) {
        materialEditor_.SetWorkingCopy(std::move(*refreshedMaterialDocument));
        MarkSceneRenderDirty();
    }
    if (!refreshDiagnostics.empty()) {
        materialEditor_.SetDiagnostics(std::move(refreshDiagnostics), false);
    }
    if (!materialTypeDiagnostics.empty()) {
        for (const std::string& diagnostic : materialTypeDiagnostics) {
            console_.Error("Materials", diagnostic);
        }
        materialEditor_.SetDiagnostics(std::move(materialTypeDiagnostics), materialTypeDiagnosticsHaveError);
    }
    console_.Info("Materials", "Opened Material Editor: " + metadata->virtualPath.generic_string());
    return true;
}

bool EditorSceneContext::OpenMaterialEditorGraphSourceAsset(kb::assets::AssetId id) {
    const std::optional<kb::render::RenderMaterialAssetData> material =
        materialEditor_.OpenAssetId() == id && materialEditor_.WorkingCopy().has_value()
            ? materialEditor_.WorkingCopy()
            : ReadMaterialDocumentAsset(id);
    if (!material.has_value()) {
        console_.Error("Materials", "Open Graph requires an open material document.");
        return false;
    }

    const kb::assets::AssetManager& manager = scene_->Assets().Manager();
    const kb::assets::AssetMetadata* graph = ResolveTypedAssetReference(
        manager,
        material->graphSourceAssetId,
        material->graphSourceAssetPath,
        kb::render::kRenderMaterialGraphAssetType);
    if (graph == nullptr) {
        console_.Warning("Materials", "This material has no linked source Material Graph asset.");
        return false;
    }

    static_cast<void>(assetBrowser_.SelectAsset(graph->id, manager));
    assetBrowser_.FocusSelection(true);
    console_.Info("Materials", "Selected source Material Graph: " + graph->virtualPath.generic_string());
    return true;
}

bool EditorSceneContext::OpenMaterialEditorMaterialTypeAsset(kb::assets::AssetId id) {
    const std::optional<kb::render::RenderMaterialAssetData> material =
        materialEditor_.OpenAssetId() == id && materialEditor_.WorkingCopy().has_value()
            ? materialEditor_.WorkingCopy()
            : ReadMaterialDocumentAsset(id);
    if (!material.has_value()) {
        console_.Error("Materials", "Open Material Type requires an open material document.");
        return false;
    }

    const kb::assets::AssetManager& manager = scene_->Assets().Manager();
    const kb::assets::AssetMetadata* materialType = ResolveTypedAssetReference(
        manager,
        material->materialTypeAssetId,
        material->materialTypeAssetPath,
        kb::render::kRenderMaterialTypeAssetType);
    if (materialType == nullptr) {
        console_.Warning("Materials", "This material uses a built-in or missing Material Type asset.");
        return false;
    }

    static_cast<void>(assetBrowser_.SelectAsset(materialType->id, manager));
    assetBrowser_.FocusSelection(true);
    console_.Info("Materials", "Selected Material Type: " + materialType->virtualPath.generic_string());
    return true;
}

void EditorSceneContext::CloseMaterialEditorAsset() noexcept {
    try {
        CancelMaterialGraphWorkingCopyTransaction();
        ClearMaterialEditorWorkingCopyRuntimePreview();
        MarkSceneRenderDirty();
    } catch (...) {
    }
    materialEditor_.Close();
}

bool EditorSceneContext::HasDirtyMaterialAssetEdit() const noexcept {
    if (HasActiveMaterialAssetEdit()) {
        return true;
    }
    if (materialEditor_.Dirty()) {
        return true;
    }
    if (!inspector_.IsTextEditDirty() || !IsMaterialFloatProperty(inspector_.EditedProperty())) {
        return false;
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(materialEditor_.OpenAssetId());
    return metadata != nullptr && metadata->type == "RenderMaterial";
}

bool EditorSceneContext::PrepareMaterialAssetSelectionChange(kb::assets::AssetId nextAsset) {
    if (!HasDirtyMaterialAssetEdit() || nextAsset == materialEditor_.OpenAssetId()) {
        return true;
    }
    console_.Warning("Materials", "Unsaved material value edit. Press Enter to save it or Escape to discard it before selecting another asset.");
    return false;
}

bool EditorSceneContext::PrepareMaterialEditorClose(std::string_view reason) {
    if (!HasDirtyMaterialAssetEdit()) {
        return true;
    }
    console_.Warning(
        "Materials",
        "Unsaved material value edit before " + std::string{ reason } + ". Press Save to commit it or Revert to discard it.");
    return false;
}

std::optional<kb::input::InputActionAsset> EditorSceneContext::ReadInputActionAsset(kb::assets::AssetId id) const {
    return EditorInputAssetGateway::ReadAction(*scene_, id);
}

bool EditorSceneContext::SetInputActionName(kb::assets::AssetId id, std::string name) {
    return InputActionAuthoring().SetName(id, std::move(name));
}

bool EditorSceneContext::CycleInputActionValueType(kb::assets::AssetId id) {
    return InputActionAuthoring().CycleValueType(id);
}

bool EditorSceneContext::SetInputActionValueType(kb::assets::AssetId id, kb::input::InputActionValueType valueType) {
    return InputActionAuthoring().SetValueType(id, valueType);
}

bool EditorSceneContext::ToggleInputActionConsume(kb::assets::AssetId id) {
    return InputActionAuthoring().ToggleConsume(id);
}

std::optional<kb::render::RenderMaterialAssetData> EditorSceneContext::ReadMaterialAsset(kb::assets::AssetId id) const {
    return EditorMaterialAssetGateway::Read(*scene_, id);
}

std::optional<kb::render::RenderMaterialInstanceAssetData> EditorSceneContext::ReadMaterialInstanceAsset(kb::assets::AssetId id) const {
    const kb::assets::AssetManager& manager = scene_->Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr || metadata->type != "RenderMaterialInstance") {
        return std::nullopt;
    }
    const std::filesystem::path path = ResolveAssetPath(manager, *metadata);
    if (path.empty()) {
        return std::nullopt;
    }
    return kb::render::RenderMaterialInstanceAssetLoader::LoadInstance(path);
}

std::optional<kb::render::RenderMaterialAssetData> EditorSceneContext::ReadEffectiveMaterialAsset(kb::assets::AssetId id) const {
    const kb::assets::AssetManager& manager = scene_->Assets().Manager();
    std::unordered_set<std::uint64_t> visited;
    std::vector<kb::render::RenderMaterialInstanceAssetData> chain;
    kb::assets::AssetId current = id;
    while (current.IsValid()) {
        if (!visited.insert(current.value).second) {
            return std::nullopt;
        }
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(current);
        if (metadata == nullptr) {
            return std::nullopt;
        }
        if (metadata->type == "RenderMaterial") {
            std::optional<kb::render::RenderMaterialAssetData> material = ReadMaterialAsset(current);
            if (!material.has_value()) {
                return std::nullopt;
            }
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                material = kb::render::BuildEffectiveRenderMaterialInstanceAsset(*material, *it);
            }
            return material;
        }
        if (metadata->type != "RenderMaterialInstance") {
            return std::nullopt;
        }
        std::optional<kb::render::RenderMaterialInstanceAssetData> instance = ReadMaterialInstanceAsset(current);
        if (!instance.has_value() || !instance->parentMaterialAssetId.IsValid()) {
            return std::nullopt;
        }
        current = instance->parentMaterialAssetId;
        chain.push_back(std::move(*instance));
    }
    return std::nullopt;
}

std::optional<kb::render::RenderMaterialAssetData> EditorSceneContext::ReadMaterialDocumentAsset(kb::assets::AssetId id) const {
    const kb::assets::AssetManager& manager = scene_->Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr) {
        return std::nullopt;
    }
    if (metadata->type == "RenderMaterial") {
        return ReadMaterialAsset(id);
    }
    if (metadata->type != "RenderMaterialInstance") {
        return std::nullopt;
    }

    return ReadEffectiveMaterialAsset(id);
}

const kb::scene::Scene& EditorSceneContext::MaterialPreviewScene(kb::assets::AssetId id) {
    const kb::render::RenderMaterialAssetData* workingCopy = nullptr;
    if (materialEditor_.OpenAssetId() == id && materialEditor_.WorkingCopy().has_value()) {
        workingCopy = &*materialEditor_.WorkingCopy();
    }
    return materialPreviewScene_->SceneFor(*scene_, id, workingCopy);
}

const EditorMaterialPreviewTelemetry& EditorSceneContext::MaterialPreviewTelemetry() const noexcept {
    return materialPreviewScene_->Telemetry();
}

std::uint64_t EditorSceneContext::MaterialPreviewRevision() const noexcept {
    return materialPreviewScene_->Revision();
}

const std::string& EditorSceneContext::GraphShaderCacheRoot() const noexcept {
    return graphShaderCacheRoot_;
}

EditorMaterialGraphCookService& EditorSceneContext::MaterialGraphCookService() noexcept {
    return *materialGraphCookService_;
}

EditorMaterialGraphCookResult EditorSceneContext::OpenMaterialGraphCookResult() const {
    const kb::assets::AssetId openAsset = materialEditor_.OpenAssetId();
    if (materialGraphCookService_ == nullptr || !openAsset.IsValid()) {
        EditorMaterialGraphCookResult idle{};
        idle.materialAssetId = openAsset;
        idle.status = EditorMaterialGraphCookStatus::Idle;
        return idle;
    }
    return materialGraphCookService_->LatestResult(openAsset);
}

std::size_t EditorSceneContext::PumpMaterialGraphCookResults() {
    if (materialGraphCookService_ == nullptr) {
        return 0U;
    }
    // Batch-cook scene materials once per scene load, deferred to here so the active renderer backend
    // (resolved from live bgfx) is known before cooking (MAT-84).
    if (sceneGraphCookPending_) {
        sceneGraphCookPending_ = false;
        CookSceneGraphMaterials();
    }
    const std::vector<EditorMaterialGraphCookResult> results = materialGraphCookService_->DrainResults();
    for (const EditorMaterialGraphCookResult& result : results) {
        if (result.status == EditorMaterialGraphCookStatus::Failed) {
            for (const std::string& diagnostic : result.diagnostics) {
                console_.Warning("Materials", "Graph shader cook: " + diagnostic);
            }
        } else if (result.status == EditorMaterialGraphCookStatus::CookUnavailable && !result.diagnostics.empty()) {
            console_.Warning("Materials", "Graph shader cook: " + result.diagnostics.front());
        }
    }
    // A freshly cooked program is picked up by the renderer's MaterialProgramRegistry on the next
    // frame via the shared cache root + runtime asset reload; surface that the preview must refresh.
    if (!results.empty()) {
        MarkSceneRenderDirty();
    }
    return results.size();
}

std::uint32_t EditorSceneContext::SelectedMaterialGraphNodeId() const noexcept {
    return materialEditor_.SelectedNodeId();
}

const std::vector<std::uint32_t>& EditorSceneContext::SelectedMaterialGraphNodeIds() const noexcept {
    return materialEditor_.SelectedNodeIds();
}

bool EditorSceneContext::IsMaterialGraphNodeSelected(std::uint32_t nodeId) const noexcept {
    return materialEditor_.IsNodeSelected(nodeId);
}

bool EditorSceneContext::SelectMaterialGraphNode(std::uint32_t nodeId) {
    return materialEditor_.SelectNode(nodeId);
}

bool EditorSceneContext::SelectMaterialGraphNode(std::uint32_t nodeId, bool additive, bool toggle) {
    if (toggle) {
        return materialEditor_.ToggleNodeSelection(nodeId);
    }
    if (additive) {
        return materialEditor_.AddNodeToSelection(nodeId);
    }
    return materialEditor_.SelectNode(nodeId);
}

bool EditorSceneContext::SetMaterialGraphNodeSelection(std::vector<std::uint32_t> nodeIds, std::uint32_t primaryNodeId) {
    return materialEditor_.SetNodeSelection(std::move(nodeIds), primaryNodeId);
}

bool EditorSceneContext::ClearMaterialGraphNodeSelection() {
    return materialEditor_.ClearNodeSelection();
}

std::uint32_t EditorSceneContext::SelectedMaterialGraphCommentId() const noexcept {
    return materialEditor_.SelectedCommentId();
}

bool EditorSceneContext::IsMaterialGraphCommentSelected(std::uint32_t commentId) const noexcept {
    return materialEditor_.IsCommentSelected(commentId);
}

bool EditorSceneContext::SelectMaterialGraphComment(std::uint32_t commentId) {
    return materialEditor_.SelectComment(commentId);
}

bool EditorSceneContext::ClearMaterialGraphCommentSelection() {
    return materialEditor_.ClearCommentSelection();
}

void EditorSceneContext::FocusMaterialGraph(bool focused) noexcept {
    materialGraphFocused_ = focused;
}

bool EditorSceneContext::IsMaterialGraphFocused() const noexcept {
    return materialGraphFocused_;
}

float EditorSceneContext::MaterialGraphZoom() const noexcept {
    return materialGraphZoom_;
}

int EditorSceneContext::MaterialGraphPanX() const noexcept {
    return materialGraphPanX_;
}

int EditorSceneContext::MaterialGraphPanY() const noexcept {
    return materialGraphPanY_;
}

bool EditorSceneContext::ZoomMaterialGraph(int wheelDelta) noexcept {
    return ZoomMaterialGraph(wheelDelta, 0, 0);
}

bool EditorSceneContext::ZoomMaterialGraph(int wheelDelta, int focusCanvasX, int focusCanvasY) noexcept {
    constexpr float minZoom = 0.45F;
    constexpr float maxZoom = 1.60F;
    const float step = wheelDelta > 0 ? 1.10F : 0.90F;
    const float previousZoom = materialGraphZoom_;
    const float zoom = std::clamp(previousZoom * step, minZoom, maxZoom);
    if (std::fabs(zoom - previousZoom) < 0.0001F) {
        return false;
    }
    const float graphFocusX = (static_cast<float>(focusCanvasX - materialGraphPanX_) / std::max(0.1F, previousZoom));
    const float graphFocusY = (static_cast<float>(focusCanvasY - materialGraphPanY_) / std::max(0.1F, previousZoom));
    materialGraphZoom_ = zoom;
    materialGraphPanX_ = focusCanvasX - static_cast<int>(std::lround(graphFocusX * zoom));
    materialGraphPanY_ = focusCanvasY - static_cast<int>(std::lround(graphFocusY * zoom));
    return true;
}

void EditorSceneContext::SetMaterialEditorFindQuery(std::string query) {
    materialEditor_.SetFindQuery(std::move(query));
}

bool EditorSceneContext::FocusMaterialEditorFindResult(std::size_t resultIndex, int canvasWidth, int canvasHeight) {
    const std::optional<MaterialEditorFindFocusTarget> target = materialEditor_.FindResultFocusTarget(resultIndex);
    if (!target.has_value()) {
        return false;
    }
    if (!materialEditor_.FocusFindResult(resultIndex)) {
        return false;
    }
    materialGraphPanX_ = (canvasWidth / 2) - static_cast<int>(std::lround(static_cast<float>(target->graphX) * materialGraphZoom_));
    materialGraphPanY_ = (canvasHeight / 2) - static_cast<int>(std::lround(static_cast<float>(target->graphY) * materialGraphZoom_));
    materialGraphFocused_ = true;
    return true;
}

int EditorSceneContext::MaterialGraphNodeOffsetX(kb::assets::AssetId assetId, std::uint32_t nodeId) const noexcept {
    static_cast<void>(assetId);
    static_cast<void>(nodeId);
    return 0;
}

int EditorSceneContext::MaterialGraphNodeOffsetY(kb::assets::AssetId assetId, std::uint32_t nodeId) const noexcept {
    static_cast<void>(assetId);
    static_cast<void>(nodeId);
    return 0;
}

bool EditorSceneContext::BeginMaterialGraphNodeDrag(kb::assets::AssetId assetId, std::uint32_t nodeId, int x, int y) {
    if (!assetId.IsValid() || nodeId == 0U || materialEditor_.OpenAssetId() != assetId || !materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    const std::optional<std::pair<std::int32_t, std::int32_t>> position = materialEditor_.GraphNodePosition(nodeId);
    if (!position.has_value()) {
        return false;
    }
    materialGraphDragAssetId_ = assetId;
    materialGraphDragNodeId_ = nodeId;
    materialGraphDragStartX_ = x;
    materialGraphDragStartY_ = y;
    materialGraphDragStartOffsetX_ = 0;
    materialGraphDragStartOffsetY_ = 0;
    materialGraphDragStartNodeX_ = position->first;
    materialGraphDragStartNodeY_ = position->second;
    materialGraphDragStartDocument_ = materialEditor_.WorkingCopy();
    materialGraphDragStartSelectedNodeId_ = materialEditor_.SelectedNodeId();
    materialGraphDragStartSelectedNodeIds_ = materialEditor_.SelectedNodeIds();
    materialGraphDragStartNodes_.clear();
    const std::vector<std::uint32_t>& selectedNodeIds = materialEditor_.SelectedNodeIds();
    const bool dragSelection = materialEditor_.IsNodeSelected(nodeId) && selectedNodeIds.size() > 1U;
    if (dragSelection) {
        materialGraphDragStartNodes_.reserve(selectedNodeIds.size());
        for (std::uint32_t selectedNodeId : selectedNodeIds) {
            if (const std::optional<std::pair<std::int32_t, std::int32_t>> selectedPosition = materialEditor_.GraphNodePosition(selectedNodeId)) {
                materialGraphDragStartNodes_.push_back(MaterialGraphDragNodeStart{
                    .nodeId = selectedNodeId,
                    .positionX = selectedPosition->first,
                    .positionY = selectedPosition->second,
                });
            }
        }
    }
    if (materialGraphDragStartNodes_.empty()) {
        materialGraphDragStartNodes_.push_back(MaterialGraphDragNodeStart{
            .nodeId = nodeId,
            .positionX = position->first,
            .positionY = position->second,
        });
    }
    materialGraphDragChanged_ = false;
    materialGraphNodeDragging_ = true;
    return true;
}

bool EditorSceneContext::DragMaterialGraphNode(int x, int y) {
    if (!materialGraphNodeDragging_ || !materialGraphDragAssetId_.IsValid() || materialGraphDragNodeId_ == 0U) {
        return false;
    }
    const int deltaX = static_cast<int>(std::lround(static_cast<float>(x - materialGraphDragStartX_) / std::max(0.1F, materialGraphZoom_)));
    const int deltaY = static_cast<int>(std::lround(static_cast<float>(y - materialGraphDragStartY_) / std::max(0.1F, materialGraphZoom_)));
    std::vector<std::pair<std::uint32_t, std::pair<std::int32_t, std::int32_t>>> positions;
    positions.reserve(materialGraphDragStartNodes_.size());
    for (const MaterialGraphDragNodeStart& start : materialGraphDragStartNodes_) {
        positions.push_back({
            start.nodeId,
            {
                static_cast<std::int32_t>(start.positionX + deltaX),
                static_cast<std::int32_t>(start.positionY + deltaY),
            },
        });
    }
    if (!materialEditor_.MoveGraphNodes(positions)) {
        return false;
    }
    materialGraphDragChanged_ = true;
    materialEditor_.ClearDiagnostics();
    return true;
}

bool EditorSceneContext::EndMaterialGraphNodeDrag() {
    if (!materialGraphNodeDragging_) {
        return false;
    }
    const bool shouldRecord = materialGraphDragChanged_ && materialGraphDragStartDocument_.has_value();
    const kb::assets::AssetId assetId = materialGraphDragAssetId_;
    std::optional<kb::render::RenderMaterialAssetData> before = std::move(materialGraphDragStartDocument_);
    const std::uint32_t beforeSelectedNodeId = materialGraphDragStartSelectedNodeId_;
    std::vector<std::uint32_t> beforeSelectedNodeIds = std::move(materialGraphDragStartSelectedNodeIds_);
    materialGraphNodeDragging_ = false;
    materialGraphDragAssetId_ = {};
    materialGraphDragNodeId_ = 0U;
    materialGraphDragStartNodeX_ = 0;
    materialGraphDragStartNodeY_ = 0;
    materialGraphDragStartSelectedNodeId_ = 0U;
    materialGraphDragStartSelectedNodeIds_.clear();
    materialGraphDragStartNodes_.clear();
    materialGraphDragChanged_ = false;
    if (shouldRecord) {
        return RecordMaterialGraphWorkingCopyEdit(assetId, "Move Material Graph Node", std::move(*before), beforeSelectedNodeId, std::move(beforeSelectedNodeIds));
    }
    return true;
}

bool EditorSceneContext::IsMaterialGraphNodeDragging() const noexcept {
    return materialGraphNodeDragging_;
}

bool EditorSceneContext::BeginMaterialGraphCommentDrag(kb::assets::AssetId assetId, std::uint32_t commentId, int x, int y) {
    if (!assetId.IsValid() || commentId == 0U || materialEditor_.OpenAssetId() != assetId || !materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    const std::optional<std::pair<std::int32_t, std::int32_t>> position = materialEditor_.GraphCommentPosition(commentId);
    if (!position.has_value()) {
        return false;
    }
    materialGraphCommentDragAssetId_ = assetId;
    materialGraphCommentDragId_ = commentId;
    materialGraphCommentDragStartX_ = x;
    materialGraphCommentDragStartY_ = y;
    materialGraphCommentDragStartCommentX_ = position->first;
    materialGraphCommentDragStartCommentY_ = position->second;
    materialGraphCommentDragStartDocument_ = materialEditor_.WorkingCopy();
    materialGraphCommentDragStartSelectedNodeId_ = materialEditor_.SelectedNodeId();
    materialGraphCommentDragStartSelectedNodeIds_ = materialEditor_.SelectedNodeIds();
    materialGraphCommentDragStartSelectedCommentId_ = materialEditor_.SelectedCommentId();
    materialGraphCommentDragChanged_ = false;
    materialGraphCommentDragging_ = true;
    return true;
}

bool EditorSceneContext::DragMaterialGraphComment(int x, int y) {
    if (!materialGraphCommentDragging_ || !materialGraphCommentDragAssetId_.IsValid() || materialGraphCommentDragId_ == 0U) {
        return false;
    }
    const int deltaX = static_cast<int>(std::lround(static_cast<float>(x - materialGraphCommentDragStartX_) / std::max(0.1F, materialGraphZoom_)));
    const int deltaY = static_cast<int>(std::lround(static_cast<float>(y - materialGraphCommentDragStartY_) / std::max(0.1F, materialGraphZoom_)));
    if (!materialEditor_.MoveGraphCommentGroup(
            materialGraphCommentDragId_,
            static_cast<std::int32_t>(materialGraphCommentDragStartCommentX_ + deltaX),
            static_cast<std::int32_t>(materialGraphCommentDragStartCommentY_ + deltaY))) {
        return false;
    }
    materialGraphCommentDragChanged_ = true;
    materialEditor_.ClearDiagnostics();
    return true;
}

bool EditorSceneContext::EndMaterialGraphCommentDrag() {
    if (!materialGraphCommentDragging_) {
        return false;
    }
    const bool shouldRecord = materialGraphCommentDragChanged_ && materialGraphCommentDragStartDocument_.has_value();
    const kb::assets::AssetId assetId = materialGraphCommentDragAssetId_;
    std::optional<kb::render::RenderMaterialAssetData> before = std::move(materialGraphCommentDragStartDocument_);
    const std::uint32_t beforeSelectedNodeId = materialGraphCommentDragStartSelectedNodeId_;
    std::vector<std::uint32_t> beforeSelectedNodeIds = std::move(materialGraphCommentDragStartSelectedNodeIds_);
    const std::uint32_t beforeSelectedCommentId = materialGraphCommentDragStartSelectedCommentId_;
    materialGraphCommentDragging_ = false;
    materialGraphCommentDragAssetId_ = {};
    materialGraphCommentDragId_ = 0U;
    materialGraphCommentDragStartX_ = 0;
    materialGraphCommentDragStartY_ = 0;
    materialGraphCommentDragStartCommentX_ = 0;
    materialGraphCommentDragStartCommentY_ = 0;
    materialGraphCommentDragStartSelectedNodeId_ = 0U;
    materialGraphCommentDragStartSelectedNodeIds_.clear();
    materialGraphCommentDragStartSelectedCommentId_ = 0U;
    materialGraphCommentDragChanged_ = false;
    if (shouldRecord) {
        return RecordMaterialGraphWorkingCopyEdit(
            assetId,
            "Move Material Graph Comment",
            std::move(*before),
            beforeSelectedNodeId,
            std::move(beforeSelectedNodeIds),
            beforeSelectedCommentId);
    }
    return true;
}

bool EditorSceneContext::IsMaterialGraphCommentDragging() const noexcept {
    return materialGraphCommentDragging_;
}

bool EditorSceneContext::BeginMaterialGraphBoxSelection(kb::assets::AssetId assetId, int x, int y, bool additive) noexcept {
    if (!assetId.IsValid() || materialEditor_.OpenAssetId() != assetId || !materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    materialGraphBoxSelectionAssetId_ = assetId;
    materialGraphBoxSelectionStartX_ = x;
    materialGraphBoxSelectionStartY_ = y;
    materialGraphBoxSelectionCurrentX_ = x;
    materialGraphBoxSelectionCurrentY_ = y;
    materialGraphBoxSelectionAdditive_ = additive;
    materialGraphBoxSelecting_ = true;
    return true;
}

bool EditorSceneContext::DragMaterialGraphBoxSelection(int x, int y) noexcept {
    if (!materialGraphBoxSelecting_) {
        return false;
    }
    if (materialGraphBoxSelectionCurrentX_ == x && materialGraphBoxSelectionCurrentY_ == y) {
        return false;
    }
    materialGraphBoxSelectionCurrentX_ = x;
    materialGraphBoxSelectionCurrentY_ = y;
    return true;
}

bool EditorSceneContext::EndMaterialGraphBoxSelection(std::vector<std::uint32_t> nodeIds, std::uint32_t primaryNodeId) {
    if (!materialGraphBoxSelecting_) {
        return false;
    }
    const bool additive = materialGraphBoxSelectionAdditive_;
    materialGraphBoxSelectionAssetId_ = {};
    materialGraphBoxSelecting_ = false;
    materialGraphBoxSelectionAdditive_ = false;
    if (additive) {
        std::vector<std::uint32_t> merged = materialEditor_.SelectedNodeIds();
        for (std::uint32_t nodeId : nodeIds) {
            if (nodeId != 0U && std::ranges::find(merged, nodeId) == merged.end()) {
                merged.push_back(nodeId);
            }
        }
        if (primaryNodeId == 0U && !nodeIds.empty()) {
            primaryNodeId = nodeIds.back();
        }
        return materialEditor_.SetNodeSelection(std::move(merged), primaryNodeId == 0U ? materialEditor_.SelectedNodeId() : primaryNodeId);
    }
    return materialEditor_.SetNodeSelection(std::move(nodeIds), primaryNodeId);
}

bool EditorSceneContext::IsMaterialGraphBoxSelecting() const noexcept {
    return materialGraphBoxSelecting_;
}

bool EditorSceneContext::MaterialGraphBoxSelectionAdditive() const noexcept {
    return materialGraphBoxSelectionAdditive_;
}

int EditorSceneContext::MaterialGraphBoxSelectionStartX() const noexcept {
    return materialGraphBoxSelectionStartX_;
}

int EditorSceneContext::MaterialGraphBoxSelectionStartY() const noexcept {
    return materialGraphBoxSelectionStartY_;
}

int EditorSceneContext::MaterialGraphBoxSelectionCurrentX() const noexcept {
    return materialGraphBoxSelectionCurrentX_;
}

int EditorSceneContext::MaterialGraphBoxSelectionCurrentY() const noexcept {
    return materialGraphBoxSelectionCurrentY_;
}

bool EditorSceneContext::BeginMaterialGraphPan(int x, int y) noexcept {
    materialGraphPanStartX_ = x;
    materialGraphPanStartY_ = y;
    materialGraphPanStartOffsetX_ = materialGraphPanX_;
    materialGraphPanStartOffsetY_ = materialGraphPanY_;
    materialGraphPanning_ = true;
    materialGraphPanMoved_ = false;
    return true;
}

bool EditorSceneContext::DragMaterialGraphPan(int x, int y) noexcept {
    if (!materialGraphPanning_) {
        return false;
    }
    const int newPanX = materialGraphPanStartOffsetX_ + (x - materialGraphPanStartX_);
    const int newPanY = materialGraphPanStartOffsetY_ + (y - materialGraphPanStartY_);
    if (newPanX == materialGraphPanX_ && newPanY == materialGraphPanY_) {
        return false;
    }
    materialGraphPanMoved_ = true;
    materialGraphPanX_ = newPanX;
    materialGraphPanY_ = newPanY;
    return true;
}

bool EditorSceneContext::EndMaterialGraphPan() noexcept {
    if (!materialGraphPanning_) {
        return false;
    }
    materialGraphPanning_ = false;
    return true;
}

bool EditorSceneContext::IsMaterialGraphPanning() const noexcept {
    return materialGraphPanning_;
}

bool EditorSceneContext::HasMaterialGraphPanMoved() const noexcept {
    return materialGraphPanMoved_;
}

bool EditorSceneContext::AddMaterialGraphNode(
    kb::assets::AssetId id,
    kb::render::RenderMaterialGraphNodeKind kind,
    int graphX,
    int graphY) {
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (metadata == nullptr || metadata->type != "RenderMaterial" || materialEditor_.OpenAssetId() != id) {
        console_.Error("Materials", "Graph nodes can only be edited on an open Material asset.");
        return false;
    }
    if (!materialEditor_.WorkingCopy().has_value()) {
        console_.Warning("Materials", "Material graph working copy is not available.");
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    std::vector<std::uint32_t> beforeSelectedNodeIds = materialEditor_.SelectedNodeIds();
    const std::uint32_t beforeSelectedCommentId = materialEditor_.SelectedCommentId();
    std::uint32_t nodeId = 0U;
    if (!materialEditor_.AddGraphNode(kind, graphX, graphY, &nodeId)) {
        console_.Warning("Materials", "Material graph node could not be created.");
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Create Material Graph Node", std::move(before), beforeSelectedNodeId, std::move(beforeSelectedNodeIds), beforeSelectedCommentId)) {
        console_.Warning("Materials", "Material graph node creation could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Created material graph node #" + std::to_string(nodeId) + ".");
    return true;
}

bool EditorSceneContext::AddMaterialGraphNodeForPendingConnection(kb::assets::AssetId id, MaterialEditorGraphMenuCommand command, int graphX, int graphY) {
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (metadata == nullptr || metadata->type != "RenderMaterial" || materialEditor_.OpenAssetId() != id ||
        materialGraphPendingConnectionAssetId_ != id || materialGraphPendingConnectionNodeId_ == 0U ||
        materialGraphPendingConnectionPin_.empty()) {
        return false;
    }
    if (!materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    const std::optional<kb::render::RenderMaterialGraphNodeKind> kind = MaterialEditorGraphMenuCommandNodeKind(command);
    if (!kind.has_value()) {
        return false;
    }
    const std::optional<std::string> newNodePin = MaterialEditorGraphCompatibleCommandPin(
        materialEditor_.WorkingCopy()->graph,
        materialGraphPendingConnectionNodeId_,
        materialGraphPendingConnectionPin_,
        materialGraphPendingConnectionOutput_,
        command);
    if (!newNodePin.has_value()) {
        console_.Warning("Materials", "Material graph node is not compatible with the dragged pin.");
        return false;
    }

    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    std::vector<std::uint32_t> beforeSelectedNodeIds = materialEditor_.SelectedNodeIds();
    const std::uint32_t beforeSelectedCommentId = materialEditor_.SelectedCommentId();
    const std::uint32_t sourceNodeId = materialGraphPendingConnectionNodeId_;
    const std::string sourcePin = materialGraphPendingConnectionPin_;
    const bool sourceOutput = materialGraphPendingConnectionOutput_;
    const bool ownsTransaction = materialGraphPendingConnectionOwnsTransaction_;

    std::uint32_t nodeId = 0U;
    if (!materialEditor_.AddGraphNode(*kind, graphX, graphY, &nodeId)) {
        console_.Warning("Materials", "Material graph node could not be created.");
        if (ownsTransaction) {
            CancelMaterialGraphWorkingCopyTransaction();
        }
        return false;
    }
    const bool connected = sourceOutput
        ? materialEditor_.ConnectGraphPins(sourceNodeId, sourcePin, nodeId, *newNodePin)
        : materialEditor_.ConnectGraphPins(nodeId, *newNodePin, sourceNodeId, sourcePin);
    if (!connected) {
        materialEditor_.SetWorkingCopy(std::move(before));
        static_cast<void>(materialEditor_.SetNodeSelection(std::move(beforeSelectedNodeIds), beforeSelectedNodeId));
        if (beforeSelectedCommentId != 0U) {
            static_cast<void>(materialEditor_.SelectComment(beforeSelectedCommentId));
        }
        if (ownsTransaction) {
            CancelMaterialGraphWorkingCopyTransaction();
        }
        console_.Warning("Materials", "Material graph node could not be connected to the dragged pin.");
        return false;
    }

    ClearMaterialGraphPinConnectionState();
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Create And Connect Material Graph Node", std::move(before), beforeSelectedNodeId, std::move(beforeSelectedNodeIds), beforeSelectedCommentId)) {
        console_.Warning("Materials", "Material graph node creation could not be recorded.");
        if (ownsTransaction) {
            CancelMaterialGraphWorkingCopyTransaction();
        }
        return false;
    }
    if (ownsTransaction && !CommitMaterialGraphWorkingCopyTransaction()) {
        console_.Warning("Materials", "Material graph rewire could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Created and connected material graph node #" + std::to_string(nodeId) + ".");
    return true;
}

bool EditorSceneContext::AddMaterialGraphComment(kb::assets::AssetId id, int graphX, int graphY) {
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (metadata == nullptr || metadata->type != "RenderMaterial" || materialEditor_.OpenAssetId() != id) {
        console_.Error("Materials", "Graph comments can only be edited on an open Material asset.");
        return false;
    }
    if (!materialEditor_.WorkingCopy().has_value()) {
        console_.Warning("Materials", "Material graph working copy is not available.");
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    std::vector<std::uint32_t> beforeSelectedNodeIds = materialEditor_.SelectedNodeIds();
    const std::uint32_t beforeSelectedCommentId = materialEditor_.SelectedCommentId();
    std::uint32_t commentId = 0U;
    if (!materialEditor_.AddGraphComment("Comment", graphX, graphY, 360, 180, 0x4A6385U, &commentId)) {
        console_.Warning("Materials", "Material graph comment could not be created.");
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Create Material Graph Comment", std::move(before), beforeSelectedNodeId, std::move(beforeSelectedNodeIds), beforeSelectedCommentId)) {
        console_.Warning("Materials", "Material graph comment creation could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Created material graph comment #" + std::to_string(commentId) + ".");
    return true;
}

bool EditorSceneContext::AddMaterialGraphComposite(kb::assets::AssetId id, int graphX, int graphY) {
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (metadata == nullptr || metadata->type != "RenderMaterial" || materialEditor_.OpenAssetId() != id) {
        console_.Error("Materials", "Graph composites can only be edited on an open Material asset.");
        return false;
    }
    if (!materialEditor_.WorkingCopy().has_value()) {
        console_.Warning("Materials", "Material graph working copy is not available.");
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    std::vector<std::uint32_t> beforeSelectedNodeIds = materialEditor_.SelectedNodeIds();
    const std::uint32_t beforeSelectedCommentId = materialEditor_.SelectedCommentId();
    std::uint32_t compositeId = 0U;
    if (!materialEditor_.CreateGraphCompositeFromSelection("Composite", graphX, graphY, 420, 260, &compositeId)) {
        console_.Warning("Materials", "Material graph composite could not be created.");
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Create Material Graph Composite", std::move(before), beforeSelectedNodeId, std::move(beforeSelectedNodeIds), beforeSelectedCommentId)) {
        console_.Warning("Materials", "Material graph composite creation could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Created material graph composite #" + std::to_string(compositeId) + ".");
    return true;
}

bool EditorSceneContext::DeleteSelectedMaterialGraphNode(kb::assets::AssetId id) {
    if (materialEditor_.OpenAssetId() != id) {
        return false;
    }
    const std::vector<std::uint32_t> selectedNodeIds = materialEditor_.SelectedNodeIds();
    if (selectedNodeIds.empty()) {
        return false;
    }
    if (!materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    if (!materialEditor_.DeleteSelectedGraphNodes()) {
        console_.Warning("Materials", "Material Output cannot be deleted.");
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Delete Material Graph Node", std::move(before), beforeSelectedNodeId, selectedNodeIds)) {
        console_.Warning("Materials", "Material graph node deletion could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Deleted " + std::to_string(selectedNodeIds.size()) + " material graph node(s).");
    return true;
}

bool EditorSceneContext::DeleteSelectedMaterialGraphComment(kb::assets::AssetId id) {
    if (materialEditor_.OpenAssetId() != id || materialEditor_.SelectedCommentId() == 0U) {
        return false;
    }
    if (!materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    std::vector<std::uint32_t> beforeSelectedNodeIds = materialEditor_.SelectedNodeIds();
    const std::uint32_t beforeSelectedCommentId = materialEditor_.SelectedCommentId();
    if (!materialEditor_.DeleteSelectedGraphComment()) {
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Delete Material Graph Comment", std::move(before), beforeSelectedNodeId, std::move(beforeSelectedNodeIds), beforeSelectedCommentId)) {
        console_.Warning("Materials", "Material graph comment deletion could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Deleted material graph comment.");
    return true;
}

bool EditorSceneContext::DisconnectSelectedMaterialGraphNodeLinks(kb::assets::AssetId id) {
    if (materialEditor_.OpenAssetId() != id) {
        return false;
    }
    const std::uint32_t nodeId = materialEditor_.SelectedNodeId();
    if (!materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    if (nodeId == 0U || !materialEditor_.DisconnectGraphNodeLinks(nodeId)) {
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Disconnect Material Graph Node", std::move(before), beforeSelectedNodeId)) {
        console_.Warning("Materials", "Material graph disconnect could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Disconnected material graph node #" + std::to_string(nodeId) + ".");
    return true;
}

bool EditorSceneContext::CopySelectedMaterialGraphNodes() {
    if (!materialEditor_.CopySelectedGraphNodes()) {
        console_.Warning("Materials", "Select at least one non-output material graph node to copy.");
        return false;
    }
    console_.Info("Materials", "Copied material graph selection.");
    return true;
}

bool EditorSceneContext::PasteMaterialGraphNodes(kb::assets::AssetId id, int offsetX, int offsetY) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    std::vector<std::uint32_t> beforeSelectedNodeIds = materialEditor_.SelectedNodeIds();
    std::vector<std::uint32_t> pastedNodeIds;
    if (!materialEditor_.PasteGraphClipboard(offsetX, offsetY, &pastedNodeIds)) {
        console_.Warning("Materials", "Material graph clipboard is empty.");
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Paste Material Graph Nodes", std::move(before), beforeSelectedNodeId, std::move(beforeSelectedNodeIds))) {
        console_.Warning("Materials", "Material graph paste could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Pasted " + std::to_string(pastedNodeIds.size()) + " material graph node(s).");
    return true;
}

bool EditorSceneContext::DuplicateSelectedMaterialGraphNodes(kb::assets::AssetId id, int offsetX, int offsetY) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    std::vector<std::uint32_t> beforeSelectedNodeIds = materialEditor_.SelectedNodeIds();
    std::vector<std::uint32_t> duplicatedNodeIds;
    if (!materialEditor_.DuplicateSelectedGraphNodes(offsetX, offsetY, &duplicatedNodeIds)) {
        console_.Warning("Materials", "Select at least one non-output material graph node to duplicate.");
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Duplicate Material Graph Nodes", std::move(before), beforeSelectedNodeId, std::move(beforeSelectedNodeIds))) {
        console_.Warning("Materials", "Material graph duplicate could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Duplicated " + std::to_string(duplicatedNodeIds.size()) + " material graph node(s).");
    return true;
}

bool EditorSceneContext::BeginMaterialGraphWorkingCopyTransaction(kb::assets::AssetId id, std::string label) {
    if (HasMaterialGraphWorkingCopyTransaction() || materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    if (label.empty()) {
        label = "Edit Material Graph";
    }
    materialGraphWorkingCopyTransactionAssetId_ = id;
    materialGraphWorkingCopyTransactionLabel_ = std::move(label);
    materialGraphWorkingCopyTransactionBefore_ = materialEditor_.WorkingCopy();
    materialGraphWorkingCopyTransactionBeforeSelectedNodeId_ = materialEditor_.SelectedNodeId();
    materialGraphWorkingCopyTransactionBeforeSelectedNodeIds_ = materialEditor_.SelectedNodeIds();
    materialGraphWorkingCopyTransactionBeforeSelectedCommentId_ = materialEditor_.SelectedCommentId();
    materialGraphWorkingCopyTransactionChanged_ = false;
    return true;
}

bool EditorSceneContext::CommitMaterialGraphWorkingCopyTransaction() {
    if (!HasMaterialGraphWorkingCopyTransaction()) {
        return false;
    }
    const kb::assets::AssetId assetId = materialGraphWorkingCopyTransactionAssetId_;
    std::string label = std::move(materialGraphWorkingCopyTransactionLabel_);
    std::optional<kb::render::RenderMaterialAssetData> before = std::move(materialGraphWorkingCopyTransactionBefore_);
    const std::uint32_t beforeSelectedNodeId = materialGraphWorkingCopyTransactionBeforeSelectedNodeId_;
    std::vector<std::uint32_t> beforeSelectedNodeIds = std::move(materialGraphWorkingCopyTransactionBeforeSelectedNodeIds_);
    const std::uint32_t beforeSelectedCommentId = materialGraphWorkingCopyTransactionBeforeSelectedCommentId_;
    const bool changed = materialGraphWorkingCopyTransactionChanged_;
    ClearMaterialGraphWorkingCopyTransaction();
    if (!changed) {
        return true;
    }
    if (!before.has_value()) {
        return false;
    }
    return RecordMaterialGraphWorkingCopyEdit(assetId, std::move(label), std::move(*before), beforeSelectedNodeId, std::move(beforeSelectedNodeIds), beforeSelectedCommentId);
}

void EditorSceneContext::CancelMaterialGraphWorkingCopyTransaction() {
    if (!HasMaterialGraphWorkingCopyTransaction()) {
        return;
    }
    const kb::assets::AssetId assetId = materialGraphWorkingCopyTransactionAssetId_;
    const std::uint32_t selectedNodeId = materialGraphWorkingCopyTransactionBeforeSelectedNodeId_;
    const std::vector<std::uint32_t> selectedNodeIds = materialGraphWorkingCopyTransactionBeforeSelectedNodeIds_;
    const std::uint32_t selectedCommentId = materialGraphWorkingCopyTransactionBeforeSelectedCommentId_;
    if (materialEditor_.OpenAssetId() == assetId && materialGraphWorkingCopyTransactionBefore_.has_value()) {
        materialEditor_.SetWorkingCopy(*materialGraphWorkingCopyTransactionBefore_);
        static_cast<void>(materialEditor_.SetNodeSelection(selectedNodeIds, selectedNodeId));
        if (selectedCommentId != 0U) {
            static_cast<void>(materialEditor_.SelectComment(selectedCommentId));
        }
        materialEditor_.ClearDiagnostics();
        SyncMaterialEditorWorkingCopyRuntimePreview();
        MarkSceneRenderDirty();
    }
    ClearMaterialGraphWorkingCopyTransaction();
}

bool EditorSceneContext::HasMaterialGraphWorkingCopyTransaction() const noexcept {
    return materialGraphWorkingCopyTransactionAssetId_.IsValid() && materialGraphWorkingCopyTransactionBefore_.has_value();
}

bool EditorSceneContext::SetMaterialGraphTextureSampleAsset(kb::assets::AssetId id, std::uint32_t nodeId, kb::assets::AssetId textureId) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value() || nodeId == 0U) {
        return false;
    }
    if (textureId.IsValid()) {
        const kb::assets::AssetMetadata* texture = scene_->Assets().Manager().Registry().Find(textureId);
        if (texture == nullptr || !IsTextureAsset(*texture)) {
            console_.Error("Materials", "Texture Sample rejected a non-texture asset.");
            return false;
        }
    }

    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    kb::render::RenderMaterialAssetData working = *materialEditor_.WorkingCopy();
    kb::render::RenderMaterialGraphNode* node = nullptr;
    for (kb::render::RenderMaterialGraphNode& candidate : working.graph.nodes) {
        if (candidate.id == nodeId) {
            node = &candidate;
            break;
        }
    }
    if (node == nullptr ||
        (node->kind != kb::render::RenderMaterialGraphNodeKind::TextureSample &&
            node->kind != kb::render::RenderMaterialGraphNodeKind::ParameterTexture)) {
        return false;
    }
    if (node->parameter.stableId.empty()) {
        node->parameter.stableId = node->kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture
            ? "texture" + std::to_string(node->id)
            : "textureSample" + std::to_string(node->id);
    }
    if (node->parameter.displayName.empty()) {
        node->parameter.displayName = node->kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture
            ? "Texture " + std::to_string(node->id)
            : "Texture Sample " + std::to_string(node->id);
    }
    if (node->parameter.textureRole.empty()) {
        node->parameter.textureRole = "baseColor";
    }
    if (node->parameter.expectedTextureColorSpace == kb::render::RenderMaterialTextureColorSpace::Unknown) {
        node->parameter.expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb;
    }
    node->parameter.overrideSupported = true;

    auto existing = std::ranges::find_if(working.graphParameterValues, [stableId = node->parameter.stableId](const kb::render::RenderMaterialGraphParameterValue& value) {
        return value.stableId == stableId && value.type == kb::render::RenderMaterialParameterType::Texture;
    });
    if (existing != working.graphParameterValues.end()) {
        existing->assetId = textureId.value;
    } else {
        working.graphParameterValues.push_back(kb::render::RenderMaterialGraphParameterValue{
            .stableId = node->parameter.stableId,
            .type = kb::render::RenderMaterialParameterType::Texture,
            .assetId = textureId.value,
        });
    }

    materialEditor_.SetWorkingCopy(std::move(working));
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Set Texture Sample Asset", std::move(before), materialEditor_.SelectedNodeId())) {
        console_.Warning("Materials", "Texture Sample asset change could not be recorded.");
        return false;
    }
    console_.Info("Materials", textureId.IsValid() ? "Texture Sample asset assigned." : "Texture Sample asset cleared.");
    return true;
}

bool EditorSceneContext::SetMaterialGraphConstantColorValue(
    kb::assets::AssetId id,
    std::uint32_t nodeId,
    const std::array<float, 4U>& color) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value() || nodeId == 0U) {
        console_.Error("Materials", "Open the material in Material Editor before editing graph colors.");
        return false;
    }

    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    if (!materialEditor_.SetGraphConstantColorValue(nodeId, color)) {
        console_.Error("Materials", "Material graph color value is invalid.");
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Edit Material Graph Color", std::move(before), beforeSelectedNodeId)) {
        return false;
    }
    console_.Info("Materials", "Edited material graph color #" + std::to_string(nodeId) + ".");
    return true;
}

bool EditorSceneContext::SetMaterialGraphNodeEnumValue(
    kb::assets::AssetId id,
    std::uint32_t nodeId,
    std::string_view propertyId,
    std::string_view value) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value() || nodeId == 0U) {
        console_.Error("Materials", "Open the material in Material Editor before editing graph node properties.");
        return false;
    }

    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    if (!materialEditor_.SetGraphNodeEnumValue(nodeId, propertyId, value)) {
        console_.Error("Materials", "Material graph node enum value is invalid.");
        return false;
    }
    materialEditor_.CloseGraphNodeEnumDropdown();
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Edit Material Graph Node Property", std::move(before), beforeSelectedNodeId)) {
        return false;
    }
    console_.Info("Materials", "Edited material graph node property '" + std::string{ propertyId } + "'.");
    return true;
}

void EditorSceneContext::ToggleMaterialGraphNodeEnumDropdown(std::uint32_t nodeId, std::string propertyId) {
    materialEditor_.ToggleGraphNodeEnumDropdown(nodeId, std::move(propertyId));
}

void EditorSceneContext::CloseMaterialGraphNodeEnumDropdown() noexcept {
    materialEditor_.CloseGraphNodeEnumDropdown();
}

bool EditorSceneContext::SetMaterialGraphConstantValue(
    kb::assets::AssetId id,
    std::uint32_t nodeId,
    std::string_view valueText) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value() || nodeId == 0U) {
        console_.Error("Materials", "Open the material in Material Editor before editing graph constants.");
        return false;
    }

    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    if (!materialEditor_.SetGraphConstantValue(nodeId, valueText)) {
        console_.Error("Materials", "Material graph constant value is invalid.");
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Edit Material Graph Constant", std::move(before), beforeSelectedNodeId)) {
        return false;
    }
    console_.Info("Materials", "Edited material graph constant #" + std::to_string(nodeId) + ".");
    return true;
}

bool EditorSceneContext::BeginMaterialGraphConstantInlineEdit(kb::assets::AssetId id, std::uint32_t nodeId) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value() || nodeId == 0U) {
        return false;
    }
    return materialEditor_.BeginGraphConstantInlineEdit(nodeId);
}

bool EditorSceneContext::IsMaterialGraphConstantInlineEditing() const noexcept {
    return materialEditor_.IsGraphConstantInlineEditing();
}

bool EditorSceneContext::BeginMaterialGraphConstantSliderDrag(kb::assets::AssetId id, std::uint32_t nodeId, std::size_t componentIndex, int x) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value() || nodeId == 0U) {
        return false;
    }
    const std::optional<float> value = materialEditor_.GraphConstantComponentValue(nodeId, componentIndex);
    if (!value.has_value()) {
        return false;
    }
    materialGraphConstantSliderAssetId_ = id;
    materialGraphConstantSliderNodeId_ = nodeId;
    materialGraphConstantSliderComponentIndex_ = componentIndex;
    materialGraphConstantSliderStartX_ = x;
    materialGraphConstantSliderStartValue_ = *value;
    materialGraphConstantSliderLastValue_ = *value;
    materialGraphConstantSliderStartDocument_ = materialEditor_.WorkingCopy();
    materialGraphConstantSliderStartSelectedNodeId_ = materialEditor_.SelectedNodeId();
    materialGraphConstantSliderChanged_ = false;
    materialGraphConstantSliderDragging_ = true;
    return true;
}

bool EditorSceneContext::DragMaterialGraphConstantSlider(int x) {
    if (!materialGraphConstantSliderDragging_ ||
        materialEditor_.OpenAssetId() != materialGraphConstantSliderAssetId_ ||
        !materialEditor_.WorkingCopy().has_value() ||
        materialGraphConstantSliderNodeId_ == 0U) {
        return false;
    }
    const float graphAdjustedDelta = static_cast<float>(x - materialGraphConstantSliderStartX_) / std::max(0.1F, materialGraphZoom_);
    const float rawValue = materialGraphConstantSliderStartValue_ + (graphAdjustedDelta * 0.02F);
    float nextValue = std::round(rawValue * 10.0F) / 10.0F;
    if (const std::optional<kb::render::RenderMaterialParameterRange> range =
            materialEditor_.GraphConstantComponentRange(materialGraphConstantSliderNodeId_, materialGraphConstantSliderComponentIndex_)) {
        nextValue = std::clamp(nextValue, range->min, range->max);
    }
    if (std::abs(nextValue - materialGraphConstantSliderLastValue_) < 0.0001F) {
        return false;
    }
    if (!materialEditor_.SetGraphConstantComponentValue(materialGraphConstantSliderNodeId_, materialGraphConstantSliderComponentIndex_, nextValue)) {
        return false;
    }
    materialGraphConstantSliderLastValue_ = nextValue;
    materialGraphConstantSliderChanged_ = true;
    materialEditor_.ClearDiagnostics();
    return true;
}

bool EditorSceneContext::EndMaterialGraphConstantSliderDrag() {
    if (!materialGraphConstantSliderDragging_) {
        return false;
    }
    const bool shouldRecord = materialGraphConstantSliderChanged_ && materialGraphConstantSliderStartDocument_.has_value();
    const kb::assets::AssetId assetId = materialGraphConstantSliderAssetId_;
    std::optional<kb::render::RenderMaterialAssetData> before = std::move(materialGraphConstantSliderStartDocument_);
    const std::uint32_t beforeSelectedNodeId = materialGraphConstantSliderStartSelectedNodeId_;
    materialGraphConstantSliderDragging_ = false;
    materialGraphConstantSliderAssetId_ = {};
    materialGraphConstantSliderNodeId_ = 0U;
    materialGraphConstantSliderComponentIndex_ = 0U;
    materialGraphConstantSliderStartX_ = 0;
    materialGraphConstantSliderStartValue_ = 0.0F;
    materialGraphConstantSliderLastValue_ = 0.0F;
    materialGraphConstantSliderStartSelectedNodeId_ = 0U;
    materialGraphConstantSliderChanged_ = false;
    if (shouldRecord) {
        return RecordMaterialGraphWorkingCopyEdit(assetId, "Edit Material Graph Constant", std::move(*before), beforeSelectedNodeId);
    }
    return true;
}

bool EditorSceneContext::IsMaterialGraphConstantSliderDragging() const noexcept {
    return materialGraphConstantSliderDragging_;
}

void EditorSceneContext::AppendMaterialGraphConstantInlineEditText(wchar_t character) {
    materialEditor_.AppendGraphConstantInlineEditText(character);
}

void EditorSceneContext::BackspaceMaterialGraphConstantInlineEdit() {
    materialEditor_.BackspaceGraphConstantInlineEdit();
}

bool EditorSceneContext::CommitMaterialGraphConstantInlineEdit() {
    const kb::assets::AssetId id = materialEditor_.OpenAssetId();
    const std::uint32_t nodeId = materialEditor_.GraphConstantInlineEditNodeId();
    const std::string value{ materialEditor_.GraphConstantInlineEditBuffer() };
    if (!id.IsValid() || nodeId == 0U) {
        materialEditor_.CancelGraphConstantInlineEdit();
        return false;
    }
    const bool committed = SetMaterialGraphConstantValue(id, nodeId, value);
    if (committed) {
        materialEditor_.CancelGraphConstantInlineEdit();
    }
    return committed;
}

void EditorSceneContext::CancelMaterialGraphConstantInlineEdit() noexcept {
    materialEditor_.CancelGraphConstantInlineEdit();
}

bool EditorSceneContext::BeginMaterialGraphPinConnection(kb::assets::AssetId id, std::uint32_t nodeId, std::string pin) {
    return BeginMaterialGraphPinConnection(id, nodeId, std::move(pin), true, 0, 0);
}

bool EditorSceneContext::BeginMaterialGraphPinConnection(
    kb::assets::AssetId id,
    std::uint32_t nodeId,
    std::string pin,
    bool outputPin,
    int x,
    int y) {
    if (materialEditor_.OpenAssetId() != id || nodeId == 0U || pin.empty()) {
        return false;
    }
    if (!materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(materialEditor_.WorkingCopy()->graph, nodeId);
    if (node == nullptr ||
        (outputPin && !kb::render::IsRenderMaterialGraphOutputPin(*node, pin)) ||
        (!outputPin && !kb::render::IsRenderMaterialGraphInputPin(*node, pin))) {
        return false;
    }
    materialGraphPendingConnectionAssetId_ = id;
    materialGraphPendingConnectionNodeId_ = nodeId;
    materialGraphPendingConnectionPin_ = std::move(pin);
    materialGraphPendingConnectionOutput_ = outputPin;
    materialGraphPendingConnectionX_ = x;
    materialGraphPendingConnectionY_ = y;
    materialGraphPendingConnectionOwnsTransaction_ = false;
    return true;
}

bool EditorSceneContext::DragMaterialGraphPinConnection(int x, int y) noexcept {
    if (!HasMaterialGraphPinConnection()) {
        return false;
    }
    if (materialGraphPendingConnectionX_ == x && materialGraphPendingConnectionY_ == y) {
        return false;
    }
    materialGraphPendingConnectionX_ = x;
    materialGraphPendingConnectionY_ = y;
    return true;
}

bool EditorSceneContext::CompleteMaterialGraphPinConnection(kb::assets::AssetId id, std::uint32_t toNodeId, std::string toPin) {
    return CompleteMaterialGraphPinConnection(id, toNodeId, std::move(toPin), true);
}

bool EditorSceneContext::CompleteMaterialGraphPinConnection(
    kb::assets::AssetId id,
    std::uint32_t nodeId,
    std::string pin,
    bool inputPin) {
    if (materialGraphPendingConnectionAssetId_ != id || materialGraphPendingConnectionNodeId_ == 0U || nodeId == 0U || pin.empty()) {
        return false;
    }
    if (pin.empty() || inputPin != materialGraphPendingConnectionOutput_) {
        const std::string direction = materialGraphPendingConnectionOutput_ ? "output-to-output" : "input-to-input";
        const std::string diagnostic = "Material graph pins are not compatible: " + direction + " connections are not allowed.";
        materialEditor_.SetDiagnostics({ "Error graph.link_direction_mismatch: " + diagnostic }, true);
        console_.Warning("Materials", diagnostic);
        return false;
    }
    const std::uint32_t fromNodeId = materialGraphPendingConnectionOutput_ ? materialGraphPendingConnectionNodeId_ : nodeId;
    const std::uint32_t toNodeId = materialGraphPendingConnectionOutput_ ? nodeId : materialGraphPendingConnectionNodeId_;
    std::string fromPin = materialGraphPendingConnectionOutput_ ? materialGraphPendingConnectionPin_ : std::move(pin);
    std::string toPin = materialGraphPendingConnectionOutput_ ? std::move(pin) : materialGraphPendingConnectionPin_;
    const bool ownsTransaction = materialGraphPendingConnectionOwnsTransaction_;
    ClearMaterialGraphPinConnectionState();
    if (!materialEditor_.WorkingCopy().has_value()) {
        if (ownsTransaction) {
            CancelMaterialGraphWorkingCopyTransaction();
        }
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    if (!materialEditor_.ConnectGraphPins(fromNodeId, fromPin, toNodeId, toPin)) {
        const std::string diagnostic = MaterialGraphPinConnectionDiagnostic(*materialEditor_.WorkingCopy(), fromNodeId, fromPin, toNodeId, toPin);
        materialEditor_.SetDiagnostics({ "Error graph.link_type_mismatch: " + diagnostic }, true);
        console_.Warning("Materials", diagnostic);
        if (ownsTransaction && !CommitMaterialGraphWorkingCopyTransaction()) {
            console_.Warning("Materials", "Material graph detach could not be recorded.");
        }
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Connect Material Graph Pins", std::move(before), beforeSelectedNodeId)) {
        console_.Warning("Materials", "Material graph connection could not be recorded.");
        if (ownsTransaction) {
            CancelMaterialGraphWorkingCopyTransaction();
        }
        return false;
    }
    if (ownsTransaction && !CommitMaterialGraphWorkingCopyTransaction()) {
        console_.Warning("Materials", "Material graph rewire could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Connected material graph pins.");
    return true;
}

bool EditorSceneContext::DisconnectMaterialGraphInputPin(kb::assets::AssetId id, std::uint32_t toNodeId, std::string_view toPin) {
    if (materialEditor_.OpenAssetId() != id) {
        return false;
    }
    if (!materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    if (!materialEditor_.DisconnectGraphInputPin(toNodeId, toPin)) {
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Disconnect Material Graph Input", std::move(before), beforeSelectedNodeId)) {
        console_.Warning("Materials", "Material graph input disconnect could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Disconnected material graph input.");
    return true;
}

bool EditorSceneContext::DisconnectMaterialGraphOutputPin(kb::assets::AssetId id, std::uint32_t fromNodeId, std::string_view fromPin) {
    if (materialEditor_.OpenAssetId() != id) {
        return false;
    }
    if (!materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    if (!materialEditor_.DisconnectGraphOutputPin(fromNodeId, fromPin)) {
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Disconnect Material Graph Output", std::move(before), beforeSelectedNodeId)) {
        console_.Warning("Materials", "Material graph output disconnect could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Disconnected material graph output.");
    return true;
}

bool EditorSceneContext::DisconnectMaterialGraphLink(
    kb::assets::AssetId id,
    std::uint32_t fromNodeId,
    std::string_view fromPin,
    std::uint32_t toNodeId,
    std::string_view toPin) {
    if (materialEditor_.OpenAssetId() != id) {
        return false;
    }
    if (!materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    if (!materialEditor_.DisconnectGraphLink(fromNodeId, fromPin, toNodeId, toPin)) {
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Disconnect Material Graph Link", std::move(before), beforeSelectedNodeId)) {
        console_.Warning("Materials", "Material graph link disconnect could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Disconnected material graph link.");
    return true;
}

bool EditorSceneContext::DetachMaterialGraphInputPinConnection(
    kb::assets::AssetId id,
    std::uint32_t toNodeId,
    std::string_view toPin,
    int x,
    int y) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value()) {
        return false;
    }

    const kb::render::RenderMaterialGraphLink* detachedLink = nullptr;
    for (const kb::render::RenderMaterialGraphLink& link : materialEditor_.WorkingCopy()->graph.links) {
        if (link.toNodeId == toNodeId && link.toPin == toPin) {
            detachedLink = &link;
            break;
        }
    }
    if (detachedLink == nullptr) {
        return false;
    }

    if (!BeginMaterialGraphWorkingCopyTransaction(id, "Rewire Material Graph Link")) {
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    const std::uint32_t fromNodeId = detachedLink->fromNodeId;
    const std::string fromPin = detachedLink->fromPin;
    const std::string inputPin{ toPin };
    if (!materialEditor_.DisconnectGraphLink(fromNodeId, fromPin, toNodeId, inputPin)) {
        CancelMaterialGraphWorkingCopyTransaction();
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Rewire Material Graph Link", std::move(before), beforeSelectedNodeId)) {
        CancelMaterialGraphWorkingCopyTransaction();
        return false;
    }
    if (!BeginMaterialGraphPinConnection(id, fromNodeId, fromPin, true, x, y)) {
        return CommitMaterialGraphWorkingCopyTransaction();
    }
    materialGraphPendingConnectionOwnsTransaction_ = true;
    return true;
}

bool EditorSceneContext::CancelMaterialGraphPinConnection() {
    const bool ownsTransaction = materialGraphPendingConnectionOwnsTransaction_;
    ClearMaterialGraphPinConnectionState();
    if (ownsTransaction) {
        return CommitMaterialGraphWorkingCopyTransaction();
    }
    return true;
}

void EditorSceneContext::ClearMaterialGraphPinConnectionState() noexcept {
    materialGraphPendingConnectionAssetId_ = {};
    materialGraphPendingConnectionNodeId_ = 0U;
    materialGraphPendingConnectionPin_.clear();
    materialGraphPendingConnectionOutput_ = true;
    materialGraphPendingConnectionX_ = 0;
    materialGraphPendingConnectionY_ = 0;
    materialGraphPendingConnectionOwnsTransaction_ = false;
}

bool EditorSceneContext::HasMaterialGraphPinConnection() const noexcept {
    return materialGraphPendingConnectionAssetId_.IsValid() && materialGraphPendingConnectionNodeId_ != 0U && !materialGraphPendingConnectionPin_.empty();
}

kb::assets::AssetId EditorSceneContext::MaterialGraphPinConnectionAssetId() const noexcept {
    return materialGraphPendingConnectionAssetId_;
}

std::uint32_t EditorSceneContext::MaterialGraphPinConnectionNodeId() const noexcept {
    return materialGraphPendingConnectionNodeId_;
}

std::string_view EditorSceneContext::MaterialGraphPinConnectionPin() const noexcept {
    return materialGraphPendingConnectionPin_;
}

bool EditorSceneContext::MaterialGraphPinConnectionIsOutput() const noexcept {
    return materialGraphPendingConnectionOutput_;
}

int EditorSceneContext::MaterialGraphPinConnectionX() const noexcept {
    return materialGraphPendingConnectionX_;
}

int EditorSceneContext::MaterialGraphPinConnectionY() const noexcept {
    return materialGraphPendingConnectionY_;
}

bool EditorSceneContext::OpenMaterialGraphContextMenu(kb::assets::AssetId id, int x, int y, int graphX, int graphY) noexcept {
    if (materialEditor_.OpenAssetId() != id || !id.IsValid()) {
        return false;
    }
    materialGraphContextMenuAssetId_ = id;
    materialGraphContextMenuX_ = x;
    materialGraphContextMenuY_ = y;
    materialGraphContextMenuGraphX_ = graphX;
    materialGraphContextMenuGraphY_ = graphY;
    materialGraphContextMenuExpandedMask_ = 0U;
    materialGraphContextMenuHoveredCategory_ = static_cast<std::size_t>(-1);
    materialGraphContextMenuHoveredCommand_ = MaterialEditorGraphMenuCommand::None;
    materialGraphContextMenuSearchQuery_.clear();
    materialGraphContextMenuPinFilterActive_ = false;
    materialGraphContextMenuPinFilterNodeId_ = 0U;
    materialGraphContextMenuPinFilterPin_.clear();
    materialGraphContextMenuPinFilterOutput_ = true;
    return true;
}

bool EditorSceneContext::OpenMaterialGraphContextMenuForPinConnection(kb::assets::AssetId id, int x, int y, int graphX, int graphY) noexcept {
    if (materialEditor_.OpenAssetId() != id || !id.IsValid() || materialGraphPendingConnectionAssetId_ != id ||
        materialGraphPendingConnectionNodeId_ == 0U || materialGraphPendingConnectionPin_.empty()) {
        return false;
    }
    materialGraphContextMenuAssetId_ = id;
    materialGraphContextMenuX_ = x;
    materialGraphContextMenuY_ = y;
    materialGraphContextMenuGraphX_ = graphX;
    materialGraphContextMenuGraphY_ = graphY;
    materialGraphContextMenuExpandedMask_ = 0U;
    materialGraphContextMenuHoveredCategory_ = static_cast<std::size_t>(-1);
    materialGraphContextMenuHoveredCommand_ = MaterialEditorGraphMenuCommand::None;
    materialGraphContextMenuSearchQuery_.clear();
    materialGraphContextMenuPinFilterNodeId_ = materialGraphPendingConnectionNodeId_;
    materialGraphContextMenuPinFilterPin_ = materialGraphPendingConnectionPin_;
    materialGraphContextMenuPinFilterOutput_ = materialGraphPendingConnectionOutput_;
    materialGraphContextMenuPinFilterActive_ = true;
    return true;
}

bool EditorSceneContext::CloseMaterialGraphContextMenu() noexcept {
    if (!materialGraphContextMenuAssetId_.IsValid()) {
        return false;
    }
    materialGraphContextMenuAssetId_ = {};
    materialGraphContextMenuHoveredCategory_ = static_cast<std::size_t>(-1);
    materialGraphContextMenuHoveredCommand_ = MaterialEditorGraphMenuCommand::None;
    materialGraphContextMenuSearchQuery_.clear();
    materialGraphContextMenuPinFilterActive_ = false;
    materialGraphContextMenuPinFilterNodeId_ = 0U;
    materialGraphContextMenuPinFilterPin_.clear();
    materialGraphContextMenuPinFilterOutput_ = true;
    return true;
}

bool EditorSceneContext::IsMaterialGraphContextMenuOpen() const noexcept {
    return materialGraphContextMenuAssetId_.IsValid();
}

int EditorSceneContext::MaterialGraphContextMenuX() const noexcept {
    return materialGraphContextMenuX_;
}

int EditorSceneContext::MaterialGraphContextMenuY() const noexcept {
    return materialGraphContextMenuY_;
}

int EditorSceneContext::MaterialGraphContextMenuGraphX() const noexcept {
    return materialGraphContextMenuGraphX_;
}

int EditorSceneContext::MaterialGraphContextMenuGraphY() const noexcept {
    return materialGraphContextMenuGraphY_;
}

std::string_view EditorSceneContext::MaterialGraphContextMenuSearchQuery() const noexcept {
    return materialGraphContextMenuSearchQuery_;
}

void EditorSceneContext::SetMaterialGraphContextMenuSearchQuery(std::string query) {
    if (query.size() > 64U) {
        query.resize(64U);
    }
    materialGraphContextMenuSearchQuery_ = std::move(query);
}

void EditorSceneContext::AppendMaterialGraphContextMenuSearchText(wchar_t character) {
    if (character < 32 || character > 126 || materialGraphContextMenuSearchQuery_.size() >= 64U) {
        return;
    }
    materialGraphContextMenuSearchQuery_.push_back(static_cast<char>(character));
    materialGraphContextMenuExpandedMask_ = 0U;
}

void EditorSceneContext::BackspaceMaterialGraphContextMenuSearch() {
    if (!materialGraphContextMenuSearchQuery_.empty()) {
        materialGraphContextMenuSearchQuery_.pop_back();
    }
}

void EditorSceneContext::ClearMaterialGraphContextMenuSearch() noexcept {
    materialGraphContextMenuSearchQuery_.clear();
}

const std::vector<MaterialEditorGraphMenuCommand>& EditorSceneContext::MaterialGraphPaletteFavoriteCommands() const noexcept {
    return materialGraphPaletteFavorites_;
}

bool EditorSceneContext::IsMaterialGraphPaletteFavorite(MaterialEditorGraphMenuCommand command) const noexcept {
    return std::find(materialGraphPaletteFavorites_.begin(), materialGraphPaletteFavorites_.end(), command) != materialGraphPaletteFavorites_.end();
}

bool EditorSceneContext::ToggleMaterialGraphPaletteFavorite(MaterialEditorGraphMenuCommand command) {
    if (command == MaterialEditorGraphMenuCommand::None ||
        command == MaterialEditorGraphMenuCommand::DisconnectSelected ||
        command == MaterialEditorGraphMenuCommand::DeleteSelected) {
        return false;
    }
    const auto found = std::find(materialGraphPaletteFavorites_.begin(), materialGraphPaletteFavorites_.end(), command);
    if (found != materialGraphPaletteFavorites_.end()) {
        materialGraphPaletteFavorites_.erase(found);
        return true;
    }
    materialGraphPaletteFavorites_.push_back(command);
    return true;
}

bool EditorSceneContext::IsMaterialGraphContextMenuPinFiltered() const noexcept {
    return materialGraphContextMenuPinFilterActive_;
}

std::uint32_t EditorSceneContext::MaterialGraphContextMenuPinFilterNodeId() const noexcept {
    return materialGraphContextMenuPinFilterNodeId_;
}

std::string_view EditorSceneContext::MaterialGraphContextMenuPinFilterPin() const noexcept {
    return materialGraphContextMenuPinFilterPin_;
}

bool EditorSceneContext::MaterialGraphContextMenuPinFilterIsOutput() const noexcept {
    return materialGraphContextMenuPinFilterOutput_;
}

bool EditorSceneContext::IsMaterialGraphContextMenuCategoryExpanded(std::size_t categoryIndex) const noexcept {
    if (categoryIndex >= 32U) {
        return false;
    }
    return (materialGraphContextMenuExpandedMask_ & (1U << categoryIndex)) != 0U;
}

bool EditorSceneContext::IsMaterialGraphContextMenuCategoryHovered(std::size_t categoryIndex) const noexcept {
    return materialGraphContextMenuHoveredCategory_ == categoryIndex &&
        materialGraphContextMenuHoveredCommand_ == MaterialEditorGraphMenuCommand::None;
}

bool EditorSceneContext::IsMaterialGraphContextMenuCommandHovered(std::size_t categoryIndex, MaterialEditorGraphMenuCommand command) const noexcept {
    return materialGraphContextMenuHoveredCategory_ == categoryIndex &&
        materialGraphContextMenuHoveredCommand_ == command &&
        command != MaterialEditorGraphMenuCommand::None;
}

bool EditorSceneContext::SetMaterialGraphContextMenuHover(std::size_t categoryIndex, MaterialEditorGraphMenuCommand command) noexcept {
    if (materialGraphContextMenuHoveredCategory_ == categoryIndex && materialGraphContextMenuHoveredCommand_ == command) {
        return false;
    }
    materialGraphContextMenuHoveredCategory_ = categoryIndex;
    materialGraphContextMenuHoveredCommand_ = command;
    return true;
}

bool EditorSceneContext::ClearMaterialGraphContextMenuHover() noexcept {
    return SetMaterialGraphContextMenuHover(static_cast<std::size_t>(-1), MaterialEditorGraphMenuCommand::None);
}

bool EditorSceneContext::ToggleMaterialGraphContextMenuCategory(std::size_t categoryIndex) noexcept {
    if (categoryIndex >= 32U) {
        return false;
    }
    materialGraphContextMenuExpandedMask_ ^= (1U << categoryIndex);
    return true;
}

bool EditorSceneContext::ExecuteMaterialGraphContextMenuCommand(MaterialEditorGraphMenuCommand command) {
    const kb::assets::AssetId id = materialGraphContextMenuAssetId_;
    const int graphX = materialGraphContextMenuGraphX_;
    const int graphY = materialGraphContextMenuGraphY_;
    const bool pinFiltered = materialGraphContextMenuPinFilterActive_;
    if (pinFiltered && MaterialEditorGraphMenuCommandNodeKind(command).has_value()) {
        const bool created = AddMaterialGraphNodeForPendingConnection(id, command, graphX, graphY);
        static_cast<void>(CloseMaterialGraphContextMenu());
        if (!created) {
            static_cast<void>(CancelMaterialGraphPinConnection());
        }
        return created;
    }
    static_cast<void>(CloseMaterialGraphContextMenu());
    if (pinFiltered) {
        static_cast<void>(CancelMaterialGraphPinConnection());
        return false;
    }
    switch (command) {
    case MaterialEditorGraphMenuCommand::CreateTextureSample:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::TextureSample, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateTextureParameter:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ParameterTexture, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateUv:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Uv, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateScalar:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantScalar, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateBool:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantBool, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateVector2:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantVector2, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateVector:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantVector, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateColor:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantColor, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateScalarParameter:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ParameterScalar, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateVectorParameter:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ParameterVector, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateColorParameter:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ParameterColor, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateCollectionParameter:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::CollectionParameter, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateAdd:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Add, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateSubtract:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Subtract, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateMultiply:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Multiply, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateDivide:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Divide, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreatePower:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Power, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateOneMinus:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::OneMinus, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateAbsolute:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Absolute, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateMinimum:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Minimum, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateMaximum:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Maximum, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateSaturate:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Saturate, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateFloor:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Floor, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateCeil:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Ceil, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateFraction:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Fraction, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateSquareRoot:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::SquareRoot, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateSine:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Sine, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateCosine:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Cosine, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateExponential:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Exponential, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateExponential2:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Exponential2, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateLogarithm:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Logarithm, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateLogarithm2:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Logarithm2, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateSrgbToLinear:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::SrgbToLinear, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateLinearToSrgb:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::LinearToSrgb, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateLogarithm10:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Logarithm10, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateHsvToRgb:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::HsvToRgb, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateRgbToHsv:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::RgbToHsv, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateDeriveNormalZ:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::DeriveNormalZ, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateFmod:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Fmod, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateInverseLerp:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::InverseLerp, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreatePartialDerivativeX:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::PartialDerivativeX, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreatePartialDerivativeY:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::PartialDerivativeY, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateSphereMask:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::SphereMask, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateBlackBody:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::BlackBody, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateNoise:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Noise, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateVectorNoise:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::VectorNoise, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateAppendVector:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::AppendVector, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateColorRamp:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ColorRamp, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateAntialiasedTextureMask:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::AntialiasedTextureMask, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateTransform:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Transform, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateTransformPosition:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::TransformPosition, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateDotProduct:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::DotProduct, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateCrossProduct:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::CrossProduct, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateNormalize:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Normalize, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateLength:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Length, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateDistance:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Distance, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateBreakVector:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::BreakVector, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateMakeVector:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::MakeVector, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateStep:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Step, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateSmoothStep:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::SmoothStep, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateIf:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::If, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateDesaturate:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Desaturate, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateFresnel:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Fresnel, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateNegate:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Negate, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateSign:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Sign, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateRound:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Round, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateTruncate:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Truncate, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateTangent:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Tangent, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateArcSine:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ArcSine, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateArcCosine:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ArcCosine, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateArcTangent:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ArcTangent, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateArcTangent2:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ArcTangent2, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateClamp:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Clamp, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateLerp:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Lerp, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateNormalUnpack:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::NormalUnpack, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateTime:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Time, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateVertexColor:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::VertexColor, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateScreenPosition:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ScreenPosition, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateLocalPosition:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::LocalPosition, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateObjectPosition:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ObjectPosition, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateWorldPosition:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::WorldPosition, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreatePerInstanceRandom:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::PerInstanceRandom, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateObjectRadius:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ObjectRadius, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateObjectBounds:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ObjectBounds, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateObjectOrientation:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ObjectOrientation, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateMakeMaterialAttributes:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::MakeMaterialAttributes, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateBreakMaterialAttributes:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::BreakMaterialAttributes, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateBlendMaterialAttributes:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::BlendMaterialAttributes, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateGetMaterialAttributes:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::GetMaterialAttributes, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateSetMaterialAttributes:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::SetMaterialAttributes, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateStaticBoolParameter:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::StaticBoolParameter, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateStaticSwitch:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::StaticSwitch, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateStaticComponentMask:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::StaticComponentMask, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateQualitySwitch:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::QualitySwitch, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateFeatureLevelSwitch:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::FeatureLevelSwitch, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateShadingPathSwitch:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ShadingPathSwitch, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateShaderStageSwitch:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ShaderStageSwitch, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateTextureCoordinate:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::TextureCoordinate, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreatePanner:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Panner, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateRotator:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Rotator, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateBumpOffset:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::BumpOffset, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateConstantBiasScale:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantBiasScale, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateRotateAboutAxis:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::RotateAboutAxis, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateViewportUV:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ViewportUV, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateCameraPosition:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::CameraPosition, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateCameraVector:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::CameraVector, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateReflectionVector:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ReflectionVector, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateLightVector:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::LightVector, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreatePixelNormalWS:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::PixelNormalWS, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateVertexNormalWS:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::VertexNormalWS, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateVertexTangentWS:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::VertexTangentWS, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateViewProperty:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ViewProperty, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateViewSize:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ViewSize, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateSceneDepth:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::SceneDepth, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateDepthFade:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::DepthFade, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateCustomCode:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::CustomCode, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateReroute:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Reroute, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateNamedRerouteDeclaration:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::NamedRerouteDeclaration, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateNamedRerouteUsage:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::NamedRerouteUsage, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateCompositeInput:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::CompositeInput, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateCompositeOutput:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::CompositeOutput, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateFunctionInput:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::FunctionInput, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateFunctionOutput:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::FunctionOutput, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateMaterialFunctionCall:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateLayerStack:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::LayerStack, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateComposite:
        return AddMaterialGraphComposite(id, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateComment:
        return AddMaterialGraphComment(id, graphX, graphY);
    case MaterialEditorGraphMenuCommand::DisconnectSelected:
        return DisconnectSelectedMaterialGraphNodeLinks(id);
    case MaterialEditorGraphMenuCommand::DeleteSelected:
        return DeleteSelectedMaterialGraphNode(id) || DeleteSelectedMaterialGraphComment(id);
    case MaterialEditorGraphMenuCommand::None:
        return false;
    }
    return false;
}

bool EditorSceneContext::SetMaterialEditorGraphParameterValue(
    kb::assets::AssetId id,
    std::string_view stableId,
    kb::render::RenderMaterialParameterType type,
    std::string_view valueText) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value()) {
        console_.Error("Materials", "Open the material in Material Editor before editing graph parameters.");
        return false;
    }
    if (stableId.empty() || type == kb::render::RenderMaterialParameterType::Texture) {
        console_.Error("Materials", "Material graph parameter cannot be edited as a numeric value.");
        return false;
    }
    std::optional<kb::render::RenderMaterialGraphParameterValue> value =
        ParseMaterialGraphParameterValue(stableId, type, valueText);
    if (!value.has_value()) {
        console_.Error("Materials", "Material graph parameter value is invalid.");
        return false;
    }

    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    kb::render::RenderMaterialAssetData after = before;
    UpsertGraphParameterValue(after, std::move(*value));
    materialEditor_.SetWorkingCopy(std::move(after));
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Edit Material Graph Parameter", std::move(before), beforeSelectedNodeId)) {
        return false;
    }
    console_.Info("Materials", "Edited material graph parameter '" + std::string{ stableId } + "'.");
    return true;
}

bool EditorSceneContext::SetMaterialInstanceEditorGraphParameterValue(
    kb::assets::AssetId id,
    std::string_view stableId,
    kb::render::RenderMaterialParameterType type,
    std::string_view valueText) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.InstanceWorkingCopy().has_value()) {
        console_.Error("Materials", "Open the material instance in Material Editor before editing overrides.");
        return false;
    }
    if (stableId.empty() || type == kb::render::RenderMaterialParameterType::Texture) {
        console_.Error("Materials", "Material instance parameter override cannot be edited as a numeric value.");
        return false;
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (metadata == nullptr || metadata->type != "RenderMaterialInstance") {
        console_.Error("Materials", "Selected asset is not a Material Instance.");
        return false;
    }
    if (!materialEditor_.InstanceParentSnapshot().has_value()) {
        console_.Error("Materials", "Material instance parent document is not available for override validation.");
        return false;
    }

    std::optional<kb::render::RenderMaterialGraphParameterValue> value =
        ParseMaterialGraphParameterValue(stableId, type, valueText);
    if (!value.has_value()) {
        console_.Error("Materials", "Material instance parameter override value is invalid.");
        return false;
    }

    kb::render::RenderMaterialInstanceAssetData instance = *materialEditor_.InstanceWorkingCopy();
    const kb::render::RenderMaterialAssetData& parent = *materialEditor_.InstanceParentSnapshot();
    EnsureMaterialInstanceOverrideDocument(instance, parent);
    RemoveGraphParameterValue(instance.overrides, stableId);
    UpsertGraphParameterValue(instance.overrides, std::move(*value));

    const kb::render::RenderMaterialInstanceValidationResult validation =
        kb::render::RenderMaterialInstanceAssetLoader::ValidateAgainstParent(instance, parent);
    if (!validation.Succeeded()) {
        std::vector<std::string> diagnostics = MaterialInstanceValidationDiagnosticLines(validation);
        for (const std::string& diagnostic : diagnostics) {
            console_.Error("Materials", diagnostic);
        }
        materialEditor_.SetDiagnostics(std::move(diagnostics), true);
        return false;
    }

    const kb::render::RenderMaterialAssetData effective = kb::render::BuildEffectiveRenderMaterialInstanceAsset(parent, instance);
    materialEditor_.SetInstanceWorkingCopy(std::move(instance), effective);
    materialEditor_.ClearDiagnostics();
    SyncMaterialEditorWorkingCopyRuntimePreview();
    MarkSceneRenderDirty();
    console_.Info("Materials", "Edited material instance override '" + std::string{ stableId } + "'.");
    return true;
}

bool EditorSceneContext::ClearMaterialInstanceEditorGraphParameterOverride(
    kb::assets::AssetId id,
    std::string_view stableId,
    kb::render::RenderMaterialParameterType type) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.InstanceWorkingCopy().has_value()) {
        console_.Error("Materials", "Open the material instance in Material Editor before clearing overrides.");
        return false;
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (metadata == nullptr || metadata->type != "RenderMaterialInstance") {
        console_.Error("Materials", "Selected asset is not a Material Instance.");
        return false;
    }
    if (!materialEditor_.ClearInstanceParameterOverride(stableId, type)) {
        console_.Warning("Materials", "Material instance override could not be cleared.");
        return false;
    }
    materialEditor_.ClearDiagnostics();
    SyncMaterialEditorWorkingCopyRuntimePreview();
    MarkSceneRenderDirty();
    console_.Info("Materials", "Cleared material instance override '" + std::string{ stableId } + "'.");
    return true;
}

bool EditorSceneContext::SetMaterialInstanceEditorStaticParameterOverride(
    kb::assets::AssetId id,
    std::string_view stableId,
    kb::render::RenderMaterialGraphNodeKind nodeKind,
    std::string value) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.InstanceWorkingCopy().has_value()) {
        console_.Error("Materials", "Open the material instance in Material Editor before editing static overrides.");
        return false;
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (metadata == nullptr || metadata->type != "RenderMaterialInstance") {
        console_.Error("Materials", "Selected asset is not a Material Instance.");
        return false;
    }
    if (!materialEditor_.SetInstanceStaticParameterOverride(stableId, nodeKind, std::move(value))) {
        console_.Warning("Materials", "Material instance static override could not be edited.");
        return false;
    }
    materialEditor_.ClearDiagnostics();
    SyncMaterialEditorWorkingCopyRuntimePreview();
    MarkSceneRenderDirty();
    console_.Info("Materials", "Edited material instance static override '" + std::string{ stableId } + "'.");
    return true;
}

bool EditorSceneContext::SetMaterialInstanceEditorTextureParameterValue(
    kb::assets::AssetId id,
    std::string_view stableId,
    kb::assets::AssetId textureId) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.InstanceWorkingCopy().has_value()) {
        console_.Error("Materials", "Open the material instance in Material Editor before editing texture overrides.");
        return false;
    }
    if (stableId.empty()) {
        console_.Error("Materials", "Material instance texture override requires a stable parameter id.");
        return false;
    }
    if (textureId.IsValid()) {
        const kb::assets::AssetMetadata* texture = scene_->Assets().Manager().Registry().Find(textureId);
        if (texture == nullptr || !IsTextureAsset(*texture)) {
            console_.Error("Materials", "Material instance texture override rejected a non-texture asset.");
            return false;
        }
    }
    if (!materialEditor_.InstanceParentSnapshot().has_value()) {
        console_.Error("Materials", "Material instance parent document is not available for texture override validation.");
        return false;
    }

    kb::render::RenderMaterialInstanceAssetData instance = *materialEditor_.InstanceWorkingCopy();
    const kb::render::RenderMaterialAssetData& parent = *materialEditor_.InstanceParentSnapshot();
    EnsureMaterialInstanceOverrideDocument(instance, parent);
    RemoveGraphParameterValue(instance.overrides, stableId);
    UpsertGraphParameterValue(instance.overrides, kb::render::RenderMaterialGraphParameterValue{
        .stableId = std::string{ stableId },
        .type = kb::render::RenderMaterialParameterType::Texture,
        .assetId = textureId.value,
    });

    const kb::render::RenderMaterialInstanceValidationResult validation =
        kb::render::RenderMaterialInstanceAssetLoader::ValidateAgainstParent(instance, parent);
    if (!validation.Succeeded()) {
        std::vector<std::string> diagnostics = MaterialInstanceValidationDiagnosticLines(validation);
        for (const std::string& diagnostic : diagnostics) {
            console_.Error("Materials", diagnostic);
        }
        materialEditor_.SetDiagnostics(std::move(diagnostics), true);
        return false;
    }

    const kb::render::RenderMaterialAssetData effective = kb::render::BuildEffectiveRenderMaterialInstanceAsset(parent, instance);
    materialEditor_.SetInstanceWorkingCopy(std::move(instance), effective);
    materialEditor_.ClearDiagnostics();
    SyncMaterialEditorWorkingCopyRuntimePreview();
    MarkSceneRenderDirty();
    console_.Info("Materials", "Edited material instance texture override '" + std::string{ stableId } + "'.");
    return true;
}

bool EditorSceneContext::SetMaterialBaseColor(kb::assets::AssetId id, int channel, float value) {
    if (channel < 0 || channel >= 4) {
        return false;
    }
    return ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialBaseColorChannelEdit>(channel, value));
}

bool EditorSceneContext::SetMaterialEmissiveColor(kb::assets::AssetId id, int channel, float value) {
    if (channel < 0 || channel >= 3) {
        return false;
    }
    return ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialEmissiveColorChannelEdit>(channel, value));
}

bool EditorSceneContext::SetMaterialMetallicFactor(kb::assets::AssetId id, float value) {
    return ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialMetallicFactorEdit>(value));
}

bool EditorSceneContext::SetMaterialRoughnessFactor(kb::assets::AssetId id, float value) {
    return ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialRoughnessFactorEdit>(value));
}

bool EditorSceneContext::SetMaterialNormalScale(kb::assets::AssetId id, float value) {
    return ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialNormalScaleEdit>(value));
}

bool EditorSceneContext::SetMaterialOcclusionStrength(kb::assets::AssetId id, float value) {
    return ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialOcclusionStrengthEdit>(value));
}

bool EditorSceneContext::SetMaterialEmissiveStrength(kb::assets::AssetId id, float value) {
    return ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialEmissiveStrengthEdit>(value));
}

bool EditorSceneContext::SetMaterialAlphaCutoff(kb::assets::AssetId id, float value) {
    return ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialAlphaCutoffEdit>(value));
}

bool EditorSceneContext::SetMaterialAlphaMode(kb::assets::AssetId id, kb::render::RenderMaterialAlphaMode mode) {
    return ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialAlphaModeEdit>(mode));
}

bool EditorSceneContext::CycleMaterialAlphaMode(kb::assets::AssetId id) {
    const std::optional<kb::render::RenderMaterialAssetData> material = MaterialSourceForEdit(id);
    if (!material.has_value()) {
        console_.Error("Materials", "Material alpha mode could not be read.");
        return false;
    }
    return SetMaterialAlphaMode(id, NextAlphaMode(material->desc.alphaMode));
}

bool EditorSceneContext::ToggleMaterialDoubleSided(kb::assets::AssetId id) {
    const std::optional<kb::render::RenderMaterialAssetData> material = MaterialSourceForEdit(id);
    if (!material.has_value()) {
        console_.Error("Materials", "Material double-sided flag could not be read.");
        return false;
    }
    return ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialDoubleSidedEdit>(!material->desc.doubleSided));
}

bool EditorSceneContext::SetMaterialTextureAsset(kb::assets::AssetId id, EditorMaterialTextureSlot slot, kb::assets::AssetId textureId) {
    if (textureId.IsValid()) {
        const kb::assets::AssetMetadata* texture = scene_->Assets().Manager().Registry().Find(textureId);
        if (texture == nullptr || !IsTextureAsset(*texture)) {
            console_.Error("Materials", "Material texture slot rejected a non-texture asset.");
            return false;
        }
        const EditorMaterialTextureSlotValidationResult validation = EditorMaterialTextureSlotValidation::Validate(*texture, slot);
        if (!validation.accepted) {
            const std::string message = EditorMaterialTextureSlotValidation::RejectionMessage(*texture, validation);
            console_.Error("Materials", message);
            if (materialEditor_.OpenAssetId() == id) {
                materialEditor_.SetDiagnostics({ "texture_color_space_mismatch: " + message }, true);
            }
            return false;
        }
    }
    return ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialTextureAssetEdit>(slot, textureId));
}

bool EditorSceneContext::CycleMaterialTextureAsset(kb::assets::AssetId id, EditorMaterialTextureSlot slot) {
    const std::optional<kb::render::RenderMaterialAssetData> material = MaterialSourceForEdit(id);
    if (!material.has_value()) {
        console_.Error("Materials", "Material texture slot could not be read.");
        return false;
    }

    const std::vector<kb::assets::AssetId> textures = TextureAssetIds(scene_->Assets().Manager());
    return SetMaterialTextureAsset(id, slot, NextTextureAssetId(textures, MaterialTextureSlotValue(*material, slot)));
}

bool EditorSceneContext::SaveMaterialEditorAsset(kb::assets::AssetId id) {
    if (!id.IsValid()) {
        console_.Error("Materials", "No material asset is selected for Save.");
        return false;
    }
    if (inspector_.IsTextEditDirty() && IsMaterialFloatProperty(inspector_.EditedProperty())) {
        const std::optional<float> value = ParsePlainFloat(inspector_.EditBuffer());
        if (!value.has_value()) {
            console_.Error("Materials", "Material value edit could not be saved because the value is not a valid number.");
            return false;
        }
        std::unique_ptr<IEditorMaterialAssetPropertyEdit> edit = MaterialFloatEditForProperty(inspector_.EditedProperty(), *value);
        if (edit == nullptr || !ExecuteMaterialAssetEdit(id, std::move(edit))) {
            return false;
        }
        inspector_.EndTextEdit();
        return true;
    }
    if (HasActiveMaterialAssetEdit()) {
        if (activeMaterialEditAsset_ != id) {
            console_.Error("Materials", "Cannot save material while another material edit is active.");
            return false;
        }
        return CommitActiveMaterialAssetEdit();
    }
    if (materialEditor_.OpenAssetId() == id && materialEditor_.Dirty()) {
        if (materialEditor_.IsMaterialInstanceOpen()) {
            return CopyWorkingMaterialInstanceToSource(id);
        }
        return CopyWorkingMaterialToSource(id);
    }
    return ValidateMaterialEditorAsset(id);
}

bool EditorSceneContext::RevertMaterialEditorAsset(kb::assets::AssetId id) {
    if (!id.IsValid()) {
        console_.Error("Materials", "No material asset is selected for Revert.");
        return false;
    }
    if (inspector_.IsTextEditDirty() && IsMaterialFloatProperty(inspector_.EditedProperty())) {
        inspector_.EndTextEdit();
        if (materialEditor_.OpenAssetId() == id) {
            materialEditor_.RevertToCleanSnapshot();
        }
        console_.Info("Materials", "Material value edit reverted.");
        return true;
    }
    if (!HasActiveMaterialAssetEdit()) {
        if (materialEditor_.OpenAssetId() == id && materialEditor_.Dirty()) {
            materialEditor_.RevertToCleanSnapshot();
            ClearMaterialEditorWorkingCopyRuntimePreview();
            MarkSceneRenderDirty();
            console_.Info("Materials", "Material working copy reverted.");
            return true;
        }
        console_.Info("Materials", "Material has no pending edit to revert.");
        return true;
    }
    if (activeMaterialEditAsset_ != id) {
        console_.Error("Materials", "Cannot revert material while another material edit is active.");
        return false;
    }
    CancelActiveMaterialAssetEdit();
    console_.Info("Materials", "Material edit reverted.");
    return true;
}

bool EditorSceneContext::ValidateMaterialEditorAsset(kb::assets::AssetId id) {
    if (!id.IsValid()) {
        console_.Error("Materials", "No material asset is selected for Validate.");
        return false;
    }

    const kb::assets::AssetManager& manager = scene_->Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr || (metadata->type != "RenderMaterial" && metadata->type != "RenderMaterialInstance")) {
        console_.Error("Materials", "Selected asset is not a Material document.");
        return false;
    }

    if (metadata->type == "RenderMaterialInstance") {
        std::optional<kb::render::RenderMaterialInstanceAssetData> instance =
            materialEditor_.OpenAssetId() == id && materialEditor_.InstanceWorkingCopy().has_value()
                ? materialEditor_.InstanceWorkingCopy()
                : ReadMaterialInstanceAsset(id);
        if (!instance.has_value() || !instance->parentMaterialAssetId.IsValid()) {
            console_.Error("Materials", "Material instance could not be read or has no parent material.");
            return false;
        }
        const std::optional<kb::render::RenderMaterialAssetData> parent = ReadEffectiveMaterialAsset(instance->parentMaterialAssetId);
        if (!parent.has_value()) {
            console_.Error("Materials", "Material instance parent material could not be read.");
            return false;
        }
        const kb::render::RenderMaterialInstanceValidationResult validation =
            kb::render::RenderMaterialInstanceAssetLoader::ValidateAgainstParent(*instance, *parent);
        std::vector<std::string> diagnostics = MaterialInstanceValidationDiagnosticLines(validation);
        for (const std::string& diagnostic : diagnostics) {
            console_.Error("Materials", diagnostic);
        }
        if (materialEditor_.OpenAssetId() == id) {
            materialEditor_.SetDiagnostics(std::move(diagnostics), !validation.Succeeded());
        }
        if (validation.Succeeded()) {
            console_.Info("Materials", "Material instance validated: " + metadata->virtualPath.generic_string());
        }
        return validation.Succeeded();
    }

    kb::render::RenderMaterialAssetParseResult result{};
    if (materialEditor_.OpenAssetId() == id && materialEditor_.WorkingCopy().has_value()) {
        std::ostringstream serialized;
        kb::render::RenderMaterialAssetWriter::Write(serialized, *materialEditor_.WorkingCopy());
        std::istringstream input{ serialized.str() };
        result = kb::render::RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input, kb::render::RenderMaterialAssetParseSourceContext{ .assetId = id });
    } else {
        const std::filesystem::path path = metadata->physicalPath.empty()
            ? manager.Mounts().Resolve(metadata->virtualPath).value_or(std::filesystem::path{})
            : metadata->physicalPath;
        if (path.empty()) {
            console_.Error("Materials", "Material asset path could not be resolved: " + metadata->virtualPath.generic_string());
            return false;
        }
        result = kb::render::RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(path, id);
    }
    std::vector<std::string> diagnostics;
    bool hasError = false;
    for (const kb::render::RenderMaterialAssetParseDiagnostic& diagnostic : result.diagnostics) {
        std::string message = std::string{ kb::render::RenderMaterialAssetParseDiagnosticCodeName(diagnostic.code) } + ": " + diagnostic.message;
        if (!diagnostic.field.empty()) {
            message += " [" + diagnostic.field + "]";
        }
        diagnostics.push_back(message);
        if (diagnostic.severity == kb::render::RenderMaterialAssetParseDiagnosticSeverity::Warning) {
            console_.Warning("Materials", message);
        } else {
            hasError = true;
            console_.Error("Materials", message);
        }
    }

    const std::size_t runtimeValidationDiagnosticStart = diagnostics.size();
    if (result.asset.has_value()) {
        AppendMaterialTypeReferenceValidationDiagnostics(
            manager,
            *metadata,
            *result.asset,
            diagnostics,
            hasError);
        AppendMaterialTextureValidationDiagnostics(
            manager,
            *result.asset,
            MaterialEditorSchemaForMaterial(manager, *result.asset),
            diagnostics,
            hasError);
    }
    for (std::size_t index = runtimeValidationDiagnosticStart; index < diagnostics.size(); ++index) {
        if (diagnostics[index].find("texture_invalid_asset:") == 0U || diagnostics[index].find("material_type_reference:") == 0U) {
            console_.Error("Materials", diagnostics[index]);
        } else {
            console_.Warning("Materials", diagnostics[index]);
        }
    }

    if (materialEditor_.OpenAssetId() == id) {
        materialEditor_.SetDiagnostics(std::move(diagnostics), hasError);
    }
    if (result.Succeeded() && !hasError) {
        console_.Info("Materials", "Material validated: " + metadata->virtualPath.generic_string());
    }
    return result.Succeeded() && !hasError;
}

bool EditorSceneContext::BeginMaterialAssetFloatEdit(kb::assets::AssetId id, InspectorPropertyId property) {
    if (HasActiveMaterialAssetEdit() || !IsMaterialFloatProperty(property)) {
        return false;
    }
    std::optional<kb::render::RenderMaterialAssetData> material = MaterialSourceForEdit(id);
    if (!material.has_value()) {
        console_.Error("Materials", "Material drag edit could not read the material asset.");
        return false;
    }

    activeMaterialEditAsset_ = id;
    activeMaterialEditProperty_ = property;
    activeMaterialEditBefore_ = std::move(material);
    return true;
}

bool EditorSceneContext::ApplyActiveMaterialAssetFloatEdit(float value) {
    if (!HasActiveMaterialAssetEdit()) {
        return false;
    }

    std::optional<kb::render::RenderMaterialAssetData> current = MaterialSourceForEdit(activeMaterialEditAsset_);
    if (!current.has_value()) {
        return false;
    }
    std::unique_ptr<IEditorMaterialAssetPropertyEdit> edit = MaterialFloatEditForProperty(activeMaterialEditProperty_, value);
    if (edit == nullptr) {
        return false;
    }
    edit->Apply(*current);
    if (materialEditor_.OpenAssetId() == activeMaterialEditAsset_) {
        materialEditor_.SetWorkingCopy(*current);
        SyncMaterialEditorWorkingCopyRuntimePreview();
    } else if (!EditorMaterialAssetGateway::WriteExisting(*scene_, activeMaterialEditAsset_, *current)) {
        return false;
    }

    MarkSceneRenderDirty();
    return true;
}

bool EditorSceneContext::CommitActiveMaterialAssetEdit() {
    if (!HasActiveMaterialAssetEdit()) {
        return false;
    }

    const kb::assets::AssetId editedAsset = activeMaterialEditAsset_;
    std::optional<kb::render::RenderMaterialAssetData> after = MaterialSourceForEdit(editedAsset);
    if (!after.has_value()) {
        CancelActiveMaterialAssetEdit();
        return false;
    }
    const kb::render::RenderMaterialAssetData committed = *after;
    if (materialEditor_.OpenAssetId() == editedAsset) {
        ClearMaterialEditorWorkingCopyRuntimePreview();
    }

    std::unique_ptr<EditorMaterialAssetEditCommand> command = EditorMaterialAssetEditCommand::CreateRecorded(
        *scene_,
        editedAsset,
        "Edit Material",
        std::move(*activeMaterialEditBefore_),
        committed);
    activeMaterialEditAsset_ = {};
    activeMaterialEditProperty_ = InspectorPropertyId::None;
    activeMaterialEditBefore_.reset();
    if (!commandStack_.Execute(std::move(command))) {
        console_.Warning("Materials", "Material edit commit failed.");
        return false;
    }
    if (materialEditor_.OpenAssetId() == editedAsset) {
        materialEditor_.SetWorkingCopy(committed);
        materialEditor_.MarkSaved();
    }
    MarkSceneRenderDirty();
    return true;
}

void EditorSceneContext::CancelActiveMaterialAssetEdit() noexcept {
    if (!HasActiveMaterialAssetEdit()) {
        return;
    }
    if (materialEditor_.OpenAssetId() == activeMaterialEditAsset_) {
        materialEditor_.SetWorkingCopy(*activeMaterialEditBefore_);
        SyncMaterialEditorWorkingCopyRuntimePreview();
    } else {
        static_cast<void>(EditorMaterialAssetGateway::WriteExisting(*scene_, activeMaterialEditAsset_, *activeMaterialEditBefore_));
    }
    activeMaterialEditAsset_ = {};
    activeMaterialEditProperty_ = InspectorPropertyId::None;
    activeMaterialEditBefore_.reset();
    MarkSceneRenderDirty();
}

bool EditorSceneContext::HasActiveMaterialAssetEdit() const noexcept {
    return activeMaterialEditAsset_.IsValid() && activeMaterialEditBefore_.has_value() && IsMaterialFloatProperty(activeMaterialEditProperty_);
}

bool EditorSceneContext::ToggleProjectInputEnabled() {
    project_.inputEnabled = !project_.inputEnabled;
    return SaveProjectDescriptor();
}

std::vector<std::string> EditorSceneContext::ProjectInputMappingContextOptions() const {
    // Empty first entry is the "(None)" choice so the project input can be cleared.
    std::vector<std::string> options{ std::string{} };
    for (const kb::assets::AssetMetadata& metadata : scene_->Assets().Manager().Registry().All()) {
        if (metadata.type == "InputMappingContext") {
            options.push_back(kb::assets::NormalizeAssetPath(metadata.virtualPath));
        }
    }
    std::sort(options.begin() + 1, options.end());
    return options;
}

bool EditorSceneContext::SetProjectInputMappingContext(std::string virtualPath) {
    if (project_.inputMappingContext == virtualPath) {
        return false;
    }
    project_.inputMappingContext = std::move(virtualPath);
    return SaveProjectDescriptor();
}

bool EditorSceneContext::CloseProjectSettingsDropdowns() noexcept {
    return projectSettings_.CloseDropdowns();
}

bool EditorSceneContext::IsProjectPluginEnabled(std::string_view pluginId) const noexcept {
    const auto iter = std::ranges::find_if(project_.plugins, [pluginId](const kb::project::ProjectPluginReference& plugin) {
        return plugin.name == pluginId;
    });
    return iter != project_.plugins.end() && iter->enabled;
}

std::string EditorSceneContext::ProjectPluginBinaryPath(std::string_view pluginId) const {
    const auto iter = std::ranges::find_if(project_.plugins, [pluginId](const kb::project::ProjectPluginReference& plugin) {
        return plugin.name == pluginId;
    });
    return iter == project_.plugins.end() ? std::string{} : iter->binaryPath;
}

bool EditorSceneContext::ToggleProjectPlugin(std::size_t catalogIndex) {
    const EditorPluginDescriptor* descriptor = EditorPluginCatalog::At(catalogIndex);
    if (descriptor == nullptr) {
        return false;
    }

    auto iter = std::ranges::find_if(project_.plugins, [descriptor](const kb::project::ProjectPluginReference& plugin) {
        return plugin.name == descriptor->id;
    });
    if (iter == project_.plugins.end()) {
        project_.plugins.push_back(kb::project::ProjectPluginReference{
            .name = std::string{ descriptor->id },
            .binaryPath = EditorPluginCatalog::PersistentBinaryPath(descriptor->id),
            .enabled = true,
        });
        console_.Info("Plugins", std::string{ descriptor->displayName } + " enabled. Reopen the scene or enter play mode after reload to apply.");
        const bool saved = SaveProjectDescriptor();
        if (saved) {
            plugins_.MarkPendingReload();
        }
        return saved;
    }

    iter->enabled = !iter->enabled;
    if (iter->binaryPath.empty()) {
        iter->binaryPath = EditorPluginCatalog::PersistentBinaryPath(descriptor->id);
    }
    console_.Info("Plugins", std::string{ descriptor->displayName } + (iter->enabled ? " enabled." : " disabled.") + " Project plugin changes apply when the scene/module host is rebuilt.");
    const bool saved = SaveProjectDescriptor();
    if (saved) {
        plugins_.MarkPendingReload();
    }
    return saved;
}

void EditorSceneContext::ActivateProjectInput() {
    if (!project_.inputEnabled || project_.inputMappingContext.empty()) {
        return;
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().FindByPath(project_.inputMappingContext);
    if (metadata != nullptr && metadata->type == "InputMappingContext") {
        static_cast<void>(scene_->Input().AddMappingContext(metadata->id.value, 0));
    }
}

bool EditorSceneContext::SaveProjectDescriptor() {
    if (projectFile_.empty()) {
        return false;
    }
    const bool saved = kb::project::ProjectDescriptorWriter::Write(projectFile_, project_);
    if (saved) {
        console_.Info("Project", "Project settings saved.");
    } else {
        console_.Error("Project", "Project settings could not be saved.");
    }
    return saved;
}

std::optional<kb::input::InputMappingContextAsset> EditorSceneContext::ReadInputMappingContextAsset(kb::assets::AssetId id) const {
    return EditorInputAssetGateway::ReadContext(*scene_, id);
}

bool EditorSceneContext::AddInputMapping(kb::assets::AssetId id) {
    return InputMappingContextAuthoring().AddMapping(id);
}

bool EditorSceneContext::RemoveInputMapping(kb::assets::AssetId id, std::size_t index) {
    return InputMappingContextAuthoring().RemoveMapping(id, index);
}

bool EditorSceneContext::SetInputMappingKey(kb::assets::AssetId id, std::size_t index, kb::input::InputKey key) {
    return InputMappingContextAuthoring().SetMappingKey(id, index, key);
}

bool EditorSceneContext::SetInputMappingScale(kb::assets::AssetId id, std::size_t index, float scale) {
    return InputMappingContextAuthoring().SetMappingScale(id, index, scale);
}

bool EditorSceneContext::CycleInputMappingAction(kb::assets::AssetId id, std::size_t index) {
    return InputMappingContextAuthoring().CycleMappingAction(id, index);
}

bool EditorSceneContext::CycleInputMappingTrigger(kb::assets::AssetId id, std::size_t index) {
    return InputMappingContextAuthoring().CycleMappingTrigger(id, index);
}

bool EditorSceneContext::InstantiatePrefabAsset(const std::filesystem::path& path, kb::scene::SceneEntity parent) {
    return InstantiatePrefabAsset(path, {}, parent);
}

bool EditorSceneContext::InstantiatePrefabAsset(const std::filesystem::path& path, const std::filesystem::path& virtualPath, kb::scene::SceneEntity parent) {
    if (pendingSceneTransactionLabel_.has_value()) {
        console_.Warning("Edit", "Scene command ignored while another scene transaction is active.");
        return false;
    }

    const std::filesystem::path& displayPath = virtualPath.empty() ? path : virtualPath;
    const std::optional<kb::scene::SceneEntity> root = EditorScenePrefabActions::InstantiateAsset(*scene_, path, virtualPath, parent);
    if (!root.has_value() || !scene_->Entities().IsAlive(*root)) {
        console_.Error("Prefabs", "Prefab instantiation failed: " + displayPath.generic_string());
        return false;
    }

    const std::array<kb::scene::SceneEntity, 1U> created{ *root };
    if (!AdoptCreatedHierarchyEntities("Instantiate Prefab", created)) {
        scene_->Entities().Destroy(*root);
        console_.Error("Prefabs", "Prefab instantiation failed: " + displayPath.generic_string());
        return false;
    }

    console_.Info("Prefabs", "Prefab instantiated: " + displayPath.generic_string());
    return true;
}

bool EditorSceneContext::InstantiatePrefabAssetAt(
    const std::filesystem::path& path,
    const std::filesystem::path& virtualPath,
    kb::scene::Vec3 position) {
    if (pendingSceneTransactionLabel_.has_value()) {
        console_.Warning("Edit", "Scene command ignored while another scene transaction is active.");
        return false;
    }

    const std::optional<kb::scene::SceneEntity> root = EditorScenePrefabActions::InstantiateAsset(*scene_, path, virtualPath, {});
    if (!root.has_value() || !scene_->Entities().IsAlive(*root)) {
        console_.Error("Prefabs", "Prefab instantiation failed: " + (virtualPath.empty() ? path.generic_string() : virtualPath.generic_string()));
        return false;
    }

    kb::scene::TransformComponent transform = scene_->Transforms().Get(*root);
    transform.localPosition = position;
    scene_->Transforms().Set(*root, transform);
    const std::array<kb::scene::SceneEntity, 1U> created{ *root };
    if (!AdoptCreatedHierarchyEntities("Instantiate Prefab", created)) {
        scene_->Entities().Destroy(*root);
        console_.Error("Prefabs", "Prefab instantiation failed: " + (virtualPath.empty() ? path.generic_string() : virtualPath.generic_string()));
        return false;
    }

    console_.Info("Prefabs", "Prefab instantiated: " + (virtualPath.empty() ? path.generic_string() : virtualPath.generic_string()));
    return true;
}

kb::scene::SceneEntity EditorSceneContext::CreateMeshAssetEntity(kb::assets::AssetId assetId) {
    return CreateMeshAssetEntity(assetId, {}, true);
}

kb::scene::SceneEntity EditorSceneContext::CreateMeshAssetEntity(kb::assets::AssetId assetId, kb::scene::Vec3 position, bool logCreation) {
    if (!assetId.IsValid()) {
        console_.Warning("Assets", "Mesh entity creation ignored for invalid asset.");
        return {};
    }

    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(assetId);
    if (metadata == nullptr) {
        console_.Error("Assets", "Mesh asset metadata was not found.");
        return {};
    }
    if (metadata->importCategory != "Mesh" || metadata->type != "RenderMesh") {
        console_.Warning("Assets", "Only imported mesh assets can be placed on the scene.");
        return {};
    }

    kb::scene::SceneEntity entity{};
    if (!logCreation) {
        entity = EditorSceneMeshAssetActions::CreateMeshEntity(*scene_, assetId, metadata->name, position);
        if (!entity.IsValid()) {
            console_.Error("Assets", "Mesh entity could not be created: " + metadata->name);
            return {};
        }
        SelectEntity(entity);
        MarkSceneRenderDirty();
        return entity;
    }

    const bool created = ExecuteSceneCommand("Create Mesh Entity", [this, assetId, position, metadata, &entity]() {
        entity = EditorSceneMeshAssetActions::CreateMeshEntity(*scene_, assetId, metadata->name, position);
        if (!entity.IsValid()) {
            return false;
        }
        SelectEntity(entity);
        return true;
    });
    if (!entity.IsValid()) {
        console_.Error("Assets", "Mesh entity could not be created: " + metadata->name);
        return {};
    }

    if (created && logCreation) {
        console_.Info("Assets", "Mesh entity created: " + metadata->name);
    }
    return entity;
}

bool EditorSceneContext::AddBehaviourAssetToEntity(kb::assets::AssetId assetId, kb::scene::SceneEntity entity) {
    if (!assetId.IsValid() || !entity.IsValid() || !scene_->Entities().IsAlive(entity)) {
        console_.Warning("Scripts", "Behaviour asset assignment ignored for invalid target.");
        return false;
    }

    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(assetId);
    if (metadata == nullptr) {
        console_.Error("Scripts", "Behaviour asset metadata was not found.");
        return false;
    }

    const std::optional<kb::scene::BehaviourComponent> behaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*metadata);
    if (!behaviour.has_value()) {
        console_.Error("Scripts", "Behaviour component could not be created from asset: " + metadata->name);
        return false;
    }

    if (!ExecuteSceneCommand("Assign Behaviour", [this, entity, behaviour]() {
            if (!scene_->Entities().IsAlive(entity)) {
                return false;
            }
            scene_->Components().Behaviours().Set(entity, *behaviour);
            SelectEntity(entity);
            return true;
        })) {
        console_.Error("Scripts", "Behaviour asset assignment failed: " + metadata->name);
        return false;
    }
    console_.Info("Scripts", "Behaviour asset assigned: " + metadata->name);
    return true;
}

bool EditorSceneContext::SetMeshRendererMeshAsset(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) {
    if (!entity.IsValid() || !scene_->Entities().IsAlive(entity)) {
        console_.Warning("Inspector", "Mesh assignment ignored for invalid entity.");
        return false;
    }
    if (!scene_->Components().MeshRenderers().Has(entity)) {
        console_.Warning("Inspector", "Selected entity does not have a Mesh Renderer component.");
        return false;
    }
    if (assetId.IsValid()) {
        const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(assetId);
        if (metadata == nullptr || !EditorSceneMeshAssetActions::IsMeshAsset(*metadata)) {
            console_.Warning("Inspector", "Only mesh assets can be assigned to a Mesh Renderer.");
            return false;
        }
    }

    return ExecuteSceneCommand(assetId.IsValid() ? "Assign Mesh" : "Clear Mesh", [this, entity, assetId]() {
        return EditorSceneMeshAssetActions::AssignMesh(*scene_, entity, assetId);
    });
}

bool EditorSceneContext::SetMeshRendererMaterialAsset(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) {
    if (!entity.IsValid() || !scene_->Entities().IsAlive(entity)) {
        console_.Warning("Inspector", "Material assignment ignored for invalid entity.");
        return false;
    }
    if (!scene_->Components().MeshRenderers().Has(entity)) {
        console_.Warning("Inspector", "Selected entity does not have a Mesh Renderer component.");
        return false;
    }
    if (assetId.IsValid()) {
        const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(assetId);
        if (metadata != nullptr && EditorSceneMaterialAssetActions::IsMaterialGraphAsset(*metadata)) {
            console_.Warning("Inspector", EditorSceneMaterialAssetActions::MaterialGraphAssignmentRejectionMessage());
            return false;
        }
        if (metadata == nullptr || !EditorSceneMaterialAssetActions::IsMaterialAsset(*metadata)) {
            console_.Warning("Inspector", "Only material assets can be assigned to a Mesh Renderer.");
            return false;
        }
    }

    return ExecuteSceneCommand(assetId.IsValid() ? "Assign Mesh Material" : "Clear Mesh Material", [this, entity, assetId]() {
        return EditorSceneMaterialAssetActions::AssignMaterialToAllSlots(*scene_, entity, assetId);
    });
}

bool EditorSceneContext::ApplyMaterialToSelectedMeshRenderers(kb::assets::AssetId assetId) {
    if (!assetId.IsValid()) {
        console_.Warning("Material Editor", "Apply To Selection requires a material asset.");
        return false;
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(assetId);
    if (metadata == nullptr || !EditorSceneMaterialAssetActions::IsMaterialAsset(*metadata)) {
        console_.Warning("Material Editor", "Apply To Selection can only assign material assets.");
        return false;
    }

    std::vector<kb::scene::SceneEntity> targets;
    targets.reserve(SelectedHierarchyEntities().size());
    for (const kb::scene::SceneEntity entity : SelectedHierarchyEntities()) {
        if (scene_->Entities().IsAlive(entity) && scene_->Components().MeshRenderers().Has(entity)) {
            targets.push_back(entity);
        }
    }
    if (targets.empty()) {
        console_.Warning("Material Editor", "Apply To Selection found no selected Mesh Renderer.");
        return false;
    }

    const bool applied = ExecuteSceneCommand("Apply Material To Selection", [this, targets = std::move(targets), assetId]() {
        bool assigned = false;
        for (const kb::scene::SceneEntity entity : targets) {
            assigned = EditorSceneMaterialAssetActions::AssignMaterialToAllSlots(*scene_, entity, assetId) || assigned;
        }
        return assigned;
    });
    if (applied) {
        static_cast<void>(assetBrowser_.SelectAsset(assetId, scene_->Assets().Manager()));
    }
    return applied;
}

bool EditorSceneContext::CycleMeshRendererMaterialAsset(kb::scene::SceneEntity entity) {
    const kb::scene::MeshRendererComponent* renderer = scene_->Components().MeshRenderers().TryGet(entity);
    if (renderer == nullptr) {
        return false;
    }
    const std::vector<kb::assets::AssetId> materials = MaterialAssetIds(scene_->Assets().Manager());
    return SetMeshRendererMaterialAsset(entity, NextMaterialAssetId(materials, renderer->materialAssetId));
}

bool EditorSceneContext::SetMeshRendererMaterialSlotAsset(kb::scene::SceneEntity entity, std::uint32_t slotIndex, kb::assets::AssetId assetId) {
    if (!entity.IsValid() || !scene_->Entities().IsAlive(entity)) {
        console_.Warning("Inspector", "Material slot assignment ignored for invalid entity.");
        return false;
    }
    if (slotIndex >= kb::scene::kMaxMeshRendererMaterialSlotOverrides) {
        console_.Warning("Inspector", "Material slot assignment ignored for invalid slot.");
        return false;
    }
    if (!scene_->Components().MeshRenderers().Has(entity)) {
        console_.Warning("Inspector", "Selected entity does not have a Mesh Renderer component.");
        return false;
    }
    if (assetId.IsValid()) {
        const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(assetId);
        if (metadata != nullptr && EditorSceneMaterialAssetActions::IsMaterialGraphAsset(*metadata)) {
            console_.Warning("Inspector", EditorSceneMaterialAssetActions::MaterialGraphAssignmentRejectionMessage());
            return false;
        }
        if (metadata == nullptr || !EditorSceneMaterialAssetActions::IsMaterialAsset(*metadata)) {
            console_.Warning("Inspector", "Only material assets can be assigned to a Mesh Renderer slot.");
            return false;
        }
    }

    return ExecuteSceneCommand(assetId.IsValid() ? "Assign Mesh Material Slot" : "Clear Mesh Material Slot", [this, entity, slotIndex, assetId]() {
        return EditorSceneMaterialAssetActions::AssignMaterialSlotOverride(*scene_, entity, slotIndex, assetId);
    });
}

bool EditorSceneContext::CycleMeshRendererMaterialSlotAsset(kb::scene::SceneEntity entity, std::uint32_t slotIndex) {
    const kb::scene::MeshRendererComponent* renderer = scene_->Components().MeshRenderers().TryGet(entity);
    if (renderer == nullptr || slotIndex >= kb::scene::kMaxMeshRendererMaterialSlotOverrides) {
        return false;
    }
    const std::uint64_t current = slotIndex < renderer->materialSlotOverrideCount ? renderer->materialSlotAssetIds[slotIndex] : 0U;
    if (current != 0U) {
        return SetMeshRendererMaterialSlotAsset(entity, slotIndex, {});
    }
    const std::vector<kb::assets::AssetId> materials = MaterialAssetIds(scene_->Assets().Manager());
    return SetMeshRendererMaterialSlotAsset(entity, slotIndex, NextMaterialAssetId(materials, current));
}

bool EditorSceneContext::HasEntityScript(kb::scene::SceneEntity entity) const {
    return entity.IsValid() && scene_->Components().Behaviours().Has(entity);
}

std::string EditorSceneContext::EntityScriptName(kb::scene::SceneEntity entity) const {
    const kb::scene::BehaviourComponent* behaviour = scene_->Components().Behaviours().TryGet(entity);
    if (behaviour == nullptr) {
        return {};
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(kb::assets::AssetId{ behaviour->behaviourAssetId });
    if (metadata == nullptr) {
        return "(missing script)";
    }
    return metadata->name.empty() ? metadata->virtualPath.filename().string() : metadata->name;
}

bool EditorSceneContext::EntityScriptEnabled(kb::scene::SceneEntity entity) const {
    const kb::scene::BehaviourComponent* behaviour = scene_->Components().Behaviours().TryGet(entity);
    return behaviour != nullptr && behaviour->enabled;
}

std::vector<std::pair<kb::assets::AssetId, std::string>> EditorSceneContext::AvailableScriptAssets() const {
    std::vector<std::pair<kb::assets::AssetId, std::string>> scripts;
    for (const kb::assets::AssetMetadata& metadata : scene_->Assets().Manager().Registry().All()) {
        if (kb::script::ScriptBehaviourAsset::IsBehaviourAsset(metadata)) {
            scripts.emplace_back(metadata.id, metadata.name.empty() ? metadata.virtualPath.filename().string() : metadata.name);
        }
    }
    std::sort(scripts.begin(), scripts.end(), [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; });
    return scripts;
}

bool EditorSceneContext::AttachScriptToEntity(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) {
    return AddBehaviourAssetToEntity(assetId, entity);
}

bool EditorSceneContext::RemoveScriptFromEntity(kb::scene::SceneEntity entity) {
    if (!entity.IsValid() || !scene_->Components().Behaviours().Has(entity)) {
        return false;
    }
    return ExecuteSceneCommand("Remove Component", [this, entity]() {
        if (!scene_->Entities().IsAlive(entity)) {
            return false;
        }
        scene_->Components().Behaviours().Remove(entity);
        return true;
    });
}

bool EditorSceneContext::AddComponentToEntity(kb::scene::SceneEntity entity, std::string_view componentId) {
    if (!entity.IsValid() || !scene_->Entities().IsAlive(entity)) {
        console_.Warning("Inspector", "Component add ignored for invalid entity.");
        return false;
    }

    if (componentId == "Camera") {
        if (scene_->Components().Cameras().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Camera component.");
            return false;
        }
        return ExecuteSceneCommand("Add Camera Component", [this, entity]() {
            scene_->Components().Cameras().Set(entity, kb::scene::CameraComponent{});
            return true;
        });
    }
    if (componentId == "Light") {
        if (scene_->Components().Lights().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Light component.");
            return false;
        }
        return ExecuteSceneCommand("Add Light Component", [this, entity]() {
            scene_->Components().Lights().Set(entity, kb::scene::LightComponent{});
            return true;
        });
    }
    if (componentId == "MeshRenderer") {
        if (scene_->Components().MeshRenderers().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Mesh Renderer component.");
            return false;
        }
        return ExecuteSceneCommand("Add Mesh Renderer Component", [this, entity]() {
            scene_->Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{});
            return true;
        });
    }
    if (componentId == "AudioSource") {
        if (scene_->Components().AudioSources().Has(entity)) {
            console_.Warning("Inspector", "Entity already has an Audio Source component.");
            return false;
        }
        return ExecuteSceneCommand("Add Audio Source Component", [this, entity]() {
            scene_->Components().AudioSources().Set(entity, kb::scene::AudioSourceComponent{});
            return true;
        });
    }
    if (componentId == "AudioListener") {
        if (scene_->Components().AudioListeners().Has(entity)) {
            console_.Warning("Inspector", "Entity already has an Audio Listener component.");
            return false;
        }
        return ExecuteSceneCommand("Add Audio Listener Component", [this, entity]() {
            scene_->Components().AudioListeners().Set(entity, kb::scene::AudioListenerComponent{});
            return true;
        });
    }

    console_.Warning("Inspector", "Unknown component: " + std::string{ componentId });
    return false;
}

bool EditorSceneContext::SetAudioSourceClipAsset(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) {
    if (!entity.IsValid() || !assetId.IsValid()) {
        return false;
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(assetId);
    if (metadata == nullptr || !EditorSceneAudioAssetActions::IsAudioAsset(*metadata)) {
        console_.Warning("Inspector", "Only audio assets can be assigned to an Audio Source.");
        return false;
    }
    if (!scene_->Components().AudioSources().Has(entity)) {
        console_.Warning("Inspector", "Selected entity does not have an Audio Source component.");
        return false;
    }
    return ExecuteSceneCommand("Assign Audio Clip", [this, entity, assetId]() {
        return EditorSceneAudioAssetActions::AssignAudioClip(*scene_, entity, assetId);
    });
}

bool EditorSceneContext::ToggleEntityScriptEnabled(kb::scene::SceneEntity entity) {
    if (!entity.IsValid() || !scene_->Components().Behaviours().Has(entity)) {
        return false;
    }
    return ExecuteSceneCommand("Toggle Script Enabled", [this, entity]() {
        kb::scene::BehaviourComponent* behaviour = scene_->Components().Behaviours().TryGet(entity);
        if (behaviour == nullptr) {
            return false;
        }
        behaviour->enabled = !behaviour->enabled;
        scene_->Components().Behaviours().MarkModified(entity);
        return true;
    });
}

bool EditorSceneContext::BeginSelectedTransformEdit(std::string label) {
    if (activeTransformEdit_.Active()) {
        return false;
    }

    const kb::scene::SceneEntity primary = SelectedEntity();
    if (!scene_->Entities().IsAlive(primary)) {
        return false;
    }

    std::vector<kb::scene::SceneEntity> editing = TopLevelSelectedEntities(*scene_, hierarchySelection_.SelectedEntities());
    if (editing.empty()) {
        editing.push_back(primary);
    } else if (!ContainsEntity(editing, primary)) {
        editing.clear();
        editing.push_back(primary);
    }

    std::vector<EditorSceneObjectTransformChange> changes = EditorSceneTransformSnapshotBuilder::Capture(*scene_, editing);
    if (changes.empty()) {
        return false;
    }

    const kb::scene::Vec3 targetStart = EditorSceneSelectionPivot::Resolve(
        *scene_,
        hierarchySelection_.SelectedEntities(),
        primary).value_or(scene_->Transforms().Get(primary).localPosition);
    activeTransformEdit_.Begin(std::move(label), primary, targetStart, std::move(changes));
    return true;
}

bool EditorSceneContext::ApplyActiveTransformEditPrimaryPosition(kb::scene::Vec3 position) {
    const EditorSceneTransformEditApplyResult result =
        EditorSceneTransformEditController{ *scene_, activeTransformEdit_ }.ApplyPrimaryPosition(position);
    if (result.changed) {
        MarkSceneEntitiesRenderDirty(result.touched);
    }
    return result.changed;
}

bool EditorSceneContext::ApplyActiveTransformEditPrimaryRotation(kb::scene::Vec3 rotation) {
    const EditorSceneTransformEditApplyResult result =
        EditorSceneTransformEditController{ *scene_, activeTransformEdit_ }.ApplyPrimaryRotation(rotation);
    if (result.changed) {
        MarkSceneEntitiesRenderDirty(result.touched);
    }
    return result.changed;
}

bool EditorSceneContext::ApplyActiveTransformEditRotationDelta(kb::scene::Quat delta) {
    const EditorSceneTransformEditApplyResult result =
        EditorSceneTransformEditController{ *scene_, activeTransformEdit_ }.ApplyRotationDelta(delta);
    if (result.changed) {
        MarkSceneEntitiesRenderDirty(result.touched);
    }
    return result.changed;
}

bool EditorSceneContext::ApplyActiveTransformEditPrimaryScale(kb::scene::Vec3 scale) {
    const EditorSceneTransformEditApplyResult result =
        EditorSceneTransformEditController{ *scene_, activeTransformEdit_ }.ApplyPrimaryScale(scale);
    if (result.changed) {
        MarkSceneEntitiesRenderDirty(result.touched);
    }
    return result.changed;
}

bool EditorSceneContext::ApplyActiveTransformEditProperty(InspectorPropertyId property, float value) {
    const EditorSceneTransformEditApplyResult result =
        EditorSceneTransformEditController{ *scene_, activeTransformEdit_ }.ApplyProperty(property, value);
    if (result.changed) {
        MarkSceneEntitiesRenderDirty(result.touched);
    }
    return result.changed;
}

float EditorSceneContext::ActiveTransformEditPropertyStart(InspectorPropertyId property) const noexcept {
    return EditorSceneTransformEditController::PropertyStart(activeTransformEdit_, property);
}

bool EditorSceneContext::CommitActiveTransformEdit() {
    if (!activeTransformEdit_.Active()) {
        activeTransformEdit_.Clear();
        return false;
    }

    std::vector<EditorSceneObjectTransformChange> committed = EditorSceneTransformCommitBuilder::Build(*scene_, activeTransformEdit_);
    const std::string label = activeTransformEdit_.LabelOrDefault();
    activeTransformEdit_.Clear();
    if (committed.empty()) {
        return false;
    }

    const std::vector<kb::scene::SceneEntity> touched = EditorSceneTransformCommitBuilder::TouchedEntities(committed);
    commandStack_.PushExecuted(std::make_unique<EditorSceneTransformDeltaCommand>(*this, label, std::move(committed)));
    MarkSceneEntitiesRenderDirty(touched);
    MarkSceneDocumentDirty();
    scene_->Runtime().SynchronizeTransforms();
    return true;
}

void EditorSceneContext::CancelActiveTransformEdit() noexcept {
    if (!activeTransformEdit_.Active()) {
        activeTransformEdit_.Clear();
        return;
    }

    const EditorSceneTransformEditApplyResult result =
        EditorSceneTransformEditApplier::RestoreBefore(*scene_, activeTransformEdit_.Changes());
    activeTransformEdit_.Clear();
    if (result.changed) {
        MarkSceneEntitiesRenderDirty(result.touched);
        scene_->Runtime().SynchronizeTransforms();
    }
}

bool EditorSceneContext::HasActiveTransformEdit() const noexcept {
    return activeTransformEdit_.Active();
}

EditorSceneCommandController EditorSceneContext::SceneCommands() noexcept {
    return EditorSceneCommandController{
        *scene_,
        commandStack_,
        console_,
        viewportState_,
        hierarchySelection_,
        assetBrowser_,
        hierarchyExpansion_,
        hierarchySearch_,
        pendingSceneTransactionLabel_,
        sceneRenderRevision_,
        sceneRenderDirtyBaseRevision_,
        sceneRenderDirtyEntityIds_,
        sceneRenderFullDirty_,
        sceneDocumentDirty_,
        hierarchyRowsDirty_,
    };
}

bool EditorSceneContext::ExecuteSceneCommand(std::string label, std::function<bool()> mutation) {
    return SceneCommands().Execute(std::move(label), std::move(mutation));
}

bool EditorSceneContext::ExecuteMaterialAssetEdit(kb::assets::AssetId id, std::unique_ptr<IEditorMaterialAssetPropertyEdit> edit) {
    if (edit == nullptr) {
        console_.Warning("Materials", "Material edit command could not be created.");
        return false;
    }

    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (materialEditor_.OpenAssetId() == id && metadata != nullptr && metadata->type == "RenderMaterial") {
        return ApplyPatchToMaterialEditorWorkingCopy(id, *edit);
    }

    std::unique_ptr<EditorMaterialAssetEditCommand> command = EditorMaterialAssetEditCommand::Create(*scene_, id, std::move(edit));
    if (command == nullptr) {
        console_.Warning("Materials", "Material edit command could not be created.");
        return false;
    }

    const std::string label{ command->Label() };
    if (!commandStack_.Execute(std::move(command))) {
        console_.Warning("Materials", "Material edit failed: " + label);
        return false;
    }

    if (materialEditor_.OpenAssetId() == id) {
        if (std::optional<kb::render::RenderMaterialAssetData> material = ReadMaterialAsset(id)) {
            materialEditor_.SetWorkingCopy(std::move(*material));
            materialEditor_.MarkSaved();
        }
    }
    MarkSceneRenderDirty();
    return true;
}

bool EditorSceneContext::RecordMaterialGraphWorkingCopyEdit(
    kb::assets::AssetId id,
    std::string label,
    kb::render::RenderMaterialAssetData before,
    std::uint32_t beforeSelectedNodeId,
    std::vector<std::uint32_t> beforeSelectedNodeIds,
    std::uint32_t beforeSelectedCommentId) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value()) {
        return false;
    }

    if (HasMaterialGraphWorkingCopyTransaction()) {
        if (materialGraphWorkingCopyTransactionAssetId_ != id) {
            return false;
        }
        materialGraphWorkingCopyTransactionChanged_ = true;
        materialEditor_.ClearDiagnostics();
        SyncMaterialEditorWorkingCopyRuntimePreview();
        MarkSceneRenderDirty();
        return true;
    }

    kb::render::RenderMaterialAssetData after = *materialEditor_.WorkingCopy();
    if (beforeSelectedNodeIds.empty() && beforeSelectedNodeId != 0U) {
        beforeSelectedNodeIds.push_back(beforeSelectedNodeId);
    }
    std::vector<std::uint32_t> afterSelectedNodeIds = materialEditor_.SelectedNodeIds();
    const std::uint32_t afterSelectedCommentId = materialEditor_.SelectedCommentId();
    std::unique_ptr<EditorMaterialWorkingCopyEditCommand> command = EditorMaterialWorkingCopyEditCommand::Create(
        materialEditor_,
        id,
        std::move(label),
        before,
        std::move(after),
        beforeSelectedNodeIds,
        std::move(afterSelectedNodeIds),
        beforeSelectedCommentId,
        afterSelectedCommentId);
    if (!commandStack_.Execute(std::move(command))) {
        materialEditor_.SetWorkingCopy(std::move(before));
        static_cast<void>(materialEditor_.SetNodeSelection(std::move(beforeSelectedNodeIds), beforeSelectedNodeId));
        if (beforeSelectedCommentId != 0U) {
            static_cast<void>(materialEditor_.SelectComment(beforeSelectedCommentId));
        }
        return false;
    }

    materialEditor_.ClearDiagnostics();
    SyncMaterialEditorWorkingCopyRuntimePreview();
    MarkSceneRenderDirty();
    return true;
}

void EditorSceneContext::ClearMaterialGraphWorkingCopyTransaction() noexcept {
    materialGraphWorkingCopyTransactionAssetId_ = {};
    materialGraphWorkingCopyTransactionLabel_.clear();
    materialGraphWorkingCopyTransactionBefore_.reset();
    materialGraphWorkingCopyTransactionBeforeSelectedNodeId_ = 0U;
    materialGraphWorkingCopyTransactionBeforeSelectedNodeIds_.clear();
    materialGraphWorkingCopyTransactionBeforeSelectedCommentId_ = 0U;
    materialGraphWorkingCopyTransactionChanged_ = false;
}

std::optional<kb::render::RenderMaterialAssetData> EditorSceneContext::MaterialSourceForEdit(kb::assets::AssetId id) const {
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (materialEditor_.OpenAssetId() == id && metadata != nullptr && metadata->type == "RenderMaterial" && materialEditor_.WorkingCopy().has_value()) {
        return materialEditor_.WorkingCopy();
    }
    return ReadMaterialAsset(id);
}

bool EditorSceneContext::ApplyPatchToMaterialEditorWorkingCopy(kb::assets::AssetId id, IEditorMaterialAssetPropertyEdit& edit) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value()) {
        return false;
    }

    kb::render::RenderMaterialAssetData working = *materialEditor_.WorkingCopy();
    edit.Apply(working);
    materialEditor_.SetWorkingCopy(std::move(working));
    materialEditor_.ClearDiagnostics();
    SyncMaterialEditorWorkingCopyRuntimePreview();
    MarkSceneRenderDirty();
    return true;
}

void EditorSceneContext::SyncMaterialEditorWorkingCopyRuntimePreview() {
    if (scene_ == nullptr || !materialEditor_.OpenAssetId().IsValid() || !materialEditor_.WorkingCopy().has_value() || !materialEditor_.Dirty()) {
        ClearMaterialEditorWorkingCopyRuntimePreview();
        return;
    }

    const kb::assets::AssetId openAsset = materialEditor_.OpenAssetId();
    kb::assets::AssetManager& manager = scene_->Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(openAsset);
    if (metadata == nullptr || (metadata->type != "RenderMaterial" && metadata->type != "RenderMaterialInstance")) {
        ClearMaterialEditorWorkingCopyRuntimePreview();
        return;
    }

    if (materialRuntimePreviewAssetId_.IsValid() && materialRuntimePreviewAssetId_ != openAsset) {
        ClearMaterialEditorWorkingCopyRuntimePreview();
        metadata = manager.Registry().Find(openAsset);
        if (metadata == nullptr || (metadata->type != "RenderMaterial" && metadata->type != "RenderMaterialInstance")) {
            return;
        }
    }

    const std::uint64_t runtimeContentHash = MaterialWorkingCopyRuntimeContentHash(*materialEditor_.WorkingCopy());
    if (materialRuntimePreviewAssetId_ == openAsset && materialRuntimePreviewContentHash_ == runtimeContentHash) {
        std::error_code existsError;
        if (!materialRuntimePreviewPath_.empty() && std::filesystem::exists(materialRuntimePreviewPath_, existsError)) {
            return;
        }
    }

    // Hot-reload last-good (MAT-33): if the working copy is currently invalid but we already have a
    // live runtime preview, keep rendering that last-good material and only kick a recook so the
    // cook service reports Stale (with the failure reason) instead of dropping to a black/error frame.
    if (materialEditor_.DiagnosticsHaveError() && materialRuntimePreviewAssetId_ == openAsset) {
        if (materialGraphCookService_ != nullptr) {
            static_cast<void>(materialGraphCookService_->RequestCook(openAsset, *materialEditor_.WorkingCopy()));
        }
        return;
    }

    if (!materialRuntimePreviewSourceMetadata_.has_value()) {
        materialRuntimePreviewSourceMetadata_ = *metadata;
    }

    const std::filesystem::path runtimePath = materialRuntimePreviewPath_.empty()
        ? SceneMaterialWorkingCopyRuntimePath(openAsset)
        : materialRuntimePreviewPath_;
    if (!kb::render::RenderMaterialAssetWriter::Save(runtimePath, *materialEditor_.WorkingCopy())) {
        console_.Warning("Materials", "Material graph live preview could not write its runtime working copy.");
        return;
    }

    kb::assets::AssetMetadata runtimeMetadata = *materialRuntimePreviewSourceMetadata_;
    runtimeMetadata.id = openAsset;
    runtimeMetadata.type = "RenderMaterial";
    runtimeMetadata.importCategory = std::string{ kEditorLiveAssetOverrideCategory };
    runtimeMetadata.physicalPath = runtimePath;
    runtimeMetadata.contentHash = runtimeContentHash;
    runtimeMetadata.runtimeLoadable = true;
    if (runtimeMetadata.name.empty()) {
        runtimeMetadata.name = "Material Working Copy";
    }
    if (runtimeMetadata.virtualPath.empty()) {
        runtimeMetadata.virtualPath = std::filesystem::path{ "/Editor/Runtime" } / ("WorkingMaterial" + std::to_string(openAsset.value) + ".kbmat");
    }

    static_cast<void>(manager.RegisterAsset(std::move(runtimeMetadata)));
    static_cast<void>(manager.Unload(openAsset));
    materialRuntimePreviewAssetId_ = openAsset;
    materialRuntimePreviewPath_ = runtimePath;
    materialRuntimePreviewContentHash_ = runtimeContentHash;

    // The working copy changed: kick a debounced GPU cook so the preview and scene render the
    // authored graph program (not the CPU PBR fallback) on the next frame (MAT-30/32/33).
    if (materialGraphCookService_ != nullptr) {
        static_cast<void>(materialGraphCookService_->RequestCook(openAsset, *materialEditor_.WorkingCopy()));
    }
}

void EditorSceneContext::ClearMaterialEditorWorkingCopyRuntimePreview() {
    if (scene_ != nullptr && materialRuntimePreviewAssetId_.IsValid()) {
        kb::assets::AssetManager& manager = scene_->Assets().Manager();
        if (materialRuntimePreviewSourceMetadata_.has_value()) {
            static_cast<void>(manager.RegisterAsset(*materialRuntimePreviewSourceMetadata_));
        }
        static_cast<void>(manager.Unload(materialRuntimePreviewAssetId_));
    }

    if (!materialRuntimePreviewPath_.empty()) {
        std::error_code removeError;
        static_cast<void>(std::filesystem::remove(materialRuntimePreviewPath_, removeError));
    }

    materialRuntimePreviewAssetId_ = {};
    materialRuntimePreviewSourceMetadata_.reset();
    materialRuntimePreviewPath_.clear();
    materialRuntimePreviewContentHash_ = 0U;
}

bool EditorSceneContext::CopyWorkingMaterialToSource(kb::assets::AssetId id) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value()) {
        console_.Error("Materials", "Material working copy is not available for Save.");
        return false;
    }

    kb::render::RenderMaterialAssetData before{};
    if (materialEditor_.CleanSnapshot().has_value()) {
        before = *materialEditor_.CleanSnapshot();
    } else if (std::optional<kb::render::RenderMaterialAssetData> source = ReadMaterialAsset(id)) {
        before = std::move(*source);
    } else {
        console_.Error("Materials", "Material source could not be read before Save.");
        return false;
    }

    kb::render::RenderMaterialAssetData after = *materialEditor_.WorkingCopy();
    ClearMaterialEditorWorkingCopyRuntimePreview();
    std::unique_ptr<EditorMaterialAssetEditCommand> command = EditorMaterialAssetEditCommand::CreateRecorded(
        *scene_,
        id,
        "Save Material",
        std::move(before),
        after);
    if (!commandStack_.Execute(std::move(command))) {
        console_.Warning("Materials", "Material working copy could not be saved.");
        return false;
    }

    materialEditor_.SetWorkingCopy(after);
    materialEditor_.MarkSaved();
    ClearMaterialEditorWorkingCopyRuntimePreview();
    // MAT-87: the saved material must propagate to every scene mesh using it. Recook the scene's
    // graph materials (deduped) and re-resolve so meshes pick up the new program next frame.
    if (materialGraphCookService_ != nullptr && (!after.graph.links.empty() || after.graph.nodes.size() > 1U)) {
        static_cast<void>(materialGraphCookService_->RequestCook(id, after));
        sceneGraphCookPending_ = true;
    }
    MarkSceneRenderDirty();
    return ValidateMaterialEditorAsset(id);
}

bool EditorSceneContext::CopyWorkingMaterialInstanceToSource(kb::assets::AssetId id) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.InstanceWorkingCopy().has_value()) {
        console_.Error("Materials", "Material instance working copy is not available for Save.");
        return false;
    }

    kb::render::RenderMaterialInstanceAssetData before{};
    if (materialEditor_.InstanceCleanSnapshot().has_value()) {
        before = *materialEditor_.InstanceCleanSnapshot();
    } else if (std::optional<kb::render::RenderMaterialInstanceAssetData> source = ReadMaterialInstanceAsset(id)) {
        before = std::move(*source);
    } else {
        console_.Error("Materials", "Material instance source could not be read before Save.");
        return false;
    }

    kb::render::RenderMaterialInstanceAssetData after = *materialEditor_.InstanceWorkingCopy();
    std::optional<kb::render::RenderMaterialAssetData> parent = materialEditor_.InstanceParentSnapshot();
    if (!parent.has_value() && after.parentMaterialAssetId.IsValid()) {
        parent = ReadEffectiveMaterialAsset(after.parentMaterialAssetId);
    }
    if (!parent.has_value()) {
        console_.Error("Materials", "Material instance parent material is not available for Save.");
        return false;
    }

    const kb::render::RenderMaterialInstanceValidationResult validation =
        kb::render::RenderMaterialInstanceAssetLoader::ValidateAgainstParent(after, *parent);
    if (!validation.Succeeded()) {
        std::vector<std::string> diagnostics = MaterialInstanceValidationDiagnosticLines(validation);
        for (const std::string& diagnostic : diagnostics) {
            console_.Error("Materials", diagnostic);
        }
        materialEditor_.SetDiagnostics(std::move(diagnostics), true);
        return false;
    }

    ClearMaterialEditorWorkingCopyRuntimePreview();
    std::unique_ptr<EditorMaterialInstanceEditCommand> command = EditorMaterialInstanceEditCommand::CreateRecorded(
        *scene_,
        id,
        "Save Material Instance",
        std::move(before),
        after);
    if (!commandStack_.Execute(std::move(command))) {
        console_.Warning("Materials", "Material instance working copy could not be saved.");
        return false;
    }

    const kb::render::RenderMaterialAssetData effective = kb::render::BuildEffectiveRenderMaterialInstanceAsset(*parent, after);
    materialEditor_.SetInstanceWorkingCopy(std::move(after), effective);
    materialEditor_.MarkSaved();
    ClearMaterialEditorWorkingCopyRuntimePreview();
    if (materialGraphCookService_ != nullptr && (!effective.graph.links.empty() || effective.graph.nodes.size() > 1U)) {
        static_cast<void>(materialGraphCookService_->RequestCook(id, effective));
        sceneGraphCookPending_ = true;
    }
    MarkSceneRenderDirty();
    return ValidateMaterialEditorAsset(id);
}

void EditorSceneContext::RefreshOpenMaterialEditorFromSource() {
    const kb::assets::AssetId openAsset = materialEditor_.OpenAssetId();
    if (!openAsset.IsValid()) {
        return;
    }
    ClearMaterialEditorWorkingCopyRuntimePreview();
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(openAsset);
    if (metadata != nullptr && metadata->type == "RenderMaterialInstance") {
        std::optional<kb::render::RenderMaterialInstanceAssetData> instance = ReadMaterialInstanceAsset(openAsset);
        if (!instance.has_value() || !instance->parentMaterialAssetId.IsValid()) {
            materialEditor_.SetDiagnostics({ "Material instance source could not be reloaded after undo/redo." }, true);
            return;
        }
        std::optional<kb::render::RenderMaterialAssetData> parent = ReadEffectiveMaterialAsset(instance->parentMaterialAssetId);
        if (!parent.has_value()) {
            materialEditor_.SetDiagnostics({ "Material instance parent could not be reloaded after undo/redo." }, true);
            return;
        }
        const kb::render::RenderMaterialAssetData effective = kb::render::BuildEffectiveRenderMaterialInstanceAsset(*parent, *instance);
        materialEditor_.SetInstanceWorkingCopy(std::move(*instance), effective);
        materialEditor_.MarkSaved();
        materialEditor_.ClearDiagnostics();
        MarkSceneRenderDirty();
        return;
    }
    std::optional<kb::render::RenderMaterialAssetData> material = ReadMaterialDocumentAsset(openAsset);
    if (!material.has_value()) {
        materialEditor_.SetDiagnostics({ "Material source could not be reloaded after undo/redo." }, true);
        return;
    }
    materialEditor_.SetWorkingCopy(std::move(*material));
    materialEditor_.MarkSaved();
    materialEditor_.ClearDiagnostics();
    MarkSceneRenderDirty();
}

void EditorSceneContext::ClearSceneDocumentDirty() noexcept {
    sceneDocumentDirty_ = false;
}

void EditorSceneContext::InvalidateHierarchyRows() noexcept {
    hierarchyRowsDirty_ = true;
}

void EditorSceneContext::RebuildHierarchyRowsIfNeeded() const {
    if (!hierarchyRowsDirty_) {
        return;
    }

    hierarchyRowsCache_ = EditorHierarchyRowBuilder::Build(*scene_, hierarchyExpansion_.CollapsedEntities(), hierarchySearch_.Query());
    hierarchyRowsDirty_ = false;
}

void EditorSceneContext::ResetSceneEditState() {
    ClearMaterialEditorWorkingCopyRuntimePreview();
    commandStack_.Clear();
    pendingSceneTransactionLabel_.reset();
    activeTransformEdit_.Clear();
    activeMaterialEditAsset_ = {};
    activeMaterialEditProperty_ = InspectorPropertyId::None;
    activeMaterialEditBefore_.reset();
    CancelHierarchyRename();
    inspector_.EndTextEdit();
    MarkSceneRenderDirty();
    scene_->Runtime().SynchronizeTransforms();
}

void EditorSceneContext::CookSceneGraphMaterials() {
    if (scene_ == nullptr || materialGraphCookService_ == nullptr) {
        return;
    }

    // Collect every material asset referenced by scene mesh renderers (primary slot + overrides).
    std::vector<std::uint64_t> referenced;
    scene_->Components().Visitors().ForEachMeshRenderer(
        [](kb::scene::SceneEntity, const kb::scene::TransformComponent&, const kb::scene::MeshRendererComponent& renderer, void* context) {
            auto* ids = static_cast<std::vector<std::uint64_t>*>(context);
            if (renderer.materialAssetId != 0U) {
                ids->push_back(renderer.materialAssetId);
            }
            const std::uint32_t slotCount = std::min(renderer.materialSlotOverrideCount, kb::scene::kMaxMeshRendererMaterialSlotOverrides);
            for (std::uint32_t slot = 0U; slot < slotCount; ++slot) {
                if (renderer.materialSlotAssetIds[slot] != 0U) {
                    ids->push_back(renderer.materialSlotAssetIds[slot]);
                }
            }
        },
        &referenced);

    std::sort(referenced.begin(), referenced.end());
    referenced.erase(std::unique(referenced.begin(), referenced.end()), referenced.end());

    for (const std::uint64_t idValue : referenced) {
        const kb::assets::AssetId id{ idValue };
        // The open material is already cooked from its live working copy (MAT-30); skip it here.
        if (materialEditor_.OpenAssetId() == id) {
            continue;
        }
        const std::optional<kb::render::RenderMaterialAssetData> material = ReadMaterialDocumentAsset(id);
        if (!material.has_value()) {
            continue;
        }
        // Only authored graph materials need a cooked program; builtin/default materials carry just
        // the implicit Material Output node and use the static PBR program.
        if (material->graph.links.empty() && material->graph.nodes.size() <= 1U) {
            continue;
        }
        static_cast<void>(materialGraphCookService_->RequestCook(id, *material));
    }
}

void EditorSceneContext::SelectFirstSceneEntityOrClear() noexcept {
    // A scene was (re)loaded or seeded: its referenced graph materials must be (re)cooked (MAT-84).
    sceneGraphCookPending_ = true;
    const std::vector<kb::scene::SceneEntity> roots = scene_->Hierarchy().RootEntities();
    if (roots.empty()) {
        hierarchySelection_.Clear();
        return;
    }
    hierarchySelection_.SelectEntity(roots.front());
    assetBrowser_.ClearSelection();
}

std::filesystem::path EditorSceneContext::ResolveProjectVirtualPath(const std::filesystem::path& virtualPath) const {
    if (const std::optional<std::filesystem::path> physical = scene_->Assets().Manager().Mounts().Resolve(virtualPath)) {
        return *physical;
    }
    return EditorProjectPaths::DefaultScenePath();
}

std::filesystem::path EditorSceneContext::ResolveDefaultScenePath() const {
    const std::filesystem::path defaultScene = project_.defaultScene.empty()
        ? std::filesystem::path{ "/Game/Scenes/Main.21kbscene" }
        : std::filesystem::path{ project_.defaultScene };
    return ResolveProjectVirtualPath(defaultScene);
}

} // namespace kb::editor
