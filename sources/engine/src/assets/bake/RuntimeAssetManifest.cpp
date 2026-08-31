#include "engine/assets/bake/RuntimeAssetManifest.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/bake/AssetPack.hpp"
#include "project/ProjectDescriptorFormat.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace kb::assets::bake {
namespace {

constexpr std::array<std::uint8_t, 8U> kMagic{ '2', '1', 'K', 'B', 'R', 'M', 'F', 0U };
constexpr std::array<std::uint8_t, 8U> kSourceMagic{ '2', '1', 'K', 'B', 'S', 'R', 'C', 0U };
constexpr std::uint64_t kMaxManifestBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kMaxDescriptorTargetPlatforms =
    kb::project::ProjectDescriptorFormat::MaxTargetPlatformCount;
constexpr std::uint32_t kMaxDescriptorModules = kb::project::ProjectDescriptorFormat::MaxModuleCount;
constexpr std::uint32_t kMaxDescriptorPlugins = kb::project::ProjectDescriptorFormat::MaxPluginCount;
constexpr std::uint32_t kMaxAssets = 65'536U;
constexpr std::uint32_t kMaxDependenciesPerAsset = 65'536U;
constexpr std::uint32_t kMaxArtifactsPerAsset = 64U;
constexpr std::uint32_t kMaxAuxiliaryFiles = 131'072U;
constexpr std::uint32_t kMaxShortStringBytes = 255U;
constexpr std::uint32_t kMaxPathBytes = 4096U;
constexpr std::uint32_t kMaxDescriptionBytes = 65535U;

void PutUInt8(std::vector<std::uint8_t>& bytes, std::uint8_t value) {
    bytes.push_back(value);
}

void PutUInt32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void PutUInt64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void PutString(std::vector<std::uint8_t>& bytes, std::string_view value) {
    PutUInt32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

class Reader final {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) noexcept
        : bytes_{ bytes } {}

    [[nodiscard]] bool ReadUInt8(std::uint8_t& value) noexcept {
        if (cursor_ >= bytes_.size()) {
            return false;
        }
        value = bytes_[cursor_++];
        return true;
    }

    [[nodiscard]] bool ReadUInt32(std::uint32_t& value) noexcept {
        if (bytes_.size() - cursor_ < 4U) {
            return false;
        }
        value = 0U;
        for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
            value |= static_cast<std::uint32_t>(bytes_[cursor_++]) << shift;
        }
        return true;
    }

    [[nodiscard]] bool ReadUInt64(std::uint64_t& value) noexcept {
        if (bytes_.size() - cursor_ < 8U) {
            return false;
        }
        value = 0U;
        for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
            value |= static_cast<std::uint64_t>(bytes_[cursor_++]) << shift;
        }
        return true;
    }

    [[nodiscard]] bool ReadString(std::string& value, std::uint32_t maximumBytes) {
        std::uint32_t length = 0U;
        if (!ReadUInt32(length) || length > maximumBytes || bytes_.size() - cursor_ < length) {
            return false;
        }
        value.assign(reinterpret_cast<const char*>(bytes_.data() + cursor_), length);
        cursor_ += length;
        return true;
    }

    [[nodiscard]] bool Finished() const noexcept {
        return cursor_ == bytes_.size();
    }

    [[nodiscard]] std::size_t Remaining() const noexcept {
        return bytes_.size() - cursor_;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t cursor_ = 0U;
};

[[nodiscard]] RuntimeAssetManifestStatus ReadBoundedCount(
    Reader& reader,
    std::uint32_t& count,
    std::uint32_t maximumCount,
    std::size_t minimumEntryBytes) noexcept {
    if (!reader.ReadUInt32(count)) {
        return RuntimeAssetManifestStatus::Malformed;
    }
    if (count > maximumCount) {
        return RuntimeAssetManifestStatus::TooLarge;
    }
    if (minimumEntryBytes == 0U || count > reader.Remaining() / minimumEntryBytes) {
        return RuntimeAssetManifestStatus::Malformed;
    }
    return RuntimeAssetManifestStatus::Success;
}

