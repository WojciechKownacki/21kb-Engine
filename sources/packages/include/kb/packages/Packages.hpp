#pragma once

#include "engine/mods/ModVersion.hpp"
#include "engine/security/ReleaseKeys.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Project packages (kb_cli package, the Packages panel of the editor, docs/packages.md): a package is a folder with a
// package.json and content (assets, scripts, native modules). A registry is an index.json that lists package versions with the
// archive of each (a local folder or an http(s) address); a project asks for some in packages.json, the resolver picks
// exact versions, they are copied to Assets/Packages/<name> (the asset path /Game/Packages/<name>) and
// packages.lock.json records the versions and the SHA-512 sums of every installed file.
// Names and versions follow the rules mods use (kb::mods::IsValidModId, ModVersion and its ranges).
// Every function works on the calling thread and may block on the disk or the network: call them off the UI thread.
namespace kb::packages {

inline constexpr std::string_view kManifestFile = "package.json";
inline constexpr std::string_view kProjectFile = "packages.json";
inline constexpr std::string_view kLockFile = "packages.lock.json";
inline constexpr std::string_view kIndexFile = "index.json";
inline constexpr std::string_view kArchiveExtension = ".kbpackage";
// The signatures of an index: the file beside it with this name added (index.json.sig).
inline constexpr std::string_view kIndexSignatureSuffix = ".sig";
// The largest archive, as stored: the HTTP client's own limit.
inline constexpr std::uint64_t kMaxArchiveBytes = 256U * 1024U * 1024U;

// A version range as written ("^1.2", "~2.1.0", ">=1 <3", "*") and parsed; short versions count as x.0 and x.0.0.
struct Dependency {
    std::string text;
    kb::mods::ModVersionRange range;

    [[nodiscard]] bool operator==(const Dependency&) const = default;
};
using Dependencies = std::map<std::string, Dependency>;

[[nodiscard]] bool ParseRange(std::string_view text, Dependency& out);

// A native module a package carries for one platform: the engine module `name` in `file` (a path inside the package), built
// for `platform` ("windows-x64", "linux-x64", ...) against module interface `abi`.
struct ModuleEntry {
    std::string name;
    std::string platform;
    std::string file;
    std::uint32_t abi = 0U;

    [[nodiscard]] bool operator==(const ModuleEntry&) const = default;
};

struct Manifest {
    std::string name;
    kb::mods::ModVersion version;
    std::string description;
    Dependencies dependencies;
    std::vector<ModuleEntry> modules;
};

// The platform of this build as packages name it, such as "windows-x64".
[[nodiscard]] std::string_view CurrentModulePlatform() noexcept;
// Refuses, with a sentence for a person, a package whose modules cannot be used here: none is built for this platform or
// one was built for another version of the engine's module interface. A package without modules always passes.
[[nodiscard]] bool CheckModules(const Manifest& manifest, std::string& error);

// One version of a package in a registry index.
struct IndexEntry {
    Manifest manifest;
    // The archive as the index gives it: a path or address relative to the index, or an https address.
    std::string archive;
    // SHA-512 of the archive, 128 lower case hexadecimal digits, and its size in bytes.
    std::string sha512;
    std::uint64_t size = 0U;
};

struct RegistryOptions {
    // Relative registry paths are relative to this, and downloads and unpacking work below its Saved folder.
    std::filesystem::path projectRoot;
    // Where downloaded archives are kept, by their sum; empty: <projectRoot>/Saved/PackageCache.
    std::filesystem::path cacheFolder;
    // Public keys (64 hexadecimal digits) the project trusts. When there are any, the index must carry a valid signature
    // by one of them (index.json.sig), or Open refuses it before a single sum of it is used.
    std::vector<std::string> trustedKeys;
};

// The registry: the index of a local folder (or index.json file) or of an http(s) address.
class Registry {
public:
    // Reads the index. Plain http:// is accepted for the local machine only.
    [[nodiscard]] static std::optional<Registry> Open(std::string_view location, const RegistryOptions& options, std::string& error);

