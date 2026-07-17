#pragma once

#include "engine/save/SaveGame.hpp"

#include <cstdint>
#include <filesystem>

namespace kb::save {

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
};

struct SaveGameLoadResult {
    SaveGameLoadStatus status = SaveGameLoadStatus::FileNotFound;
    SaveGame save;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == SaveGameLoadStatus::Ok;
    }
};

class SaveGameService {
public:
    SaveGameService() = delete;

    // Serializes `save` at the current schema version and writes it to `path`
    // ATOMICALLY (write to a temp file, then replace) — a crash mid-write can
    // never corrupt a previous save at that path. Returns false if the bytes
    // could not be written. Parent directories are created as needed.
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const SaveGame& save);

    // Reads and decodes a SaveGame from `path`, running the built-in schema
    // migration chain to bring an older save up to the current schema. The
    // result's status names any failure precisely; on failure the returned
    // save is empty.
    [[nodiscard]] static SaveGameLoadResult Load(const std::filesystem::path& path);
};

} // namespace kb::save
