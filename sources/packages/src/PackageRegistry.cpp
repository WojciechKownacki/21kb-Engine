#include "PackagesInternal.hpp"

#include "kb/packages/PackageArchive.hpp"
#include "kb/packages/Packages.hpp"

#include "kb/http/HttpClient.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/security/Crypto.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <span>
#include <system_error>

namespace kb::packages {
namespace {

using namespace detail;

// Rounds the resolver may take to settle; a project's packages settle within a few.
constexpr std::size_t kMaxRounds = 100U;
constexpr std::uint64_t kMaxIndexBytes = 32U * 1024U * 1024U;
constexpr std::uint32_t kIndexTimeoutMilliseconds = 30'000U;
constexpr std::uint32_t kArchiveTimeoutMilliseconds = 300'000U;
constexpr std::uint64_t kMaxSignatureBytes = 64U * 1024U;
// What a signature covers: this line, then the exact bytes of index.json.
constexpr std::string_view kIndexSignatureContext = "21kb package index v1\n";

struct Need {
    // Who asks: "the project" or "<package> <version>".
    std::string by;
    Dependency dependency;
};

[[nodiscard]] std::string JoinVersions(const std::vector<ModVersion>& versions) {
    std::string text;
    for (const ModVersion& version : versions) {
        text += (text.empty() ? "" : ", ") + kb::mods::ToString(version);
    }
    return text;
}

[[nodiscard]] bool Pick(
    const Registry& registry,
    const std::string& name,
    const std::vector<Need>& needs,
    const std::map<std::string, ModVersion>& preferred,
    ModVersion& picked,
    std::string& error) {
    const std::vector<ModVersion> versions = registry.Versions(name);
    if (versions.empty()) {
        error = "The package '" + name + "' is not in the registry (" + registry.Location() + "), but " + needs.front().by +
            " needs it. kb_cli package search lists what the registry has.";
        return false;
    }
    const auto fits = [&needs](const ModVersion& version) {
        return std::ranges::all_of(needs, [&version](const Need& need) { return kb::mods::Satisfies(need.dependency.range, version); });
    };
    if (const auto locked = preferred.find(name); locked != preferred.end() && std::ranges::find(versions, locked->second) != versions.end() && fits(locked->second)) {
        picked = locked->second;
        return true;
    }
    for (auto version = versions.rbegin(); version != versions.rend(); ++version) {
        if (fits(*version)) {
            picked = *version;
            return true;
        }
    }
    std::string asked;
    for (const Need& need : needs) {
        asked += (asked.empty() ? "" : "; ") + need.by + " needs " + name + ' ' + need.dependency.text;
    }
    error = "Cannot install '" + name + "': no version fits all that is asked of it (" + asked + "). Available versions: " + JoinVersions(versions) + ".";
    return false;
}

// Appends to `cycle` the packages from the first visit of the one that closes a circle.
[[nodiscard]] bool VisitCycle(
    const std::string& name,
    const std::map<std::string, Manifest>& manifests,
    std::map<std::string, int>& state,
    std::vector<std::string>& path,
    std::string& cycle) {
    state[name] = 1;
    path.push_back(name);
    for (const auto& [next, dependency] : manifests.at(name).dependencies) {
        if (state[next] == 1) {
            for (auto step = std::ranges::find(path, next); step != path.end(); ++step) {
                cycle += Show(*step, manifests.at(*step).version) + " -> ";
            }
            cycle += Show(next, manifests.at(next).version);
            return true;
        }
        if (state[next] == 0 && VisitCycle(next, manifests, state, path, cycle)) {
            return true;
        }
    }
    path.pop_back();
    state[name] = 2;
    return false;
}

[[nodiscard]] bool IsHttp(std::string_view location) {
    return location.starts_with("http://") || location.starts_with("https://");
}

// Plain http:// is only for a registry on this computer.
[[nodiscard]] bool IsLoopbackHttp(std::string_view url) {
    constexpr std::string_view scheme = "http://";
    if (!url.starts_with(scheme)) {
        return false;
    }
    const std::string_view rest = url.substr(scheme.size());
    const std::string_view host = rest.substr(0U, rest.find_first_of("/:?#"));
    return host == "127.0.0.1" || host == "localhost";
}

[[nodiscard]] bool Download(std::string_view url, std::uint64_t maxBytes, std::uint32_t timeoutMilliseconds, std::vector<std::uint8_t>& body, std::string& error) {
    if (url.starts_with("http://") && !IsLoopbackHttp(url)) {
        error = "The package registry address " + std::string{ url } + " must start with https:// (plain http:// is allowed for this computer only).";
        return false;
    }
    kb::http::Request request;
    request.url = std::string{ url };
    request.maxResponseBytes = static_cast<std::size_t>(maxBytes);
    request.timeoutMilliseconds = timeoutMilliseconds;
    request.allowInsecureHttp = IsLoopbackHttp(url);
    const kb::http::Response response = kb::http::Perform(std::move(request));
    if (response.status != kb::http::Status::Ok) {
        error = "Cannot reach the package registry (" + std::string{ url } + "): " + response.error;
        return false;
    }
    if (response.httpStatus != 200) {
        error = "The package registry answered " + std::to_string(response.httpStatus) + " for " + std::string{ url } + ".";
        return false;
    }
    body = response.body;
    return true;
}

[[nodiscard]] bool ReadBinary(const fs::path& path, std::uint64_t limit, std::vector<std::uint8_t>& bytes, std::string& error) {
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec || size > limit) {
        error = ec ? "cannot read " + Utf8Text(path) + ": " + ec.message() : Utf8Text(path) + " is larger than " + std::to_string(limit) + " bytes";
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    std::ifstream input{ path, std::ios::binary };
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!input && size != 0U) {
        error = "cannot read " + Utf8Text(path);
        return false;
    }
    return true;
}

[[nodiscard]] std::string BytesText(const std::vector<std::uint8_t>& bytes) {
    return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
}

// A path below the index: letters, digits and . _ - in parts separated by '/', none empty or climbing.
[[nodiscard]] bool IsRelativeArchive(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.find("//") != std::string_view::npos || path.back() == '/') {
        return false;
    }
    for (std::size_t start = 0U;;) {
        const std::size_t end = path.find('/', start);
        const std::string_view part = path.substr(start, end == std::string_view::npos ? end : end - start);
        if (part == ".." || part == "." || !std::ranges::all_of(part, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '_' || c == '-'; })) {
            return false;
        }
        if (end == std::string_view::npos) {
            return true;
        }
        start = end + 1U;
    }
}

