#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/save/SaveGame.hpp"
#include "engine/save/SaveGameMigration.hpp"
#include "engine/save/SaveGameService.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path SaveTestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_engine_save_game_tests";
}

void ResetSaveTestRoot() {
    std::error_code error;
    std::filesystem::remove_all(SaveTestRoot(), error);
    std::filesystem::create_directories(SaveTestRoot(), error);
    kb::tests::Require(!error, "SaveGame test root could not be prepared");
}

void WriteRawFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    kb::tests::Require(output.is_open(), "SaveGame raw fixture file could not be opened");
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// LIB-162: full disk round trip of every scalar type through the versioned,
// atomic Save/Load, plus the honest typed-miss contract.
void RunSaveGameRoundTripTest() {
    ResetSaveTestRoot();
    const std::filesystem::path path = SaveTestRoot() / "slot0.kbsave";

    kb::save::SaveGame save;
    save.SetBool("flag", true);
    save.SetInt("score", 4242);
    save.SetFloat("volume", 0.75);
    save.SetString("name", "Player One");
    kb::tests::Require(save.Count() == 4, "SaveGame must hold every set entry");

    // A typed getter is an honest miss for a wrong-type or absent key.
    std::int64_t asInt = 0;
    kb::tests::Require(!save.GetInt("name", asInt), "GetInt on a String entry must miss, never coerce");
    kb::tests::Require(!save.GetInt("missing", asInt), "GetInt on an absent key must miss");

    kb::tests::Require(kb::save::SaveGameService::Save(path, save), "SaveGameService::Save must succeed");

    const kb::save::SaveGameLoadResult loaded = kb::save::SaveGameService::Load(path);
    kb::tests::Require(loaded.Succeeded() && loaded.status == kb::save::SaveGameLoadStatus::Ok, "SaveGameService::Load must succeed for a just-written save");
    kb::tests::Require(loaded.save.Count() == 4, "A loaded save must carry every entry");

    bool flag = false;
    std::int64_t score = 0;
    double volume = 0.0;
    std::string name;
    kb::tests::Require(loaded.save.GetBool("flag", flag) && flag, "Round-tripped bool must survive");
    kb::tests::Require(loaded.save.GetInt("score", score) && score == 4242, "Round-tripped int must survive exactly");
    kb::tests::Require(loaded.save.GetFloat("volume", volume) && kb::tests::NearlyEqual(static_cast<float>(volume), 0.75F), "Round-tripped float must survive");
    kb::tests::Require(loaded.save.GetString("name", name) && name == "Player One", "Round-tripped string must survive");
}

// LIB-162: the write is atomic — no ".tmp" is left behind, and overwriting an
// existing save replaces it cleanly.
void RunSaveGameAtomicWriteTest() {
    ResetSaveTestRoot();
    const std::filesystem::path path = SaveTestRoot() / "atomic.kbsave";
    const std::filesystem::path tempPath = std::filesystem::path{ path }.concat(".tmp");

    kb::save::SaveGame first;
    first.SetInt("v", 1);
    kb::tests::Require(kb::save::SaveGameService::Save(path, first), "First atomic save must succeed");
    kb::tests::Require(std::filesystem::exists(path), "The save file must exist after Save");
    kb::tests::Require(!std::filesystem::exists(tempPath), "The temp file must not remain after an atomic Save");

    kb::save::SaveGame second;
    second.SetInt("v", 2);
    kb::tests::Require(kb::save::SaveGameService::Save(path, second), "Overwriting an existing save must succeed");
    kb::tests::Require(!std::filesystem::exists(tempPath), "The temp file must not remain after overwriting");
    const kb::save::SaveGameLoadResult loaded = kb::save::SaveGameService::Load(path);
    std::int64_t v = 0;
    kb::tests::Require(loaded.Succeeded() && loaded.save.GetInt("v", v) && v == 2, "The overwrite must have fully replaced the previous save");

    // Save/Load symmetry: a save carrying an entry that exceeds the format
    // limits must be REFUSED at Save time (never written), not written and
    // then rejected by Load — otherwise a successful Save would not be
    // reloadable.
    const std::filesystem::path oversizePath = SaveTestRoot() / "oversize.kbsave";
    kb::save::SaveGame oversize;
    oversize.SetInt(std::string(2000U, 'k'), 1); // key longer than kMaxKeyBytes (1024)
    kb::tests::Require(!kb::save::SaveGameService::Save(oversizePath, oversize), "Save must refuse an entry that exceeds the format limits");
    kb::tests::Require(!std::filesystem::exists(oversizePath), "A refused Save must not write any file");
}

