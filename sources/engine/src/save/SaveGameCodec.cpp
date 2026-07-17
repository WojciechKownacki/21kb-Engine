#include "save/SaveGameCodec.hpp"

#include "save/SaveGameBinaryIO.hpp"
#include "save/SaveGameFormat.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace kb::save {
namespace {

[[nodiscard]] bool IsKnownType(std::uint8_t tag) noexcept {
    return tag <= static_cast<std::uint8_t>(SaveValueType::String);
}

[[nodiscard]] bool ReadScalar(SaveGameBinaryIO::ByteReader& reader, SaveValueType type, SaveValue& out) {
    switch (type) {
    case SaveValueType::Bool: {
        std::uint8_t raw = 0;
        if (!reader.ReadUInt8(raw)) {
            return false;
        }
        out = SaveValue::MakeBool(raw != 0);
        return true;
    }
    case SaveValueType::Int: {
        std::int64_t raw = 0;
        if (!reader.ReadInt64(raw)) {
            return false;
        }
        out = SaveValue::MakeInt(raw);
        return true;
    }
    case SaveValueType::Float: {
        double raw = 0.0;
        if (!reader.ReadDouble(raw)) {
            return false;
        }
        out = SaveValue::MakeFloat(raw);
        return true;
    }
    case SaveValueType::String: {
        std::string raw;
        if (!reader.ReadString(raw, SaveGameFormat::kMaxStringValueBytes)) {
            return false;
        }
        out = SaveValue::MakeString(std::move(raw));
        return true;
    }
    }
    return false;
}

void WriteScalar(std::vector<std::uint8_t>& out, const SaveValue& value) {
    switch (value.type) {
    case SaveValueType::Bool:
        SaveGameBinaryIO::WriteUInt8(out, value.boolValue ? 1U : 0U);
        return;
    case SaveValueType::Int:
        SaveGameBinaryIO::WriteInt64(out, value.intValue);
        return;
    case SaveValueType::Float:
        SaveGameBinaryIO::WriteDouble(out, value.floatValue);
        return;
    case SaveValueType::String:
        SaveGameBinaryIO::WriteString(out, value.stringValue);
        return;
    }
}

} // namespace

std::vector<std::uint8_t> SaveGameCodec::Encode(const SaveGame& save, std::uint32_t schemaVersion) {
    std::vector<std::uint8_t> bytes;
    SaveGameBinaryIO::WriteRaw(bytes, SaveGameFormat::kMagic.data(), SaveGameFormat::kMagic.size());
    SaveGameBinaryIO::WriteUInt32(bytes, schemaVersion);
    SaveGameBinaryIO::WriteUInt32(bytes, static_cast<std::uint32_t>(save.Entries().size()));

    // Deterministic key order -> identical saves produce identical bytes.
    std::vector<std::string> keys;
    keys.reserve(save.Entries().size());
    for (const auto& [key, value] : save.Entries()) {
        static_cast<void>(value);
        keys.push_back(key);
    }
    std::ranges::sort(keys);

    for (const std::string& key : keys) {
        const SaveValue& value = save.Entries().at(key);
        SaveGameBinaryIO::WriteString(bytes, key);
        SaveGameBinaryIO::WriteUInt8(bytes, static_cast<std::uint8_t>(value.type));
        WriteScalar(bytes, value);
    }
    return bytes;
}

SaveGameLoadResult SaveGameCodec::Decode(std::span<const std::uint8_t> bytes, std::uint32_t targetVersion, std::span<const SaveGameMigration> migrations) {
    SaveGameLoadResult result;
    SaveGameBinaryIO::ByteReader reader{ bytes };

    std::array<std::uint8_t, SaveGameFormat::kMagic.size()> magic{};
    if (!reader.ReadRaw(magic.data(), magic.size()) || magic != SaveGameFormat::kMagic) {
        result.status = SaveGameLoadStatus::BadMagic;
        return result;
    }

    std::uint32_t schemaVersion = 0;
    if (!reader.ReadUInt32(schemaVersion)) {
        result.status = SaveGameLoadStatus::Corrupt;
        return result;
    }
    if (schemaVersion == 0 || schemaVersion > targetVersion) {
        result.status = SaveGameLoadStatus::UnsupportedVersion;
        return result;
    }

    std::uint32_t entryCount = 0;
    if (!reader.ReadUInt32(entryCount) || entryCount > SaveGameFormat::kMaxEntries) {
        result.status = SaveGameLoadStatus::Corrupt;
        return result;
    }

    std::unordered_map<std::string, SaveValue> entries;
    entries.reserve(entryCount);
    for (std::uint32_t index = 0; index < entryCount; ++index) {
        std::string key;
        std::uint8_t typeTag = 0;
        if (!reader.ReadString(key, SaveGameFormat::kMaxKeyBytes) || !reader.ReadUInt8(typeTag) || !IsKnownType(typeTag)) {
            result.status = SaveGameLoadStatus::Corrupt;
            return result;
        }
        SaveValue value;
        if (!ReadScalar(reader, static_cast<SaveValueType>(typeTag), value)) {
            result.status = SaveGameLoadStatus::Corrupt;
            return result;
        }
        entries.insert_or_assign(std::move(key), std::move(value));
    }

    if (!ApplySaveGameMigrations(entries, schemaVersion, targetVersion, migrations)) {
        result.status = SaveGameLoadStatus::MigrationFailed;
        return result;
    }

    result.status = SaveGameLoadStatus::Ok;
    result.save.SetEntries(std::move(entries));
    return result;
}

std::span<const SaveGameMigration> BuiltInSaveGameMigrations() {
    // Empty: SaveGame schema v1 is the first version (SaveGameMigration holds
    // std::string members, so this is a runtime-const, not constexpr, table).
    static const std::vector<SaveGameMigration> kMigrations{};
    return kMigrations;
}

} // namespace kb::save
