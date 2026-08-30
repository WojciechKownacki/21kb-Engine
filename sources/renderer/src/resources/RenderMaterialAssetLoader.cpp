#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMemoryInputStream.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "resources/RenderMaterialAssetParser.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialNumericParsing.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "kb/render/resources/RenderMaterialTypeAssetLoader.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kb::render {
namespace {

void AppendUnique(std::vector<kb::assets::AssetId>& dependencies, kb::assets::AssetId id) {
    if (!id.IsValid()) {
        return;
    }
    for (const kb::assets::AssetId existing : dependencies) {
        if (existing == id) {
            return;
        }
    }
    dependencies.push_back(id);
}

[[nodiscard]] kb::assets::AssetId MaterialTypeDependencyId(std::string_view materialType, std::uint32_t materialTypeVersion) {
    std::string key{ "MaterialType:" };
    key += materialType;
    key += ':';
    key += std::to_string(materialTypeVersion);
    return kb::assets::MakeAssetId(key);
}

[[nodiscard]] bool IsMaterialTypeAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMaterialType" || metadata.type == "MaterialType" || metadata.importCategory == "MaterialType";
}

[[nodiscard]] bool IsMaterialGraphAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMaterialGraph" || metadata.type == "MaterialGraph" || metadata.importCategory == "MaterialGraph";
}

[[nodiscard]] bool IsTextureAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderTexture" || metadata.type == "Texture" || metadata.importCategory == "Texture";
}

[[nodiscard]] std::optional<kb::assets::AssetId> ResolveMaterialTypePathDependency(
    std::string_view materialTypePath,
    const kb::assets::AssetMetadata& owner,
    const kb::assets::AssetRegistry& registry) {
    if (materialTypePath.empty()) {
        return std::nullopt;
    }

    const std::filesystem::path authoredPath{ std::string{ materialTypePath } };
    const std::filesystem::path candidate = authoredPath.is_absolute()
        ? authoredPath.lexically_normal()
        : (owner.virtualPath.parent_path() / authoredPath).lexically_normal();
    const kb::assets::AssetMetadata* materialType = registry.FindByPath(candidate);
    if (materialType == nullptr || !IsMaterialTypeAsset(*materialType)) {
        return std::nullopt;
    }
    return materialType->id;
}

[[nodiscard]] std::optional<kb::assets::AssetId> ResolveMaterialGraphPathDependency(
    std::string_view graphPath,
    const kb::assets::AssetMetadata& owner,
    const kb::assets::AssetRegistry& registry) {
    if (graphPath.empty()) {
        return std::nullopt;
    }

    const std::filesystem::path authoredPath{ std::string{ graphPath } };
    const std::filesystem::path candidate = authoredPath.is_absolute()
        ? authoredPath.lexically_normal()
        : (owner.virtualPath.parent_path() / authoredPath).lexically_normal();
    const kb::assets::AssetMetadata* graph = registry.FindByPath(candidate);
    if (graph == nullptr || !IsMaterialGraphAsset(*graph)) {
        return std::nullopt;
    }
    return graph->id;
}

[[nodiscard]] std::optional<kb::assets::AssetId> ResolveTexturePathDependency(
    std::string_view texturePath,
    const kb::assets::AssetMetadata& owner,
    const kb::assets::AssetRegistry& registry) {
    if (texturePath.empty()) {
        return std::nullopt;
    }

    const std::filesystem::path authoredPath{ std::string{ texturePath } };
    const std::filesystem::path candidate = authoredPath.is_absolute()
        ? authoredPath.lexically_normal()
        : (owner.virtualPath.parent_path() / authoredPath).lexically_normal();
    const kb::assets::AssetMetadata* texture = registry.FindByPath(candidate);
    if (texture == nullptr || !IsTextureAsset(*texture)) {
        return std::nullopt;
    }
    return texture->id;
}

[[nodiscard]] const RenderMaterialGraphParameterValue* FindGraphParameterValue(
    const std::vector<RenderMaterialGraphParameterValue>& values,
    std::string_view stableId) noexcept {
    for (const RenderMaterialGraphParameterValue& value : values) {
        if (value.stableId == stableId) {
            return &value;
        }
    }
    return nullptr;
}

