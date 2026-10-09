#include "engine/assets/bake/RuntimeAssetPack.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/TerrainAsset.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
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
        if (asset.sourceExtension == kTerrainAssetExtension) {
            return sourceCount == 1U && asset.artifacts.size() == 1U;
        }
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
    case RuntimeAssetPackStatus::PackSetInvalid: return "PackSetInvalid";
    case RuntimeAssetPackStatus::PackSetBaseMismatch: return "PackSetBaseMismatch";
    }
    return "Unknown";
}

RuntimeAssetPackStatus RuntimeAssetPack::Mount(
    const std::filesystem::path& path,
    const BakeTargetProfile& profile,
    AssetPackAccess access,
    const AssetPackTrust& trust) {
    const std::array<RuntimeAssetPackMount, 1U> base{ RuntimeAssetPackMount{ .path = path } };
    return MountSet(base, profile, access, trust);
}

RuntimeAssetPackStatus RuntimeAssetPack::MountMemory(
    std::span<const std::uint8_t> bytes,
    const BakeTargetProfile& profile,
    const AssetPackTrust& trust) {
    Unmount();
    refusedContainer_ = 0U;
    Container container{
        .reader = std::make_unique<AssetPackReader>(),
        .readMutex = std::make_unique<std::mutex>(),
    };
    containerStatus_ = container.reader->MountMemory(bytes, trust);
    if (containerStatus_ != AssetPackReadStatus::Success) {
        return RuntimeAssetPackStatus::ContainerRejected;
    }
    containers_.push_back(std::move(container));
    const std::array<RuntimeAssetPackMount, 1U> base{ RuntimeAssetPackMount{} };
    return FinishMount(base, profile);
}

RuntimeAssetPackStatus RuntimeAssetPack::MountSet(
    std::span<const RuntimeAssetPackMount> packs,
    const BakeTargetProfile& profile,
    AssetPackAccess access,
    const AssetPackTrust& trust) {
    Unmount();
    refusedContainer_ = 0U;
    if (packs.empty() || packs.size() > kMaxAssetPackSetPacks) {
        return RuntimeAssetPackStatus::PackSetInvalid;
    }
    containers_.reserve(packs.size());
    for (std::size_t index = 0U; index < packs.size(); ++index) {
        Container container{
            .path = packs[index].path,
            .reader = std::make_unique<AssetPackReader>(),
            .readMutex = std::make_unique<std::mutex>(),
        };
        containerStatus_ = container.reader->Mount(packs[index].path, access, trust);
        if (containerStatus_ != AssetPackReadStatus::Success) {
            Unmount();
            refusedContainer_ = static_cast<std::uint32_t>(index);
            return RuntimeAssetPackStatus::ContainerRejected;
        }
        containers_.push_back(std::move(container));
    }
    return FinishMount(packs, profile);
}

RuntimeAssetPackStatus RuntimeAssetPack::MountSetIndex(
    const std::filesystem::path& indexPath,
    const BakeTargetProfile& profile,
    AssetPackAccess access,
    const AssetPackTrust& trust) {
    Unmount();
    refusedContainer_ = 0U;
    AssetPackSetIndex index{};
    if (ReadAssetPackSetIndex(indexPath, index) != AssetPackSetStatus::Success) {
        return RuntimeAssetPackStatus::PackSetInvalid;
    }
    std::vector<RuntimeAssetPackMount> packs;
    packs.reserve(index.packs.size());
    for (const AssetPackSetEntry& entry : index.packs) {
        packs.push_back(RuntimeAssetPackMount{
            .path = ResolveAssetPackSetPath(indexPath, entry.path),
            .role = entry.role,
            .label = entry.label,
            .patchLevel = entry.patchLevel,
        });
    }
    return MountSet(packs, profile, access, trust);
}

