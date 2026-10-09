#include "engine/assets/bake/AssetPackTools.hpp"

#include "engine/assets/bake/AssetPackReader.hpp"
#include "engine/assets/bake/AssetPackSet.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetManifest.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace kb::assets::bake {
namespace {

constexpr std::string_view kRuntimeManifestBakerId = "RuntimeManifest";

[[nodiscard]] std::uint64_t NonZeroHash(std::span<const std::uint8_t> bytes) noexcept {
    const std::uint64_t hash = HashBakeBytes(bytes);
    return hash == 0U ? 1U : hash;
}

[[nodiscard]] bool ProfileOf(const AssetPackHeader& header, BakeTargetProfile& profile, std::string& error) {
    if (!TryFindBakeTargetProfile(header.targetProfileId, profile) ||
        BakeTargetProfileFingerprint(profile) != header.targetProfileHash) {
        error = "the pack was baked for a target profile this build does not know: " + header.targetProfileId;
        return false;
    }
    return true;
}

[[nodiscard]] bool Describe(const std::filesystem::path& path, AssetPackToolReport& report, std::string& error) {
    AssetPackReader reader;
    if (const AssetPackReadStatus status = reader.Mount(path); status != AssetPackReadStatus::Success) {
        error = "the written pack does not mount: " + std::string{ ToString(status) };
        return false;
    }
    report = DescribeAssetPack(reader);
    return true;
}

// Writes `manifest` and the artifacts it references, copied out of `source`, into a new pack.
[[nodiscard]] bool WriteRuntimePack(
    AssetPackReader& source,
    const BakeTargetProfile& profile,
    const RuntimeAssetManifest& manifest,
    const AssetPackWriterOptions& options,
    const std::filesystem::path& output,
    AssetPackToolReport& report,
    std::string& error) {
    std::set<AssetBakeDigest> digests;
    for (const RuntimeAssetManifestEntry& asset : manifest.assets) {
        for (const RuntimeArtifactReference& reference : asset.artifacts) {
            digests.insert(reference.digest);
        }
    }
    for (const RuntimeAuxiliaryFileEntry& file : manifest.auxiliaryFiles) {
        digests.insert(file.artifactDigest);
    }
    std::vector<std::uint8_t> manifestBytes;
    if (const RuntimeAssetManifestStatus status = EncodeRuntimeAssetManifest(manifest, manifestBytes);
        status != RuntimeAssetManifestStatus::Success) {
        error = "the runtime manifest does not encode: " + std::string{ ToString(status) };
        return false;
    }

    AssetPackWriter writer{ output, profile, options };
    for (const AssetBakeDigest& digest : digests) {
        const AssetPackArtifactEntry* artifact = source.FindArtifact(digest);
        if (artifact == nullptr) {
            error = "an artifact the manifest references is missing from the source pack: " + digest.ToString();
            return false;
        }
        if (const BakedAssetSinkStatus status = writer.CopyArtifact(source, *artifact);
            status != BakedAssetSinkStatus::Success) {
            error = "artifact " + digest.ToString() + " could not be copied: " + std::string{ ToString(status) };
            return false;
        }
    }
    AssetBakeKey key{};
    key.sourceContentHash = NonZeroHash(manifestBytes);
    key.bakerId = std::string{ kRuntimeManifestBakerId };
    key.bakerVersion = "1";
    key.targetProfileId = std::string{ profile.identifier };
    key.targetProfileHash = BakeTargetProfileFingerprint(profile);
    const std::string salt = std::string{ ToString(options.role) } + ":" + options.label;
    key.settingsHash = NonZeroHash(std::span{ reinterpret_cast<const std::uint8_t*>(salt.data()), salt.size() });
    BakedAssetSinkStatus status = writer.BeginAsset(BakedAssetDescriptor{ .key = key,
        .assetTypeId = std::string{ kRuntimeManifestAssetTypeId } });
    if (status == BakedAssetSinkStatus::Success) {
        status = writer.WritePrimaryBlock(manifestBytes, profile.packageBlockAlignmentBytes);
    }
    if (status == BakedAssetSinkStatus::Success) {
        status = writer.CommitAsset();
    }
    if (status == BakedAssetSinkStatus::Success) {
        status = writer.Finish();
    }
    if (status != BakedAssetSinkStatus::Success) {
        writer.AbortAsset();
        error = "the pack could not be written: " + std::string{ ToString(status) };
        return false;
    }
    return Describe(output, report, error);
}

// Whether two manifests carry the same project descriptor and settings, compared in their
// canonical encoding.
[[nodiscard]] bool SameProject(const RuntimeAssetManifest& lhs, const RuntimeAssetManifest& rhs) {
    const auto project = [](const RuntimeAssetManifest& manifest) {
        RuntimeAssetManifest only{};
        only.targetProfileId = manifest.targetProfileId;
        only.targetProfileHash = manifest.targetProfileHash;
        only.partial = true;
        only.descriptor = manifest.descriptor;
        only.settings = manifest.settings;
        std::vector<std::uint8_t> bytes;
        static_cast<void>(EncodeRuntimeAssetManifest(only, bytes));
        return bytes;
    };
    return project(lhs) == project(rhs);
}

[[nodiscard]] bool StartsWith(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && text.substr(0U, prefix.size()) == prefix;
}

} // namespace

