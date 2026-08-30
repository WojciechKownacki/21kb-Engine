#include "engine/assets/bake/RuntimeAssetPack.hpp"

#include "engine/assets/AssetMetadata.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace kb::assets::bake {
namespace {

[[nodiscard]] std::string_view ExpectedArtifactType(RuntimeArtifactEncoding encoding) noexcept {
    switch (encoding) {
    case RuntimeArtifactEncoding::SourceBytes: return kSourceAssetTypeId;
    case RuntimeArtifactEncoding::BakedMesh: return "StaticMesh";
    case RuntimeArtifactEncoding::BakedTexture: return "Texture2D";
    case RuntimeArtifactEncoding::MaterialShader: return kMaterialShaderAssetTypeId;
    }
    return {};
}

[[nodiscard]] bool IsTextureAsset(const RuntimeAssetManifestEntry& asset) noexcept {
    return asset.type == "RenderTexture" || asset.importCategory == "Texture";
}

[[nodiscard]] bool ValidateAssetArtifactShape(
    const RuntimeAssetManifestEntry& asset,
    const BakeTargetProfile& profile) {
    std::size_t sourceCount = 0U;
    std::size_t meshCount = 0U;
    std::size_t textureCount = 0U;
    std::size_t shaderCount = 0U;
    std::set<std::string> textureFamilies;
    for (const RuntimeArtifactReference& reference : asset.artifacts) {
        switch (reference.encoding) {
        case RuntimeArtifactEncoding::SourceBytes:
            ++sourceCount;
            break;
        case RuntimeArtifactEncoding::BakedMesh:
            ++meshCount;
            break;
        case RuntimeArtifactEncoding::BakedTexture:
            ++textureCount;
            textureFamilies.insert(reference.qualifier);
            break;
        case RuntimeArtifactEncoding::MaterialShader:
            ++shaderCount;
            break;
        }
    }
    if (asset.type == "RenderMesh") {
        return meshCount == 1U && asset.artifacts.size() == 1U;
    }
    if (IsTextureAsset(asset)) {
        std::size_t requiredFamilies = 0U;
        for (std::uint32_t index = 0U; index < kTextureCompressionFamilyCount; ++index) {
            const auto family = static_cast<TextureCompressionFamily>(index);
            if (!HasTextureCompressionFamily(profile.textureCompressions, family)) {
                continue;
            }
            ++requiredFamilies;
            if (!textureFamilies.contains(std::string{ TextureCompressionFamilyName(family) })) {
                return false;
            }
        }
        return textureCount == requiredFamilies && asset.artifacts.size() == requiredFamilies;
    }
    if (asset.type == "RenderMaterial") {
        return sourceCount == 1U && meshCount == 0U && textureCount == 0U &&
            asset.artifacts.size() == sourceCount + shaderCount;
    }
    return sourceCount == 1U && asset.artifacts.size() == 1U;
}

} // namespace

std::string_view ToString(RuntimeAssetPackStatus status) noexcept {
    switch (status) {
    case RuntimeAssetPackStatus::Success: return "Success";
    case RuntimeAssetPackStatus::ContainerRejected: return "ContainerRejected";
    case RuntimeAssetPackStatus::ProfileMismatch: return "ProfileMismatch";
    case RuntimeAssetPackStatus::ManifestMissing: return "ManifestMissing";
    case RuntimeAssetPackStatus::ManifestDuplicate: return "ManifestDuplicate";
    case RuntimeAssetPackStatus::ManifestCorrupt: return "ManifestCorrupt";
    case RuntimeAssetPackStatus::ReferenceMissing: return "ReferenceMissing";
    case RuntimeAssetPackStatus::ReferenceTypeMismatch: return "ReferenceTypeMismatch";
    case RuntimeAssetPackStatus::DependencyMissing: return "DependencyMissing";
    case RuntimeAssetPackStatus::OrphanArtifact: return "OrphanArtifact";
    case RuntimeAssetPackStatus::ArtifactCorrupt: return "ArtifactCorrupt";
    case RuntimeAssetPackStatus::SourceCorrupt: return "SourceCorrupt";
    case RuntimeAssetPackStatus::NotMounted: return "NotMounted";
    }
    return "Unknown";
}

