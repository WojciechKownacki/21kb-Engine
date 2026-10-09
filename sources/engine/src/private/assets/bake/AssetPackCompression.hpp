#pragma once

#include <cstdint>
#include <span>
#include <vector>

// Block compression of .kbpack containers (AssetPackBlockCompression::Zstd). One block is one
// zstd frame that declares its content size, so a block is decoded on its own -- the container
// keeps its random access -- and a frame that would inflate past what the index promised is
// refused before a byte of output is written.
namespace kb::assets::bake {

// The level the writer uses unless told otherwise: a cook-time cost that stays proportionate on
// gigabytes of texture blocks while keeping most of what the higher levels save.
inline constexpr int kDefaultAssetPackCompressionLevel = 9;
inline constexpr int kMinAssetPackCompressionLevel = 1;
inline constexpr int kMaxAssetPackCompressionLevel = 19;

// Compresses `bytes` into one frame at `level`. Deterministic for the same bytes and level.
// False when the encoder fails or `bytes` is empty.
[[nodiscard]] bool CompressAssetPackBlock(std::span<const std::uint8_t> bytes, int level, std::vector<std::uint8_t>& out);

// Decodes a block stored by CompressAssetPackBlock into exactly `uncompressedBytes` bytes. Refuses
// anything but one complete frame whose declared content size is `uncompressedBytes` and whose
// decoded length matches it; `out` is empty after a refusal. Safe to call from several threads.
[[nodiscard]] bool DecompressAssetPackBlock(
    std::span<const std::uint8_t> stored,
    std::uint64_t uncompressedBytes,
    std::vector<std::uint8_t>& out);

} // namespace kb::assets::bake
