#include "engine/save/SaveGameMigration.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace kb::save {
namespace {

void ApplyStep(std::unordered_map<std::string, SaveValue>& entries, const SaveGameMigration& step) {
    switch (step.kind) {
    case SaveGameMigrationKind::RenameKey: {
        const auto source = entries.find(step.key);
        if (source == entries.end() || step.newKey.empty() || step.newKey == step.key) {
            return;
        }
        entries[step.newKey] = std::move(source->second);
        entries.erase(source);
        return;
    }
    case SaveGameMigrationKind::SetDefault:
        entries.try_emplace(step.key, step.defaultValue);
        return;
    case SaveGameMigrationKind::RemoveKey:
        entries.erase(step.key);
        return;
    }
}

} // namespace

bool ApplySaveGameMigrations(
    std::unordered_map<std::string, SaveValue>& entries,
    std::uint32_t fromVersion,
    std::uint32_t targetVersion,
    std::span<const SaveGameMigration> migrations) {
    if (fromVersion == targetVersion) {
        return true;
    }
    if (fromVersion > targetVersion) {
        // A save from a NEWER schema than we target cannot be downgraded.
        return false;
    }

    // Work on a copy so a chain that cannot complete leaves the caller's map
    // untouched (all-or-nothing migration).
    std::unordered_map<std::string, SaveValue> working = entries;
    std::uint32_t version = fromVersion;
    while (version < targetVersion) {
        // Find the single migration that bridges the current version forward.
        // Steps are matched by exact fromVersion so the chain is unambiguous;
        // a step must strictly advance the version (toVersion > fromVersion),
        // and never past the target.
        const auto step = std::ranges::find_if(migrations, [version, targetVersion](const SaveGameMigration& candidate) {
            return candidate.fromVersion == version && candidate.toVersion > candidate.fromVersion && candidate.toVersion <= targetVersion;
        });
        if (step == migrations.end()) {
            return false; // gap in the chain — cannot safely reach the target
        }
        // Apply every step declared for this exact version hop, in list order
        // (multiple field edits can share one fromVersion->toVersion bump).
        const std::uint32_t nextVersion = step->toVersion;
        for (const SaveGameMigration& candidate : migrations) {
            if (candidate.fromVersion == version && candidate.toVersion == nextVersion) {
                ApplyStep(working, candidate);
            }
        }
        version = nextVersion;
    }

    entries = std::move(working);
    return true;
}

} // namespace kb::save
