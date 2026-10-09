#include "engine/save/SaveGameService.hpp"

#include "save/SaveGameBinaryIO.hpp"
#include "save/SaveGameCodec.hpp"
#include "save/SaveGameFormat.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
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

constexpr std::string_view kSaveKeySalt = "21KB-SAVE-KEY-V1";
constexpr std::string_view kDevelopmentGameSecret = "21kb development save secret";

std::mutex& IntegrityMutex() {
    static std::mutex mutex;
    return mutex;
}

SaveGameIntegrity& ConfiguredIntegrity() {
    static SaveGameIntegrity integrity = DevelopmentSaveGameIntegrity();
    return integrity;
}

[[nodiscard]] SaveGameIntegrity CurrentIntegrity() {
    const std::scoped_lock lock{ IntegrityMutex() };
    return ConfiguredIntegrity();
}

} // namespace

SaveGameIntegrity DeriveSaveGameIntegrity(
    std::span<const std::uint8_t> gameSecret,
    std::span<const std::uint8_t> installationSecret) {
    std::vector<std::uint8_t> material(gameSecret.begin(), gameSecret.end());
    material.insert(material.end(), installationSecret.begin(), installationSecret.end());
    SaveGameIntegrity integrity{};
    kb::security::HkdfSha512(
        integrity.key.Span(),
        material,
        std::span{ reinterpret_cast<const std::uint8_t*>(kSaveKeySalt.data()), kSaveKeySalt.size() },
        {});
    kb::security::SecureWipe(material);
    return integrity;
}

SaveGameIntegrity DevelopmentSaveGameIntegrity() {
    return DeriveSaveGameIntegrity(
        std::span{ reinterpret_cast<const std::uint8_t*>(kDevelopmentGameSecret.data()), kDevelopmentGameSecret.size() },
        {});
}

void SaveGameService::ConfigureIntegrity(const SaveGameIntegrity& integrity) {
    const std::scoped_lock lock{ IntegrityMutex() };
    ConfiguredIntegrity() = integrity;
}

std::optional<std::vector<std::uint8_t>> SaveGameService::Serialize(const SaveGame& save, SaveDomain domain) {
    if (!WithinFormatLimits(save)) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes =
        SaveGameCodec::Encode(save, SaveGameFormat::kCurrentSchemaVersion, domain, CurrentIntegrity());
    if (bytes.size() > SaveGameFormat::kMaxSerializedBytes) {
        return std::nullopt;
    }
    return bytes;
}

bool SaveGameService::Save(const std::filesystem::path& path, const SaveGame& save, SaveDomain domain) {
    const std::optional<std::vector<std::uint8_t>> bytes = Serialize(save, domain);
    return bytes.has_value() && SaveGameBinaryIO::WriteBytesAtomically(path, *bytes);
}

SaveGameLoadResult SaveGameService::Deserialize(std::span<const std::uint8_t> bytes, SaveDomain expectedDomain) {
    if (bytes.size() > SaveGameFormat::kMaxSerializedBytes) {
        return SaveGameLoadResult{
            .status = SaveGameLoadStatus::TooLarge,
            .save = {},
            .diagnostic = "save file exceeds the 16 MiB serialized-size limit",
        };
    }
    return SaveGameCodec::Decode(
        bytes, SaveGameFormat::kCurrentSchemaVersion, expectedDomain, BuiltInSaveGameMigrations(), CurrentIntegrity());
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
    std::error_code finalSizeError;
    const std::uintmax_t finalFileSize = std::filesystem::file_size(path, finalSizeError);
    if (!finalSizeError && finalFileSize != bytes.size()) {
        return SaveGameLoadResult{
            .status = SaveGameLoadStatus::Corrupt,
            .save = {},
            .diagnostic = "save file changed while it was being read",
        };
    }
    SaveGameLoadResult result = Deserialize(bytes, expectedDomain);
    // An accepted legacy save is rewritten authenticated straight away, so it is trusted on the
    // strength of its old format exactly once.
    if (result.Succeeded() && result.migrated) {
        const std::optional<std::vector<std::uint8_t>> upgraded = Serialize(result.save, expectedDomain);
        result.diagnostic = upgraded.has_value() && SaveGameBinaryIO::WriteBytesAtomically(path, *upgraded)
            ? "save was written by an older version and has been upgraded to the authenticated format"
            : "save was written by an older version and could not be rewritten in the authenticated format";
    }
    return result;
}

} // namespace kb::save
