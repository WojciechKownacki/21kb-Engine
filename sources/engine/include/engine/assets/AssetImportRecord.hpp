#pragma once

#include "engine/assets/AssetImportTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::assets {

class AssetManager;

// What an import left behind, kept beside the main asset as `<asset>.importrecord`: where the model came from, the options it was imported
// with, and the content hash of every asset the import wrote (the model itself, its skeleton, clips, materials and textures) at the moment it
// wrote it. A reimport compares those hashes with the files now: a file whose hash changed was edited since the import, and overwriting it
// would lose that edit.
inline constexpr std::string_view kAssetImportRecordExtension = ".importrecord";

struct AssetImportRecordEntry {
    std::string virtualPath;
    std::string assetType;
    std::uint64_t hash = 0U;

    [[nodiscard]] bool operator==(const AssetImportRecordEntry&) const noexcept = default;
};

struct AssetImportRecord {
    std::string sourceName;
    std::uint64_t sourceHash = 0U;
    std::uint16_t optionFlags = 0U;
    AssetMeshImportTransform transform{};
    std::vector<AssetImportRecordEntry> entries;

    [[nodiscard]] const AssetImportRecordEntry* Find(std::string_view virtualPath) const noexcept;
    // Adds or replaces the entry of `virtualPath`.
    void Set(std::string virtualPath, std::string assetType, std::uint64_t hash);
    [[nodiscard]] bool operator==(const AssetImportRecord&) const noexcept = default;
};

class AssetImportRecordIO final {
public:
    AssetImportRecordIO() = delete;

    [[nodiscard]] static std::filesystem::path PathFor(const std::filesystem::path& mainAssetPhysicalPath);
    [[nodiscard]] static std::string Encode(const AssetImportRecord& record);
    // Strict: an unknown line, a missing header or a value out of range gives no record.
    [[nodiscard]] static std::optional<AssetImportRecord> Parse(std::string_view text, std::string* error = nullptr);
    [[nodiscard]] static std::optional<AssetImportRecord> Read(const std::filesystem::path& path, std::string* error = nullptr);
    [[nodiscard]] static bool Write(const std::filesystem::path& path, const AssetImportRecord& record, std::string* error = nullptr);
};

// The hash an import record keeps for a file (the asset registry's content hash); 0 for a file that is not there.
[[nodiscard]] std::uint64_t HashAssetFile(const std::filesystem::path& path) noexcept;

// What each asset of a reimport would become, from the files now, the hashes the import recorded and the content the reimport would write.
// `recorded` may be null (an import from before records: nothing is known about local edits, so nothing is called one).
struct AssetImportPlanEntry {
    std::string virtualPath;
    std::string assetType;
    std::uint64_t newHash = 0U;
};
[[nodiscard]] std::vector<AssetImportChange> PlanImportChanges(const AssetManager& manager, const AssetImportRecord* recorded,
    const std::vector<AssetImportPlanEntry>& targets);

// A reimport that overwrites files keeps the old ones here first, so it can be undone: `Saved/ImportBackups/<id>/` of the project.
struct AssetImportBackupFile {
    std::string virtualPath;
    std::filesystem::path physicalPath;
};

class AssetImportBackups final {
public:
    AssetImportBackups() = delete;

    [[nodiscard]] static std::optional<std::filesystem::path> Root(const AssetManager& manager);
    // Copies every file that exists now (and its .meta) and notes every file that does not, which a restore then removes. Returns the id.
    [[nodiscard]] static std::optional<std::string> Create(const AssetManager& manager, const std::vector<AssetImportBackupFile>& files, std::string* error = nullptr);
    // Puts the old files back, removes the files the reimport created and deletes the backup. The caller discovers assets again.
    [[nodiscard]] static bool Restore(const AssetManager& manager, std::string_view id, std::string* error = nullptr);
    // Backup ids, oldest first.
    [[nodiscard]] static std::vector<std::string> List(const AssetManager& manager);
};

} // namespace kb::assets
