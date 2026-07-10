#include "scene/material/EditorMaterialAssetAuthoring.hpp"

#include "console/EditorConsoleState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialTypeAssetLoader.hpp"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kb::editor {
namespace {

[[nodiscard]] float Clamp01(float value) noexcept {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float ClampNonNegative(float value) noexcept {
    return std::max(0.0F, value);
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

[[nodiscard]] std::uint64_t& TextureSlot(kb::render::RenderMaterialAssetData& asset, EditorMaterialTextureSlot slot) noexcept {
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
    return asset.desc.albedoTextureAssetId;
}

[[nodiscard]] std::uint64_t TextureSlotValue(const kb::render::RenderMaterialAssetData& asset, EditorMaterialTextureSlot slot) noexcept {
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

[[nodiscard]] std::filesystem::path ResolveAssetPhysicalPath(
    kb::scene::Scene& scene,
    const kb::assets::AssetMetadata& metadata) {
    if (!metadata.physicalPath.empty()) {
        return metadata.physicalPath;
    }
    return scene.Assets().Manager().Mounts().Resolve(metadata.virtualPath).value_or(std::filesystem::path{});
}

[[nodiscard]] std::string StableTypeIdFromGraph(const kb::assets::AssetMetadata& graphMetadata) {
    std::string stable = graphMetadata.virtualPath.stem().generic_string();
    if (stable.empty()) {
        stable = graphMetadata.name.empty() ? "graph.surface" : graphMetadata.name;
    }
    for (char& ch : stable) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))) {
            ch = '.';
        }
    }
    return "graph." + stable;
}

[[nodiscard]] std::optional<float> ParseDefaultFloat(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    std::string value{ text };
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str()) {
        return std::nullopt;
    }
    return parsed;
}

[[nodiscard]] kb::render::RenderMaterialGraphParameterMetadata FunctionEndpointMetadata(
    std::string stableId,
    std::string displayName) {
    return kb::render::RenderMaterialGraphParameterMetadata{
        .stableId = std::move(stableId),
        .displayName = std::move(displayName),
        .defaultValueHint = "float4",
        .overrideSupported = false,
    };
}

[[nodiscard]] kb::render::RenderMaterialFunctionAssetData MakeDefaultMaterialFunctionAsset() {
    kb::render::RenderMaterialFunctionAssetData function{};
    function.graph.storageModel = "material-function-asset";
    function.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 1U,
        .kind = kb::render::RenderMaterialGraphNodeKind::FunctionInput,
        .positionX = 80,
        .positionY = 160,
        .parameter = FunctionEndpointMetadata("Input", "Input"),
    });
    function.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::FunctionOutput,
        .positionX = 420,
        .positionY = 160,
        .parameter = FunctionEndpointMetadata("Output", "Output"),
    });
    function.graph.links.push_back(kb::render::RenderMaterialGraphLink{
        .fromNodeId = 1U,
        .fromPinId = kb::render::RenderMaterialGraphStablePinId(function.graph.nodes[0], "value", true),
        .fromPin = "value",
        .toNodeId = 2U,
        .toPinId = kb::render::RenderMaterialGraphStablePinId(function.graph.nodes[1], "value", false),
        .toPin = "value",
    });
    function.graph.links.back().id = kb::render::MakeRenderMaterialGraphLinkId(function.graph.links.back());
    return function;
}

void ApplySchemaDefaultsToMaterial(kb::render::RenderMaterialAssetData& material, const kb::render::RenderMaterialTypeSchema& schema) {
    for (const kb::render::RenderMaterialParameterSchema& parameter : schema.parameters) {
        const std::optional<float> defaultValue = ParseDefaultFloat(parameter.defaultValueHint);
        if (!defaultValue.has_value()) {
            continue;
        }
        if (parameter.name == "roughnessFactor") {
            material.desc.roughnessFactor = std::clamp(*defaultValue, 0.0F, 1.0F);
        } else if (parameter.name == "metallicFactor") {
            material.desc.metallicFactor = std::clamp(*defaultValue, 0.0F, 1.0F);
        } else if (parameter.name == "emissiveStrength") {
            material.desc.emissiveStrength = ClampNonNegative(*defaultValue);
        }
    }
}

} // namespace