// LIB-162: every corruption / bad-header path returns a precise status rather
// than crashing or silently returning empty data.
void RunSaveGameCorruptionTest() {
    ResetSaveTestRoot();

    // Missing file.
    kb::tests::Require(kb::save::SaveGameService::Load(SaveTestRoot() / "does_not_exist.kbsave").status == kb::save::SaveGameLoadStatus::FileNotFound,
        "Loading a missing file must report FileNotFound");

    // The format magic (a contract constant): "21KBSAV\0".
    const std::array<std::uint8_t, 8> magic{ '2', '1', 'K', 'B', 'S', 'A', 'V', 0 };
    const auto appendUInt32 = [](std::vector<std::uint8_t>& out, std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
        }
    };

    // Wrong magic.
    const std::filesystem::path badMagicPath = SaveTestRoot() / "badmagic.kbsave";
    WriteRawFile(badMagicPath, std::vector<std::uint8_t>{ 'N', 'O', 'P', 'E', 'N', 'O', 'P', 'E', 1, 0, 0, 0 });
    kb::tests::Require(kb::save::SaveGameService::Load(badMagicPath).status == kb::save::SaveGameLoadStatus::BadMagic, "A file with the wrong magic must report BadMagic");

    // Version 0 (reserved / invalid).
    std::vector<std::uint8_t> versionZero{ magic.begin(), magic.end() };
    appendUInt32(versionZero, 0U);
    appendUInt32(versionZero, 0U);
    const std::filesystem::path versionZeroPath = SaveTestRoot() / "v0.kbsave";
    WriteRawFile(versionZeroPath, versionZero);
    kb::tests::Require(kb::save::SaveGameService::Load(versionZeroPath).status == kb::save::SaveGameLoadStatus::UnsupportedVersion, "Schema version 0 must report UnsupportedVersion");

    // A version newer than this build understands.
    std::vector<std::uint8_t> versionFuture{ magic.begin(), magic.end() };
    appendUInt32(versionFuture, 9999U);
    appendUInt32(versionFuture, 0U);
    const std::filesystem::path versionFuturePath = SaveTestRoot() / "future.kbsave";
    WriteRawFile(versionFuturePath, versionFuture);
    kb::tests::Require(kb::save::SaveGameService::Load(versionFuturePath).status == kb::save::SaveGameLoadStatus::UnsupportedVersion, "A future schema version must report UnsupportedVersion, never a partial read");

    // A valid header claiming 2 entries but with a truncated body.
    std::vector<std::uint8_t> truncated{ magic.begin(), magic.end() };
    appendUInt32(truncated, 1U); // schema version 1
    truncated.push_back(0U);     // domain 0 (SaveGame)
    appendUInt32(truncated, 2U); // claims 2 entries
    appendUInt32(truncated, 3U); // a key length of 3...
    truncated.push_back('a');    // ...but only 1 byte of it
    const std::filesystem::path truncatedPath = SaveTestRoot() / "truncated.kbsave";
    WriteRawFile(truncatedPath, truncated);
    kb::tests::Require(kb::save::SaveGameService::Load(truncatedPath).status == kb::save::SaveGameLoadStatus::Corrupt, "A truncated payload must report Corrupt, never read out of bounds");

    // A well-formed header carrying an unrecognized domain byte (neither
    // SaveGame nor UserSettings) must be rejected as WrongDomain, not accepted.
    std::vector<std::uint8_t> garbageDomain{ magic.begin(), magic.end() };
    appendUInt32(garbageDomain, 1U); // schema version 1
    garbageDomain.push_back(99U);    // an unknown domain
    appendUInt32(garbageDomain, 0U); // zero entries
    const std::filesystem::path garbageDomainPath = SaveTestRoot() / "garbagedomain.kbsave";
    WriteRawFile(garbageDomainPath, garbageDomain);
    kb::tests::Require(kb::save::SaveGameService::Load(garbageDomainPath, kb::save::SaveDomain::SaveGame).status == kb::save::SaveGameLoadStatus::WrongDomain,
        "A file with an unrecognized domain byte must be rejected as WrongDomain");
}

// LIB-163: the persistence domains are separated — a file written for one
// domain cannot be loaded as another (WrongDomain), so save games and user
// settings never cross-contaminate even when they share the kb::save format.
void RunSaveGameDomainSeparationTest() {
    ResetSaveTestRoot();
    const std::filesystem::path savePath = SaveTestRoot() / "progress.kbsave";
    const std::filesystem::path settingsPath = SaveTestRoot() / "settings.kbsave";

    kb::save::SaveGame progress;
    progress.SetInt("level", 7);
    kb::tests::Require(kb::save::SaveGameService::Save(savePath, progress, kb::save::SaveDomain::SaveGame), "Saving a SaveGame-domain file must succeed");

    kb::save::SaveGame settings;
    settings.SetFloat("masterVolume", 0.8);
    kb::tests::Require(kb::save::SaveGameService::Save(settingsPath, settings, kb::save::SaveDomain::UserSettings), "Saving a UserSettings-domain file must succeed");

    // Same-domain load works.
    kb::tests::Require(kb::save::SaveGameService::Load(savePath, kb::save::SaveDomain::SaveGame).Succeeded(), "Loading a SaveGame file as SaveGame must succeed");
    kb::tests::Require(kb::save::SaveGameService::Load(settingsPath, kb::save::SaveDomain::UserSettings).Succeeded(), "Loading a UserSettings file as UserSettings must succeed");

    // Cross-domain load is rejected (the core of the separation).
    kb::tests::Require(kb::save::SaveGameService::Load(savePath, kb::save::SaveDomain::UserSettings).status == kb::save::SaveGameLoadStatus::WrongDomain,
        "Loading a SaveGame file as UserSettings must be rejected as WrongDomain");
    kb::tests::Require(kb::save::SaveGameService::Load(settingsPath, kb::save::SaveDomain::SaveGame).status == kb::save::SaveGameLoadStatus::WrongDomain,
        "Loading a UserSettings file as SaveGame must be rejected as WrongDomain");
}

