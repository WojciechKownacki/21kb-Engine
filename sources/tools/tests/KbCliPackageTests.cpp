#include "CliCommands.hpp"
#include "CliProgram.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// kb_cli package against a registry folder: resolution, conflicts, cycles, update, remove, verify, project assets.
namespace {

namespace fs = std::filesystem;
using kb::core::JsonValue;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::fputs(message.c_str(), stderr);
        std::fputs("\n", stderr);
        std::exit(EXIT_FAILURE);
    }
}

struct Run {
    int exitCode = 0;
    std::string out;
    std::string err;
};

[[nodiscard]] Run RunLine(const std::vector<std::string>& words) {
    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    const int exitCode = kb::cli::RunCli(words, in, kb::cli::CommandIo{ .out = out, .err = err });
    return { exitCode, out.str(), err.str() };
}

void WriteText(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output << text;
    Require(output.good(), "the package test file could not be written: " + path.string());
}

[[nodiscard]] std::string ReadText(const fs::path& path) {
    std::ifstream input{ path, std::ios::binary };
    return { std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

[[nodiscard]] JsonValue Parse(const std::string& text) {
    JsonValue json;
    std::string error;
    Require(JsonValue::Parse(text, json, error), "not JSON (" + error + "): " + text);
    return json;
}

// A package in the registry: <registry>/<name>/<version>/package.json and its files.
void Publish(
    const fs::path& registry,
    const std::string& name,
    const std::string& version,
    const std::vector<std::pair<std::string, std::string>>& dependencies,
    const std::vector<std::pair<std::string, std::string>>& files = {},
    const std::string& description = {}) {
    std::string manifest = "{\"name\":\"" + name + "\",\"version\":\"" + version + "\"";
    if (!description.empty()) manifest += ",\"description\":\"" + description + "\"";
    manifest += ",\"dependencies\":{";
    for (std::size_t index = 0U; index < dependencies.size(); ++index) {
        manifest += (index == 0U ? "" : ",") + std::string{ "\"" } + dependencies[index].first + "\":\"" + dependencies[index].second + "\"";
    }
    manifest += "}}";
    const fs::path folder = registry / name / version;
    WriteText(folder / "package.json", manifest);
    for (const auto& [path, text] : files) WriteText(folder / path, text);
}

// Every file of the project by path and content.
[[nodiscard]] std::map<std::string, std::string> Snapshot(const fs::path& root) {
    std::map<std::string, std::string> files;
    for (const auto& entry : fs::recursive_directory_iterator{ root }) {
        if (entry.is_regular_file()) files[entry.path().lexically_relative(root).generic_string()] = ReadText(entry.path());
    }
    return files;
}

[[nodiscard]] bool Changed(const JsonValue& data, const std::string& path, const std::string& kind) {
    const JsonValue* changes = data.Find("changes");
    for (std::size_t index = 0U; changes != nullptr && index < changes->Size(); ++index) {
        const JsonValue& change = *changes->At(index);
        if (change.Find("path")->AsString() == path && change.Find("kind")->AsString() == kind) return true;
    }
    return false;
}

[[nodiscard]] const JsonValue& LockEntry(const JsonValue& lock, const std::string& name) {
    const JsonValue* entry = lock.Find("packages")->Find(name);
    Require(entry != nullptr, "the lock has no entry for " + name);
    return *entry;
}

} // namespace

void RunPackageCommandTests() {
    const fs::path base = fs::temp_directory_path() / "21kb_kb_cli_package_tests";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    const fs::path registry = base / "Registry";
    const fs::path root = base / "Pkg";
    const std::string project = root.string();
    const std::string registryText = registry.string();
    Require(RunLine({ "new", "--parent", base.string(), "--name", "Pkg", "--template", "Empty" }).exitCode == 0, "the package test project could not be created");
    // A Lua script as the engine writes one, to be shipped in a package.
    Require(RunLine({ "script-new", "--project", project, "--name", "Hello", "--language", "lua" }).exitCode == 0, "script-new failed");
    const std::string script = ReadText(root / "Assets" / "Scripts" / "Hello.lua");
    Require(!script.empty(), "the package test script is empty");

    Publish(registry, "acme.a", "1.1.0", {});
    Publish(registry, "acme.a", "1.2.3", { { "acme.b", "~2.1" } });
    Publish(registry, "acme.a", "1.4.0", { { "acme.b", "~2.1" } }, { { "Scripts/Hello.lua", script }, { "Data/Notes.txt", "notes\n" } });
    Publish(registry, "acme.a", "2.0.0", { { "acme.b", "^3" } }, {}, "Crates for the warehouse");
    for (const char* version : { "2.0.5", "2.1.0", "2.1.7", "3.0.0" }) Publish(registry, "acme.b", version, {});
    Publish(registry, "acme.c", "1.0.0", { { "acme.b", "^3" } });
    Publish(registry, "acme.loop1", "1.0.0", { { "acme.loop2", "*" } });
    Publish(registry, "acme.loop2", "1.0.0", { { "acme.loop1", "*" } });

    const auto package = [&](std::vector<std::string> words) {
        words.insert(words.begin(), "package");
        words.insert(words.end(), { "--project", project });
        return RunLine(words);
    };

    // The registry is named once; add keeps it in packages.json.
    const Run noRegistry = package({ "add", "acme.a" });
    Require(noRegistry.exitCode != 0 && noRegistry.err.find("registry") != std::string::npos, "add without a registry must say so: " + noRegistry.err);
    const Run missing = package({ "add", "--registry", registryText, "acme.nothing" });
    Require(missing.exitCode != 0 && missing.err.find("not in the registry") != std::string::npos, "an unknown package must be refused: " + missing.err);
    const Run badRange = package({ "add", "--registry", registryText, "acme.a@^x" });
    Require(badRange.exitCode != 0 && badRange.err.find("version range") != std::string::npos, "a bad range must be refused: " + badRange.err);

    // 1. add A@^1.2: the newest 1.x, with the dependency B@~2.1 brought along, and the lock records versions and sums.
    const Run added = package({ "add", "--registry", registryText, "acme.a@^1.2", "--json" });
    Require(added.exitCode == 0, "add failed: " + added.out + added.err);
    const JsonValue result = Parse(added.out);
    const JsonValue& data = *result.Find("data");
    Require(data.Find("schema")->AsString() == "21kb.cli.package-add/v1" && data.Find("installed")->Size() == 2U && data.Find("removed")->Size() == 0U,
        "add must report what it installed: " + added.out);
    const JsonValue lock = Parse(ReadText(root / "packages.lock.json"));
    const JsonValue& lockedA = LockEntry(lock, "acme.a");
    Require(lockedA.Find("version")->AsString() == "1.4.0" && lockedA.Find("checksum")->AsString().size() == 128U && lockedA.Find("files")->Size() == 3U &&
            lockedA.Find("dependencies")->Find("acme.b")->AsString() == "~2.1" && LockEntry(lock, "acme.b").Find("version")->AsString() == "2.1.7",
        "the lock must hold exact versions and sums, and the dependency of the dependency: " + ReadText(root / "packages.lock.json"));
    const JsonValue wanted = Parse(ReadText(root / "packages.json"));
    Require(wanted.Find("dependencies")->Find("acme.a")->AsString() == "^1.2" && !wanted.Find("registry")->AsString().empty() && wanted.Find("dependencies")->Size() == 1U,
        "packages.json must keep what the project asked for and the registry");
    // 7. data.changes lists the files, packages as assets.
    Require(Changed(data, "packages.lock.json", "created") && Changed(data, "packages.json", "created") && Changed(data, "Assets/Packages/acme.a/Scripts/Hello.lua", "created") &&
            Changed(data, "Assets/Packages/acme.b/package.json", "created"),
        "add must list the files it created: " + added.out);

    // 6. A package asset is a project asset: /Game/Packages/<name>/...
    {
        kb::scene::Scene scene;
        std::string error;
        Require(kb::cli::MountProjectAssets(scene, root, error), "mounting the project failed: " + error);
        const kb::assets::AssetMetadata* asset = scene.Assets().Manager().Registry().FindByPath("/Game/Packages/acme.a/Scripts/Hello.lua");
        Require(asset != nullptr && asset->physicalPath.filename() == "Hello.lua", "a package script must be an asset of the project");
    }
    const Run validated = RunLine({ "validate", "--project", project, "Assets/Packages/acme.a/Scripts/Hello.lua" });
    Require(validated.exitCode == 0, "a package script must validate like the project's own: " + validated.out + validated.err);

    // list, search, info.
    const JsonValue listed = *Parse(package({ "list", "--json" }).out).Find("data");
    Require(listed.Find("packages")->Size() == 2U && listed.Find("packages")->At(0)->Find("requested")->AsString() == "^1.2" &&
            listed.Find("packages")->At(1)->Find("neededBy")->At(0)->AsString() == "acme.a" && listed.Find("changes") == nullptr,
        "list must name what was asked for and what needs the rest: " + listed.Dump());
    const JsonValue found = *Parse(package({ "search", "CRATE", "--json" }).out).Find("data");
    Require(found.Find("results")->Size() == 1U && found.Find("results")->At(0)->Find("latest")->AsString() == "2.0.0" && found.Find("results")->At(0)->Find("installed")->AsString() == "1.4.0",
        "search must find a package by its description: " + found.Dump());
    const JsonValue info = *Parse(package({ "info", "acme.a@^1", "--json" }).out).Find("data");
    Require(info.Find("version")->AsString() == "1.4.0" && info.Find("versions")->Size() == 4U && info.Find("files")->AsNumber() == 3.0, "info must describe the newest version in range: " + info.Dump());

    // 2. A conflict names both requirements and leaves the project as it was.
    const auto before = Snapshot(root);
    const Run conflict = package({ "add", "acme.c" });
    Require(conflict.exitCode != 0 && conflict.err.find("acme.b") != std::string::npos && conflict.err.find("acme.a 1.4.0 needs acme.b ~2.1") != std::string::npos &&
            conflict.err.find("acme.c 1.0.0 needs acme.b ^3") != std::string::npos && conflict.err.find("Nothing was changed") != std::string::npos,
        "a conflict must name both requirements: " + conflict.err);
    // 3. A cycle is an error, not a loop.
    const Run cycle = package({ "add", "acme.loop1" });
    Require(cycle.exitCode != 0 && cycle.err.find("circle") != std::string::npos && cycle.err.find("acme.loop1 1.0.0 -> acme.loop2 1.0.0 -> acme.loop1 1.0.0") != std::string::npos,
        "a cycle must be named: " + cycle.err);
    Require(Snapshot(root) == before, "a refused add must leave the project as it was");

    // 4. update stays inside the ranges; one name moves alone.
    Publish(registry, "acme.a", "1.5.0", { { "acme.b", "~2.1" } });
    Publish(registry, "acme.a", "2.1.0", { { "acme.b", "~2.1" } });
    Publish(registry, "acme.b", "2.1.9", {});
    Publish(registry, "acme.b", "2.2.0", {});
    Require(package({ "update", "acme.nothing" }).exitCode != 0, "update of a package that is not installed must be refused");
    Require(package({ "update", "acme.b" }).exitCode == 0, "update of one package failed");
    Require(Parse(ReadText(root / "packages.lock.json")).Find("packages")->Find("acme.a")->Find("version")->AsString() == "1.4.0" &&
            Parse(ReadText(root / "packages.lock.json")).Find("packages")->Find("acme.b")->Find("version")->AsString() == "2.1.9",
        "update of one package must move only that one, inside its range");
    const Run updated = package({ "update", "--json" });
    Require(updated.exitCode == 0 && Parse(updated.out).Find("data")->Find("updated")->Size() == 1U, "update must report the package it moved: " + updated.out);
    const JsonValue updatedLock = Parse(ReadText(root / "packages.lock.json"));
    Require(LockEntry(updatedLock, "acme.a").Find("version")->AsString() == "1.5.0" && LockEntry(updatedLock, "acme.b").Find("version")->AsString() == "2.1.9",
        "update must take the newest versions the ranges allow: " + ReadText(root / "packages.lock.json"));
    Require(!fs::exists(root / "Assets" / "Packages" / "acme.a" / "Scripts" / "Hello.lua") && fs::exists(root / "Assets" / "Packages" / "acme.a" / "package.json"),
        "an updated package must be replaced by the files of the new version");
    const Run unchanged = package({ "update", "--json" });
    Require(unchanged.exitCode == 0 && Parse(unchanged.out).Find("data")->Find("changes") == nullptr, "an update with nothing to do must change no file: " + unchanged.out);

    // 5. verify finds what was changed by hand.
    Require(package({ "verify" }).exitCode == 0, "verify of untouched packages must pass");
    const fs::path manifest = root / "Assets" / "Packages" / "acme.b" / "package.json";
    const std::string original = ReadText(manifest);
    WriteText(manifest, original + " ");
    WriteText(root / "Assets" / "Packages" / "acme.b" / "Extra.txt", "x");
    const Run broken = package({ "verify", "--json" });
    const JsonValue problems = *Parse(broken.out).Find("data")->Find("problems");
    Require(broken.exitCode != 0 && problems.Size() == 2U && problems.At(0)->Find("file")->AsString() == "package.json" && problems.At(0)->Find("problem")->AsString() == "modified" &&
            problems.At(0)->Find("package")->AsString() == "acme.b" && problems.At(1)->Find("file")->AsString() == "Extra.txt" && problems.At(1)->Find("problem")->AsString() == "added",
        "verify must name the changed files: " + broken.out);
    fs::remove(root / "Assets" / "Packages" / "acme.b" / "Extra.txt", ec);
    fs::remove(manifest, ec);
    const Run lost = package({ "verify" });
    Require(lost.exitCode != 0 && lost.out.find("missing acme.b/package.json") != std::string::npos, "verify must name a file that is gone: " + lost.out);
    WriteText(manifest, original);
    Require(package({ "verify" }).exitCode == 0, "verify must pass again once the files are as installed");

    // remove: a dependency is not removed by name; its package takes it along.
    const Run needed = package({ "remove", "acme.b" });
    Require(needed.exitCode != 0 && needed.err.find("needed by acme.a") != std::string::npos, "a package that is needed must be kept: " + needed.err);
    const Run removed = package({ "remove", "acme.a", "--json" });
    const JsonValue removedData = *Parse(removed.out).Find("data");
    Require(removed.exitCode == 0 && removedData.Find("removed")->Size() == 2U && Changed(removedData, "Assets/Packages/acme.b/package.json", "removed"),
        "remove must take the dependencies nothing else needs: " + removed.out);
    Require(!fs::exists(root / "Assets" / "Packages") && Parse(ReadText(root / "packages.lock.json")).Find("packages")->Size() == 0U &&
            Parse(ReadText(root / "packages.json")).Find("dependencies")->Size() == 0U,
        "after the last package is removed no package files or entries remain");
    // The registry named in packages.json serves the next add.
    Require(package({ "add", "acme.b@2.1.0" }).exitCode == 0 && Parse(ReadText(root / "packages.lock.json")).Find("packages")->Find("acme.b")->Find("version")->AsString() == "2.1.0",
        "add must use the registry kept in packages.json, and an exact version");
    // A package of very many files: the lock lists them one per line and verify still covers each.
    std::vector<std::pair<std::string, std::string>> many;
    for (int index = 0; index < 1500; ++index) many.push_back({ "Data/Part" + std::to_string(index % 10) + "/File" + std::to_string(index) + ".txt", std::to_string(index) });
    Publish(registry, "acme.big", "1.0.0", {}, many);
    Require(package({ "add", "acme.big" }).exitCode == 0 && Parse(ReadText(root / "packages.lock.json")).Find("packages")->Find("acme.big")->Find("files")->Size() == 1501U,
        "a package of many files must be installed and listed in the lock");
    WriteText(root / "Assets" / "Packages" / "acme.big" / "Data" / "Part3" / "File1003.txt", "changed");
    const Run bigBroken = package({ "verify" });
    Require(bigBroken.exitCode != 0 && bigBroken.out.find("modified acme.big/Data/Part3/File1003.txt") != std::string::npos && bigBroken.out.find("1 problem") != std::string::npos,
        "verify must find the one changed file among many: " + bigBroken.out);
    fs::remove_all(base, ec);
}