EditorMaterialAssetAuthoring::EditorMaterialAssetAuthoring(kb::scene::Scene& scene, EditorAssetBrowserState& browser, EditorConsoleState& console) noexcept
    : gateway_(scene, browser)
    , console_(console) {}

bool EditorMaterialAssetAuthoring::Create(const std::filesystem::path& virtualFolder) {
    const std::optional<std::filesystem::path> folder = gateway_.ResolveFolder(virtualFolder);
    if (!folder.has_value()) {
        console_.Error("Materials", "Could not resolve a physical folder for the new material.");
        return false;
    }

    const std::filesystem::path path = EditorMaterialAssetGateway::UniqueFilePath(*folder, "NewMaterial");
    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    if (!gateway_.WriteNewMaterial(path, material)) {
        console_.Error("Materials", "Material asset could not be written: " + path.generic_string());
        return false;
    }

    console_.Info("Materials", "Material asset created: " + path.generic_string());
    return true;
}

bool EditorMaterialAssetAuthoring::CreateFunction(const std::filesystem::path& virtualFolder) {
    const std::optional<std::filesystem::path> folder = gateway_.ResolveFolder(virtualFolder);
    if (!folder.has_value()) {
        console_.Error("Materials", "Could not resolve a physical folder for the new material function.");
        return false;
    }

    const kb::render::RenderMaterialFunctionAssetData function = MakeDefaultMaterialFunctionAsset();
    const std::filesystem::path path = EditorMaterialAssetGateway::UniqueFilePath(*folder, "NewMaterialFunction", kb::render::kRenderMaterialFunctionAssetExtension);
    if (!gateway_.WriteNewMaterialFunction(path, function)) {
        console_.Error("Materials", "Material function asset could not be written: " + path.generic_string());
        return false;
    }

    console_.Info("Materials", "Material function asset created: " + path.generic_string());
    return true;
}

bool EditorMaterialAssetAuthoring::CreateGraph(const std::filesystem::path& virtualFolder) {
    const std::optional<std::filesystem::path> folder = gateway_.ResolveFolder(virtualFolder);
    if (!folder.has_value()) {
        console_.Error("Materials", "Could not resolve a physical folder for the new material graph.");
        return false;
    }

    kb::render::RenderMaterialGraphDocument graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    graph.storageModel = "material-graph-asset";
    const std::filesystem::path path = EditorMaterialAssetGateway::UniqueFilePath(*folder, "NewMaterialGraph", kb::render::kRenderMaterialGraphAssetExtension);
    if (!gateway_.WriteNewMaterialGraph(path, graph)) {
        console_.Error("Materials", "Material graph asset could not be written: " + path.generic_string());
        return false;
    }

    console_.Info("Materials", "Material graph asset created: " + path.generic_string());
    return true;
}

bool EditorMaterialAssetAuthoring::CreateInstance(kb::assets::AssetId parentMaterial) {
    const kb::assets::AssetMetadata* parent = gateway_.Scene().Assets().Manager().Registry().Find(parentMaterial);
    if (parent == nullptr || parent->type != "RenderMaterial") {
        console_.Error("Materials", "Material instance creation requires a parent material asset.");
        return false;
    }

    std::filesystem::path parentPath = parent->physicalPath;
    if (parentPath.empty()) {
        const std::optional<std::filesystem::path> resolved = gateway_.Scene().Assets().Manager().Mounts().Resolve(parent->virtualPath);
        if (!resolved.has_value()) {
            console_.Error("Materials", "Parent material path could not be resolved for material instance creation.");
            return false;
        }
        parentPath = *resolved;
    }

    kb::render::RenderMaterialInstanceAssetData instance{};
    instance.parentMaterialAssetId = parentMaterial;
    const std::filesystem::path baseName = parentPath.stem().string() + std::string{ "Instance" };
    const std::filesystem::path path = EditorMaterialAssetGateway::UniqueFilePath(parentPath.parent_path(), baseName.string(), ".kbmatinst");
    if (!gateway_.WriteNewMaterialInstance(path, instance)) {
        console_.Error("Materials", "Material instance asset could not be written: " + path.generic_string());
        return false;
    }

    console_.Info("Materials", "Material instance asset created: " + path.generic_string());
    return true;
}

