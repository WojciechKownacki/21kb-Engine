#include "world/WorldAssetLoaders.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/navigation/NavMeshAsset.hpp"
#include "engine/world/WorldCellIndex.hpp"
#include "engine/world/WorldDescriptor.hpp"
#include "world/WorldTextFormat.hpp"

#include <algorithm>
#include <memory>

namespace kb::world {
namespace {

[[nodiscard]] bool ReadText(const kb::assets::AssetLoadRequest& request, std::string& output, std::string& error) {
    std::vector<std::uint8_t> bytes;
    if (!request.ReadSourceBytes(bytes, error)) {
        return false;
    }
    output.assign(bytes.begin(), bytes.end());
    return true;
}

void AppendUnique(std::vector<kb::assets::AssetId>& ids, kb::assets::AssetId id) {
    if (id.IsValid() && std::ranges::none_of(ids, [id](kb::assets::AssetId existing) { return existing.value == id.value; })) {
        ids.push_back(id);
    }
}

struct IndexReferences {
    std::vector<kb::assets::AssetId> ids;
    std::string missing;
};

[[nodiscard]] IndexReferences ResolveIndex(const WorldCellIndex& index, const std::string& indexVirtualPath, const kb::assets::AssetRegistry& registry) {
    IndexReferences result;
    // Cell scenes are required. An HLOD mesh is only a distant stand-in: a host
    // without mesh loaders (a headless tool) streams the cells without it.
    const auto resolve = [&](std::string_view relative, std::string_view type, bool required) {
        const std::string path = ResolveWorldCellPath(indexVirtualPath, relative);
        const kb::assets::AssetMetadata* metadata = registry.FindByPath(path);
        if (metadata == nullptr || metadata->type != type) {
            if (required && result.missing.empty()) {
                result.missing = "references " + std::string{ type } + " " + path + " which is not a registered asset";
            }
            return;
        }
        AppendUnique(result.ids, metadata->id);
    };
    for (const WorldCellUnit& unit : index.units) {
        resolve(unit.scene, "Scene", true);
    }
    for (const WorldCellNavMesh& navMesh : index.navMeshes) {
        resolve(navMesh.mesh, kb::navigation::NavMeshAsset::AssetType, true);
    }
    for (const WorldCellHlod& hlod : index.hlods) {
        resolve(hlod.mesh, "RenderMesh", false);
        for (const std::uint64_t material : hlod.materials) {
            if (material != 0U && registry.Find(kb::assets::AssetId{ material }) != nullptr) {
                AppendUnique(result.ids, kb::assets::AssetId{ material });
            }
        }
    }
    return result;
}

} // namespace

std::string_view WorldDescriptorAssetLoader::Type() const noexcept {
    return WorldDescriptor::AssetType;
}

std::type_index WorldDescriptorAssetLoader::PayloadType() const noexcept {
    return typeid(WorldDescriptor);
}

std::vector<std::string> WorldDescriptorAssetLoader::Extensions() const {
    return { std::string{ WorldDescriptor::Extension } };
}

kb::assets::AssetLoadResult WorldDescriptorAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::string source;
    std::string error;
    if (!ReadText(request, source, error)) {
        return { .asset = {}, .error = std::move(error) };
    }
    WorldDescriptorReadResult parsed = WorldDescriptorIO::Parse(source);
    if (!parsed.succeeded) {
        return { .asset = {}, .error = std::move(parsed.error) };
    }
    return { .asset = std::make_shared<WorldDescriptor>(std::move(parsed.descriptor)), .error = {} };
}

std::vector<kb::assets::AssetId> WorldDescriptorAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const {
    const kb::assets::AssetMetadata* index = registry.FindByPath(WorldPaths::CellIndexVirtualPath(metadata.virtualPath.generic_string()));
    if (index == nullptr || index->type != WorldCellIndex::AssetType) {
        return {};
    }
    return { index->id };
}

std::string_view WorldCellIndexAssetLoader::Type() const noexcept {
    return WorldCellIndex::AssetType;
}

std::type_index WorldCellIndexAssetLoader::PayloadType() const noexcept {
    return typeid(WorldCellIndex);
}

std::vector<std::string> WorldCellIndexAssetLoader::Extensions() const {
    return { std::string{ WorldCellIndex::Extension } };
}

kb::assets::AssetLoadResult WorldCellIndexAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::string source;
    std::string error;
    if (!ReadText(request, source, error)) {
        return { .asset = {}, .error = std::move(error) };
    }
    WorldCellIndexReadResult parsed = WorldCellIndexIO::Parse(source);
    if (!parsed.succeeded) {
        return { .asset = {}, .error = std::move(parsed.error) };
    }
    return { .asset = std::make_shared<WorldCellIndex>(std::move(parsed.index)), .error = {} };
}

std::vector<kb::assets::AssetId> WorldCellIndexAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const {
    const WorldCellIndexReadResult index = WorldCellIndexIO::Read(metadata.physicalPath);
    if (!index.succeeded) {
        return {};
    }
    return ResolveIndex(index.index, metadata.virtualPath.generic_string(), registry).ids;
}

std::optional<std::string> WorldCellIndexAssetLoader::ValidateDependencies(
    const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const {
    const WorldCellIndexReadResult index = WorldCellIndexIO::Read(metadata.physicalPath);
    if (!index.succeeded) {
        return index.error;
    }
    IndexReferences references = ResolveIndex(index.index, metadata.virtualPath.generic_string(), registry);
    if (!references.missing.empty()) {
        return std::move(references.missing);
    }
    return std::nullopt;
}

std::optional<std::string> WorldCellIndexAssetLoader::ValidateRuntimeDependencies(
    const kb::assets::AssetLoadRequest& request, const kb::assets::AssetRegistry& registry) const {
    std::string source;
    std::string error;
    if (!ReadText(request, source, error)) {
        return error;
    }
    const WorldCellIndexReadResult index = WorldCellIndexIO::Parse(source);
    if (!index.succeeded) {
        return index.error;
    }
    IndexReferences references = ResolveIndex(index.index, request.metadata.virtualPath.generic_string(), registry);
    if (!references.missing.empty()) {
        return std::move(references.missing);
    }
    return std::nullopt;
}

} // namespace kb::world
