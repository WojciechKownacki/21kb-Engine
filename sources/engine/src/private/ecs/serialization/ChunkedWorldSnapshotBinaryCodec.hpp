#pragma once

#include "engine/ecs/WorldSnapshot.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace kb::ecs {

class World;

class ChunkedWorldSnapshotBinaryCodec {
public:
    [[nodiscard]] static bool Encode(const World& world, const ChunkedWorldSnapshot& snapshot, std::vector<std::byte>& output);
    [[nodiscard]] static bool Decode(std::span<const std::byte> source, ChunkedWorldSnapshotHeader& header, std::vector<ChunkedWorldSnapshotChunk>& chunks);
};

} // namespace kb::ecs
