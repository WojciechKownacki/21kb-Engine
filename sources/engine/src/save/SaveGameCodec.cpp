#include "save/SaveGameCodec.hpp"

#include "save/SaveGameBinaryIO.hpp"
#include "save/SaveGameFormat.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::save {
namespace {

// The domain includes its terminating NUL, which separates it from what follows it.
constexpr std::string_view kMacDomain{ "21KB-SAVE-V3\0", 13U };

// HMAC over everything in the file but the HMAC field: the header in front of it and the
// payload behind it, so neither the version, the size, the hash nor a payload byte can change.
[[nodiscard]] kb::security::Sha512Digest SaveMac(
    const SaveGameIntegrity& integrity,
    std::span<const std::uint8_t> header,
    std::span<const std::uint8_t> payload) noexcept {
    kb::security::HmacSha512Hasher mac{ integrity.key.Span() };
    mac.Update(std::span{ reinterpret_cast<const std::uint8_t*>(kMacDomain.data()), kMacDomain.size() });
    mac.Update(header);
    mac.Update(payload);
    return mac.Finish();
}

[[nodiscard]] bool IsKnownType(std::uint8_t tag) noexcept {
    return tag <= static_cast<std::uint8_t>(SaveValueType::AssetRef);
}

[[nodiscard]] std::uint64_t IntegrityHash(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
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
    case SaveValueType::AssetRef: {
        std::uint64_t raw = 0U;
        if (!reader.ReadUInt64(raw)) {
            return false;
        }
        out = SaveValue::MakeAssetRef(raw);
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
    case SaveValueType::AssetRef:
        SaveGameBinaryIO::WriteUInt64(out, value.assetIdValue);
        return;
    }
}

} // namespace

std::vector<std::uint8_t> SaveGameCodec::Encode(
    const SaveGame& save, std::uint32_t schemaVersion, SaveDomain domain, const SaveGameIntegrity& integrity) {
    std::vector<std::uint8_t> payload;
    SaveGameBinaryIO::WriteUInt8(payload, static_cast<std::uint8_t>(domain));
    SaveGameBinaryIO::WriteUInt32(payload, static_cast<std::uint32_t>(save.Entries().size()));

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
        SaveGameBinaryIO::WriteString(payload, key);
        SaveGameBinaryIO::WriteUInt8(payload, static_cast<std::uint8_t>(value.type));
        WriteScalar(payload, value);
    }

    std::vector<std::uint8_t> bytes;
    SaveGameBinaryIO::WriteRaw(bytes, SaveGameFormat::kMagic.data(), SaveGameFormat::kMagic.size());
    SaveGameBinaryIO::WriteUInt32(bytes, schemaVersion);
    if (schemaVersion >= 2U) {
        SaveGameBinaryIO::WriteUInt64(bytes, static_cast<std::uint64_t>(payload.size()));
        SaveGameBinaryIO::WriteUInt64(bytes, IntegrityHash(payload));
    }
    if (schemaVersion >= SaveGameFormat::kFirstAuthenticatedSchemaVersion) {
        const kb::security::Sha512Digest mac = SaveMac(integrity, bytes, payload);
        SaveGameBinaryIO::WriteRaw(bytes, mac.data(), mac.size());
    }
    SaveGameBinaryIO::WriteRaw(bytes, payload.data(), payload.size());
    return bytes;
}