[[nodiscard]] bool IsCleanText(std::string_view text) noexcept {
    return std::ranges::none_of(text, [](char value) { return value == '\0'; });
}

[[nodiscard]] bool IsPortableQualifier(std::string_view text) noexcept {
    return text.size() <= kMaxShortStringBytes && std::ranges::all_of(text, [](unsigned char value) {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '.' || value == '_' || value == '-' || value == ':';
    });
}

[[nodiscard]] bool IsGameVirtualPath(std::string_view path) {
    if (path.empty() || path.size() > kMaxPathBytes || !path.starts_with("/Game/")) {
        return false;
    }
    const std::string normalized = NormalizeAssetPath(std::filesystem::path{ path });
    return normalized == path && path.find("/../") == std::string_view::npos && !path.ends_with("/..");
}

[[nodiscard]] bool IsPackVirtualPath(std::string_view path) {
    if (path.empty() || path.size() > kMaxPathBytes ||
        (!path.starts_with("/Game/") && !path.starts_with("/Engine/"))) {
        return false;
    }
    const std::string normalized = NormalizeAssetPath(std::filesystem::path{ path });
    return normalized == path && path.find("/../") == std::string_view::npos && !path.ends_with("/..");
}

[[nodiscard]] bool IsValidDescriptor(const kb::project::ProjectDescriptor& descriptor) noexcept {
    if (descriptor.fileVersion == 0U || descriptor.fileVersion > kb::project::ProjectDescriptor::CurrentFileVersion ||
        descriptor.engineAssociation.empty() || descriptor.engineAssociation.size() > kMaxShortStringBytes ||
        descriptor.contentRoot.empty() || descriptor.contentRoot.size() > kMaxPathBytes) {
        return false;
    }
    for (const std::string& target : descriptor.targetPlatforms) {
        if (target.empty() || target.size() > kMaxShortStringBytes || !IsCleanText(target)) {
            return false;
        }
    }
    for (const kb::project::ProjectModuleDescriptor& module : descriptor.modules) {
        if (module.name.empty() || module.name.size() > kMaxShortStringBytes ||
            module.type.size() > kMaxShortStringBytes || module.loadingPhase.size() > kMaxShortStringBytes ||
            !IsCleanText(module.name) || !IsCleanText(module.type) || !IsCleanText(module.loadingPhase)) {
            return false;
        }
    }
    for (const kb::project::ProjectPluginReference& plugin : descriptor.plugins) {
        if (plugin.name.empty() || plugin.name.size() > kMaxShortStringBytes ||
            plugin.binaryPath.size() > kMaxPathBytes || !IsCleanText(plugin.name) || !IsCleanText(plugin.binaryPath)) {
            return false;
        }
    }
    return IsCleanText(descriptor.engineAssociation) && IsCleanText(descriptor.contentRoot);
}

[[nodiscard]] bool IsValidSettings(const kb::project::ProjectSettings& settings) noexcept {
    const std::array<std::string_view, 7U> shortStrings{
        settings.name,
        settings.gameName,
        settings.category,
        settings.defaultMap,
        settings.lastOpenMap,
        settings.inputMappingContext,
        settings.physicsLayersAsset,
    };
    if (std::ranges::any_of(shortStrings, [](std::string_view value) {
            return value.size() > kMaxPathBytes || !IsCleanText(value);
        }) || settings.description.size() > kMaxDescriptionBytes || !IsCleanText(settings.description)) {
        return false;
    }
    return static_cast<std::uint32_t>(settings.lightingPath) <=
        static_cast<std::uint32_t>(kb::project::ProjectSceneLightingPath::ForwardPlus);
}

