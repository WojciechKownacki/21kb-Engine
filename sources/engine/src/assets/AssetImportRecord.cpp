#include "engine/assets/AssetImportRecord.hpp"

#include "assets/AssetFileSystem.hpp"
#include "engine/assets/AssetManager.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace kb::assets {
namespace {

constexpr std::string_view kHeader = "21kb-import-record 1";
constexpr std::size_t kMaxEntries = 4096U;

[[nodiscard]] bool ParseUnsigned(std::string_view text, std::uint64_t& value) {
    if (text.empty()) return false;
    const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), value);
    return code == std::errc{} && end == text.data() + text.size();
}

[[nodiscard]] bool ParseFloat(std::string_view text, float& value) {
    if (text.empty()) return false;
    const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), value);
    return code == std::errc{} && end == text.data() + text.size() && std::isfinite(value);
}

[[nodiscard]] std::string FloatText(float value) {
    char buffer[40]{};
    const auto [end, code] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return code == std::errc{} ? std::string{ buffer, end } : std::string{ "0" };
}

[[nodiscard]] std::vector<std::string_view> Split(std::string_view line, std::size_t maxParts) {
    std::vector<std::string_view> parts;
    while (!line.empty() && parts.size() + 1U < maxParts) {
        const std::size_t space = line.find(' ');
        if (space == std::string_view::npos) break;
        parts.push_back(line.substr(0U, space));
        line.remove_prefix(space + 1U);
    }
    parts.push_back(line);
    return parts;
}

[[nodiscard]] bool Fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

[[nodiscard]] std::string BackupStamp() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

[[nodiscard]] std::optional<std::filesystem::path> GameRoot(const AssetManager& manager) {
    for (const AssetMount& mount : manager.Mounts().Mounts()) {
        if (mount.name == "Game") return mount.root;
    }
    return std::nullopt;
}

[[nodiscard]] std::filesystem::path MetaOf(std::filesystem::path path) {
    path.replace_extension(".meta");
    return path;
}

} // namespace

const AssetImportRecordEntry* AssetImportRecord::Find(std::string_view virtualPath) const noexcept {
    const auto found = std::ranges::find_if(entries, [&](const AssetImportRecordEntry& entry) { return entry.virtualPath == virtualPath; });
    return found == entries.end() ? nullptr : &*found;
}

void AssetImportRecord::Set(std::string virtualPath, std::string assetType, std::uint64_t hash) {
    const auto found = std::ranges::find_if(entries, [&](const AssetImportRecordEntry& entry) { return entry.virtualPath == virtualPath; });
    if (found != entries.end()) {
        found->assetType = std::move(assetType);
        found->hash = hash;
        return;
    }
    entries.push_back({ std::move(virtualPath), std::move(assetType), hash });
}

std::filesystem::path AssetImportRecordIO::PathFor(const std::filesystem::path& mainAssetPhysicalPath) {
    std::filesystem::path path = mainAssetPhysicalPath;
    path.replace_extension(std::string{ kAssetImportRecordExtension });
    return path;
}

std::string AssetImportRecordIO::Encode(const AssetImportRecord& record) {
    std::ostringstream out;
    out << kHeader << '\n';
    out << "source " << record.sourceHash << ' ' << record.sourceName << '\n';
    out << "options " << record.optionFlags << ' ' << FloatText(record.transform.scale) << ' ' << FloatText(record.transform.rotationDegrees[0]) << ' '
        << FloatText(record.transform.rotationDegrees[1]) << ' ' << FloatText(record.transform.rotationDegrees[2]) << '\n';
    for (const AssetImportRecordEntry& entry : record.entries) {
        out << "asset " << entry.hash << ' ' << (entry.assetType.empty() ? "-" : entry.assetType) << ' ' << entry.virtualPath << '\n';
    }
    return out.str();
}

