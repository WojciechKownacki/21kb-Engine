#include "scene/material/EditorMaterialAssetGateway.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "kb/render/resources/RenderMaterialTypeAssetLoader.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace kb::editor {
namespace {

constexpr std::string_view kMaterialExtension = ".kbmat";

void UpsertGraphParameterValue(
    kb::render::RenderMaterialAssetData& material,
    std::string_view stableId,
    kb::render::RenderMaterialParameterType type,
    std::array<float, 4U> numbers) {
    const auto existing = std::ranges::find_if(material.graphParameterValues, [stableId](const kb::render::RenderMaterialGraphParameterValue& value) {
        return value.stableId == stableId;
    });
    kb::render::RenderMaterialGraphParameterValue value{
        .stableId = std::string{ stableId },
        .type = type,
        .numbers = numbers,
    };
    if (existing == material.graphParameterValues.end()) {
        material.graphParameterValues.push_back(std::move(value));
    } else {
        *existing = std::move(value);
    }
}

void SyncBuiltInGraphParameterOverrides(kb::render::RenderMaterialAssetData& material) {
    for (const kb::render::RenderMaterialGraphNode& node : material.graph.nodes) {
        const std::string& stableId = node.parameter.stableId;
        if (node.kind == kb::render::RenderMaterialGraphNodeKind::ParameterColor &&
            (stableId == "baseColor" || stableId == "baseColorFactor")) {
            UpsertGraphParameterValue(material, stableId, kb::render::RenderMaterialParameterType::Color,
                { material.desc.baseColor[0], material.desc.baseColor[1], material.desc.baseColor[2], material.desc.baseColor[3] });
        } else if (node.kind == kb::render::RenderMaterialGraphNodeKind::ParameterScalar && stableId == "roughnessFactor") {
            UpsertGraphParameterValue(material, stableId, kb::render::RenderMaterialParameterType::Scalar,
                { material.desc.roughnessFactor, 0.0F, 0.0F, 0.0F });
        } else if (node.kind == kb::render::RenderMaterialGraphNodeKind::ParameterScalar && stableId == "metallicFactor") {
            UpsertGraphParameterValue(material, stableId, kb::render::RenderMaterialParameterType::Scalar,
                { material.desc.metallicFactor, 0.0F, 0.0F, 0.0F });
        }
    }
}

[[nodiscard]] std::filesystem::path MaterialGatewayDebugLogPath() {
    return std::filesystem::temp_directory_path() / "_material_graph_debug.log";
}

void WriteMaterialGatewayDebugLog(std::string_view message) {
    try {
        std::ofstream output{ MaterialGatewayDebugLogPath(), std::ios::out | std::ios::app };
        if (output.is_open()) {
            output << "[MaterialGraph] " << message << '\n';
        }
#if defined(_WIN32)
        std::string debugLine{ "[MaterialGraph] " };
        debugLine += message;
        debugLine.push_back('\n');
        OutputDebugStringA(debugLine.c_str());
#endif
    } catch (...) {
    }
}

[[nodiscard]] bool GraphOutputHasNormalLink(const kb::render::RenderMaterialAssetData& asset) noexcept {
    for (const kb::render::RenderMaterialGraphLink& link : asset.graph.links) {
        if (link.toNodeId == 1U && link.toPin == "normal") {
            return true;
        }
    }
    return false;
}

} // namespace

EditorMaterialAssetGateway::EditorMaterialAssetGateway(kb::scene::Scene& scene, EditorAssetBrowserState& browser) noexcept
    : scene_(scene)
    , browser_(browser) {}

kb::scene::Scene& EditorMaterialAssetGateway::Scene() const noexcept {
    return scene_;
}

std::optional<std::filesystem::path> EditorMaterialAssetGateway::ResolveFolder(const std::filesystem::path& virtualFolder) const {
    if (virtualFolder.empty()) {
        return std::nullopt;
    }
    const std::optional<std::filesystem::path> probe = scene_.Assets().Manager().Mounts().Resolve(virtualFolder / "probe");
    return probe.has_value() ? std::optional<std::filesystem::path>{ probe->parent_path() } : std::nullopt;
}

std::filesystem::path EditorMaterialAssetGateway::UniqueFilePath(const std::filesystem::path& folder, std::string_view baseName) {
    return UniqueFilePath(folder, baseName, kMaterialExtension);
}

