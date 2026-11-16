#include "PackagesInternal.hpp"

#include "kb/packages/Packages.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/mods/ModManifest.hpp"
#include "engine/security/Crypto.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace kb::packages {
namespace detail {


[[nodiscard]] std::string Utf8Text(const fs::path& path) {
    const std::u8string text = path.generic_u8string();
    return { reinterpret_cast<const char*>(text.data()), text.size() };
}

[[nodiscard]] std::string Show(const std::string& name, const ModVersion& version) {
    return name + ' ' + kb::mods::ToString(version);
}

[[nodiscard]] bool ReadText(const fs::path& path, std::uintmax_t limit, std::string& text, std::string& error) {
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        error = "cannot read " + Utf8Text(path) + ": " + ec.message();
        return false;
    }
    if (size > limit) {
        error = Utf8Text(path) + " is larger than " + std::to_string(limit) + " bytes";
        return false;
    }
    std::ifstream input{ path, std::ios::binary };
    text.assign(std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{});
    if (input.bad() || text.size() != size) {
        error = "cannot read " + Utf8Text(path);
        return false;
    }
    return true;
}

[[nodiscard]] bool WriteText(const fs::path& path, std::string_view text, std::string& error) {
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    if (!output) {
        error = "cannot write " + Utf8Text(path);
        return false;
    }
    return true;
}

// JSON with one member per line, so a project file reads and compares well.
void Pretty(const JsonValue& value, std::size_t depth, std::string& out) {
    const bool object = value.GetKind() == JsonValue::Kind::Object;
    if ((!object && value.GetKind() != JsonValue::Kind::Array) || value.Size() == 0U) {
        out += value.Dump();
        return;
    }
    out += object ? "{\n" : "[\n";
    for (std::size_t index = 0U; index < value.Size(); ++index) {
        out.append((depth + 1U) * 2U, ' ');
        if (object) {
            out += JsonValue::MakeString(value.MemberName(index)).Dump() + ": ";
        }
        Pretty(*value.At(index), depth + 1U, out);
        out += index + 1U < value.Size() ? ",\n" : "\n";
    }
    out.append(depth * 2U, ' ');
    out += object ? '}' : ']';
}

[[nodiscard]] std::string Document(const JsonValue& value) {
    std::string text;
    Pretty(value, 0U, text);
    return text + '\n';
}

[[nodiscard]] bool ParseDependencies(const JsonValue* value, Dependencies& out, std::string& error) {
    if (value == nullptr) {
        return true;
    }
    if (value->GetKind() != JsonValue::Kind::Object) {
        error = "\"dependencies\" must be an object of package names and version ranges";
        return false;
    }
    for (std::size_t index = 0U; index < value->Size(); ++index) {
        const std::string& name = value->MemberName(index);
        const JsonValue& range = *value->At(index);
        Dependency dependency;
        if (!kb::mods::IsValidModId(name)) {
            error = "\"" + name + "\" is not a package name (lower case letters, digits, '.', '_' and '-')";
            return false;
        }
        if (range.GetKind() != JsonValue::Kind::String || !ParseRange(range.AsString(), dependency)) {
            error = "the version range of \"" + name + "\" is not valid (write ^1.2, ~1.2.3, >=1.0 <2.0 or *)";
            return false;
        }
        out.emplace(name, std::move(dependency));
    }
    return true;
}

[[nodiscard]] JsonValue DependenciesJson(const Dependencies& dependencies) {
    JsonValue object = JsonValue::MakeObject();
    for (const auto& [name, dependency] : dependencies) {
        object.Set(name, JsonValue::MakeString(dependency.text));
    }
    return object;
}

