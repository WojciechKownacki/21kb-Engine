#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace kb::save {

// LIB-162: the on-disk SaveGame binary format constants. Mirrors the
// per-subsystem Format convention (kb::scene's SceneAssetFormat, kb::project's
// ProjectDescriptorFormat): an 8-byte magic followed by a uint32 schema
// version, then the payload.
struct SaveGameFormat {
    // "21KBSAV\0" — distinct from the scene ("21KBSCN") and project ("21KBPRJ")
    // magics so a mis-pointed file is rejected, not misread.
    static constexpr std::array<std::uint8_t, 8> kMagic{ '2', '1', 'K', 'B', 'S', 'A', 'V', 0 };

    // The schema version THIS build writes. Bump when the entry layout or the
    // meaning of a well-known key changes, and add a SaveGameMigration bridging
    // the previous version to the new one to BuiltInSaveGameMigrations().
    static constexpr std::uint32_t kCurrentSchemaVersion = 1U;

    // Guards against a malformed/hostile file exhausting memory before the
    // per-entry reads bounds-check themselves.
    static constexpr std::uint32_t kMaxEntries = 1U << 16U; // 65536 save entries
    static constexpr std::size_t kMaxKeyBytes = 1024U;
    static constexpr std::size_t kMaxStringValueBytes = 1U << 20U; // 1 MiB per string value
};

} // namespace kb::save