bool EditorMaterialAssetAuthoring::CreateMaterialType(const std::filesystem::path& virtualFolder) {
    const std::optional<std::filesystem::path> folder = gateway_.ResolveFolder(virtualFolder);
    if (!folder.has_value()) {
        console_.Error("Materials", "Could not resolve a physical folder for the new material type.");
        return false;
    }

    kb::render::RenderMaterialTypeDocument document = kb::render::GetBuiltInPbrMaterialTypeDocument();
    document.stableTypeId = "graph.surface";
    document.displayName = "Graph Surface";
    document.schema.typeName = document.stableTypeId;
    const std::filesystem::path path = EditorMaterialAssetGateway::UniqueFilePath(*folder, "NewMaterialType", kb::render::kRenderMaterialTypeAssetExtension);
    if (!gateway_.WriteNewMaterialType(path, document)) {
        console_.Error("Materials", "Material Type asset could not be written: " + path.generic_string());
        return false;
    }

    console_.Info("Materials", "Material Type asset created: " + path.generic_string());
    return true;
}

bool EditorMaterialAssetAuthoring::CreateMaterialFromGraph(kb::assets::AssetId graphAssetId) {
    const kb::assets::AssetMetadata* graphMetadata = gateway_.Scene().Assets().Manager().Registry().Find(graphAssetId);
    if (graphMetadata == nullptr || graphMetadata->type != kb::render::kRenderMaterialGraphAssetType) {
        console_.Error("Materials", "Create Material From Graph requires a Material Graph asset.");
        return false;
    }
    const std::filesystem::path graphPath = ResolveAssetPhysicalPath(gateway_.Scene(), *graphMetadata);
    if (graphPath.empty()) {
        console_.Error("Materials", "Material Graph path could not be resolved.");
        return false;
    }
    const kb::assets::AssetId sourceGraphId = graphMetadata->id;
    const std::string sourceGraphPath = graphMetadata->virtualPath.generic_string();
    std::optional<kb::render::RenderMaterialGraphDocument> graph = kb::render::RenderMaterialGraphAssetLoader::LoadGraph(graphPath);
    if (!graph.has_value()) {
        console_.Error("Materials", "Material Graph could not be loaded: " + graphPath.generic_string());
        return false;
    }

    const std::string stableTypeId = StableTypeIdFromGraph(*graphMetadata);
    kb::render::RenderMaterialGraphMaterialTypeBuildResult typeResult = kb::render::BuildRenderMaterialGraphMaterialTypeDocument(
        *graph,
        stableTypeId,
        1U,
        kb::render::RenderMaterialGraphBuildContext{
            .assetId = graphAssetId.value,
            .sourcePath = graphMetadata->virtualPath.generic_string(),
        });
    if (!typeResult.Succeeded() || !typeResult.document.has_value()) {
        console_.Error("Materials", "Material Graph could not generate a Material Type.");
        return false;
    }

    const std::filesystem::path folder = graphPath.parent_path();
    const std::string baseName = graphPath.stem().string();
    const std::filesystem::path typePath = EditorMaterialAssetGateway::UniqueFilePath(folder, baseName + std::string{ "Type" }, kb::render::kRenderMaterialTypeAssetExtension);
    if (!gateway_.WriteNewMaterialType(typePath, *typeResult.document)) {
        console_.Error("Materials", "Generated Material Type could not be written: " + typePath.generic_string());
        return false;
    }

    const kb::assets::AssetMetadata* typeMetadata = nullptr;
    if (const std::optional<std::filesystem::path> typeVirtualPath = gateway_.Scene().Assets().Manager().Mounts().ToVirtual(typePath)) {
        typeMetadata = gateway_.Scene().Assets().Manager().Registry().FindByPath(*typeVirtualPath);
    }
    if (typeMetadata == nullptr) {
        console_.Error("Materials", "Generated Material Type metadata was not discovered.");
        return false;
    }

    kb::render::RenderMaterialAssetData material{};
    material.materialType = typeResult.document->stableTypeId;
    material.materialTypeVersion = typeResult.document->version;
    material.hasExplicitMaterialType = true;
    material.hasExplicitMaterialTypeVersion = true;
    material.materialTypeAssetId = typeMetadata->id.value;
    material.materialTypeAssetPath = typeMetadata->virtualPath.generic_string();
    material.graphSourceAssetId = sourceGraphId.value;
    material.graphSourceAssetPath = sourceGraphPath;
    material.graph = *graph;
    material.graph.storageModel = "material-graph-asset";
    ApplySchemaDefaultsToMaterial(material, typeResult.document->schema);

    const std::filesystem::path materialPath = EditorMaterialAssetGateway::UniqueFilePath(folder, baseName + std::string{ "Material" });
    if (!gateway_.WriteNewMaterial(materialPath, material)) {
        console_.Error("Materials", "Graph-backed Material could not be written: " + materialPath.generic_string());
        return false;
    }
    console_.Info("Materials", "Graph-backed Material created: " + materialPath.generic_string());
    return true;
}

