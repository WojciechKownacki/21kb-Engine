#include "CliCommands.hpp"
#include "CliProgram.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/security/Crypto.hpp"
#include "engine/security/ReleaseKeys.hpp"

#include "kb/packages/Packages.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

// kb_cli package: the packages of a project (docs/packages.md).
namespace kb::cli {
namespace {

namespace fs = std::filesystem;
namespace pkg = kb::packages;
using kb::core::JsonValue;
using kb::mods::ModVersion;

[[nodiscard]] int Fail(CommandIo io, const std::string& message) {
    io.err << "error: " << message << '\n';
    return 1;
}

[[nodiscard]] std::string Utf8Text(const fs::path& path) {
    const std::u8string text = path.generic_u8string();
    return { reinterpret_cast<const char*>(text.data()), text.size() };
}

[[nodiscard]] JsonValue Text(std::string_view text) {
    return JsonValue::MakeString(ToValidUtf8(text));
}

[[nodiscard]] std::string Show(const std::string& name, const ModVersion& version) {
    return name + ' ' + kb::mods::ToString(version);
}

[[nodiscard]] std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

struct Session {
    fs::path root;
    pkg::ProjectPackages config;
    pkg::Lock lock;

    [[nodiscard]] pkg::ProjectState State() const { return { config, lock }; }
};

[[nodiscard]] std::optional<Session> OpenSession(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: package needs --project <dir>\n";
        return std::nullopt;
    }
    std::string error;
    const fs::path file = kb::project::ProjectManager::FindProjectFile(kb::assets::Utf8Path(*project), error);
    if (file.empty()) {
        io.err << "error: " << error << '\n';
        return std::nullopt;
    }
    Session session{ .root = file.parent_path() };
    if (!pkg::ReadProjectPackages(session.root, session.config, error) || !pkg::ReadLock(session.root, session.lock, error)) {
        io.err << "error: " << error << '\n';
        return std::nullopt;
    }
    return session;
}

[[nodiscard]] bool IsAddress(const std::string& location) {
    return location.starts_with("http://") || location.starts_with("https://");
}

// --registry <folder | index.json | address>, else what packages.json names; --cache <folder> keeps the downloads.
[[nodiscard]] std::optional<pkg::Registry> OpenRegistry(const ArgumentList& arguments, const Session& session, CommandIo io) {
    const std::optional<std::string> option = arguments.Option("--registry");
    std::string location = option.value_or(session.config.registry);
    if (location.empty()) {
        io.err << "error: the project has no package registry; pass --registry <folder or address> (package add keeps it in packages.json)\n";
        return std::nullopt;
    }
    // A path typed on the command line is the working folder's; one in packages.json is the project's.
    if (option.has_value() && !IsAddress(location)) {
        location = Utf8Text(fs::absolute(kb::assets::Utf8Path(location)));
    }
    pkg::RegistryOptions options{ .projectRoot = session.root, .trustedKeys = session.config.trustedKeys };
    if (const std::optional<std::string> cache = arguments.Option("--cache"); cache.has_value()) {
        options.cacheFolder = fs::absolute(kb::assets::Utf8Path(*cache));
    }
    std::string error;
    std::optional<pkg::Registry> registry = pkg::Registry::Open(location, options, error);
    if (!registry.has_value()) {
        io.err << "error: " << error << '\n';
    }
    return registry;
}

// "name" or "name@range".
struct Spec {
    std::string name;
    std::string range;
};

[[nodiscard]] std::optional<Spec> ParseSpec(const std::string& text) {
    const std::size_t at = text.find('@');
    if (text.empty() || at == 0U) {
        return std::nullopt;
    }
    return Spec{ .name = text.substr(0U, at), .range = at == std::string::npos ? std::string{} : text.substr(at + 1U) };
}

[[nodiscard]] std::string JoinNames(const std::vector<std::string>& names) {
    std::string text;
    for (const std::string& name : names) {
        text += (text.empty() ? "" : ", ") + name;
    }
    return text;
}

// What a command that changes packages did: the packages it installed, updated and removed.
void ReportChanges(CommandIo io, std::string_view schema, const pkg::Lock& before, const pkg::Lock& after) {
    const pkg::LockChanges changes = pkg::Diff(before, after);
    JsonValue installed = JsonValue::MakeArray();
    JsonValue updated = JsonValue::MakeArray();
    JsonValue removed = JsonValue::MakeArray();
    for (const auto& [name, version] : changes.installed) {
        io.out << "installed " << Show(name, version) << '\n';
        JsonValue row = JsonValue::MakeObject();
        row.Set("name", Text(name));
        row.Set("version", Text(kb::mods::ToString(version)));
        installed.Append(std::move(row));
    }
    for (const pkg::LockChange& change : changes.updated) {
        io.out << "updated " << change.name << ' ' << kb::mods::ToString(change.from) << " -> " << kb::mods::ToString(change.to) << '\n';
        JsonValue row = JsonValue::MakeObject();
        row.Set("name", Text(change.name));
        row.Set("from", Text(kb::mods::ToString(change.from)));
        row.Set("to", Text(kb::mods::ToString(change.to)));
        updated.Append(std::move(row));
    }
    for (const auto& [name, version] : changes.removed) {
        io.out << "removed " << Show(name, version) << '\n';
        JsonValue row = JsonValue::MakeObject();
        row.Set("name", Text(name));
        row.Set("version", Text(kb::mods::ToString(version)));
        removed.Append(std::move(row));
    }
    if (installed.Size() + updated.Size() + removed.Size() == 0U) {
        io.out << "nothing to change\n";
    }
    JsonValue data = DataObject(schema);
    data.Set("installed", std::move(installed));
    data.Set("updated", std::move(updated));
    data.Set("removed", std::move(removed));
    data.Set("packages", JsonValue::MakeNumber(static_cast<double>(after.size())));
    PublishData(io, std::move(data));
}

int RunList(const ArgumentList& arguments, CommandIo io) {
    const std::optional<Session> session = OpenSession(arguments, io);
    if (!session.has_value()) {
        return 1;
    }
    JsonValue rows = JsonValue::MakeArray();
    for (const auto& [name, locked] : session->lock) {
        const auto requested = session->config.requested.find(name);
        const std::vector<std::string> by = pkg::NeededBy(session->lock, name);
        io.out << Show(name, locked.version) << "  "
               << (requested != session->config.requested.end() ? "asked for " + requested->second.text : "needed by " + JoinNames(by)) << '\n';
        JsonValue row = JsonValue::MakeObject();
        row.Set("name", Text(name));
        row.Set("version", Text(kb::mods::ToString(locked.version)));
        row.Set("requested", requested != session->config.requested.end() ? Text(requested->second.text) : JsonValue{});
        JsonValue needed = JsonValue::MakeArray();
        for (const std::string& other : by) {
            needed.Append(Text(other));
        }
        row.Set("neededBy", std::move(needed));
        JsonValue dependencies = JsonValue::MakeObject();
        for (const auto& [dependency, range] : locked.dependencies) {
            dependencies.Set(dependency, Text(range));
        }
        row.Set("dependencies", std::move(dependencies));
        row.Set("files", JsonValue::MakeNumber(static_cast<double>(locked.files.size())));
        row.Set("checksum", Text(locked.checksum));
        rows.Append(std::move(row));
    }
    if (session->lock.empty()) {
        io.out << "no packages installed; kb_cli package add --project <dir> --registry <folder> <name> installs one\n";
    } else {
        io.out << session->lock.size() << " package(s) installed\n";
    }
    JsonValue data = DataObject("21kb.cli.package-list/v1");
    data.Set("registry", session->config.registry.empty() ? JsonValue{} : Text(session->config.registry));
    data.Set("packages", std::move(rows));
    PublishData(io, std::move(data));
    return 0;
}

int RunSearch(const ArgumentList& arguments, CommandIo io) {
    if (arguments.Positionals().size() > 2U) {
        return Fail(io, "package search expects [<text>]");
    }
    const std::optional<Session> session = OpenSession(arguments, io);
    const std::optional<pkg::Registry> registry = session.has_value() ? OpenRegistry(arguments, *session, io) : std::nullopt;
    if (!registry.has_value()) {
        return 1;
    }
    const std::string query = Lower(arguments.Positionals().size() == 2U ? arguments.Positionals()[1] : std::string{});
    JsonValue rows = JsonValue::MakeArray();
    for (const std::string& name : registry->Names()) {
        const std::vector<ModVersion> versions = registry->Versions(name);
        pkg::Manifest latest;
        std::string error;
        if (!registry->Read(name, versions.back(), latest, error)) {
            io.err << "warning: " << error << '\n';
            continue;
        }
        if (Lower(name + ' ' + latest.description).find(query) == std::string::npos) {
            continue;
        }
        const auto installed = session->lock.find(name);
        io.out << Show(name, versions.back()) << (installed != session->lock.end() ? "  (installed " + kb::mods::ToString(installed->second.version) + ")" : std::string{})
               << (latest.description.empty() ? "" : "  " + latest.description) << '\n';
        JsonValue row = JsonValue::MakeObject();
        row.Set("name", Text(name));
        row.Set("latest", Text(kb::mods::ToString(versions.back())));
        JsonValue all = JsonValue::MakeArray();
        for (const ModVersion& version : versions) {
            all.Append(Text(kb::mods::ToString(version)));
        }
        row.Set("versions", std::move(all));
        row.Set("description", Text(latest.description));
        row.Set("installed", installed != session->lock.end() ? Text(kb::mods::ToString(installed->second.version)) : JsonValue{});
        rows.Append(std::move(row));
    }
    if (rows.Size() == 0U) {
        io.out << "no package matches\n";
    }
    JsonValue data = DataObject("21kb.cli.package-search/v1");
    data.Set("registry", Text(registry->Location()));
    data.Set("results", std::move(rows));
    PublishData(io, std::move(data));
    return 0;
}

int RunInfo(const ArgumentList& arguments, CommandIo io) {
    std::string error;
    const std::optional<Spec> spec = arguments.Positionals().size() == 2U ? ParseSpec(arguments.Positionals()[1]) : std::nullopt;
    if (!spec.has_value()) {
        return Fail(io, "package info expects <name>[@<range>]");
    }
    pkg::Dependency range;
    if (!pkg::ParseRange(spec->range.empty() ? "*" : spec->range, range)) {
        return Fail(io, "'" + spec->range + "' is not a version range (write ^1.2, ~1.2.3, >=1.0 <2.0 or *)");
    }
    const std::optional<Session> session = OpenSession(arguments, io);
    const std::optional<pkg::Registry> registry = session.has_value() ? OpenRegistry(arguments, *session, io) : std::nullopt;
    if (!registry.has_value()) {
        return 1;
    }
    const std::vector<ModVersion> versions = registry->Versions(spec->name);
    if (versions.empty()) {
        return Fail(io, "the package '" + spec->name + "' is not in the registry; kb_cli package search lists what is");
    }
    const auto chosen = std::ranges::find_if(versions.rbegin(), versions.rend(), [&](const ModVersion& version) { return kb::mods::Satisfies(range.range, version); });
    std::string available;
    for (const ModVersion& version : versions) {
        available += (available.empty() ? "" : ", ") + kb::mods::ToString(version);
    }
    if (chosen == versions.rend()) {
        return Fail(io, "no version of '" + spec->name + "' fits " + range.text + " (available: " + available + ")");
    }
    pkg::Manifest manifest;
    if (!registry->Read(spec->name, *chosen, manifest, error)) {
        return Fail(io, error);
    }
    const std::uint64_t archiveBytes = registry->Find(spec->name, *chosen)->size;
    const auto installed = session->lock.find(spec->name);
    io.out << Show(spec->name, *chosen) << '\n';
    if (!registry->Signed()) {
        io.out << "  unsigned registry (packages.json trusts no key of its publisher; kb_cli package trust adds one)\n";
    }
    if (!manifest.description.empty()) {
        io.out << "  " << manifest.description << '\n';
    }
    std::string needed;
    JsonValue needs = JsonValue::MakeObject();
    for (const auto& [name, dependency] : manifest.dependencies) {
        needed += (needed.empty() ? "" : ", ") + name + ' ' + dependency.text;
        needs.Set(name, Text(dependency.text));
    }
    io.out << "  versions: " << available << "\n  needs: " << (needed.empty() ? "nothing" : needed) << "\n  download: " << archiveBytes << " bytes\n  installed: "
           << (installed != session->lock.end() ? kb::mods::ToString(installed->second.version) : std::string{ "no" }) << '\n';
    JsonValue data = DataObject("21kb.cli.package-info/v1");
    data.Set("name", Text(spec->name));
    data.Set("version", Text(kb::mods::ToString(*chosen)));
    data.Set("description", Text(manifest.description));
    JsonValue all = JsonValue::MakeArray();
    for (const ModVersion& version : versions) {
        all.Append(Text(kb::mods::ToString(version)));
    }
    data.Set("versions", std::move(all));
    data.Set("dependencies", std::move(needs));
    data.Set("size", JsonValue::MakeNumber(static_cast<double>(archiveBytes)));
    data.Set("signed", JsonValue::MakeBool(registry->Signed()));
    data.Set("installed", installed != session->lock.end() ? Text(kb::mods::ToString(installed->second.version)) : JsonValue{});
    PublishData(io, std::move(data));
    return 0;
}

// The result of an operation of the library: the report of what changed, or the reason it was refused.
[[nodiscard]] int Finish(const Session& session, bool done, const pkg::ProjectState& state, const std::string& error, std::string_view schema, CommandIo io) {
    if (!done) {
        return Fail(io, error);
    }
    ReportChanges(io, schema, session.lock, state.lock);
    return 0;
}

int RunAdd(const ArgumentList& arguments, CommandIo io) {
    const std::optional<Spec> spec = arguments.Positionals().size() == 2U ? ParseSpec(arguments.Positionals()[1]) : std::nullopt;
    if (!spec.has_value()) {
        return Fail(io, "package add expects <name>[@<range>]");
    }
    const std::optional<Session> session = OpenSession(arguments, io);
    const std::optional<pkg::Registry> registry = session.has_value() ? OpenRegistry(arguments, *session, io) : std::nullopt;
    if (!registry.has_value()) {
        return 1;
    }
    pkg::ProjectState state = session->State();
    std::string error;
    return Finish(*session, pkg::AddPackage(session->root, *registry, spec->name, spec->range, arguments.Option("--registry").has_value(), state, error), state, error,
        "21kb.cli.package-add/v1", io);
}

int RunUpdate(const ArgumentList& arguments, CommandIo io) {
    if (arguments.Positionals().size() > 2U) {
        return Fail(io, "package update expects [<name>]");
    }
    const std::optional<Session> session = OpenSession(arguments, io);
    const std::optional<pkg::Registry> registry = session.has_value() ? OpenRegistry(arguments, *session, io) : std::nullopt;
    if (!registry.has_value()) {
        return 1;
    }
    pkg::ProjectState state = session->State();
    std::string error;
    const std::string name = arguments.Positionals().size() == 2U ? arguments.Positionals()[1] : std::string{};
    return Finish(*session, pkg::UpdatePackages(session->root, *registry, name, state, error), state, error, "21kb.cli.package-update/v1", io);
}

int RunRemove(const ArgumentList& arguments, CommandIo io) {
    if (arguments.Positionals().size() != 2U) {
        return Fail(io, "package remove expects <name>");
    }
    const std::optional<Session> session = OpenSession(arguments, io);
    if (!session.has_value()) {
        return 1;
    }
    pkg::ProjectState state = session->State();
    std::string error;
    return Finish(*session, pkg::RemovePackage(session->root, arguments.Positionals()[1], state, error), state, error, "21kb.cli.package-remove/v1", io);
}

// Every --key <release signing key file> (made by kb_cli keys generate).
[[nodiscard]] bool LoadSigningKeys(const ArgumentList& arguments, std::vector<kb::security::ReleaseSigningKey>& keys, std::string& error) {
    for (const std::string& path : arguments.Options("--key")) {
        std::ifstream input{ kb::assets::Utf8Path(path), std::ios::binary };
        std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
        keys.emplace_back();
        const bool decoded = !text.empty() && kb::security::DecodeReleaseSigningKey(text, keys.back(), error);
        kb::security::SecureWipe(std::span{ reinterpret_cast<std::uint8_t*>(text.data()), text.size() });
        if (!decoded) {
            error = "--key " + path + ": " + (error.empty() ? "the key file cannot be read" : error);
            return false;
        }
    }
    return true;
}

int RunPublish(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> registry = arguments.Option("--registry");
    if (arguments.Positionals().size() != 2U || !registry.has_value() || IsAddress(*registry)) {
        return Fail(io, "package publish expects --registry <folder> <package folder> [--key <key file>]... (it writes a registry folder, not an address)");
    }
    pkg::IndexEntry entry;
    std::vector<kb::security::ReleaseSigningKey> keys;
    std::string error;
    if (!LoadSigningKeys(arguments, keys, error) ||
        !pkg::Publish(fs::absolute(kb::assets::Utf8Path(*registry)), fs::absolute(kb::assets::Utf8Path(arguments.Positionals()[1])), entry, error, keys)) {
        return Fail(io, error);
    }
    const std::string version = kb::mods::ToString(entry.manifest.version);
    io.out << "published " << Show(entry.manifest.name, entry.manifest.version) << ": " << entry.archive << ", " << entry.size << " bytes" << (keys.empty() ? "" : ", index signed") << '\n';
    JsonValue data = DataObject("21kb.cli.package-publish/v1");
    data.Set("name", Text(entry.manifest.name));
    data.Set("version", Text(version));
    data.Set("archive", Text(entry.archive));
    data.Set("sha512", Text(entry.sha512));
    data.Set("size", JsonValue::MakeNumber(static_cast<double>(entry.size)));
    data.Set("signed", JsonValue::MakeBool(!keys.empty()));
    PublishData(io, std::move(data));
    return 0;
}

// Signs the index of a registry folder again, for example to add the key of a new publisher key pair.
int RunSign(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> registry = arguments.Option("--registry");
    std::vector<kb::security::ReleaseSigningKey> keys;
    std::string error;
    if (arguments.Positionals().size() != 1U || !registry.has_value() || IsAddress(*registry) || arguments.Options("--key").empty()) {
        return Fail(io, "package sign expects --registry <folder> --key <key file> [--key <key file>]...");
    }
    if (!LoadSigningKeys(arguments, keys, error) || !pkg::SignIndex(fs::absolute(kb::assets::Utf8Path(*registry)), keys, error)) {
        return Fail(io, error);
    }
    io.out << "signed the index with " << keys.size() << " key(s)\n";
    JsonValue published = JsonValue::MakeArray();
    for (const kb::security::ReleaseSigningKey& key : keys) {
        published.Append(Text(kb::security::ToHex(key.publicKey)));
    }
    JsonValue data = DataObject("21kb.cli.package-sign/v1");
    data.Set("keys", std::move(published));
    PublishData(io, std::move(data));
    return 0;
}

// The public keys of the registry's publisher the project trusts: shows them, adds (--add) or removes (--remove) some.
int RunTrust(const ArgumentList& arguments, CommandIo io) {
    const std::optional<Session> session = OpenSession(arguments, io);
    if (!session.has_value() || arguments.Positionals().size() != 1U) {
        return session.has_value() ? Fail(io, "package trust expects [--add <public key>] [--remove <public key>]") : 1;
    }
    pkg::ProjectPackages wanted = session->config;
    const auto parse = [](const std::string& text, std::string& key) {
        kb::security::Ed25519PublicKey parsed{};
        if (!kb::security::TryParseHex(text, parsed)) {
            return false;
        }
        key = kb::security::ToHex(parsed);
        return true;
    };
    for (const std::string& text : arguments.Options("--add")) {
        std::string key;
        if (!parse(text, key)) {
            return Fail(io, "--add expects the 64 hexadecimal digits of a public key");
        }
        if (std::ranges::find(wanted.trustedKeys, key) == wanted.trustedKeys.end()) {
            wanted.trustedKeys.push_back(key);
        }
    }
    for (const std::string& text : arguments.Options("--remove")) {
        std::string key;
        if (!parse(text, key)) {
            return Fail(io, "--remove expects the 64 hexadecimal digits of a public key");
        }
        std::erase(wanted.trustedKeys, key);
    }
    std::string error;
    if (wanted != session->config && !pkg::WriteProjectPackages(session->root, wanted, error)) {
        return Fail(io, error);
    }
    JsonValue keys = JsonValue::MakeArray();
    for (const std::string& key : wanted.trustedKeys) {
        io.out << "trusted registry key " << key << '\n';
        keys.Append(Text(key));
    }
    if (wanted.trustedKeys.empty()) {
        io.out << "no registry key is trusted: the registry is used unsigned\n";
    }
    JsonValue data = DataObject("21kb.cli.package-trust/v1");
    data.Set("keys", std::move(keys));
    PublishData(io, std::move(data));
    return 0;
}

int RunVerify(const ArgumentList& arguments, CommandIo io) {
    const std::optional<Session> session = OpenSession(arguments, io);
    if (!session.has_value()) {
        return 1;
    }
    std::vector<pkg::Problem> problems;
    std::string error;
    if (!pkg::Verify(session->root, session->lock, session->config.requested, problems, error)) {
        return Fail(io, error);
    }
    JsonValue rows = JsonValue::MakeArray();
    for (const pkg::Problem& problem : problems) {
        io.out << problem.what << ' ' << problem.package << (problem.file.empty() ? "" : '/' + problem.file) << '\n';
        JsonValue row = JsonValue::MakeObject();
        row.Set("package", Text(problem.package));
        row.Set("file", problem.file.empty() ? JsonValue{} : Text(problem.file));
        row.Set("problem", Text(problem.what));
        rows.Append(std::move(row));
    }
    io.out << (problems.empty() ? "all " + std::to_string(session->lock.size()) + " package(s) are as installed" : std::to_string(problems.size()) + " problem(s) found") << '\n';
    JsonValue data = DataObject("21kb.cli.package-verify/v1");
    data.Set("ok", JsonValue::MakeBool(problems.empty()));
    data.Set("checked", JsonValue::MakeNumber(static_cast<double>(session->lock.size())));
    data.Set("problems", std::move(rows));
    PublishData(io, std::move(data));
    return problems.empty() ? 0 : 1;
}

} // namespace

int RunPackageCommand(const ArgumentList& arguments, CommandIo io) {
    const std::string action = arguments.Positionals().empty() ? std::string{} : arguments.Positionals().front();
    if (action == "list") {
        return RunList(arguments, io);
    }
    if (action == "search") {
        return RunSearch(arguments, io);
    }
    if (action == "info") {
        return RunInfo(arguments, io);
    }
    if (action == "add") {
        return RunAdd(arguments, io);
    }
    if (action == "update") {
        return RunUpdate(arguments, io);
    }
    if (action == "remove") {
        return RunRemove(arguments, io);
    }
    if (action == "verify") {
        return RunVerify(arguments, io);
    }
    if (action == "publish") {
        return RunPublish(arguments, io);
    }
    if (action == "sign") {
        return RunSign(arguments, io);
    }
    if (action == "trust") {
        return RunTrust(arguments, io);
    }
    return Fail(io, "package expects list, search, info, add, update, remove, verify, publish, sign or trust");
}

} // namespace kb::cli