RuntimeAssetPackStatus RuntimeAssetPack::ValidateContainer(Container& container, const BakeTargetProfile& profile) {
    AssetPackReader& reader = *container.reader;
    if (!reader.MatchesTargetProfile(profile)) {
        return RuntimeAssetPackStatus::ProfileMismatch;
    }

    const AssetPackArtifactEntry* manifestArtifact = nullptr;
    for (const AssetPackArtifactEntry& artifact : reader.Artifacts()) {
        if (artifact.assetTypeId != kRuntimeManifestAssetTypeId) {
            continue;
        }
        if (manifestArtifact != nullptr) {
            return RuntimeAssetPackStatus::ManifestDuplicate;
        }
        manifestArtifact = &artifact;
    }
    if (manifestArtifact == nullptr) {
        return RuntimeAssetPackStatus::ManifestMissing;
    }
    if (manifestArtifact->blocks.size() != 1U) {
        return RuntimeAssetPackStatus::ManifestCorrupt;
    }
    std::vector<std::uint8_t> manifestBytes;
    if (reader.ReadBlock(*manifestArtifact, kBakedAssetPrimaryBlockName, manifestBytes) !=
        AssetPackReadStatus::Success) {
        return RuntimeAssetPackStatus::ManifestCorrupt;
    }
    RuntimeAssetManifest manifest{};
    // A base pack's manifest is complete; a chunk's or a patch's lists only its own pack.
    const bool partialExpected = reader.Header().role != AssetPackRole::Base;
    if (DecodeRuntimeAssetManifest(manifestBytes, manifest) != RuntimeAssetManifestStatus::Success ||
        manifest.targetProfileId != reader.Header().targetProfileId ||
        manifest.targetProfileHash != reader.Header().targetProfileHash ||
        manifest.partial != partialExpected) {
        return RuntimeAssetPackStatus::ManifestCorrupt;
    }

    std::map<AssetBakeDigest, const AssetPackArtifactEntry*> artifactsByDigest;
    for (const AssetPackArtifactEntry& artifact : reader.Artifacts()) {
        artifactsByDigest.emplace(artifact.key, &artifact);
    }
    std::set<AssetBakeDigest> referenced;
    for (const RuntimeAssetManifestEntry& asset : manifest.assets) {
        if (!ValidateAssetArtifactShape(asset, profile)) {
            return RuntimeAssetPackStatus::ReferenceTypeMismatch;
        }
        // An asset's artifacts travel with it: they lie in the same pack as its manifest entry,
        // so replacing the entry (a patch) replaces exactly its bytes.
        for (const RuntimeArtifactReference& reference : asset.artifacts) {
            const auto artifact = artifactsByDigest.find(reference.digest);
            if (artifact == artifactsByDigest.end()) {
                return RuntimeAssetPackStatus::ReferenceMissing;
            }
            if (artifact->second->assetTypeId != ExpectedArtifactType(reference.encoding)) {
                return RuntimeAssetPackStatus::ReferenceTypeMismatch;
            }
            referenced.insert(reference.digest);
        }
    }
    // Dependencies are checked once the whole set is merged: a base may depend on content its
    // chunks carry, and a chunk on content of the base.
    if (!manifest.partial) {
        const auto defaultMap = std::ranges::find(
            manifest.assets, manifest.settings.defaultMap, &RuntimeAssetManifestEntry::virtualPath);
        if (defaultMap == manifest.assets.end()) {
            return RuntimeAssetPackStatus::DependencyMissing;
        }
        if (defaultMap->type != "Scene" || !defaultMap->runtimeLoadable) {
            return RuntimeAssetPackStatus::ReferenceTypeMismatch;
        }
    }
    for (const RuntimeAuxiliaryFileEntry& file : manifest.auxiliaryFiles) {
        const auto artifact = artifactsByDigest.find(file.artifactDigest);
        if (artifact == artifactsByDigest.end()) {
            return RuntimeAssetPackStatus::ReferenceMissing;
        }
        if (artifact->second->assetTypeId != kSourceAssetTypeId) {
            return RuntimeAssetPackStatus::ReferenceTypeMismatch;
        }
        referenced.insert(file.artifactDigest);
    }
    for (const AssetPackArtifactEntry& artifact : reader.Artifacts()) {
        if (&artifact == manifestArtifact) {
            continue;
        }
        if (!referenced.contains(artifact.key)) {
            return RuntimeAssetPackStatus::OrphanArtifact;
        }
    }
    container.manifest = std::move(manifest);
    return RuntimeAssetPackStatus::Success;
}

