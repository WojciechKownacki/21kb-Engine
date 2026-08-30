#include "engine/assets/IAssetLoader.hpp"

#include "engine/assets/bake/RuntimeAssetPack.hpp"

#include <fstream>
#include <limits>
#include <utility>

namespace kb::assets {
namespace {

[[nodiscard]] bool ReadLooseSourceBytes(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>& out,
    std::string& error) {
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
    if (sizeError || size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        error = "Asset source size could not be read";
        return false;
    }
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        error = "Asset source could not be opened";
        return false;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size), 0U);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if ((!bytes.empty() && input.gcount() != static_cast<std::streamsize>(bytes.size())) || input.bad()) {
        error = "Asset source could not be read completely";
        return false;
    }
    out = std::move(bytes);
    return true;
}

[[nodiscard]] bool ReadPackagedSourceBytes(
    const std::shared_ptr<bake::RuntimeAssetPack>& runtimePack,
    AssetId id,
    std::vector<std::uint8_t>& out,
    std::string& error) {
    bake::RuntimeAssetPayload payload{};
    const bake::RuntimeAssetPackStatus status = runtimePack->ReadAssetPayload(
        id,
        bake::RuntimeArtifactEncoding::SourceBytes,
        {},
        payload);
    if (status != bake::RuntimeAssetPackStatus::Success || payload.blocks.size() != 1U) {
        error = "Packaged source asset read failed: " + std::string{ bake::ToString(status) };
        return false;
    }
    out = std::move(payload.blocks.front().bytes);
    return true;
}

} // namespace

bool AssetLoadRequest::ReadSourceBytes(
    std::vector<std::uint8_t>& out,
    std::string& error) const {
    error.clear();
    if (runtimePack != nullptr) {
        return ReadPackagedSourceBytes(runtimePack, metadata.id, out, error);
    }
    return ReadLooseSourceBytes(resolvedPath, out, error);
}

bool AssetLoadRequest::ReadDependencySourceBytes(
    const AssetMetadata& dependency,
    std::vector<std::uint8_t>& out,
    std::string& error) const {
    error.clear();
    if (runtimePack != nullptr) {
        return ReadPackagedSourceBytes(runtimePack, dependency.id, out, error);
    }
    return ReadLooseSourceBytes(dependency.physicalPath, out, error);
}

bool AssetLoadRequest::ReadPackagedPayload(
    bake::RuntimeArtifactEncoding encoding,
    std::string_view qualifier,
    bake::RuntimeAssetPayload& out,
    std::string& error) const {
    error.clear();
    if (runtimePack == nullptr) {
        error = "Asset load request is not backed by a runtime package";
        return false;
    }
    const bake::RuntimeAssetPackStatus status =
        runtimePack->ReadAssetPayload(metadata.id, encoding, qualifier, out);
    if (status != bake::RuntimeAssetPackStatus::Success) {
        error = "Packaged asset read failed: " + std::string{ bake::ToString(status) };
        return false;
    }
    return true;
}

} // namespace kb::assets