AssetPackToolReport DescribeAssetPack(const AssetPackReader& reader) {
    AssetPackToolReport report{};
    report.fileBytes = reader.Header().fileBytes;
    for (const AssetPackArtifactEntry& artifact : reader.Artifacts()) {
        ++report.artifacts;
        for (const AssetPackBlockEntry& block : artifact.blocks) {
            ++report.blocks;
            report.payloadBytes += block.uncompressedBytes;
            report.storedBytes += block.storedBytes;
            if (block.compression != AssetPackBlockCompression::None) {
                ++report.compressedBlocks;
            }
        }
    }
    return report;
}

bool RepackAssetPack(
    const std::filesystem::path& input,
    const std::filesystem::path& output,
    const AssetPackWriterOptions& options,
    AssetPackToolReport& report,
    std::string& error) {
    AssetPackReader source;
    if (const AssetPackReadStatus status = source.Mount(input); status != AssetPackReadStatus::Success) {
        error = "the input pack does not mount: " + std::string{ ToString(status) };
        return false;
    }
    BakeTargetProfile profile{};
    if (!ProfileOf(source.Header(), profile, error)) {
        return false;
    }
    if (!IsValidAssetPackWriterOptions(options)) {
        error = "the pack options do not describe a valid pack";
        return false;
    }
    AssetPackWriter writer{ output, profile, options };
    for (const AssetPackArtifactEntry& artifact : source.Artifacts()) {
        if (const BakedAssetSinkStatus status = writer.CopyArtifact(source, artifact); status != BakedAssetSinkStatus::Success) {
            error = "artifact " + artifact.key.ToString() + " could not be copied: " + std::string{ ToString(status) };
            return false;
        }
    }
    if (const BakedAssetSinkStatus status = writer.Finish(); status != BakedAssetSinkStatus::Success) {
        error = "the pack could not be written: " + std::string{ ToString(status) };
        return false;
    }
    source.Unmount();
    return Describe(output, report, error);
}