SaveGameLoadResult SaveGameCodec::Decode(
    std::span<const std::uint8_t> bytes,
    std::uint32_t targetVersion,
    SaveDomain expectedDomain,
    std::span<const SaveGameMigration> migrations,
    const SaveGameIntegrity& integrity) {
    const auto fail = [](SaveGameLoadStatus status, std::string diagnostic) {
        return SaveGameLoadResult{ .status = status, .save = {}, .diagnostic = std::move(diagnostic) };
    };
    if (bytes.size() > SaveGameFormat::kMaxSerializedBytes) {
        return fail(SaveGameLoadStatus::TooLarge, "save file exceeds the 16 MiB serialized-size limit");
    }
    SaveGameBinaryIO::ByteReader headerReader{ bytes };

    std::array<std::uint8_t, SaveGameFormat::kMagic.size()> magic{};
    if (!headerReader.ReadRaw(magic.data(), magic.size()) || magic != SaveGameFormat::kMagic) {
        return fail(SaveGameLoadStatus::BadMagic, "save file magic is invalid");
    }

    std::uint32_t schemaVersion = 0;
    if (!headerReader.ReadUInt32(schemaVersion)) {
        return fail(SaveGameLoadStatus::Corrupt, "save header is truncated before schema version");
    }
    if (schemaVersion == 0 || schemaVersion > targetVersion) {
        return fail(SaveGameLoadStatus::UnsupportedVersion, "save schema version is unsupported");
    }

    const bool authenticated = schemaVersion >= SaveGameFormat::kFirstAuthenticatedSchemaVersion;
    if (!authenticated && !integrity.acceptUnauthenticatedLegacySaves) {
        return fail(SaveGameLoadStatus::Tampered, "save predates authenticated saves and this game no longer accepts those");
    }
    constexpr std::size_t kLegacyHeaderBytes = SaveGameFormat::kMagic.size() + sizeof(std::uint32_t);
    constexpr std::size_t kIntegrityHeaderBytes = kLegacyHeaderBytes + sizeof(std::uint64_t) + sizeof(std::uint64_t);
    const std::size_t headerBytes = kIntegrityHeaderBytes + (authenticated ? kb::security::kSha512Bytes : 0U);
    std::span<const std::uint8_t> payload;
    if (schemaVersion >= 2U) {
        std::uint64_t payloadSize = 0U;
        std::uint64_t expectedHash = 0U;
        kb::security::Sha512Digest expectedMac{};
        if (!headerReader.ReadUInt64(payloadSize) || !headerReader.ReadUInt64(expectedHash) ||
            (authenticated && !headerReader.ReadRaw(expectedMac.data(), expectedMac.size()))) {
            return fail(SaveGameLoadStatus::Corrupt, "save integrity header is truncated");
        }
        if (payloadSize > SaveGameFormat::kMaxSerializedBytes) {
            return fail(SaveGameLoadStatus::TooLarge, "declared save payload exceeds the 16 MiB serialized-size limit");
        }
        if (payloadSize != bytes.size() - headerBytes) {
            return fail(SaveGameLoadStatus::Corrupt, "declared save payload size does not match the file length");
        }
        payload = bytes.subspan(headerBytes);
        if (IntegrityHash(payload) != expectedHash) {
            return fail(SaveGameLoadStatus::IntegrityMismatch, "save payload integrity hash does not match");
        }
        if (authenticated &&
            !kb::security::ConstantTimeEqual(SaveMac(integrity, bytes.first(kIntegrityHeaderBytes), payload), expectedMac)) {
            return fail(SaveGameLoadStatus::Tampered,
                "save was modified outside the game or written by another installation");
        }
    } else {
        payload = bytes.subspan(kLegacyHeaderBytes);
    }
    SaveGameBinaryIO::ByteReader reader{ payload };

    // LIB-163: the domain tag separates the persistence categories — a file
    // written for another domain is a valid file but the wrong KIND of data,
    // rejected here rather than loaded into the wrong store.
    std::uint8_t domainTag = 0;
    if (!reader.ReadUInt8(domainTag)) {
        return fail(SaveGameLoadStatus::Corrupt, "save payload is truncated before domain");
    }
    if (domainTag != static_cast<std::uint8_t>(expectedDomain)) {
        return fail(SaveGameLoadStatus::WrongDomain, "save domain does not match the requested persistence domain");
    }

    std::uint32_t entryCount = 0;
    if (!reader.ReadUInt32(entryCount)) {
        return fail(SaveGameLoadStatus::Corrupt, "save payload is truncated before entry count");
    }
    if (entryCount > SaveGameFormat::kMaxEntries) {
        return fail(SaveGameLoadStatus::Corrupt, "save entry count exceeds the format limit");
    }

    std::unordered_map<std::string, SaveValue> entries;
    entries.reserve(entryCount);
    for (std::uint32_t index = 0; index < entryCount; ++index) {
        std::string key;
        std::uint8_t typeTag = 0;
        if (!reader.ReadString(key, SaveGameFormat::kMaxKeyBytes)) {
            return fail(SaveGameLoadStatus::Corrupt, "save entry " + std::to_string(index) + " has an invalid or truncated key");
        }
        if (!reader.ReadUInt8(typeTag) || !IsKnownType(typeTag)) {
            return fail(SaveGameLoadStatus::Corrupt, "save entry '" + key + "' has an invalid value type");
        }
        SaveValue value;
        if (!ReadScalar(reader, static_cast<SaveValueType>(typeTag), value)) {
            return fail(SaveGameLoadStatus::Corrupt, "save entry '" + key + "' has a truncated or oversized value");
        }
        entries.insert_or_assign(std::move(key), std::move(value));
    }
    if (!reader.Exhausted()) {
        return fail(SaveGameLoadStatus::Corrupt, "save payload contains trailing bytes");
    }

    if (!ApplySaveGameMigrations(entries, schemaVersion, targetVersion, migrations)) {
        return fail(SaveGameLoadStatus::MigrationFailed, "save schema migration chain is incomplete");
    }

    SaveGameLoadResult result{ .status = SaveGameLoadStatus::Ok, .save = {}, .diagnostic = {} };
    result.save.SetEntries(std::move(entries));
    result.migrated = !authenticated;
    return result;
}

std::span<const SaveGameMigration> BuiltInSaveGameMigrations() {
    static const std::vector<SaveGameMigration> kMigrations{
        SaveGameMigration{ .fromVersion = 1U, .toVersion = 2U, .kind = SaveGameMigrationKind::NoOp },
        SaveGameMigration{ .fromVersion = 2U, .toVersion = 3U, .kind = SaveGameMigrationKind::NoOp },
    };
    return kMigrations;
}

} // namespace kb::save