using Entries = std::map<std::string, std::map<ModVersion, IndexEntry>>;

[[nodiscard]] bool ParseIndex(std::string_view text, Entries& out, std::string& error) {
    JsonValue json;
    if (!JsonValue::Parse(text, json, error)) {
        return false;
    }
    const JsonValue* version = json.GetKind() == JsonValue::Kind::Object ? json.Find("indexVersion") : nullptr;
    const JsonValue* packages = json.GetKind() == JsonValue::Kind::Object ? json.Find("packages") : nullptr;
    if (version == nullptr || version->AsNumber() != 1.0 || packages == nullptr || packages->GetKind() != JsonValue::Kind::Array) {
        error = "it is not a package index (it needs \"indexVersion\": 1 and a \"packages\" list)";
        return false;
    }
    Entries entries;
    for (std::size_t index = 0U; index < packages->Size(); ++index) {
        const JsonValue& row = *packages->At(index);
        IndexEntry entry;
        std::string problem;
        const bool object = row.GetKind() == JsonValue::Kind::Object;
        const JsonValue* archive = object ? row.Find("archive") : nullptr;
        const JsonValue* sum = object ? row.Find("sha512") : nullptr;
        const JsonValue* size = object ? row.Find("size") : nullptr;
        if (!ManifestFromJson(row, entry.manifest, problem)) {
            error = "entry " + std::to_string(index + 1U) + ": " + problem;
            return false;
        }
        const std::string label = Show(entry.manifest.name, entry.manifest.version);
        if (archive == nullptr || archive->GetKind() != JsonValue::Kind::String || !(IsRelativeArchive(archive->AsString()) || IsHttp(archive->AsString()))) {
            error = label + ": \"archive\" must be a plain relative path or an address";
            return false;
        }
        if (sum == nullptr || sum->GetKind() != JsonValue::Kind::String || sum->AsString().size() != 2U * kb::security::kSha512Bytes ||
            !std::ranges::all_of(sum->AsString(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; })) {
            error = label + ": \"sha512\" must be 128 hexadecimal digits";
            return false;
        }
        if (size == nullptr || size->GetKind() != JsonValue::Kind::Number || size->AsNumber() < 1.0 || size->AsNumber() > static_cast<double>(kMaxArchiveBytes) ||
            size->AsNumber() != std::floor(size->AsNumber())) {
            error = label + ": \"size\" must be the archive's size in bytes (at most " + std::to_string(kMaxArchiveBytes) + ")";
            return false;
        }
        entry.archive = archive->AsString();
        entry.sha512 = sum->AsString();
        std::ranges::transform(entry.sha512, entry.sha512.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        entry.size = static_cast<std::uint64_t>(size->AsNumber());
        if (!entries[entry.manifest.name].emplace(entry.manifest.version, entry).second) {
            error = label + " is listed twice";
            return false;
        }
    }
    out = std::move(entries);
    return true;
}

[[nodiscard]] std::string IndexText(const Entries& entries) {
    JsonValue packages = JsonValue::MakeArray();
    for (const auto& [name, versions] : entries) {
        for (const auto& [version, entry] : versions) {
            JsonValue row = JsonValue::MakeObject();
            row.Set("name", JsonValue::MakeString(name));
            row.Set("version", JsonValue::MakeString(kb::mods::ToString(version)));
            if (!entry.manifest.description.empty()) {
                row.Set("description", JsonValue::MakeString(entry.manifest.description));
            }
            row.Set("dependencies", DependenciesJson(entry.manifest.dependencies));
            if (!entry.manifest.modules.empty()) {
                row.Set("modules", ModulesJson(entry.manifest.modules));
            }
            row.Set("archive", JsonValue::MakeString(entry.archive));
            row.Set("sha512", JsonValue::MakeString(entry.sha512));
            row.Set("size", JsonValue::MakeNumber(static_cast<double>(entry.size)));
            packages.Append(std::move(row));
        }
    }
    JsonValue index = JsonValue::MakeObject();
    index.Set("indexVersion", JsonValue::MakeNumber(1.0));
    index.Set("packages", std::move(packages));
    return Document(index);
}

[[nodiscard]] std::vector<std::uint8_t> SignedMessage(std::string_view index) {
    std::vector<std::uint8_t> message(kIndexSignatureContext.begin(), kIndexSignatureContext.end());
    message.insert(message.end(), index.begin(), index.end());
    return message;
}

// Accepts the index when one signature in `signatureText` (absent: the registry has none) is valid for one trusted key.
[[nodiscard]] bool CheckIndexSignature(
    std::string_view index, const std::optional<std::string>& signatureText, const std::vector<std::string>& trusted, const std::string& location, std::string& error) {
    const std::string label = "The package registry " + location;
    if (!signatureText.has_value()) {
        error = label + " is not signed (" + std::string{ kIndexFile } + std::string{ kIndexSignatureSuffix } + " is missing), but this project trusts only signed registries (trustedKeys in " +
            std::string{ kProjectFile } + ").";
        return false;
    }
    JsonValue json;
    std::string problem;
    const JsonValue* list = JsonValue::Parse(*signatureText, json, problem) && json.GetKind() == JsonValue::Kind::Object ? json.Find("signatures") : nullptr;
    if (list == nullptr || list->GetKind() != JsonValue::Kind::Array) {
        error = label + " has a signature file that cannot be used (it needs a \"signatures\" list).";
        return false;
    }
    const std::vector<std::uint8_t> message = SignedMessage(index);
    bool trustedKeySeen = false;
    for (std::size_t at = 0U; at < list->Size(); ++at) {
        const JsonValue& row = *list->At(at);
        const JsonValue* key = row.GetKind() == JsonValue::Kind::Object ? row.Find("key") : nullptr;
        const JsonValue* signature = row.GetKind() == JsonValue::Kind::Object ? row.Find("signature") : nullptr;
        kb::security::Ed25519PublicKey publicKey{};
        kb::security::Ed25519Signature value{};
        if (key == nullptr || signature == nullptr || key->GetKind() != JsonValue::Kind::String || signature->GetKind() != JsonValue::Kind::String ||
            !kb::security::TryParseHex(key->AsString(), publicKey) || !kb::security::TryParseHex(signature->AsString(), value) ||
            std::ranges::find(trusted, kb::security::ToHex(publicKey)) == trusted.end()) {
            continue;
        }
        trustedKeySeen = true;
        if (kb::security::Ed25519Verify(value, publicKey, message)) {
            return true;
        }
    }
    error = trustedKeySeen ? label + " was changed after it was signed: its signature does not match the index."
                           : label + " is not signed by any key this project trusts (trustedKeys in " + std::string{ kProjectFile } + ").";
    return false;
}

// Removes a folder when it goes out of scope.
struct ScopedFolder {
    fs::path folder;
    ~ScopedFolder() {
        std::error_code ignored;
        fs::remove_all(folder, ignored);
    }
};

} // namespace

