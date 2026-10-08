#include "engine/assets/ImportedAssetLoader.hpp"

#include "engine/assets/AssetMemoryInputStream.hpp"
#include "engine/assets/ImportedAsset.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <utility>

namespace kb::assets {
namespace {

constexpr std::array<char, 8> AssetMagic{ '2', '1', 'K', 'B', 'A', 'S', 'T', '\0' };
constexpr std::uint32_t SupportedVersion = 1U;
// Version 2 adds, after the names, the table of files the source referenced (AssetImportService).
constexpr std::uint32_t ResourcesVersion = 2U;
constexpr std::uint32_t MaximumResourceCount = 4096U;

[[nodiscard]] bool ReadU16(std::istream& input, std::uint16_t& value) {
    value = 0;
    for (int shift = 0; shift < 16; shift += 8) {
        const int byte = input.get();
        if (byte == EOF) {
            return false;
        }
        value |= static_cast<std::uint16_t>(static_cast<unsigned char>(byte) << shift);
    }
    return true;
}

[[nodiscard]] bool ReadU32(std::istream& input, std::uint32_t& value) {
    value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const int byte = input.get();
        if (byte == EOF) {
            return false;
        }
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(byte) << shift);
    }
    return true;
}

[[nodiscard]] bool ReadU64(std::istream& input, std::uint64_t& value) {
    value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        const int byte = input.get();
        if (byte == EOF) {
            return false;
        }
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(byte)) << shift;
    }
    return true;
}

[[nodiscard]] bool ReadString(std::istream& input, std::string& output) {
    std::uint32_t size = 0;
    if (!ReadU32(input, size) || size > 1024U * 1024U) {
        return false;
    }
    output.resize(size);
    input.read(output.data(), static_cast<std::streamsize>(output.size()));
    return input.good();
}

[[nodiscard]] bool ReadResources(
    std::istream& input,
    std::size_t streamSize,
    std::vector<ImportedAssetResource>& resources) {
    std::uint32_t count = 0;
    if (!ReadU32(input, count) || count > MaximumResourceCount) {
        return false;
    }
    resources.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        ImportedAssetResource resource;
        std::uint64_t size = 0;
        if (!ReadString(input, resource.uri) || resource.uri.empty() || !ReadU64(input, size)) {
            return false;
        }
        const std::streamoff position = input.tellg();
        if (position < 0 || static_cast<std::uint64_t>(position) > streamSize ||
            size > streamSize - static_cast<std::uint64_t>(position)) {
            return false;
        }
        resource.bytes.resize(static_cast<std::size_t>(size));
        input.read(reinterpret_cast<char*>(resource.bytes.data()), static_cast<std::streamsize>(size));
        if (!input.good() && size != 0U) {
            return false;
        }
        resources.push_back(std::move(resource));
    }
    return true;
}

} // namespace

std::string_view ImportedAssetLoader::Type() const noexcept {
    return "ImportedAsset";
}

std::type_index ImportedAssetLoader::PayloadType() const noexcept {
    return typeid(ImportedAsset);
}

std::vector<std::string> ImportedAssetLoader::Extensions() const {
    return { ".21kb" };
}

AssetLoadResult ImportedAssetLoader::Load(const AssetLoadRequest& request) {
    std::vector<std::uint8_t> sourceBytes;
    std::string error;
    if (!request.ReadSourceBytes(sourceBytes, error)) {
        return AssetLoadResult{ .asset = {}, .error = std::move(error) };
    }
    AssetMemoryInputStream input{ sourceBytes };

    std::array<char, AssetMagic.size()> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (magic != AssetMagic) {
        return AssetLoadResult{ .asset = {}, .error = "Imported asset magic is invalid." };
    }

    std::uint32_t version = 0;
    std::uint16_t category = 0;
    std::uint16_t flags = 0;
    std::uint64_t sourceSize = 0;
    std::uint64_t sourceHash = 0;
    ImportedAsset imported;
    if (!ReadU32(input, version)
        || (version != SupportedVersion && version != ResourcesVersion)
        || !ReadU16(input, category)
        || !ReadU16(input, flags)
        || !ReadU64(input, sourceSize)
        || !ReadU64(input, sourceHash)
        || !ReadString(input, imported.sourceName)
        || !ReadString(input, imported.sourceExtension)) {
        return AssetLoadResult{ .asset = {}, .error = "Imported asset header is invalid." };
    }
    if (version == ResourcesVersion && !ReadResources(input, sourceBytes.size(), imported.resources)) {
        return AssetLoadResult{ .asset = {}, .error = "Imported asset resource table is invalid." };
    }

    imported.category = static_cast<AssetImportCategory>(category);
    imported.sourceSize = sourceSize;
    imported.sourceHash = sourceHash;
    imported.importOptions = flags;
    std::vector<char> payload{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    imported.payload.resize(payload.size());
    std::ranges::transform(payload, imported.payload.begin(), [](char value) {
        return static_cast<std::byte>(static_cast<unsigned char>(value));
    });
    return AssetLoadResult{ .asset = std::make_shared<ImportedAsset>(std::move(imported)), .error = {} };
}

} // namespace kb::assets
