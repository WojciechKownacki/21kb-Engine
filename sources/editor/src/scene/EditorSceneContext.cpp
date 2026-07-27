#include "scene/EditorSceneContext.hpp"

#include "app/EditorCrashBreadcrumbs.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/script/ScriptAsset.hpp"
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
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/JointComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/PhysicsDebugDraw.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "rendering/EditorMeshPreviewRasterizer.hpp"
#include "rendering/EditorMeshPreviewService.hpp"
#include "rendering/EditorMeshPreviewTypes.hpp"
#include "inspection/InspectorPhysicsModel.hpp"
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
#include "engine/library/EngineLibraryManifest.hpp"
#include "engine/library/EngineLibraryModule.hpp"
#include "engine/modules/EngineModuleHost.hpp"
#include "engine/project/ProjectDescriptorWriter.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptModule.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialNumericParsing.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "kb/render/resources/RenderMaterialSemanticHash.hpp"
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
#include "scene/EditorSceneDocumentAssetLoaders.hpp"
#include "scene/EditorSceneAudioAssetActions.hpp"
#include "scene/EditorSceneHierarchyActions.hpp"
#include "scene/EditorSceneMaterialAssetActions.hpp"
#include "scene/EditorSceneMeshAssetActions.hpp"
#include "scene/EditorSceneObjectEditCommands.hpp"
#include "scene/EditorScenePrefabActions.hpp"
#include "scene/EditorSceneSelectionPivot.hpp"
#include "scene/material/EditorMaterialAssetAuthoring.hpp"
#include "scene/material/EditorMaterialGraphSemanticAnalysis.hpp"
#include "scene/material/EditorMaterialAssetEditCommand.hpp"
#include "scene/material/EditorMaterialAssetGateway.hpp"
#include "scene/material/EditorMaterialReferenceFinder.hpp"
#include "scene/material/EditorMaterialTextureSlotValidation.hpp"
#include "scene/material/EditorEmbeddedMaterialExtractor.hpp"
#include "scene/material_preview/EditorMaterialGraphCookService.hpp"
#include "scene/material_preview/EditorMaterialNodePreviewBuilder.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "scene/material_preview/EditorMaterialPreviewScene.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "scene/transform_edit/EditorSceneTransformCommitBuilder.hpp"
#include "scene/transform_edit/EditorSceneTransformEditApplier.hpp"
#include "scene/transform_edit/EditorSceneTransformEditController.hpp"
#include "scene/transform_edit/EditorSceneTransformSnapshotBuilder.hpp"
#include "project/EditorProjectBootstrap.hpp"
#include "project/EditorProjectPaths.hpp"

#include <bit>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kb::editor {
namespace {

constexpr std::string_view kEditorLiveAssetOverrideCategory = "EditorLiveOverride";

struct MaterialGraphContextMenuKeyboardRow {
    bool category = false;
    std::size_t categoryIndex = 0U;
    MaterialEditorGraphMenuCommand command = MaterialEditorGraphMenuCommand::None;
    int contentTop = 0;
    int height = kMaterialEditorGraphMenuCommandHeight;
};

[[nodiscard]] std::vector<MaterialGraphContextMenuKeyboardRow> MaterialGraphContextMenuKeyboardRows(const EditorSceneContext& sceneContext) {
    std::vector<MaterialGraphContextMenuKeyboardRow> rows;
    int contentTop = 0;
    const std::size_t selectedGraphNodeCount = sceneContext.SelectedMaterialGraphNodeIds().size();
    const bool hasSelectedGraphComment = sceneContext.SelectedMaterialGraphCommentId() != 0U;
    // Same grouped walk the palette draws. When filtering (search / wire-drop) the category headers are still
    // drawn (so contentTop stays in step with the rendered rows) but are NOT emitted as keyboard rows: they are
    // force-expanded, so keyboard focus moves command-to-command and Enter creates the first matching node
    // instead of toggling a category.
    const bool filtering = MaterialEditorGraphContextMenuIsFiltering(sceneContext);
    for (const std::size_t categoryIndex : MaterialEditorGraphContextMenuCategoryOrder(sceneContext)) {
        const std::vector<MaterialEditorGraphMenuCommand> commands = MaterialEditorGraphContextMenuVisibleCommands(sceneContext, categoryIndex);
        if (filtering && commands.empty()) {
            continue;
        }
        if (!filtering) {
            rows.push_back(MaterialGraphContextMenuKeyboardRow{
                .category = true,
                .categoryIndex = categoryIndex,
                .command = MaterialEditorGraphMenuCommand::None,
                .contentTop = contentTop,
                .height = kMaterialEditorGraphMenuCategoryHeight,
            });
        }
        contentTop += kMaterialEditorGraphMenuCategoryHeight;
        if (!filtering && !sceneContext.IsMaterialGraphContextMenuCategoryExpanded(categoryIndex)) {
            continue;
        }
        for (const MaterialEditorGraphMenuCommand command : commands) {
            if (MaterialEditorGraphContextMenuCommandEnabled(command, selectedGraphNodeCount, hasSelectedGraphComment)) {
                rows.push_back(MaterialGraphContextMenuKeyboardRow{
                    .category = false,
                    .categoryIndex = categoryIndex,
                    .command = command,
                    .contentTop = contentTop,
                    .height = kMaterialEditorGraphMenuCommandHeight,
                });
            }
            contentTop += kMaterialEditorGraphMenuCommandHeight;
        }
    }
    return rows;
}

[[nodiscard]] bool IsMaterialGraphContextMenuRowHovered(const EditorSceneContext& sceneContext, const MaterialGraphContextMenuKeyboardRow& row) noexcept {
    return row.category
        ? sceneContext.IsMaterialGraphContextMenuCategoryHovered(row.categoryIndex)
        : sceneContext.IsMaterialGraphContextMenuCommandHovered(row.categoryIndex, row.command);
}


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

[[nodiscard]] bool ParseDecimalAssetId(std::string_view text, std::uint64_t& output) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1U);
    }
    if (text.empty()) {
        return false;
    }
    std::uint64_t value = 0U;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10ULL) {
            return false;
        }
        value = value * 10ULL + digit;
    }
    output = value;
    return true;
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

[[nodiscard]] bool IsMaterialGraphTextureSampleNode(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    return kind == kb::render::RenderMaterialGraphNodeKind::TextureSample ||
        kind == kb::render::RenderMaterialGraphNodeKind::TextureSampleCube ||
        kind == kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume ||
        kind == kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray;
}

[[nodiscard]] bool IsMaterialGraphTextureObjectNode(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    return kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture ||
        kind == kb::render::RenderMaterialGraphNodeKind::TextureObject ||
        kind == kb::render::RenderMaterialGraphNodeKind::TextureObjectCube ||
        kind == kb::render::RenderMaterialGraphNodeKind::TextureObjectVolume ||
        kind == kb::render::RenderMaterialGraphNodeKind::TextureObject2DArray;
}

[[nodiscard]] bool IsMaterialGraphTextureAssetNode(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    return IsMaterialGraphTextureSampleNode(kind) || IsMaterialGraphTextureObjectNode(kind);
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
    return kb::render::RenderMaterialRuntimeSemanticHash(material);
}

[[nodiscard]] kb::render::RenderMaterialGraphBuildContext MaterialPreviewGraphBuildContext(
    kb::assets::AssetId assetId,
    const EditorMaterialPreviewSceneSettings& settings,
    kb::render::RenderMaterialGraphVariantUsage usage = kb::render::RenderMaterialGraphVariantUsage::Preview) noexcept {
    kb::render::RenderMaterialGraphBuildContext context{};
    context.assetId = assetId.value;
    context.qualityLevel = settings.qualityLevel;
    context.featureLevel = kb::render::RenderMaterialGraphFeatureLevel::Sm5;
    context.shadingPath = kb::render::RenderMaterialGraphShadingPath::Forward;
    context.shaderStage = kb::render::RenderMaterialGraphShaderStage::Fragment;
    context.variantUsage = usage;
    return context;
}

[[nodiscard]] kb::render::RenderMaterialGraphShadingPath MaterialGraphShadingPathForProject(
    kb::project::ProjectSceneLightingPath path) noexcept {
    switch (path) {
    case kb::project::ProjectSceneLightingPath::Deferred:
        return kb::render::RenderMaterialGraphShadingPath::Deferred;
    case kb::project::ProjectSceneLightingPath::ForwardPlus:
        return kb::render::RenderMaterialGraphShadingPath::ForwardPlus;
    case kb::project::ProjectSceneLightingPath::Forward:
        return kb::render::RenderMaterialGraphShadingPath::Forward;
    }
    return kb::render::RenderMaterialGraphShadingPath::Forward;
}

[[nodiscard]] kb::render::RenderMaterialGraphBuildContext SceneMaterialGraphBuildContext(
    kb::assets::AssetId assetId,
    const kb::assets::AssetMetadata* metadata,
    kb::project::ProjectSceneLightingPath lightingPath) noexcept {
    kb::render::RenderMaterialGraphBuildContext context{};
    context.assetId = assetId.value;
    if (metadata != nullptr) {
        context.sourcePath = metadata->virtualPath.generic_string();
    }
    context.qualityLevel = kb::render::RenderMaterialGraphQualityLevel::High;
    context.featureLevel = kb::render::RenderMaterialGraphFeatureLevel::Sm5;
    context.shadingPath = MaterialGraphShadingPathForProject(lightingPath);
    context.shaderStage = kb::render::RenderMaterialGraphShaderStage::Fragment;
    context.variantUsage = kb::render::RenderMaterialGraphVariantUsage::Scene;
    return context;
}