[[nodiscard]] bool HasMaterialParameter(const RenderMaterialTypeSchema& schema, std::string_view stableId) noexcept {
    for (const RenderMaterialParameterSchema& parameter : schema.parameters) {
        if (parameter.name == stableId) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] const RenderMaterialParameterSchema* FindMaterialParameter(
    const RenderMaterialTypeSchema& schema,
    std::string_view stableId) noexcept {
    for (const RenderMaterialParameterSchema& parameter : schema.parameters) {
        if (parameter.name == stableId) {
            return &parameter;
        }
    }
    return nullptr;
}

[[nodiscard]] const RenderMaterialTextureSlotSchema* FindMaterialTextureSlot(
    const RenderMaterialTypeSchema& schema,
    std::string_view stableId) noexcept {
    for (const RenderMaterialTextureSlotSchema& slot : schema.textureSlots) {
        if ((!slot.stableId.empty() ? std::string_view{ slot.stableId } : std::string_view{ slot.name }) == stableId) {
            return &slot;
        }
    }
    return nullptr;
}

[[nodiscard]] bool HasMaterialValue(const RenderMaterialTypeSchema& schema, std::string_view stableId) noexcept {
    return HasMaterialParameter(schema, stableId) || FindMaterialTextureSlot(schema, stableId) != nullptr;
}

[[nodiscard]] std::vector<float> ParseDefaultNumbers(std::string_view text) {
    std::vector<float> numbers;
    if (text.empty() || text == "_") {
        return numbers;
    }
    static_cast<void>(ParseFiniteMaterialFloatSequence(text, numbers, 1U, 64U));
    return numbers;
}

[[nodiscard]] RenderMaterialGraphParameterValue DefaultGraphParameterValue(const RenderMaterialParameterSchema& parameter) {
    RenderMaterialGraphParameterValue value{
        .stableId = parameter.name,
        .type = parameter.type,
    };
    const std::vector<float> numbers = ParseDefaultNumbers(parameter.defaultValueHint);
    switch (parameter.type) {
    case RenderMaterialParameterType::Scalar:
        if (!numbers.empty()) value.numbers[0] = numbers[0];
        break;
    case RenderMaterialParameterType::Vec3:
        for (std::size_t index = 0U; index < std::min<std::size_t>(3U, numbers.size()); ++index) value.numbers[index] = numbers[index];
        break;
    case RenderMaterialParameterType::Vec4:
    case RenderMaterialParameterType::Color:
        for (std::size_t index = 0U; index < std::min<std::size_t>(4U, numbers.size()); ++index) value.numbers[index] = numbers[index];
        break;
    case RenderMaterialParameterType::Bool:
        value.boolValue = parameter.defaultValueHint == "true" || parameter.defaultValueHint == "1";
        break;
    case RenderMaterialParameterType::Enum:
        value.text = parameter.defaultValueHint == "_" ? std::string{} : parameter.defaultValueHint;
        break;
    case RenderMaterialParameterType::Texture:
        value.assetId = 0U;
        break;
    }
    return value;
}

[[nodiscard]] bool IsBuiltInMaterialField(std::string_view stableId) noexcept {
    return stableId == "baseColor" ||
        stableId == "baseColorFactor" ||
        stableId == "emissiveColor" ||
        stableId == "emissiveFactor" ||
        stableId == "metallicFactor" ||
        stableId == "roughnessFactor" ||
        stableId == "normalScale" ||
        stableId == "occlusionStrength" ||
        stableId == "emissiveStrength" ||
        stableId == "alphaCutoff" ||
        stableId == "alphaMode" ||
        stableId == "doubleSided";
}

[[nodiscard]] bool IsBuiltInMaterialTextureField(std::string_view field) noexcept {
    return field == "albedoTextureAssetId" ||
        field == "normalTextureAssetId" ||
        field == "metallicRoughnessTextureAssetId" ||
        field == "occlusionTextureAssetId" ||
        field == "emissiveTextureAssetId" ||
        field == "clearcoatTextureAssetId" ||
        field == "clearcoatRoughnessTextureAssetId" ||
        field == "sheenColorTextureAssetId" ||
        field == "transmissionTextureAssetId" ||
        field == "thicknessTextureAssetId" ||
        field == "anisotropyTextureAssetId" ||
        field == "decalTextureAssetId" ||
        field == "layerMaskTextureAssetId";
}

[[nodiscard]] std::filesystem::path ResolveAssetPath(
    const kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& metadata) {
    if (!metadata.physicalPath.empty()) {
        return metadata.physicalPath;
    }
    return manager.Mounts().Resolve(metadata.virtualPath).value_or(std::filesystem::path{});
}

void AppendMaterialTypeReferenceDiagnostic(
    RenderMaterialTypeReferenceValidationResult& result,
    RenderMaterialTypeReferenceDiagnosticCode code,
    kb::assets::AssetId assetId,
    std::filesystem::path path,
    std::string message) {
    result.diagnostics.push_back(RenderMaterialTypeReferenceDiagnostic{
        .code = code,
        .assetId = assetId,
        .path = std::move(path),
        .message = std::move(message),
    });
}

[[nodiscard]] const kb::assets::AssetMetadata* FindMaterialTypeAssetByPath(
    const kb::assets::AssetManager& manager,
    std::string_view materialTypeAssetPath) {
    if (materialTypeAssetPath.empty()) {
        return nullptr;
    }
    const std::filesystem::path authoredPath{ std::string{ materialTypeAssetPath } };
    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(authoredPath);
    if (metadata != nullptr) {
        return metadata;
    }
    if (const std::optional<std::filesystem::path> resolved = manager.Mounts().Resolve(authoredPath)) {
        for (const kb::assets::AssetMetadata& candidate : manager.Registry().All()) {
            const std::filesystem::path candidatePath = ResolveAssetPath(manager, candidate);
            std::error_code error;
            if (!candidatePath.empty() && std::filesystem::equivalent(candidatePath, *resolved, error) && !error) {
                return &candidate;
            }
        }
    }
    return nullptr;
}

} // namespace