[[nodiscard]] bool ArtifactLess(
    const RuntimeArtifactReference& lhs,
    const RuntimeArtifactReference& rhs) noexcept {
    return std::tie(lhs.encoding, lhs.qualifier, lhs.digest) < std::tie(rhs.encoding, rhs.qualifier, rhs.digest);
}

[[nodiscard]] RuntimeAssetManifestStatus CanonicalizeAndValidate(RuntimeAssetManifest& manifest) {
    BakeTargetProfile profile{};
    if (!TryFindBakeTargetProfile(manifest.targetProfileId, profile) ||
        manifest.targetProfileHash != BakeTargetProfileFingerprint(profile)) {
        return RuntimeAssetManifestStatus::InvalidProfile;
    }
    if (manifest.descriptor.targetPlatforms.size() > kMaxDescriptorTargetPlatforms ||
        manifest.descriptor.modules.size() > kMaxDescriptorModules ||
        manifest.descriptor.plugins.size() > kMaxDescriptorPlugins || manifest.assets.size() > kMaxAssets ||
        manifest.auxiliaryFiles.size() > kMaxAuxiliaryFiles) {
        return RuntimeAssetManifestStatus::TooLarge;
    }
    if (!IsValidDescriptor(manifest.descriptor) || !IsValidSettings(manifest.settings)) {
        return RuntimeAssetManifestStatus::InvalidProject;
    }

    std::ranges::sort(manifest.assets, [](const RuntimeAssetManifestEntry& lhs, const RuntimeAssetManifestEntry& rhs) {
        return std::tie(lhs.id.value, lhs.virtualPath) < std::tie(rhs.id.value, rhs.virtualPath);
    });
    std::set<std::uint64_t> assetIds;
    std::set<std::string> virtualPaths;
    for (RuntimeAssetManifestEntry& asset : manifest.assets) {
        if (asset.dependencies.size() > kMaxDependenciesPerAsset ||
            asset.artifacts.size() > kMaxArtifactsPerAsset) {
            return RuntimeAssetManifestStatus::TooLarge;
        }
        if (!asset.id.IsValid() || !IsValidBakeCacheName(asset.type) ||
            asset.importCategory.size() > kMaxShortStringBytes || asset.browseTag.size() > kMaxShortStringBytes ||
            asset.name.empty() || asset.name.size() > kMaxShortStringBytes ||
            !IsGameVirtualPath(asset.virtualPath) || asset.sourceExtension.size() > kMaxShortStringBytes ||
            asset.contentHash == 0U || asset.artifacts.empty() ||
            !IsCleanText(asset.importCategory) || !IsCleanText(asset.browseTag) ||
            !IsCleanText(asset.name) || !IsCleanText(asset.sourceExtension)) {
            return RuntimeAssetManifestStatus::InvalidAsset;
        }
        if (MakeAssetId(asset.virtualPath + ":" + asset.type) != asset.id) {
            return RuntimeAssetManifestStatus::InvalidAsset;
        }
        if (!assetIds.insert(asset.id.value).second || !virtualPaths.insert(asset.virtualPath).second) {
            return RuntimeAssetManifestStatus::DuplicateEntry;
        }
        std::ranges::sort(asset.dependencies, {}, &AssetId::value);
        if (std::ranges::any_of(asset.dependencies, [](AssetId dependency) { return !dependency.IsValid(); }) ||
            std::ranges::adjacent_find(asset.dependencies) != asset.dependencies.end()) {
            return RuntimeAssetManifestStatus::InvalidAsset;
        }
        std::ranges::sort(asset.artifacts, ArtifactLess);
        for (std::size_t index = 0U; index < asset.artifacts.size(); ++index) {
            const RuntimeArtifactReference& artifact = asset.artifacts[index];
            if ((artifact.digest.high == 0U && artifact.digest.low == 0U) ||
                static_cast<std::uint8_t>(artifact.encoding) >
                    static_cast<std::uint8_t>(RuntimeArtifactEncoding::MaterialShader) ||
                !IsPortableQualifier(artifact.qualifier) ||
                ((artifact.encoding == RuntimeArtifactEncoding::SourceBytes ||
                     artifact.encoding == RuntimeArtifactEncoding::BakedMesh) &&
                    !artifact.qualifier.empty()) ||
                ((artifact.encoding == RuntimeArtifactEncoding::BakedTexture ||
                     artifact.encoding == RuntimeArtifactEncoding::MaterialShader) &&
                    artifact.qualifier.empty())) {
                return RuntimeAssetManifestStatus::InvalidAsset;
            }
            if (index != 0U && artifact.encoding == asset.artifacts[index - 1U].encoding &&
                artifact.qualifier == asset.artifacts[index - 1U].qualifier) {
                return RuntimeAssetManifestStatus::DuplicateEntry;
            }
        }
    }

    const auto defaultMap = std::ranges::find(
        manifest.assets, manifest.settings.defaultMap, &RuntimeAssetManifestEntry::virtualPath);
    if (defaultMap == manifest.assets.end() || defaultMap->type != "Scene" ||
        !defaultMap->runtimeLoadable) {
        return RuntimeAssetManifestStatus::InvalidProject;
    }

    std::ranges::sort(manifest.auxiliaryFiles, {}, &RuntimeAuxiliaryFileEntry::virtualPath);
    for (const RuntimeAuxiliaryFileEntry& file : manifest.auxiliaryFiles) {
        if (!IsPackVirtualPath(file.virtualPath) || file.contentHash == 0U ||
            (file.artifactDigest.high == 0U && file.artifactDigest.low == 0U) ||
            !virtualPaths.insert(file.virtualPath).second) {
            return RuntimeAssetManifestStatus::DuplicateEntry;
        }
    }
    return RuntimeAssetManifestStatus::Success;
}