RuntimeAssetPackStatus RuntimeAssetPack::Mount(
    const std::filesystem::path& path,
    const BakeTargetProfile& profile,
    AssetPackAccess access) {
    Unmount();
    if (reader_.Mount(path, access) != AssetPackReadStatus::Success) {
        return RuntimeAssetPackStatus::ContainerRejected;
    }
    return FinishMount(profile);
}

RuntimeAssetPackStatus RuntimeAssetPack::MountMemory(
    std::span<const std::uint8_t> bytes,
    const BakeTargetProfile& profile) {
    Unmount();
    if (reader_.MountMemory(bytes) != AssetPackReadStatus::Success) {
        return RuntimeAssetPackStatus::ContainerRejected;
    }
    return FinishMount(profile);
}

RuntimeAssetPackStatus RuntimeAssetPack::FinishMount(const BakeTargetProfile& profile) {
    const auto refuse = [this](RuntimeAssetPackStatus status) {
        Unmount();
        return status;
    };
    if (!reader_.MatchesTargetProfile(profile)) {
        return refuse(RuntimeAssetPackStatus::ProfileMismatch);
    }

    const AssetPackArtifactEntry* manifestArtifact = nullptr;
    for (const AssetPackArtifactEntry& artifact : reader_.Artifacts()) {
        if (artifact.assetTypeId != kRuntimeManifestAssetTypeId) {
            continue;
        }
        if (manifestArtifact != nullptr) {
            return refuse(RuntimeAssetPackStatus::ManifestDuplicate);
        }
        manifestArtifact = &artifact;
    }
    if (manifestArtifact == nullptr) {
        return refuse(RuntimeAssetPackStatus::ManifestMissing);
    }
    if (manifestArtifact->blocks.size() != 1U) {
        return refuse(RuntimeAssetPackStatus::ManifestCorrupt);
    }
    std::vector<std::uint8_t> manifestBytes;
    if (reader_.ReadBlock(*manifestArtifact, kBakedAssetPrimaryBlockName, manifestBytes) !=
        AssetPackReadStatus::Success) {
        return refuse(RuntimeAssetPackStatus::ManifestCorrupt);
    }
    RuntimeAssetManifest manifest{};
    if (DecodeRuntimeAssetManifest(manifestBytes, manifest) != RuntimeAssetManifestStatus::Success ||
        manifest.targetProfileId != reader_.Header().targetProfileId ||
        manifest.targetProfileHash != reader_.Header().targetProfileHash) {
        return refuse(RuntimeAssetPackStatus::ManifestCorrupt);
    }

    std::map<AssetBakeDigest, const AssetPackArtifactEntry*> artifactsByDigest;
    for (const AssetPackArtifactEntry& artifact : reader_.Artifacts()) {
        artifactsByDigest.emplace(artifact.key, &artifact);
    }
    std::set<AssetBakeDigest> referenced;
    std::set<std::uint64_t> assetIds;
    for (const RuntimeAssetManifestEntry& asset : manifest.assets) {
        assetIds.insert(asset.id.value);
        if (!ValidateAssetArtifactShape(asset, profile)) {
            return refuse(RuntimeAssetPackStatus::ReferenceTypeMismatch);
        }
        for (const RuntimeArtifactReference& reference : asset.artifacts) {
            const auto artifact = artifactsByDigest.find(reference.digest);
            if (artifact == artifactsByDigest.end()) {
                return refuse(RuntimeAssetPackStatus::ReferenceMissing);
            }
            if (artifact->second->assetTypeId != ExpectedArtifactType(reference.encoding)) {
                return refuse(RuntimeAssetPackStatus::ReferenceTypeMismatch);
            }
            referenced.insert(reference.digest);
        }
    }
    for (const RuntimeAssetManifestEntry& asset : manifest.assets) {
        for (const AssetId dependency : asset.dependencies) {
            if (!assetIds.contains(dependency.value)) {
                return refuse(RuntimeAssetPackStatus::DependencyMissing);
            }
        }
    }
    const auto defaultMap = std::ranges::find(
        manifest.assets, manifest.settings.defaultMap, &RuntimeAssetManifestEntry::virtualPath);
    if (defaultMap == manifest.assets.end()) {
        return refuse(RuntimeAssetPackStatus::DependencyMissing);
    }
    if (defaultMap->type != "Scene" || !defaultMap->runtimeLoadable) {
        return refuse(RuntimeAssetPackStatus::ReferenceTypeMismatch);
    }
    for (const RuntimeAuxiliaryFileEntry& file : manifest.auxiliaryFiles) {
        const auto artifact = artifactsByDigest.find(file.artifactDigest);
        if (artifact == artifactsByDigest.end()) {
            return refuse(RuntimeAssetPackStatus::ReferenceMissing);
        }
        if (artifact->second->assetTypeId != kSourceAssetTypeId) {
            return refuse(RuntimeAssetPackStatus::ReferenceTypeMismatch);
        }
        referenced.insert(file.artifactDigest);
    }
    for (const AssetPackArtifactEntry& artifact : reader_.Artifacts()) {
        if (&artifact == manifestArtifact) {
            continue;
        }
        if (!referenced.contains(artifact.key)) {
            return refuse(RuntimeAssetPackStatus::OrphanArtifact);
        }
    }

    manifest_ = std::move(manifest);
    mounted_ = true;
    return RuntimeAssetPackStatus::Success;
}