std::optional<AssetImportRecord> AssetImportRecordIO::Parse(std::string_view text, std::string* error) {
    AssetImportRecord record;
    bool sawHeader = false;
    bool sawSource = false;
    bool sawOptions = false;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0U, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1U);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
        if (line.empty()) continue;
        if (!sawHeader) {
            if (line != kHeader) { static_cast<void>(Fail(error, "not an import record")); return std::nullopt; }
            sawHeader = true;
            continue;
        }
        if (line.starts_with("source ")) {
            const auto parts = Split(line.substr(7U), 2U);
            if (sawSource || parts.size() != 2U || !ParseUnsigned(parts[0], record.sourceHash)) { static_cast<void>(Fail(error, "bad source line")); return std::nullopt; }
            record.sourceName = std::string{ parts[1] };
            sawSource = true;
        } else if (line.starts_with("options ")) {
            const auto parts = Split(line.substr(8U), 5U);
            std::uint64_t flags = 0U;
            if (sawOptions || parts.size() != 5U || !ParseUnsigned(parts[0], flags) || flags > 0xFFFFU || !ParseFloat(parts[1], record.transform.scale) ||
                !ParseFloat(parts[2], record.transform.rotationDegrees[0]) || !ParseFloat(parts[3], record.transform.rotationDegrees[1]) ||
                !ParseFloat(parts[4], record.transform.rotationDegrees[2])) {
                static_cast<void>(Fail(error, "bad options line"));
                return std::nullopt;
            }
            record.optionFlags = static_cast<std::uint16_t>(flags);
            sawOptions = true;
        } else if (line.starts_with("asset ")) {
            const auto parts = Split(line.substr(6U), 3U);
            AssetImportRecordEntry entry;
            if (parts.size() != 3U || !ParseUnsigned(parts[0], entry.hash) || parts[2].empty() || record.entries.size() >= kMaxEntries) {
                static_cast<void>(Fail(error, "bad asset line"));
                return std::nullopt;
            }
            entry.assetType = parts[1] == "-" ? std::string{} : std::string{ parts[1] };
            entry.virtualPath = std::string{ parts[2] };
            if (record.Find(entry.virtualPath) != nullptr) { static_cast<void>(Fail(error, "an asset is listed twice")); return std::nullopt; }
            record.entries.push_back(std::move(entry));
        } else {
            static_cast<void>(Fail(error, "unknown line: " + std::string{ line.substr(0U, 20U) }));
            return std::nullopt;
        }
    }
    if (!sawHeader || !sawSource || !sawOptions) { static_cast<void>(Fail(error, "the record is incomplete")); return std::nullopt; }
    return record;
}

std::optional<AssetImportRecord> AssetImportRecordIO::Read(const std::filesystem::path& path, std::string* error) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) { static_cast<void>(Fail(error, "the import record could not be opened")); return std::nullopt; }
    const std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    return Parse(text, error);
}

bool AssetImportRecordIO::Write(const std::filesystem::path& path, const AssetImportRecord& record, std::string* error) {
    const std::filesystem::path temp = path.string() + ".tmp";
    {
        std::ofstream output{ temp, std::ios::binary | std::ios::trunc };
        if (!output.is_open()) return Fail(error, "the import record could not be written");
        output << Encode(record);
        if (!output.good()) { std::error_code ignored; std::filesystem::remove(temp, ignored); return Fail(error, "the import record could not be written"); }
    }
    std::error_code rename;
    std::filesystem::rename(temp, path, rename);
    if (rename) { std::error_code ignored; std::filesystem::remove(temp, ignored); return Fail(error, "the import record could not be replaced"); }
    return true;
}

std::uint64_t HashAssetFile(const std::filesystem::path& path) noexcept {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) return 0U;
    return AssetFileSystem::HashFile(path);
}

std::vector<AssetImportChange> PlanImportChanges(const AssetManager& manager, const AssetImportRecord* recorded, const std::vector<AssetImportPlanEntry>& targets) {
    std::vector<AssetImportChange> changes;
    std::set<std::string> targeted;
    for (const AssetImportPlanEntry& target : targets) {
        targeted.insert(target.virtualPath);
        AssetImportChange change;
        change.virtualPath = target.virtualPath;
        change.assetType = target.assetType;
        change.newHash = target.newHash;
        if (const AssetImportRecordEntry* entry = recorded != nullptr ? recorded->Find(target.virtualPath) : nullptr) change.recordedHash = entry->hash;
        const std::optional<std::filesystem::path> physical = manager.Mounts().Resolve(std::filesystem::path{ target.virtualPath });
        change.currentHash = physical.has_value() ? HashAssetFile(*physical) : 0U;
        if (change.currentHash == 0U) {
            change.kind = AssetImportChangeKind::Added;
        } else if (change.currentHash == change.newHash) {
            change.kind = AssetImportChangeKind::Unchanged;
        } else if (change.recordedHash != 0U && change.recordedHash != change.currentHash) {
            change.kind = AssetImportChangeKind::LocalOverride;
        } else {
            change.kind = AssetImportChangeKind::Changed;
        }
        changes.push_back(std::move(change));
    }
    if (recorded != nullptr) {
        for (const AssetImportRecordEntry& entry : recorded->entries) {
            if (targeted.contains(entry.virtualPath)) continue;
            AssetImportChange change;
            change.virtualPath = entry.virtualPath;
            change.assetType = entry.assetType;
            change.kind = AssetImportChangeKind::Removed;
            change.recordedHash = entry.hash;
            const std::optional<std::filesystem::path> physical = manager.Mounts().Resolve(std::filesystem::path{ entry.virtualPath });
            change.currentHash = physical.has_value() ? HashAssetFile(*physical) : 0U;
            changes.push_back(std::move(change));
        }
    }
    return changes;
}

std::optional<std::filesystem::path> AssetImportBackups::Root(const AssetManager& manager) {
    const std::optional<std::filesystem::path> assets = GameRoot(manager);
    if (!assets.has_value()) return std::nullopt;
    return assets->parent_path() / "Saved" / "ImportBackups";
}