RuntimeAssetPackStatus RuntimeAssetPack::FinishMount(
    std::span<const RuntimeAssetPackMount> expected,
    const BakeTargetProfile& profile) {
    std::uint32_t current = 0U;
    const auto refuse = [this, &current](RuntimeAssetPackStatus status) {
        Unmount();
        refusedContainer_ = current;
        return status;
    };
    if (expected.size() != containers_.size()) {
        return refuse(RuntimeAssetPackStatus::PackSetInvalid);
    }

    // What every pack says about itself, in its sealed header, must be what the set expects of
    // it, and the order must be the set's: the base, the chunks, the patches in ascending level.
    const AssetBakeDigest baseIdentity = containers_.front().reader->CatalogIdentity();
    bool seenPatch = false;
    std::uint32_t previousPatchLevel = 0U;
    for (current = 0U; current < containers_.size(); ++current) {
        const AssetPackHeader& header = containers_[current].reader->Header();
        const RuntimeAssetPackMount& mount = expected[current];
        if (header.role != mount.role || (current == 0U) != (header.role == AssetPackRole::Base) ||
            header.patchLevel != mount.patchLevel ||
            (header.role != AssetPackRole::Base && header.label != mount.label)) {
            return refuse(RuntimeAssetPackStatus::PackSetInvalid);
        }
        if (header.role == AssetPackRole::Chunk && seenPatch) {
            return refuse(RuntimeAssetPackStatus::PackSetInvalid);
        }
        if (header.role == AssetPackRole::Patch) {
            if (seenPatch && header.patchLevel <= previousPatchLevel) {
                return refuse(RuntimeAssetPackStatus::PackSetInvalid);
            }
            seenPatch = true;
            previousPatchLevel = header.patchLevel;
        }
        if (current != 0U && header.baseIdentity != baseIdentity) {
            return refuse(RuntimeAssetPackStatus::PackSetBaseMismatch);
        }
        if (const RuntimeAssetPackStatus status = ValidateContainer(containers_[current], profile);
            status != RuntimeAssetPackStatus::Success) {
            return refuse(status);
        }
    }

    // Merge, in mount order. An entry is a slot that a patch may empty; slots are compacted at
    // the end.
    struct Slot {
        const RuntimeAssetManifestEntry* entry = nullptr;
        std::uint32_t container = 0U;
    };
    struct FileSlot {
        const RuntimeAuxiliaryFileEntry* entry = nullptr;
        std::uint32_t container = 0U;
    };
    std::vector<Slot> slots;
    std::vector<FileSlot> fileSlots;
    std::unordered_map<std::uint64_t, std::size_t> slotById;
    std::unordered_map<std::string, std::size_t> slotByPath;
    std::unordered_map<std::string, std::size_t> fileSlotByPath;
    const RuntimeAssetManifest* settingsSource = &containers_.front().manifest;
    for (current = 0U; current < containers_.size(); ++current) {
        const Container& container = containers_[current];
        const bool patch = container.reader->Header().role == AssetPackRole::Patch;
        if (patch) {
            settingsSource = &container.manifest;
        }
        for (const RuntimeAssetManifestEntry& asset : container.manifest.assets) {
            const auto byId = slotById.find(asset.id.value);
            const auto byPath = slotByPath.find(asset.virtualPath);
            if (!patch && (byId != slotById.end() || byPath != slotByPath.end())) {
                // A chunk adds content; two packs claiming one asset is a split gone wrong.
                return refuse(RuntimeAssetPackStatus::ManifestDuplicate);
            }
            if (byId != slotById.end()) {
                slotByPath.erase(slots[byId->second].entry->virtualPath);
                slots[byId->second].entry = nullptr;
                slotById.erase(byId);
            }
            if (const auto again = slotByPath.find(asset.virtualPath); again != slotByPath.end()) {
                slotById.erase(slots[again->second].entry->id.value);
                slots[again->second].entry = nullptr;
                slotByPath.erase(again);
            }
            slotById.emplace(asset.id.value, slots.size());
            slotByPath.emplace(asset.virtualPath, slots.size());
            slots.push_back(Slot{ &asset, current });
        }
        for (const RuntimeAuxiliaryFileEntry& file : container.manifest.auxiliaryFiles) {
            const auto existing = fileSlotByPath.find(file.virtualPath);
            if (existing != fileSlotByPath.end()) {
                if (!patch) {
                    return refuse(RuntimeAssetPackStatus::ManifestDuplicate);
                }
                fileSlots[existing->second].entry = nullptr;
                fileSlotByPath.erase(existing);
            }
            fileSlotByPath.emplace(file.virtualPath, fileSlots.size());
            fileSlots.push_back(FileSlot{ &file, current });
        }
    }
    current = 0U;

    RuntimeAssetManifest merged{};
    merged.targetProfileId = containers_.front().manifest.targetProfileId;
    merged.targetProfileHash = containers_.front().manifest.targetProfileHash;
    merged.descriptor = settingsSource->descriptor;
    merged.settings = settingsSource->settings;
    std::vector<Slot> live;
    live.reserve(slots.size());
    for (const Slot& slot : slots) {
        if (slot.entry != nullptr) {
            live.push_back(slot);
        }
    }
    std::ranges::sort(live, [](const Slot& lhs, const Slot& rhs) { return lhs.entry->id.value < rhs.entry->id.value; });
    merged.assets.reserve(live.size());
    std::vector<std::uint32_t> assetContainers;
    assetContainers.reserve(live.size());
    for (const Slot& slot : live) {
        merged.assets.push_back(*slot.entry);
        assetContainers.push_back(slot.container);
    }
    std::vector<FileSlot> liveFiles;
    for (const FileSlot& slot : fileSlots) {
        if (slot.entry != nullptr) {
            liveFiles.push_back(slot);
        }
    }
    std::ranges::sort(liveFiles, [](const FileSlot& lhs, const FileSlot& rhs) {
        return lhs.entry->virtualPath < rhs.entry->virtualPath;
    });
    std::vector<std::uint32_t> auxiliaryContainers;
    auxiliaryContainers.reserve(liveFiles.size());
    for (const FileSlot& slot : liveFiles) {
        if (slotByPath.contains(slot.entry->virtualPath)) {
            return refuse(RuntimeAssetPackStatus::ManifestDuplicate);
        }
        merged.auxiliaryFiles.push_back(*slot.entry);
        auxiliaryContainers.push_back(slot.container);
    }

    std::unordered_map<std::uint64_t, std::size_t> byId;
    std::unordered_map<std::string, std::size_t> byPath;
    byId.reserve(merged.assets.size());
    byPath.reserve(merged.assets.size());
    for (std::size_t index = 0U; index < merged.assets.size(); ++index) {
        byId.emplace(merged.assets[index].id.value, index);
        byPath.emplace(merged.assets[index].virtualPath, index);
    }
    // Dependencies and the default map are a property of the whole set: a chunk's asset may
    // depend on the base, and a patch may bring the default map.
    for (const RuntimeAssetManifestEntry& asset : merged.assets) {
        for (const AssetId dependency : asset.dependencies) {
            if (!byId.contains(dependency.value)) {
                return refuse(RuntimeAssetPackStatus::DependencyMissing);
            }
        }
    }
    const auto defaultMap = byPath.find(merged.settings.defaultMap);
    if (defaultMap == byPath.end()) {
        return refuse(RuntimeAssetPackStatus::DependencyMissing);
    }
    if (merged.assets[defaultMap->second].type != "Scene" || !merged.assets[defaultMap->second].runtimeLoadable) {
        return refuse(RuntimeAssetPackStatus::ReferenceTypeMismatch);
    }

    std::map<AssetBakeDigest, ArtifactOwner> artifacts;
    for (std::uint32_t index = 0U; index < containers_.size(); ++index) {
        for (const AssetPackArtifactEntry& artifact : containers_[index].reader->Artifacts()) {
            artifacts.insert_or_assign(artifact.key, ArtifactOwner{ index, &artifact });
        }
    }

    manifest_ = std::move(merged);
    assetContainers_ = std::move(assetContainers);
    auxiliaryContainers_ = std::move(auxiliaryContainers);
    assetsById_ = std::move(byId);
    assetsByPath_ = std::move(byPath);
    artifacts_ = std::move(artifacts);
    mounted_ = true;
    return RuntimeAssetPackStatus::Success;
}