    [[nodiscard]] const std::string& Location() const noexcept { return location_; }
    // True when the index was checked against a trusted key; false for a registry the project has no keys for.
    [[nodiscard]] bool Signed() const noexcept { return signed_; }
    [[nodiscard]] std::vector<std::string> Names() const;
    // The versions of a package, lowest first.
    [[nodiscard]] std::vector<kb::mods::ModVersion> Versions(const std::string& name) const;
    [[nodiscard]] const IndexEntry* Find(const std::string& name, const kb::mods::ModVersion& version) const;
    [[nodiscard]] bool Read(const std::string& name, const kb::mods::ModVersion& version, Manifest& out, std::string& error) const;
    // Makes the files of a version available as the folder <staging>/<name>/<version>: reads or downloads the archive
    // (a download is kept in the cache), checks its sum before unpacking and unpacks it safely.
    [[nodiscard]] bool Fetch(
        const std::string& name, const kb::mods::ModVersion& version, const std::filesystem::path& staging, std::filesystem::path& folder, std::string& error) const;

private:
    [[nodiscard]] bool LoadArchive(const IndexEntry& entry, std::vector<std::uint8_t>& bytes, std::string& error) const;

    std::string location_;
    // The index's folder or address, ending in a separator, for the archives it names.
    std::string base_;
    bool remote_ = false;
    bool signed_ = false;
    RegistryOptions options_;
    std::map<std::string, std::map<kb::mods::ModVersion, IndexEntry>> entries_;
};

// Builds the archive of a package folder into a local registry folder and adds its entry to the folder's index.json
// (made when missing). A published version is never replaced. With `keys` the index is signed by each of them (SignIndex);
// a registry that has signatures cannot be published into without keys, since the index would no longer match them.
[[nodiscard]] bool Publish(
    const std::filesystem::path& registryFolder,
    const std::filesystem::path& packageFolder,
    IndexEntry& published,
    std::string& error,
    std::span<const kb::security::ReleaseSigningKey> keys = {});
// Writes index.json.sig beside the registry's index: one Ed25519 signature of its exact bytes by each key. A registry
// signed with several keys lets a project move from one of them to another.
[[nodiscard]] bool SignIndex(const std::filesystem::path& registryFolder, std::span<const kb::security::ReleaseSigningKey> keys, std::string& error);

// packages.json: what the project asks for and where it takes it from.
struct ProjectPackages {
    // A registry folder (relative to the project when not absolute), index file or http(s) address; empty when none is set.
    std::string registry;
    // Public keys of the registry's publisher (64 lower case hexadecimal digits); see RegistryOptions::trustedKeys.
    std::vector<std::string> trustedKeys;
    Dependencies requested;

    [[nodiscard]] bool operator==(const ProjectPackages&) const = default;
};

// One installed package as packages.lock.json records it.
struct LockedPackage {
    kb::mods::ModVersion version;
    // SHA-512 over the sorted file names and file sums, as hexadecimal digits.
    std::string checksum;
    std::map<std::string, std::string> dependencies;
    // Sum of each file, by path inside the package.
    std::map<std::string, std::string> files;