[[nodiscard]] bool ManifestFromJson(const JsonValue& json, Manifest& out, std::string& error) {
    if (json.GetKind() != JsonValue::Kind::Object) {
        error = "it must be a JSON object";
        return false;
    }
    Manifest manifest;
    const JsonValue* name = json.Find("name");
    if (name == nullptr || name->GetKind() != JsonValue::Kind::String || !kb::mods::IsValidModId(name->AsString())) {
        error = "\"name\" must be lower case letters, digits, '.', '_' and '-' (at most 48 characters)";
        return false;
    }
    manifest.name = name->AsString();
    const JsonValue* version = json.Find("version");
    if (version == nullptr || version->GetKind() != JsonValue::Kind::String || !kb::mods::ParseModVersion(version->AsString(), manifest.version)) {
        error = "\"version\" must be major.minor.patch, such as 1.2.0";
        return false;
    }
    if (const JsonValue* description = json.Find("description"); description != nullptr) {
        if (description->GetKind() != JsonValue::Kind::String) {
            error = "\"description\" must be text";
            return false;
        }
        manifest.description = description->AsString();
    }
    if (!ParseDependencies(json.Find("dependencies"), manifest.dependencies, error)) {
        return false;
    }
    out = std::move(manifest);
    return true;
}

[[nodiscard]] bool ParseManifest(std::string_view text, Manifest& out, std::string& error) {
    JsonValue json;
    if (!JsonValue::Parse(text, json, error)) {
        error = "package.json is not valid JSON: " + error;
        return false;
    }
    if (!ManifestFromJson(json, out, error)) {
        error = "package.json: " + error;
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseStringMap(const JsonValue* value, std::map<std::string, std::string>& out) {
    if (value == nullptr || value->GetKind() != JsonValue::Kind::Object) {
        return false;
    }
    for (std::size_t index = 0U; index < value->Size(); ++index) {
        if (value->At(index)->GetKind() != JsonValue::Kind::String) {
            return false;
        }
        out.emplace(value->MemberName(index), value->At(index)->AsString());
    }
    return true;
}

// The files of a package are an array of "<sum> <path>" lines: a package may hold very many files, and a JSON object
// with that many members is slow to build and to read.
[[nodiscard]] bool ParseFileSums(const JsonValue* value, std::map<std::string, std::string>& out) {
    if (value == nullptr || value->GetKind() != JsonValue::Kind::Array) {
        return false;
    }
    for (std::size_t index = 0U; index < value->Size(); ++index) {
        const JsonValue& line = *value->At(index);
        const std::size_t space = line.GetKind() == JsonValue::Kind::String ? line.AsString().find(' ') : std::string::npos;
        if (space == std::string::npos || space == 0U || space + 1U == line.AsString().size()) {
            return false;
        }
        out.emplace(line.AsString().substr(space + 1U), line.AsString().substr(0U, space));
    }
    return true;
}

[[nodiscard]] std::string HashFile(const fs::path& file, std::string& error) {
    std::ifstream input{ file, std::ios::binary };
    if (!input) {
        error = "cannot read " + Utf8Text(file);
        return {};
    }
    kb::security::Sha512Hasher hasher;
    std::array<char, 65536U> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        hasher.Update({ reinterpret_cast<const std::uint8_t*>(buffer.data()), static_cast<std::size_t>(input.gcount()) });
    }
    if (input.bad()) {
        error = "cannot read " + Utf8Text(file);
        return {};
    }
    return kb::security::ToHex(hasher.Finish());
}

[[nodiscard]] bool HashFiles(
    const fs::path& folder, const std::map<std::string, std::uintmax_t>& sizes, std::map<std::string, std::string>& hashes, std::string& error) {
    hashes.clear();
    for (const auto& entry : sizes) {
        std::string hash = HashFile(folder / kb::assets::Utf8Path(entry.first), error);
        if (hash.empty()) {
            return false;
        }
        hashes.emplace(entry.first, std::move(hash));
    }
    return true;
}

// One sum for a package: over its file names and file sums, in path order.
[[nodiscard]] std::string Checksum(const std::map<std::string, std::string>& hashes) {
    kb::security::Sha512Hasher hasher;
    const auto add = [&hasher](std::string_view text) {
        hasher.Update({ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
    };
    for (const auto& [path, hash] : hashes) {
        add(path);
        add("\n");
        add(hash);
        add("\n");
    }
    return kb::security::ToHex(hasher.Finish());
}

std::string Sha512Hex(std::string_view bytes) {
    return kb::security::ToHex(kb::security::Sha512({ reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size() }));
}

} // namespace detail

using namespace detail;

bool ParseRange(std::string_view text, Dependency& out) {
    std::string spaced{ text };
    std::ranges::replace(spaced, ',', ' ');
    std::istringstream words{ spaced };
    std::string shown;
    std::string padded;
    for (std::string word; words >> word;) {
        shown += (shown.empty() ? "" : " ") + word;
        const std::size_t start = word.find_first_not_of("<>=^~");
        if (start != std::string::npos && word.find_first_not_of("0123456789.", start) == std::string::npos) {
            for (auto dots = std::ranges::count(word, '.'); dots < 2; ++dots) {
                word += ".0";
            }
        }
        padded += (padded.empty() ? "" : " ") + word;
    }
    if (padded.empty()) {
        shown = padded = "*";
    }
    out.text = std::move(shown);
    return kb::mods::ParseModVersionRange(padded, out.range);
}

bool ReadProjectPackages(const fs::path& projectRoot, ProjectPackages& out, std::string& error) {
    out = {};
    const fs::path file = projectRoot / std::string{ kProjectFile };
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return true;
    }
    std::string text;
    std::string problem;
    JsonValue json;
    if (!ReadText(file, kMaxManifestBytes, text, problem) || !JsonValue::Parse(text, json, problem)) {
        error = std::string{ kProjectFile } + " cannot be used: " + problem;
        return false;
    }
    const JsonValue* registry = json.Find("registry");
    if (json.GetKind() != JsonValue::Kind::Object || (registry != nullptr && registry->GetKind() != JsonValue::Kind::String) ||
        !ParseDependencies(json.Find("dependencies"), out.requested, problem)) {
        error = std::string{ kProjectFile } + " cannot be used: " + (problem.empty() ? "it must be an object with \"registry\" and \"dependencies\"" : problem);
        return false;
    }
    if (registry != nullptr) {
        out.registry = registry->AsString();
    }
    return true;
}

bool WriteProjectPackages(const fs::path& projectRoot, const ProjectPackages& value, std::string& error) {
    JsonValue json = JsonValue::MakeObject();
    if (!value.registry.empty()) {
        json.Set("registry", JsonValue::MakeString(value.registry));
    }
    json.Set("dependencies", DependenciesJson(value.requested));
    return WriteText(projectRoot / std::string{ kProjectFile }, Document(json), error);
}

bool ReadLock(const fs::path& projectRoot, Lock& out, std::string& error) {
    out.clear();
    const fs::path file = projectRoot / std::string{ kLockFile };
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return true;
    }
    const std::string label = std::string{ kLockFile } + " cannot be used: ";
    std::string text;
    std::string problem;
    JsonValue json;
    if (!ReadText(file, kMaxLockBytes, text, problem) || !JsonValue::Parse(text, json, problem)) {
        error = label + problem;
        return false;
    }
    const JsonValue* packages = json.GetKind() == JsonValue::Kind::Object ? json.Find("packages") : nullptr;
    if (packages == nullptr || packages->GetKind() != JsonValue::Kind::Object) {
        error = label + "it has no \"packages\" object";
        return false;
    }
    for (std::size_t index = 0U; index < packages->Size(); ++index) {
        const std::string& name = packages->MemberName(index);
        const JsonValue& entry = *packages->At(index);
        LockedPackage locked;
        const JsonValue* version = entry.GetKind() == JsonValue::Kind::Object ? entry.Find("version") : nullptr;
        const JsonValue* checksum = entry.GetKind() == JsonValue::Kind::Object ? entry.Find("checksum") : nullptr;
        if (!kb::mods::IsValidModId(name) || version == nullptr || version->GetKind() != JsonValue::Kind::String ||
            !kb::mods::ParseModVersion(version->AsString(), locked.version) || checksum == nullptr || checksum->GetKind() != JsonValue::Kind::String ||
            !ParseStringMap(entry.Find("dependencies"), locked.dependencies) || !ParseFileSums(entry.Find("files"), locked.files)) {
            error = label + "the entry of \"" + name + "\" is not valid";
            return false;
        }
        locked.checksum = checksum->AsString();
        out.emplace(name, std::move(locked));
    }
    return true;
}

