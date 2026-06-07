#include "engine/assets/AssetImportService.hpp"

#include "assets/AssetFileSystem.hpp"
#include "assets/AssetPathUtilities.hpp"
#include "engine/assets/AssetImportCatalog.hpp"
#include "engine/assets/AssetManager.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <system_error>

namespace kb::assets {
namespace {

constexpr std::array<char, 8> AssetMagic{ '2', '1', 'K', 'B', 'A', 'S', 'T', '\0' };
constexpr std::array<char, 8> MetaMagic{ '2', '1', 'K', 'B', 'M', 'E', 'T', 'A' };
constexpr std::uint32_t ImportFormatVersion = 1U;
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

[[nodiscard]] bool WriteString(std::ostream& output, std::string_view text) {
    if (text.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    WriteU32(output, static_cast<std::uint32_t>(text.size()));
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}

[[nodiscard]] bool CopyPayload(std::ostream& output, const std::filesystem::path& sourcePath) {
    std::ifstream input{ sourcePath, std::ios::binary };
    if (!input.is_open()) {
        return false;
    }
    output << input.rdbuf();
    return output.good() && !input.bad();
}

[[nodiscard]] bool WriteAssetContainer(
    const std::filesystem::path& outputPath,
    const std::filesystem::path& sourcePath,
    AssetImportCategory category,
    std::uint64_t sourceSize,
    std::uint64_t sourceHash) {
    const std::filesystem::path tempPath = outputPath.string() + ".tmp";
    bool wrote = false;
    {
        std::ofstream output{ tempPath, std::ios::binary | std::ios::trunc };
        if (!output.is_open()) {
            return false;
        }
        output.write(AssetMagic.data(), static_cast<std::streamsize>(AssetMagic.size()));
        WriteU32(output, ImportFormatVersion);
        WriteU16(output, static_cast<std::uint16_t>(category));
        WriteU16(output, 0U);
        WriteU64(output, sourceSize);
        WriteU64(output, sourceHash);
        wrote = WriteString(output, sourcePath.filename().string())
            && WriteString(output, sourcePath.extension().string())
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

[[nodiscard]] AssetImportItemResult ImportOne(
    AssetManager& manager,
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& destinationFolder,
    const std::filesystem::path& destinationVirtualFolder) {
    AssetImportItemResult result;
    result.sourcePath = sourcePath;
    result.category = AssetImportCatalog::ClassifyExtension(sourcePath.extension());

    std::error_code error;
    if (sourcePath.empty() || !std::filesystem::is_regular_file(sourcePath, error)) {
        result.error = "Source file is not a regular file.";
        return result;
    }
    if (AssetImportCatalog::IsMetaExtension(sourcePath.extension())) {
        result.error = "Meta sidecar files cannot be imported as source assets.";
        return result;
    }

    std::filesystem::create_directories(destinationFolder, error);
    if (error || !std::filesystem::is_directory(destinationFolder, error)) {
        result.error = "Destination folder could not be prepared.";
        return result;
    }

    result.assetPhysicalPath = UniqueAssetPathWithMeta(destinationFolder, sourcePath.stem().string());
    if (result.assetPhysicalPath.empty()) {
        result.error = "Unique destination asset path could not be created.";
        return result;
    }
    result.metaPhysicalPath = MetaPathForAssetPath(result.assetPhysicalPath);
    result.virtualPath = destinationVirtualFolder / result.assetPhysicalPath.filename();
    const std::string runtimeType{ RuntimeAssetType(result.category) };
    result.id = MakeAssetId(NormalizeAssetPath(result.virtualPath) + ":" + runtimeType);

    result.sourceHash = AssetFileSystem::HashFile(sourcePath);
    const std::uint64_t sourceSize = static_cast<std::uint64_t>(std::filesystem::file_size(sourcePath, error));
    if (error) {
        result.error = "Source file could not be measured.";
        return result;
    }

    if (!WriteAssetContainer(result.assetPhysicalPath, sourcePath, result.category, sourceSize, result.sourceHash)) {
        result.error = "Imported asset container could not be written.";
        return result;
    }
    result.assetHash = AssetFileSystem::HashFile(result.assetPhysicalPath);
    if (!WriteMeta(result.metaPhysicalPath, result.id, result.virtualPath, sourcePath, result.category, sourceSize, result.sourceHash, result.assetHash)) {
        std::filesystem::remove(result.assetPhysicalPath, error);
        result.error = "Imported asset meta could not be written.";
        return result;
    }

    static_cast<void>(manager.RegisterAsset(AssetMetadata{
        .id = result.id,
        .type = runtimeType,
        .importCategory = std::string{ ToString(result.category) },
        .name = result.assetPhysicalPath.stem().string(),
        .virtualPath = result.virtualPath,
        .physicalPath = result.assetPhysicalPath,
        .contentHash = result.assetHash,
        .dependencies = {},
        .runtimeLoadable = true,
    }));
    return result;
}

} // namespace

AssetImportResult AssetImportService::ImportFiles(
    AssetManager& manager,
    std::span<const std::filesystem::path> sourceFiles,
    const std::filesystem::path& destinationVirtualFolder) {
    AssetImportResult result;
    result.items.reserve(sourceFiles.size());

    const std::optional<std::filesystem::path> destinationFolder = AssetPathUtilities::ResolveMountedFolderRoot(manager.Mounts(), destinationVirtualFolder);
    if (!destinationFolder.has_value()) {
        for (const std::filesystem::path& sourceFile : sourceFiles) {
            result.items.push_back(AssetImportItemResult{
                .sourcePath = sourceFile,
                .error = "Destination virtual folder is not mounted.",
            });
        }
        return result;
    }

    for (const std::filesystem::path& sourceFile : sourceFiles) {
        result.items.push_back(ImportOne(manager, sourceFile, *destinationFolder, destinationVirtualFolder));
    }
    return result;
}

} // namespace kb::assets
