#pragma once

#include "engine/save/SaveDomain.hpp"
#include "engine/save/SaveGame.hpp"
#include "engine/security/Crypto.hpp"

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kb::save {

inline constexpr std::size_t kMaxSaveGameSerializedBytes = 16U << 20U;

// LIB-162: why a SaveGame could not be loaded. Ok is the only success value;
// every other value names a distinct, non-silent failure a caller can react
// to (e.g. offer to start a new game on FileNotFound, warn on Corrupt).
enum class SaveGameLoadStatus : std::uint8_t {
    Ok,
    FileNotFound,       // the path does not exist / could not be read
    BadMagic,           // not a SaveGame file (wrong magic bytes)
    UnsupportedVersion, // schema version is 0, or newer than this build understands
    Corrupt,            // truncated / malformed payload past a valid header
    MigrationFailed,    // the schema version is old but no migration chain reaches current
    WrongDomain,        // LIB-163: a valid file, but of a different domain than requested
    TooLarge,           // file exceeds the production save-size budget
    IntegrityMismatch,  // payload does not match its stored integrity hash (damaged on disk)
    Tampered,           // intact, but not authenticated by this game on this installation:
                        // edited outside the game, or copied from another installation
};

struct SaveGameLoadResult {
    SaveGameLoadStatus status = SaveGameLoadStatus::FileNotFound;
    SaveGame save;
    std::string diagnostic;
    // An unauthenticated save from before schema 3 was loaded and rewritten, authenticated,
    // in the current format.
    bool migrated = false;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == SaveGameLoadStatus::Ok;
    }
};

// How saves are authenticated. Every save carries an HMAC-SHA512 under `key`; a save whose
// code does not match is Tampered, never loaded.
//
// A packaged player derives the key from the per-game secret in its trust anchor
// (DeriveSaveGameIntegrity with no installation secret), so a save edited by hand is refused while
// saves still move between the player's machines, cloud saves and reinstalls. A game that wants
// saves bound to one installation passes an installation secret as well (LoadOrCreateInstallationSecret);
// a save copied from elsewhere is then refused. The secret lives in the player's copy of the game, so
// this makes editing a save deliberate work rather than a hex edit -- it cannot stop someone who
// extracts it. Until a player configures one, saves use a fixed development key: hand edits are
// still caught, but anyone can forge one.
struct SaveGameIntegrity {
    kb::security::SecretBytes<kb::security::kSha512Bytes> key;
    // Saves written before saves were authenticated (schema 1 and 2) load once and are rewritten
    // authenticated. Such a file cannot be told apart from one forged by hand, so a game whose
    // first release already wrote schema 3 turns this off.
    bool acceptUnauthenticatedLegacySaves = true;
};

[[nodiscard]] SaveGameIntegrity DeriveSaveGameIntegrity(
    std::span<const std::uint8_t> gameSecret,
    std::span<const std::uint8_t> installationSecret);
[[nodiscard]] SaveGameIntegrity DevelopmentSaveGameIntegrity();

class SaveGameService {
public:
    SaveGameService() = delete;
    static constexpr std::size_t MaxSerializedBytes = kMaxSaveGameSerializedBytes;

    // Installs the integrity every later Save signs with and every later Load checks against.
    static void ConfigureIntegrity(const SaveGameIntegrity& integrity);

    // Serializes `save` at the current schema version, stamped with `domain`
    // (LIB-163), and writes it to `path` ATOMICALLY (write to a temp file,
    // then replace) — a crash mid-write can never corrupt a previous save at
    // that path. Returns false if the bytes could not be written or an entry
    // exceeds the format limits. Parent directories are created as needed.
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const SaveGame& save, SaveDomain domain = SaveDomain::SaveGame);

    // Reads and decodes a save from `path`, requiring it to be of
    // `expectedDomain` (a file of any other domain is rejected as WrongDomain,
    // keeping the persistence categories separated), and running the built-in
    // schema migration chain to bring an older save up to current. The
    // result's status names any failure precisely; on failure the save is
    // empty. An accepted legacy save is rewritten at `path` in the current,
    // authenticated format before Load returns.
    [[nodiscard]] static SaveGameLoadResult Load(const std::filesystem::path& path, SaveDomain expectedDomain = SaveDomain::SaveGame);

    // The byte form Save writes and Load reads, for callers that persist a
    // save through their own storage (e.g. kb::platform::UserStorage) rather
    // than a caller-chosen path. Serialize returns nothing when `save`
    // exceeds the format limits; Deserialize applies the same size, domain,
    // integrity and migration checks as Load.
    [[nodiscard]] static std::optional<std::vector<std::uint8_t>> Serialize(const SaveGame& save, SaveDomain domain = SaveDomain::SaveGame);
    [[nodiscard]] static SaveGameLoadResult Deserialize(std::span<const std::uint8_t> bytes, SaveDomain expectedDomain = SaveDomain::SaveGame);
};

} // namespace kb::save