void EncodeDescriptor(std::vector<std::uint8_t>& bytes, const kb::project::ProjectDescriptor& descriptor) {
    PutUInt32(bytes, descriptor.fileVersion);
    PutString(bytes, descriptor.engineAssociation);
    PutString(bytes, descriptor.contentRoot);
    PutUInt32(bytes, static_cast<std::uint32_t>(descriptor.targetPlatforms.size()));
    for (const std::string& target : descriptor.targetPlatforms) {
        PutString(bytes, target);
    }
    PutUInt32(bytes, static_cast<std::uint32_t>(descriptor.modules.size()));
    for (const kb::project::ProjectModuleDescriptor& module : descriptor.modules) {
        PutString(bytes, module.name);
        PutString(bytes, module.type);
        PutString(bytes, module.loadingPhase);
    }
    PutUInt32(bytes, static_cast<std::uint32_t>(descriptor.plugins.size()));
    for (const kb::project::ProjectPluginReference& plugin : descriptor.plugins) {
        PutString(bytes, plugin.name);
        PutString(bytes, plugin.binaryPath);
        PutUInt8(bytes, plugin.enabled ? 1U : 0U);
    }
    PutUInt8(bytes, descriptor.disableEnginePluginsByDefault ? 1U : 0U);
}

[[nodiscard]] RuntimeAssetManifestStatus DecodeDescriptor(
    Reader& reader,
    kb::project::ProjectDescriptor& descriptor) {
    std::uint32_t targetCount = 0U;
    std::uint32_t moduleCount = 0U;
    std::uint32_t pluginCount = 0U;
    if (!reader.ReadUInt32(descriptor.fileVersion) ||
        !reader.ReadString(descriptor.engineAssociation, kMaxShortStringBytes) ||
        !reader.ReadString(descriptor.contentRoot, kMaxPathBytes)) {
        return RuntimeAssetManifestStatus::Malformed;
    }
    RuntimeAssetManifestStatus countStatus =
        ReadBoundedCount(reader, targetCount, kMaxDescriptorTargetPlatforms, 4U);
    if (countStatus != RuntimeAssetManifestStatus::Success) {
        return countStatus;
    }
    descriptor.targetPlatforms.resize(targetCount);
    for (std::string& target : descriptor.targetPlatforms) {
        if (!reader.ReadString(target, kMaxShortStringBytes)) {
            return RuntimeAssetManifestStatus::Malformed;
        }
    }
    countStatus = ReadBoundedCount(reader, moduleCount, kMaxDescriptorModules, 12U);
    if (countStatus != RuntimeAssetManifestStatus::Success) {
        return countStatus;
    }
    descriptor.modules.resize(moduleCount);
    for (kb::project::ProjectModuleDescriptor& module : descriptor.modules) {
        if (!reader.ReadString(module.name, kMaxShortStringBytes) ||
            !reader.ReadString(module.type, kMaxShortStringBytes) ||
            !reader.ReadString(module.loadingPhase, kMaxShortStringBytes)) {
            return RuntimeAssetManifestStatus::Malformed;
        }
    }
    countStatus = ReadBoundedCount(reader, pluginCount, kMaxDescriptorPlugins, 9U);
    if (countStatus != RuntimeAssetManifestStatus::Success) {
        return countStatus;
    }
    descriptor.plugins.resize(pluginCount);
    for (kb::project::ProjectPluginReference& plugin : descriptor.plugins) {
        std::uint8_t enabled = 0U;
        if (!reader.ReadString(plugin.name, kMaxShortStringBytes) ||
            !reader.ReadString(plugin.binaryPath, kMaxPathBytes) || !reader.ReadUInt8(enabled) || enabled > 1U) {
            return RuntimeAssetManifestStatus::Malformed;
        }
        plugin.enabled = enabled != 0U;
    }
    std::uint8_t disabled = 0U;
    if (!reader.ReadUInt8(disabled) || disabled > 1U) {
        return RuntimeAssetManifestStatus::Malformed;
    }
    descriptor.disableEnginePluginsByDefault = disabled != 0U;
    return RuntimeAssetManifestStatus::Success;
}