bool WriteLock(const fs::path& projectRoot, const Lock& value, std::string& error) {
    JsonValue packages = JsonValue::MakeObject();
    for (const auto& [name, locked] : value) {
        JsonValue entry = JsonValue::MakeObject();
        entry.Set("version", JsonValue::MakeString(kb::mods::ToString(locked.version)));
        entry.Set("checksum", JsonValue::MakeString(locked.checksum));
        JsonValue dependencies = JsonValue::MakeObject();
        for (const auto& [dependency, range] : locked.dependencies) {
            dependencies.Set(dependency, JsonValue::MakeString(range));
        }
        entry.Set("dependencies", std::move(dependencies));
        JsonValue files = JsonValue::MakeArray();
        for (const auto& [path, hash] : locked.files) {
            files.Append(JsonValue::MakeString(hash + ' ' + path));
        }
        entry.Set("files", std::move(files));
        packages.Set(name, std::move(entry));
    }
    JsonValue json = JsonValue::MakeObject();
    json.Set("lockVersion", JsonValue::MakeNumber(1.0));
    json.Set("packages", std::move(packages));
    return WriteText(projectRoot / std::string{ kLockFile }, Document(json), error);
}

fs::path InstallFolder(const fs::path& projectRoot, const std::string& name) {
    return projectRoot / kPackagesFolder / name;
}