void RuntimeAssetPack::Unmount() noexcept {
    reader_.Unmount();
    manifest_ = RuntimeAssetManifest{};
    mounted_ = false;
}

bool RuntimeAssetPack::IsMounted() const noexcept {
    return mounted_;
}

const RuntimeAssetManifest& RuntimeAssetPack::Manifest() const noexcept {
    return manifest_;
}

const RuntimeAssetManifestEntry* RuntimeAssetPack::FindAsset(AssetId id) const noexcept {
    if (!mounted_) {
        return nullptr;
    }
    const auto found = std::ranges::find(manifest_.assets, id, &RuntimeAssetManifestEntry::id);
    return found == manifest_.assets.end() ? nullptr : &*found;
}

const RuntimeAssetManifestEntry* RuntimeAssetPack::FindAsset(std::string_view virtualPath) const noexcept {
    if (!mounted_) {
        return nullptr;
    }
    const auto found = std::ranges::find_if(manifest_.assets, [virtualPath](const RuntimeAssetManifestEntry& asset) {
        return asset.virtualPath == virtualPath;
    });
    return found == manifest_.assets.end() ? nullptr : &*found;
}

AssetPackReadStatus RuntimeAssetPack::ReadArtifactBlock(
    const AssetBakeDigest& digest,
    std::string_view blockName,
    std::vector<std::uint8_t>& out) {
    out.clear();
    if (!mounted_) {
        return AssetPackReadStatus::NotMounted;
    }
    std::scoped_lock lock{ readMutex_ };
    const AssetPackArtifactEntry* artifact = reader_.FindArtifact(digest);
    return artifact == nullptr
        ? AssetPackReadStatus::ArtifactNotFound
        : reader_.ReadBlock(*artifact, blockName, out);
}

RuntimeAssetPackStatus RuntimeAssetPack::ReadSourceFile(
    const AssetBakeDigest& digest,
    std::vector<std::uint8_t>& out) {
    out.clear();
    if (!mounted_) {
        return RuntimeAssetPackStatus::NotMounted;
    }
    std::scoped_lock lock{ readMutex_ };
    const AssetPackArtifactEntry* artifact = reader_.FindArtifact(digest);
    if (artifact == nullptr) {
        return RuntimeAssetPackStatus::ReferenceMissing;
    }
    if (artifact->assetTypeId != kSourceAssetTypeId) {
        return RuntimeAssetPackStatus::ReferenceTypeMismatch;
    }
    std::vector<std::uint8_t> blob;
    if (reader_.ReadBlock(*artifact, kBakedAssetPrimaryBlockName, blob) != AssetPackReadStatus::Success) {
        return RuntimeAssetPackStatus::SourceCorrupt;
    }
    std::span<const std::uint8_t> source;
    if (!DecodeRuntimeSourceBlob(blob, source)) {
        return RuntimeAssetPackStatus::SourceCorrupt;
    }
    out.assign(source.begin(), source.end());
    return RuntimeAssetPackStatus::Success;
}

