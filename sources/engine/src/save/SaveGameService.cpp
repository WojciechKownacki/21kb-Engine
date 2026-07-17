#include "engine/save/SaveGameService.hpp"

#include "save/SaveGameBinaryIO.hpp"
#include "save/SaveGameCodec.hpp"
#include "save/SaveGameFormat.hpp"

#include <cstdint>
#include <vector>

namespace kb::save {

namespace {

// LIB-162: refuse to write a save that Load would reject as Corrupt for
// exceeding the format limits — Save and Load must be symmetric, so a
// successful Save is always reloadable (never a silent write-then-can't-read).
[[nodiscard]] bool WithinFormatLimits(const SaveGame& save) {
    if (save.Entries().size() > SaveGameFormat::kMaxEntries) {
        return false;
    }
    for (const auto& [key, value] : save.Entries()) {
        if (key.size() > SaveGameFormat::kMaxKeyBytes) {
            return false;
        }
        if (value.type == SaveValueType::String && value.stringValue.size() > SaveGameFormat::kMaxStringValueBytes) {
            return false;
        }
    }
    return true;
}

} // namespace

bool SaveGameService::Save(const std::filesystem::path& path, const SaveGame& save) {
    if (!WithinFormatLimits(save)) {
        return false;
    }
    const std::vector<std::uint8_t> bytes = SaveGameCodec::Encode(save, SaveGameFormat::kCurrentSchemaVersion);
    return SaveGameBinaryIO::WriteBytesAtomically(path, bytes);
}

SaveGameLoadResult SaveGameService::Load(const std::filesystem::path& path) {
    std::vector<std::uint8_t> bytes;
    if (!SaveGameBinaryIO::ReadAllBytes(path, bytes)) {
        return SaveGameLoadResult{ .status = SaveGameLoadStatus::FileNotFound, .save = {} };
    }
    return SaveGameCodec::Decode(bytes, SaveGameFormat::kCurrentSchemaVersion, BuiltInSaveGameMigrations());
}

} // namespace kb::save