void EncodeSettings(std::vector<std::uint8_t>& bytes, const kb::project::ProjectSettings& settings) {
    PutString(bytes, settings.name);
    PutString(bytes, settings.gameName);
    PutString(bytes, settings.category);
    PutString(bytes, settings.description);
    PutString(bytes, settings.defaultMap);
    PutString(bytes, settings.lastOpenMap);
    PutUInt32(bytes, static_cast<std::uint32_t>(settings.lightingPath));
    PutUInt8(bytes, settings.inputEnabled ? 1U : 0U);
    PutString(bytes, settings.inputMappingContext);
    PutString(bytes, settings.physicsLayersAsset);
}

[[nodiscard]] bool DecodeSettings(Reader& reader, kb::project::ProjectSettings& settings) {
    std::uint32_t lightingPath = 0U;
    std::uint8_t inputEnabled = 0U;
    if (!reader.ReadString(settings.name, kMaxPathBytes) ||
        !reader.ReadString(settings.gameName, kMaxPathBytes) ||
        !reader.ReadString(settings.category, kMaxPathBytes) ||
        !reader.ReadString(settings.description, kMaxDescriptionBytes) ||
        !reader.ReadString(settings.defaultMap, kMaxPathBytes) ||
        !reader.ReadString(settings.lastOpenMap, kMaxPathBytes) || !reader.ReadUInt32(lightingPath) ||
        !reader.ReadUInt8(inputEnabled) || inputEnabled > 1U ||
        !reader.ReadString(settings.inputMappingContext, kMaxPathBytes) ||
        !reader.ReadString(settings.physicsLayersAsset, kMaxPathBytes)) {
        return false;
    }
    settings.lightingPath = static_cast<kb::project::ProjectSceneLightingPath>(lightingPath);
    settings.inputEnabled = inputEnabled != 0U;
    return true;
}

} // namespace

