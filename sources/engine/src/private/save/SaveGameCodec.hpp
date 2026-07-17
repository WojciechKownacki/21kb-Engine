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
    // + domain + entry count + [key, typeTag, scalar]... Keys are emitted in
    // sorted order so the byte output is deterministic (identical save ->
    // identical bytes), which a later integrity hash (LIB-164) depends on.
    // `schemaVersion` is a parameter (not hard-wired) so a test can produce an
    // older-version file to exercise the Decode migration path; production
    // Save always passes SaveGameFormat::kCurrentSchemaVersion.
    [[nodiscard]] static std::vector<std::uint8_t> Encode(const SaveGame& save, std::uint32_t schemaVersion, SaveDomain domain);

    // Decodes bytes, validates the header (magic, version, and that the file's
    // domain equals `expectedDomain`), migrates the decoded entries from the
    // file's schema version up to `targetVersion` using `migrations`, and
    // returns the result with a precise status. On any failure the returned
    // save is empty.
    [[nodiscard]] static SaveGameLoadResult Decode(std::span<const std::uint8_t> bytes, std::uint32_t targetVersion, SaveDomain expectedDomain, std::span<const SaveGameMigration> migrations);
};

// The schema migrations THIS build ships. Empty today: SaveGame schema v1 is
// the first version, so no save can predate it. When kCurrentSchemaVersion is
// bumped, add the step(s) bridging the previous version here and old saves
// upgrade automatically on Load. The migration MECHANISM itself
// (ApplySaveGameMigrations / Decode) is proven independently by
// RunSaveGameMigrationTest, so it is ready the moment a real step is added.
[[nodiscard]] std::span<const SaveGameMigration> BuiltInSaveGameMigrations();

} // namespace kb::save