std::optional<Registry> Registry::Open(std::string_view location, const RegistryOptions& options, std::string& error) {
    Registry registry;
    registry.location_ = std::string{ location };
    registry.options_ = options;
    if (registry.options_.cacheFolder.empty()) {
        registry.options_.cacheFolder = options.projectRoot / "Saved" / "PackageCache";
    }
    for (const std::string& key : options.trustedKeys) {
        kb::security::Ed25519PublicKey parsed{};
        if (!kb::security::TryParseHex(key, parsed)) {
            error = "A trusted registry key must be 64 hexadecimal digits: " + key;
            return std::nullopt;
        }
    }
    const bool mustBeSigned = !options.trustedKeys.empty();
    std::string text;
    std::optional<std::string> signatureText;
    std::string ignored;
    if (IsHttp(location)) {
        std::string url{ location };
        if (url.ends_with('/')) {
            url += kIndexFile;
        }
        std::vector<std::uint8_t> body;
        if (!Download(url, kMaxIndexBytes, kIndexTimeoutMilliseconds, body, error)) {
            return std::nullopt;
        }
        text = BytesText(body);
        if (mustBeSigned && Download(url + std::string{ kIndexSignatureSuffix }, kMaxSignatureBytes, kIndexTimeoutMilliseconds, body, ignored)) {
            signatureText = BytesText(body);
        }
        registry.remote_ = true;
        registry.base_ = url.substr(0U, url.find_last_of('/') + 1U);
    } else {
        fs::path path = kb::assets::Utf8Path(location);
        if (path.is_relative() && !options.projectRoot.empty()) {
            path = options.projectRoot / path;
        }
        std::error_code ec;
        if (fs::is_directory(path, ec)) {
            path /= std::string{ kIndexFile };
        }
        if (!fs::is_regular_file(path, ec)) {
            error = "There is no package registry at " + registry.location_ + " (its " + std::string{ kIndexFile } + " is missing).";
            return std::nullopt;
        }
        if (!ReadText(path, kMaxIndexBytes, text, error)) {
            return std::nullopt;
        }
        fs::path signaturePath = path;
        signaturePath += std::string{ kIndexSignatureSuffix };
        std::string signature;
        if (mustBeSigned && fs::is_regular_file(signaturePath, ec) && ReadText(signaturePath, kMaxSignatureBytes, signature, ignored)) {
            signatureText = std::move(signature);
        }
        registry.base_ = Utf8Text(path.parent_path()) + '/';
    }
    // The signature is checked before any sum of the index is read.
    if (mustBeSigned && !CheckIndexSignature(text, signatureText, options.trustedKeys, registry.location_, error)) {
        return std::nullopt;
    }
    registry.signed_ = mustBeSigned;
    if (!ParseIndex(text, registry.entries_, error)) {
        error = "The package registry index of " + registry.location_ + " cannot be used: " + error;
        return std::nullopt;
    }
    return registry;
}

