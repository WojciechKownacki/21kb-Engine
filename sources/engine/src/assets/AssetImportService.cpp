#include "engine/assets/AssetImportService.hpp"

#include "assets/AssetFileSystem.hpp"
#include "assets/AssetPathUtilities.hpp"
#include "engine/assets/AssetImportCatalog.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/GltfExternalResources.hpp"
#include "engine/assets/ImportedAsset.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <system_error>

namespace kb::assets {
namespace {

constexpr std::array<char, 8> AssetMagic{ '2', '1', 'K', 'B', 'A', 'S', 'T', '\0' };
constexpr std::array<char, 8> MetaMagic{ '2', '1', 'K', 'B', 'M', 'E', 'T', 'A' };
constexpr std::uint32_t ImportFormatVersion = 1U;
// An asset container that also carries the files its source referenced (ImportedAssetResource):
// the version-1 header and names, then a resource table, then the payload.
constexpr std::uint32_t ImportContainerWithResourcesVersion = 2U;
constexpr std::uint64_t FnvPrime = 1099511628211ULL;
[[nodiscard]] std::string SanitizeStem(std::string name) {
    for (char& character : name) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (!std::isalnum(value) && character != '-' && character != '_') {
            character = '_';
        }
    }
    return name.empty() ? std::string{ "ImportedAsset" } : name;
}

[[nodiscard]] std::filesystem::path MetaPathForAssetPath(std::filesystem::path assetPath) {
    assetPath.replace_extension(".meta");
    return assetPath;
}

[[nodiscard]] std::filesystem::path UniqueAssetPathWithMeta(
    const std::filesystem::path& folder,
    std::string baseName) {
    const std::string stem = SanitizeStem(std::move(baseName));
    for (int index = 0; index < 10000; ++index) {
        const std::string suffix = index == 0 ? std::string{} : "_" + std::to_string(index);
        const std::filesystem::path assetPath = (folder / (stem + suffix + ".21kb")).lexically_normal();
        const std::filesystem::path metaPath = MetaPathForAssetPath(assetPath);
        std::error_code error;
        const bool assetExists = std::filesystem::exists(assetPath, error);
        error.clear();
        const bool metaExists = std::filesystem::exists(metaPath, error);
        if (!assetExists && !metaExists) {
            return assetPath;
        }
    }
    return {};
}

void WriteU16(std::ostream& output, std::uint16_t value) {
    output.put(static_cast<char>(value & 0xFFU));
    output.put(static_cast<char>((value >> 8U) & 0xFFU));
}

void WriteU32(std::ostream& output, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        output.put(static_cast<char>((value >> shift) & 0xFFU));
    }
}

void WriteU64(std::ostream& output, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        output.put(static_cast<char>((value >> shift) & 0xFFULL));
    }
}

[[nodiscard]] bool ReadU16(std::istream& input, std::uint16_t& value) {
    char bytes[2]{};
    input.read(bytes, 2);
    if (!input.good()) {
        return false;
    }
    value = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[0])) |
        (static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[1])) << 8U));
    return true;
}

[[nodiscard]] bool ReadU32(std::istream& input, std::uint32_t& value) {
    char bytes[4]{};
    input.read(bytes, 4);
    if (!input.good()) {
        return false;
    }
    value = 0U;
    for (int index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index])) << (index * 8);
    }
    return true;
}

[[nodiscard]] bool ReadU64(std::istream& input, std::uint64_t& value) {
    char bytes[8]{};
    input.read(bytes, 8);
    if (!input.good()) {
        return false;
    }
    value = 0U;
    for (int index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[index])) << (index * 8);
    }
    return true;
}

[[nodiscard]] bool WriteString(std::ostream& output, std::string_view text) {
    if (text.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    WriteU32(output, static_cast<std::uint32_t>(text.size()));
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}

[[nodiscard]] bool ReadString(std::istream& input, std::string& text) {
    std::uint32_t length = 0U;
    if (!ReadU32(input, length) || length > 65536U) {
        return false;
    }
    text.assign(length, '\0');
    input.read(text.data(), static_cast<std::streamsize>(length));
    return input.good();
}

[[nodiscard]] bool CopyPayload(std::ostream& output, const std::filesystem::path& sourcePath) {
    std::ifstream input{ sourcePath, std::ios::binary };
    if (!input.is_open()) {
        return false;
    }
    output << input.rdbuf();
    return output.good() && !input.bad();
}

[[nodiscard]] bool WriteResources(std::ostream& output, std::span<const ImportedAssetResource> resources) {
    WriteU32(output, static_cast<std::uint32_t>(resources.size()));
    for (const ImportedAssetResource& resource : resources) {
        if (!WriteString(output, resource.uri)) {
            return false;
        }
        WriteU64(output, static_cast<std::uint64_t>(resource.bytes.size()));
        output.write(reinterpret_cast<const char*>(resource.bytes.data()), static_cast<std::streamsize>(resource.bytes.size()));
    }
    return output.good();
}

// The source hash of a document together with the files it references, so a changed .bin makes
// a .gltf import stale even when the .gltf itself is unchanged.
[[nodiscard]] std::uint64_t HashWithResources(std::uint64_t hash, std::span<const ImportedAssetResource> resources) {
    const auto mix = [&hash](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0U; index < size; ++index) {
            hash ^= bytes[index];
            hash *= FnvPrime;
        }
    };
    for (const ImportedAssetResource& resource : resources) {
        mix(resource.uri.data(), resource.uri.size());
        const unsigned char separator = 0U;
        mix(&separator, 1U);
        mix(resource.bytes.data(), resource.bytes.size());
    }
    return hash;
}

