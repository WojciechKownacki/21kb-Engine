#pragma once

#include "kb/packages/Packages.hpp"

#include "engine/core/JsonValue.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

// What the sources of kb_packages share; not part of the library's interface.
namespace kb::packages {

inline constexpr std::uintmax_t kMaxManifestBytes = 256U * 1024U;
inline constexpr std::uintmax_t kMaxLockBytes = 64U * 1024U * 1024U;
// The folder of a project the packages are installed in (the asset path /Game/Packages).
inline constexpr std::string_view kPackagesFolder = "Assets/Packages";

namespace detail {

namespace fs = std::filesystem;
using kb::core::JsonValue;
using kb::mods::ModVersion;

[[nodiscard]] std::string Utf8Text(const fs::path& path);
// "<name> <version>".
[[nodiscard]] std::string Show(const std::string& name, const ModVersion& version);
[[nodiscard]] bool ReadText(const fs::path& path, std::uintmax_t limit, std::string& text, std::string& error);
[[nodiscard]] bool WriteText(const fs::path& path, std::string_view text, std::string& error);
// JSON with one member per line, ending with a line break.
[[nodiscard]] std::string Document(const JsonValue& value);
[[nodiscard]] bool ParseDependencies(const JsonValue* value, Dependencies& out, std::string& error);
[[nodiscard]] JsonValue DependenciesJson(const Dependencies& dependencies);
[[nodiscard]] JsonValue ModulesJson(const std::vector<ModuleEntry>& modules);
// Every module file a manifest lists is a file of the unpacked package folder.
[[nodiscard]] bool ModuleFilesExist(const fs::path& folder, const Manifest& manifest, std::string& error);
[[nodiscard]] bool ManifestFromJson(const JsonValue& json, Manifest& out, std::string& error);
[[nodiscard]] bool ParseManifest(std::string_view text, Manifest& out, std::string& error);
[[nodiscard]] bool HashFiles(
    const fs::path& folder, const std::map<std::string, std::uintmax_t>& sizes, std::map<std::string, std::string>& hashes, std::string& error);
// One sum for a package: over its file names and file sums, in path order.
[[nodiscard]] std::string Checksum(const std::map<std::string, std::string>& hashes);
// The SHA-512 of the bytes as 128 lower case hexadecimal digits.
[[nodiscard]] std::string Sha512Hex(std::string_view bytes);

} // namespace detail
} // namespace kb::packages