std::vector<std::string> Registry::Names() const {
    std::vector<std::string> names;
    for (const auto& entry : entries_) {
        names.push_back(entry.first);
    }
    return names;
}

std::vector<ModVersion> Registry::Versions(const std::string& name) const {
    std::vector<ModVersion> versions;
    if (const auto found = entries_.find(name); found != entries_.end()) {
        for (const auto& entry : found->second) {
            versions.push_back(entry.first);
        }
    }
    return versions;
}

const IndexEntry* Registry::Find(const std::string& name, const ModVersion& version) const {
    const auto found = entries_.find(name);
    if (found == entries_.end()) {
        return nullptr;
    }
    const auto entry = found->second.find(version);
    return entry == found->second.end() ? nullptr : &entry->second;
}

bool Registry::Read(const std::string& name, const ModVersion& version, Manifest& out, std::string& error) const {
    const IndexEntry* entry = Find(name, version);
    if (entry == nullptr) {
        error = "The registry has no " + Show(name, version) + ".";
        return false;
    }
    out = entry->manifest;
    return true;
}

bool Registry::LoadArchive(const IndexEntry& entry, std::vector<std::uint8_t>& bytes, std::string& error) const {
    const std::string label = Show(entry.manifest.name, entry.manifest.version);
    const auto verified = [&]() {
        if (bytes.size() == entry.size && Sha512Hex(BytesText(bytes)) == entry.sha512) {
            return true;
        }
        error = "The archive of " + label + " does not match the checksum the registry lists for it, so it was not installed.";
        return false;
    };
    if (!remote_ && !IsHttp(entry.archive)) {
        return ReadBinary(kb::assets::Utf8Path(base_ + entry.archive), kMaxArchiveBytes, bytes, error) && verified();
    }
    const fs::path cached = options_.cacheFolder / (entry.sha512 + std::string{ kArchiveExtension });
    std::error_code ec;
    std::string ignored;
    if (fs::is_regular_file(cached, ec) && ReadBinary(cached, kMaxArchiveBytes, bytes, ignored) && verified()) {
        return true;
    }
    error.clear();
    const std::string url = IsHttp(entry.archive) ? entry.archive : base_ + entry.archive;
    if (!Download(url, entry.size, kArchiveTimeoutMilliseconds, bytes, error) || !verified()) {
        return false;
    }
    // The cache is a convenience: a folder that cannot be written only means the next install downloads again.
    fs::create_directories(options_.cacheFolder, ec);
    const fs::path temporary = options_.cacheFolder / (entry.sha512 + ".part");
    {
        std::ofstream output{ temporary, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    fs::rename(temporary, cached, ec);
    return true;
}

bool Registry::Fetch(const std::string& name, const ModVersion& version, const fs::path& staging, fs::path& folder, std::string& error) const {
    const IndexEntry* entry = Find(name, version);
    if (entry == nullptr) {
        error = "The registry has no " + Show(name, version) + ".";
        return false;
    }
    std::vector<std::uint8_t> bytes;
    folder = staging / name / kb::mods::ToString(version);
    std::error_code ec;
    fs::remove_all(folder, ec);
    if (!LoadArchive(*entry, bytes, error) || !ExtractArchive(bytes, folder, error)) {
        return false;
    }
    // The archive must hold the package its index entry describes.
    std::string text;
    std::string problem;
    Manifest manifest;
    if (!ReadText(folder / std::string{ kManifestFile }, kMaxManifestBytes, text, problem) || !ParseManifest(text, manifest, problem) ||
        manifest.name != name || manifest.version != version || manifest.dependencies != entry->manifest.dependencies ||
        manifest.modules != entry->manifest.modules || !ModuleFilesExist(folder, manifest, problem)) {
        error = "The archive of " + Show(name, version) + " does not match the registry's index entry for it" + (problem.empty() ? "" : " (" + problem + ")") + ".";
        return false;
    }
    return true;
}

bool SignIndex(const fs::path& registryFolder, std::span<const kb::security::ReleaseSigningKey> keys, std::string& error) {
    if (keys.empty()) {
        error = "Signing the package index needs at least one key.";
        return false;
    }
    const fs::path indexFile = registryFolder / std::string{ kIndexFile };
    std::string index;
    if (!ReadText(indexFile, kMaxIndexBytes, index, error)) {
        return false;
    }
    const std::vector<std::uint8_t> message = SignedMessage(index);
    JsonValue list = JsonValue::MakeArray();
    for (const kb::security::ReleaseSigningKey& key : keys) {
        JsonValue row = JsonValue::MakeObject();
        row.Set("key", JsonValue::MakeString(kb::security::ToHex(key.publicKey)));
        row.Set("signature", JsonValue::MakeString(kb::security::ToHex(kb::security::Ed25519Sign(key.secretKey, message))));
        list.Append(std::move(row));
    }
    JsonValue signatures = JsonValue::MakeObject();
    signatures.Set("signatures", std::move(list));
    fs::path signatureFile = indexFile;
    signatureFile += std::string{ kIndexSignatureSuffix };
    return WriteText(signatureFile, Document(signatures), error);
}

bool Publish(
    const fs::path& registryFolder, const fs::path& packageFolder, IndexEntry& published, std::string& error, std::span<const kb::security::ReleaseSigningKey> keys) {
    std::string text;
    IndexEntry entry;
    if (!ReadText(packageFolder / std::string{ kManifestFile }, kMaxManifestBytes, text, error) || !ParseManifest(text, entry.manifest, error) ||
        !ModuleFilesExist(packageFolder, entry.manifest, error)) {
        error = Utf8Text(packageFolder) + ": " + error;
        return false;
    }
    const std::string label = Show(entry.manifest.name, entry.manifest.version);
    Entries entries;
    const fs::path indexFile = registryFolder / std::string{ kIndexFile };
    std::error_code ec;
    if (fs::exists(indexFile, ec) && (!ReadText(indexFile, kMaxIndexBytes, text, error) || !ParseIndex(text, entries, error))) {
        error = "The package registry index " + Utf8Text(indexFile) + " cannot be used: " + error;
        return false;
    }
    fs::path signatureFile = indexFile;
    signatureFile += std::string{ kIndexSignatureSuffix };
    if (keys.empty() && fs::exists(signatureFile, ec)) {
        error = "The registry is signed; publish with its key (--key) so the index keeps a valid signature.";
        return false;
    }
    if (entries[entry.manifest.name].contains(entry.manifest.version)) {
        error = label + " is already in the registry; a published version is never replaced. Raise the version in package.json.";
        return false;
    }
    std::vector<std::uint8_t> archive;
    if (!BuildArchive(packageFolder, archive, error)) {
        return false;
    }
    entry.archive = entry.manifest.name + '/' + kb::mods::ToString(entry.manifest.version) + '/' + entry.manifest.name + '-' +
        kb::mods::ToString(entry.manifest.version) + std::string{ kArchiveExtension };
    entry.sha512 = Sha512Hex(BytesText(archive));
    entry.size = archive.size();
    const fs::path archiveFile = registryFolder / kb::assets::Utf8Path(entry.archive);
    fs::create_directories(archiveFile.parent_path(), ec);
    {
        std::ofstream output{ archiveFile, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size()));
        output.flush();
        if (!output) {
            error = "cannot write " + Utf8Text(archiveFile);
            return false;
        }
    }
    entries[entry.manifest.name].emplace(entry.manifest.version, entry);
    if (!WriteText(indexFile, IndexText(entries), error) || (!keys.empty() && !SignIndex(registryFolder, keys, error))) {
        return false;
    }
    published = std::move(entry);
    return true;
}

bool Resolve(
    const Registry& registry,
    const Dependencies& requested,
    const std::map<std::string, ModVersion>& preferred,
    std::map<std::string, ModVersion>& resolved,
    std::string& error) {
    std::map<std::string, ModVersion> chosen;
    std::map<std::string, Manifest> manifests;
    for (std::size_t round = 0U; round < kMaxRounds; ++round) {
        // What is asked of each package by the project and by the packages chosen so far.
        std::map<std::string, std::vector<Need>> needs;
        std::vector<std::string> queue;
        const auto ask = [&](const std::string& name, Need need) {
            const auto [slot, added] = needs.try_emplace(name);
            if (added) {
                queue.push_back(name);
            }
            slot->second.push_back(std::move(need));
        };
        for (const auto& [name, dependency] : requested) {
            ask(name, Need{ "the project", dependency });
        }
        for (std::size_t next = 0U; next < queue.size(); ++next) {
            const auto picked = chosen.find(queue[next]);
            if (picked == chosen.end()) {
                continue;
            }
            Manifest& manifest = manifests[queue[next]];
            if (!registry.Read(queue[next], picked->second, manifest, error)) {
                return false;
            }
            for (const auto& [name, dependency] : manifest.dependencies) {
                ask(name, Need{ Show(queue[next], picked->second), dependency });
            }
        }
        std::map<std::string, ModVersion> settled;
        for (const auto& [name, list] : needs) {
            ModVersion version;
            if (!Pick(registry, name, list, preferred, version, error)) {
                return false;
            }
            settled.emplace(name, version);
        }
        if (settled != chosen) {
            chosen = std::move(settled);
            continue;
        }
        std::map<std::string, int> state;
        for (const auto& [name, dependency] : requested) {
            std::vector<std::string> path;
            std::string cycle;
            if (state[name] == 0 && VisitCycle(name, manifests, state, path, cycle)) {
                error = "These packages need each other in a circle: " + cycle + ". Remove one of those dependencies.";
                return false;
            }
        }
        resolved = std::move(chosen);
        return true;
    }
    error = "The requirements of the packages keep changing the versions chosen; loosen a version range in " + std::string{ kProjectFile } + " or in a package.";
    return false;
}

bool Install(const fs::path& projectRoot, const Registry& registry, const std::map<std::string, ModVersion>& resolved, Lock& lock, std::string& error) {
    struct Copy {
        std::string name;
        ModVersion version;
        Manifest manifest;
        fs::path source;
        std::map<std::string, std::uintmax_t> sizes;
        std::map<std::string, std::string> hashes;
    };
    // Downloads and unpacking happen below the project's Saved folder and are removed when this returns.
    const fs::path stagingRoot = projectRoot / "Saved" / "PackageStaging";
    std::error_code ec;
    fs::remove_all(stagingRoot, ec);
    const ScopedFolder staging{ stagingRoot };
    std::vector<Copy> copies;
    for (const auto& [name, version] : resolved) {
        const auto locked = lock.find(name);
        if (locked != lock.end() && locked->second.version == version && fs::is_directory(InstallFolder(projectRoot, name), ec)) {
            continue;
        }
        Copy copy{ .name = name, .version = version };
        if (!registry.Read(name, version, copy.manifest, error) || !registry.Fetch(name, version, stagingRoot, copy.source, error) ||
            !ListFiles(copy.source, copy.sizes, error) || !HashFiles(copy.source, copy.sizes, copy.hashes, error)) {
            return false;
        }
        if (locked != lock.end() && locked->second.version == version && locked->second.checksum != Checksum(copy.hashes)) {
            error = "The registry's copy of " + Show(name, version) + " is not the one installed before (the sums differ); " +
                "the registry may have been changed after that version was published.";
            return false;
        }
        copies.push_back(std::move(copy));
    }
    if (!Prune(projectRoot, resolved, lock, error)) {
        return false;
    }
    for (Copy& copy : copies) {
        const fs::path folder = InstallFolder(projectRoot, copy.name);
        fs::remove_all(folder, ec);
        for (const auto& entry : copy.sizes) {
            const fs::path target = folder / kb::assets::Utf8Path(entry.first);
            fs::create_directories(target.parent_path(), ec);
            if (!ec) {
                fs::copy_file(copy.source / kb::assets::Utf8Path(entry.first), target, fs::copy_options::overwrite_existing, ec);
            }
            if (ec) {
                error = "cannot copy " + entry.first + " of " + Show(copy.name, copy.version) + ": " + ec.message() + ". Verify the packages.";
                return false;
            }
        }
        std::map<std::string, std::uintmax_t> sizes;
        std::map<std::string, std::string> hashes;
        if (!ListFiles(folder, sizes, error) || !HashFiles(folder, sizes, hashes, error)) {
            return false;
        }
        if (hashes != copy.hashes) {
            error = "The copy of " + Show(copy.name, copy.version) + " does not match the registry's files. Verify the packages.";
            return false;
        }
        LockedPackage locked{ .version = copy.version, .checksum = Checksum(hashes), .files = std::move(hashes) };
        for (const auto& [name, dependency] : copy.manifest.dependencies) {
            locked.dependencies.emplace(name, dependency.text);
        }
        lock[copy.name] = std::move(locked);
    }
    return true;
}

} // namespace kb::packages
