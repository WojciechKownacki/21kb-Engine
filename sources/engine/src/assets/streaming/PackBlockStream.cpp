#include "engine/assets/streaming/PackBlockStream.hpp"

#include <string>
#include <utility>

namespace kb::assets::streaming {

BackgroundRequestHandle ReadPackBlockAsync(
    BackgroundLoadService& reader,
    std::shared_ptr<const kb::assets::bake::RuntimeAssetPack> pack,
    const kb::assets::bake::AssetBakeDigest& artifact,
    std::string_view blockName,
    BackgroundPriority priority,
    kb::assets::bake::AssetPackReadStatus& status) {
    using kb::assets::bake::AssetPackReadStatus;
    if (pack == nullptr || !pack->IsMounted()) {
        status = AssetPackReadStatus::NotMounted;
        return nullptr;
    }
    kb::assets::bake::RuntimeAssetBlockLocation location{};
    status = pack->LocateArtifactBlock(artifact, blockName, location);
    if (status != AssetPackReadStatus::Success) {
        return nullptr;
    }
    const std::uint64_t offset = location.block->offset;
    const std::uint64_t length = location.block->storedBytes;
    const std::span<const std::uint8_t> resident = pack->ContainerResidentBytes(location.container);
    const std::filesystem::path path = pack->ContainerPath(location.container);
    BackgroundReadTransform decode = [pack, location](std::vector<std::uint8_t>& bytes, std::string& error) {
        const AssetPackReadStatus decoded = pack->DecodeStoredArtifactBlock(location, bytes);
        if (decoded != AssetPackReadStatus::Success) {
            error = "pack block refused: " + std::string{ kb::assets::bake::ToString(decoded) };
            return false;
        }
        return true;
    };
    if (!resident.empty()) {
        return reader.ReadMemory(resident, offset, length, priority, std::move(decode));
    }
    return reader.Read(path, offset, length, priority, std::move(decode));
}

} // namespace kb::assets::streaming
