#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/world/WorldPartitionGrid.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kb::world {

// Everything about a placed object that partitioning, region loading and the
// cell builder need without decoding its scene payload.
struct WorldObjectHeader {
    // 32 lowercase hexadecimal characters; also the file name stem.
    std::string guid;
    std::string name;
    // Empty for the base layer.
    std::string dataLayer;
    // Loaded with the world regardless of streaming sources.
    bool alwaysLoaded = false;
    // World position of the object's root, the point that decides its cell.
    WorldPoint position{};
    // Objects this one points at (joints, portals, UI links). Objects connected
    // this way always share a cell and are loaded for editing together.
    std::vector<std::string> references;
    std::uint32_t nodeCount = 0U;
};

// One placed object (a root and its whole hierarchy) in its own file, so that
// several people can edit one world without touching the same file.
struct WorldObjectFile {
    static constexpr std::string_view Extension = ".21kbobject";
    static constexpr std::uint32_t CurrentVersion = 1U;

    WorldObjectHeader header;
    // Node 0 is the object's root; every other node descends from it. Node stable
    // ids are unique across the whole world (see WorldObjectStableId).
    kb::scene::ScenePrefab prefab;
};

struct WorldObjectReadResult {
    bool succeeded = false;
    WorldObjectFile object;
    std::string error;
};

[[nodiscard]] bool IsValidWorldObjectGuid(std::string_view guid) noexcept;
// A fresh random object guid.
[[nodiscard]] std::string MakeWorldObjectGuid();
// A guid derived deterministically from `key` (used by scene migration so that
// converting the same scene twice yields the same files).
[[nodiscard]] std::string MakeDeterministicWorldObjectGuid(std::string_view key);
// The stable node id of the `nodeIndex`-th node of an object. Derived from the
// object's guid so ids never collide between objects created independently.
[[nodiscard]] std::uint64_t WorldObjectStableId(std::string_view guid, std::uint32_t nodeIndex) noexcept;

class WorldObjectFileIO {
public:
    WorldObjectFileIO() = delete;

    [[nodiscard]] static std::vector<std::uint8_t> Serialize(const WorldObjectFile& object, std::string& error);
    [[nodiscard]] static WorldObjectReadResult Parse(std::vector<std::uint8_t> bytes, bool headerOnly = false);
    [[nodiscard]] static WorldObjectReadResult Read(const std::filesystem::path& path, bool headerOnly = false);
    // Writes atomically; the bytes must come from Serialize.
    [[nodiscard]] static bool WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes, std::string& error);
    // Every object file in `directory`, sorted by file name (= guid).
    [[nodiscard]] static std::vector<std::filesystem::path> List(const std::filesystem::path& directory);
};

// Every stable node id a node of `prefab` references (joint, region portal,
// lens echo and UI links); 0 entries are skipped.
[[nodiscard]] std::vector<std::uint64_t> CollectNodeReferences(const kb::scene::ScenePrefab& prefab);

} // namespace kb::world