std::string_view ToString(RuntimeAssetManifestStatus status) noexcept {
    switch (status) {
    case RuntimeAssetManifestStatus::Success: return "Success";
    case RuntimeAssetManifestStatus::Malformed: return "Malformed";
    case RuntimeAssetManifestStatus::UnsupportedVersion: return "UnsupportedVersion";
    case RuntimeAssetManifestStatus::InvalidProfile: return "InvalidProfile";
    case RuntimeAssetManifestStatus::InvalidProject: return "InvalidProject";
    case RuntimeAssetManifestStatus::InvalidAsset: return "InvalidAsset";
    case RuntimeAssetManifestStatus::DuplicateEntry: return "DuplicateEntry";
    case RuntimeAssetManifestStatus::TooLarge: return "TooLarge";
    }
    return "Unknown";
}

RuntimeAssetManifestStatus EncodeRuntimeAssetManifest(
    const RuntimeAssetManifest& manifest,
    std::vector<std::uint8_t>& out) {
    RuntimeAssetManifest canonical = manifest;
    const RuntimeAssetManifestStatus validation = CanonicalizeAndValidate(canonical);
    if (validation != RuntimeAssetManifestStatus::Success) {
        return validation;
    }

    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
    PutUInt32(bytes, kRuntimeAssetManifestVersion);
    PutUInt32(bytes, 0U);
    PutString(bytes, canonical.targetProfileId);
    PutUInt64(bytes, canonical.targetProfileHash);
    EncodeDescriptor(bytes, canonical.descriptor);
    EncodeSettings(bytes, canonical.settings);
    PutUInt32(bytes, static_cast<std::uint32_t>(canonical.assets.size()));
    for (const RuntimeAssetManifestEntry& asset : canonical.assets) {
        PutUInt64(bytes, asset.id.value);
        PutString(bytes, asset.type);
        PutString(bytes, asset.importCategory);
        PutString(bytes, asset.browseTag);
        PutString(bytes, asset.name);
        PutString(bytes, asset.virtualPath);
        PutString(bytes, asset.sourceExtension);
        PutUInt64(bytes, asset.contentHash);
        PutUInt8(bytes, asset.runtimeLoadable ? 1U : 0U);
        PutUInt32(bytes, static_cast<std::uint32_t>(asset.dependencies.size()));
        for (const AssetId dependency : asset.dependencies) {
            PutUInt64(bytes, dependency.value);
        }
        PutUInt32(bytes, static_cast<std::uint32_t>(asset.artifacts.size()));
        for (const RuntimeArtifactReference& artifact : asset.artifacts) {
            PutUInt64(bytes, artifact.digest.high);
            PutUInt64(bytes, artifact.digest.low);
            PutUInt8(bytes, static_cast<std::uint8_t>(artifact.encoding));
            PutUInt8(bytes, 0U);
            PutUInt8(bytes, 0U);
            PutUInt8(bytes, 0U);
            PutString(bytes, artifact.qualifier);
        }
    }
    PutUInt32(bytes, static_cast<std::uint32_t>(canonical.auxiliaryFiles.size()));
    for (const RuntimeAuxiliaryFileEntry& file : canonical.auxiliaryFiles) {
        PutString(bytes, file.virtualPath);
        PutUInt64(bytes, file.contentHash);
        PutUInt64(bytes, file.artifactDigest.high);
        PutUInt64(bytes, file.artifactDigest.low);
    }
    if (bytes.size() > kMaxManifestBytes || bytes.size() > kMaxAssetPackBlockBytes) {
        return RuntimeAssetManifestStatus::TooLarge;
    }
    out = std::move(bytes);
    return RuntimeAssetManifestStatus::Success;
}