std::string_view RenderMaterialAssetParseDiagnosticCodeName(RenderMaterialAssetParseDiagnosticCode code) noexcept {
    switch (code) {
    case RenderMaterialAssetParseDiagnosticCode::FileOpenFailed:
        return "file_open_failed";
    case RenderMaterialAssetParseDiagnosticCode::EmptyDocument:
        return "empty_document";
    case RenderMaterialAssetParseDiagnosticCode::UnknownField:
        return "unknown_field";
    case RenderMaterialAssetParseDiagnosticCode::InvalidFieldValue:
        return "invalid_field_value";
    case RenderMaterialAssetParseDiagnosticCode::InvalidFloat:
        return "invalid_float";
    case RenderMaterialAssetParseDiagnosticCode::InvalidEnum:
        return "invalid_enum";
    case RenderMaterialAssetParseDiagnosticCode::OutOfRange:
        return "out_of_range";
    case RenderMaterialAssetParseDiagnosticCode::UnsupportedAdvancedField:
        return "unsupported_advanced_field";
    case RenderMaterialAssetParseDiagnosticCode::InvalidDocumentVersion:
        return "invalid_document_version";
    case RenderMaterialAssetParseDiagnosticCode::UnsupportedDocumentVersion:
        return "unsupported_document_version";
    case RenderMaterialAssetParseDiagnosticCode::MissingMaterialType:
        return "missing_material_type";
    case RenderMaterialAssetParseDiagnosticCode::UnsupportedMaterialType:
        return "unsupported_material_type";
    case RenderMaterialAssetParseDiagnosticCode::InvalidMaterialTypeVersion:
        return "invalid_material_type_version";
    case RenderMaterialAssetParseDiagnosticCode::UnsupportedMaterialTypeVersion:
        return "unsupported_material_type_version";
    case RenderMaterialAssetParseDiagnosticCode::TextureColorSpaceExpectation:
        return "texture_color_space_expectation";
    case RenderMaterialAssetParseDiagnosticCode::InvalidGraphField:
        return "invalid_graph_field";
    case RenderMaterialAssetParseDiagnosticCode::UnsupportedGraphVersion:
        return "unsupported_graph_version";
    case RenderMaterialAssetParseDiagnosticCode::GraphMigration:
        return "graph_migration";
    case RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode:
        return "invalid_graph_node";
    case RenderMaterialAssetParseDiagnosticCode::DuplicateGraphNode:
        return "duplicate_graph_node";
    case RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink:
        return "invalid_graph_link";
    }
    return "unknown_diagnostic";
}

std::string_view RenderMaterialTypeReferenceDiagnosticCodeName(RenderMaterialTypeReferenceDiagnosticCode code) noexcept {
    switch (code) {
    case RenderMaterialTypeReferenceDiagnosticCode::MissingMaterialTypeReference:
        return "missing_material_type_reference";
    case RenderMaterialTypeReferenceDiagnosticCode::MissingMaterialTypeAsset:
        return "missing_material_type_asset";
    case RenderMaterialTypeReferenceDiagnosticCode::IncompatibleMaterialTypeAsset:
        return "incompatible_material_type_asset";
    case RenderMaterialTypeReferenceDiagnosticCode::MaterialTypeAssetLoadFailed:
        return "material_type_asset_load_failed";
    case RenderMaterialTypeReferenceDiagnosticCode::IncompatibleMaterialType:
        return "incompatible_material_type";
    case RenderMaterialTypeReferenceDiagnosticCode::IncompatibleMaterialTypeVersion:
        return "incompatible_material_type_version";
    }
    return "material_type_reference_error";
}