std::filesystem::path EditorMaterialAssetGateway::UniqueFilePath(const std::filesystem::path& folder, std::string_view baseName, std::string_view extension) {
    std::filesystem::path candidate = folder / (std::string{ baseName } + std::string{ extension });
    int suffix = 1;
    while (std::filesystem::exists(candidate)) {
        candidate = folder / (std::string{ baseName } + std::to_string(suffix) + std::string{ extension });
        ++suffix;
    }
    return candidate;
}

std::optional<std::filesystem::path> EditorMaterialAssetGateway::ResolveFile(const kb::scene::Scene& scene, kb::assets::AssetId id) {
    const kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr || metadata->type != "RenderMaterial") {
        return std::nullopt;
    }
    if (!metadata->physicalPath.empty()) {
        return metadata->physicalPath;
    }
    return manager.Mounts().Resolve(metadata->virtualPath);
}

std::optional<std::filesystem::path> EditorMaterialAssetGateway::ResolveInstanceFile(const kb::scene::Scene& scene, kb::assets::AssetId id) {
    const kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr || metadata->type != "RenderMaterialInstance") {
        return std::nullopt;
    }
    if (!metadata->physicalPath.empty()) {
        return metadata->physicalPath;
    }
    return manager.Mounts().Resolve(metadata->virtualPath);
}

std::optional<kb::render::RenderMaterialAssetData> EditorMaterialAssetGateway::Read(const kb::scene::Scene& scene, kb::assets::AssetId id) {
    const std::optional<std::filesystem::path> path = ResolveFile(scene, id);
    return path.has_value() ? kb::render::RenderMaterialAssetLoader::LoadMaterial(*path) : std::nullopt;
}

std::optional<kb::render::RenderMaterialInstanceAssetData> EditorMaterialAssetGateway::ReadInstance(const kb::scene::Scene& scene, kb::assets::AssetId id) {
    const std::optional<std::filesystem::path> path = ResolveInstanceFile(scene, id);
    return path.has_value() ? kb::render::RenderMaterialInstanceAssetLoader::LoadInstance(*path) : std::nullopt;
}