RuntimeAssetManifestStatus DecodeRuntimeAssetManifest(
    std::span<const std::uint8_t> bytes,
    RuntimeAssetManifest& out) {
    try {
    if (bytes.size() > kMaxManifestBytes || bytes.size() > kMaxAssetPackBlockBytes) {
        return RuntimeAssetManifestStatus::TooLarge;
    }
    if (bytes.size() < kMagic.size() + 8U || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        return RuntimeAssetManifestStatus::Malformed;
    }
    Reader reader{ bytes.subspan(kMagic.size()) };
    std::uint32_t version = 0U;
    std::uint32_t reserved = 0U;
    RuntimeAssetManifest manifest{};
    if (!reader.ReadUInt32(version)) {
        return RuntimeAssetManifestStatus::Malformed;
    }
    if (version != kRuntimeAssetManifestVersion) {
        return RuntimeAssetManifestStatus::UnsupportedVersion;
    }
    if (!reader.ReadUInt32(reserved) || reserved != 0U ||
        !reader.ReadString(manifest.targetProfileId, kMaxShortStringBytes) ||
        !reader.ReadUInt64(manifest.targetProfileHash)) {
        return RuntimeAssetManifestStatus::Malformed;
    }
    const RuntimeAssetManifestStatus descriptorStatus = DecodeDescriptor(reader, manifest.descriptor);
    if (descriptorStatus != RuntimeAssetManifestStatus::Success) {
        return descriptorStatus;
    }
    if (!DecodeSettings(reader, manifest.settings)) {
        return RuntimeAssetManifestStatus::Malformed;
    }

    std::uint32_t assetCount = 0U;
    RuntimeAssetManifestStatus countStatus = ReadBoundedCount(reader, assetCount, kMaxAssets, 49U);
    if (countStatus != RuntimeAssetManifestStatus::Success) {
        return countStatus;
    }
    manifest.assets.resize(assetCount);
    for (RuntimeAssetManifestEntry& asset : manifest.assets) {
        std::uint32_t dependencyCount = 0U;
        std::uint32_t artifactCount = 0U;
        std::uint8_t runtimeLoadable = 0U;
        if (!reader.ReadUInt64(asset.id.value) || !reader.ReadString(asset.type, kMaxShortStringBytes) ||
            !reader.ReadString(asset.importCategory, kMaxShortStringBytes) ||
            !reader.ReadString(asset.browseTag, kMaxShortStringBytes) ||
            !reader.ReadString(asset.name, kMaxShortStringBytes) ||
            !reader.ReadString(asset.virtualPath, kMaxPathBytes) ||
            !reader.ReadString(asset.sourceExtension, kMaxShortStringBytes) ||
            !reader.ReadUInt64(asset.contentHash) || !reader.ReadUInt8(runtimeLoadable) ||
            runtimeLoadable > 1U) {
            return RuntimeAssetManifestStatus::Malformed;
        }
        countStatus = ReadBoundedCount(reader, dependencyCount, kMaxDependenciesPerAsset, 8U);
        if (countStatus != RuntimeAssetManifestStatus::Success) {
            return countStatus;
        }
        asset.runtimeLoadable = runtimeLoadable != 0U;
        asset.dependencies.resize(dependencyCount);
        for (AssetId& dependency : asset.dependencies) {
            if (!reader.ReadUInt64(dependency.value)) {
                return RuntimeAssetManifestStatus::Malformed;
            }
        }
        countStatus = ReadBoundedCount(reader, artifactCount, kMaxArtifactsPerAsset, 24U);
        if (countStatus != RuntimeAssetManifestStatus::Success) {
            return countStatus;
        }
        asset.artifacts.resize(artifactCount);
        for (RuntimeArtifactReference& artifact : asset.artifacts) {
            std::uint8_t encoding = 0U;
            std::uint8_t reserved0 = 0U;
            std::uint8_t reserved1 = 0U;
            std::uint8_t reserved2 = 0U;
            if (!reader.ReadUInt64(artifact.digest.high) || !reader.ReadUInt64(artifact.digest.low) ||
                !reader.ReadUInt8(encoding) || !reader.ReadUInt8(reserved0) ||
                !reader.ReadUInt8(reserved1) || !reader.ReadUInt8(reserved2) ||
                !reader.ReadString(artifact.qualifier, kMaxShortStringBytes) ||
                reserved0 != 0U || reserved1 != 0U || reserved2 != 0U) {
                return RuntimeAssetManifestStatus::Malformed;
            }
            artifact.encoding = static_cast<RuntimeArtifactEncoding>(encoding);
        }
    }

    std::uint32_t auxiliaryCount = 0U;
    countStatus = ReadBoundedCount(reader, auxiliaryCount, kMaxAuxiliaryFiles, 28U);
    if (countStatus != RuntimeAssetManifestStatus::Success) {
        return countStatus;
    }
    manifest.auxiliaryFiles.resize(auxiliaryCount);
    for (RuntimeAuxiliaryFileEntry& file : manifest.auxiliaryFiles) {
        if (!reader.ReadString(file.virtualPath, kMaxPathBytes) || !reader.ReadUInt64(file.contentHash) ||
            !reader.ReadUInt64(file.artifactDigest.high) || !reader.ReadUInt64(file.artifactDigest.low)) {
            return RuntimeAssetManifestStatus::Malformed;
        }
    }
    if (!reader.Finished()) {
        return RuntimeAssetManifestStatus::Malformed;
    }

    const RuntimeAssetManifestStatus validation = CanonicalizeAndValidate(manifest);
    if (validation != RuntimeAssetManifestStatus::Success) {
        return validation;
    }
    out = std::move(manifest);
    return RuntimeAssetManifestStatus::Success;
    } catch (const std::bad_alloc&) {
        return RuntimeAssetManifestStatus::TooLarge;
    } catch (const std::length_error&) {
        return RuntimeAssetManifestStatus::TooLarge;
    } catch (const std::exception&) {
        // Filesystem path conversion and normalization are allowed to reject hostile byte
        // sequences. A decoder is a trust boundary: such input is malformed, never an
        // exception that may escape into the runtime mount path.
        return RuntimeAssetManifestStatus::Malformed;
    }
}