std::vector<RenderMaterialGraphDiagnostic> ValidateRenderMaterialAssetGraphDiagnostics(const RenderMaterialAssetData& asset) {
    std::vector<RenderMaterialGraphDiagnostic> diagnostics = ValidateRenderMaterialGraphDocument(asset.graph);
    const RenderMaterialTypeSchema graphSchema = BuildRenderMaterialGraphParameterSchema(asset.graph, "runtime.validation", 1U);
    const bool graphDeclaresParameters = !graphSchema.parameters.empty() || !graphSchema.textureSlots.empty();
    std::unordered_map<std::string, std::size_t> valueStableIds;
    for (std::size_t index = 0U; index < asset.graphParameterValues.size(); ++index) {
        const RenderMaterialGraphParameterValue& value = asset.graphParameterValues[index];
        const auto [existing, inserted] = valueStableIds.emplace(value.stableId, index);
        if (value.stableId.empty() || !inserted) {
            diagnostics.push_back(RenderMaterialGraphDiagnostic{
                .severity = RenderMaterialGraphDiagnosticSeverity::Error,
                .kind = RenderMaterialGraphDiagnosticKind::DuplicateParameterStableId,
                .message = value.stableId.empty()
                    ? "Material graph parameter value requires a non-empty stable id."
                    : "Material graph parameter value stable id '" + value.stableId + "' is duplicated at entries " +
                        std::to_string(existing->second) + " and " + std::to_string(index) + ".",
            });
            continue;
        }
        if (!graphDeclaresParameters) {
            // Values may be declared by an external Material Type schema. That schema is not
            // available to this graph-only validator, so only intrinsic duplicate/id checks apply.
            continue;
        }

        const RenderMaterialParameterSchema* parameter = FindMaterialParameter(graphSchema, value.stableId);
        const RenderMaterialTextureSlotSchema* texture = FindMaterialTextureSlot(graphSchema, value.stableId);
        if (parameter == nullptr && texture == nullptr) {
            // The parameter schema only covers nodes reachable from Material Output, so a value left over from a
            // node that is currently disconnected (or was removed) has no declaration here. That is harmless -
            // shader generation walks the graph, not the value list, so the value is simply unused - and it is
            // dropped by the schema reconciliation on save/reopen. Flag it as a Warning, not a blocking
            // shader_generation_failed Error, so a WIP graph with a disconnected texture node doesn't read as broken.
            diagnostics.push_back(RenderMaterialGraphDiagnostic{
                .severity = RenderMaterialGraphDiagnosticSeverity::Warning,
                .kind = RenderMaterialGraphDiagnosticKind::UnusedParameterValue,
                .message = "Material graph parameter value '" + value.stableId +
                    "' is unused: its node is not connected to the Material Output. It is cleaned up when you save.",
            });
            continue;
        }
        const RenderMaterialParameterType expectedType = texture != nullptr ? RenderMaterialParameterType::Texture : parameter->type;
        if (value.type != expectedType) {
            diagnostics.push_back(RenderMaterialGraphDiagnostic{
                .severity = RenderMaterialGraphDiagnosticSeverity::Error,
                .kind = RenderMaterialGraphDiagnosticKind::TypeMismatch,
                .message = "Material graph parameter value '" + value.stableId + "' has a type incompatible with its graph declaration.",
            });
        }
    }
    if (asset.desc.alphaMode == RenderMaterialAlphaMode::Blend) {
        diagnostics.push_back(RenderMaterialGraphDiagnostic{
            .severity = RenderMaterialGraphDiagnosticSeverity::Warning,
            .kind = RenderMaterialGraphDiagnosticKind::UnsupportedBlendMode,
            .nodeId = 0U,
            .linkId = 0U,
            .pin = "alpha",
            .message = "Alpha BLEND is parsed but disabled by the current mesh runtime; use OPAQUE or MASK until transparent pass support is enabled.",
        });
    }
    return diagnostics;
}