bool SplitRuntimeAssetPack(
    const std::filesystem::path& input,
    const std::filesystem::path& baseOutput,
    std::span<const AssetPackChunkRule> rules,
    AssetPackBlockCompression compression,
    int compressionLevel,
    AssetPackSplitReport& report,
    std::string& error) {
    report = {};
    if (rules.empty()) {
        error = "a split needs at least one chunk rule";
        return false;
    }
    RuntimeAssetPack runtime;
    BakeTargetProfile profile{};
    {
        AssetPackReader probe;
        if (const AssetPackReadStatus status = probe.Mount(input); status != AssetPackReadStatus::Success) {
            error = "the input pack does not mount: " + std::string{ ToString(status) };
            return false;
        }
        if (probe.Header().role != AssetPackRole::Base) {
            error = "only a base pack can be split";
            return false;
        }
        if (!ProfileOf(probe.Header(), profile, error)) {
            return false;
        }
    }
    if (const RuntimeAssetPackStatus status = runtime.Mount(input, profile); status != RuntimeAssetPackStatus::Success) {
        error = "the input pack is not a valid runtime pack: " + std::string{ ToString(status) };
        return false;
    }
    const RuntimeAssetManifest& full = runtime.Manifest();
    std::set<std::string> labels;
    for (const AssetPackChunkRule& rule : rules) {
        if (!IsValidBakeCacheName(rule.label) || !labels.insert(rule.label).second || rule.virtualPathPrefixes.empty() ||
            rule.output.empty()) {
            error = "chunk rules need distinct valid labels, at least one prefix and an output: " + rule.label;
            return false;
        }
    }

    RuntimeAssetManifest base = full;
    base.assets.clear();
    std::vector<RuntimeAssetManifest> chunks(rules.size());
    for (RuntimeAssetManifest& chunk : chunks) {
        chunk = full;
        chunk.assets.clear();
        chunk.auxiliaryFiles.clear();
        chunk.partial = true;
    }
    for (const RuntimeAssetManifestEntry& asset : full.assets) {
        std::size_t target = rules.size();
        for (std::size_t index = 0U; index < rules.size() && target == rules.size(); ++index) {
            for (const std::string& prefix : rules[index].virtualPathPrefixes) {
                if (!prefix.empty() && StartsWith(asset.virtualPath, prefix)) {
                    target = index;
                    break;
                }
            }
        }
        if (target == rules.size()) {
            base.assets.push_back(asset);
            continue;
        }
        if (asset.virtualPath == full.settings.defaultMap) {
            error = "the default map cannot leave the base pack: " + asset.virtualPath;
            return false;
        }
        chunks[target].assets.push_back(asset);
    }
    for (std::size_t index = 0U; index < rules.size(); ++index) {
        if (chunks[index].assets.empty()) {
            error = "chunk " + rules[index].label + " matches no asset";
            return false;
        }
    }
    runtime.Unmount();

    AssetPackReader source;
    if (const AssetPackReadStatus status = source.Mount(input); status != AssetPackReadStatus::Success) {
        error = "the input pack does not mount: " + std::string{ ToString(status) };
        return false;
    }
    AssetPackWriterOptions baseOptions{};
    baseOptions.compression = compression;
    baseOptions.compressionLevel = compressionLevel;
    if (!WriteRuntimePack(source, profile, base, baseOptions, baseOutput, report.base, error)) {
        return false;
    }
    AssetBakeDigest baseIdentity{};
    {
        AssetPackReader written;
        if (written.Mount(baseOutput) != AssetPackReadStatus::Success) {
            error = "the base pack does not mount after writing";
            return false;
        }
        baseIdentity = written.CatalogIdentity();
    }
    for (std::size_t index = 0U; index < rules.size(); ++index) {
        AssetPackWriterOptions options = baseOptions;
        options.role = AssetPackRole::Chunk;
        options.label = rules[index].label;
        options.baseIdentity = baseIdentity;
        AssetPackToolReport chunkReport{};
        if (!WriteRuntimePack(source, profile, chunks[index], options, rules[index].output, chunkReport, error)) {
            return false;
        }
        report.chunks.push_back(chunkReport);
        report.chunkAssets.push_back(chunks[index].assets.size());
    }
    return true;
}

