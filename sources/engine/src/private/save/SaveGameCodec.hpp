#pragma once

#include "engine/save/SaveDomain.hpp"
#include "engine/save/SaveGame.hpp"
#include "engine/save/SaveGameMigration.hpp"
#include "engine/save/SaveGameService.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace kb::save {

class SaveGameCodec {
public:
    SaveGameCodec() = delete;

    // Serializes `save` at `schemaVersion` for `domain`: magic + schemaVersion
    // + payload byte count + integrity hash + HMAC + domain + entry count +
    // [key, typeTag, value]... Keys are emitted in sorted order so identical
    // saves produce identical bytes and hashes. Encoding schema v1 and v2
    // preserves their legacy envelopes for compatibility tests.
    // `schemaVersion` is a parameter (not hard-wired) so a test can produce an
    // older-version file to exercise the Decode migration path; production
    // Save always passes SaveGameFormat::kCurrentSchemaVersion.
    [[nodiscard]] static std::vector<std::uint8_t> Encode(
        const SaveGame& save, std::uint32_t schemaVersion, SaveDomain domain, const SaveGameIntegrity& integrity);

    // Decodes bytes, validates the header (magic, version, and that the file's
    // domain equals `expectedDomain`), migrates the decoded entries from the
    // file's schema version up to `targetVersion` using `migrations`, and
    // returns the result with a precise status. On any failure the returned
    // save is empty. A file older than the first authenticated schema decodes
    // only when `integrity` accepts legacy saves, and is then flagged migrated.
    [[nodiscard]] static SaveGameLoadResult Decode(
        std::span<const std::uint8_t> bytes,
        std::uint32_t targetVersion,
        SaveDomain expectedDomain,
        std::span<const SaveGameMigration> migrations,
        const SaveGameIntegrity& integrity);
};

// The schema migrations THIS build ships. v1 -> v2 -> v3 are data no-ops: v2
// adds the size/hash envelope and v3 the HMAC, neither changes entry meanings.
[[nodiscard]] std::span<const SaveGameMigration> BuiltInSaveGameMigrations();

} // namespace kb::save