bool ListFiles(const fs::path& folder, std::map<std::string, std::uintmax_t>& sizes, std::string& error) {
    sizes.clear();
    std::error_code ec;
    fs::recursive_directory_iterator entry{ folder, fs::directory_options::none, ec };
    for (; !ec && entry != fs::recursive_directory_iterator{}; entry.increment(ec)) {
        const fs::file_status status = entry->symlink_status(ec);
        if (ec) {
            break;
        }
        if (fs::is_directory(status)) {
            continue;
        }
        const std::string relative = Utf8Text(entry->path().lexically_relative(folder));
        if (!fs::is_regular_file(status)) {
            error = Utf8Text(folder) + " holds a link or special file (" + relative + "); packages hold plain files only";
            return false;
        }
        sizes[relative] = entry->file_size(ec);
        if (ec) {
            break;
        }
    }
    if (ec) {
        error = "cannot read " + Utf8Text(folder) + ": " + ec.message();
        return false;
    }
    return true;
}

bool Prune(const fs::path& projectRoot, const std::map<std::string, ModVersion>& keep, Lock& lock, std::string& error) {
    for (auto locked = lock.begin(); locked != lock.end();) {
        if (keep.contains(locked->first)) {
            ++locked;
            continue;
        }
        std::error_code ec;
        fs::remove_all(InstallFolder(projectRoot, locked->first), ec);
        if (ec) {
            error = "cannot remove " + Utf8Text(InstallFolder(projectRoot, locked->first)) + ": " + ec.message();
            return false;
        }
        locked = lock.erase(locked);
    }
    // The folder is left only while it holds packages.
    std::error_code ignored;
    fs::remove(projectRoot / kPackagesFolder, ignored);
    return true;
}

bool Verify(const fs::path& projectRoot, const Lock& lock, const Dependencies& requested, std::vector<Problem>& problems, std::string& error) {
    for (const auto& [name, dependency] : requested) {
        if (!lock.contains(name)) {
            problems.push_back({ name, {}, "not installed" });
        }
    }
    for (const auto& [name, locked] : lock) {
        const fs::path folder = InstallFolder(projectRoot, name);
        std::error_code ec;
        if (!fs::is_directory(folder, ec)) {
            problems.push_back({ name, {}, "missing" });
            continue;
        }
        std::map<std::string, std::uintmax_t> sizes;
        std::map<std::string, std::string> hashes;
        if (!ListFiles(folder, sizes, error) || !HashFiles(folder, sizes, hashes, error)) {
            return false;
        }
        if (Checksum(hashes) == locked.checksum) {
            continue;
        }
        const std::size_t before = problems.size();
        for (const auto& [file, hash] : locked.files) {
            const auto found = hashes.find(file);
            if (found == hashes.end()) {
                problems.push_back({ name, file, "missing" });
            } else if (found->second != hash) {
                problems.push_back({ name, file, "modified" });
            }
        }
        for (const auto& entry : hashes) {
            if (!locked.files.contains(entry.first)) {
                problems.push_back({ name, entry.first, "added" });
            }
        }
        if (problems.size() == before) {
            problems.push_back({ name, {}, "checksum" });
        }
    }
    return true;
}

} // namespace kb::packages
