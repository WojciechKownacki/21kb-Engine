#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace kb::world {

// One pack chunk of a built world: every cell scene and HLOD proxy of one region (a square of
// `regionCells` x `regionCells` cells, in every data layer), named by virtual path prefixes in
// the form the pack splitter takes (kb_cli pack split --chunk, package_game.py --pack-chunk).
struct WorldRegionChunk {
    std::string label;
    std::vector<std::string> prefixes;
};

struct WorldRegionChunksResult {
    bool succeeded = false;
    std::vector<WorldRegionChunk> chunks;
    std::string error;
};

// Chunks for every built world (cell index) under `contentRoot`, mounted as /Game. Always-loaded
// units stay out of every chunk: they ship with the base pack. A built file whose virtual path
// starts with one of `excludedPrefixes` is left to the rule that names that prefix, and a region
// left with no file gets no chunk. Labels are "<world>.r_<x>_<z>", unique and at most 64 bytes.
[[nodiscard]] WorldRegionChunksResult CollectWorldRegionChunks(
    const std::filesystem::path& contentRoot,
    std::span<const std::string> excludedPrefixes);

} // namespace kb::world