RenderMaterialSchemaRefreshResult RefreshRenderMaterialGraphBackedMaterialSchema(
    const RenderMaterialAssetData& material,
    const RenderMaterialTypeDocument& materialType) {
    RenderMaterialSchemaRefreshResult result{
        .material = material,
        .diagnostics = {},
    };
    result.material.materialType = materialType.stableTypeId;
    result.material.materialTypeVersion = materialType.version;
    result.material.hasExplicitMaterialType = true;
    result.material.hasExplicitMaterialTypeVersion = true;

    if (material.materialType != materialType.stableTypeId || material.materialTypeVersion != materialType.version) {
        result.diagnostics.push_back(RenderMaterialSchemaRefreshDiagnostic{
            .kind = RenderMaterialSchemaRefreshDiagnosticKind::MaterialTypeChanged,
            .stableId = materialType.stableTypeId,
            .message = "Material Type reference refreshed to " + materialType.stableTypeId + " version " + std::to_string(materialType.version) + ".",
        });
    }

    std::vector<RenderMaterialGraphParameterValue> refreshedValues;
    refreshedValues.reserve(materialType.schema.parameters.size() + materialType.schema.textureSlots.size());
    for (const RenderMaterialParameterSchema& parameter : materialType.schema.parameters) {
        if (IsBuiltInMaterialField(parameter.name)) {
            continue;
        }
        if (const RenderMaterialGraphParameterValue* existing = FindGraphParameterValue(material.graphParameterValues, parameter.name);
            existing != nullptr) {
            if (existing->type == parameter.type) {
                refreshedValues.push_back(*existing);
                continue;
            }
            refreshedValues.push_back(DefaultGraphParameterValue(parameter));
            result.diagnostics.push_back(RenderMaterialSchemaRefreshDiagnostic{
                .kind = RenderMaterialSchemaRefreshDiagnosticKind::ChangedParameterType,
                .stableId = parameter.name,
                .message = "Reset graph parameter '" + parameter.name + "' to its default because its type changed in the refreshed Material Type schema.",
            });
            continue;
        }
        refreshedValues.push_back(DefaultGraphParameterValue(parameter));
        result.diagnostics.push_back(RenderMaterialSchemaRefreshDiagnostic{
            .kind = RenderMaterialSchemaRefreshDiagnosticKind::AddedDefaultParameter,
            .stableId = parameter.name,
            .message = "Added default value for graph parameter '" + parameter.name + "'.",
        });
    }

    for (const RenderMaterialTextureSlotSchema& slot : materialType.schema.textureSlots) {
        if (slot.stableId.empty() && IsBuiltInMaterialTextureField(slot.assetIdFieldName)) {
            continue;
        }
        const std::string& stableId = slot.stableId.empty() ? slot.name : slot.stableId;
        if (const RenderMaterialGraphParameterValue* existing = FindGraphParameterValue(material.graphParameterValues, stableId);
            existing != nullptr) {
            if (existing->type == RenderMaterialParameterType::Texture) {
                refreshedValues.push_back(*existing);
                continue;
            }
            refreshedValues.push_back(RenderMaterialGraphParameterValue{
                .stableId = stableId,
                .type = RenderMaterialParameterType::Texture,
            });
            result.diagnostics.push_back(RenderMaterialSchemaRefreshDiagnostic{
                .kind = RenderMaterialSchemaRefreshDiagnosticKind::ChangedParameterType,
                .stableId = stableId,
                .message = "Reset graph texture slot '" + stableId + "' because its stored value was not a texture.",
            });
            continue;
        }
        refreshedValues.push_back(RenderMaterialGraphParameterValue{
            .stableId = stableId,
            .type = RenderMaterialParameterType::Texture,
        });
        result.diagnostics.push_back(RenderMaterialSchemaRefreshDiagnostic{
            .kind = RenderMaterialSchemaRefreshDiagnosticKind::AddedDefaultParameter,
            .stableId = stableId,
            .message = "Added default value for graph texture slot '" + stableId + "'.",
        });
    }

    for (const RenderMaterialGraphParameterValue& existing : material.graphParameterValues) {
        if (!HasMaterialValue(materialType.schema, existing.stableId)) {
            result.diagnostics.push_back(RenderMaterialSchemaRefreshDiagnostic{
                .kind = RenderMaterialSchemaRefreshDiagnosticKind::RemovedUnknownParameter,
                .stableId = existing.stableId,
                .message = "Removed graph parameter value '" + existing.stableId + "' because it is not present in the refreshed Material Type schema.",
            });
        }
    }

    result.material.graphParameterValues = std::move(refreshedValues);
    return result;
}