bool EncodeRuntimeSourceBlob(
    std::span<const std::uint8_t> sourceBytes,
    std::vector<std::uint8_t>& out) {
    if (sourceBytes.size() > kMaxAssetPackBlockBytes - kSourceMagic.size() - sizeof(std::uint64_t)) {
        return false;
    }
    std::vector<std::uint8_t> blob;
    blob.reserve(kSourceMagic.size() + sizeof(std::uint64_t) + sourceBytes.size());
    blob.insert(blob.end(), kSourceMagic.begin(), kSourceMagic.end());
    PutUInt64(blob, sourceBytes.size());
    blob.insert(blob.end(), sourceBytes.begin(), sourceBytes.end());
    out = std::move(blob);
    return true;
}

bool DecodeRuntimeSourceBlob(
    std::span<const std::uint8_t> blob,
    std::span<const std::uint8_t>& sourceBytes) noexcept {
    sourceBytes = {};
    if (blob.size() < kSourceMagic.size() + sizeof(std::uint64_t) ||
        !std::equal(kSourceMagic.begin(), kSourceMagic.end(), blob.begin())) {
        return false;
    }
    std::uint64_t declared = 0U;
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        declared |= static_cast<std::uint64_t>(blob[kSourceMagic.size() + shift / 8U]) << shift;
    }
    const std::size_t payloadOffset = kSourceMagic.size() + sizeof(std::uint64_t);
    if (declared != blob.size() - payloadOffset) {
        return false;
    }
    sourceBytes = blob.subspan(payloadOffset);
    return true;
}

} // namespace kb::assets::bake