bool EditorMaterialAssetAuthoring::CreateMaterialFromMaterialType(kb::assets::AssetId materialTypeAssetId) {
    const kb::assets::AssetMetadata* typeMetadata = gateway_.Scene().Assets().Manager().Registry().Find(materialTypeAssetId);
    if (typeMetadata == nullptr || typeMetadata->type != kb::render::kRenderMaterialTypeAssetType) {
        console_.Error("Materials", "Create Material From Material Type requires a Material Type asset.");
        return false;
    }
    const std::filesystem::path typePath = ResolveAssetPhysicalPath(gateway_.Scene(), *typeMetadata);
    if (typePath.empty()) {
        console_.Error("Materials", "Material Type path could not be resolved.");
        return false;
    }
    std::optional<kb::render::RenderMaterialTypeDocument> type = kb::render::RenderMaterialTypeAssetLoader::LoadType(typePath);
    if (!type.has_value()) {
        console_.Error("Materials", "Material Type could not be loaded: " + typePath.generic_string());
        return false;
    }

    kb::render::RenderMaterialAssetData material{};
    material.materialType = type->stableTypeId;
    material.materialTypeVersion = type->version;
    material.hasExplicitMaterialType = true;
    material.hasExplicitMaterialTypeVersion = true;
    material.materialTypeAssetId = typeMetadata->id.value;
    material.materialTypeAssetPath = typeMetadata->virtualPath.generic_string();
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    ApplySchemaDefaultsToMaterial(material, type->schema);

    const std::filesystem::path materialPath = EditorMaterialAssetGateway::UniqueFilePath(typePath.parent_path(), typePath.stem().string() + std::string{ "Material" });
    if (!gateway_.WriteNewMaterial(materialPath, material)) {
        console_.Error("Materials", "Material from Material Type could not be written: " + materialPath.generic_string());
        return false;
    }
    console_.Info("Materials", "Material from Material Type created: " + materialPath.generic_string());
    return true;
}

std::optional<kb::render::RenderMaterialAssetData> EditorMaterialAssetAuthoring::Read(kb::assets::AssetId id) const {
    return EditorMaterialAssetGateway::Read(gateway_.Scene(), id);
}