template <typename LoadMaterialType>
RenderMaterialTypeReferenceValidationResult ValidateRenderMaterialTypeReferenceImpl(
    const RenderMaterialAssetData& material,
    const kb::assets::AssetMetadata& materialMetadata,
    const kb::assets::AssetManager& manager,
    LoadMaterialType&& loadMaterialType) {
    RenderMaterialTypeReferenceValidationResult result{};
    const bool builtInPbr =
        material.materialType.empty() ||
        (material.materialType == kRenderMaterialAssetBuiltInPbrType &&
            material.materialTypeVersion == kRenderMaterialAssetBuiltInPbrTypeVersion);
    const bool hasTypeReference = material.materialTypeAssetId != 0U || !material.materialTypeAssetPath.empty();
    if (builtInPbr && !hasTypeReference) {
        return result;
    }
    if (!hasTypeReference) {
        AppendMaterialTypeReferenceDiagnostic(
            result,
            RenderMaterialTypeReferenceDiagnosticCode::MissingMaterialTypeReference,
            materialMetadata.id,
            materialMetadata.virtualPath,
            "Material type '" + material.materialType + "' version " + std::to_string(material.materialTypeVersion) + " requires a Material Type asset reference.");
        return result;
    }

    const kb::assets::AssetMetadata* idMetadata = nullptr;
    if (material.materialTypeAssetId != 0U) {
        idMetadata = manager.Registry().Find(kb::assets::AssetId{ material.materialTypeAssetId });
        if (idMetadata == nullptr) {
            AppendMaterialTypeReferenceDiagnostic(
                result,
                RenderMaterialTypeReferenceDiagnosticCode::MissingMaterialTypeAsset,
                kb::assets::AssetId{ material.materialTypeAssetId },
                {},
                "Referenced Material Type asset id " + std::to_string(material.materialTypeAssetId) + " is not registered.");
        } else if (!IsMaterialTypeAsset(*idMetadata)) {
            AppendMaterialTypeReferenceDiagnostic(
                result,
                RenderMaterialTypeReferenceDiagnosticCode::IncompatibleMaterialTypeAsset,
                idMetadata->id,
                idMetadata->virtualPath,
                "Referenced asset id " + std::to_string(idMetadata->id.value) + " is not a Material Type asset.");
            idMetadata = nullptr;
        }
    }

    const kb::assets::AssetMetadata* pathMetadata = nullptr;
    if (!material.materialTypeAssetPath.empty()) {
        pathMetadata = FindMaterialTypeAssetByPath(manager, material.materialTypeAssetPath);
        if (pathMetadata == nullptr) {
            AppendMaterialTypeReferenceDiagnostic(
                result,
                RenderMaterialTypeReferenceDiagnosticCode::MissingMaterialTypeAsset,
                {},
                material.materialTypeAssetPath,
                "Referenced Material Type asset path '" + material.materialTypeAssetPath + "' is not registered.");
        } else if (!IsMaterialTypeAsset(*pathMetadata)) {
            AppendMaterialTypeReferenceDiagnostic(
                result,
                RenderMaterialTypeReferenceDiagnosticCode::IncompatibleMaterialTypeAsset,
                pathMetadata->id,
                pathMetadata->virtualPath,
                "Referenced asset path '" + material.materialTypeAssetPath + "' is not a Material Type asset.");
            pathMetadata = nullptr;
        }
    }

    if (idMetadata != nullptr && pathMetadata != nullptr && idMetadata->id != pathMetadata->id) {
        AppendMaterialTypeReferenceDiagnostic(
            result,
            RenderMaterialTypeReferenceDiagnosticCode::IncompatibleMaterialTypeAsset,
            idMetadata->id,
            pathMetadata->virtualPath,
            "Material Type asset id and path resolve to different assets.");
        return result;
    }

    const kb::assets::AssetMetadata* typeMetadata = idMetadata != nullptr ? idMetadata : pathMetadata;
    if (typeMetadata == nullptr) {
        return result;
    }

    const std::filesystem::path resolvedPath = ResolveAssetPath(manager, *typeMetadata);
    const std::filesystem::path diagnosticPath = resolvedPath.empty() ? typeMetadata->virtualPath : resolvedPath;
    std::optional<RenderMaterialTypeDocument> loadedType;
    std::string loadError;
    if (!loadMaterialType(*typeMetadata, resolvedPath, loadedType, loadError) || !loadedType.has_value()) {
        AppendMaterialTypeReferenceDiagnostic(
            result,
            RenderMaterialTypeReferenceDiagnosticCode::MaterialTypeAssetLoadFailed,
            typeMetadata->id,
            diagnosticPath,
            loadError.empty()
                ? "Material Type asset could not be loaded."
                : "Material Type asset could not be loaded: " + loadError);
        return result;
    }

    result.materialType = *loadedType;
    if (loadedType->stableTypeId != material.materialType) {
        AppendMaterialTypeReferenceDiagnostic(
            result,
            RenderMaterialTypeReferenceDiagnosticCode::IncompatibleMaterialType,
            typeMetadata->id,
            diagnosticPath,
            "Material Type asset stable id '" + loadedType->stableTypeId + "' does not match material type '" + material.materialType + "'.");
    }
    if (loadedType->version != material.materialTypeVersion) {
        AppendMaterialTypeReferenceDiagnostic(
            result,
            RenderMaterialTypeReferenceDiagnosticCode::IncompatibleMaterialTypeVersion,
            typeMetadata->id,
            diagnosticPath,
            "Material Type asset version " + std::to_string(loadedType->version) + " does not match material version " + std::to_string(material.materialTypeVersion) + ".");
    }
    return result;
}

RenderMaterialTypeReferenceValidationResult ValidateRenderMaterialTypeReference(
    const RenderMaterialAssetData& material,
    const kb::assets::AssetMetadata& materialMetadata,
    kb::assets::AssetManager& manager) {
    return ValidateRenderMaterialTypeReferenceImpl(
        material,
        materialMetadata,
        manager,
        [&manager](
            const kb::assets::AssetMetadata& metadata,
            const std::filesystem::path&,
            std::optional<RenderMaterialTypeDocument>& out,
            std::string& error) {
            const kb::assets::AssetHandle<RenderMaterialTypeDocument> loaded =
                manager.Load<RenderMaterialTypeDocument>(metadata.id);
            if (!loaded.IsLoaded()) {
                error = manager.LastError();
                return false;
            }
            out = *loaded;
            return true;
        });
}

RenderMaterialTypeReferenceValidationResult ValidateRenderMaterialTypeReference(
    const RenderMaterialAssetData& material,
    const kb::assets::AssetMetadata& materialMetadata,
    const kb::assets::AssetManager& manager) {
    return ValidateRenderMaterialTypeReferenceImpl(
        material,
        materialMetadata,
        manager,
        [](const kb::assets::AssetMetadata&,
           const std::filesystem::path& path,
           std::optional<RenderMaterialTypeDocument>& out,
           std::string& error) {
            if (path.empty()) {
                error = "Material Type asset path could not be resolved.";
                return false;
            }
            RenderMaterialTypeDocumentParseResult loaded =
                RenderMaterialTypeAssetLoader::LoadTypeWithDiagnostics(path);
            if (!loaded.document.has_value()) {
                error = "Material Type asset document could not be parsed.";
                return false;
            }
            out = std::move(*loaded.document);
            return true;
        });
}