bool BuildAssetPackPatch(const AssetPackPatchRequest& request, AssetPackPatchReport& report, std::string& error) {
    report = {};
    if (!IsValidBakeCacheName(request.label) || request.patchLevel == 0U) {
        error = "a patch needs a valid label and a patch level of at least 1";
        return false;
    }
    BakeTargetProfile profile{};
    AssetPackReader nextSource;
    if (const AssetPackReadStatus status = nextSource.Mount(request.next); status != AssetPackReadStatus::Success) {
        error = "the new cook does not mount: " + std::string{ ToString(status) };
        return false;
    }
    if (nextSource.Header().role != AssetPackRole::Base || !ProfileOf(nextSource.Header(), profile, error)) {
        if (error.empty()) {
            error = "the new cook is not a complete base pack";
        }
        return false;
    }
    RuntimeAssetPack next;
    if (const RuntimeAssetPackStatus status = next.Mount(request.next, profile); status != RuntimeAssetPackStatus::Success) {
        error = "the new cook is not a valid runtime pack: " + std::string{ ToString(status) };
        return false;
    }
    RuntimeAssetPack current;
    std::string extension = request.current.extension().string();
    std::ranges::transform(extension, extension.begin(), [](char value) {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
    });
    const RuntimeAssetPackStatus currentStatus = extension == kAssetPackSetFileExtension
        ? current.MountSetIndex(request.current, profile)
        : current.Mount(request.current, profile);
    if (currentStatus != RuntimeAssetPackStatus::Success) {
        error = "the current content does not mount for this profile: " + std::string{ ToString(currentStatus) };
        return false;
    }
    for (std::uint32_t container = 0U; container < current.ContainerCount(); ++container) {
        if (current.ContainerHeader(container).role == AssetPackRole::Patch &&
            current.ContainerHeader(container).patchLevel >= request.patchLevel) {
            error = "patch level " + std::to_string(request.patchLevel) + " does not go past the current patch level " +
                std::to_string(current.ContainerHeader(container).patchLevel);
            return false;
        }
        if (current.ContainerHeader(container).label == request.label && container != 0U) {
            error = "the current content already has a pack labelled " + request.label;
            return false;
        }
    }

    RuntimeAssetManifest patch = next.Manifest();
    patch.partial = true;
    patch.assets.clear();
    patch.auxiliaryFiles.clear();
    for (const RuntimeAssetManifestEntry& asset : next.Manifest().assets) {
        const RuntimeAssetManifestEntry* existing = current.FindAsset(asset.id);
        if (existing == nullptr) {
            ++report.addedAssets;
            patch.assets.push_back(asset);
        } else if (!(*existing == asset)) {
            ++report.changedAssets;
            patch.assets.push_back(asset);
        }
    }
    for (const RuntimeAssetManifestEntry& asset : current.Manifest().assets) {
        if (next.FindAsset(asset.id) == nullptr) {
            ++report.removedAssets;
        }
    }
    std::unordered_map<std::string, const RuntimeAuxiliaryFileEntry*> currentFiles;
    for (const RuntimeAuxiliaryFileEntry& file : current.Manifest().auxiliaryFiles) {
        currentFiles.emplace(file.virtualPath, &file);
    }
    for (const RuntimeAuxiliaryFileEntry& file : next.Manifest().auxiliaryFiles) {
        const auto existing = currentFiles.find(file.virtualPath);
        if (existing == currentFiles.end() || !(*existing->second == file)) {
            ++report.changedFiles;
            patch.auxiliaryFiles.push_back(file);
        }
    }
    report.settingsChanged = !SameProject(current.Manifest(), next.Manifest());
    if (patch.assets.empty() && patch.auxiliaryFiles.empty() && !report.settingsChanged) {
        error = "the new cook changes nothing the current content does not already have";
        return false;
    }

    AssetPackWriterOptions options{};
    options.compression = request.compression;
    options.compressionLevel = request.compressionLevel;
    options.role = AssetPackRole::Patch;
    options.label = request.label;
    options.patchLevel = request.patchLevel;
    options.baseIdentity = current.ContainerCatalogIdentity(0U);
    current.Unmount();
    next.Unmount();
    return WriteRuntimePack(nextSource, profile, patch, options, request.output, report.pack, error);
}

} // namespace kb::assets::bake