bool EditorMaterialAssetAuthoring::SetBaseColor(kb::assets::AssetId id, int channel, float value) {
    if (channel < 0 || channel >= 4) {
        return false;
    }
    const bool saved = gateway_.Mutate(id, [channel, value](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.baseColor[channel] = Clamp01(value);
    });
    if (!saved) {
        console_.Error("Materials", "Material base color could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::SetEmissiveColor(kb::assets::AssetId id, int channel, float value) {
    if (channel < 0 || channel >= 3) {
        return false;
    }
    const bool saved = gateway_.Mutate(id, [channel, value](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.emissiveColor[channel] = ClampNonNegative(value);
    });
    if (!saved) {
        console_.Error("Materials", "Material emissive color could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::SetMetallicFactor(kb::assets::AssetId id, float value) {
    const bool saved = gateway_.Mutate(id, [value](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.metallicFactor = Clamp01(value);
    });
    if (!saved) {
        console_.Error("Materials", "Material metallic factor could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::SetRoughnessFactor(kb::assets::AssetId id, float value) {
    const bool saved = gateway_.Mutate(id, [value](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.roughnessFactor = Clamp01(value);
    });
    if (!saved) {
        console_.Error("Materials", "Material roughness factor could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::SetNormalScale(kb::assets::AssetId id, float value) {
    const bool saved = gateway_.Mutate(id, [value](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.normalScale = ClampNonNegative(value);
    });
    if (!saved) {
        console_.Error("Materials", "Material normal scale could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::SetOcclusionStrength(kb::assets::AssetId id, float value) {
    const bool saved = gateway_.Mutate(id, [value](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.occlusionStrength = Clamp01(value);
    });
    if (!saved) {
        console_.Error("Materials", "Material occlusion strength could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::SetEmissiveStrength(kb::assets::AssetId id, float value) {
    const bool saved = gateway_.Mutate(id, [value](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.emissiveStrength = ClampNonNegative(value);
    });
    if (!saved) {
        console_.Error("Materials", "Material emissive strength could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::SetAlphaCutoff(kb::assets::AssetId id, float value) {
    const bool saved = gateway_.Mutate(id, [value](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.alphaCutoff = Clamp01(value);
    });
    if (!saved) {
        console_.Error("Materials", "Material alpha cutoff could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::SetAlphaMode(kb::assets::AssetId id, kb::render::RenderMaterialAlphaMode mode) {
    const bool saved = gateway_.Mutate(id, [mode](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.alphaMode = mode;
    });
    if (!saved) {
        console_.Error("Materials", "Material alpha mode could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::CycleAlphaMode(kb::assets::AssetId id) {
    const bool saved = gateway_.Mutate(id, [](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.alphaMode = NextAlphaMode(asset.desc.alphaMode);
    });
    if (!saved) {
        console_.Error("Materials", "Material alpha mode could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::ToggleDoubleSided(kb::assets::AssetId id) {
    const bool saved = gateway_.Mutate(id, [](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.doubleSided = !asset.desc.doubleSided;
    });
    if (!saved) {
        console_.Error("Materials", "Material double-sided flag could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::SetTextureAsset(kb::assets::AssetId id, EditorMaterialTextureSlot slot, kb::assets::AssetId textureId) {
    if (textureId.IsValid()) {
        const kb::assets::AssetMetadata* texture = gateway_.Scene().Assets().Manager().Registry().Find(textureId);
        if (texture == nullptr || !IsTextureAsset(*texture)) {
            console_.Error("Materials", "Material texture slot rejected a non-texture asset.");
            return false;
        }
    }
    const bool saved = gateway_.Mutate(id, [slot, textureId](kb::render::RenderMaterialAssetData& asset) {
        TextureSlot(asset, slot) = textureId.value;
    });
    if (!saved) {
        console_.Error("Materials", "Material texture slot could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::CycleTextureAsset(kb::assets::AssetId id, EditorMaterialTextureSlot slot) {
    const std::optional<kb::render::RenderMaterialAssetData> material = Read(id);
    if (!material.has_value()) {
        console_.Error("Materials", "Material texture slot could not be read.");
        return false;
    }

    std::vector<kb::assets::AssetId> textures;
    for (const kb::assets::AssetMetadata& metadata : gateway_.Scene().Assets().Manager().Registry().All()) {
        if (IsTextureAsset(metadata)) {
            textures.push_back(metadata.id);
        }
    }
    std::ranges::sort(textures, [](kb::assets::AssetId lhs, kb::assets::AssetId rhs) {
        return lhs.value < rhs.value;
    });

    const std::uint64_t current = TextureSlotValue(*material, slot);
    if (textures.empty()) {
        return SetTextureAsset(id, slot, {});
    }
    if (current == 0U) {
        return SetTextureAsset(id, slot, textures.front());
    }
    auto currentIt = std::ranges::find_if(textures, [current](kb::assets::AssetId candidate) {
        return candidate.value == current;
    });
    if (currentIt == textures.end()) {
        return SetTextureAsset(id, slot, {});
    }
    ++currentIt;
    return currentIt == textures.end() ? SetTextureAsset(id, slot, {}) : SetTextureAsset(id, slot, *currentIt);
}

} // namespace kb::editor