bool RenderMaterialTypeReferenceValidationResult::Succeeded() const noexcept {
    return diagnostics.empty();
}

bool RenderMaterialAssetParseResult::HasErrors() const noexcept {
    return std::ranges::any_of(diagnostics, [](const RenderMaterialAssetParseDiagnostic& diagnostic) {
        return diagnostic.severity == RenderMaterialAssetParseDiagnosticSeverity::Error;
    });
}

bool RenderMaterialAssetParseResult::HasWarnings() const noexcept {
    return std::ranges::any_of(diagnostics, [](const RenderMaterialAssetParseDiagnostic& diagnostic) {
        return diagnostic.severity == RenderMaterialAssetParseDiagnosticSeverity::Warning;
    });
}

bool RenderMaterialAssetParseResult::Succeeded() const noexcept {
    return asset.has_value() && !HasErrors();
}

std::string RenderMaterialAssetParseResult::DiagnosticMessage() const {
    if (diagnostics.empty()) {
        return {};
    }

    std::ostringstream output;
    output << "Render material asset diagnostics";
    for (const RenderMaterialAssetParseDiagnostic& diagnostic : diagnostics) {
        output << "; code " << RenderMaterialAssetParseDiagnosticCodeName(diagnostic.code);
        if (diagnostic.assetId.IsValid()) {
            output << ", asset " << diagnostic.assetId.value;
        }
        if (!diagnostic.path.empty()) {
            output << ", path " << diagnostic.path.generic_string();
        }
        output << ": ";
        if (diagnostic.line > 0U) {
            output << "line " << diagnostic.line << ": ";
        }
        output << diagnostic.message;
        if (!diagnostic.text.empty()) {
            output << " [" << diagnostic.text << "]";
        }
    }
    return output.str();
}

std::string RenderMaterialAssetParseResult::ErrorMessage() const {
    if (!HasErrors()) {
        return {};
    }

    std::ostringstream output;
    output << "Render material asset load failed";
    for (const RenderMaterialAssetParseDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity != RenderMaterialAssetParseDiagnosticSeverity::Error) {
            continue;
        }
        output << "; code " << RenderMaterialAssetParseDiagnosticCodeName(diagnostic.code);
        if (diagnostic.assetId.IsValid()) {
            output << ", asset " << diagnostic.assetId.value;
        }
        if (!diagnostic.path.empty()) {
            output << ", path " << diagnostic.path.generic_string();
        }
        output << ": ";
        if (diagnostic.line > 0U) {
            output << "line " << diagnostic.line << ": ";
        }
        output << diagnostic.message;
        if (!diagnostic.text.empty()) {
            output << " [" << diagnostic.text << "]";
        }
    }
    return output.str();
}

std::string_view RenderMaterialAssetLoader::Type() const noexcept {
    return "RenderMaterial";
}

std::type_index RenderMaterialAssetLoader::PayloadType() const noexcept {
    return typeid(RenderMaterialAssetData);
}

std::vector<std::string> RenderMaterialAssetLoader::Extensions() const {
    return { ".kbmat" };
}

kb::assets::AssetLoadResult RenderMaterialAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::vector<std::uint8_t> sourceBytes;
    std::string error;
    if (!request.ReadSourceBytes(sourceBytes, error)) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = std::move(error) };
    }
    kb::assets::AssetMemoryInputStream input{ sourceBytes };
    RenderMaterialAssetParseResult material = LoadMaterialWithDiagnostics(
        input,
        RenderMaterialAssetParseSourceContext{
            .assetId = request.metadata.id,
            .path = request.resolvedPath,
        });
    if (!material.asset.has_value()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = material.ErrorMessage() };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<RenderMaterialAssetData>(*material.asset),
        .error = {},
    };
}

std::vector<kb::assets::AssetId> RenderMaterialAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) const {
    const RenderMaterialAssetParseResult material = LoadMaterialWithDiagnostics(metadata.physicalPath, metadata.id);
    if (!material.asset.has_value()) {
        return {};
    }
    return DiscoverMaterialDependencies(*material.asset, metadata, registry);
}

std::optional<RenderMaterialAssetData> RenderMaterialAssetLoader::LoadMaterial(const std::filesystem::path& path) {
    return RenderMaterialAssetParser::Load(path);
}

std::optional<RenderMaterialAssetData> RenderMaterialAssetLoader::LoadMaterial(std::istream& input) {
    return RenderMaterialAssetParser::Parse(input);
}