RuntimeAssetPackStatus RuntimeAssetPack::ReadAssetPayload(
    AssetId assetId,
    RuntimeArtifactEncoding encoding,
    std::string_view qualifier,
    RuntimeAssetPayload& out) {
    if (!mounted_) {
        return RuntimeAssetPackStatus::NotMounted;
    }
    const RuntimeAssetManifestEntry* asset = FindAsset(assetId);
    if (asset == nullptr) {
        return RuntimeAssetPackStatus::ReferenceMissing;
    }
    const auto reference = std::ranges::find_if(
        asset->artifacts,
        [encoding, qualifier](const RuntimeArtifactReference& candidate) {
            return candidate.encoding == encoding && candidate.qualifier == qualifier;
        });
    if (reference == asset->artifacts.end()) {
        return RuntimeAssetPackStatus::ReferenceMissing;
    }

    std::scoped_lock lock{ readMutex_ };
    const AssetPackArtifactEntry* artifact = reader_.FindArtifact(reference->digest);
    if (artifact == nullptr || artifact->assetTypeId != ExpectedArtifactType(encoding)) {
        return artifact == nullptr
            ? RuntimeAssetPackStatus::ReferenceMissing
            : RuntimeAssetPackStatus::ReferenceTypeMismatch;
    }
    RuntimeAssetPayload payload{
        .digest = reference->digest,
        .encoding = encoding,
        .qualifier = reference->qualifier,
    };
    payload.blocks.reserve(artifact->blocks.size());
    for (const AssetPackBlockEntry& indexedBlock : artifact->blocks) {
        RuntimeAssetPayloadBlock block{
            .name = indexedBlock.name,
            .residency = indexedBlock.residency,
            .alignmentBytes = indexedBlock.alignmentBytes,
        };
        const auto fragment = std::ranges::find_if(
            reader_.Fragments(),
            [&indexedBlock](const AssetPackFragmentEntry& candidate) {
                return candidate.offset == indexedBlock.offset &&
                    candidate.bytes == indexedBlock.storedBytes;
            });
        if (fragment != reader_.Fragments().end()) {
            block.fragment = BakedAssetBlockFragment{
                .boundsMin = fragment->boundsMin,
                .boundsMax = fragment->boundsMax,
                .clusterCount = fragment->clusterCount,
            };
        }
        if (reader_.ReadBlock(*artifact, indexedBlock.name, block.bytes) !=
            AssetPackReadStatus::Success) {
            return RuntimeAssetPackStatus::ArtifactCorrupt;
        }
        if (encoding == RuntimeArtifactEncoding::SourceBytes) {
            std::span<const std::uint8_t> source;
            if (artifact->blocks.size() != 1U ||
                !DecodeRuntimeSourceBlob(block.bytes, source)) {
                return RuntimeAssetPackStatus::SourceCorrupt;
            }
            if (HashBakeBytes(source) != asset->contentHash) {
                return RuntimeAssetPackStatus::SourceCorrupt;
            }
            block.bytes.assign(source.begin(), source.end());
        }
        payload.blocks.push_back(std::move(block));
    }
    out = std::move(payload);
    return RuntimeAssetPackStatus::Success;
}

RuntimeAssetPackStatus RuntimeAssetPack::ReadAuxiliaryFile(
    std::string_view virtualPath,
    std::vector<std::uint8_t>& out) {
    out.clear();
    if (!mounted_) {
        return RuntimeAssetPackStatus::NotMounted;
    }
    const auto file = std::ranges::find(
        manifest_.auxiliaryFiles,
        virtualPath,
        &RuntimeAuxiliaryFileEntry::virtualPath);
    if (file == manifest_.auxiliaryFiles.end()) {
        return RuntimeAssetPackStatus::ReferenceMissing;
    }
    const RuntimeAssetPackStatus status = ReadSourceFile(file->artifactDigest, out);
    if (status != RuntimeAssetPackStatus::Success) {
        return status;
    }
    if (HashBakeBytes(out) != file->contentHash) {
        out.clear();
        return RuntimeAssetPackStatus::SourceCorrupt;
    }
    return RuntimeAssetPackStatus::Success;
}

const AssetPackHeader& RuntimeAssetPack::Header() const noexcept {
    return reader_.Header();
}

std::span<const AssetPackArtifactEntry> RuntimeAssetPack::Artifacts() const noexcept {
    return reader_.Artifacts();
}

} // namespace kb::assets::bake