    [[nodiscard]] bool operator==(const LockedPackage&) const = default;
};
using Lock = std::map<std::string, LockedPackage>;

// A missing file is an empty value.
[[nodiscard]] bool ReadProjectPackages(const std::filesystem::path& projectRoot, ProjectPackages& out, std::string& error);
[[nodiscard]] bool WriteProjectPackages(const std::filesystem::path& projectRoot, const ProjectPackages& value, std::string& error);
[[nodiscard]] bool ReadLock(const std::filesystem::path& projectRoot, Lock& out, std::string& error);
[[nodiscard]] bool WriteLock(const std::filesystem::path& projectRoot, const Lock& value, std::string& error);

// The version of every package the project needs: the highest that fits all requirements, the one in `preferred` when
// it fits. Fails with a sentence for a person on a missing package, requirements no version meets together, or a cycle.
[[nodiscard]] bool Resolve(
    const Registry& registry,
    const Dependencies& requested,
    const std::map<std::string, kb::mods::ModVersion>& preferred,
    std::map<std::string, kb::mods::ModVersion>& resolved,
    std::string& error);

// Where a package is installed.
[[nodiscard]] std::filesystem::path InstallFolder(const std::filesystem::path& projectRoot, const std::string& name);

// Makes the installed packages exactly `resolved`: removes the others, copies those that are missing or of another
// version, and updates `lock`. The archives are fetched, checked and unpacked before anything is changed.
[[nodiscard]] bool Install(
    const std::filesystem::path& projectRoot,
    const Registry& registry,
    const std::map<std::string, kb::mods::ModVersion>& resolved,
    Lock& lock,
    std::string& error);
// Removes the installed packages not in `keep`.
[[nodiscard]] bool Prune(const std::filesystem::path& projectRoot, const std::map<std::string, kb::mods::ModVersion>& keep, Lock& lock, std::string& error);

struct Problem {
    std::string package;
    // Path inside the package; empty when the whole package is meant.
    std::string file;
    // modified, missing, added or "not installed" / "checksum".
    std::string what;
};
// Compares what is installed with the lock; an empty result means every file is as installed.
[[nodiscard]] bool Verify(
    const std::filesystem::path& projectRoot, const Lock& lock, const Dependencies& requested, std::vector<Problem>& problems, std::string& error);

// Makes the plugin list of the project file name the modules of the installed packages that are built for this platform,
// each as the project-relative path of its installed file, and drops those of packages no longer installed. Entries the
// project owns are left alone; the enabled flag of a listed module is kept. `changed` tells whether the project file was
// written.
[[nodiscard]] bool SyncProjectModules(const std::filesystem::path& projectRoot, const Lock& lock, bool& changed, std::string& error);

// The files of a package folder with their sizes, by path inside it. Links and other special files are refused.
[[nodiscard]] bool ListFiles(const std::filesystem::path& folder, std::map<std::string, std::uintmax_t>& sizes, std::string& error);

// What a project says about its packages: what it asks for and what is installed. The operations below change the
// project and `state` together; they resolve, fetch and check everything first, so a refusal (an error that ends with
// "Nothing was changed.") leaves both as they were.
struct ProjectState {
    ProjectPackages config;
    Lock lock;
    // Set by an operation that changed which package modules the project lists; a running editor then reloads its modules.
    bool modulesChanged = false;
};
[[nodiscard]] bool ReadProjectState(const std::filesystem::path& projectRoot, ProjectState& state, std::string& error);
// How packages.json names a registry: an address as it is, a folder relative to the project when it can be.
[[nodiscard]] std::string RegistryText(const std::filesystem::path& projectRoot, std::string_view location);
[[nodiscard]] bool SetRegistry(const std::filesystem::path& projectRoot, std::string_view location, ProjectState& state, std::string& error);
// `range` empty asks for what is chosen now (^<version>); `keepRegistry` writes the registry into packages.json.
[[nodiscard]] bool AddPackage(
    const std::filesystem::path& projectRoot,
    const Registry& registry,
    const std::string& name,
    std::string_view range,
    bool keepRegistry,
    ProjectState& state,
    std::string& error);
// Moves packages to the newest versions their ranges allow; one name moves alone, an empty name moves them all.
[[nodiscard]] bool UpdatePackages(
    const std::filesystem::path& projectRoot, const Registry& registry, const std::string& name, ProjectState& state, std::string& error);
// Removes a package the project asked for, and the packages only it needed.
[[nodiscard]] bool RemovePackage(const std::filesystem::path& projectRoot, const std::string& name, ProjectState& state, std::string& error);
// The installed packages that need `name`.
[[nodiscard]] std::vector<std::string> NeededBy(const Lock& lock, const std::string& name);

struct LockChange {
    std::string name;
    kb::mods::ModVersion from;
    kb::mods::ModVersion to;
};
struct LockChanges {
    std::vector<std::pair<std::string, kb::mods::ModVersion>> installed;
    std::vector<LockChange> updated;
    std::vector<std::pair<std::string, kb::mods::ModVersion>> removed;
};
[[nodiscard]] LockChanges Diff(const Lock& before, const Lock& after);

} // namespace kb::packages