void RuntimeAssetPack::Unmount() noexcept {
    containers_.clear();
    manifest_ = RuntimeAssetManifest{};
    assetContainers_.clear();
    auxiliaryContainers_.clear();
    assetsById_.clear();
    assetsByPath_.clear();
    artifacts_.clear();
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
    const auto found = assetsById_.find(id.value);
    return found == assetsById_.end() ? nullptr : &manifest_.assets[found->second];
}

const RuntimeAssetManifestEntry* RuntimeAssetPack::FindAsset(std::string_view virtualPath) const noexcept {
    if (!mounted_) {
        return nullptr;
    }
    const auto found = assetsByPath_.find(std::string{ virtualPath });
    return found == assetsByPath_.end() ? nullptr : &manifest_.assets[found->second];
}

std::optional<std::uint32_t> RuntimeAssetPack::AssetContainer(AssetId id) const noexcept {
    if (!mounted_) {
        return std::nullopt;
    }
    const auto found = assetsById_.find(id.value);
    if (found == assetsById_.end()) {
        return std::nullopt;
    }
    return assetContainers_[found->second];
}

const RuntimeAssetPack::ArtifactOwner* RuntimeAssetPack::FindArtifactOwner(const AssetBakeDigest& digest) const noexcept {
    const auto found = artifacts_.find(digest);
    return found == artifacts_.end() ? nullptr : &found->second;
}

