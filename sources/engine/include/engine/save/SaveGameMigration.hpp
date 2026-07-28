#pragma once

#include "engine/save/SaveValue.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>

namespace kb::save {

// LIB-162: one declarative save-schema migration step, mirroring the shape
// kb::render's RenderMaterialTypeMigrationOperation already established.
enum class SaveGameMigrationKind : std::uint8_t {
    // Advance an on-disk envelope/schema version without changing entries.
    NoOp,
    // Move the value at `key` to `newKey` (no-op if `key` is absent; if
    // `newKey` already exists it is overwritten — the migration author is
    // asserting the rename is the source of truth).
    RenameKey,
    // Insert `defaultValue` at `key` ONLY if the key is absent (a value the
    // old schema never wrote gets its new default; an existing value is kept).
    SetDefault,
    // Drop `key` entirely (a value the new schema no longer understands).
    RemoveKey,
};

struct SaveGameMigration {
    std::uint32_t fromVersion = 0;
    std::uint32_t toVersion = 0;
    SaveGameMigrationKind kind = SaveGameMigrationKind::NoOp;
    std::string key;
    std::string newKey;   // RenameKey only
    SaveValue defaultValue; // SetDefault only
};

// Advances a decoded entry table from `fromVersion` up to `targetVersion` by
// applying, in ascending version order, every migration whose fromVersion is
// at or above the current version and below the target. Each applied step
// raises the working version to its toVersion; the loop repeats until the
// working version reaches `targetVersion`. Returns false (leaving `entries`
// untouched) if the chain cannot be completed — a gap with no migration
// bridging the next version, or a step that would move the version backward/
// nowhere — so a save that cannot be safely upgraded is rejected rather than
// half-migrated. A `fromVersion` already equal to `targetVersion` is a
// success no-op.
[[nodiscard]] bool ApplySaveGameMigrations(
    std::unordered_map<std::string, SaveValue>& entries,
    std::uint32_t fromVersion,
    std::uint32_t targetVersion,
    std::span<const SaveGameMigration> migrations);

} // namespace kb::save
