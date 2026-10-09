#pragma once

#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/assets/streaming/BackgroundLoadService.hpp"

#include <memory>
#include <string_view>

namespace kb::assets::streaming {

// Starts an asynchronous read of one block of a mounted pack set. A background worker reads the
// block's stored bytes through its own handle (or copies them out of a pack mounted from
// memory) and then verifies them against the seal, decrypts and decompresses them -- exactly
// what RuntimeAssetPack::ReadArtifactBlock does, without the caller's thread taking part. A
// tampered stored block fails the request; the caller never sees its bytes.
//
// Returns nullptr and sets `status` when the block is not in the set; otherwise `status` is
// Success and the handle completes with the payload. The pack is kept alive by the request.
[[nodiscard]] BackgroundRequestHandle ReadPackBlockAsync(
    BackgroundLoadService& reader,
    std::shared_ptr<const kb::assets::bake::RuntimeAssetPack> pack,
    const kb::assets::bake::AssetBakeDigest& artifact,
    std::string_view blockName,
    BackgroundPriority priority,
    kb::assets::bake::AssetPackReadStatus& status);

} // namespace kb::assets::streaming
