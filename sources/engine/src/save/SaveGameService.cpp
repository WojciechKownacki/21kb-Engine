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

bool SaveGameService::Save(const std::filesystem::path& path, const SaveGame& save, SaveDomain domain) {
    if (!WithinFormatLimits(save)) {
        return false;
    }
    const std::vector<std::uint8_t> bytes = SaveGameCodec::Encode(save, SaveGameFormat::kCurrentSchemaVersion, domain);
    if (bytes.size() > SaveGameFormat::kMaxSerializedBytes) {
        return false;
    }
    return SaveGameBinaryIO::WriteBytesAtomically(path, bytes);
}

SaveGameLoadResult SaveGameService::Load(const std::filesystem::path& path, SaveDomain expectedDomain) {
    std::error_code sizeError;
    const std::uintmax_t fileSize = std::filesystem::file_size(path, sizeError);
    if (!sizeError && fileSize > SaveGameFormat::kMaxSerializedBytes) {
        return SaveGameLoadResult{
            .status = SaveGameLoadStatus::TooLarge,
            .save = {},
            .diagnostic = "save file exceeds the 16 MiB serialized-size limit",
        };
    }
    std::vector<std::uint8_t> bytes;
    bool tooLarge = false;
    if (!SaveGameBinaryIO::ReadAllBytes(path, bytes, SaveGameFormat::kMaxSerializedBytes, tooLarge)) {
        if (tooLarge) {
            return SaveGameLoadResult{
                .status = SaveGameLoadStatus::TooLarge,
                .save = {},
                .diagnostic = "save file exceeds the 16 MiB serialized-size limit",
            };
        }
        return SaveGameLoadResult{ .status = SaveGameLoadStatus::FileNotFound, .save = {}, .diagnostic = "save file could not be opened" };
    }
    if (bytes.size() > SaveGameFormat::kMaxSerializedBytes) {
        return SaveGameLoadResult{
            .status = SaveGameLoadStatus::TooLarge,
            .save = {},
            .diagnostic = "save file exceeds the 16 MiB serialized-size limit",
        };
    }
    std::error_code finalSizeError;
    const std::uintmax_t finalFileSize = std::filesystem::file_size(path, finalSizeError);
    if (!finalSizeError && finalFileSize != bytes.size()) {
        return SaveGameLoadResult{
            .status = SaveGameLoadStatus::Corrupt,
            .save = {},
            .diagnostic = "save file changed while it was being read",
        };
    }
    return SaveGameCodec::Decode(bytes, SaveGameFormat::kCurrentSchemaVersion, expectedDomain, BuiltInSaveGameMigrations());
}

} // namespace kb::save