// LIB-162: the schema migration mechanism, exercised directly with real steps
// (the shipped built-in table is empty because v1 is the first schema).
void RunSaveGameMigrationTest() {
    using kb::save::SaveGameMigration;
    using kb::save::SaveGameMigrationKind;
    using kb::save::SaveValue;

    // A no-op when already at the target version.
    {
        std::unordered_map<std::string, kb::save::SaveValue> entries{ { "a", SaveValue::MakeInt(1) } };
        kb::tests::Require(kb::save::ApplySaveGameMigrations(entries, 3U, 3U, {}), "Migrating from a version equal to the target must be a success no-op");
        kb::tests::Require(entries.size() == 1 && entries.at("a").intValue == 1, "A no-op migration must not alter the entries");
    }

    // RenameKey + SetDefault + RemoveKey across a two-hop chain (1 -> 2 -> 3).
    {
        std::unordered_map<std::string, kb::save::SaveValue> entries{
            { "oldName", SaveValue::MakeString("Hero") },
            { "legacyFlag", SaveValue::MakeBool(true) },
        };
        const std::array<SaveGameMigration, 3> migrations{
            SaveGameMigration{ .fromVersion = 1, .toVersion = 2, .kind = SaveGameMigrationKind::RenameKey, .key = "oldName", .newKey = "displayName" },
            SaveGameMigration{ .fromVersion = 2, .toVersion = 3, .kind = SaveGameMigrationKind::SetDefault, .key = "difficulty", .defaultValue = SaveValue::MakeInt(2) },
            SaveGameMigration{ .fromVersion = 2, .toVersion = 3, .kind = SaveGameMigrationKind::RemoveKey, .key = "legacyFlag" },
        };
        kb::tests::Require(kb::save::ApplySaveGameMigrations(entries, 1U, 3U, migrations), "A complete migration chain must succeed");
        kb::tests::Require(!entries.contains("oldName") && entries.contains("displayName") && entries.at("displayName").stringValue == "Hero", "RenameKey must move the value to the new key");
        kb::tests::Require(entries.contains("difficulty") && entries.at("difficulty").intValue == 2, "SetDefault must insert the default for a key old saves lacked");
        kb::tests::Require(!entries.contains("legacyFlag"), "RemoveKey must drop a key the new schema no longer understands");
    }

    // SetDefault must NOT overwrite a value the save already carries.
    {
        std::unordered_map<std::string, kb::save::SaveValue> entries{ { "difficulty", SaveValue::MakeInt(5) } };
        const std::array<SaveGameMigration, 1> migrations{
            SaveGameMigration{ .fromVersion = 1, .toVersion = 2, .kind = SaveGameMigrationKind::SetDefault, .key = "difficulty", .defaultValue = SaveValue::MakeInt(2) },
        };
        kb::tests::Require(kb::save::ApplySaveGameMigrations(entries, 1U, 2U, migrations), "SetDefault migration must succeed");
        kb::tests::Require(entries.at("difficulty").intValue == 5, "SetDefault must keep an existing value, only supplying a default when absent");
    }

    // A gap in the chain (no step bridges 1 -> 2) fails and leaves entries untouched.
    {
        std::unordered_map<std::string, kb::save::SaveValue> entries{ { "a", SaveValue::MakeInt(1) } };
        const std::array<SaveGameMigration, 1> migrations{
            SaveGameMigration{ .fromVersion = 2, .toVersion = 3, .kind = SaveGameMigrationKind::RemoveKey, .key = "a" },
        };
        kb::tests::Require(!kb::save::ApplySaveGameMigrations(entries, 1U, 3U, migrations), "A migration chain with a gap must fail rather than half-migrate");
        kb::tests::Require(entries.size() == 1 && entries.contains("a"), "A failed migration must leave the caller's entries untouched");
    }

    // A save from a newer schema than the target cannot be downgraded.
    {
        std::unordered_map<std::string, kb::save::SaveValue> entries{ { "a", SaveValue::MakeInt(1) } };
        kb::tests::Require(!kb::save::ApplySaveGameMigrations(entries, 5U, 3U, {}), "A newer-than-target schema must be rejected, never silently accepted");
    }
}

} // namespace

namespace kb::tests {

void RunSaveGameTests() {
    RunSaveGameRoundTripTest();
    RunSaveGameAtomicWriteTest();
    RunSaveGameCorruptionTest();
    RunSaveGameMigrationTest();
    RunSaveGameDomainSeparationTest();
}

} // namespace kb::tests