std::optional<std::string> AssetImportBackups::Create(const AssetManager& manager, const std::vector<AssetImportBackupFile>& files, std::string* error) {
    const std::optional<std::filesystem::path> root = Root(manager);
    if (!root.has_value()) { static_cast<void>(Fail(error, "the project has no Assets folder to keep backups beside")); return std::nullopt; }
    std::string id = BackupStamp();
    std::error_code ec;
    for (int attempt = 0; attempt < 100 && std::filesystem::exists(*root / id, ec); ++attempt) id = BackupStamp() + "_" + std::to_string(attempt);
    const std::filesystem::path directory = *root / id;
    std::filesystem::create_directories(directory / "files", ec);
    if (ec) { static_cast<void>(Fail(error, "the backup folder could not be created")); return std::nullopt; }
    std::ostringstream manifest;
    manifest << "21kb-import-backup 1\n";
    std::size_t index = 0U;
    for (const AssetImportBackupFile& file : files) {
        for (const bool meta : { false, true }) {
            const std::filesystem::path source = meta ? MetaOf(file.physicalPath) : file.physicalPath;
            const bool existed = std::filesystem::is_regular_file(source, ec) && !ec;
            const std::string stored = existed ? "files/" + std::to_string(index++) : "-";
            if (existed) {
                std::filesystem::copy_file(source, directory / stored, std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) { std::filesystem::remove_all(directory, ec); static_cast<void>(Fail(error, "a file could not be backed up: " + source.string())); return std::nullopt; }
            }
            const std::u8string path = source.generic_u8string();
            manifest << "file " << stored << ' ' << std::string{ reinterpret_cast<const char*>(path.data()), path.size() } << '\n';
        }
    }
    std::ofstream out{ directory / "manifest.txt", std::ios::binary | std::ios::trunc };
    out << manifest.str();
    if (!out.good()) { out.close(); std::filesystem::remove_all(directory, ec); static_cast<void>(Fail(error, "the backup manifest could not be written")); return std::nullopt; }
    return id;
}

bool AssetImportBackups::Restore(const AssetManager& manager, std::string_view id, std::string* error) {
    const std::optional<std::filesystem::path> root = Root(manager);
    if (!root.has_value()) return Fail(error, "the project has no Assets folder");
    if (id.empty() || id.find_first_of("/\\.") != std::string_view::npos) return Fail(error, "not a backup id");
    const std::filesystem::path directory = *root / std::string{ id };
    std::ifstream in{ directory / "manifest.txt", std::ios::binary };
    if (!in.is_open()) return Fail(error, "there is no backup " + std::string{ id });
    std::string line;
    if (!std::getline(in, line) || line.rfind("21kb-import-backup 1", 0U) != 0U) return Fail(error, "the backup manifest is not valid");
    struct Entry { std::string stored; std::filesystem::path target; };
    std::vector<Entry> entries;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("file ", 0U) != 0U) continue;
        const std::string rest = line.substr(5U);
        const std::size_t space = rest.find(' ');
        if (space == std::string::npos) return Fail(error, "the backup manifest is not valid");
        const std::u8string target(reinterpret_cast<const char8_t*>(rest.data() + space + 1U), rest.size() - space - 1U);
        entries.push_back({ rest.substr(0U, space), std::filesystem::path{ target } });
    }
    in.close();
    std::error_code ec;
    // Every target must sit inside the project's Assets folder: a backup is not a way to write elsewhere.
    const std::optional<std::filesystem::path> assets = GameRoot(manager);
    if (!assets.has_value()) return Fail(error, "the project has no Assets folder");
    for (const Entry& entry : entries) {
        const std::filesystem::path relative = entry.target.lexically_normal().lexically_relative(assets->lexically_normal());
        if (relative.empty() || *relative.begin() == "..") return Fail(error, "the backup names a file outside the project's Assets folder");
    }
    for (const Entry& entry : entries) {
        if (entry.stored == "-") {
            std::filesystem::remove(entry.target, ec);
        } else {
            std::filesystem::create_directories(entry.target.parent_path(), ec);
            std::filesystem::copy_file(directory / entry.stored, entry.target, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) return Fail(error, "a file could not be restored: " + entry.target.string());
        }
    }
    std::filesystem::remove_all(directory, ec);
    return true;
}

std::vector<std::string> AssetImportBackups::List(const AssetManager& manager) {
    std::vector<std::string> ids;
    const std::optional<std::filesystem::path> root = Root(manager);
    std::error_code ec;
    if (!root.has_value() || !std::filesystem::is_directory(*root, ec)) return ids;
    for (const auto& entry : std::filesystem::directory_iterator(*root, ec)) {
        if (entry.is_directory(ec) && std::filesystem::exists(entry.path() / "manifest.txt", ec)) ids.push_back(entry.path().filename().string());
    }
    std::ranges::sort(ids, [](const std::string& left, const std::string& right) { return left.size() != right.size() ? left.size() < right.size() : left < right; });
    return ids;
}

} // namespace kb::assets