AssetPackReadStatus RuntimeAssetPack::ReadArtifactBlock(
    const AssetBakeDigest& digest,
    std::string_view blockName,
    std::vector<std::uint8_t>& out) {
    out.clear();
    if (!mounted_) {
        return AssetPackReadStatus::NotMounted;
    }
    const ArtifactOwner* owner = FindArtifactOwner(digest);
    if (owner == nullptr) {
        return AssetPackReadStatus::ArtifactNotFound;
    }
    Container& container = containers_[owner->container];
    std::scoped_lock lock{ *container.readMutex };
    return container.reader->ReadBlock(*owner->artifact, blockName, out);
}

RuntimeAssetPackStatus RuntimeAssetPack::ReadSourceFile(
    const AssetBakeDigest& digest,
    std::vector<std::uint8_t>& out) {
    out.clear();
    if (!mounted_) {
        return RuntimeAssetPackStatus::NotMounted;
    }
    const ArtifactOwner* owner = FindArtifactOwner(digest);
    if (owner == nullptr) {
        return RuntimeAssetPackStatus::ReferenceMissing;
    }
    if (owner->artifact->assetTypeId != kSourceAssetTypeId) {
        return RuntimeAssetPackStatus::ReferenceTypeMismatch;
    }
    std::vector<std::uint8_t> blob;
    {
        Container& container = containers_[owner->container];
        std::scoped_lock lock{ *container.readMutex };
        if (container.reader->ReadBlock(*owner->artifact, kBakedAssetPrimaryBlockName, blob) !=
            AssetPackReadStatus::Success) {
            return RuntimeAssetPackStatus::SourceCorrupt;
        }
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
    return ReadPayload(assetId, encoding, qualifier, nullptr, out);
}

RuntimeAssetPackStatus RuntimeAssetPack::ReadAssetPayloadBlocks(
    AssetId assetId,
    RuntimeArtifactEncoding encoding,
    std::string_view qualifier,
    const std::function<bool(const AssetPackBlockEntry&)>& include,
    RuntimeAssetPayload& out) {
    return ReadPayload(assetId, encoding, qualifier, &include, out);
}

RuntimeAssetPackStatus RuntimeAssetPack::ReadPayload(
    AssetId assetId,
    RuntimeArtifactEncoding encoding,
    std::string_view qualifier,
    const std::function<bool(const AssetPackBlockEntry&)>* include,
    RuntimeAssetPayload& out) {
    if (!mounted_) {
        return RuntimeAssetPackStatus::NotMounted;
    }
    const auto found = assetsById_.find(assetId.value);
    if (found == assetsById_.end()) {
        return RuntimeAssetPackStatus::ReferenceMissing;
    }
    const RuntimeAssetManifestEntry& asset = manifest_.assets[found->second];
    Container& container = containers_[assetContainers_[found->second]];
    const auto reference = std::ranges::find_if(
        asset.artifacts,
        [encoding, qualifier](const RuntimeArtifactReference& candidate) {
            return candidate.encoding == encoding && candidate.qualifier == qualifier;
        });
    if (reference == asset.artifacts.end()) {
        return RuntimeAssetPackStatus::ReferenceMissing;
    }

    std::scoped_lock lock{ *container.readMutex };
    // The asset's artifacts lie in the pack that supplies its manifest entry; a later pack that
    // happens to hold the same digest holds the same bytes, but the owner is the one validated.
    const AssetPackReader& reader = *container.reader;
    const AssetPackArtifactEntry* artifact = reader.FindArtifact(reference->digest);
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
            reader.Fragments(),
            [&indexedBlock](const AssetPackFragmentEntry& candidate) {
                return candidate.offset == indexedBlock.offset &&
                    candidate.bytes == indexedBlock.storedBytes;
            });
        if (fragment != reader.Fragments().end()) {
            block.fragment = BakedAssetBlockFragment{
                .boundsMin = fragment->boundsMin,
                .boundsMax = fragment->boundsMax,
                .clusterCount = fragment->clusterCount,
            };
        }
        const bool primary = indexedBlock.name == kBakedAssetPrimaryBlockName;
        if (include != nullptr && !primary && !(*include)(indexedBlock)) {
            payload.blocks.push_back(std::move(block));
            continue;
        }
        if (container.reader->ReadBlock(*artifact, indexedBlock.name, block.bytes) !=
            AssetPackReadStatus::Success) {
            return RuntimeAssetPackStatus::ArtifactCorrupt;
        }
        if (encoding == RuntimeArtifactEncoding::SourceBytes) {
            std::span<const std::uint8_t> source;
            if (artifact->blocks.size() != 1U ||
                !DecodeRuntimeSourceBlob(block.bytes, source)) {
                return RuntimeAssetPackStatus::SourceCorrupt;
            }
            if (HashBakeBytes(source) != asset.contentHash) {
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
    const auto file = std::ranges::lower_bound(
        manifest_.auxiliaryFiles,
        virtualPath,
        {},
        [](const RuntimeAuxiliaryFileEntry& entry) -> std::string_view { return entry.virtualPath; });
    if (file == manifest_.auxiliaryFiles.end() || file->virtualPath != virtualPath) {
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

AssetPackReadStatus RuntimeAssetPack::LocateArtifactBlock(
    const AssetBakeDigest& digest,
    std::string_view blockName,
    RuntimeAssetBlockLocation& out) const {
    out = {};
    if (!mounted_) {
        return AssetPackReadStatus::NotMounted;
    }
    const ArtifactOwner* owner = FindArtifactOwner(digest);
    if (owner == nullptr) {
        return AssetPackReadStatus::ArtifactNotFound;
    }
    const AssetPackBlockEntry* block = containers_[owner->container].reader->FindBlock(*owner->artifact, blockName);
    if (block == nullptr) {
        return AssetPackReadStatus::BlockNotFound;
    }
    out = RuntimeAssetBlockLocation{ owner->container, block };
    return AssetPackReadStatus::Success;
}

AssetPackReadStatus RuntimeAssetPack::DecodeStoredArtifactBlock(
    const RuntimeAssetBlockLocation& location,
    std::vector<std::uint8_t>& bytes) const {
    if (!mounted_) {
        bytes.clear();
        return AssetPackReadStatus::NotMounted;
    }
    if (location.container >= containers_.size() || location.block == nullptr) {
        bytes.clear();
        return AssetPackReadStatus::BlockNotFound;
    }
    return containers_[location.container].reader->DecodeStoredBlock(*location.block, bytes);
}

namespace {

[[nodiscard]] const AssetPackHeader& EmptyHeader() noexcept {
    static const AssetPackHeader header{};
    return header;
}

[[nodiscard]] const std::filesystem::path& EmptyPath() noexcept {
    static const std::filesystem::path path{};
    return path;
}

[[nodiscard]] const kb::security::Sha512Digest& EmptyDigest() noexcept {
    static const kb::security::Sha512Digest digest{};
    return digest;
}

[[nodiscard]] const AssetBakeDigest& EmptyIdentity() noexcept {
    static const AssetBakeDigest identity{};
    return identity;
}

} // namespace

const AssetPackHeader& RuntimeAssetPack::Header() const noexcept {
    return ContainerHeader(0U);
}

std::span<const AssetPackArtifactEntry> RuntimeAssetPack::Artifacts() const noexcept {
    return ContainerArtifacts(0U);
}

AssetPackReadStatus RuntimeAssetPack::ContainerStatus() const noexcept {
    return containerStatus_;
}

std::uint32_t RuntimeAssetPack::RefusedContainer() const noexcept {
    return refusedContainer_;
}

const AssetPackSeal* RuntimeAssetPack::Seal() const noexcept {
    return ContainerSeal(0U);
}

const kb::security::Sha512Digest& RuntimeAssetPack::SealDigest() const noexcept {
    return ContainerSealDigest(0U);
}

std::uint32_t RuntimeAssetPack::ContainerCount() const noexcept {
    return mounted_ ? static_cast<std::uint32_t>(containers_.size()) : 0U;
}

const AssetPackHeader& RuntimeAssetPack::ContainerHeader(std::uint32_t container) const noexcept {
    return container < containers_.size() ? containers_[container].reader->Header() : EmptyHeader();
}

std::span<const AssetPackArtifactEntry> RuntimeAssetPack::ContainerArtifacts(std::uint32_t container) const noexcept {
    return container < containers_.size() ? containers_[container].reader->Artifacts()
                                          : std::span<const AssetPackArtifactEntry>{};
}

const std::filesystem::path& RuntimeAssetPack::ContainerPath(std::uint32_t container) const noexcept {
    return container < containers_.size() ? containers_[container].path : EmptyPath();
}

std::span<const std::uint8_t> RuntimeAssetPack::ContainerResidentBytes(std::uint32_t container) const noexcept {
    return container < containers_.size() ? containers_[container].reader->ResidentBytes()
                                          : std::span<const std::uint8_t>{};
}

const AssetPackSeal* RuntimeAssetPack::ContainerSeal(std::uint32_t container) const noexcept {
    return container < containers_.size() ? containers_[container].reader->Seal() : nullptr;
}

const kb::security::Sha512Digest& RuntimeAssetPack::ContainerSealDigest(std::uint32_t container) const noexcept {
    return container < containers_.size() ? containers_[container].reader->SealDigest() : EmptyDigest();
}

const AssetBakeDigest& RuntimeAssetPack::ContainerCatalogIdentity(std::uint32_t container) const noexcept {
    return container < containers_.size() ? containers_[container].reader->CatalogIdentity() : EmptyIdentity();
}

AssetPackReadStatus RuntimeAssetPack::ReadContainerBlock(
    std::uint32_t container,
    const AssetPackArtifactEntry& artifact,
    std::string_view blockName,
    std::vector<std::uint8_t>& out) {
    out.clear();
    if (!mounted_) {
        return AssetPackReadStatus::NotMounted;
    }
    if (container >= containers_.size()) {
        return AssetPackReadStatus::ArtifactNotFound;
    }
    std::scoped_lock lock{ *containers_[container].readMutex };
    return containers_[container].reader->ReadBlock(artifact, blockName, out);
}

} // namespace kb::assets::bake
