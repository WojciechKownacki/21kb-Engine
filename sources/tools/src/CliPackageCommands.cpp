#include "CliCommands.hpp"
#include "CliPackages.hpp"
#include "CliProgram.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/mods/ModManifest.hpp"
#include "engine/project/ProjectManager.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

// kb_cli package: the packages of a project (docs/packages.md).
namespace kb::cli {
namespace {

namespace fs = std::filesystem;
namespace pkg = packages;
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

// --registry <folder>, else the folder packages.json names.
[[nodiscard]] std::optional<pkg::Registry> OpenRegistry(const ArgumentList& arguments, const Session& session, CommandIo io) {
    fs::path folder;
    if (const std::optional<std::string> option = arguments.Option("--registry"); option.has_value()) {
        folder = fs::absolute(kb::assets::Utf8Path(*option));
    } else if (!session.config.registry.empty()) {
        folder = kb::assets::Utf8Path(session.config.registry);
        if (folder.is_relative()) {
            folder = session.root / folder;
        }
    } else {
        io.err << "error: the project has no package registry; pass --registry <folder> (package add keeps it in packages.json)\n";
        return std::nullopt;
    }
    folder = folder.lexically_normal();
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        io.err << "error: the package registry " << Utf8Text(folder) << " is not a folder\n";
        return std::nullopt;
    }
    return pkg::Registry{ folder };
}

// How packages.json names the registry: relative to the project when it can be.
[[nodiscard]] std::string StoredRegistry(const Session& session, const pkg::Registry& registry) {
    std::error_code ec;
    const fs::path relative = fs::weakly_canonical(registry.Root(), ec).lexically_relative(fs::weakly_canonical(session.root, ec));
    return Utf8Text(relative.empty() ? registry.Root() : relative);
}

[[nodiscard]] std::map<std::string, ModVersion> LockedVersions(const pkg::Lock& lock) {
    std::map<std::string, ModVersion> versions;
    for (const auto& [name, locked] : lock) {
        versions.emplace(name, locked.version);
    }
    return versions;
}

// "name" or "name@range".
struct Spec {
    std::string name;
    std::optional<std::string> range;
};

[[nodiscard]] std::optional<Spec> ParseSpec(const std::string& text, std::string& error) {
    const std::size_t at = text.find('@');
    Spec spec{ .name = text.substr(0U, at) };
    if (at != std::string::npos) {
        spec.range = text.substr(at + 1U);
    }
    if (!kb::mods::IsValidModId(spec.name)) {
        error = "'" + spec.name + "' is not a package name (lower case letters, digits, '.', '_' and '-')";
        return std::nullopt;
    }
    return spec;
}

[[nodiscard]] std::string JoinNames(const std::vector<std::string>& names) {
    std::string text;
    for (const std::string& name : names) {
        text += (text.empty() ? "" : ", ") + name;
    }
    return text;
}

// The packages of the project, with what asked for each: the project itself or the packages that need it.
[[nodiscard]] std::vector<std::string> NeededBy(const Session& session, const std::string& name) {
    std::vector<std::string> by;
    for (const auto& [other, locked] : session.lock) {
        if (locked.dependencies.contains(name)) {
            by.push_back(other);
        }
    }
    return by;
}

// What a command that changes packages did: the packages it installed, updated and removed.
void ReportChanges(CommandIo io, std::string_view schema, const pkg::Lock& before, const pkg::Lock& after) {
    JsonValue installed = JsonValue::MakeArray();
    JsonValue updated = JsonValue::MakeArray();
    JsonValue removed = JsonValue::MakeArray();
    for (const auto& [name, now] : after) {
        const auto old = before.find(name);
        JsonValue row = JsonValue::MakeObject();
        row.Set("name", Text(name));
        if (old == before.end()) {
            io.out << "installed " << Show(name, now.version) << '\n';
            row.Set("version", Text(kb::mods::ToString(now.version)));
            installed.Append(std::move(row));
        } else if (old->second.version != now.version) {
            io.out << "updated " << name << ' ' << kb::mods::ToString(old->second.version) << " -> " << kb::mods::ToString(now.version) << '\n';
            row.Set("from", Text(kb::mods::ToString(old->second.version)));
            row.Set("to", Text(kb::mods::ToString(now.version)));
            updated.Append(std::move(row));
        }
    }
    for (const auto& [name, old] : before) {
        if (!after.contains(name)) {
            io.out << "removed " << Show(name, old.version) << '\n';
            JsonValue row = JsonValue::MakeObject();
            row.Set("name", Text(name));
            row.Set("version", Text(kb::mods::ToString(old.version)));
            removed.Append(std::move(row));
        }
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

// Writes the lock and packages.json when they differ from what was read.
[[nodiscard]] bool Save(const Session& session, const pkg::Lock& lock, const pkg::ProjectPackages& config, std::string& error) {
    return (lock == session.lock || pkg::WriteLock(session.root, lock, error)) &&
        (config == session.config || pkg::WriteProjectPackages(session.root, config, error));
}

int RunList(const ArgumentList& arguments, CommandIo io) {
    const std::optional<Session> session = OpenSession(arguments, io);
    if (!session.has_value()) {
        return 1;
    }
    JsonValue rows = JsonValue::MakeArray();
    for (const auto& [name, locked] : session->lock) {
        const auto requested = session->config.requested.find(name);
        const std::vector<std::string> by = NeededBy(*session, name);
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
    data.Set("registry", Text(Utf8Text(registry->Root())));
    data.Set("results", std::move(rows));
    PublishData(io, std::move(data));
    return 0;
}

int RunInfo(const ArgumentList& arguments, CommandIo io) {
    std::string error;
    const std::optional<Spec> spec = arguments.Positionals().size() == 2U ? ParseSpec(arguments.Positionals()[1], error) : std::nullopt;
    if (!spec.has_value()) {
        return Fail(io, error.empty() ? "package info expects <name>[@<range>]" : error);
    }
    pkg::Dependency range;
    if (!pkg::ParseRange(spec->range.value_or("*"), range)) {
        return Fail(io, "'" + *spec->range + "' is not a version range (write ^1.2, ~1.2.3, >=1.0 <2.0 or *)");
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
    std::map<std::string, std::uintmax_t> sizes;
    if (!registry->Read(spec->name, *chosen, manifest, error) || !pkg::ListFiles(registry->Folder(spec->name, *chosen), sizes, error)) {
        return Fail(io, error);
    }
    std::uintmax_t bytes = 0U;
    for (const auto& entry : sizes) {
        bytes += entry.second;
    }
    const auto installed = session->lock.find(spec->name);
    io.out << Show(spec->name, *chosen) << '\n';
    if (!manifest.description.empty()) {
        io.out << "  " << manifest.description << '\n';
    }
    std::string needed;
    JsonValue needs = JsonValue::MakeObject();
    for (const auto& [name, dependency] : manifest.dependencies) {
        needed += (needed.empty() ? "" : ", ") + name + ' ' + dependency.text;
        needs.Set(name, Text(dependency.text));
    }
    io.out << "  versions: " << available << "\n  needs: " << (needed.empty() ? "nothing" : needed) << "\n  files: " << sizes.size() << " (" << bytes << " bytes)\n  installed: "
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
    data.Set("files", JsonValue::MakeNumber(static_cast<double>(sizes.size())));
    data.Set("bytes", JsonValue::MakeNumber(static_cast<double>(bytes)));
    data.Set("installed", installed != session->lock.end() ? Text(kb::mods::ToString(installed->second.version)) : JsonValue{});
    PublishData(io, std::move(data));
    return 0;
}

// Resolves, installs and saves: the project changes only when every package could be chosen and checked. `pin` names
// a package whose range becomes ^<the version chosen> (what `add` without a range asks for).
[[nodiscard]] int Apply(
    const Session& session,
    const pkg::Registry& registry,
    pkg::ProjectPackages wanted,
    const std::map<std::string, ModVersion>& preferred,
    const std::string& pin,
    std::string_view schema,
    CommandIo io) {
    std::map<std::string, ModVersion> resolved;
    std::string error;
    if (!pkg::Resolve(registry, wanted.requested, preferred, resolved, error)) {
        return Fail(io, error + " Nothing was changed.");
    }
    if (!pin.empty()) {
        // The text is always a valid range.
        static_cast<void>(pkg::ParseRange('^' + kb::mods::ToString(resolved.at(pin)), wanted.requested[pin]));
    }
    pkg::Lock lock = session.lock;
    if (!pkg::Install(session.root, registry, resolved, lock, error) || !Save(session, lock, wanted, error)) {
        return Fail(io, error);
    }
    ReportChanges(io, schema, session.lock, lock);
    return 0;
}

int RunAdd(const ArgumentList& arguments, CommandIo io) {
    std::string error;
    const std::optional<Spec> spec = arguments.Positionals().size() == 2U ? ParseSpec(arguments.Positionals()[1], error) : std::nullopt;
    if (!spec.has_value()) {
        return Fail(io, error.empty() ? "package add expects <name>[@<range>]" : error);
    }
    const std::optional<Session> session = OpenSession(arguments, io);
    const std::optional<pkg::Registry> registry = session.has_value() ? OpenRegistry(arguments, *session, io) : std::nullopt;
    if (!registry.has_value()) {
        return 1;
    }
    pkg::Dependency range;
    if (!pkg::ParseRange(spec->range.value_or("*"), range)) {
        return Fail(io, "'" + *spec->range + "' is not a version range (write ^1.2, ~1.2.3, >=1.0 <2.0 or *)");
    }
    pkg::ProjectPackages wanted = session->config;
    wanted.requested[spec->name] = range;
    if (arguments.Option("--registry").has_value()) {
        wanted.registry = StoredRegistry(*session, *registry);
    }
    // Packages already installed keep their version while it fits.
    return Apply(*session, *registry, std::move(wanted), LockedVersions(session->lock), spec->range.has_value() ? std::string{} : spec->name,
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
    // One name moves to the newest version it allows while the rest stay; no name moves them all.
    std::map<std::string, ModVersion> preferred;
    if (arguments.Positionals().size() == 2U) {
        const std::string& name = arguments.Positionals()[1];
        if (!session->lock.contains(name)) {
            return Fail(io, "'" + name + "' is not installed; kb_cli package list shows what is");
        }
        preferred = LockedVersions(session->lock);
        preferred.erase(name);
    }
    return Apply(*session, *registry, session->config, preferred, {}, "21kb.cli.package-update/v1", io);
}

int RunRemove(const ArgumentList& arguments, CommandIo io) {
    if (arguments.Positionals().size() != 2U) {
        return Fail(io, "package remove expects <name>");
    }
    const std::string& name = arguments.Positionals()[1];
    const std::optional<Session> session = OpenSession(arguments, io);
    if (!session.has_value()) {
        return 1;
    }
    if (!session->config.requested.contains(name)) {
        const std::vector<std::string> by = NeededBy(*session, name);
        if (by.empty()) {
            return Fail(io, "'" + name + "' is not installed; kb_cli package list shows what is");
        }
        return Fail(io, "'" + name + "' was not added by the project; it is needed by " + JoinNames(by) + ". Remove those instead.");
    }
    pkg::ProjectPackages wanted = session->config;
    wanted.requested.erase(name);
    // What stays: the packages still asked for and everything they need, by the lock.
    std::map<std::string, ModVersion> keep;
    std::vector<std::string> queue;
    for (const auto& [asked, range] : wanted.requested) {
        queue.push_back(asked);
    }
    for (std::size_t next = 0U; next < queue.size(); ++next) {
        const auto locked = session->lock.find(queue[next]);
        if (locked == session->lock.end() || !keep.emplace(queue[next], locked->second.version).second) {
            continue;
        }
        for (const auto& [dependency, range] : locked->second.dependencies) {
            queue.push_back(dependency);
        }
    }
    pkg::Lock lock = session->lock;
    std::string error;
    if (!pkg::Prune(session->root, keep, lock, error) || !Save(*session, lock, wanted, error)) {
        return Fail(io, error);
    }
    ReportChanges(io, "21kb.cli.package-remove/v1", session->lock, lock);
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
    return Fail(io, "package expects list, search, info, add, update, remove or verify");
}

} // namespace kb::cli