bool EditorMaterialAssetGateway::WriteExisting(kb::scene::Scene& scene, kb::assets::AssetId id, const kb::render::RenderMaterialAssetData& asset) {
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(id);
    const std::optional<std::filesystem::path> path = ResolveFile(scene, id);
    if (!path.has_value()) {
        std::ostringstream row;
        row << "gateway-write-material-failed-resolve asset=" << id.value;
        if (metadata != nullptr) {
            row << " type=" << metadata->type
                << " virtualPath=" << metadata->virtualPath.generic_string()
                << " physicalPath=" << metadata->physicalPath.generic_string();
        }
        WriteMaterialGatewayDebugLog(row.str());
        return false;
    }
    {
        std::ostringstream row;
        row << "gateway-write-material asset=" << id.value
            << " path=" << path->generic_string()
            << " nodes=" << asset.graph.nodes.size()
            << " links=" << asset.graph.links.size()
            << " outputNormalLinked=" << (GraphOutputHasNormalLink(asset) ? "true" : "false");
        if (metadata != nullptr) {
            row << " type=" << metadata->type
                << " virtualPath=" << metadata->virtualPath.generic_string()
                << " physicalPath=" << metadata->physicalPath.generic_string();
        }
        WriteMaterialGatewayDebugLog(row.str());
    }
    kb::render::RenderMaterialAssetData materialToWrite = asset;
    SyncBuiltInGraphParameterOverrides(materialToWrite);
    const auto resolveReferencedAsset = [&scene](std::uint64_t assetId, const std::string& virtualPath, std::string_view type) {
        const kb::assets::AssetManager& manager = scene.Assets().Manager();
        const kb::assets::AssetMetadata* referenced = assetId != 0U
            ? manager.Registry().Find(kb::assets::AssetId{ assetId })
            : nullptr;
        if ((referenced == nullptr || referenced->type != type) && !virtualPath.empty()) {
            referenced = manager.Registry().FindByPath(std::filesystem::path{ virtualPath });
        }
        return referenced != nullptr && referenced->type == type ? referenced : nullptr;
    };
    const auto physicalPath = [&scene](const kb::assets::AssetMetadata& referenced) {
        return referenced.physicalPath.empty()
            ? scene.Assets().Manager().Mounts().Resolve(referenced.virtualPath).value_or(std::filesystem::path{})
            : referenced.physicalPath;
    };
    if (materialToWrite.graphSourceAssetId != 0U || !materialToWrite.graphSourceAssetPath.empty()) {
        const kb::assets::AssetMetadata* sourceGraph = resolveReferencedAsset(
            materialToWrite.graphSourceAssetId,
            materialToWrite.graphSourceAssetPath,
            kb::render::kRenderMaterialGraphAssetType);
        const std::filesystem::path sourceGraphPath = sourceGraph != nullptr ? physicalPath(*sourceGraph) : std::filesystem::path{};
        if (sourceGraph == nullptr || sourceGraphPath.empty() ||
            !kb::render::RenderMaterialGraphAssetLoader::SaveGraph(sourceGraphPath, materialToWrite.graph)) {
            WriteMaterialGatewayDebugLog("gateway-write-material-source-graph-save-failed asset=" + std::to_string(id.value));
            return false;
        }
        materialToWrite.graph.storageModel = "material-graph-asset";

        const kb::assets::AssetMetadata* materialType = resolveReferencedAsset(
            materialToWrite.materialTypeAssetId,
            materialToWrite.materialTypeAssetPath,
            kb::render::kRenderMaterialTypeAssetType);
        if (materialType != nullptr) {
            const kb::render::RenderMaterialGraphMaterialTypeBuildResult generatedType =
                kb::render::BuildRenderMaterialGraphMaterialTypeDocument(
                    materialToWrite.graph,
                    materialToWrite.materialType,
                    materialToWrite.materialTypeVersion == 0U ? 1U : materialToWrite.materialTypeVersion,
                    kb::render::RenderMaterialGraphBuildContext{
                        .assetId = sourceGraph->id.value,
                        .sourcePath = sourceGraph->virtualPath.generic_string(),
                    });
            const std::filesystem::path materialTypePath = physicalPath(*materialType);
            if (!generatedType.Succeeded() || !generatedType.document.has_value() || materialTypePath.empty() ||
                !kb::render::RenderMaterialTypeAssetLoader::SaveType(materialTypePath, *generatedType.document)) {
                WriteMaterialGatewayDebugLog("gateway-write-material-type-regeneration-failed asset=" + std::to_string(id.value));
                return false;
            }
        }
    }
    if (!kb::render::RenderMaterialAssetWriter::Save(*path, materialToWrite)) {
        WriteMaterialGatewayDebugLog("gateway-write-material-save-failed asset=" + std::to_string(id.value) + " path=" + path->generic_string());
        return false;
    }
    {
        std::ostringstream row;
        row << "gateway-write-material-save-ok asset=" << id.value
            << " path=" << path->generic_string()
            << " exists=" << (std::filesystem::exists(*path) ? "true" : "false");
        WriteMaterialGatewayDebugLog(row.str());
    }
    static_cast<void>(scene.Assets().Manager().Unload(id));
    static_cast<void>(scene.Assets().Discover());
    return true;
}

bool EditorMaterialAssetGateway::WriteExistingInstance(kb::scene::Scene& scene, kb::assets::AssetId id, const kb::render::RenderMaterialInstanceAssetData& asset) {
    const std::optional<std::filesystem::path> path = ResolveInstanceFile(scene, id);
    if (!path.has_value()) {
        return false;
    }
    if (!kb::render::RenderMaterialInstanceAssetWriter::Save(*path, asset)) {
        return false;
    }
    static_cast<void>(scene.Assets().Manager().Unload(id));
    static_cast<void>(scene.Assets().Discover());
    return true;
}

void EditorMaterialAssetGateway::EnsureMaterialLoader() {
    static_cast<void>(scene_.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()));
}

void EditorMaterialAssetGateway::EnsureMaterialFunctionLoader() {
    static_cast<void>(scene_.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialFunctionAssetLoader>()));
}

void EditorMaterialAssetGateway::EnsureMaterialGraphLoader() {
    static_cast<void>(scene_.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialGraphAssetLoader>()));
}

void EditorMaterialAssetGateway::EnsureMaterialInstanceLoader() {
    static_cast<void>(scene_.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialInstanceAssetLoader>()));
}