RenderMaterialAssetParseResult RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(const std::filesystem::path& path) {
    return RenderMaterialAssetParser::LoadWithDiagnostics(path);
}

RenderMaterialAssetParseResult RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(const std::filesystem::path& path, kb::assets::AssetId assetId) {
    return RenderMaterialAssetParser::LoadWithDiagnostics(path, assetId);
}

RenderMaterialAssetParseResult RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(std::istream& input) {
    return RenderMaterialAssetParser::ParseWithDiagnostics(input);
}

RenderMaterialAssetParseResult RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(std::istream& input, const RenderMaterialAssetParseSourceContext& sourceContext) {
    return RenderMaterialAssetParser::ParseWithDiagnostics(input, sourceContext);
}

std::vector<kb::assets::AssetId> RenderMaterialAssetLoader::DiscoverMaterialDependencies(
    const RenderMaterialAssetData& material,
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) {
    std::vector<kb::assets::AssetId> dependencies;
    dependencies.reserve(16U);
    if (material.materialTypeAssetId != 0U) {
        AppendUnique(dependencies, kb::assets::AssetId{ material.materialTypeAssetId });
    } else {
        AppendUnique(dependencies, MaterialTypeDependencyId(material.materialType, material.materialTypeVersion));
    }
    if (std::optional<kb::assets::AssetId> materialTypePathDependency = ResolveMaterialTypePathDependency(material.materialTypeAssetPath, metadata, registry); materialTypePathDependency.has_value()) {
        AppendUnique(dependencies, *materialTypePathDependency);
    }
    if (material.graphSourceAssetId != 0U) {
        AppendUnique(dependencies, kb::assets::AssetId{ material.graphSourceAssetId });
    }
    if (std::optional<kb::assets::AssetId> graphSourcePathDependency = ResolveMaterialGraphPathDependency(material.graphSourceAssetPath, metadata, registry); graphSourcePathDependency.has_value()) {
        AppendUnique(dependencies, *graphSourcePathDependency);
    }
    AppendUnique(dependencies, kb::assets::AssetId{ material.graph.lastGoodArtifact.assetId });
    for (const std::uint64_t functionAssetId : DiscoverRenderMaterialGraphFunctionDependencies(material.graph)) {
        const kb::assets::AssetId id{ functionAssetId };
        const kb::assets::AssetMetadata* functionMetadata = registry.Find(id);
        if (functionMetadata != nullptr && functionMetadata->type == kRenderMaterialFunctionAssetType) {
            AppendUnique(dependencies, id);
        }
    }
    for (const std::uint64_t collectionAssetId : DiscoverRenderMaterialGraphParameterCollectionDependencies(material.graph)) {
        const kb::assets::AssetId id{ collectionAssetId };
        const kb::assets::AssetMetadata* collectionMetadata = registry.Find(id);
        if (collectionMetadata != nullptr && collectionMetadata->type == kRenderMaterialParameterCollectionAssetType) {
            AppendUnique(dependencies, id);
        }
    }

    const std::array<std::uint64_t, 13U> textureAssetIds{
        material.desc.albedoTextureAssetId,
        material.desc.normalTextureAssetId,
        material.desc.metallicRoughnessTextureAssetId,
        material.desc.occlusionTextureAssetId,
        material.desc.emissiveTextureAssetId,
        material.desc.clearcoatTextureAssetId,
        material.desc.clearcoatRoughnessTextureAssetId,
        material.desc.sheenColorTextureAssetId,
        material.desc.transmissionTextureAssetId,
        material.desc.thicknessTextureAssetId,
        material.desc.anisotropyTextureAssetId,
        material.desc.decalTextureAssetId,
        material.desc.layerMaskTextureAssetId,
    };
    for (const std::uint64_t textureAssetId : textureAssetIds) {
        AppendUnique(dependencies, kb::assets::AssetId{ textureAssetId });
    }

    const std::array<std::string_view, 13U> texturePaths{
        material.albedoTexturePath,
        material.normalTexturePath,
        material.metallicRoughnessTexturePath,
        material.occlusionTexturePath,
        material.emissiveTexturePath,
        material.clearcoatTexturePath,
        material.clearcoatRoughnessTexturePath,
        material.sheenColorTexturePath,
        material.transmissionTexturePath,
        material.thicknessTexturePath,
        material.anisotropyTexturePath,
        material.decalTexturePath,
        material.layerMaskTexturePath,
    };
    for (const std::string_view texturePath : texturePaths) {
        if (const std::optional<kb::assets::AssetId> textureId = ResolveTexturePathDependency(texturePath, metadata, registry)) {
            AppendUnique(dependencies, *textureId);
        }
    }
    for (const RenderMaterialGraphParameterValue& value : material.graphParameterValues) {
        if (value.type == RenderMaterialParameterType::Texture) {
            AppendUnique(dependencies, kb::assets::AssetId{ value.assetId });
        }
    }

    return dependencies;
}

} // namespace kb::render