[[nodiscard]] bool IsGltfModel(const std::filesystem::path& sourcePath, AssetImportCategory category) {
    const std::string extension = AssetPathUtilities::LowerExtension(sourcePath.extension());
    return category == AssetImportCategory::Model && (extension == ".gltf" || extension == ".glb");
}

[[nodiscard]] std::optional<std::vector<ImportedAssetResource>> CollectSourceResources(
    const std::filesystem::path& sourcePath,
    AssetImportCategory category,
    std::string& error) {
    if (!IsGltfModel(sourcePath, category)) {
        return std::vector<ImportedAssetResource>{};
    }
    std::ifstream input{ sourcePath, std::ios::binary };
    if (!input.is_open()) {
        error = "Source file could not be read.";
        return std::nullopt;
    }
    const std::string document{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    if (input.bad()) {
        error = "Source file could not be read.";
        return std::nullopt;
    }
    return CollectGltfExternalBuffers(
        std::span<const std::uint8_t>{ reinterpret_cast<const std::uint8_t*>(document.data()), document.size() },
        sourcePath.parent_path(),
        &error);
}

[[nodiscard]] bool WriteAssetContainer(
    const std::filesystem::path& outputPath,
    const std::filesystem::path& sourcePath,
    AssetImportCategory category,
    std::uint64_t sourceSize,
    std::uint64_t sourceHash,
    std::uint16_t importOptions,
    std::span<const ImportedAssetResource> resources) {
    const std::filesystem::path tempPath = outputPath.string() + ".tmp";
    bool wrote = false;
    {
        std::ofstream output{ tempPath, std::ios::binary | std::ios::trunc };
        if (!output.is_open()) {
            return false;
        }
        output.write(AssetMagic.data(), static_cast<std::streamsize>(AssetMagic.size()));
        WriteU32(output, resources.empty() ? ImportFormatVersion : ImportContainerWithResourcesVersion);
        WriteU16(output, static_cast<std::uint16_t>(category));
        WriteU16(output, importOptions);
        WriteU64(output, sourceSize);
        WriteU64(output, sourceHash);
        wrote = WriteString(output, sourcePath.filename().string())
            && WriteString(output, sourcePath.extension().string())
            && (resources.empty() || WriteResources(output, resources))
            && CopyPayload(output, sourcePath)
            && output.good();
    }

    if (!wrote) {
        std::error_code cleanupError;
        std::filesystem::remove(tempPath, cleanupError);
        return false;
    }

    std::error_code error;
    std::filesystem::rename(tempPath, outputPath, error);
    if (error) {
        std::filesystem::remove(tempPath, error);
        return false;
    }
    return true;
}

[[nodiscard]] bool WriteMeta(
    const std::filesystem::path& metaPath,
    AssetId id,
    const std::filesystem::path& virtualPath,
    const std::filesystem::path& sourcePath,
    AssetImportCategory category,
    std::uint64_t sourceSize,
    std::uint64_t sourceHash,
    std::uint64_t assetHash) {
    const std::filesystem::path tempPath = metaPath.string() + ".tmp";
    bool wrote = false;
    {
        std::ofstream output{ tempPath, std::ios::binary | std::ios::trunc };
        if (!output.is_open()) {
            return false;
        }
        output.write(MetaMagic.data(), static_cast<std::streamsize>(MetaMagic.size()));
        WriteU32(output, ImportFormatVersion);
        WriteU64(output, id.value);
        WriteU16(output, static_cast<std::uint16_t>(category));
        WriteU16(output, 0U);
        WriteU64(output, sourceSize);
        WriteU64(output, sourceHash);
        WriteU64(output, assetHash);
        wrote = WriteString(output, NormalizeAssetPath(virtualPath))
            && WriteString(output, sourcePath.filename().string())
            && WriteString(output, sourcePath.extension().string())
            && WriteString(output, std::string{ ToString(category) })
            && output.good();
    }

    if (!wrote) {
        std::error_code cleanupError;
        std::filesystem::remove(tempPath, cleanupError);
        return false;
    }

    std::error_code error;
    std::filesystem::rename(tempPath, metaPath, error);
    if (error) {
        std::filesystem::remove(tempPath, error);
        return false;
    }
    return true;
}

struct ImportedAssetHeader {
    AssetImportCategory category = AssetImportCategory::Unknown;
    std::uint64_t sourceSize = 0U;
    std::uint64_t sourceHash = 0U;
    std::uint16_t importOptions = 0U;
    std::string sourceName;
    std::string sourceExtension;
};

[[nodiscard]] std::optional<ImportedAssetHeader> ReadImportedAssetHeader(const std::filesystem::path& assetPath) {
    std::ifstream input{ assetPath, std::ios::binary };
    if (!input.is_open()) {
        return std::nullopt;
    }

    std::array<char, AssetMagic.size()> magic{};
    std::uint32_t version = 0U;
    std::uint16_t category = 0U;
    std::uint16_t flags = 0U;
    ImportedAssetHeader header{};
    if (!input.read(magic.data(), static_cast<std::streamsize>(magic.size())) ||
        magic != AssetMagic ||
        !ReadU32(input, version) ||
        (version != ImportFormatVersion && version != ImportContainerWithResourcesVersion) ||
        !ReadU16(input, category) ||
        !ReadU16(input, flags) ||
        !ReadU64(input, header.sourceSize) ||
        !ReadU64(input, header.sourceHash) ||
        !ReadString(input, header.sourceName) ||
        !ReadString(input, header.sourceExtension)) {
        return std::nullopt;
    }

    header.category = static_cast<AssetImportCategory>(category);
    header.importOptions = flags;
    return header;
}

[[nodiscard]] bool SameVirtualFolder(const std::filesystem::path& left, const std::filesystem::path& right) {
    return NormalizeAssetPath(left) == NormalizeAssetPath(right);
}

[[nodiscard]] const AssetMetadata* FindReusableImportedAsset(
    const AssetManager& manager,
    const std::filesystem::path& destinationVirtualFolder,
    const std::filesystem::path& sourcePath,
    AssetImportCategory category,
    std::uint64_t sourceHash,
    std::uint16_t importOptions) {
    for (const AssetMetadata& metadata : manager.Registry().All()) {
        if (metadata.type != RuntimeAssetType(category) ||
            metadata.importCategory != ToString(category) ||
            !SameVirtualFolder(metadata.virtualPath.parent_path(), destinationVirtualFolder) ||
            metadata.physicalPath.empty()) {
            continue;
        }

        const std::optional<ImportedAssetHeader> header = ReadImportedAssetHeader(metadata.physicalPath);
        if (header.has_value() &&
            header->category == category &&
            header->sourceHash == sourceHash &&
            header->importOptions == importOptions &&
            header->sourceName == sourcePath.filename().string() &&
            header->sourceExtension == sourcePath.extension().string()) {
            return &metadata;
        }
    }
    return nullptr;
}

[[nodiscard]] AssetImportItemResult ImportOne(
    AssetManager& manager,
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& destinationFolder,
    const std::filesystem::path& destinationVirtualFolder,
    const AssetImportOptions& options) {
    AssetImportItemResult result;
    result.sourcePath = sourcePath;
    result.category = AssetImportCatalog::ClassifyExtension(sourcePath.extension());

    std::error_code error;
    if (sourcePath.empty() || !std::filesystem::exists(sourcePath, error)) {
        result.error = "Source file is not a regular file.";
        result.status = AssetImportItemStatus::Missing;
        return result;
    }
    if (!std::filesystem::is_regular_file(sourcePath, error)) {
        result.error = "Source file is not a regular file.";
        result.status = AssetImportItemStatus::Failed;
        return result;
    }
    if (AssetImportCatalog::IsMetaExtension(sourcePath.extension())) {
        result.error = "Meta sidecar files cannot be imported as source assets.";
        result.status = AssetImportItemStatus::Unsupported;
        return result;
    }
    if (result.category == AssetImportCategory::Unknown) {
        result.error = "Unsupported source asset extension.";
        result.status = AssetImportItemStatus::Unsupported;
        return result;
    }

    std::filesystem::create_directories(destinationFolder, error);
    if (error || !std::filesystem::is_directory(destinationFolder, error)) {
        result.error = "Destination folder could not be prepared.";
        result.status = AssetImportItemStatus::Failed;
        return result;
    }

    std::string resourceError;
    const std::optional<std::vector<ImportedAssetResource>> resources =
        CollectSourceResources(sourcePath, result.category, resourceError);
    if (!resources.has_value()) {
        result.error = resourceError.empty() ? std::string{ "Referenced source files could not be read." } : resourceError;
        result.status = AssetImportItemStatus::Failed;
        return result;
    }
    result.sourceHash = HashWithResources(AssetFileSystem::HashFile(sourcePath), *resources);
    const std::uint64_t sourceSize = static_cast<std::uint64_t>(std::filesystem::file_size(sourcePath, error));
    if (error) {
        result.error = "Source file could not be measured.";
        result.status = AssetImportItemStatus::Failed;
        return result;
    }

    const std::uint16_t importOptions = AssetImportOptionFlags(options);
    if (const AssetMetadata* reusable = FindReusableImportedAsset(manager, destinationVirtualFolder, sourcePath, result.category, result.sourceHash, importOptions);
        reusable != nullptr) {
        result.id = reusable->id;
        result.assetPhysicalPath = reusable->physicalPath;
        result.metaPhysicalPath = MetaPathForAssetPath(reusable->physicalPath);
        result.virtualPath = reusable->virtualPath;
        result.assetHash = reusable->contentHash;
        result.status = AssetImportItemStatus::Reused;
        return result;
    }

    result.assetPhysicalPath = UniqueAssetPathWithMeta(destinationFolder, sourcePath.stem().string());
    if (result.assetPhysicalPath.empty()) {
        result.error = "Unique destination asset path could not be created.";
        result.status = AssetImportItemStatus::Failed;
        return result;
    }
    result.metaPhysicalPath = MetaPathForAssetPath(result.assetPhysicalPath);
    result.virtualPath = destinationVirtualFolder / result.assetPhysicalPath.filename();
    const std::string runtimeType{ RuntimeAssetType(result.category) };
    result.id = MakeAssetId(NormalizeAssetPath(result.virtualPath) + ":" + runtimeType);

    if (!WriteAssetContainer(result.assetPhysicalPath, sourcePath, result.category, sourceSize, result.sourceHash, importOptions, *resources)) {
        result.error = "Imported asset container could not be written.";
        result.status = AssetImportItemStatus::Failed;
        return result;
    }
    result.assetHash = AssetFileSystem::HashFile(result.assetPhysicalPath);
    if (!WriteMeta(result.metaPhysicalPath, result.id, result.virtualPath, sourcePath, result.category, sourceSize, result.sourceHash, result.assetHash)) {
        std::filesystem::remove(result.assetPhysicalPath, error);
        result.error = "Imported asset meta could not be written.";
        result.status = AssetImportItemStatus::Failed;
        return result;
    }

    static_cast<void>(manager.RegisterAsset(AssetMetadata{
        .id = result.id,
        .type = runtimeType,
        .importCategory = std::string{ ToString(result.category) },
        .name = result.assetPhysicalPath.stem().string(),
        .virtualPath = result.virtualPath,
        .physicalPath = result.assetPhysicalPath,
        .sourceExtension = AssetPathUtilities::LowerExtension(result.assetPhysicalPath.extension()),
        .contentHash = result.assetHash,
        .dependencies = {},
        .runtimeLoadable = true,
    }));
    result.status = AssetImportItemStatus::Created;
    return result;
}

} // namespace

AssetImportResult AssetImportService::ImportFiles(
    AssetManager& manager,
    std::span<const std::filesystem::path> sourceFiles,
    const std::filesystem::path& destinationVirtualFolder,
    const AssetImportOptions& options) {
    AssetImportResult result;
    result.items.reserve(sourceFiles.size());

    const std::optional<std::filesystem::path> destinationFolder = AssetPathUtilities::ResolveMountedFolderRoot(manager.Mounts(), destinationVirtualFolder);
    if (!destinationFolder.has_value()) {
        for (const std::filesystem::path& sourceFile : sourceFiles) {
            result.items.push_back(AssetImportItemResult{
                .sourcePath = sourceFile,
                .category = AssetImportCatalog::ClassifyExtension(sourceFile.extension()),
                .status = AssetImportItemStatus::Failed,
                .error = "Destination virtual folder is not mounted.",
            });
        }
        return result;
    }

    for (const std::filesystem::path& sourceFile : sourceFiles) {
        result.items.push_back(ImportOne(manager, sourceFile, *destinationFolder, destinationVirtualFolder, options));
    }
    return result;
}

} // namespace kb::assets
