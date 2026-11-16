#pragma once

#include "engine/mods/ModVersion.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Project packages (kb_cli package, docs/packages.md): a package is a folder with a package.json and content (assets,
// scripts). A registry folder holds <name>/<version>/ packages; a project asks for some in packages.json, the resolver
// picks exact versions, they are copied to Assets/Packages/<name> (the asset path /Game/Packages/<name>/...) and
// packages.lock.json records the versions and the SHA-512 sums of every installed file.
// Names and versions follow the rules mods use (kb::mods::IsValidModId, ModVersion and its ranges).
namespace kb::cli::packages {

inline constexpr std::string_view kManifestFile = "package.json";
inline constexpr std::string_view kProjectFile = "packages.json";
inline constexpr std::string_view kLockFile = "packages.lock.json";

// A version range as written ("^1.2", "~2.1.0", ">=1 <3", "*") and parsed; short versions count as x.0 and x.0.0.
struct Dependency {
    std::string text;
    kb::mods::ModVersionRange range;

    [[nodiscard]] bool operator==(const Dependency&) const = default;
};
using Dependencies = std::map<std::string, Dependency>;

[[nodiscard]] bool ParseRange(std::string_view text, Dependency& out);

struct Manifest {
    std::string name;
    kb::mods::ModVersion version;
    std::string description;
    Dependencies dependencies;
};

// The registry: a folder of <name>/<version>/ packages.
class Registry {
public:
    explicit Registry(std::filesystem::path root) : root_{ std::move(root) } {}

    [[nodiscard]] const std::filesystem::path& Root() const noexcept { return root_; }
    [[nodiscard]] std::vector<std::string> Names() const;
    // The versions of a package, lowest first.
    [[nodiscard]] std::vector<kb::mods::ModVersion> Versions(const std::string& name) const;
    [[nodiscard]] std::filesystem::path Folder(const std::string& name, const kb::mods::ModVersion& version) const;
    [[nodiscard]] bool Read(const std::string& name, const kb::mods::ModVersion& version, Manifest& out, std::string& error) const;

private:
    std::filesystem::path root_;
};

// packages.json: what the project asks for (packages.json beside the project file) and where it takes it from.
struct ProjectPackages {
    // The registry folder, relative to the project when not absolute; empty when none is set.
    std::string registry;
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
// version, and updates `lock`. The registry copies are checked before anything is changed.
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

// The files of a package folder with their sizes, by path inside it. Links and other special files are refused.
[[nodiscard]] bool ListFiles(const std::filesystem::path& folder, std::map<std::string, std::uintmax_t>& sizes, std::string& error);

} // namespace kb::cli::packages