[[nodiscard]] std::filesystem::path SceneMaterialWorkingCopyRuntimePath(
    kb::assets::AssetId materialAssetId,
    std::uint64_t contentHash,
    const std::filesystem::path& projectIdentityPath,
    const void* editorInstance) {
#if defined(_WIN32)
    const std::uint64_t processId = static_cast<std::uint64_t>(::_getpid());
#else
    const std::uint64_t processId = static_cast<std::uint64_t>(::getpid());
#endif
    std::error_code canonicalError;
    const std::filesystem::path configuredProjectRoot = EditorProjectPaths::ProjectRoot();
    const std::filesystem::path canonicalProjectRoot =
        std::filesystem::weakly_canonical(configuredProjectRoot, canonicalError);
    const std::string projectIdentity =
        (canonicalError ? configuredProjectRoot : canonicalProjectRoot).generic_string() + "|" +
        projectIdentityPath.generic_string();
    return std::filesystem::temp_directory_path() /
        "21kb_scene_material" /
        ("project_" + std::to_string(HashBytes(projectIdentity))) /
        ("process_" + std::to_string(processId)) /
        ("editor_" + std::to_string(reinterpret_cast<std::uintptr_t>(editorInstance))) /
        ("material_" + std::to_string(materialAssetId.value)) /
        ("variant_" + std::to_string(contentHash) + ".kbmat");
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

[[nodiscard]] std::string MaterialAssetParseDiagnosticLine(
    const kb::render::RenderMaterialAssetParseDiagnostic& diagnostic) {
    std::string line{ kb::render::RenderMaterialAssetParseDiagnosticCodeName(diagnostic.code) };
    if (!diagnostic.field.empty()) {
        line += " (" + diagnostic.field + ")";
    }
    line += ": " + diagnostic.message;
    if (diagnostic.line > 0U) {
        line += " [line " + std::to_string(diagnostic.line) + "]";
    }
    return line;
}

void AppendMaterialAssetParseDiagnostics(
    const kb::render::RenderMaterialAssetParseResult& result,
    std::vector<std::string>& output,
    bool& hasError,
    EditorConsoleState& console) {
    output.reserve(output.size() + result.diagnostics.size());
    for (const kb::render::RenderMaterialAssetParseDiagnostic& diagnostic : result.diagnostics) {
        std::string line = MaterialAssetParseDiagnosticLine(diagnostic);
        if (diagnostic.severity == kb::render::RenderMaterialAssetParseDiagnosticSeverity::Error) {
            hasError = true;
            console.Error("Materials", line);
        } else {
            console.Warning("Materials", line);
        }
        output.push_back(std::move(line));
    }
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
    std::vector<float> values;
    return kb::render::ParseFiniteMaterialFloatSequence(text, values, 1U, 1U, false)
        ? std::optional<float>{ values.front() }
        : std::nullopt;
}

[[nodiscard]] std::optional<kb::render::RenderMaterialGraphParameterValue> ParseMaterialGraphParameterValue(
    std::string_view stableId,
    kb::render::RenderMaterialParameterType type,
    std::string_view text) {
    kb::render::RenderMaterialGraphParameterValue value{};
    value.stableId = std::string{ stableId };
    value.type = type;

    std::vector<float> numbers;
    switch (type) {
    case kb::render::RenderMaterialParameterType::Scalar:
        if (kb::render::ParseFiniteMaterialFloatSequence(text, numbers, 1U, 1U)) {
            value.numbers[0] = numbers[0];
            return value;
        }
        return std::nullopt;
    case kb::render::RenderMaterialParameterType::Vec3:
        if (kb::render::ParseFiniteMaterialFloatSequence(text, numbers, 3U, 3U)) {
            std::copy_n(numbers.begin(), 3U, value.numbers.begin());
            return value;
        }
        return std::nullopt;
    case kb::render::RenderMaterialParameterType::Vec4:
    case kb::render::RenderMaterialParameterType::Color:
        if (kb::render::ParseFiniteMaterialFloatSequence(text, numbers, 3U, 4U)) {
            std::copy(numbers.begin(), numbers.end(), value.numbers.begin());
            value.numbers[3] = numbers.size() == 4U ? numbers[3] : 1.0F;
            return value;
        }
        return std::nullopt;
    case kb::render::RenderMaterialParameterType::Bool: {
        std::istringstream input{ std::string{ text } };
        std::string boolText;
        std::string trailing;
        if (!(input >> boolText) || (input >> trailing)) {
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

[[nodiscard]] bool MaterialGraphNodeFeedsSurfaceNormal(
    const kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t sourceNodeId) noexcept {
    return EditorMaterialGraphNodeFeedsSurfaceNormal(graph, sourceNodeId);
}

[[nodiscard]] bool MaterialGraphTextureNodeFeedsSurfaceNormal(
    const kb::render::RenderMaterialGraphDocument& graph,
    const kb::render::RenderMaterialGraphNode& node) noexcept {
    return (IsMaterialGraphTextureSampleNode(node.kind) || IsMaterialGraphTextureObjectNode(node.kind)) &&
        MaterialGraphNodeFeedsSurfaceNormal(graph, node.id);
}

[[nodiscard]] bool LooksLikeEditorGeneratedTextureStableId(std::string_view stableId) noexcept {
    return stableId.starts_with("textureSample") ||
        stableId.starts_with("textureObject") ||
        stableId.starts_with("texture");
}

void SanitizeMaterialGraphTextureMetadata(kb::render::RenderMaterialAssetData& material) {
    std::unordered_set<std::string> liveTextureStableIds;
    for (kb::render::RenderMaterialGraphLink& link : material.graph.links) {
        const kb::render::RenderMaterialGraphNode* fromNode = kb::render::FindRenderMaterialGraphNode(material.graph, link.fromNodeId);
        const kb::render::RenderMaterialGraphNode* toNode = kb::render::FindRenderMaterialGraphNode(material.graph, link.toNodeId);
        if (fromNode == nullptr || toNode == nullptr) {
            continue;
        }
        const std::uint32_t fromPinId = kb::render::RenderMaterialGraphStablePinId(*fromNode, link.fromPin, true);
        const std::uint32_t toPinId = kb::render::RenderMaterialGraphStablePinId(*toNode, link.toPin, false);
        if (fromPinId == 0U || toPinId == 0U) {
            continue;
        }
        link.fromPinId = fromPinId;
        link.toPinId = toPinId;
        link.id = kb::render::MakeRenderMaterialGraphLinkId(link);
    }

    for (kb::render::RenderMaterialGraphNode& node : material.graph.nodes) {
        if (!IsMaterialGraphTextureAssetNode(node.kind) || node.parameter.stableId.empty()) {
            continue;
        }
        liveTextureStableIds.insert(node.parameter.stableId);
        if (MaterialGraphTextureNodeFeedsSurfaceNormal(material.graph, node)) {
            node.parameter.textureRole = "normal";
            node.parameter.expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Linear;
            continue;
        }
        if (node.parameter.textureRole.empty()) {
            node.parameter.textureRole = "baseColor";
        }
        if (node.parameter.expectedTextureColorSpace == kb::render::RenderMaterialTextureColorSpace::Unknown) {
            node.parameter.expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb;
        }
    }

    const auto oldEnd = std::remove_if(
        material.graphParameterValues.begin(),
        material.graphParameterValues.end(),
        [&liveTextureStableIds](const kb::render::RenderMaterialGraphParameterValue& value) {
            return value.type == kb::render::RenderMaterialParameterType::Texture &&
                LooksLikeEditorGeneratedTextureStableId(value.stableId) &&
                !liveTextureStableIds.contains(value.stableId);
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

[[nodiscard]] std::string_view MaterialGraphTextureColorSpaceDebugName(kb::render::RenderMaterialTextureColorSpace colorSpace) noexcept {
    switch (colorSpace) {
    case kb::render::RenderMaterialTextureColorSpace::Srgb: return "Srgb";
    case kb::render::RenderMaterialTextureColorSpace::Linear: return "Linear";
    case kb::render::RenderMaterialTextureColorSpace::Unknown: return "Unknown";
    }
    return "Unknown";
}

[[nodiscard]] const kb::render::RenderMaterialGraphParameterValue* MaterialGraphDebugParameterValue(
    const kb::render::RenderMaterialAssetData& material,
    std::string_view stableId) noexcept {
    for (const kb::render::RenderMaterialGraphParameterValue& value : material.graphParameterValues) {
        if (value.stableId == stableId) {
            return &value;
        }
    }
    return nullptr;
}

[[nodiscard]] bool MaterialGraphDebugLinkTouchesNormalPath(
    const kb::render::RenderMaterialGraphDocument& graph,
    const kb::render::RenderMaterialGraphLink& link) noexcept {
    const kb::render::RenderMaterialGraphNode* fromNode = kb::render::FindRenderMaterialGraphNode(graph, link.fromNodeId);
    const kb::render::RenderMaterialGraphNode* toNode = kb::render::FindRenderMaterialGraphNode(graph, link.toNodeId);
    return (fromNode != nullptr && fromNode->kind == kb::render::RenderMaterialGraphNodeKind::NormalUnpack) ||
        (toNode != nullptr && toNode->kind == kb::render::RenderMaterialGraphNodeKind::NormalUnpack) ||
        (toNode != nullptr && toNode->kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput && link.toPin == "normal");
}

[[nodiscard]] bool MaterialGraphDebugOutputHasLink(
    const kb::render::RenderMaterialGraphDocument& graph,
    std::string_view outputPin) noexcept {
    for (const kb::render::RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind != kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
            continue;
        }
        for (const kb::render::RenderMaterialGraphLink& link : graph.links) {
            if (link.toNodeId == node.id && link.toPin == outputPin) {
                return true;
            }
        }
    }
    return false;
}

std::filesystem::path MaterialGraphDebugLogPath(std::string_view extension) {
    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path(error);
    return (error ? std::filesystem::current_path() : root) / ("_material_graph_debug" + std::string{ extension });
}

std::mutex& MaterialGraphDebugLogMutex() {
    static std::mutex mutex;
    return mutex;
}

bool MaterialGraphDebugLoggingEnabled() noexcept {
    return false;
}

void WriteMaterialGraphDebugTrace(std::string_view message) {
    if (!MaterialGraphDebugLoggingEnabled()) {
        return;
    }
    try {
        std::ostringstream line;
        line << "[MaterialGraph] " << message;
        const std::string text = line.str();

        std::lock_guard lock{ MaterialGraphDebugLogMutex() };
        for (std::string_view extension : { std::string_view{ ".log" }, std::string_view{ ".md" } }) {
            std::ofstream output{ MaterialGraphDebugLogPath(extension), std::ios::out | std::ios::app };
            if (output.is_open()) {
                output << text << '\n';
            }
        }
#if defined(_WIN32)
        std::string debugLine = text;
        debugLine.push_back('\n');
        OutputDebugStringA(debugLine.c_str());
#endif
    } catch (...) {
    }
}

void LogMaterialGraphDebug(EditorConsoleState& console, std::string_view message) {
    if (!MaterialGraphDebugLoggingEnabled()) {
        return;
    }
    WriteMaterialGraphDebugTrace(message);
    console.Info("MaterialGraph", std::string{ message });
}

void LogMaterialGraphDebugDocument(
    EditorConsoleState& console,
    std::string_view phase,
    const kb::render::RenderMaterialAssetData& material) {
    if (!MaterialGraphDebugLoggingEnabled()) {
        return;
    }
    std::ostringstream header;
    header << phase
           << " materialType=" << material.materialType
           << " nodes=" << material.graph.nodes.size()
           << " links=" << material.graph.links.size()
           << " values=" << material.graphParameterValues.size()
           << " outputNormalLinked=" << (MaterialGraphDebugOutputHasLink(material.graph, "normal") ? "true" : "false");
    LogMaterialGraphDebug(console, header.str());

    for (const kb::render::RenderMaterialGraphNode& node : material.graph.nodes) {
        std::ostringstream row;
        row << phase
            << " node#" << node.id
            << " kind=" << kb::render::RenderMaterialGraphNodeKindName(node.kind)
            << " stableId=" << (node.parameter.stableId.empty() ? "<empty>" : node.parameter.stableId)
            << " role=" << (node.parameter.textureRole.empty() ? "<empty>" : node.parameter.textureRole)
            << " colorSpace=" << MaterialGraphTextureColorSpaceDebugName(node.parameter.expectedTextureColorSpace);
        if (const kb::render::RenderMaterialGraphParameterValue* value = MaterialGraphDebugParameterValue(material, node.parameter.stableId);
            value != nullptr && value->type == kb::render::RenderMaterialParameterType::Texture) {
            row << " textureAssetId=" << value->assetId;
        }
        if (node.kind == kb::render::RenderMaterialGraphNodeKind::NormalUnpack ||
            node.kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput ||
            IsMaterialGraphTextureAssetNode(node.kind)) {
            LogMaterialGraphDebug(console, row.str());
        } else {
            WriteMaterialGraphDebugTrace(row.str());
        }
    }

    for (const kb::render::RenderMaterialGraphLink& link : material.graph.links) {
        std::ostringstream row;
        row << phase
            << " link#" << link.id
            << " " << link.fromNodeId << "." << link.fromPin
            << " -> " << link.toNodeId << "." << link.toPin
            << " pinIds=" << link.fromPinId << "->" << link.toPinId
            << (MaterialGraphDebugLinkTouchesNormalPath(material.graph, link) ? " NORMAL_PATH" : "");
        if (MaterialGraphDebugLinkTouchesNormalPath(material.graph, link)) {
            LogMaterialGraphDebug(console, row.str());
        } else {
            WriteMaterialGraphDebugTrace(row.str());
        }
    }
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
    RegisterEditorSceneDocumentAssetLoaders(*scene_);
    const std::size_t discovered = scene_->Assets().Discover();
    console_.Info("Assets", "Asset discovery completed. Found " + std::to_string(discovered) + " asset(s).");
    static_cast<void>(ActivateProjectPhysicsLayers(*scene_));
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
        SurfaceScriptLibraryStartupReport();
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
            EditorCrashBreadcrumbs::Write("runtime_script_log", message);
            return kb::script::ScriptFunctionCallResult{ .executed = true, .outputs = {}, .errors = {} };
        };
        static_cast<void>(host.RegisterFunction(std::move(logDesc)));
    };

    auto scriptModule = std::make_unique<kb::script::ScriptModule>(std::move(scriptOptions));
    kb::script::ScriptModule* scriptModuleView = scriptModule.get();
    scriptModule_ = scriptModuleView;
    // The scene's own EngineModuleHost (Scene.cpp) already loaded AND attached the
    // project's DLL plugins (physics/audio/rendering) for this scene. This play-mode
    // host exists only to add the editor's script module (Log -> Console). Loading
    // the project's plugins a SECOND time here re-shadow-copies the exact same DLLs
    // to the same temp files the scene host still holds mapped — a guaranteed
    // sharing violation, and a redundant second plugin instance. Strip the plugins
    // so this host carries only the script module.
    kb::project::ProjectDescriptor scriptRuntimeProject = project_;
    scriptRuntimeProject.plugins.clear();
    scriptModuleHost_ = std::make_unique<kb::modules::EngineModuleHost>(scriptRuntimeProject);
    scriptModuleHost_->Add(std::move(scriptModule));
    // Loading/attaching the project's engine plugins (physics, audio, rendering,
    // …) must not silently abort play. A throw here previously unwound before
    // the script scene system ran, leaving Play engaged but no behaviour ticking
    // and no message. Catch it, and surface any plugin-load diagnostics.
    try {
        scriptModuleHost_->Load(scene_->Runtime().EcsWorld());
        scriptModuleHost_->AttachScene(*scene_);
    } catch (const std::exception& error) {
        console_.Error("Plugins", std::string{ "A plugin faulted while starting play mode: " } + error.what());
    } catch (...) {
        console_.Error("Plugins", "A plugin faulted while starting play mode (unknown error).");
    }
    for (const std::string& diagnostic : scriptModuleHost_->Diagnostics()) {
        console_.Warning("Plugins", diagnostic);
    }

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
    SurfaceScriptLibraryStartupReport();
}

void EditorSceneContext::SurfaceScriptLibraryStartupReport() {
    if (scriptModule_ == nullptr || scriptModule_->Host() == nullptr) {
        return;
    }

    kb::script::ScriptRuntimeHost& host = *scriptModule_->Host();
    std::istringstream report{ kb::library::FormatStartupReport(host.LibraryStartupReport()) };
    for (std::string line; std::getline(report, line);) {
        if (!line.empty()) {
            console_.Info("Library", std::move(line));
        }
    }

    const kb::script::ScriptApiCatalog catalog =
        kb::script::ScriptApiCatalog::Build(host, scene_->Assets().Manager());
    const kb::library::ApiManifest manifest = kb::library::BuildApiManifest(catalog);
    const kb::visual::VisualGraphNodeCatalog visualGraphCatalog = host.CreateVisualGraphNodeCatalog();
    const std::size_t missingDescriptionCount =
        static_cast<std::size_t>(std::count_if(
            catalog.functions.begin(),
            catalog.functions.end(),
            [](const kb::script::ScriptApiCatalogFunction& function) {
                return function.description.empty();
            }));
    std::size_t auditedFunctionCount = 0U;
    std::size_t invalidFunctionMetadataCount = 0U;
    for (const kb::library::LibraryModuleDesc& module : kb::library::EngineLibraryModule::Catalog()) {
        for (const kb::library::LibraryFunctionDesc& function : module.functions) {
            ++auditedFunctionCount;
            if (!kb::library::FunctionDescMatchesCatalog(function, catalog)) {
                ++invalidFunctionMetadataCount;
            }
        }
    }

    std::ostringstream summary;
    summary << "Live API " << kb::library::ToString(manifest.version)
            << " hash=" << manifest.manifestHash
            << " functions=" << catalog.functions.size()
            << " components=" << catalog.components.size()
            << " luaBindings=" << catalog.luaBindings.size()
            << " visualGraphNodes=" << visualGraphCatalog.Entries().size()
            << " lifecycleEvents=" << catalog.lifecycleEvents.size()
            << " projectEntries=" << catalog.projectEntries.size()
            << " auditedMetadata=" << auditedFunctionCount
            << " missingDescriptions=" << missingDescriptionCount
            << " registryLocked=" << (host.Functions().IsLocked() ? "true" : "false");
    console_.Info("Library", summary.str());

    if (invalidFunctionMetadataCount != 0U) {
        console_.Error(
            "Library",
            std::to_string(invalidFunctionMetadataCount)
                + " audited function descriptor(s) do not match the live runtime catalog.");
    }
    if (missingDescriptionCount != 0U) {
        console_.Error(
            "Library",
            std::to_string(missingDescriptionCount)
                + " live function(s) are missing API descriptions.");
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

void EditorSceneContext::SurfaceScriptDiagnostics() {
    if (scriptModule_ == nullptr || scriptModule_->Host() == nullptr) {
        return;
    }
    for (const std::string& diagnostic : scriptModule_->Host()->DrainSceneSystemDiagnostics()) {
        console_.Error("Scripts", diagnostic);
    }
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

void EditorSceneContext::MarkMaterialAssetRenderDirty(kb::assets::AssetId id) {
    if (scene_ == nullptr || !id.IsValid()) {
        return;
    }
    struct Context {
        std::uint64_t targetAssetId;
        std::vector<kb::scene::SceneEntity> entities;
    } context{ id.value, {} };
    scene_->Components().Visitors().ForEachMeshRenderer(
        [](kb::scene::SceneEntity entity, const kb::scene::TransformComponent&, const kb::scene::MeshRendererComponent& renderer, void* opaque) {
            auto& ctx = *static_cast<Context*>(opaque);
            bool referencesTarget = renderer.materialAssetId == ctx.targetAssetId;
            const std::uint32_t slotCount = std::min(renderer.materialSlotOverrideCount, kb::scene::kMaxMeshRendererMaterialSlotOverrides);
            for (std::uint32_t slot = 0U; !referencesTarget && slot < slotCount; ++slot) {
                referencesTarget = renderer.materialSlotAssetIds[slot] == ctx.targetAssetId;
            }
            if (referencesTarget) {
                ctx.entities.push_back(entity);
            }
        },
        &context);
    // No scene mesh uses this material (the common case while authoring a graph against the Material
    // Editor's own preview scene, which has its own independent revision counter): do nothing at all,
    // not even an incremental mark, so the main scene panel's render loop sees no revision change and
    // performs zero resync work for this edit.
    MarkSceneEntitiesRenderDirty(std::span<const kb::scene::SceneEntity>{ context.entities.data(), context.entities.size() });
}

void EditorSceneContext::AcknowledgeSceneRenderSubmitted() noexcept {
    sceneRenderFullDirty_ = false;
    sceneRenderDirtyEntityIds_.clear();
    sceneRenderDirtyBaseRevision_ = sceneRenderRevision_;
}

void EditorSceneContext::MarkSceneDocumentDirty() noexcept {
    sceneDocumentDirty_ = true;
}

bool EditorSceneContext::SaveOpenDocuments() {
    LogMaterialGraphDebug(console_, "save-open-documents-request materialOpen=" +
        std::to_string(materialEditor_.OpenAssetId().value) +
        " materialDirty=" + std::string{ materialEditor_.Dirty() ? "true" : "false" } +
        " materialAssetEditDirty=" + std::string{ HasDirtyMaterialAssetEdit() ? "true" : "false" } +
        " sceneDirty=" + std::string{ sceneDocumentDirty_ ? "true" : "false" });
    if (HasDirtyMaterialAssetEdit() && !SaveMaterialEditorAsset(materialEditor_.OpenAssetId())) {
        LogMaterialGraphDebug(console_, "save-open-documents-failed material editor save failed");
        return false;
    }
    if (!sceneDocumentDirty_) {
        LogMaterialGraphDebug(console_, "save-open-documents-ok no dirty scene");
        return true;
    }
    const bool savedScene = SaveCurrentScene();
    LogMaterialGraphDebug(console_, "save-open-documents-scene-save result=" + std::string{ savedScene ? "true" : "false" });
    return savedScene;
}

bool EditorSceneContext::CanUndoSceneCommand() const noexcept {
    if (materialGraphFocused_ && materialEditor_.OpenAssetId().IsValid()) {
        return commandStack_.CanUndo(EditorCommandHistoryKey::MaterialAsset(materialEditor_.OpenAssetId().value));
    }
    return commandStack_.CanUndo(EditorCommandHistoryKey::Scene());
}

bool EditorSceneContext::CanRedoSceneCommand() const noexcept {
    if (materialGraphFocused_ && materialEditor_.OpenAssetId().IsValid()) {
        return commandStack_.CanRedo(EditorCommandHistoryKey::MaterialAsset(materialEditor_.OpenAssetId().value));
    }
    return commandStack_.CanRedo(EditorCommandHistoryKey::Scene());
}

bool EditorSceneContext::UndoSceneCommand() {
    static_cast<void>(CommitHierarchyRename());
    inspector_.EndTextEdit();
    const bool materialHistoryActive = materialGraphFocused_ && materialEditor_.OpenAssetId().IsValid();
    const bool undone = materialHistoryActive
        ? commandStack_.Undo(EditorCommandHistoryKey::MaterialAsset(materialEditor_.OpenAssetId().value))
        : SceneCommands().Undo();
    if (undone && commandStack_.LastCompletedCommandAffectsOpenMaterialSource() &&
        commandStack_.LastCompletedCommandHistoryKey() == EditorCommandHistoryKey::MaterialAsset(materialEditor_.OpenAssetId().value)) {
        RefreshOpenMaterialEditorFromSource();
    } else if (undone && materialEditor_.OpenAssetId().IsValid()) {
        SyncMaterialEditorWorkingCopyRuntimePreview();
        sceneGraphCookPending_ = true;
        RequestOpenMaterialSceneGraphCook();
        MarkSceneRenderDirty();
    }
    return undone;
}

bool EditorSceneContext::RedoSceneCommand() {
    static_cast<void>(CommitHierarchyRename());
    inspector_.EndTextEdit();
    const bool materialHistoryActive = materialGraphFocused_ && materialEditor_.OpenAssetId().IsValid();
    const bool redone = materialHistoryActive
        ? commandStack_.Redo(EditorCommandHistoryKey::MaterialAsset(materialEditor_.OpenAssetId().value))
        : SceneCommands().Redo();
    if (redone && commandStack_.LastCompletedCommandAffectsOpenMaterialSource() &&
        commandStack_.LastCompletedCommandHistoryKey() == EditorCommandHistoryKey::MaterialAsset(materialEditor_.OpenAssetId().value)) {
        RefreshOpenMaterialEditorFromSource();
    } else if (redone && materialEditor_.OpenAssetId().IsValid()) {
        SyncMaterialEditorWorkingCopyRuntimePreview();
        sceneGraphCookPending_ = true;
        RequestOpenMaterialSceneGraphCook();
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

bool EditorSceneContext::IsAnyInlineTextEditActive() const noexcept {
    return IsHierarchyRenaming()
        || IsHierarchySearchFocused()
        || assetBrowser_.IsTextEditing()
        || assetBrowser_.IsSearchFocused()
        || Inspector().IsTextEditing()
        || IsMaterialGraphNodeRenameEditing()
        || IsMaterialGraphConstantInlineEditing()
        || IsMaterialEditorFindFocused();
}

bool EditorSceneContext::FrameSelectedEntitiesInViewport() noexcept {
    const kb::scene::SceneEntity primary = SelectedEntity();
    const std::vector<kb::scene::SceneEntity>& selected = SelectedHierarchyEntities();
    const std::optional<kb::scene::Vec3> center = EditorSceneSelectionPivot::Resolve(*scene_, selected, primary);
    if (!center.has_value()) {
        return false;
    }

    // Bounding-sphere radius of the selected entities around the pivot. A
    // single point-like selection collapses to zero spread; the default floor
    // then frames it at a comfortable distance.
    float maxSpread = 0.0F;
    for (const kb::scene::SceneEntity entity : selected) {
        if (!entity.IsValid() || !scene_->Entities().IsAlive(entity)) {
            continue;
        }
        const kb::scene::TransformComponent* transform = scene_->Transforms().TryGet(entity);
        if (transform == nullptr) {
            continue;
        }
        const kb::scene::Vec3 delta{
            transform->localPosition.x - center->x,
            transform->localPosition.y - center->y,
            transform->localPosition.z - center->z,
        };
        maxSpread = std::max(maxSpread, std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z));
    }

    // Frame every live scene-viewport camera (each docked/floating scene panel
    // renders its own per-panel camera; the parameterless default is not one of
    // them), so F actually reframes the viewport the user is looking at. The
    // 0.5s eased animation is advanced each frame by TickViewportFocusAnimations.
    constexpr float kFrameSelectionAnimationSeconds = 0.5F;
    static_cast<void>(viewportState_.FocusAllCamerasOn(*center, std::max(1.5F, maxSpread + 1.5F), kFrameSelectionAnimationSeconds));
    MarkSceneRenderDirty();
    return true;
}

bool EditorSceneContext::TickViewportFocusAnimations(float deltaSeconds) noexcept {
    return viewportState_.TickFocusAnimations(deltaSeconds);
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
    return ImportAssetFiles(sourceFiles, destinationVirtualFolder, {});
}

bool EditorSceneContext::ImportAssetFiles(
    std::span<const std::filesystem::path> sourceFiles,
    const std::filesystem::path& destinationVirtualFolder,
    const kb::assets::AssetImportOptions& options) {
    const kb::assets::AssetImportResult report = EditorSceneAssetBrowserCommands::ImportFilesWithReport(*scene_, assetBrowser_, sourceFiles, destinationVirtualFolder, options);
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
    EditorCrashBreadcrumbs::WriteValue("material_open", "begin asset", id.value);
    if (!id.IsValid()) {
        EditorCrashBreadcrumbs::Write("material_open", "invalid asset id");
        console_.Error("Materials", "No material asset was provided for the Material Editor.");
        return false;
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (metadata != nullptr && metadata->type == kb::render::kRenderMaterialGraphAssetType) {
        const kb::assets::AssetMetadata* rawGraphMetadata = metadata;
        const kb::assets::AssetId graphId = metadata->id;
        const std::filesystem::path graphPath = metadata->virtualPath;
        metadata = nullptr;
        for (const kb::assets::AssetMetadata& candidate : scene_->Assets().Manager().Registry().All()) {
            if (candidate.type != "RenderMaterial") {
                continue;
            }
            const std::optional<kb::render::RenderMaterialAssetData> candidateMaterial = ReadMaterialAsset(candidate.id);
            if (candidateMaterial.has_value() &&
                (candidateMaterial->graphSourceAssetId == graphId.value ||
                    (!candidateMaterial->graphSourceAssetPath.empty() && std::filesystem::path{ candidateMaterial->graphSourceAssetPath } == graphPath))) {
                id = candidate.id;
                metadata = &candidate;
                break;
            }
        }
        if (metadata == nullptr) {
            metadata = rawGraphMetadata;
        }
    }
    if (metadata == nullptr || (metadata->type != "RenderMaterial" && metadata->type != "RenderMaterialInstance" &&
            metadata->type != kb::render::kRenderMaterialGraphAssetType)) {
        EditorCrashBreadcrumbs::Write("material_open", metadata == nullptr ? "metadata missing" : "metadata is not material");
        console_.Error("Materials", "Selected asset is not a material document.");
        return false;
    }
    EditorCrashBreadcrumbs::Write("material_open", "metadata type=" + metadata->type + " path=" + metadata->virtualPath.generic_string());
    // Re-opening the material that is already open is a "focus the editor" gesture (double-click in
    // Project Files, Open from the context menu). Reloading it from disk here would silently discard the
    // unsaved working copy, so keep it. An armed Inspector text edit is deliberately excluded: it lives in
    // the Inspector, a reload leaves it untouched, and blocking the refresh over it would be a false alarm.
    if (id == materialEditor_.OpenAssetId() && materialEditor_.WorkingCopy().has_value() &&
        (materialEditor_.Dirty() || HasActiveMaterialAssetEdit() || materialEditor_.IsGraphNodeRenameEditing() ||
            materialEditor_.IsGraphConstantInlineEditDirty())) {
        EditorCrashBreadcrumbs::Write("material_open", "already open and dirty; kept unsaved working copy");
        console_.Warning(
            "Materials",
            "Material Editor already has unsaved edits for " + metadata->virtualPath.generic_string() +
                ". Kept them; Save them, or Revert and open the asset again to load it from disk.");
        return true;
    }
    if (!PrepareMaterialAssetSelectionChange(id)) {
        EditorCrashBreadcrumbs::Write("material_open", "selection change cancelled");
        return false;
    }
    ResetMaterialGraphTransientState();
    EditorCrashBreadcrumbs::Write("material_open", "clear runtime preview begin");
    ClearMaterialEditorWorkingCopyRuntimePreview();
    EditorCrashBreadcrumbs::Write("material_open", "clear runtime preview end");
    std::optional<kb::render::RenderMaterialInstanceAssetData> instanceDocument =
        metadata->type == "RenderMaterialInstance" ? ReadMaterialInstanceAsset(id) : std::nullopt;
    EditorCrashBreadcrumbs::Write("material_open", instanceDocument.has_value() ? "instance document loaded" : "not an instance or instance missing");
    std::vector<std::string> graphLoadDiagnostics;
    bool graphLoadDiagnosticsHaveError = false;
    std::optional<kb::render::RenderMaterialAssetData> materialDocument;
    if (metadata->type == kb::render::kRenderMaterialGraphAssetType) {
        const std::filesystem::path graphPath = ResolveAssetPath(scene_->Assets().Manager(), *metadata);
        kb::render::RenderMaterialAssetParseResult graphLoad =
            kb::render::RenderMaterialGraphAssetLoader::LoadGraphWithDiagnostics(graphPath, metadata->id);
        AppendMaterialAssetParseDiagnostics(
            graphLoad,
            graphLoadDiagnostics,
            graphLoadDiagnosticsHaveError,
            console_);
        if (graphLoad.asset.has_value()) {
            kb::render::RenderMaterialAssetData graphDocument{};
            graphDocument.materialType = "builtin.pbr";
            graphDocument.materialTypeVersion = 1U;
            graphDocument.hasExplicitMaterialType = true;
            graphDocument.hasExplicitMaterialTypeVersion = true;
            graphDocument.graphSourceAssetId = metadata->id.value;
            graphDocument.graphSourceAssetPath = metadata->virtualPath.generic_string();
            graphDocument.graph = graphLoad.asset->graph;
            graphDocument.graph.storageModel = "material-graph-asset";
            materialDocument = std::move(graphDocument);
        }
    } else {
        materialDocument = instanceDocument.has_value() && instanceDocument->parentMaterialAssetId.IsValid()
            ? ReadEffectiveMaterialAsset(instanceDocument->parentMaterialAssetId)
            : ReadMaterialAsset(id);
    }
    if (metadata->type == kb::render::kRenderMaterialGraphAssetType && !materialDocument.has_value()) {
        EditorCrashBreadcrumbs::Write("material_open", "standalone graph parse failed");
        console_.Error("Materials", "The standalone Material Graph could not be loaded.");
        return false;
    }
    if (materialDocument.has_value() &&
        (materialDocument->graphSourceAssetId != 0U || !materialDocument->graphSourceAssetPath.empty())) {
        const kb::assets::AssetMetadata* sourceGraph = ResolveTypedAssetReference(
            scene_->Assets().Manager(),
            materialDocument->graphSourceAssetId,
            materialDocument->graphSourceAssetPath,
            kb::render::kRenderMaterialGraphAssetType);
        const std::filesystem::path sourcePath = sourceGraph == nullptr
            ? std::filesystem::path{}
            : (sourceGraph->physicalPath.empty()
                ? scene_->Assets().Manager().Mounts().Resolve(sourceGraph->virtualPath).value_or(std::filesystem::path{})
                : sourceGraph->physicalPath);
        kb::render::RenderMaterialAssetParseResult sourceLoad;
        if (!sourcePath.empty()) {
            sourceLoad = kb::render::RenderMaterialGraphAssetLoader::LoadGraphWithDiagnostics(sourcePath, sourceGraph->id);
            AppendMaterialAssetParseDiagnostics(
                sourceLoad,
                graphLoadDiagnostics,
                graphLoadDiagnosticsHaveError,
                console_);
        }
        if (!sourceLoad.asset.has_value()) {
            EditorCrashBreadcrumbs::Write("material_open", "authoritative source graph missing");
            console_.Error("Materials", "The authoritative source Material Graph could not be loaded.");
            return false;
        }
        materialDocument->graph = sourceLoad.asset->graph;
        materialDocument->graph.storageModel = "material-graph-asset";
    }
    if (materialDocument.has_value()) {
        EditorCrashBreadcrumbs::Write(
            "material_open",
            "material document loaded nodes=" + std::to_string(materialDocument->graph.nodes.size()) +
                " links=" + std::to_string(materialDocument->graph.links.size()) +
                " params=" + std::to_string(materialDocument->graphParameterValues.size()));
    } else {
        EditorCrashBreadcrumbs::Write("material_open", "material document missing");
    }
    std::optional<kb::render::RenderMaterialAssetData> refreshedMaterialDocument;
    std::optional<kb::render::RenderMaterialTypeSchema> schema;
    std::vector<std::string> refreshDiagnostics;
    std::vector<std::string> materialTypeDiagnostics;
    bool materialTypeDiagnosticsHaveError = false;
    if (materialDocument.has_value()) {
        EditorCrashBreadcrumbs::Write("material_open", "load material type/schema begin");
        const std::optional<kb::render::RenderMaterialTypeDocument> materialType =
            LoadMaterialTypeDocumentForMaterial(scene_->Assets().Manager(), *materialDocument);
        EditorCrashBreadcrumbs::Write("material_open", materialType.has_value() ? "material type document loaded" : "material type document missing");
        if (materialType.has_value() && materialType->stableTypeId == materialDocument->materialType) {
            if (materialType->version != materialDocument->materialTypeVersion) {
                EditorCrashBreadcrumbs::Write("material_open", "schema refresh begin");
                kb::render::RenderMaterialSchemaRefreshResult refreshed =
                    kb::render::RefreshRenderMaterialGraphBackedMaterialSchema(*materialDocument, *materialType);
                refreshDiagnostics.reserve(refreshed.diagnostics.size());
                for (const kb::render::RenderMaterialSchemaRefreshDiagnostic& diagnostic : refreshed.diagnostics) {
                    refreshDiagnostics.push_back(MaterialSchemaRefreshDiagnosticLine(diagnostic));
                    console_.Warning("Materials", refreshDiagnostics.back());
                }
                refreshedMaterialDocument = std::move(refreshed.material);
                EditorCrashBreadcrumbs::WriteValue("material_open", "schema refresh diagnostics", refreshDiagnostics.size());
            }
            schema = materialType->schema;
        } else {
            schema = MaterialEditorSchemaForMaterial(scene_->Assets().Manager(), *materialDocument);
        }
        EditorCrashBreadcrumbs::Write("material_open", "type reference validation begin");
        AppendMaterialTypeReferenceValidationDiagnostics(
            scene_->Assets().Manager(),
            *metadata,
            refreshedMaterialDocument.has_value() ? *refreshedMaterialDocument : *materialDocument,
            materialTypeDiagnostics,
            materialTypeDiagnosticsHaveError);
        EditorCrashBreadcrumbs::WriteValue("material_open", "type reference diagnostics", materialTypeDiagnostics.size());
    }
    const kb::assets::AssetId previouslyOpenAsset = materialEditor_.OpenAssetId();
    if (previouslyOpenAsset.IsValid() && previouslyOpenAsset != id) {
        commandStack_.Clear(EditorCommandHistoryKey::MaterialAsset(previouslyOpenAsset.value));
    }
    EditorCrashBreadcrumbs::Write("material_open", "materialEditor.Open begin");
    materialEditor_.Open(id, std::move(materialDocument), std::move(schema), std::move(instanceDocument));
    if (const auto view = materialGraphViewStates_.find(id.value); view != materialGraphViewStates_.end()) {
        materialGraphZoom_ = view->second.zoom;
        materialGraphPanX_ = view->second.panX;
        materialGraphPanY_ = view->second.panY;
    }
    materialEditorDetailsScrollOffset_ = 0;
    EditorCrashBreadcrumbs::Write(
        "material_open",
        std::string{"materialEditor.Open end workingCopy="} + (materialEditor_.WorkingCopy().has_value() ? "yes" : "no"));
    if (refreshedMaterialDocument.has_value()) {
        EditorCrashBreadcrumbs::Write("material_open", "set refreshed working copy begin");
        materialEditor_.SetWorkingCopy(std::move(*refreshedMaterialDocument));
        MarkSceneRenderDirty();
        EditorCrashBreadcrumbs::Write("material_open", "set refreshed working copy end");
    }
    std::vector<std::string> openDiagnostics;
    openDiagnostics.reserve(
        graphLoadDiagnostics.size() + refreshDiagnostics.size() + materialTypeDiagnostics.size());
    std::ranges::move(graphLoadDiagnostics, std::back_inserter(openDiagnostics));
    std::ranges::move(refreshDiagnostics, std::back_inserter(openDiagnostics));
    for (const std::string& diagnostic : materialTypeDiagnostics) {
        if (materialTypeDiagnosticsHaveError) {
            console_.Error("Materials", diagnostic);
        } else {
            console_.Warning("Materials", diagnostic);
        }
    }
    std::ranges::move(materialTypeDiagnostics, std::back_inserter(openDiagnostics));
    if (!openDiagnostics.empty()) {
        materialEditor_.SetDiagnostics(
            std::move(openDiagnostics),
            graphLoadDiagnosticsHaveError || materialTypeDiagnosticsHaveError);
    }
    console_.Info("Materials", "Opened Material Editor: " + metadata->virtualPath.generic_string());
    EditorCrashBreadcrumbs::Write("material_open", "end success");
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

    return OpenMaterialEditorAsset(graph->id);
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
    const kb::assets::AssetId closingAsset = materialEditor_.OpenAssetId();
    try {
        if (materialEditor_.IsGraphNodeRenameEditing()) {
            static_cast<void>(CommitMaterialGraphNodeRenameEdit());
        }
        // Same contract as the rename above: a value typed into a constant node is pending work, not
        // scratch state, so commit it before ResetMaterialGraphTransientState cancels it. The commit
        // clears the edit on every outcome, so no failure handling is needed here.
        if (materialEditor_.IsGraphConstantInlineEditing()) {
            static_cast<void>(CommitMaterialGraphConstantInlineEdit());
        }
        ResetMaterialGraphTransientState();
        ClearMaterialEditorWorkingCopyRuntimePreview();
        MarkSceneRenderDirty();
    } catch (const std::exception& exception) {
        console_.Error("Materials", std::string{ "Material Editor cleanup failed while closing asset: " } + exception.what());
    } catch (...) {
        console_.Error("Materials", "Material Editor cleanup failed while closing asset with an unknown error.");
    }
    materialEditor_.Close();
    materialEditorDetailsScrollOffset_ = 0;
    if (closingAsset.IsValid()) {
        commandStack_.Clear(EditorCommandHistoryKey::MaterialAsset(closingAsset.value));
    }
}

bool EditorSceneContext::HasDirtyMaterialAssetEdit() const noexcept {
    if (materialEditor_.IsGraphNodeRenameEditing()) {
        return true;
    }
    // A typed inline constant value exists nowhere else, so counting it here is what makes the close/quit
    // prompts and the unsaved markers see it. Merely arming the edit (a click, prefilled buffer, nothing
    // typed) is not pending work and must not mark a byte-identical document as unsaved.
    if (materialEditor_.IsGraphConstantInlineEditDirty()) {
        return true;
    }
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
    if (nextAsset != materialEditor_.OpenAssetId() && materialEditor_.IsGraphNodeRenameEditing()) {
        static_cast<void>(CommitMaterialGraphNodeRenameEdit());
    }
    if (nextAsset != materialEditor_.OpenAssetId() && materialEditor_.IsGraphConstantInlineEditing()) {
        static_cast<void>(CommitMaterialGraphConstantInlineEdit());
    }
    if (!HasDirtyMaterialAssetEdit() || nextAsset == materialEditor_.OpenAssetId()) {
        return true;
    }
    console_.Warning("Materials", "Unsaved material value edit. Press Enter to save it or Escape to discard it before selecting another asset.");
    return false;
}

bool EditorSceneContext::PrepareMaterialEditorClose(std::string_view reason) {
    if (materialEditor_.IsGraphNodeRenameEditing()) {
        static_cast<void>(CommitMaterialGraphNodeRenameEdit());
    }
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
    if (metadata->type == kb::render::kRenderMaterialGraphAssetType) {
        const std::filesystem::path path = ResolveAssetPath(manager, *metadata);
        const kb::render::RenderMaterialAssetParseResult graphLoad =
            kb::render::RenderMaterialGraphAssetLoader::LoadGraphWithDiagnostics(path, metadata->id);
        if (!graphLoad.asset.has_value()) {
            return std::nullopt;
        }
        kb::render::RenderMaterialAssetData document{};
        document.materialType = "builtin.pbr";
        document.materialTypeVersion = 1U;
        document.hasExplicitMaterialType = true;
        document.hasExplicitMaterialTypeVersion = true;
        document.graphSourceAssetId = metadata->id.value;
        document.graphSourceAssetPath = metadata->virtualPath.generic_string();
        document.graph = graphLoad.asset->graph;
        document.graph.storageModel = "material-graph-asset";
        return document;
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
        materialNodePreviewWorkingCopy_.reset();
        if (materialPreviewNodePreviewEnabled_) {
            materialNodePreviewWorkingCopy_ =
                EditorMaterialNodePreviewBuilder::Build(*materialEditor_.WorkingCopy(), materialEditor_.SelectedNodeId());
            if (materialNodePreviewWorkingCopy_.has_value()) {
                workingCopy = &*materialNodePreviewWorkingCopy_;
            }
        }
    } else {
        materialNodePreviewWorkingCopy_.reset();
    }
    const kb::render::RenderMaterialGraphVariantUsage usage =
        materialPreviewNodePreviewEnabled_ && materialNodePreviewWorkingCopy_.has_value()
        ? kb::render::RenderMaterialGraphVariantUsage::NodePreview
        : kb::render::RenderMaterialGraphVariantUsage::Preview;
    // Cheap "did anything feeding the preview change?" key so SceneFor can skip the full content-hash recompute
    // on steady-state frames (see SceneFor). Combines the asset registry generation (external texture/material
    // changes) with the material editor document revision (in-editor edits) and, when node preview is on, the
    // selected node + variant usage that pick which derived working copy is shown.
    const auto mixRevision = [](std::uint64_t seed, std::uint64_t value) noexcept {
        return seed ^ (value + 0x9E3779B97F4A7C15ULL + (seed << 6U) + (seed >> 2U));
    };
    std::uint64_t previewInputRevision = mixRevision(0x21B7C0DEULL, scene_->Assets().Manager().Registry().Generation());
    if (workingCopy != nullptr) {
        previewInputRevision = mixRevision(previewInputRevision, materialEditor_.DocumentRevision());
        if (materialPreviewNodePreviewEnabled_ && materialNodePreviewWorkingCopy_.has_value()) {
            previewInputRevision = mixRevision(previewInputRevision, static_cast<std::uint64_t>(materialEditor_.SelectedNodeId()) + 1U);
            previewInputRevision = mixRevision(previewInputRevision, static_cast<std::uint64_t>(usage));
        }
    }
    const std::uint64_t revisionBefore = materialPreviewScene_->Revision();
    const auto resolveStart = std::chrono::steady_clock::now();
    const kb::scene::Scene& previewScene = materialPreviewScene_->SceneFor(*scene_, id, workingCopy, previewInputRevision);
    const bool previewRebuilt = materialPreviewScene_->Revision() != revisionBefore;
    if (previewRebuilt) {
        // [perf] One-shot log: only fires when the preview scene actually rebuilds (i.e. after an edit), never
        // on a steady frame. This is the synchronous resolve/flatten cost of reflecting a graph edit.
        const double resolveMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - resolveStart).count();
        std::ostringstream row;
        row << "[perf] preview resolve after edit: " << resolveMs << " ms (material=" << id.value << ")";
        console_.Info("MaterialPerf", row.str());
    }
    if (workingCopy != nullptr && materialGraphCookService_ != nullptr && previewRebuilt) {
        static_cast<void>(materialGraphCookService_->RequestCook(
            id,
            *workingCopy,
            MaterialPreviewGraphBuildContext(id, materialPreviewScene_->SceneSettings(), usage)));
    }
    return previewScene;
}

const EditorMaterialPreviewTelemetry& EditorSceneContext::MaterialPreviewTelemetry() const noexcept {
    return materialPreviewScene_->Telemetry();
}

const EditorMaterialPreviewPrimitivePolicy& EditorSceneContext::MaterialPreviewPrimitivePolicy() const noexcept {
    return materialPreviewScene_->PrimitivePolicy();
}

bool EditorSceneContext::SetMaterialPreviewPrimitivePolicy(EditorMaterialPreviewPrimitivePolicy policy) {
    const bool changed = materialPreviewScene_->SetPrimitivePolicy(policy);
    if (changed) {
        MarkSceneRenderDirty();
    }
    return changed;
}

bool EditorSceneContext::CycleMaterialPreviewPrimitive() {
    switch (materialPreviewScene_->PrimitivePolicy().kind) {
    case EditorMaterialPreviewPrimitiveKind::Sphere:
        return SetMaterialPreviewPrimitivePolicy(EditorMaterialPreviewPrimitivePolicy::Cylinder());
    case EditorMaterialPreviewPrimitiveKind::Cylinder:
        return SetMaterialPreviewPrimitivePolicy(EditorMaterialPreviewPrimitivePolicy::Cube());
    case EditorMaterialPreviewPrimitiveKind::Cube:
        return SetMaterialPreviewPrimitivePolicy(EditorMaterialPreviewPrimitivePolicy::Plane());
    case EditorMaterialPreviewPrimitiveKind::Plane:
    case EditorMaterialPreviewPrimitiveKind::CustomMesh:
    case EditorMaterialPreviewPrimitiveKind::Fallback:
        return SetMaterialPreviewPrimitivePolicy(EditorMaterialPreviewPrimitivePolicy::Sphere());
    }
    return false;
}

const kb::scene::Scene& EditorSceneContext::MaterialThumbnailScene(kb::assets::AssetId id) {
    if (materialThumbnailScene_ == nullptr) {
        materialThumbnailScene_ = std::make_unique<EditorMaterialPreviewScene>();
        static_cast<void>(materialThumbnailScene_->SetSceneSettings(materialPreviewScene_->SceneSettings()));
        static_cast<void>(materialThumbnailScene_->SetPrimitivePolicy(EditorMaterialPreviewPrimitivePolicy::Sphere()));
    }
    // Thumbnails show the material as saved on disk, so no working copy is passed: a tile must not change
    // while an unrelated edit is open in the Material Editor.
    // On-disk material, so the only thing that can change a tile is an asset registry mutation (reimport,
    // discovery, edit-then-save). Gate on the registry generation so a visible-but-unchanged tile stops
    // re-resolving (and re-decoding its textures) every frame the Project Files panel drains its queue.
    const std::uint64_t thumbnailInputRevision =
        0x7C0FFEEULL ^ (scene_->Assets().Manager().Registry().Generation() + 0x9E3779B97F4A7C15ULL);
    const std::uint64_t revisionBefore = materialThumbnailScene_->Revision();
    const kb::scene::Scene& thumbnailScene = materialThumbnailScene_->SceneFor(*scene_, id, nullptr, thumbnailInputRevision);
    // A graph-backed material needs its graph program, or the thumbnail would render the fallback instead
    // of the material. Only on a rebuild, so this costs one cook per thumbnail at most.
    if (materialGraphCookService_ != nullptr && materialThumbnailScene_->Revision() != revisionBefore) {
        if (const std::optional<kb::render::RenderMaterialAssetData> saved = ReadMaterialDocumentAsset(id);
            saved.has_value() && !saved->graph.nodes.empty()) {
            static_cast<void>(materialGraphCookService_->RequestCook(
                id,
                *saved,
                MaterialPreviewGraphBuildContext(
                    id,
                    materialThumbnailScene_->SceneSettings(),
                    kb::render::RenderMaterialGraphVariantUsage::Preview)));
        }
    }
    return thumbnailScene;
}

std::uint64_t EditorSceneContext::MaterialThumbnailSceneRevision() const noexcept {
    return materialThumbnailScene_ == nullptr ? 0U : materialThumbnailScene_->Revision();
}

std::uint64_t EditorSceneContext::RequestMaterialThumbnailCapture(const std::filesystem::path& path) {
    kb::scene::Scene* thumbnailScene = materialThumbnailScene_ == nullptr ? nullptr : materialThumbnailScene_->MutableScene();
    if (thumbnailScene == nullptr || path.empty()) {
        return 0U;
    }
    return kb::scene::SceneRenderFeedback::RequestScreenCapture(*thumbnailScene, path.generic_string());
}

kb::scene::SceneScreenCaptureStatus EditorSceneContext::MaterialThumbnailCaptureStatus(std::uint64_t captureId) const noexcept {
    const kb::scene::Scene* thumbnailScene = materialThumbnailScene_ == nullptr
        ? nullptr
        : const_cast<EditorMaterialPreviewScene*>(materialThumbnailScene_.get())->MutableScene();
    if (thumbnailScene == nullptr || captureId == 0U) {
        return kb::scene::SceneScreenCaptureStatus::Unknown;
    }
    return kb::scene::SceneRenderFeedback::ScreenCaptureStatus(*thumbnailScene, captureId);
}

const EditorMaterialPreviewSceneSettings& EditorSceneContext::MaterialPreviewSceneSettings() const noexcept {
    return materialPreviewScene_->SceneSettings();
}

bool EditorSceneContext::SetMaterialPreviewSceneSettings(EditorMaterialPreviewSceneSettings settings) {
    const bool changed = materialPreviewScene_->SetSceneSettings(settings);
    if (changed) {
        if (materialGraphCookService_ != nullptr && materialEditor_.OpenAssetId().IsValid() && materialEditor_.WorkingCopy().has_value()) {
            const kb::assets::AssetId openAsset = materialEditor_.OpenAssetId();
            static_cast<void>(materialGraphCookService_->RequestCook(
                openAsset,
                *materialEditor_.WorkingCopy(),
                MaterialPreviewGraphBuildContext(openAsset, settings)));
        }
        MarkSceneRenderDirty();
    }
    return changed;
}

bool EditorSceneContext::OrbitMaterialPreviewCamera(float deltaYawDegrees, float deltaPitchDegrees) {
    const EditorMaterialPreviewSceneSettings current = materialPreviewScene_->SceneSettings();
    // A pure camera move: no cook, no scene rebuild - just the next present. That is why it goes through the
    // scene's camera-only path rather than SetMaterialPreviewSceneSettings (which re-cooks the shader).
    if (!materialPreviewScene_->SetCameraOrbit(
            current.orbitYawDegrees + deltaYawDegrees,
            current.orbitPitchDegrees + deltaPitchDegrees,
            current.cameraDistance)) {
        return false;
    }
    // No MarkSceneRenderDirty(): that re-syncs the main scene to the GPU, and the preview repaints every
    // frame anyway (the pointer routers also invalidate the panel), so a camera move stays cheap.
    return true;
}

bool EditorSceneContext::ZoomMaterialPreviewCamera(float scale) {
    const EditorMaterialPreviewSceneSettings current = materialPreviewScene_->SceneSettings();
    if (!materialPreviewScene_->SetCameraOrbit(
            current.orbitYawDegrees,
            current.orbitPitchDegrees,
            current.cameraDistance * scale)) {
        return false;
    }
    return true;
}

bool EditorSceneContext::CycleMaterialPreviewSceneLightingPreset() {
    const EditorMaterialPreviewSceneSettings current = materialPreviewScene_->SceneSettings();
    EditorMaterialPreviewSceneSettings next = EditorMaterialPreviewSceneSettingsForPreset(
        NextEditorMaterialPreviewLightingPreset(current.lightingPreset));
    next.qualityLevel = current.qualityLevel;
    next.normalDebugView = current.normalDebugView;
    return SetMaterialPreviewSceneSettings(next);
}

bool EditorSceneContext::CycleMaterialPreviewQualityLevel() {
    EditorMaterialPreviewSceneSettings next = materialPreviewScene_->SceneSettings();
    next.qualityLevel = NextEditorMaterialPreviewQualityLevel(next.qualityLevel);
    return SetMaterialPreviewSceneSettings(next);
}

bool EditorSceneContext::MaterialPreviewNodePreviewEnabled() const noexcept {
    return materialPreviewNodePreviewEnabled_;
}

bool EditorSceneContext::SetMaterialPreviewNodePreviewEnabled(bool enabled) noexcept {
    if (materialPreviewNodePreviewEnabled_ == enabled) {
        return false;
    }
    materialPreviewNodePreviewEnabled_ = enabled;
    materialNodePreviewWorkingCopy_.reset();
    if (materialPreviewScene_ != nullptr) {
        materialPreviewScene_->Clear();
    }
    MarkSceneRenderDirty();
    return true;
}

bool EditorSceneContext::ToggleMaterialPreviewNodePreview() noexcept {
    return SetMaterialPreviewNodePreviewEnabled(!materialPreviewNodePreviewEnabled_);
}

bool EditorSceneContext::MaterialPreviewNormalDebugViewEnabled() const noexcept {
    return materialPreviewScene_->SceneSettings().normalDebugView;
}

bool EditorSceneContext::SetMaterialPreviewNormalDebugViewEnabled(bool enabled) {
    EditorMaterialPreviewSceneSettings next = materialPreviewScene_->SceneSettings();
    if (next.normalDebugView == enabled) {
        return false;
    }
    next.normalDebugView = enabled;
    return SetMaterialPreviewSceneSettings(next);
}

bool EditorSceneContext::ToggleMaterialPreviewNormalDebugView() {
    return SetMaterialPreviewNormalDebugViewEnabled(!MaterialPreviewNormalDebugViewEnabled());
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
    const kb::render::RenderMaterialGraphVariantUsage usage = materialPreviewNodePreviewEnabled_
        ? kb::render::RenderMaterialGraphVariantUsage::NodePreview
        : kb::render::RenderMaterialGraphVariantUsage::Preview;
    return materialGraphCookService_->LatestResult(
        openAsset,
        MaterialPreviewGraphBuildContext(openAsset, materialPreviewScene_->SceneSettings(), usage));
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
    const auto pumpStart = std::chrono::steady_clock::now();
    const std::vector<EditorMaterialGraphCookResult> results = materialGraphCookService_->DrainResults();
    for (const EditorMaterialGraphCookResult& result : results) {
        {
            std::ostringstream row;
            row << "cook-result asset=" << result.materialAssetId.value
                << " status=" << static_cast<int>(result.status)
                << " diagnostics=" << result.diagnostics.size();
            LogMaterialGraphDebug(console_, row.str());
        }
        if (result.status == EditorMaterialGraphCookStatus::Failed) {
            for (const std::string& diagnostic : result.diagnostics) {
                console_.Warning("Materials", "Graph shader cook: " + diagnostic);
                LogMaterialGraphDebug(console_, "cook-diagnostic " + diagnostic);
            }
        } else if (result.status == EditorMaterialGraphCookStatus::CookUnavailable && !result.diagnostics.empty()) {
            console_.Warning("Materials", "Graph shader cook: " + result.diagnostics.front());
            LogMaterialGraphDebug(console_, "cook-unavailable " + result.diagnostics.front());
        }
        const kb::render::RenderMaterialGraphVariantUsage visibleUsage = materialPreviewNodePreviewEnabled_
            ? kb::render::RenderMaterialGraphVariantUsage::NodePreview
            : kb::render::RenderMaterialGraphVariantUsage::Preview;
        if (result.materialAssetId == materialEditor_.OpenAssetId() && result.variantKey.usage == visibleUsage) {
            const bool cookSucceeded = result.status == EditorMaterialGraphCookStatus::Ready ||
                result.status == EditorMaterialGraphCookStatus::UpToDate;
            const bool hasLastGood = result.status == EditorMaterialGraphCookStatus::Stale;
            const bool fallbackApplied = result.status == EditorMaterialGraphCookStatus::Stale ||
                result.status == EditorMaterialGraphCookStatus::Failed ||
                result.status == EditorMaterialGraphCookStatus::CookUnavailable;
            materialEditor_.ApplyCookResult(
                result.diagnostics,
                cookSucceeded,
                result.HasGpuProgram(),
                hasLastGood,
                fallbackApplied);
            std::vector<MaterialEditorCookPassTelemetry> passTelemetry;
            passTelemetry.reserve(result.passes.size());
            for (const EditorMaterialGraphCookPassResult& pass : result.passes) {
                passTelemetry.push_back(MaterialEditorCookPassTelemetry{
                    .passName = pass.pass,
                    .succeeded = pass.succeeded,
                    .cacheHit = pass.cacheHit,
                    .binaryByteSize = pass.binaryByteSize,
                });
            }
            materialEditor_.ApplyCookMaterialStats(
                result.graphSourceHash,
                result.backendName,
                result.textureBindingCount,
                result.uniformCount,
                result.varyingCount,
                std::move(passTelemetry));
        }
    }
    // A freshly cooked program is picked up by the renderer's MaterialProgramRegistry on the next frame via the
    // shared cache root + runtime asset reload; surface that the affected entities must refresh. Only the
    // entities that use the just-cooked material changed, so re-sync JUST those - a full MarkSceneRenderDirty()
    // here forced a whole-scene GPU resync every time an async cook landed, i.e. ~1-2s after every single graph
    // edit on a content-heavy scene. The synchronous edit/resolve itself is ~2ms (measured: 0.4ms edit + ~1ms
    // resolve + 0.2ms codegen), so this deferred full resync was the real "adding a node lags" stall. Mirrors
    // the same full->incremental fix already applied to the edit path (see MarkMaterialAssetRenderDirty).
    if (!results.empty()) {
        std::vector<std::uint64_t> cookedMaterials;
        cookedMaterials.reserve(results.size());
        for (const EditorMaterialGraphCookResult& result : results) {
            if (result.materialAssetId.IsValid() &&
                std::find(cookedMaterials.begin(), cookedMaterials.end(), result.materialAssetId.value) == cookedMaterials.end()) {
                cookedMaterials.push_back(result.materialAssetId.value);
                MarkMaterialAssetRenderDirty(result.materialAssetId);
            }
        }
        // [perf] One-shot log when cook results land (only when an async shaderc cook finishes, never on an idle
        // frame). Shows how long draining + applying the result + marking the affected entities took.
        const double pumpMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pumpStart).count();
        std::ostringstream row;
        row << "[perf] cook results applied: " << results.size() << " result(s), " << cookedMaterials.size()
            << " material(s) re-synced incrementally, in " << pumpMs << " ms";
        console_.Info("MaterialPerf", row.str());
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
    if (materialEditor_.IsGraphNodeRenameEditing() && materialEditor_.GraphNodeRenameEditNodeId() != nodeId) {
        static_cast<void>(CommitMaterialGraphNodeRenameEdit());
    }
    const bool changed = materialEditor_.SelectNode(nodeId);
    if (changed && materialPreviewNodePreviewEnabled_) {
        materialNodePreviewWorkingCopy_.reset();
        materialPreviewScene_->Clear();
        MarkSceneRenderDirty();
    }
    return changed;
}

bool EditorSceneContext::SelectMaterialGraphNode(std::uint32_t nodeId, bool additive, bool toggle) {
    if (materialEditor_.IsGraphNodeRenameEditing() &&
        (materialEditor_.GraphNodeRenameEditNodeId() != nodeId || toggle)) {
        static_cast<void>(CommitMaterialGraphNodeRenameEdit());
    }
    bool changed = false;
    if (toggle) {
        changed = materialEditor_.ToggleNodeSelection(nodeId);
    } else if (additive) {
        changed = materialEditor_.AddNodeToSelection(nodeId);
    } else {
        changed = materialEditor_.SelectNode(nodeId);
    }
    if (changed && materialPreviewNodePreviewEnabled_) {
        materialNodePreviewWorkingCopy_.reset();
        materialPreviewScene_->Clear();
        MarkSceneRenderDirty();
    }
    return changed;
}

bool EditorSceneContext::SetMaterialGraphNodeSelection(std::vector<std::uint32_t> nodeIds, std::uint32_t primaryNodeId) {
    if (materialEditor_.IsGraphNodeRenameEditing() &&
        std::ranges::find(nodeIds, materialEditor_.GraphNodeRenameEditNodeId()) == nodeIds.end()) {
        static_cast<void>(CommitMaterialGraphNodeRenameEdit());
    }
    const bool changed = materialEditor_.SetNodeSelection(std::move(nodeIds), primaryNodeId);
    if (changed && materialPreviewNodePreviewEnabled_) {
        materialNodePreviewWorkingCopy_.reset();
        materialPreviewScene_->Clear();
        MarkSceneRenderDirty();
    }
    return changed;
}

bool EditorSceneContext::ClearMaterialGraphNodeSelection() {
    if (materialEditor_.IsGraphNodeRenameEditing()) {
        static_cast<void>(CommitMaterialGraphNodeRenameEdit());
    }
    const bool changed = materialEditor_.ClearNodeSelection();
    if (changed && materialPreviewNodePreviewEnabled_) {
        materialNodePreviewWorkingCopy_.reset();
        materialPreviewScene_->Clear();
        MarkSceneRenderDirty();
    }
    return changed;
}

std::uint32_t EditorSceneContext::SelectedMaterialGraphCommentId() const noexcept {
    return materialEditor_.SelectedCommentId();
}

bool EditorSceneContext::IsMaterialGraphCommentSelected(std::uint32_t commentId) const noexcept {
    return materialEditor_.IsCommentSelected(commentId);
}

bool EditorSceneContext::SelectMaterialGraphComment(std::uint32_t commentId) {
    if (materialEditor_.IsGraphNodeRenameEditing()) {
        static_cast<void>(CommitMaterialGraphNodeRenameEdit());
    }
    return materialEditor_.SelectComment(commentId);
}

bool EditorSceneContext::ClearMaterialGraphCommentSelection() {
    return materialEditor_.ClearCommentSelection();
}

bool EditorSceneContext::SelectMaterialGraphContextTarget(std::uint32_t nodeId, std::uint32_t commentId) {
    if (nodeId != 0U) {
        return IsMaterialGraphNodeSelected(nodeId) ? false : SelectMaterialGraphNode(nodeId);
    }
    if (commentId != 0U) {
        return IsMaterialGraphCommentSelected(commentId) ? false : SelectMaterialGraphComment(commentId);
    }
    return false;
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
    const float step = wheelDelta > 0 ? 1.10F : 0.90F;
    const float previousZoom = materialGraphZoom_;
    const float zoom = std::clamp(
        previousZoom * step,
        MaterialGraphInteractionPolicy::MinimumZoom,
        MaterialGraphInteractionPolicy::MaximumZoom);
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

void EditorSceneContext::SetMaterialGraphCanvasViewport(int width, int height) noexcept {
    if (width > 0) {
        materialGraphCanvasWidth_ = width;
    }
    if (height > 0) {
        materialGraphCanvasHeight_ = height;
    }
}

void EditorSceneContext::SetMaterialGraphCanvasViewport(int left, int top, int width, int height) noexcept {
    materialGraphCanvasLeft_ = left;
    materialGraphCanvasTop_ = top;
    SetMaterialGraphCanvasViewport(width, height);
}

int EditorSceneContext::MaterialGraphCanvasLeft() const noexcept {
    return materialGraphCanvasLeft_;
}

int EditorSceneContext::MaterialGraphCanvasTop() const noexcept {
    return materialGraphCanvasTop_;
}

int EditorSceneContext::MaterialGraphCanvasWidth() const noexcept {
    return materialGraphCanvasWidth_;
}

int EditorSceneContext::MaterialGraphCanvasHeight() const noexcept {
    return materialGraphCanvasHeight_;
}

bool EditorSceneContext::IsMaterialEditorFindFocused() const noexcept {
    return materialEditor_.IsFindFocused();
}

void EditorSceneContext::FocusMaterialEditorFind(bool focused) noexcept {
    materialEditor_.FocusFind(focused);
}

void EditorSceneContext::SetMaterialEditorFindQuery(std::string query) {
    materialEditor_.SetFindQuery(std::move(query));
}

void EditorSceneContext::AppendMaterialEditorFindText(wchar_t character) {
    materialEditor_.AppendFindText(character);
}

void EditorSceneContext::InsertMaterialEditorFindText(std::string_view text) {
    materialEditor_.InsertFindText(text);
}

void EditorSceneContext::BackspaceMaterialEditorFind() {
    materialEditor_.BackspaceFind();
}

void EditorSceneContext::ClearMaterialEditorFind() {
    materialEditor_.ClearFindQuery();
}

bool EditorSceneContext::FocusFirstMaterialEditorFindResult() {
    return FocusMaterialEditorFindResult(0U, materialGraphCanvasWidth_, materialGraphCanvasHeight_);
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

bool EditorSceneContext::FocusMaterialGraphNode(std::uint32_t nodeId) {
    if (nodeId == 0U || !materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    const kb::render::RenderMaterialGraphNode* node =
        kb::render::FindRenderMaterialGraphNode(materialEditor_.WorkingCopy()->graph, nodeId);
    if (node == nullptr) {
        return false;
    }
    static_cast<void>(SelectMaterialGraphNode(nodeId));
    materialGraphPanX_ = (materialGraphCanvasWidth_ / 2) -
        static_cast<int>(std::lround(static_cast<float>(node->positionX) * materialGraphZoom_));
    materialGraphPanY_ = (materialGraphCanvasHeight_ / 2) -
        static_cast<int>(std::lround(static_cast<float>(node->positionY) * materialGraphZoom_));
    materialGraphFocused_ = true;
    return true;
}

bool EditorSceneContext::FrameSelectedMaterialGraphNodes() {
    return FrameSelectedMaterialGraphNodes(materialGraphCanvasWidth_, materialGraphCanvasHeight_);
}

bool EditorSceneContext::FrameSelectedMaterialGraphNodes(int canvasWidth, int canvasHeight) {
    if (!materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    const kb::render::RenderMaterialGraphDocument& graph = materialEditor_.WorkingCopy()->graph;

    struct FrameItem {
        float x = 0.0F;
        float y = 0.0F;
        float width = 0.0F;
        float height = 0.0F;
        bool fixedScreenSize = false;
    };
    std::vector<FrameItem> frameItems;

    for (const std::uint32_t nodeId : materialEditor_.SelectedNodeIds()) {
        const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(graph, nodeId);
        if (node == nullptr) {
            continue;
        }
#if defined(_WIN32)
        const SIZE nodeSize = MaterialEditorPanelGraphNodeSize(node->kind);
        const float width = static_cast<float>(nodeSize.cx);
        const float height = static_cast<float>(nodeSize.cy);
#else
        const float width = 240.0F;
        const float height = 160.0F;
#endif
        frameItems.push_back(FrameItem{
            static_cast<float>(node->positionX),
            static_cast<float>(node->positionY),
            std::max(1.0F, width),
            std::max(1.0F, height),
            false,
        });
    }

    if (const std::uint32_t commentId = materialEditor_.SelectedCommentId(); commentId != 0U) {
        if (const std::optional<kb::render::RenderMaterialGraphCommentBox> comment = materialEditor_.GraphComment(commentId)) {
            frameItems.push_back(FrameItem{
                static_cast<float>(comment->positionX),
                static_cast<float>(comment->positionY),
                static_cast<float>(std::max<std::int32_t>(1, comment->width)),
                static_cast<float>(std::max<std::int32_t>(1, comment->height)),
                false,
            });
        }
    }

    if (frameItems.empty()) {
        return false;
    }

    SetMaterialGraphCanvasViewport(canvasWidth, canvasHeight);
    constexpr int padding = 48;
    const int fitWidth = std::max(1, materialGraphCanvasWidth_ - (padding * 2));
    const int fitHeight = std::max(1, materialGraphCanvasHeight_ - (padding * 2));
    const auto screenBoundsAt = [&frameItems](float zoom) noexcept {
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        for (const FrameItem& item : frameItems) {
            const float x = item.x * zoom;
            const float y = item.y * zoom;
            const float width = item.fixedScreenSize ? item.width : item.width * zoom;
            const float height = item.fixedScreenSize ? item.height : item.height * zoom;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x + width);
            maxY = std::max(maxY, y + height);
        }
        return std::array<float, 4>{ minX, minY, maxX, maxY };
    };
    const auto fits = [&screenBoundsAt, fitWidth, fitHeight](float zoom) noexcept {
        const std::array<float, 4> bounds = screenBoundsAt(zoom);
        return bounds[2] - bounds[0] <= static_cast<float>(fitWidth) &&
            bounds[3] - bounds[1] <= static_cast<float>(fitHeight);
    };
    float low = MaterialGraphInteractionPolicy::MinimumZoom;
    float high = MaterialGraphInteractionPolicy::MaximumZoom;
    if (fits(high)) {
        low = high;
    } else {
        for (int iteration = 0; iteration < 32; ++iteration) {
            const float candidate = (low + high) * 0.5F;
            if (fits(candidate)) {
                low = candidate;
            } else {
                high = candidate;
            }
        }
    }
    const float nextZoom = low;
    const std::array<float, 4> screenBounds = screenBoundsAt(nextZoom);
    const float centerX = (screenBounds[0] + screenBounds[2]) * 0.5F;
    const float centerY = (screenBounds[1] + screenBounds[3]) * 0.5F;
    const int nextPanX = (materialGraphCanvasWidth_ / 2) - static_cast<int>(std::lround(centerX));
    const int nextPanY = (materialGraphCanvasHeight_ / 2) - static_cast<int>(std::lround(centerY));
    const bool changed =
        std::fabs(materialGraphZoom_ - nextZoom) >= 0.0001F ||
        materialGraphPanX_ != nextPanX ||
        materialGraphPanY_ != nextPanY ||
        !materialGraphFocused_;
    materialGraphZoom_ = nextZoom;
    materialGraphPanX_ = nextPanX;
    materialGraphPanY_ = nextPanY;
    materialGraphFocused_ = true;
    return changed;
}

bool EditorSceneContext::SelectMaterialGraphUpstream() {
    return materialEditor_.SelectGraphUpstream();
}

bool EditorSceneContext::SelectMaterialGraphDownstream() {
    return materialEditor_.SelectGraphDownstream();
}

bool EditorSceneContext::AlignSelectedMaterialGraphNodes(kb::assets::AssetId id, MaterialEditorGraphAlignMode mode) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    std::vector<std::uint32_t> beforeSelectedNodeIds = materialEditor_.SelectedNodeIds();
    const std::uint32_t beforeSelectedCommentId = materialEditor_.SelectedCommentId();
    if (!materialEditor_.AlignSelectedGraphNodes(mode)) {
        console_.Warning("Materials", "Select at least two material graph nodes to align.");
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Align Material Graph Nodes", std::move(before), beforeSelectedNodeId, std::move(beforeSelectedNodeIds), beforeSelectedCommentId)) {
        console_.Warning("Materials", "Material graph alignment could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Aligned material graph selection.");
    return true;
}

bool EditorSceneContext::DistributeSelectedMaterialGraphNodes(kb::assets::AssetId id, MaterialEditorGraphDistributeAxis axis) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    std::vector<std::uint32_t> beforeSelectedNodeIds = materialEditor_.SelectedNodeIds();
    const std::uint32_t beforeSelectedCommentId = materialEditor_.SelectedCommentId();
    if (!materialEditor_.DistributeSelectedGraphNodes(axis)) {
        console_.Warning("Materials", "Select at least three spread-out material graph nodes to distribute.");
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Distribute Material Graph Nodes", std::move(before), beforeSelectedNodeId, std::move(beforeSelectedNodeIds), beforeSelectedCommentId)) {
        console_.Warning("Materials", "Material graph distribution could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Distributed material graph selection.");
    return true;
}

bool EditorSceneContext::PromoteSelectedMaterialGraphNodeToParameter(kb::assets::AssetId id) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    std::vector<std::uint32_t> beforeSelectedNodeIds = materialEditor_.SelectedNodeIds();
    const std::uint32_t beforeSelectedCommentId = materialEditor_.SelectedCommentId();
    std::uint32_t promotedNodeId = 0U;
    if (!materialEditor_.PromoteSelectedGraphNodeToParameter(&promotedNodeId)) {
        console_.Warning("Materials", "Select one constant scalar/vector/color node to promote to a parameter.");
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Promote Material Graph Node To Parameter", std::move(before), beforeSelectedNodeId, std::move(beforeSelectedNodeIds), beforeSelectedCommentId)) {
        console_.Warning("Materials", "Material graph parameter promotion could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Promoted material graph node #" + std::to_string(promotedNodeId) + " to a parameter.");
    return true;
}

std::uint64_t EditorSceneContext::MaterialGraphViewSignature(kb::assets::AssetId assetId) const noexcept {
    // Everything the built graph canvas depends on, folded into one value the panel can compare cheaply:
    // which document (and its revision), the view transform, and the live drag offsets.
    const auto fold = [](std::uint64_t hash, std::uint64_t value) noexcept {
        hash ^= value + 0x9E3779B97F4A7C15ULL + (hash << 6U) + (hash >> 2U);
        return hash;
    };
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    hash = fold(hash, assetId.value);
    hash = fold(hash, materialEditor_.OpenAssetId().value);
    hash = fold(hash, materialEditor_.DocumentRevision());
    hash = fold(hash, static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(materialGraphZoom_)));
    hash = fold(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(materialGraphPanX_)));
    hash = fold(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(materialGraphPanY_)));
    if (materialGraphNodeDragging_ && materialGraphDragAssetId_ == assetId) {
        hash = fold(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(materialGraphDragStartOffsetX_)));
        hash = fold(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(materialGraphDragStartOffsetY_)));
        for (const MaterialGraphDragNodeStart& start : materialGraphDragStartNodes_) {
            hash = fold(hash, start.nodeId);
        }
    }
    return hash;
}

std::uint64_t EditorSceneContext::MaterialGraphContentDrawSignature(kb::assets::AssetId assetId) const noexcept {
    const auto fold = [](std::uint64_t hash, std::uint64_t value) noexcept {
        hash ^= value + 0x9E3779B97F4A7C15ULL + (hash << 6U) + (hash >> 2U);
        return hash;
    };
    // Start from the view signature (document revision + transform + live drag) and add everything else the
    // node/link/comment drawing reads. Selection changes a node's border/glow; the preview-selected node is
    // highlighted; comment selection changes the comment box chrome; diagnostic markers are drawn per node and
    // can change from an async cook without a document edit; the registry generation covers texture previews.
    std::uint64_t hash = MaterialGraphViewSignature(assetId);
    hash = fold(hash, static_cast<std::uint64_t>(SelectedMaterialGraphNodeId()));
    for (const std::uint32_t nodeId : SelectedMaterialGraphNodeIds()) {
        hash = fold(hash, static_cast<std::uint64_t>(nodeId) | 0x100000000ULL);
    }
    if (materialEditor_.WorkingCopy().has_value()) {
        for (const kb::render::RenderMaterialGraphCommentBox& comment : materialEditor_.WorkingCopy()->graph.comments) {
            if (IsMaterialGraphCommentSelected(comment.id)) {
                hash = fold(hash, static_cast<std::uint64_t>(comment.id) | 0x200000000ULL);
            }
        }
    }
    for (const MaterialEditorGraphDiagnosticMarker& marker : materialEditor_.GraphDiagnosticMarkers()) {
        hash = fold(hash, static_cast<std::uint64_t>(marker.nodeId));
        hash = fold(hash, static_cast<std::uint64_t>(marker.severity));
    }
    hash = fold(hash, scene_->Assets().Manager().Registry().Generation());
    return hash;
}

int EditorSceneContext::MaterialGraphNodeOffsetX(kb::assets::AssetId assetId, std::uint32_t nodeId) const noexcept {
    if (!materialGraphNodeDragging_ || materialGraphDragAssetId_ != assetId || nodeId == 0U) {
        return 0;
    }
    for (const MaterialGraphDragNodeStart& start : materialGraphDragStartNodes_) {
        if (start.nodeId == nodeId) {
            return materialGraphDragStartOffsetX_;
        }
    }
    return 0;
}

int EditorSceneContext::MaterialGraphNodeOffsetY(kb::assets::AssetId assetId, std::uint32_t nodeId) const noexcept {
    if (!materialGraphNodeDragging_ || materialGraphDragAssetId_ != assetId || nodeId == 0U) {
        return 0;
    }
    for (const MaterialGraphDragNodeStart& start : materialGraphDragStartNodes_) {
        if (start.nodeId == nodeId) {
            return materialGraphDragStartOffsetY_;
        }
    }
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
    const int screenDeltaX = x - materialGraphDragStartX_;
    const int screenDeltaY = y - materialGraphDragStartY_;
    if (!materialGraphDragChanged_ &&
        !MaterialGraphInteractionPolicy::CrossedDragThreshold(screenDeltaX, screenDeltaY)) {
        return false;
    }
    const int rawDeltaX = static_cast<int>(std::lround(static_cast<float>(screenDeltaX) / std::max(0.1F, materialGraphZoom_)));
    const int rawDeltaY = static_cast<int>(std::lround(static_cast<float>(screenDeltaY) / std::max(0.1F, materialGraphZoom_)));
    // Smooth dragging: apply the raw (zoom-corrected) delta directly so the
    // node tracks the cursor. The previous grid snap (SnapCoordinate, 32u)
    // quantized the target every move, making nodes jump between grid cells
    // instead of following the pointer.
    const int deltaX = rawDeltaX;
    const int deltaY = rawDeltaY;
    if (deltaX == materialGraphDragStartOffsetX_ && deltaY == materialGraphDragStartOffsetY_) {
        return false;
    }
    materialGraphDragStartOffsetX_ = deltaX;
    materialGraphDragStartOffsetY_ = deltaY;
    materialGraphDragChanged_ = deltaX != 0 || deltaY != 0;
    return true;
}

int EditorSceneContext::MaterialEditorDetailsScrollOffset() const noexcept {
    return materialEditorDetailsScrollOffset_;
}

bool EditorSceneContext::SetMaterialEditorDetailsScrollOffset(int offset, int maxOffset) noexcept {
    const int clamped = std::clamp(offset, 0, std::max(0, maxOffset));
    if (materialEditorDetailsScrollOffset_ == clamped) {
        return false;
    }
    materialEditorDetailsScrollOffset_ = clamped;
    return true;
}

bool EditorSceneContext::ScrollMaterialEditorDetails(int wheelDelta, int maxOffset) noexcept {
    if (maxOffset <= 0) {
        return false;
    }
    const int direction = wheelDelta > 0 ? -1 : 1;
    return SetMaterialEditorDetailsScrollOffset(materialEditorDetailsScrollOffset_ + direction * 54, maxOffset);
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
    const int deltaX = materialGraphDragStartOffsetX_;
    const int deltaY = materialGraphDragStartOffsetY_;
    std::vector<MaterialGraphDragNodeStart> dragStartNodes = std::move(materialGraphDragStartNodes_);
    bool committedMove = !shouldRecord;
    if (shouldRecord) {
        std::vector<std::pair<std::uint32_t, std::pair<std::int32_t, std::int32_t>>> positions;
        positions.reserve(dragStartNodes.size());
        for (const MaterialGraphDragNodeStart& start : dragStartNodes) {
            positions.push_back({
                start.nodeId,
                {
                    static_cast<std::int32_t>(start.positionX + deltaX),
                    static_cast<std::int32_t>(start.positionY + deltaY),
                },
            });
        }
        committedMove = materialEditor_.MoveGraphNodes(positions);
    }
    materialGraphNodeDragging_ = false;
    materialGraphDragAssetId_ = {};
    materialGraphDragNodeId_ = 0U;
    materialGraphDragStartOffsetX_ = 0;
    materialGraphDragStartOffsetY_ = 0;
    materialGraphDragStartNodeX_ = 0;
    materialGraphDragStartNodeY_ = 0;
    materialGraphDragStartSelectedNodeId_ = 0U;
    materialGraphDragStartSelectedNodeIds_.clear();
    materialGraphDragStartNodes_.clear();
    materialGraphDragChanged_ = false;
    if (shouldRecord) {
        if (!committedMove) {
            return false;
        }
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
    materialGraphCommentDragMemberNodeIds_ = materialEditor_.GraphNodeIdsInsideComment(commentId);
    materialGraphCommentDragChanged_ = false;
    materialGraphCommentDragging_ = true;
    return true;
}

bool EditorSceneContext::DragMaterialGraphComment(int x, int y) {
    if (!materialGraphCommentDragging_ || !materialGraphCommentDragAssetId_.IsValid() || materialGraphCommentDragId_ == 0U) {
        return false;
    }
    const int screenDeltaX = x - materialGraphCommentDragStartX_;
    const int screenDeltaY = y - materialGraphCommentDragStartY_;
    if (!materialGraphCommentDragChanged_ &&
        !MaterialGraphInteractionPolicy::CrossedDragThreshold(screenDeltaX, screenDeltaY)) {
        return false;
    }
    const int deltaX = static_cast<int>(std::lround(static_cast<float>(screenDeltaX) / std::max(0.1F, materialGraphZoom_)));
    const int deltaY = static_cast<int>(std::lround(static_cast<float>(screenDeltaY) / std::max(0.1F, materialGraphZoom_)));
    // Smooth dragging (matches DragMaterialGraphNode): track the cursor with the
    // raw zoom-corrected delta rather than quantizing the target to the 32u grid.
    const std::int32_t nextX = static_cast<std::int32_t>(materialGraphCommentDragStartCommentX_ + deltaX);
    const std::int32_t nextY = static_cast<std::int32_t>(materialGraphCommentDragStartCommentY_ + deltaY);
    if (!materialEditor_.MoveGraphCommentGroup(
            materialGraphCommentDragId_,
            nextX,
            nextY,
            materialGraphCommentDragMemberNodeIds_)) {
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
    materialGraphCommentDragMemberNodeIds_.clear();
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

bool EditorSceneContext::CancelMaterialGraphCommentDrag() {
    if (!materialGraphCommentDragging_) {
        return false;
    }
    if (materialGraphCommentDragStartDocument_.has_value() &&
        materialEditor_.OpenAssetId() == materialGraphCommentDragAssetId_) {
        materialEditor_.SetWorkingCopy(*materialGraphCommentDragStartDocument_);
        static_cast<void>(materialEditor_.SetNodeSelection(
            materialGraphCommentDragStartSelectedNodeIds_,
            materialGraphCommentDragStartSelectedNodeId_));
        if (materialGraphCommentDragStartSelectedCommentId_ != 0U) {
            static_cast<void>(materialEditor_.SelectComment(materialGraphCommentDragStartSelectedCommentId_));
        }
        materialEditor_.ClearDiagnostics();
    }
    materialGraphCommentDragging_ = false;
    materialGraphCommentDragAssetId_ = {};
    materialGraphCommentDragId_ = 0U;
    materialGraphCommentDragStartX_ = 0;
    materialGraphCommentDragStartY_ = 0;
    materialGraphCommentDragStartCommentX_ = 0;
    materialGraphCommentDragStartCommentY_ = 0;
    materialGraphCommentDragStartDocument_.reset();
    materialGraphCommentDragStartSelectedNodeId_ = 0U;
    materialGraphCommentDragStartSelectedNodeIds_.clear();
    materialGraphCommentDragMemberNodeIds_.clear();
    materialGraphCommentDragStartSelectedCommentId_ = 0U;
    materialGraphCommentDragChanged_ = false;
    return true;
}

bool EditorSceneContext::IsMaterialGraphCommentDragging() const noexcept {
    return materialGraphCommentDragging_;
}

bool EditorSceneContext::BeginMaterialGraphBoxSelection(
    kb::assets::AssetId assetId,
    int x,
    int y,
    MaterialGraphSelectionOperation operation) noexcept {
    if (!assetId.IsValid() || materialEditor_.OpenAssetId() != assetId || !materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    materialGraphBoxSelectionAssetId_ = assetId;
    materialGraphBoxSelectionStartX_ = x;
    materialGraphBoxSelectionStartY_ = y;
    materialGraphBoxSelectionCurrentX_ = x;
    materialGraphBoxSelectionCurrentY_ = y;
    materialGraphBoxSelectionOperation_ = operation;
    materialGraphBoxSelectionBaseNodeIds_ = materialEditor_.SelectedNodeIds();
    materialGraphBoxSelectionBasePrimaryNodeId_ = materialEditor_.SelectedNodeId();
    materialGraphBoxSelectionMoved_ = false;
    materialGraphBoxSelecting_ = true;
    return true;
}

bool EditorSceneContext::DragMaterialGraphBoxSelection(int x, int y) noexcept {
    if (!materialGraphBoxSelecting_) {
        return false;
    }
    const int deltaX = x - materialGraphBoxSelectionStartX_;
    const int deltaY = y - materialGraphBoxSelectionStartY_;
    if (!materialGraphBoxSelectionMoved_ &&
        !MaterialGraphInteractionPolicy::CrossedDragThreshold(deltaX, deltaY)) {
        return false;
    }
    if (materialGraphBoxSelectionCurrentX_ == x && materialGraphBoxSelectionCurrentY_ == y) {
        return false;
    }
    materialGraphBoxSelectionMoved_ = true;
    materialGraphBoxSelectionCurrentX_ = x;
    materialGraphBoxSelectionCurrentY_ = y;
    return true;
}

bool EditorSceneContext::EndMaterialGraphBoxSelection(std::vector<std::uint32_t> nodeIds, std::uint32_t primaryNodeId) {
    if (!materialGraphBoxSelecting_) {
        return false;
    }
    const MaterialGraphSelectionOperation operation = materialGraphBoxSelectionOperation_;
    std::vector<std::uint32_t> baseSelection = std::move(materialGraphBoxSelectionBaseNodeIds_);
    const std::uint32_t basePrimaryNodeId = materialGraphBoxSelectionBasePrimaryNodeId_;
    materialGraphBoxSelectionAssetId_ = {};
    materialGraphBoxSelecting_ = false;
    materialGraphBoxSelectionOperation_ = MaterialGraphSelectionOperation::Replace;
    materialGraphBoxSelectionBasePrimaryNodeId_ = 0U;
    materialGraphBoxSelectionMoved_ = false;

    nodeIds.erase(std::remove(nodeIds.begin(), nodeIds.end(), 0U), nodeIds.end());
    std::sort(nodeIds.begin(), nodeIds.end());
    nodeIds.erase(std::unique(nodeIds.begin(), nodeIds.end()), nodeIds.end());
    std::sort(baseSelection.begin(), baseSelection.end());
    baseSelection.erase(std::unique(baseSelection.begin(), baseSelection.end()), baseSelection.end());

    std::vector<std::uint32_t> result;
    switch (operation) {
    case MaterialGraphSelectionOperation::Replace:
        result = nodeIds;
        break;
    case MaterialGraphSelectionOperation::Add:
        std::set_union(
            baseSelection.begin(), baseSelection.end(),
            nodeIds.begin(), nodeIds.end(),
            std::back_inserter(result));
        break;
    case MaterialGraphSelectionOperation::Invert:
        std::set_symmetric_difference(
            baseSelection.begin(), baseSelection.end(),
            nodeIds.begin(), nodeIds.end(),
            std::back_inserter(result));
        break;
    case MaterialGraphSelectionOperation::Remove:
        std::set_difference(
            baseSelection.begin(), baseSelection.end(),
            nodeIds.begin(), nodeIds.end(),
            std::back_inserter(result));
        break;
    }

    const auto contains = [&result](std::uint32_t nodeId) {
        return nodeId != 0U && std::binary_search(result.begin(), result.end(), nodeId);
    };
    std::uint32_t resolvedPrimary = 0U;
    if ((operation == MaterialGraphSelectionOperation::Replace || operation == MaterialGraphSelectionOperation::Add) &&
        contains(primaryNodeId)) {
        resolvedPrimary = primaryNodeId;
    } else if (contains(basePrimaryNodeId)) {
        resolvedPrimary = basePrimaryNodeId;
    } else if (contains(primaryNodeId)) {
        resolvedPrimary = primaryNodeId;
    } else if (!result.empty()) {
        resolvedPrimary = result.front();
    }
    return materialEditor_.SetNodeSelection(std::move(result), resolvedPrimary);
}

bool EditorSceneContext::IsMaterialGraphBoxSelecting() const noexcept {
    return materialGraphBoxSelecting_;
}

bool EditorSceneContext::MaterialGraphBoxSelectionAdditive() const noexcept {
    return materialGraphBoxSelectionOperation_ != MaterialGraphSelectionOperation::Replace;
}

MaterialGraphSelectionOperation EditorSceneContext::MaterialGraphBoxSelectionOperation() const noexcept {
    return materialGraphBoxSelectionOperation_;
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
    const int deltaX = x - materialGraphPanStartX_;
    const int deltaY = y - materialGraphPanStartY_;
    if (!materialGraphPanMoved_ && !MaterialGraphInteractionPolicy::CrossedDragThreshold(deltaX, deltaY)) {
        return false;
    }
    const int newPanX = materialGraphPanStartOffsetX_ + deltaX;
    const int newPanY = materialGraphPanStartOffsetY_ + deltaY;
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

bool EditorSceneContext::BeginMaterialPreviewOrbit(int x, int y) noexcept {
    materialPreviewOrbitDragging_ = true;
    materialPreviewOrbitLastX_ = x;
    materialPreviewOrbitLastY_ = y;
    return true;
}

bool EditorSceneContext::DragMaterialPreviewOrbit(int x, int y) {
    if (!materialPreviewOrbitDragging_) {
        return false;
    }
    // Screen-pixel drag to degrees: a horizontal drag swings yaw, vertical drag swings pitch. Dragging right
    // rotates the object so its right side turns toward the viewer (yaw decreases), the Unreal convention -
    // the horizontal axis is negated so left/right feels like grabbing and turning the object.
    constexpr float degreesPerPixel = 0.4F;
    const float deltaYaw = -static_cast<float>(x - materialPreviewOrbitLastX_) * degreesPerPixel;
    const float deltaPitch = static_cast<float>(y - materialPreviewOrbitLastY_) * degreesPerPixel;
    materialPreviewOrbitLastX_ = x;
    materialPreviewOrbitLastY_ = y;
    return OrbitMaterialPreviewCamera(deltaYaw, deltaPitch);
}

bool EditorSceneContext::EndMaterialPreviewOrbit() noexcept {
    if (!materialPreviewOrbitDragging_) {
        return false;
    }
    materialPreviewOrbitDragging_ = false;
    return true;
}

bool EditorSceneContext::IsMaterialPreviewOrbiting() const noexcept {
    return materialPreviewOrbitDragging_;
}

bool EditorSceneContext::HasMaterialGraphPanMoved() const noexcept {
    return materialGraphPanMoved_;
}

bool EditorSceneContext::AddMaterialGraphNode(
    kb::assets::AssetId id,
    kb::render::RenderMaterialGraphNodeKind kind,
    int graphX,
    int graphY) {
    // Reachable corruption, not theory: with a node held mid-drag the graph still has keyboard focus, so
    // Space opens the palette and Enter lands here. The edit records commandA{D0->D1}; the mouse-up then
    // records commandB whose "before" is the snapshot the drag took at D0, so one Undo of B erases the new
    // node as well as the move. Same guard as paste/duplicate/delete. (The wire-drop flow is unaffected: a
    // pending pin connection routes through AddMaterialGraphNodeForPendingConnection, which is deliberately
    // not guarded - creating the node IS how that gesture finishes.)
    if (HasMaterialGraphGestureInFlight()) {
        return false;
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (metadata == nullptr || (metadata->type != "RenderMaterial" && metadata->type != kb::render::kRenderMaterialGraphAssetType) || materialEditor_.OpenAssetId() != id) {
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
    if (metadata == nullptr || (metadata->type != "RenderMaterial" && metadata->type != kb::render::kRenderMaterialGraphAssetType) || materialEditor_.OpenAssetId() != id ||
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
    if (HasMaterialGraphGestureInFlight()) { // see AddMaterialGraphNode
        return false;
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (metadata == nullptr || (metadata->type != "RenderMaterial" && metadata->type != kb::render::kRenderMaterialGraphAssetType) || materialEditor_.OpenAssetId() != id) {
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
    if (HasMaterialGraphGestureInFlight()) { // see AddMaterialGraphNode
        return false;
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
    if (metadata == nullptr || (metadata->type != "RenderMaterial" && metadata->type != kb::render::kRenderMaterialGraphAssetType) || materialEditor_.OpenAssetId() != id) {
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

bool EditorSceneContext::ExpandMaterialGraphComposite(kb::assets::AssetId id, std::uint32_t compositeId) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value() || compositeId == 0U) {
        return false;
    }
    const std::optional<kb::render::RenderMaterialGraphCompositeSubgraph> composite =
        materialEditor_.GraphCompositeSubgraph(compositeId);
    if (!composite.has_value() || !composite->collapsed) {
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    std::vector<std::uint32_t> beforeSelectedNodeIds = materialEditor_.SelectedNodeIds();
    const std::uint32_t beforeSelectedCommentId = materialEditor_.SelectedCommentId();
    if (!materialEditor_.SetGraphCompositeCollapsed(compositeId, false)) {
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(
            id,
            "Expand Material Graph Composite",
            std::move(before),
            beforeSelectedNodeId,
            std::move(beforeSelectedNodeIds),
            beforeSelectedCommentId)) {
        return false;
    }
    console_.Info("Materials", "Expanded material graph composite #" + std::to_string(compositeId) + ".");
    return true;
}

bool EditorSceneContext::DeleteSelectedMaterialGraphNode(kb::assets::AssetId id) {
    if (materialEditor_.OpenAssetId() != id) {
        return false;
    }
    // A gesture owns the working copy until it ends: editing through a live drag records a stale "before"
    // in the undo history, and doing it inside an open rewire transaction folds the change into something a
    // cancel then throws away. The same guard sits on paste/duplicate/delete-comment. Guarded here rather
    // than at each keyboard route, so every caller - shortcut, context menu, future CLI - gets the same
    // answer.
    if (HasMaterialGraphGestureInFlight()) {
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
    if (HasMaterialGraphGestureInFlight()) {
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

bool EditorSceneContext::SetMaterialGraphCommentText(kb::assets::AssetId id, std::uint32_t commentId, std::string_view text) {
    if (materialEditor_.OpenAssetId() != id || commentId == 0U || !materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    if (HasMaterialGraphGestureInFlight()) {
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    std::vector<std::uint32_t> beforeSelectedNodeIds = materialEditor_.SelectedNodeIds();
    const std::uint32_t beforeSelectedCommentId = materialEditor_.SelectedCommentId();
    if (!materialEditor_.SetGraphCommentText(commentId, text)) {
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Edit Material Graph Comment Text", std::move(before), beforeSelectedNodeId, std::move(beforeSelectedNodeIds), beforeSelectedCommentId)) {
        return false;
    }
    console_.Info("Materials", "Edited material graph comment.");
    return true;
}

bool EditorSceneContext::SetMaterialGraphCommentColor(kb::assets::AssetId id, std::uint32_t commentId, std::uint32_t color) {
    if (materialEditor_.OpenAssetId() != id || commentId == 0U || !materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    if (HasMaterialGraphGestureInFlight()) {
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    std::vector<std::uint32_t> beforeSelectedNodeIds = materialEditor_.SelectedNodeIds();
    const std::uint32_t beforeSelectedCommentId = materialEditor_.SelectedCommentId();
    if (!materialEditor_.SetGraphCommentColor(commentId, color)) {
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Edit Material Graph Comment Color", std::move(before), beforeSelectedNodeId, std::move(beforeSelectedNodeIds), beforeSelectedCommentId)) {
        return false;
    }
    console_.Info("Materials", "Recolored material graph comment.");
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
    if (HasMaterialGraphGestureInFlight()) {
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
    if (HasMaterialGraphGestureInFlight()) {
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
        const bool runtimeChanged = materialEditor_.WorkingCopy().has_value() &&
            MaterialWorkingCopyRuntimeContentHash(*materialEditor_.WorkingCopy()) !=
                MaterialWorkingCopyRuntimeContentHash(*materialGraphWorkingCopyTransactionBefore_);
        materialEditor_.SetWorkingCopy(*materialGraphWorkingCopyTransactionBefore_);
        static_cast<void>(materialEditor_.SetNodeSelection(selectedNodeIds, selectedNodeId));
        if (selectedCommentId != 0U) {
            static_cast<void>(materialEditor_.SelectComment(selectedCommentId));
        }
        if (runtimeChanged) {
            materialEditor_.ClearDiagnostics();
            SyncMaterialEditorWorkingCopyRuntimePreview();
            sceneGraphCookPending_ = true;
            RequestOpenMaterialSceneGraphCook();
            MarkSceneRenderDirty();
        }
    }
    ClearMaterialGraphWorkingCopyTransaction();
}

bool EditorSceneContext::HasMaterialGraphWorkingCopyTransaction() const noexcept {
    return materialGraphWorkingCopyTransactionAssetId_.IsValid() && materialGraphWorkingCopyTransactionBefore_.has_value();
}

bool EditorSceneContext::HasMaterialGraphGestureInFlight() const noexcept {
    return materialGraphNodeDragging_ || materialGraphCommentDragging_ || HasMaterialGraphPinConnection() ||
        HasMaterialGraphWorkingCopyTransaction();
}

bool EditorSceneContext::SettleMaterialGraphGesture() {
    if (!HasMaterialGraphGestureInFlight()) {
        return false;
    }
    // Order matters. The rewire goes first: while its transaction is open, RecordMaterialGraphWorkingCopyEdit
    // folds every edit into the transaction instead of creating an undo command, so a drag committed before
    // the cancel would be swallowed and then thrown away by the cancel's restore. Cancelling first puts the
    // document back, and the drags below then commit against it as ordinary, undoable edits.
    bool settled = CancelMaterialGraphPinConnection();
    settled = EndMaterialGraphNodeDrag() || settled;
    settled = EndMaterialGraphCommentDrag() || settled;
    // Backstop. Today a transaction is only ever opened by the rewire gesture and is always cancelled with
    // it, so this is unreachable - but if one ever outlived its gesture, the guard would refuse Save for the
    // rest of the session with no way back. Settling has to mean settled.
    if (HasMaterialGraphWorkingCopyTransaction()) {
        CancelMaterialGraphWorkingCopyTransaction();
        settled = true;
    }
    return settled;
}

bool EditorSceneContext::SetMaterialGraphTextureSampleAsset(kb::assets::AssetId id, std::uint32_t nodeId, kb::assets::AssetId textureId) {
    {
        std::ostringstream row;
        row << "set-texture-request material=" << id.value
            << " node=" << nodeId
            << " textureAssetId=" << textureId.value;
        LogMaterialGraphDebug(console_, row.str());
    }
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value() || nodeId == 0U) {
        LogMaterialGraphDebug(console_, "set-texture-rejected editor state/open asset mismatch");
        return false;
    }
    if (textureId.IsValid()) {
        const kb::assets::AssetMetadata* texture = scene_->Assets().Manager().Registry().Find(textureId);
        if (texture == nullptr || !IsTextureAsset(*texture)) {
            console_.Error("Materials", "Texture Sample rejected a non-texture asset.");
            LogMaterialGraphDebug(console_, "set-texture-rejected non-texture asset=" + std::to_string(textureId.value));
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
    if (node == nullptr || !IsMaterialGraphTextureAssetNode(node->kind)) {
        LogMaterialGraphDebug(console_, "set-texture-rejected node missing/not texture node=" + std::to_string(nodeId));
        return false;
    }
    if (node->parameter.stableId.empty()) {
        if (node->kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture) {
            node->parameter.stableId = "texture" + std::to_string(node->id);
        } else if (IsMaterialGraphTextureObjectNode(node->kind)) {
            node->parameter.stableId = "textureObject" + std::to_string(node->id);
        } else {
            node->parameter.stableId = "textureSample" + std::to_string(node->id);
        }
    }
    if (node->parameter.displayName.empty()) {
        if (node->kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture) {
            node->parameter.displayName = "Texture " + std::to_string(node->id);
        } else if (IsMaterialGraphTextureObjectNode(node->kind)) {
            node->parameter.displayName = "Texture Object " + std::to_string(node->id);
        } else {
            node->parameter.displayName = "Texture Sample " + std::to_string(node->id);
        }
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
    if (materialEditor_.WorkingCopy().has_value()) {
        LogMaterialGraphDebugDocument(console_, "set-texture-after-working-copy", *materialEditor_.WorkingCopy());
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Set Texture Sample Asset", std::move(before), materialEditor_.SelectedNodeId())) {
        console_.Warning("Materials", "Texture Sample asset change could not be recorded.");
        LogMaterialGraphDebug(console_, "set-texture-record-failed node=" + std::to_string(nodeId));
        return false;
    }
    console_.Info("Materials", textureId.IsValid() ? "Texture Sample asset assigned." : "Texture Sample asset cleared.");
    return true;
}

bool EditorSceneContext::SetMaterialGraphConstantColorValue(
    kb::assets::AssetId id,
    std::uint32_t nodeId,
    const std::array<float, 4U>& color) {
    return SetMaterialGraphNodeColorPropertyValue(id, nodeId, "constant.color", color);
}

bool EditorSceneContext::SetMaterialGraphNodeColorPropertyValue(
    kb::assets::AssetId id,
    std::uint32_t nodeId,
    std::string_view propertyId,
    const std::array<float, 4U>& color) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value() || nodeId == 0U) {
        console_.Error("Materials", "Open the material in Material Editor before editing graph colors.");
        return false;
    }

    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    if (!materialEditor_.SetGraphNodeColorPropertyValue(nodeId, propertyId, color)) {
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

bool EditorSceneContext::SetMaterialGraphSetting(kb::assets::AssetId id, std::string_view propertyId, std::string_view value) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value()) {
        console_.Error("Materials", "Open the material in Material Editor before editing its settings.");
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    if (!materialEditor_.SetGraphMaterialSetting(propertyId, value)) {
        return false; // no-op or invalid value: the setter already declined, nothing to record
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Edit Material Setting", std::move(before), beforeSelectedNodeId)) {
        return false;
    }
    console_.Info("Materials", "Edited material setting '" + std::string{ propertyId } + "'.");
    return true;
}

void EditorSceneContext::ToggleMaterialGraphSettingDropdown(std::string propertyId) {
    materialEditor_.ToggleMaterialSettingDropdown(std::move(propertyId));
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

bool EditorSceneContext::SetMaterialGraphNodeTextProperty(
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
    std::vector<std::uint32_t> beforeSelectedNodeIds = materialEditor_.SelectedNodeIds();
    const std::uint32_t beforeSelectedCommentId = materialEditor_.SelectedCommentId();
    if (propertyId == "function.assetId") {
        std::uint64_t functionAssetId = 0U;
        if (!ParseDecimalAssetId(value, functionAssetId) || functionAssetId == 0U) {
            console_.Error("Materials", "Material Function Call requires a numeric function asset id.");
            return false;
        }
        const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(kb::assets::AssetId{ functionAssetId });
        if (metadata == nullptr || metadata->type != kb::render::kRenderMaterialFunctionAssetType) {
            console_.Error("Materials", "Material Function Call rejected an asset that is not a Material Function.");
            return false;
        }
        const std::optional<kb::render::RenderMaterialFunctionAssetData> function =
            kb::render::RenderMaterialFunctionAssetLoader::LoadFunction(ResolveAssetPath(scene_->Assets().Manager(), *metadata));
        if (!function.has_value()) {
            console_.Error("Materials", "Material Function Call could not load the selected function asset.");
            return false;
        }
        if (!materialEditor_.SetGraphMaterialFunctionCallSignature(nodeId, functionAssetId, function->graph)) {
            console_.Error("Materials", "Material graph function call value is invalid.");
            return false;
        }
    } else {
        if (!materialEditor_.SetGraphNodeTextProperty(nodeId, propertyId, value)) {
            console_.Error("Materials", "Material graph node property value is invalid.");
            return false;
        }
    }
    if (!RecordMaterialGraphWorkingCopyEdit(
            id,
            "Edit Material Graph Node Property",
            std::move(before),
            beforeSelectedNodeId,
            std::move(beforeSelectedNodeIds),
            beforeSelectedCommentId)) {
        console_.Warning("Materials", "Material graph node property change could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Edited material graph node property '" + std::string{ propertyId } + "'.");
    return true;
}

bool EditorSceneContext::SetMaterialGraphNodeDisplayName(kb::assets::AssetId id, std::uint32_t nodeId, std::string_view displayName) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value() || nodeId == 0U) {
        return false;
    }

    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    std::vector<std::uint32_t> beforeSelectedNodeIds = materialEditor_.SelectedNodeIds();
    const std::uint32_t beforeSelectedCommentId = materialEditor_.SelectedCommentId();
    if (!materialEditor_.RenameGraphNode(nodeId, displayName)) {
        return false;
    }
    if (!RecordMaterialGraphWorkingCopyEdit(
            id,
            "Rename Material Graph Node",
            std::move(before),
            beforeSelectedNodeId,
            std::move(beforeSelectedNodeIds),
            beforeSelectedCommentId)) {
        console_.Warning("Materials", "Material graph node rename could not be recorded.");
        return false;
    }
    console_.Info("Materials", "Renamed material graph node #" + std::to_string(nodeId) + ".");
    return true;
}

bool EditorSceneContext::BeginMaterialGraphNodeRenameEdit(kb::assets::AssetId id, std::uint32_t nodeId) {
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value() || nodeId == 0U) {
        return false;
    }
    return materialEditor_.BeginGraphNodeRenameEdit(nodeId);
}

bool EditorSceneContext::IsMaterialGraphNodeRenameEditing() const noexcept {
    return materialEditor_.IsGraphNodeRenameEditing();
}

void EditorSceneContext::AppendMaterialGraphNodeRenameEditText(wchar_t character) {
    materialEditor_.AppendGraphNodeRenameEditText(character);
}

void EditorSceneContext::InsertMaterialGraphNodeRenameEditText(std::string_view text) {
    materialEditor_.InsertGraphNodeRenameEditText(text);
}

void EditorSceneContext::BackspaceMaterialGraphNodeRenameEdit() {
    materialEditor_.BackspaceGraphNodeRenameEdit();
}

void EditorSceneContext::ClearMaterialGraphNodeRenameEditText() {
    materialEditor_.ClearGraphNodeRenameEditText();
}

void EditorSceneContext::SelectAllMaterialGraphNodeRenameEditText() noexcept {
    materialEditor_.SelectAllGraphNodeRenameEditText();
}

bool EditorSceneContext::CommitMaterialGraphNodeRenameEdit() {
    const kb::assets::AssetId id = materialEditor_.OpenAssetId();
    const std::uint32_t nodeId = materialEditor_.GraphNodeRenameEditNodeId();
    const std::string value{ materialEditor_.GraphNodeRenameEditBuffer() };
    if (!id.IsValid() || nodeId == 0U) {
        materialEditor_.CancelGraphNodeRenameEdit();
        return false;
    }
    const std::string beforeName = materialEditor_.GraphNodeDisplayName(nodeId);
    const bool committed = SetMaterialGraphNodeDisplayName(id, nodeId, value);
    materialEditor_.CancelGraphNodeRenameEdit();
    return committed || !beforeName.empty();
}

void EditorSceneContext::CancelMaterialGraphNodeRenameEdit() noexcept {
    materialEditor_.CancelGraphNodeRenameEdit();
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
    if (!materialEditor_.IsGraphConstantInlineEditDirty()) {
        // Arming the edit prefills the buffer with the node's current value, so a click that types nothing
        // must not rewrite the document (SetGraphConstantValue re-formats hints and parameter flags).
        materialEditor_.CancelGraphConstantInlineEdit();
        return true;
    }
    const bool committed = SetMaterialGraphConstantValue(id, nodeId, value);
    // Always clear, exactly like the rename commit above: leaving an unparseable buffer armed would keep
    // HasDirtyMaterialAssetEdit() true forever, which blocks Save for the whole editor with no way out.
    materialEditor_.CancelGraphConstantInlineEdit();
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
    {
        std::ostringstream row;
        row << "begin-link material=" << id.value
            << " node=" << nodeId
            << " pin=" << pin
            << " direction=" << (outputPin ? "output" : "input")
            << " cursor=" << x << "," << y;
        LogMaterialGraphDebug(console_, row.str());
    }
    if (materialEditor_.OpenAssetId() != id || nodeId == 0U || pin.empty()) {
        LogMaterialGraphDebug(console_, "begin-link-rejected open asset/node/pin invalid");
        return false;
    }
    if (!materialEditor_.WorkingCopy().has_value()) {
        LogMaterialGraphDebug(console_, "begin-link-rejected missing working copy");
        return false;
    }
    const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(materialEditor_.WorkingCopy()->graph, nodeId);
    if (node == nullptr ||
        (outputPin && !kb::render::IsRenderMaterialGraphOutputPin(*node, pin)) ||
        (!outputPin && !kb::render::IsRenderMaterialGraphInputPin(*node, pin))) {
        LogMaterialGraphDebug(console_, "begin-link-rejected pin does not exist or wrong direction");
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
    {
        std::ostringstream row;
        row << "complete-link-request material=" << id.value
            << " pendingNode=" << materialGraphPendingConnectionNodeId_
            << " pendingPin=" << materialGraphPendingConnectionPin_
            << " pendingDirection=" << (materialGraphPendingConnectionOutput_ ? "output" : "input")
            << " targetNode=" << nodeId
            << " targetPin=" << pin
            << " targetIsInput=" << (inputPin ? "true" : "false");
        LogMaterialGraphDebug(console_, row.str());
    }
    if (materialGraphPendingConnectionAssetId_ != id || materialGraphPendingConnectionNodeId_ == 0U || nodeId == 0U || pin.empty()) {
        LogMaterialGraphDebug(console_, "complete-link-rejected pending state mismatch");
        return false;
    }
    if (pin.empty() || inputPin != materialGraphPendingConnectionOutput_) {
        const std::string direction = materialGraphPendingConnectionOutput_ ? "output-to-output" : "input-to-input";
        const std::string diagnostic = "Material graph pins are not compatible: " + direction + " connections are not allowed.";
        materialEditor_.SetDiagnostics({ "Error graph.link_direction_mismatch: " + diagnostic }, true);
        console_.Warning("Materials", diagnostic);
        LogMaterialGraphDebug(console_, "complete-link-rejected " + diagnostic);
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
        LogMaterialGraphDebug(console_, "complete-link-rejected missing working copy after clear");
        return false;
    }
    {
        const kb::render::RenderMaterialGraphNode* fromNode = kb::render::FindRenderMaterialGraphNode(materialEditor_.WorkingCopy()->graph, fromNodeId);
        const kb::render::RenderMaterialGraphNode* toNode = kb::render::FindRenderMaterialGraphNode(materialEditor_.WorkingCopy()->graph, toNodeId);
        const kb::render::RenderMaterialGraphPinType fromType = fromNode != nullptr
            ? kb::render::RenderMaterialGraphPinDataType(*fromNode, fromPin, true)
            : kb::render::RenderMaterialGraphPinType::Unknown;
        const kb::render::RenderMaterialGraphPinType toType = toNode != nullptr
            ? kb::render::RenderMaterialGraphPinDataType(*toNode, toPin, false)
            : kb::render::RenderMaterialGraphPinType::Unknown;
        std::ostringstream row;
        row << "complete-link-attempt "
            << fromNodeId << "." << fromPin << "(" << kb::render::RenderMaterialGraphPinTypeName(fromType) << ") -> "
            << toNodeId << "." << toPin << "(" << kb::render::RenderMaterialGraphPinTypeName(toType) << ")";
        LogMaterialGraphDebug(console_, row.str());
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    if (!materialEditor_.ConnectGraphPins(fromNodeId, fromPin, toNodeId, toPin)) {
        const std::string diagnostic = MaterialGraphPinConnectionDiagnostic(*materialEditor_.WorkingCopy(), fromNodeId, fromPin, toNodeId, toPin);
        materialEditor_.SetDiagnostics({ "Error graph.link_type_mismatch: " + diagnostic }, true);
        console_.Warning("Materials", diagnostic);
        LogMaterialGraphDebug(console_, "complete-link-failed " + diagnostic);
        LogMaterialGraphDebugDocument(console_, "complete-link-failed-graph", *materialEditor_.WorkingCopy());
        if (ownsTransaction) {
            CancelMaterialGraphWorkingCopyTransaction();
        }
        return false;
    }
    if (materialEditor_.WorkingCopy().has_value()) {
        LogMaterialGraphDebugDocument(console_, "complete-link-after-connect", *materialEditor_.WorkingCopy());
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Connect Material Graph Pins", std::move(before), beforeSelectedNodeId)) {
        console_.Warning("Materials", "Material graph connection could not be recorded.");
        LogMaterialGraphDebug(console_, "complete-link-record-failed");
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
    LogMaterialGraphDebug(console_, "disconnect-input-request material=" + std::to_string(id.value) + " node=" + std::to_string(toNodeId) + " pin=" + std::string{ toPin });
    if (materialEditor_.OpenAssetId() != id) {
        return false;
    }
    if (!materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    if (!materialEditor_.DisconnectGraphInputPin(toNodeId, toPin)) {
        LogMaterialGraphDebug(console_, "disconnect-input-failed");
        return false;
    }
    if (materialEditor_.WorkingCopy().has_value()) {
        LogMaterialGraphDebugDocument(console_, "disconnect-input-after", *materialEditor_.WorkingCopy());
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Disconnect Material Graph Input", std::move(before), beforeSelectedNodeId)) {
        console_.Warning("Materials", "Material graph input disconnect could not be recorded.");
        return false;
    }
    return true;
}

bool EditorSceneContext::DisconnectMaterialGraphOutputPin(kb::assets::AssetId id, std::uint32_t fromNodeId, std::string_view fromPin) {
    LogMaterialGraphDebug(console_, "disconnect-output-request material=" + std::to_string(id.value) + " node=" + std::to_string(fromNodeId) + " pin=" + std::string{ fromPin });
    if (materialEditor_.OpenAssetId() != id) {
        return false;
    }
    if (!materialEditor_.WorkingCopy().has_value()) {
        return false;
    }
    kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
    const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
    if (!materialEditor_.DisconnectGraphOutputPin(fromNodeId, fromPin)) {
        LogMaterialGraphDebug(console_, "disconnect-output-failed");
        return false;
    }
    if (materialEditor_.WorkingCopy().has_value()) {
        LogMaterialGraphDebugDocument(console_, "disconnect-output-after", *materialEditor_.WorkingCopy());
    }
    if (!RecordMaterialGraphWorkingCopyEdit(id, "Disconnect Material Graph Output", std::move(before), beforeSelectedNodeId)) {
        console_.Warning("Materials", "Material graph output disconnect could not be recorded.");
        return false;
    }
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
        CancelMaterialGraphWorkingCopyTransaction();
        return false;
    }
    materialGraphPendingConnectionOwnsTransaction_ = true;
    return true;
}

bool EditorSceneContext::CancelMaterialGraphPinConnection() {
    const bool ownsTransaction = materialGraphPendingConnectionOwnsTransaction_;
    ClearMaterialGraphPinConnectionState();
    if (ownsTransaction) {
        CancelMaterialGraphWorkingCopyTransaction();
    }
    return true;
}

bool EditorSceneContext::IsMaterialGraphPinConnectionDetach() const noexcept {
    return materialGraphPendingConnectionOwnsTransaction_;
}

bool EditorSceneContext::AbandonMaterialGraphPinConnection() {
    // Dropping a wire that was pulled off an input pin into empty space means "leave it unplugged": the
    // detach is kept and lands in undo as its own edit. Only an explicit cancel (Escape) rolls it back,
    // otherwise there would be no gesture that ends with a link actually removed.
    const bool ownsTransaction = materialGraphPendingConnectionOwnsTransaction_;
    ClearMaterialGraphPinConnectionState();
    if (ownsTransaction) {
        return CommitMaterialGraphWorkingCopyTransaction();
    }
    return true;
}

bool EditorSceneContext::CancelMaterialGraphInteractions() {
    bool changed = false;
    changed = CancelMaterialGraphCommentDrag() || changed;
    if (materialGraphNodeDragging_) {
        materialGraphNodeDragging_ = false;
        materialGraphDragAssetId_ = {};
        materialGraphDragNodeId_ = 0U;
        materialGraphDragStartX_ = 0;
        materialGraphDragStartY_ = 0;
        materialGraphDragStartOffsetX_ = 0;
        materialGraphDragStartOffsetY_ = 0;
        materialGraphDragStartNodeX_ = 0;
        materialGraphDragStartNodeY_ = 0;
        materialGraphDragStartDocument_.reset();
        materialGraphDragStartSelectedNodeId_ = 0U;
        materialGraphDragStartSelectedNodeIds_.clear();
        materialGraphDragStartNodes_.clear();
        materialGraphDragChanged_ = false;
        changed = true;
    }
    if (materialGraphBoxSelecting_) {
        materialGraphBoxSelectionAssetId_ = {};
        materialGraphBoxSelecting_ = false;
        materialGraphBoxSelectionOperation_ = MaterialGraphSelectionOperation::Replace;
        materialGraphBoxSelectionBaseNodeIds_.clear();
        materialGraphBoxSelectionBasePrimaryNodeId_ = 0U;
        materialGraphBoxSelectionMoved_ = false;
        changed = true;
    }
    if (materialGraphPanning_) {
        materialGraphPanX_ = materialGraphPanStartOffsetX_;
        materialGraphPanY_ = materialGraphPanStartOffsetY_;
        materialGraphPanning_ = false;
        materialGraphPanMoved_ = false;
        changed = true;
    }
    if (HasMaterialGraphPinConnection()) {
        // A pin connection parked behind an OPEN palette menu is intentional: a wire was dropped on empty
        // canvas and is waiting for the user to pick a node to auto-connect. Do NOT tear it down here.
        // CancelMaterialGraphInteractions fires from WM_CAPTURECHANGED / WM_CANCELMODE, and the wire-drop
        // path legitimately calls ReleaseCapture() right after opening the menu - which synchronously sends
        // WM_CAPTURECHANGED and used to cancel the parked connection before the pick could use it (pendingNode
        // reset to 0 -> "pick a node and nothing appears"). Only cancel when there is no menu holding it.
        if (!IsMaterialGraphContextMenuOpen()) {
            static_cast<void>(CancelMaterialGraphPinConnection());
            changed = true;
        }
    } else if (HasMaterialGraphWorkingCopyTransaction()) {
        CancelMaterialGraphWorkingCopyTransaction();
        changed = true;
    }
    return changed;
}

void EditorSceneContext::ResetMaterialGraphTransientState() {
    if (const kb::assets::AssetId openAsset = materialEditor_.OpenAssetId(); openAsset.IsValid()) {
        materialGraphViewStates_.insert_or_assign(openAsset.value, MaterialGraphViewState{
            .zoom = materialGraphZoom_,
            .panX = materialGraphPanX_,
            .panY = materialGraphPanY_,
        });
    }
    // Close the menu BEFORE cancelling interactions: CancelMaterialGraphInteractions now deliberately leaves
    // a pin connection parked behind an open menu alone, so the menu must be gone first for the reset to also
    // tear down any pending connection.
    static_cast<void>(CloseMaterialGraphContextMenu());
    static_cast<void>(CancelMaterialGraphInteractions());
    static_cast<void>(CloseMaterialGraphTexturePicker());
    materialEditor_.CloseGraphNodeEnumDropdown();
    materialEditor_.CancelGraphConstantInlineEdit();
    materialGraphFocused_ = false;
    materialGraphZoom_ = MaterialGraphInteractionPolicy::DefaultZoom;
    materialGraphPanX_ = 0;
    materialGraphPanY_ = 0;
    materialGraphPanMoved_ = false;
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
    materialGraphContextMenuScrollOffset_ = 0;
    // Favorites lives at the top of the palette; open it expanded when it has entries so the user's picks
    // are visible immediately, otherwise start fully collapsed.
    materialGraphContextMenuExpandedMask_ = materialGraphPaletteFavorites_.empty()
        ? 0U
        : (1U << MaterialEditorGraphContextMenuFavoritesCategoryIndex());
    materialGraphContextMenuHoveredCategory_ = static_cast<std::size_t>(-1);
    materialGraphContextMenuHoveredCommand_ = MaterialEditorGraphMenuCommand::None;
    materialGraphContextMenuSearchQuery_.clear();
    materialGraphContextMenuPinFilterActive_ = false;
    materialGraphContextMenuPinFilterNodeId_ = 0U;
    materialGraphContextMenuPinFilterPin_.clear();
    materialGraphContextMenuPinFilterOutput_ = true;
    // Anchor at the raw cursor Y; GraphContextMenuRect fits the height downward from here each frame so
    // the palette opens at the click point rather than being pulled up to the canvas top when it is tall.
    const int menuLeftMax = materialGraphCanvasLeft_ + std::max(0, materialGraphCanvasWidth_ - kMaterialEditorGraphMenuWidth);
    const int menuTopMax = materialGraphCanvasTop_ + std::max(0, materialGraphCanvasHeight_ - kMaterialEditorGraphMenuMinHeight);
    materialGraphContextMenuX_ = std::clamp(x, materialGraphCanvasLeft_, menuLeftMax);
    materialGraphContextMenuY_ = std::clamp(y, materialGraphCanvasTop_, menuTopMax);
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
    materialGraphContextMenuScrollOffset_ = 0;
    // Favorites lives at the top of the palette; open it expanded when it has entries so the user's picks
    // are visible immediately, otherwise start fully collapsed.
    materialGraphContextMenuExpandedMask_ = materialGraphPaletteFavorites_.empty()
        ? 0U
        : (1U << MaterialEditorGraphContextMenuFavoritesCategoryIndex());
    materialGraphContextMenuHoveredCategory_ = static_cast<std::size_t>(-1);
    materialGraphContextMenuHoveredCommand_ = MaterialEditorGraphMenuCommand::None;
    materialGraphContextMenuSearchQuery_.clear();
    materialGraphContextMenuPinFilterNodeId_ = materialGraphPendingConnectionNodeId_;
    materialGraphContextMenuPinFilterPin_ = materialGraphPendingConnectionPin_;
    materialGraphContextMenuPinFilterOutput_ = materialGraphPendingConnectionOutput_;
    materialGraphContextMenuPinFilterActive_ = true;
    // Keep the raw drop Y as the anchor (only shifted up enough to keep a minimal strip on screen).
    // GraphContextMenuRect fits the palette height downward from here each frame, so a tall filtered
    // wire-drop palette opens AT the drop point instead of snapping to the top of the canvas.
    const int menuLeftMax = materialGraphCanvasLeft_ + std::max(0, materialGraphCanvasWidth_ - kMaterialEditorGraphMenuWidth);
    const int menuTopMax = materialGraphCanvasTop_ + std::max(0, materialGraphCanvasHeight_ - kMaterialEditorGraphMenuMinHeight);
    materialGraphContextMenuX_ = std::clamp(x, materialGraphCanvasLeft_, menuLeftMax);
    materialGraphContextMenuY_ = std::clamp(y, materialGraphCanvasTop_, menuTopMax);
    return true;
}

bool EditorSceneContext::CloseMaterialGraphContextMenu() noexcept {
    if (!materialGraphContextMenuAssetId_.IsValid()) {
        return false;
    }
    materialGraphContextMenuAssetId_ = {};
    materialGraphContextMenuScrollOffset_ = 0;
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

int EditorSceneContext::MaterialGraphContextMenuScrollOffset() const noexcept {
    return materialGraphContextMenuScrollOffset_;
}

bool EditorSceneContext::SetMaterialGraphContextMenuScrollOffset(int offset, int maxOffset) noexcept {
    const int clamped = std::clamp(offset, 0, std::max(0, maxOffset));
    if (materialGraphContextMenuScrollOffset_ == clamped) {
        return false;
    }
    materialGraphContextMenuScrollOffset_ = clamped;
    return true;
}

bool EditorSceneContext::ScrollMaterialGraphContextMenu(int wheelDelta, int maxOffset) noexcept {
    if (!IsMaterialGraphContextMenuOpen() || maxOffset <= 0) {
        return false;
    }
    const int direction = wheelDelta > 0 ? -1 : 1;
    return SetMaterialGraphContextMenuScrollOffset(
        materialGraphContextMenuScrollOffset_ + direction * kMaterialEditorGraphMenuCommandHeight * 3,
        maxOffset);
}

bool EditorSceneContext::MoveMaterialGraphContextMenuKeyboardSelection(int direction) {
    if (!IsMaterialGraphContextMenuOpen() || direction == 0) {
        return false;
    }
    const std::vector<MaterialGraphContextMenuKeyboardRow> rows = MaterialGraphContextMenuKeyboardRows(*this);
    if (rows.empty()) {
        return ClearMaterialGraphContextMenuHover();
    }

    std::ptrdiff_t currentIndex = -1;
    for (std::size_t index = 0U; index < rows.size(); ++index) {
        if (IsMaterialGraphContextMenuRowHovered(*this, rows[index])) {
            currentIndex = static_cast<std::ptrdiff_t>(index);
            break;
        }
    }
    const std::size_t nextIndex = currentIndex < 0
        ? (direction > 0 ? 0U : rows.size() - 1U)
        : static_cast<std::size_t>((currentIndex + direction + static_cast<std::ptrdiff_t>(rows.size())) %
              static_cast<std::ptrdiff_t>(rows.size()));
    const MaterialGraphContextMenuKeyboardRow& selected = rows[nextIndex];
    bool changed = SetMaterialGraphContextMenuHover(selected.categoryIndex, selected.command);

    const RECT menu = MaterialEditorPanelRenderer::GraphContextMenuRect(*this);
    const RECT viewport = MaterialEditorGraphContextMenuViewportRect(menu);
    const int viewportHeight = std::max(0L, viewport.bottom - viewport.top);
    const int maxScroll = MaterialEditorGraphContextMenuMaxScroll(*this);
    int scrollOffset = materialGraphContextMenuScrollOffset_;
    if (selected.contentTop < scrollOffset) {
        scrollOffset = selected.contentTop;
    } else if (selected.contentTop + selected.height > scrollOffset + viewportHeight) {
        scrollOffset = selected.contentTop + selected.height - viewportHeight;
    }
    changed = SetMaterialGraphContextMenuScrollOffset(scrollOffset, maxScroll) || changed;
    return changed;
}

bool EditorSceneContext::ActivateMaterialGraphContextMenuKeyboardSelection() {
    if (!IsMaterialGraphContextMenuOpen()) {
        return false;
    }
    const std::vector<MaterialGraphContextMenuKeyboardRow> rows = MaterialGraphContextMenuKeyboardRows(*this);
    if (rows.empty()) {
        return false;
    }

    const MaterialGraphContextMenuKeyboardRow* selected = nullptr;
    for (const MaterialGraphContextMenuKeyboardRow& row : rows) {
        if (IsMaterialGraphContextMenuRowHovered(*this, row)) {
            selected = &row;
            break;
        }
    }
    if (selected == nullptr) {
        selected = &rows.front();
        static_cast<void>(SetMaterialGraphContextMenuHover(selected->categoryIndex, selected->command));
    }
    if (selected->category) {
        return ToggleMaterialGraphContextMenuCategory(selected->categoryIndex);
    }
    return ExecuteMaterialGraphContextMenuCommand(selected->command);
}

bool EditorSceneContext::OpenMaterialGraphTexturePicker(
    kb::assets::AssetId id,
    std::uint32_t nodeId,
    kb::assets::AssetId currentTexture) noexcept {
    if (materialEditor_.OpenAssetId() != id || !id.IsValid() || nodeId == 0U) {
        return false;
    }
    static_cast<void>(CloseMaterialGraphContextMenu());
    static_cast<void>(CancelMaterialGraphPinConnection());
    materialEditor_.CloseGraphNodeEnumDropdown();
    materialGraphTexturePickerAssetId_ = id;
    materialGraphTexturePickerNodeId_ = nodeId;
    materialGraphTexturePickerSelectedTextureId_ = currentTexture;
    materialGraphTexturePickerSearchQuery_.clear();
    materialGraphTexturePickerScrollOffset_ = 0;
    return true;
}

bool EditorSceneContext::CloseMaterialGraphTexturePicker() noexcept {
    if (!materialGraphTexturePickerAssetId_.IsValid()) {
        return false;
    }
    materialGraphTexturePickerAssetId_ = {};
    materialGraphTexturePickerNodeId_ = 0U;
    materialGraphTexturePickerSelectedTextureId_ = {};
    materialGraphTexturePickerSearchQuery_.clear();
    materialGraphTexturePickerScrollOffset_ = 0;
    return true;
}

bool EditorSceneContext::IsMaterialGraphTexturePickerOpen() const noexcept {
    return materialGraphTexturePickerAssetId_.IsValid();
}

kb::assets::AssetId EditorSceneContext::MaterialGraphTexturePickerAssetId() const noexcept {
    return materialGraphTexturePickerAssetId_;
}

std::uint32_t EditorSceneContext::MaterialGraphTexturePickerNodeId() const noexcept {
    return materialGraphTexturePickerNodeId_;
}

kb::assets::AssetId EditorSceneContext::MaterialGraphTexturePickerSelectedAssetId() const noexcept {
    return materialGraphTexturePickerSelectedTextureId_;
}

bool EditorSceneContext::SetMaterialGraphTexturePickerSelected(kb::assets::AssetId textureId) noexcept {
    if (!IsMaterialGraphTexturePickerOpen()) {
        return false;
    }
    if (materialGraphTexturePickerSelectedTextureId_ == textureId) {
        return false;
    }
    materialGraphTexturePickerSelectedTextureId_ = textureId;
    return true;
}

std::string_view EditorSceneContext::MaterialGraphTexturePickerSearchQuery() const noexcept {
    return materialGraphTexturePickerSearchQuery_;
}

void EditorSceneContext::SetMaterialGraphTexturePickerSearchQuery(std::string query) {
    if (query.size() > 96U) {
        query.resize(96U);
    }
    materialGraphTexturePickerSearchQuery_ = std::move(query);
    materialGraphTexturePickerScrollOffset_ = 0;
}

void EditorSceneContext::AppendMaterialGraphTexturePickerSearchText(wchar_t character) {
    if (character < 32 || character == 127 || character > 126 || materialGraphTexturePickerSearchQuery_.size() >= 96U) {
        return;
    }
    materialGraphTexturePickerSearchQuery_.push_back(static_cast<char>(character));
    materialGraphTexturePickerScrollOffset_ = 0;
}

void EditorSceneContext::BackspaceMaterialGraphTexturePickerSearch() {
    if (materialGraphTexturePickerSearchQuery_.empty()) {
        return;
    }
    materialGraphTexturePickerSearchQuery_.pop_back();
    materialGraphTexturePickerScrollOffset_ = 0;
}

void EditorSceneContext::ClearMaterialGraphTexturePickerSearch() noexcept {
    materialGraphTexturePickerSearchQuery_.clear();
    materialGraphTexturePickerScrollOffset_ = 0;
}

int EditorSceneContext::MaterialGraphTexturePickerScrollOffset() const noexcept {
    return materialGraphTexturePickerScrollOffset_;
}

bool EditorSceneContext::SetMaterialGraphTexturePickerScrollOffset(int offset, int maxOffset) noexcept {
    const int clamped = std::clamp(offset, 0, std::max(0, maxOffset));
    if (materialGraphTexturePickerScrollOffset_ == clamped) {
        return false;
    }
    materialGraphTexturePickerScrollOffset_ = clamped;
    return true;
}

bool EditorSceneContext::ScrollMaterialGraphTexturePicker(int wheelDelta, int maxOffset) noexcept {
    if (!IsMaterialGraphTexturePickerOpen() || maxOffset <= 0) {
        return false;
    }
    const int direction = wheelDelta > 0 ? -1 : 1;
    return SetMaterialGraphTexturePickerScrollOffset(
        materialGraphTexturePickerScrollOffset_ + direction * 72,
        maxOffset);
}

std::string_view EditorSceneContext::MaterialGraphContextMenuSearchQuery() const noexcept {
    return materialGraphContextMenuSearchQuery_;
}

void EditorSceneContext::SetMaterialGraphContextMenuSearchQuery(std::string query) {
    if (query.size() > 64U) {
        query.resize(64U);
    }
    materialGraphContextMenuSearchQuery_ = std::move(query);
    materialGraphContextMenuScrollOffset_ = 0;
}

void EditorSceneContext::AppendMaterialGraphContextMenuSearchText(wchar_t character) {
    if (character < 32 || character > 126 || materialGraphContextMenuSearchQuery_.size() >= 64U) {
        return;
    }
    materialGraphContextMenuSearchQuery_.push_back(static_cast<char>(character));
    materialGraphContextMenuExpandedMask_ = 0U;
    materialGraphContextMenuScrollOffset_ = 0;
}

void EditorSceneContext::BackspaceMaterialGraphContextMenuSearch() {
    if (!materialGraphContextMenuSearchQuery_.empty()) {
        materialGraphContextMenuSearchQuery_.pop_back();
        materialGraphContextMenuScrollOffset_ = 0;
    }
}

void EditorSceneContext::ClearMaterialGraphContextMenuSearch() noexcept {
    materialGraphContextMenuSearchQuery_.clear();
    materialGraphContextMenuScrollOffset_ = 0;
}

const std::vector<MaterialEditorGraphMenuCommand>& EditorSceneContext::MaterialGraphPaletteFavoriteCommands() const noexcept {
    return materialGraphPaletteFavorites_;
}

bool EditorSceneContext::IsMaterialGraphPaletteFavorite(MaterialEditorGraphMenuCommand command) const noexcept {
    return std::find(materialGraphPaletteFavorites_.begin(), materialGraphPaletteFavorites_.end(), command) != materialGraphPaletteFavorites_.end();
}

bool EditorSceneContext::ToggleMaterialGraphPaletteFavorite(MaterialEditorGraphMenuCommand command) {
    if (command == MaterialEditorGraphMenuCommand::None || MaterialEditorGraphMenuCommandIsAction(command)) {
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
    materialGraphContextMenuScrollOffset_ = 0;
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
    if (const std::optional<kb::render::RenderMaterialGraphNodeKind> kind = MaterialEditorGraphMenuCommandNodeKind(command)) {
        return AddMaterialGraphNode(id, *kind, graphX, graphY);
    }
    switch (command) {
    case MaterialEditorGraphMenuCommand::CreateTextureSample:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::TextureSample, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateTextureParameter:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ParameterTexture, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateTextureObject:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::TextureObject, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateTextureSampleCube:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::TextureSampleCube, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateTextureObjectCube:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::TextureObjectCube, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateTextureSampleVolume:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateTextureObjectVolume:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::TextureObjectVolume, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateTextureSample2DArray:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateTextureObject2DArray:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::TextureObject2DArray, graphX, graphY);
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
    case MaterialEditorGraphMenuCommand::CreateSobol:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Sobol, graphX, graphY);
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
    case MaterialEditorGraphMenuCommand::CreateSwitch:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::RuntimeSwitch, graphX, graphY);
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
    case MaterialEditorGraphMenuCommand::CreateArcSineFast:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ArcSineFast, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateArcCosineFast:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ArcCosineFast, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateArcTangentFast:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ArcTangentFast, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateArcTangent2Fast:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ArcTangent2Fast, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateClamp:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Clamp, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateLerp:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Lerp, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateNormalUnpack:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::NormalUnpack, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateTime:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::Time, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateDeltaTime:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::DeltaTime, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateDynamicParameter:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::DynamicParameter, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateVertexColor:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::VertexColor, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateScreenPosition:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ScreenPosition, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreatePixelPosition:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::PixelPosition, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateLocalPosition:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::LocalPosition, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateObjectPosition:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ObjectPosition, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateWorldPosition:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::WorldPosition, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreatePerInstanceRandom:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::PerInstanceRandom, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreatePerInstanceFadeAmount:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::PerInstanceFadeAmount, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreatePerInstanceCustomData:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::PerInstanceCustomData, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateObjectRadius:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ObjectRadius, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateObjectBounds:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ObjectBounds, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateObjectOrientation:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ObjectOrientation, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreatePreSkinnedPosition:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::PreSkinnedPosition, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreatePreSkinnedNormal:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::PreSkinnedNormal, graphX, graphY);
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
    case MaterialEditorGraphMenuCommand::CreateTwoSidedSign:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::TwoSidedSign, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateSceneColor:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::SceneColor, graphX, graphY);
    case MaterialEditorGraphMenuCommand::CreateSceneTexture:
        return AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::SceneTexture, graphX, graphY);
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
    case MaterialEditorGraphMenuCommand::FrameSelected:
        return FrameSelectedMaterialGraphNodes();
    case MaterialEditorGraphMenuCommand::SelectUpstream:
        return SelectMaterialGraphUpstream();
    case MaterialEditorGraphMenuCommand::SelectDownstream:
        return SelectMaterialGraphDownstream();
    case MaterialEditorGraphMenuCommand::AlignLeft:
        return AlignSelectedMaterialGraphNodes(id, MaterialEditorGraphAlignMode::Left);
    case MaterialEditorGraphMenuCommand::AlignCenter:
        return AlignSelectedMaterialGraphNodes(id, MaterialEditorGraphAlignMode::Center);
    case MaterialEditorGraphMenuCommand::AlignRight:
        return AlignSelectedMaterialGraphNodes(id, MaterialEditorGraphAlignMode::Right);
    case MaterialEditorGraphMenuCommand::AlignTop:
        return AlignSelectedMaterialGraphNodes(id, MaterialEditorGraphAlignMode::Top);
    case MaterialEditorGraphMenuCommand::AlignMiddle:
        return AlignSelectedMaterialGraphNodes(id, MaterialEditorGraphAlignMode::Middle);
    case MaterialEditorGraphMenuCommand::AlignBottom:
        return AlignSelectedMaterialGraphNodes(id, MaterialEditorGraphAlignMode::Bottom);
    case MaterialEditorGraphMenuCommand::DistributeHorizontal:
        return DistributeSelectedMaterialGraphNodes(id, MaterialEditorGraphDistributeAxis::Horizontal);
    case MaterialEditorGraphMenuCommand::DistributeVertical:
        return DistributeSelectedMaterialGraphNodes(id, MaterialEditorGraphDistributeAxis::Vertical);
    case MaterialEditorGraphMenuCommand::PromoteToParameter:
        return PromoteSelectedMaterialGraphNodeToParameter(id);
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
    if (channel < 0 || channel >= 4 || !std::isfinite(value)) {
        return false;
    }
    return ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialBaseColorChannelEdit>(channel, value));
}

bool EditorSceneContext::SetMaterialEmissiveColor(kb::assets::AssetId id, int channel, float value) {
    if (channel < 0 || channel >= 3 || !std::isfinite(value)) {
        return false;
    }
    return ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialEmissiveColorChannelEdit>(channel, value));
}

bool EditorSceneContext::SetMaterialMetallicFactor(kb::assets::AssetId id, float value) {
    return std::isfinite(value) && ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialMetallicFactorEdit>(value));
}

bool EditorSceneContext::SetMaterialRoughnessFactor(kb::assets::AssetId id, float value) {
    return std::isfinite(value) && ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialRoughnessFactorEdit>(value));
}

bool EditorSceneContext::SetMaterialNormalScale(kb::assets::AssetId id, float value) {
    return std::isfinite(value) && ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialNormalScaleEdit>(value));
}

bool EditorSceneContext::SetMaterialOcclusionStrength(kb::assets::AssetId id, float value) {
    return std::isfinite(value) && ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialOcclusionStrengthEdit>(value));
}

bool EditorSceneContext::SetMaterialEmissiveStrength(kb::assets::AssetId id, float value) {
    return std::isfinite(value) && ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialEmissiveStrengthEdit>(value));
}

bool EditorSceneContext::SetMaterialAlphaCutoff(kb::assets::AssetId id, float value) {
    return std::isfinite(value) && ExecuteMaterialAssetEdit(id, std::make_unique<EditorMaterialAlphaCutoffEdit>(value));
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

    const kb::assets::AssetMetadata* materialMetadata = scene_->Assets().Manager().Registry().Find(id);
    if (slot == EditorMaterialTextureSlot::Normal &&
        materialEditor_.OpenAssetId() == id &&
        materialEditor_.WorkingCopy().has_value() &&
        materialMetadata != nullptr &&
        materialMetadata->type == "RenderMaterial") {
        kb::render::RenderMaterialAssetData before = *materialEditor_.WorkingCopy();
        kb::render::RenderMaterialAssetData working = before;
        if (!ApplyEditorMaterialOutputNormalTextureGraph(working, textureId)) {
            console_.Error("Materials", "Material graph normal map could not be connected.");
            return false;
        }

        const std::uint32_t beforeSelectedNodeId = materialEditor_.SelectedNodeId();
        std::vector<std::uint32_t> beforeSelectedNodeIds = materialEditor_.SelectedNodeIds();
        const std::uint32_t beforeSelectedCommentId = materialEditor_.SelectedCommentId();
        materialEditor_.SetWorkingCopy(std::move(working));
        if (!RecordMaterialGraphWorkingCopyEdit(
                id,
                "Set Material Normal Map",
                std::move(before),
                beforeSelectedNodeId,
                std::move(beforeSelectedNodeIds),
                beforeSelectedCommentId)) {
            console_.Warning("Materials", "Material graph normal map change could not be recorded.");
            return false;
        }
        console_.Info("Materials", textureId.IsValid() ? "Normal map connected to the material graph." : "Normal map cleared from the material graph.");
        return true;
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
    // The gesture is settled BEFORE the text edits are committed, not after: a drag holds the "before"
    // snapshot its undo command will record, captured when the drag began. Committing a rename first would
    // leave that snapshot describing a document one edit out of date, so the two undo entries would no longer
    // describe consecutive states and one Undo would roll back both. Scoped to the asset being saved, like
    // the commits below - saving a different material is no reason to end a gesture on this one.
    if (materialEditor_.OpenAssetId() == id) {
        static_cast<void>(SettleMaterialGraphGesture());
    }
    if (materialEditor_.OpenAssetId() == id && materialEditor_.IsGraphNodeRenameEditing()) {
        static_cast<void>(CommitMaterialGraphNodeRenameEdit());
    }
    if (materialEditor_.OpenAssetId() == id && materialEditor_.IsGraphConstantInlineEditing()) {
        static_cast<void>(CommitMaterialGraphConstantInlineEdit());
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
        // For an open RenderMaterial the edit only reached the in-memory working copy
        // (ExecuteMaterialAssetEdit -> ApplyPatchToMaterialEditorWorkingCopy), so returning here would
        // report a successful Save that never touched the file. Fall through to the working-copy save
        // below instead. Every other route (closed asset, instance, graph source) already wrote to disk
        // through the command path and leaves the editor clean, so it returns right here.
        if (materialEditor_.OpenAssetId() != id || !materialEditor_.Dirty()) {
            return true;
        }
    }
    if (HasActiveMaterialAssetEdit()) {
        if (activeMaterialEditAsset_ != id) {
            console_.Error("Materials", "Cannot save material while another material edit is active.");
            return false;
        }
        return CommitActiveMaterialAssetEdit();
    }
    if (materialEditor_.OpenAssetId() == id && materialEditor_.Dirty()) {
        const bool rawGraphWorkingCopy =
            (materialRuntimePreviewAssetId_ == id && materialRuntimePreviewSourceMetadata_.has_value() &&
                materialRuntimePreviewSourceMetadata_->type == kb::render::kRenderMaterialGraphAssetType) ||
            (scene_->Assets().Manager().Registry().Find(id) != nullptr &&
                scene_->Assets().Manager().Registry().Find(id)->type == kb::render::kRenderMaterialGraphAssetType);
        if (rawGraphWorkingCopy) {
            ClearMaterialEditorWorkingCopyRuntimePreview();
        }
        const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
        if (metadata != nullptr && metadata->type == kb::render::kRenderMaterialGraphAssetType && materialEditor_.WorkingCopy().has_value()) {
            kb::render::RenderMaterialAssetData after = *materialEditor_.WorkingCopy();
            SanitizeMaterialGraphTextureMetadata(after);
            if (!ValidateMaterialEditorAssetCandidate(id, &after, nullptr)) {
                return false;
            }
            const std::filesystem::path graphPath = ResolveAssetPath(scene_->Assets().Manager(), *metadata);
            if (graphPath.empty() || !kb::render::RenderMaterialGraphAssetLoader::SaveGraph(graphPath, after.graph)) {
                console_.Error("Materials", "Material Graph source could not be saved.");
                return false;
            }
            materialEditor_.SetWorkingCopy(std::move(after));
            materialEditor_.MarkSaved();
            const std::string graphVirtualPath = metadata->virtualPath.generic_string();
            static_cast<void>(scene_->Assets().Manager().Unload(id));
            static_cast<void>(scene_->Assets().Discover());
            MarkSceneRenderDirty();
            console_.Info("Materials", "Material Graph saved: " + graphVirtualPath);
            return true;
        }
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
    // Revert means discard: an in-flight node rename or inline constant edit is part of what the user asked
    // to throw away, and leaving it armed would keep the editor reporting unsaved work after a Discard.
    if (materialEditor_.OpenAssetId() == id) {
        materialEditor_.CancelGraphNodeRenameEdit();
        materialEditor_.CancelGraphConstantInlineEdit();
    }
    // A gesture in flight is part of what a Discard throws away - but it has to be closed, not abandoned, or
    // the drag would keep pointing at a document the revert has already replaced.
    if (materialEditor_.OpenAssetId() == id) {
        static_cast<void>(SettleMaterialGraphGesture());
    }
    if (inspector_.IsTextEditDirty() && IsMaterialFloatProperty(inspector_.EditedProperty())) {
        inspector_.EndTextEdit();
        if (materialEditor_.OpenAssetId() == id) {
            materialEditor_.RevertToCleanSnapshot();
        }
        commandStack_.Clear(EditorCommandHistoryKey::MaterialAsset(id.value));
        console_.Info("Materials", "Material value edit reverted.");
        return true;
    }
    if (!HasActiveMaterialAssetEdit()) {
        if (materialEditor_.OpenAssetId() == id && materialEditor_.Dirty()) {
            materialEditor_.RevertToCleanSnapshot();
            commandStack_.Clear(EditorCommandHistoryKey::MaterialAsset(id.value));
            ClearMaterialEditorWorkingCopyRuntimePreview();
            MarkSceneRenderDirty();
            console_.Info("Materials", "Material working copy reverted.");
            return true;
        }
        commandStack_.Clear(EditorCommandHistoryKey::MaterialAsset(id.value));
        console_.Info("Materials", "Material has no pending edit to revert.");
        return true;
    }
    if (activeMaterialEditAsset_ != id) {
        console_.Error("Materials", "Cannot revert material while another material edit is active.");
        return false;
    }
    CancelActiveMaterialAssetEdit();
    commandStack_.Clear(EditorCommandHistoryKey::MaterialAsset(id.value));
    console_.Info("Materials", "Material edit reverted.");
    return true;
}

bool EditorSceneContext::ValidateMaterialEditorAsset(kb::assets::AssetId id) {
    return ValidateMaterialEditorAssetCandidate(id, nullptr, nullptr);
}

bool EditorSceneContext::ValidateMaterialEditorAssetCandidate(
    kb::assets::AssetId id,
    const kb::render::RenderMaterialAssetData* materialCandidate,
    const kb::render::RenderMaterialInstanceAssetData* instanceCandidate) {
    LogMaterialGraphDebug(console_, "validate-material-request asset=" + std::to_string(id.value));
    if (!id.IsValid()) {
        console_.Error("Materials", "No material asset is selected for Validate.");
        LogMaterialGraphDebug(console_, "validate-material-failed invalid asset id");
        return false;
    }

    const kb::assets::AssetManager& manager = scene_->Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr || (metadata->type != "RenderMaterial" && metadata->type != "RenderMaterialInstance" &&
            metadata->type != kb::render::kRenderMaterialGraphAssetType)) {
        console_.Error("Materials", "Selected asset is not a Material document.");
        LogMaterialGraphDebug(console_, "validate-material-failed metadata missing/not material");
        return false;
    }

    if (metadata->type == "RenderMaterialInstance") {
        std::optional<kb::render::RenderMaterialInstanceAssetData> instance =
            instanceCandidate != nullptr
                ? std::optional<kb::render::RenderMaterialInstanceAssetData>{ *instanceCandidate }
                : materialEditor_.OpenAssetId() == id && materialEditor_.InstanceWorkingCopy().has_value()
                ? materialEditor_.InstanceWorkingCopy()
                : ReadMaterialInstanceAsset(id);
        if (!instance.has_value() || !instance->parentMaterialAssetId.IsValid()) {
            console_.Error("Materials", "Material instance could not be read or has no parent material.");
            LogMaterialGraphDebug(console_, "validate-material-instance-failed missing instance/parent");
            return false;
        }
        const std::optional<kb::render::RenderMaterialAssetData> parent = ReadEffectiveMaterialAsset(instance->parentMaterialAssetId);
        if (!parent.has_value()) {
            console_.Error("Materials", "Material instance parent material could not be read.");
            LogMaterialGraphDebug(console_, "validate-material-instance-failed parent unreadable");
            return false;
        }
        const kb::render::RenderMaterialInstanceValidationResult validation =
            kb::render::RenderMaterialInstanceAssetLoader::ValidateAgainstParent(*instance, *parent);
        std::vector<std::string> diagnostics = MaterialInstanceValidationDiagnosticLines(validation);
        for (const std::string& diagnostic : diagnostics) {
            console_.Error("Materials", diagnostic);
            LogMaterialGraphDebug(console_, "validate-material-instance-diagnostic " + diagnostic);
        }
        if (materialEditor_.OpenAssetId() == id) {
            materialEditor_.SetDiagnostics(std::move(diagnostics), !validation.Succeeded());
        }
        if (validation.Succeeded()) {
            console_.Info("Materials", "Material instance validated: " + metadata->virtualPath.generic_string());
            LogMaterialGraphDebug(console_, "validate-material-instance-ok asset=" + std::to_string(id.value));
        }
        return validation.Succeeded();
    }

    if (metadata->type == kb::render::kRenderMaterialGraphAssetType) {
        const std::optional<kb::render::RenderMaterialAssetData> document = materialCandidate != nullptr
            ? std::optional<kb::render::RenderMaterialAssetData>{ *materialCandidate }
            : (materialEditor_.OpenAssetId() == id && materialEditor_.WorkingCopy().has_value()
                ? materialEditor_.WorkingCopy()
                : ReadMaterialDocumentAsset(id));
        if (!document.has_value()) {
            console_.Error("Materials", "Material Graph could not be read for validation.");
            return false;
        }
        const std::vector<kb::render::RenderMaterialGraphDiagnostic> graphDiagnostics =
            kb::render::ValidateRenderMaterialAssetGraphDiagnostics(*document);
        std::vector<std::string> diagnostics;
        bool hasError = false;
        for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : graphDiagnostics) {
            diagnostics.push_back(std::string{ kb::render::RenderMaterialGraphDiagnosticKindName(diagnostic.kind) } + ": " + diagnostic.message);
            hasError = hasError || diagnostic.severity == kb::render::RenderMaterialGraphDiagnosticSeverity::Error;
        }
        if (materialEditor_.OpenAssetId() == id) {
            materialEditor_.SetDiagnostics(std::move(diagnostics), hasError);
        }
        return !hasError;
    }

    kb::render::RenderMaterialAssetParseResult result{};
    if (materialCandidate != nullptr) {
        std::ostringstream serialized;
        kb::render::RenderMaterialAssetWriter::Write(serialized, *materialCandidate);
        std::istringstream input{ serialized.str() };
        result = kb::render::RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input, kb::render::RenderMaterialAssetParseSourceContext{ .assetId = id });
    } else if (materialEditor_.OpenAssetId() == id && materialEditor_.WorkingCopy().has_value()) {
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
            LogMaterialGraphDebug(console_, "validate-material-failed path unresolved");
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
        LogMaterialGraphDebug(console_, "validate-material-parser-diagnostic " + message);
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
        LogMaterialGraphDebug(console_, "validate-material-runtime-diagnostic " + diagnostics[index]);
    }

    if (materialEditor_.OpenAssetId() == id) {
        materialEditor_.SetDiagnostics(std::move(diagnostics), hasError);
    }
    if (result.Succeeded() && !hasError) {
        console_.Info("Materials", "Material validated: " + metadata->virtualPath.generic_string());
        LogMaterialGraphDebug(console_, "validate-material-ok asset=" + std::to_string(id.value));
    } else {
        LogMaterialGraphDebug(console_, "validate-material-failed succeeded=" + std::string{ result.Succeeded() ? "true" : "false" } + " hasError=" + std::string{ hasError ? "true" : "false" });
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

bool EditorSceneContext::SetProjectSceneLightingPath(kb::project::ProjectSceneLightingPath path) {
    if (project_.sceneLightingPath == path) {
        return false;
    }
    project_.sceneLightingPath = path;
    const bool saved = SaveProjectDescriptor();
    if (saved) {
        sceneGraphCookPending_ = true;
        RequestOpenMaterialSceneGraphCook();
        MarkSceneRenderDirty();
    }
    return saved;
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

bool EditorSceneContext::ActivateProjectPhysicsLayers(kb::scene::Scene& scene) {
    if (project_.physicsLayersAsset.empty()) {
        return true;
    }
    if (kb::scene::PhysicsBackend::LoadAndConfigureLayers(scene, project_.physicsLayersAsset)) {
        return true;
    }
    std::string error = "Project physics layers could not be loaded and applied: " + project_.physicsLayersAsset;
    const std::string assetError = scene.Assets().Manager().LastError();
    if (!assetError.empty()) {
        error += " (" + assetError + ")";
    }
    console_.Error("Physics", error);
    return false;
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
        if (!EditorSceneMeshAssetActions::AssignMesh(*scene_, entity, assetId)) {
            return false;
        }
        // Keep an existing Collider fitted to the new geometry (Unity-like: the
        // collision shape follows the mesh when you swap it). Skipped when the
        // mesh is being cleared.
        if (assetId.IsValid() && scene_->Components().Colliders().Has(entity)) {
            std::string reason;
            if (ApplyColliderFitToMesh(entity, reason)) {
                console_.Info("Physics", "Collider refit to new mesh: " + reason + ".");
            } else {
                console_.Warning("Physics", "Collider not refit to new mesh: " + reason + ".");
            }
        }
        return true;
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

    const bool assigned = ExecuteSceneCommand(assetId.IsValid() ? "Assign Mesh Material" : "Clear Mesh Material", [this, entity, assetId]() {
        return EditorSceneMaterialAssetActions::AssignMaterialToAllSlots(*scene_, entity, assetId);
    });
    if (assigned && assetId.IsValid()) {
        // The assigned material may be a graph material that has never been cooked for this
        // scene's shading variant; schedule a scene recook so the mesh shows the authored
        // graph program instead of the builtin-PBR flatten until the next scene reload.
        sceneGraphCookPending_ = true;
    }
    return assigned;
}

bool EditorSceneContext::ApplyMaterialToSelectedMeshRenderers(kb::assets::AssetId assetId) {
    {
        std::ostringstream row;
        row << "apply-material-request asset=" << assetId.value
            << " openAsset=" << materialEditor_.OpenAssetId().value
            << " dirty=" << (materialEditor_.Dirty() ? "true" : "false")
            << " selected=" << SelectedHierarchyEntities().size();
        LogMaterialGraphDebug(console_, row.str());
    }
    if (!assetId.IsValid()) {
        console_.Warning("Material Editor", "Apply To Selection requires a material asset.");
        LogMaterialGraphDebug(console_, "apply-material-rejected invalid asset");
        return false;
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(assetId);
    if (metadata == nullptr || !EditorSceneMaterialAssetActions::IsMaterialAsset(*metadata)) {
        console_.Warning("Material Editor", "Apply To Selection can only assign material assets.");
        LogMaterialGraphDebug(console_, "apply-material-rejected metadata missing/not material");
        return false;
    }

    std::vector<kb::scene::SceneEntity> targets;
    targets.reserve(SelectedHierarchyEntities().size());
    for (const kb::scene::SceneEntity entity : SelectedHierarchyEntities()) {
        if (scene_->Entities().IsAlive(entity) && scene_->Components().MeshRenderers().Has(entity)) {
            targets.push_back(entity);
            if (const kb::scene::MeshRendererComponent* renderer = scene_->Components().MeshRenderers().TryGet(entity)) {
                std::ostringstream row;
                row << "apply-material-target entity=" << entity.Id()
                    << " beforeMaterial=" << renderer->materialAssetId
                    << " mesh=" << renderer->meshAssetId
                    << " slotOverrideCount=" << renderer->materialSlotOverrideCount;
                LogMaterialGraphDebug(console_, row.str());
            }
        }
    }
    if (targets.empty()) {
        console_.Warning("Material Editor", "Apply To Selection found no selected Mesh Renderer.");
        LogMaterialGraphDebug(console_, "apply-material-rejected no mesh-renderer targets");
        return false;
    }

    const bool applied = ExecuteSceneCommand("Apply Material To Selection", [this, targets = std::move(targets), assetId]() {
        bool assigned = false;
        for (const kb::scene::SceneEntity entity : targets) {
            const bool entityAssigned = EditorSceneMaterialAssetActions::AssignMaterialToAllSlots(*scene_, entity, assetId);
            if (const kb::scene::MeshRendererComponent* renderer = scene_->Components().MeshRenderers().TryGet(entity)) {
                std::ostringstream row;
                row << "apply-material-target-result entity=" << entity.Id()
                    << " assigned=" << (entityAssigned ? "true" : "false")
                    << " afterMaterial=" << renderer->materialAssetId
                    << " slotOverrideCount=" << renderer->materialSlotOverrideCount;
                LogMaterialGraphDebug(console_, row.str());
            }
            assigned = entityAssigned || assigned;
        }
        return assigned;
    });
    if (applied) {
        static_cast<void>(assetBrowser_.SelectAsset(assetId, scene_->Assets().Manager()));
        // Cook the applied graph material for the scene's shading variant so the meshes
        // render the authored graph program rather than the builtin-PBR flatten.
        sceneGraphCookPending_ = true;
        MarkSceneRenderDirty();
    }
    LogMaterialGraphDebug(console_, "apply-material-result applied=" + std::string{ applied ? "true" : "false" } + " asset=" + std::to_string(assetId.value));
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

    const bool assigned = ExecuteSceneCommand(assetId.IsValid() ? "Assign Mesh Material Slot" : "Clear Mesh Material Slot", [this, entity, slotIndex, assetId]() {
        return EditorSceneMaterialAssetActions::AssignMaterialSlotOverride(*scene_, entity, slotIndex, assetId);
    });
    if (assigned && assetId.IsValid()) {
        // Cook the newly-assigned slot graph material for the scene's shading variant.
        sceneGraphCookPending_ = true;
    }
    return assigned;
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

std::vector<EditorSceneContext::EntityScriptVariable> EditorSceneContext::EntityScriptExposedVariables(kb::scene::SceneEntity entity) const {
    std::vector<EntityScriptVariable> result;
    const kb::scene::BehaviourComponent* behaviour = scene_->Components().Behaviours().TryGet(entity);
    if (behaviour == nullptr) {
        return result;
    }
    const kb::assets::AssetHandle<kb::script::LuaScriptAsset> asset =
        scene_->Assets().Manager().Load<kb::script::LuaScriptAsset>(kb::assets::AssetId{ behaviour->behaviourAssetId });
    if (!asset.IsLoaded()) {
        return result;
    }
    const kb::script::LuaScriptAsset& script = *asset.Get();
    const std::span<const kb::scene::BehaviourVariableOverride> overrides = scene_->Entities().BehaviourVariableOverrides(entity);
    result.reserve(script.exposedVariables.size());
    for (std::size_t index = 0U; index < script.exposedVariables.size(); ++index) {
        const kb::script::ScriptApiPin& pin = script.exposedVariables[index];
        // Effective value = the override delta if one is stored, else the
        // script's declared @expose default.
        kb::script::ScriptValue value = index < script.exposedVariableDefaults.size()
            ? script.exposedVariableDefaults[index]
            : kb::script::ScriptValue{};
        bool overridden = false;
        for (const kb::scene::BehaviourVariableOverride& entry : overrides) {
            if (entry.name == pin.name) {
                value = entry.value;
                overridden = true;
                break;
            }
        }
        result.push_back(EntityScriptVariable{
            .name = pin.name,
            .type = pin.type,
            .value = std::move(value),
            .overridden = overridden,
        });
    }
    return result;
}

bool EditorSceneContext::SetEntityScriptVariable(kb::scene::SceneEntity entity, std::string name, kb::script::ScriptValue value) {
    if (!entity.IsValid() || name.empty() || scene_->Components().Behaviours().TryGet(entity) == nullptr) {
        return false;
    }
    // Resolve the script's declared default so an edit back to it drops the
    // override instead of storing a redundant delta (store-only-non-default).
    kb::script::ScriptValue defaultValue;
    bool hasDefault = false;
    if (const kb::scene::BehaviourComponent* behaviour = scene_->Components().Behaviours().TryGet(entity); behaviour != nullptr) {
        const kb::assets::AssetHandle<kb::script::LuaScriptAsset> asset =
            scene_->Assets().Manager().Load<kb::script::LuaScriptAsset>(kb::assets::AssetId{ behaviour->behaviourAssetId });
        if (asset.IsLoaded()) {
            const kb::script::LuaScriptAsset& script = *asset.Get();
            for (std::size_t index = 0U; index < script.exposedVariables.size(); ++index) {
                if (script.exposedVariables[index].name == name && index < script.exposedVariableDefaults.size()) {
                    defaultValue = script.exposedVariableDefaults[index];
                    hasDefault = true;
                    break;
                }
            }
        }
    }
    return ExecuteSceneCommand("Set Script Variable", [this, entity, name, value, defaultValue, hasDefault]() {
        if (hasDefault && value == defaultValue) {
            static_cast<void>(scene_->Entities().RemoveBehaviourVariableOverride(entity, name));
        } else {
            scene_->Entities().SetBehaviourVariableOverride(entity, name, value);
        }
        return true;
    });
}

bool EditorSceneContext::RevertEntityScriptVariable(kb::scene::SceneEntity entity, std::string_view name) {
    if (!entity.IsValid() || name.empty()) {
        return false;
    }
    return ExecuteSceneCommand("Revert Script Variable", [this, entity, name = std::string{ name }]() {
        return scene_->Entities().RemoveBehaviourVariableOverride(entity, name);
    });
}

bool EditorSceneContext::ReloadOpenScriptAsset() {
    if (!scriptEditor_.IsOpen()) {
        return false;
    }
    const kb::assets::AssetId assetId = scriptEditor_.AssetId();
    if (!assetId.IsValid()) {
        return false;
    }
    // Erase the cache entry so EntityScriptExposedVariables' next Load re-reads
    // the file the Script Editor just wrote and re-parses the Inspector schema.
    return scene_->Assets().Manager().Unload(assetId);
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

bool EditorSceneContext::RemoveMeshRendererFromEntity(kb::scene::SceneEntity entity) {
    if (!entity.IsValid() || !scene_->Components().MeshRenderers().Has(entity)) {
        return false;
    }
    return ExecuteSceneCommand("Remove Component", [this, entity]() {
        if (!scene_->Entities().IsAlive(entity)) {
            return false;
        }
        scene_->Components().MeshRenderers().Remove(entity);
        return true;
    });
}

bool EditorSceneContext::RemovePhysicsComponent(kb::scene::SceneEntity entity, PhysicsComponentKind kind) {
    if (!entity.IsValid()) {
        return false;
    }
    const auto present = [&]() {
        switch (kind) {
        case PhysicsComponentKind::Rigidbody: return scene_->Components().Rigidbodies().Has(entity);
        case PhysicsComponentKind::Collider: return scene_->Components().Colliders().Has(entity);
        case PhysicsComponentKind::CharacterController: return scene_->Components().CharacterControllers().Has(entity);
        case PhysicsComponentKind::Joint: return scene_->Components().Joints().Has(entity);
        }
        return false;
    };
    if (!present()) {
        return false;
    }
    return ExecuteSceneCommand("Remove Component", [this, entity, kind]() {
        if (!scene_->Entities().IsAlive(entity)) {
            return false;
        }
        switch (kind) {
        case PhysicsComponentKind::Rigidbody: scene_->Components().Rigidbodies().Remove(entity); break;
        case PhysicsComponentKind::Collider: scene_->Components().Colliders().Remove(entity); break;
        case PhysicsComponentKind::CharacterController: scene_->Components().CharacterControllers().Remove(entity); break;
        case PhysicsComponentKind::Joint: scene_->Components().Joints().Remove(entity); break;
        }
        return true;
    });
}

kb::assets::AssetId EditorSceneContext::EntityScriptAssetId(kb::scene::SceneEntity entity) const {
    const kb::scene::BehaviourComponent* behaviour = scene_->Components().Behaviours().TryGet(entity);
    return behaviour != nullptr ? kb::assets::AssetId{ behaviour->behaviourAssetId } : kb::assets::AssetId{};
}

namespace {

// Mesh-local bounds of an entity's Mesh Renderer mesh: a per-axis axis-aligned
// box (center + half-extents) plus the bounding-sphere radius. Per-axis extents
// matter — a single sphere radius fits a flat plane/quad as a large CUBE, which
// is exactly the "collider is a big box around a plane" bug; the box half-extents
// hug the geometry on each axis instead.
struct EntityMeshBounds {
    kb::scene::Vec3 center{};
    kb::scene::Vec3 halfExtents{}; // per-axis, mesh-local
    float sphereRadius = 0.0F;
};

// Resolves EntityMeshBounds by loading the mesh through the AssetManager — the
// SAME mount-aware path the renderer uses — so procedural or virtual primitives
// with no on-disk physicalPath (and therefore no preview-stats entry) still fit.
// Bounds come from the same rasterizer the preview stats use, so they match what
// the viewport draws.
[[nodiscard]] bool TryLoadEntityMeshBounds(kb::scene::Scene& scene, kb::scene::SceneEntity entity, EntityMeshBounds& out, std::string& reason) {
    const kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(entity);
    if (renderer == nullptr) {
        reason = "entity has no Mesh Renderer";
        return false;
    }
    if (renderer->meshAssetId == 0U) {
        reason = "Mesh Renderer has no mesh assigned";
        return false;
    }
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetId meshAssetId{ renderer->meshAssetId };
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(meshAssetId);
    if (metadata == nullptr) {
        reason = "mesh asset " + std::to_string(renderer->meshAssetId) + " is not registered";
        return false;
    }

    const kb::assets::AssetHandle<kb::render::RenderMeshAssetData> asset =
        manager.Load<kb::render::RenderMeshAssetData>(meshAssetId);
    if (!asset.IsLoaded()) {
        reason = "mesh '" + metadata->type + "' could not be loaded for bounds";
        return false;
    }
    const kb::editor::EditorMeshPreviewGeometry geometry = kb::editor::EditorMeshPreviewRasterizer::ExtractGeometry(*asset);
    if (geometry.positions.size() < 3U) {
        reason = "mesh '" + metadata->type + "' produced no usable geometry (vertices=" + std::to_string(geometry.positions.size()) + ")";
        return false;
    }

    kb::editor::EditorMeshPreviewVector3 minPoint = geometry.positions.front();
    kb::editor::EditorMeshPreviewVector3 maxPoint = geometry.positions.front();
    for (const kb::editor::EditorMeshPreviewVector3& p : geometry.positions) {
        minPoint.x = std::min(minPoint.x, p.x);
        minPoint.y = std::min(minPoint.y, p.y);
        minPoint.z = std::min(minPoint.z, p.z);
        maxPoint.x = std::max(maxPoint.x, p.x);
        maxPoint.y = std::max(maxPoint.y, p.y);
        maxPoint.z = std::max(maxPoint.z, p.z);
    }
    out.center = kb::scene::Vec3{
        (minPoint.x + maxPoint.x) * 0.5F,
        (minPoint.y + maxPoint.y) * 0.5F,
        (minPoint.z + maxPoint.z) * 0.5F,
    };
    out.halfExtents = kb::scene::Vec3{
        (maxPoint.x - minPoint.x) * 0.5F,
        (maxPoint.y - minPoint.y) * 0.5F,
        (maxPoint.z - minPoint.z) * 0.5F,
    };
    out.sphereRadius = geometry.stats.boundsRadius > 0.0F
        ? geometry.stats.boundsRadius
        : std::sqrt(out.halfExtents.x * out.halfExtents.x + out.halfExtents.y * out.halfExtents.y + out.halfExtents.z * out.halfExtents.z);
    reason = "mesh '" + metadata->type + "' size="
        + std::to_string(out.halfExtents.x * 2.0F) + " x "
        + std::to_string(out.halfExtents.y * 2.0F) + " x "
        + std::to_string(out.halfExtents.z * 2.0F);
    return true;
}

// Writes mesh bounds into a collider according to its shape. Box uses the per-
// axis extents (so a plane becomes a thin slab, not a cube); a small minimum
// keeps a flat axis from collapsing to a degenerate zero-thickness shape the
// physics backend would reject.
void ApplyMeshBoundsToCollider(kb::scene::ColliderComponent& collider, const EntityMeshBounds& bounds) {
    constexpr float kMinHalfExtent = 0.005F;
    collider.center = bounds.center;
    switch (collider.shape) {
    case kb::scene::ColliderShape::Sphere:
        collider.radius = std::max(0.01F, bounds.sphereRadius);
        break;
    case kb::scene::ColliderShape::Capsule: {
        const float radius = std::max(0.01F, std::max(bounds.halfExtents.x, bounds.halfExtents.z));
        collider.radius = radius;
        collider.height = std::max(2.0F * radius, 2.0F * bounds.halfExtents.y);
        break;
    }
    case kb::scene::ColliderShape::Box:
    default:
        collider.boxSize = kb::scene::Vec3{
            std::max(2.0F * kMinHalfExtent, 2.0F * bounds.halfExtents.x),
            std::max(2.0F * kMinHalfExtent, 2.0F * bounds.halfExtents.y),
            std::max(2.0F * kMinHalfExtent, 2.0F * bounds.halfExtents.z),
        };
        break;
    }
}

} // namespace

bool EditorSceneContext::CanFitColliderToMesh(kb::scene::SceneEntity entity) const {
    // Cheap check only (this runs every paint): a Collider plus a Mesh Renderer
    // that references a mesh. The actual bounds load happens on the Fit action.
    if (!entity.IsValid() || !scene_->Components().Colliders().Has(entity)) {
        return false;
    }
    const kb::scene::MeshRendererComponent* renderer = scene_->Components().MeshRenderers().TryGet(entity);
    return renderer != nullptr && renderer->meshAssetId != 0U;
}

bool EditorSceneContext::ApplyColliderFitToMesh(kb::scene::SceneEntity entity, std::string& reason) {
    kb::scene::ColliderComponent* collider = scene_->Components().Colliders().TryGet(entity);
    if (collider == nullptr) {
        reason = "the entity has no Collider";
        return false;
    }
    EntityMeshBounds bounds;
    if (!TryLoadEntityMeshBounds(*scene_, entity, bounds, reason)) {
        return false;
    }
    // Mesh-local bounds; the entity's transform scale is applied later by the
    // physics backend and the debug-draw gizmo, so we store the unscaled size.
    ApplyMeshBoundsToCollider(*collider, bounds);
    scene_->Components().Colliders().MarkModified(entity);
    return true;
}

bool EditorSceneContext::FitColliderToMesh(kb::scene::SceneEntity entity) {
    if (!entity.IsValid() || !scene_->Components().Colliders().Has(entity)) {
        console_.Warning("Physics", "Fit to Mesh: the selected entity has no Collider.");
        return false;
    }
    const kb::scene::MeshRendererComponent* renderer = scene_->Components().MeshRenderers().TryGet(entity);
    if (renderer == nullptr || renderer->meshAssetId == 0U) {
        console_.Warning("Physics", "Fit to Mesh: the entity has no Mesh Renderer mesh to fit to.");
        return false;
    }
    std::string reason;
    const bool ok = ExecuteSceneCommand("Fit Collider To Mesh", [this, entity, &reason]() {
        return ApplyColliderFitToMesh(entity, reason);
    });
    if (ok) {
        console_.Info("Physics", "Fit to Mesh: " + reason + ".");
    } else {
        console_.Warning("Physics", "Fit to Mesh failed: " + (reason.empty() ? std::string{ "no resolvable mesh bounds" } : reason) + ".");
    }
    return ok;
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
    if (componentId == "Rigidbody") {
        if (scene_->Components().Rigidbodies().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Rigidbody component.");
            return false;
        }
        return ExecuteSceneCommand("Add Rigidbody Component", [this, entity]() {
            scene_->Components().Rigidbodies().Set(entity, kb::scene::RigidbodyComponent{});
            return true;
        });
    }
    if (componentId == "Collider") {
        if (scene_->Components().Colliders().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Collider component.");
            return false;
        }
        // Auto-fit the new collider to the entity's mesh so it matches the visible
        // geometry out of the box (Unity-style), instead of a default 0.5 sphere.
        // Per-axis, so a plane/quad becomes a thin slab rather than a cube.
        EntityMeshBounds bounds;
        std::string reason;
        const bool fitToMesh = TryLoadEntityMeshBounds(*scene_, entity, bounds, reason);
        if (fitToMesh) {
            console_.Info("Physics", "Collider auto-fit to mesh: " + reason + ".");
        } else {
            console_.Warning("Physics", "Collider added with default size — auto-fit skipped: " + reason + ".");
        }
        return ExecuteSceneCommand("Add Collider Component", [this, entity, fitToMesh, bounds]() {
            kb::scene::ColliderComponent collider{};
            if (fitToMesh) {
                ApplyMeshBoundsToCollider(collider, bounds);
            }
            scene_->Components().Colliders().Set(entity, collider);
            return true;
        });
    }
    if (componentId == "CharacterController") {
        if (scene_->Components().CharacterControllers().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Character Controller component.");
            return false;
        }
        return ExecuteSceneCommand("Add Character Controller Component", [this, entity]() {
            scene_->Components().CharacterControllers().Set(entity, kb::scene::CharacterControllerComponent{});
            return true;
        });
    }
    if (componentId == "Joint") {
        if (scene_->Components().Joints().Has(entity)) {
            console_.Warning("Inspector", "Entity already has a Joint component.");
            return false;
        }
        return ExecuteSceneCommand("Add Joint Component", [this, entity]() {
            scene_->Components().Joints().Set(entity, kb::scene::JointComponent{});
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
    return FinalizeActiveTransformEditApply(result.changed, result.touched);
}

bool EditorSceneContext::ApplyActiveTransformEditPrimaryRotation(kb::scene::Vec3 rotation) {
    const EditorSceneTransformEditApplyResult result =
        EditorSceneTransformEditController{ *scene_, activeTransformEdit_ }.ApplyPrimaryRotation(rotation);
    return FinalizeActiveTransformEditApply(result.changed, result.touched);
}

bool EditorSceneContext::ApplyActiveTransformEditRotationDelta(kb::scene::Quat delta) {
    const EditorSceneTransformEditApplyResult result =
        EditorSceneTransformEditController{ *scene_, activeTransformEdit_ }.ApplyRotationDelta(delta);
    return FinalizeActiveTransformEditApply(result.changed, result.touched);
}

bool EditorSceneContext::ApplyActiveTransformEditPrimaryScale(kb::scene::Vec3 scale) {
    const EditorSceneTransformEditApplyResult result =
        EditorSceneTransformEditController{ *scene_, activeTransformEdit_ }.ApplyPrimaryScale(scale);
    return FinalizeActiveTransformEditApply(result.changed, result.touched);
}

bool EditorSceneContext::ApplyActiveTransformEditProperty(InspectorPropertyId property, float value) {
    const EditorSceneTransformEditApplyResult result =
        EditorSceneTransformEditController{ *scene_, activeTransformEdit_ }.ApplyProperty(property, value);
    return FinalizeActiveTransformEditApply(result.changed, result.touched);
}

bool EditorSceneContext::FinalizeActiveTransformEditApply(
    bool changed,
    std::span<const kb::scene::SceneEntity> touched) {
    if (!changed) {
        return false;
    }

    MarkSceneEntitiesRenderDirty(touched);
    // Component overlays consume the canonical world transform cache directly.
    // Keep it current during an interactive edit instead of waiting for commit,
    // so colliders, joints, character controllers, lights, and descendants follow
    // the object on every drag update.
    scene_->Runtime().SynchronizeTransforms();
    return true;
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
        LogMaterialGraphDebug(console_, "record-edit-rejected label=" + label + " material=" + std::to_string(id.value));
        return false;
    }
    // [perf] One-shot log per edit: fires when a single graph edit (add/connect/delete a node) takes long
    // enough to feel. It brackets SetWorkingCopy + the runtime-preview sync (disk write + cook request); the
    // preview resolve on the next frame is timed separately in MaterialPreviewScene. Lets us tell whether an
    // "adding a node lags" report is the edit itself or the frame after it.
    struct EditPerfTimer {
        EditorConsoleState& console;
        std::string label;
        std::chrono::steady_clock::time_point start;
        ~EditPerfTimer() {
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (ms >= 2.0) {
                std::ostringstream row;
                row << "[perf] material edit '" << label << "': " << ms << " ms";
                console.Info("MaterialPerf", row.str());
            }
        }
    } editPerfTimer{ console_, label, std::chrono::steady_clock::now() };
    const std::string debugLabel = label;

    if (HasMaterialGraphWorkingCopyTransaction()) {
        if (materialGraphWorkingCopyTransactionAssetId_ != id) {
            LogMaterialGraphDebug(console_, "record-edit-transaction-rejected label=" + debugLabel + " material mismatch");
            return false;
        }
        materialGraphWorkingCopyTransactionChanged_ = true;
        // This branch records nothing and never reaches SetWorkingCopy, so an in-place mutator folded into an
        // open transaction would leave the dirty flag describing the document as it was before the gesture -
        // and the close/quit prompt reads that flag. Recompute it here instead.
        materialEditor_.RefreshDirty();
        LogMaterialGraphDebugDocument(console_, "record-edit-transaction " + debugLabel, *materialEditor_.WorkingCopy());
        const bool runtimeChanged = materialGraphWorkingCopyTransactionBefore_.has_value() &&
            MaterialWorkingCopyRuntimeContentHash(*materialGraphWorkingCopyTransactionBefore_) !=
                MaterialWorkingCopyRuntimeContentHash(*materialEditor_.WorkingCopy());
        if (runtimeChanged) {
            materialEditor_.ClearDiagnostics();
            SyncMaterialEditorWorkingCopyRuntimePreview();
            // An edit only changes the OPEN material, and RequestOpenMaterialSceneGraphCook (below) already
            // re-cooks it (with scene context) so meshes using it update. Re-arming sceneGraphCookPending_
            // here forced CookSceneGraphMaterials on the next frame, which re-reads and re-parses EVERY
            // material the scene references from disk, uncached - a multi-second stall on content-heavy
            // scenes. The scene-wide batch cook belongs to scene (re)load, not to a single-material edit.
            RequestOpenMaterialSceneGraphCook();
            // Likewise, a full MarkSceneRenderDirty() here forced a full-scene GPU resync on every single
            // graph edit (connect/create/disconnect) regardless of whether this material is even used by
            // the open scene - see MarkMaterialAssetRenderDirty's comment for the full rationale.
            MarkMaterialAssetRenderDirty(id);
        }
        return true;
    }

    kb::render::RenderMaterialAssetData after = *materialEditor_.WorkingCopy();
    const bool runtimeChanged = MaterialWorkingCopyRuntimeContentHash(before) != MaterialWorkingCopyRuntimeContentHash(after);
    if (beforeSelectedNodeIds.empty()) {
        beforeSelectedNodeIds = materialEditor_.SelectedNodeIds();
        if (beforeSelectedNodeId != 0U &&
            std::ranges::find(beforeSelectedNodeIds, beforeSelectedNodeId) == beforeSelectedNodeIds.end()) {
            beforeSelectedNodeIds.push_back(beforeSelectedNodeId);
        }
    }
    std::vector<std::uint32_t> afterSelectedNodeIds = materialEditor_.SelectedNodeIds();
    const std::uint32_t afterSelectedNodeId = materialEditor_.SelectedNodeId();
    const std::uint32_t afterSelectedCommentId = materialEditor_.SelectedCommentId();
    std::unique_ptr<EditorMaterialWorkingCopyEditCommand> command = EditorMaterialWorkingCopyEditCommand::Create(
        materialEditor_,
        id,
        std::move(label),
        before,
        std::move(after),
        beforeSelectedNodeIds,
        std::move(afterSelectedNodeIds),
        beforeSelectedNodeId,
        afterSelectedNodeId,
        beforeSelectedCommentId,
        afterSelectedCommentId);
    if (!commandStack_.Execute(std::move(command))) {
        materialEditor_.SetWorkingCopy(std::move(before));
        static_cast<void>(materialEditor_.SetNodeSelection(std::move(beforeSelectedNodeIds), beforeSelectedNodeId));
        if (beforeSelectedCommentId != 0U) {
            static_cast<void>(materialEditor_.SelectComment(beforeSelectedCommentId));
        }
        LogMaterialGraphDebug(console_, "record-edit-command-failed label=" + debugLabel + " material=" + std::to_string(id.value));
        return false;
    }

    if (runtimeChanged) {
        materialEditor_.ClearDiagnostics();
    }
    if (materialEditor_.WorkingCopy().has_value()) {
        LogMaterialGraphDebugDocument(console_, "record-edit-after " + debugLabel, *materialEditor_.WorkingCopy());
    }
    if (runtimeChanged) {
        SyncMaterialEditorWorkingCopyRuntimePreview();
        // Re-cook only the open material (below); do NOT re-arm the scene-wide sceneGraphCookPending_ here -
        // that made every edit re-read+re-parse all scene materials from disk next frame (the multi-second
        // stall). See the transaction path above for the full rationale.
        RequestOpenMaterialSceneGraphCook();
        MarkMaterialAssetRenderDirty(id);
    }
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

    const std::uint64_t beforeHash = MaterialWorkingCopyRuntimeContentHash(*materialEditor_.WorkingCopy());
    kb::render::RenderMaterialAssetData working = *materialEditor_.WorkingCopy();
    edit.Apply(working);
    const bool runtimeChanged = beforeHash != MaterialWorkingCopyRuntimeContentHash(working);
    materialEditor_.SetWorkingCopy(std::move(working));
    if (runtimeChanged) {
        materialEditor_.ClearDiagnostics();
    }
    if (runtimeChanged) {
        SyncMaterialEditorWorkingCopyRuntimePreview();
        // Re-cook only the open material (below); no scene-wide sceneGraphCookPending_ re-arm - see
        // RecordMaterialGraphWorkingCopyEdit for why that caused a multi-second per-edit disk stall.
        RequestOpenMaterialSceneGraphCook();
        MarkMaterialAssetRenderDirty(id);
    }
    return true;
}

void EditorSceneContext::SyncMaterialEditorWorkingCopyRuntimePreview() {
    if (scene_ == nullptr || !materialEditor_.OpenAssetId().IsValid() || !materialEditor_.WorkingCopy().has_value() || !materialEditor_.Dirty()) {
        LogMaterialGraphDebug(console_, "runtime-preview-clear reason=missing-scene-open-workingcopy-or-not-dirty");
        ClearMaterialEditorWorkingCopyRuntimePreview();
        return;
    }

    const kb::assets::AssetId openAsset = materialEditor_.OpenAssetId();
    kb::assets::AssetManager& manager = scene_->Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(openAsset);
    if (metadata == nullptr || (metadata->type != "RenderMaterial" && metadata->type != "RenderMaterialInstance" &&
            metadata->type != kb::render::kRenderMaterialGraphAssetType)) {
        LogMaterialGraphDebug(console_, "runtime-preview-clear reason=metadata-missing-or-not-material asset=" + std::to_string(openAsset.value));
        ClearMaterialEditorWorkingCopyRuntimePreview();
        return;
    }

    if (materialRuntimePreviewAssetId_.IsValid() && materialRuntimePreviewAssetId_ != openAsset) {
        ClearMaterialEditorWorkingCopyRuntimePreview();
        metadata = manager.Registry().Find(openAsset);
        if (metadata == nullptr || (metadata->type != "RenderMaterial" && metadata->type != "RenderMaterialInstance" &&
                metadata->type != kb::render::kRenderMaterialGraphAssetType)) {
            LogMaterialGraphDebug(console_, "runtime-preview-abort reason=metadata-lost-after-clear asset=" + std::to_string(openAsset.value));
            return;
        }
    }

    kb::render::RenderMaterialAssetData runtimeMaterial = *materialEditor_.WorkingCopy();
    LogMaterialGraphDebugDocument(console_, "runtime-preview-before-sanitize", runtimeMaterial);
    SanitizeMaterialGraphTextureMetadata(runtimeMaterial);
    // The temporary runtime document is the authoritative unsaved working copy. Keeping the
    // persisted sourceGraph reference here would make the resolver intentionally replace these
    // edits with the on-disk source graph before Save.
    runtimeMaterial.graphSourceAssetId = 0U;
    runtimeMaterial.graphSourceAssetPath.clear();
    runtimeMaterial.graph.storageModel = "inline-kbmat";
    LogMaterialGraphDebugDocument(console_, "runtime-preview-after-sanitize", runtimeMaterial);

    const std::uint64_t runtimeContentHash = MaterialWorkingCopyRuntimeContentHash(runtimeMaterial);
    if (materialRuntimePreviewAssetId_ == openAsset && materialRuntimePreviewContentHash_ == runtimeContentHash) {
        std::error_code existsError;
        if (!materialRuntimePreviewPath_.empty() && std::filesystem::exists(materialRuntimePreviewPath_, existsError)) {
            LogMaterialGraphDebug(console_, "runtime-preview-skip unchanged hash=" + std::to_string(runtimeContentHash) + " path=" + materialRuntimePreviewPath_.generic_string());
            return;
        }
    }

    // Hot-reload last-good (MAT-33): if the working copy is currently invalid but we already have a
    // live runtime preview, keep rendering that last-good material and only kick a recook so the
    // cook service reports Stale (with the failure reason) instead of dropping to a black/error frame.
    if (materialEditor_.DiagnosticsHaveError() && materialRuntimePreviewAssetId_ == openAsset) {
        if (materialGraphCookService_ != nullptr) {
            LogMaterialGraphDebug(console_, "runtime-preview-cook-last-good asset=" + std::to_string(openAsset.value) + " hash=" + std::to_string(runtimeContentHash));
            static_cast<void>(materialGraphCookService_->RequestCook(
                openAsset,
                runtimeMaterial,
                MaterialPreviewGraphBuildContext(openAsset, materialPreviewScene_->SceneSettings())));
        }
        return;
    }

    if (!materialRuntimePreviewSourceMetadata_.has_value()) {
        materialRuntimePreviewSourceMetadata_ = *metadata;
    }

    const std::filesystem::path runtimePath = SceneMaterialWorkingCopyRuntimePath(
        openAsset,
        runtimeContentHash,
        metadata->physicalPath.empty() ? metadata->virtualPath.parent_path() : metadata->physicalPath.parent_path(),
        this);
    if (!materialRuntimePreviewPath_.empty() && materialRuntimePreviewPath_ != runtimePath) {
        std::error_code removeError;
        std::filesystem::remove(materialRuntimePreviewPath_, removeError);
    }
    std::error_code directoryError;
    std::filesystem::create_directories(runtimePath.parent_path(), directoryError);
    if (!kb::render::RenderMaterialAssetWriter::Save(runtimePath, runtimeMaterial)) {
        console_.Warning("Materials", "Material graph live preview could not write its runtime working copy.");
        LogMaterialGraphDebug(console_, "runtime-preview-write-failed path=" + runtimePath.generic_string());
        return;
    }
    LogMaterialGraphDebug(console_, "runtime-preview-write path=" + runtimePath.generic_string() + " hash=" + std::to_string(runtimeContentHash));

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
        LogMaterialGraphDebug(console_, "runtime-preview-request-cook asset=" + std::to_string(openAsset.value) + " hash=" + std::to_string(runtimeContentHash));
        static_cast<void>(materialGraphCookService_->RequestCook(
            openAsset,
            runtimeMaterial,
            MaterialPreviewGraphBuildContext(openAsset, materialPreviewScene_->SceneSettings())));
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
    LogMaterialGraphDebug(console_, "save-material-request asset=" + std::to_string(id.value));
    if (materialEditor_.OpenAssetId() != id || !materialEditor_.WorkingCopy().has_value()) {
        console_.Error("Materials", "Material working copy is not available for Save.");
        LogMaterialGraphDebug(console_, "save-material-rejected missing/open working copy");
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
    LogMaterialGraphDebugDocument(console_, "save-material-before-sanitize", after);
    SanitizeMaterialGraphTextureMetadata(after);
    LogMaterialGraphDebugDocument(console_, "save-material-after-sanitize", after);
    if (!ValidateMaterialEditorAssetCandidate(id, &after, nullptr)) {
        LogMaterialGraphDebug(console_, "save-material-preflight-failed asset=" + std::to_string(id.value));
        return false;
    }
    ClearMaterialEditorWorkingCopyRuntimePreview();
    std::unique_ptr<EditorMaterialAssetEditCommand> command = EditorMaterialAssetEditCommand::CreateRecorded(
        *scene_,
        id,
        "Save Material",
        std::move(before),
        after);
    if (!commandStack_.Execute(std::move(command))) {
        console_.Warning("Materials", "Material working copy could not be saved.");
        LogMaterialGraphDebug(console_, "save-material-command-failed asset=" + std::to_string(id.value));
        SyncMaterialEditorWorkingCopyRuntimePreview();
        return false;
    }

    materialEditor_.SetWorkingCopy(after);
    materialEditor_.MarkSaved();
    ClearMaterialEditorWorkingCopyRuntimePreview();
    // MAT-87: the saved material must propagate to every scene mesh using it. Recook the scene's
    // graph materials (deduped) and re-resolve so meshes pick up the new program next frame.
    if (materialGraphCookService_ != nullptr && (!after.graph.links.empty() || after.graph.nodes.size() > 1U)) {
        LogMaterialGraphDebug(console_, "save-material-request-cook asset=" + std::to_string(id.value));
        static_cast<void>(materialGraphCookService_->RequestCook(id, after));
        sceneGraphCookPending_ = true;
    }
    MarkSceneRenderDirty();
    return true;
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

    if (!ValidateMaterialEditorAssetCandidate(id, nullptr, &after)) {
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
        SyncMaterialEditorWorkingCopyRuntimePreview();
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
    return true;
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
        sceneGraphCookPending_ = true;
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
    sceneGraphCookPending_ = true;
    RequestOpenMaterialSceneGraphCook();
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

void EditorSceneContext::RequestOpenMaterialSceneGraphCook() {
    if (scene_ == nullptr || materialGraphCookService_ == nullptr) {
        return;
    }
    const kb::assets::AssetId openAsset = materialEditor_.OpenAssetId();
    if (!openAsset.IsValid() || !materialEditor_.WorkingCopy().has_value()) {
        LogMaterialGraphDebug(console_, "scene-cook-open-skip reason=no-open-working-copy");
        return;
    }
    const kb::render::RenderMaterialAssetData& material = *materialEditor_.WorkingCopy();
    if (material.graph.links.empty() && material.graph.nodes.size() <= 1U) {
        LogMaterialGraphDebug(console_, "scene-cook-open-skip reason=no-authored-graph asset=" + std::to_string(openAsset.value));
        return;
    }
    const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(openAsset);
    {
        std::ostringstream row;
        row << "scene-cook-open-request asset=" << openAsset.value
            << " graphNodes=" << material.graph.nodes.size()
            << " graphLinks=" << material.graph.links.size()
            << " outputNormalLinked=" << (MaterialGraphDebugOutputHasLink(material.graph, "normal") ? "true" : "false")
            << " sceneLightingPath=" << static_cast<int>(project_.sceneLightingPath);
        LogMaterialGraphDebug(console_, row.str());
    }
    static_cast<void>(materialGraphCookService_->RequestCook(
        openAsset,
        material,
        SceneMaterialGraphBuildContext(openAsset, metadata, project_.sceneLightingPath)));
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
        // The open material must be cooked from its live working copy, but with the scene/runtime
        // graph context rather than the material-preview context.
        if (materialEditor_.OpenAssetId() == id) {
            RequestOpenMaterialSceneGraphCook();
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
        const kb::assets::AssetMetadata* metadata = scene_->Assets().Manager().Registry().Find(id);
        static_cast<void>(materialGraphCookService_->RequestCook(
            id,
            *material,
            SceneMaterialGraphBuildContext(id, metadata, project_.sceneLightingPath)));
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