void EditorMaterialAssetGateway::EnsureMaterialParameterCollectionLoader() {
    static_cast<void>(scene_.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialParameterCollectionAssetLoader>()));
}

void EditorMaterialAssetGateway::EnsureMaterialTypeLoader() {
    static_cast<void>(scene_.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialTypeAssetLoader>()));
}

void EditorMaterialAssetGateway::DiscoverAndSelect(const std::filesystem::path& path) {
    EnsureMaterialLoader();
    EnsureMaterialFunctionLoader();
    EnsureMaterialGraphLoader();
    EnsureMaterialInstanceLoader();
    EnsureMaterialParameterCollectionLoader();
    EnsureMaterialTypeLoader();
    kb::assets::AssetManager& manager = scene_.Assets().Manager();
    static_cast<void>(scene_.Assets().Discover());
    if (const std::optional<std::filesystem::path> created = manager.Mounts().ToVirtual(path)) {
        if (const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(*created); metadata != nullptr) {
            static_cast<void>(browser_.SelectAsset(metadata->id, manager));
        }
    }
}

bool EditorMaterialAssetGateway::WriteNewMaterial(const std::filesystem::path& path, const kb::render::RenderMaterialAssetData& asset) {
    if (!kb::render::RenderMaterialAssetWriter::Save(path, asset)) {
        return false;
    }
    DiscoverAndSelect(path);
    return true;
}

bool EditorMaterialAssetGateway::WriteNewMaterialFunction(const std::filesystem::path& path, const kb::render::RenderMaterialFunctionAssetData& asset) {
    if (!kb::render::RenderMaterialFunctionAssetLoader::SaveFunction(path, asset)) {
        return false;
    }
    DiscoverAndSelect(path);
    return true;
}

bool EditorMaterialAssetGateway::WriteNewMaterialGraph(const std::filesystem::path& path, const kb::render::RenderMaterialGraphDocument& graph) {
    if (!kb::render::RenderMaterialGraphAssetLoader::SaveGraph(path, graph)) {
        return false;
    }
    DiscoverAndSelect(path);
    return true;
}

bool EditorMaterialAssetGateway::WriteNewMaterialInstance(const std::filesystem::path& path, const kb::render::RenderMaterialInstanceAssetData& asset) {
    if (!kb::render::RenderMaterialInstanceAssetWriter::Save(path, asset)) {
        return false;
    }
    DiscoverAndSelect(path);
    return true;
}

bool EditorMaterialAssetGateway::WriteNewMaterialType(const std::filesystem::path& path, const kb::render::RenderMaterialTypeDocument& document) {
    if (!kb::render::RenderMaterialTypeAssetLoader::SaveType(path, document)) {
        return false;
    }
    DiscoverAndSelect(path);
    return true;
}

std::optional<std::filesystem::path> EditorMaterialAssetGateway::DuplicateMaterial(kb::assets::AssetId id) {
    const kb::assets::AssetMetadata* metadata = scene_.Assets().Manager().Registry().Find(id);
    if (metadata == nullptr || metadata->type != "RenderMaterial") {
        return std::nullopt;
    }

    const std::optional<std::filesystem::path> sourcePath = ResolveFile(scene_, id);
    if (!sourcePath.has_value()) {
        return std::nullopt;
    }

    const std::optional<kb::render::RenderMaterialAssetData> material = kb::render::RenderMaterialAssetLoader::LoadMaterial(*sourcePath);
    if (!material.has_value()) {
        return std::nullopt;
    }

    const std::filesystem::path duplicatePath = UniqueFilePath(sourcePath->parent_path(), sourcePath->stem().string() + std::string{ "Copy" });
    if (!WriteNewMaterial(duplicatePath, *material)) {
        return std::nullopt;
    }
    return duplicatePath;
}

bool EditorMaterialAssetGateway::Mutate(kb::assets::AssetId id, const std::function<void(kb::render::RenderMaterialAssetData&)>& mutate) {
    std::optional<kb::render::RenderMaterialAssetData> asset = Read(scene_, id);
    if (!asset.has_value()) {
        return false;
    }
    mutate(*asset);
    return WriteExisting(scene_, id, *asset);
}

} // namespace kb::editor
