#include "CliCommands.hpp"
#include "CliProgram.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/security/Crypto.hpp"

#include "kb/packages/PackageArchive.hpp"

#include <array>
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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <mutex>
#include <thread>
#endif

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

// A package published into the registry folder with kb_cli package publish; its sources are written beside the registry.
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
    const fs::path folder = registry.parent_path() / "Sources" / name / version;
    WriteText(folder / "package.json", manifest);
    for (const auto& [path, text] : files) WriteText(folder / path, text);
    const Run published = RunLine({ "package", "publish", "--registry", registry.string(), folder.string() });
    Require(published.exitCode == 0, "publishing " + name + " " + version + " failed: " + published.out + published.err);
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


// A registry publisher's key file and its public key.
struct TestKey {
    std::string file;
    std::string publicKey;
};

[[nodiscard]] TestKey MakeKey(const fs::path& base, const std::string& name) {
    const std::string file = (base / "Keys" / (name + ".kbkey")).string();
    const Run made = RunLine({ "keys", "generate", "--out", file });
    Require(made.exitCode == 0 && made.out.size() >= 64U, "creating a registry key failed: " + made.out + made.err);
    return { file, made.out.substr(0U, 64U) };
}

// A package published into a registry folder, its index signed by the given keys.
void PublishSigned(const fs::path& registry, const std::string& name, const std::vector<TestKey>& keys) {
    const fs::path folder = registry.parent_path() / "Sources" / name / "1.0.0";
    WriteText(folder / "package.json", "{\"name\":\"" + name + "\",\"version\":\"1.0.0\"}");
    WriteText(folder / "Data" / "item.txt", name);
    std::vector<std::string> words{ "package", "publish", "--registry", registry.string(), folder.string() };
    for (const TestKey& key : keys) {
        words.insert(words.end(), { "--key", key.file });
    }
    const Run published = RunLine(words);
    Require(published.exitCode == 0, "publishing " + name + " failed: " + published.out + published.err);
}

// Changes one hexadecimal digit of the first sum in an index.
void FlipSum(const fs::path& index) {
    std::string text = ReadText(index);
    const std::size_t sum = text.find('"', text.find(':', text.find("\"sha512\"")) + 1U) + 1U;
    text[sum] = text[sum] == '0' ? '1' : '0';
    WriteText(index, text);
}

// Signed registry indexes: a project that trusts keys uses only an index signed by one of them.
void RunSignedRegistryTests(const fs::path& base) {
    const TestKey first = MakeKey(base, "first");
    const TestKey second = MakeKey(base, "second");
    const TestKey stranger = MakeKey(base, "stranger");
    const auto project = [&](const std::string& name, const std::vector<std::string>& trusted) {
        Require(RunLine({ "new", "--parent", base.string(), "--name", name, "--template", "Empty" }).exitCode == 0, "creating a project failed");
        const fs::path root = base / name;
        for (const std::string& key : trusted) {
            Require(RunLine({ "package", "trust", "--project", root.string(), "--add", key }).exitCode == 0, "package trust failed");
        }
        return root;
    };
    const auto add = [&](const fs::path& root, const fs::path& registry) {
        return RunLine({ "package", "add", "--project", root.string(), "--registry", registry.string(), "sig.pkg" });
    };
    const auto installed = [](const fs::path& root) { return fs::exists(root / "Assets" / "Packages" / "sig.pkg" / "package.json"); };
    const auto refused = [&](const fs::path& root, const fs::path& registry, const std::string& reason) {
        const auto before = Snapshot(root);
        const Run run = add(root, registry);
        Require(run.exitCode != 0 && run.err.find(reason) != std::string::npos && Snapshot(root) == before, "the registry must be refused (" + reason + "): " + run.err);
    };

    // The index is signed by the key of its publisher, and a project that trusts that key installs from it.
    const fs::path signedRegistry = base / "SignedRegistry";
    PublishSigned(signedRegistry, "sig.pkg", { first });
    const fs::path trusting = project("Trusting", { first.publicKey });
    const Run ok = add(trusting, signedRegistry);
    Require(ok.exitCode == 0 && installed(trusting), "a project must install from an index signed by a key it trusts: " + ok.out + ok.err);
    const Run signedInfo = RunLine({ "package", "info", "--project", trusting.string(), "sig.pkg", "--json" });
    Require(signedInfo.exitCode == 0 && Parse(signedInfo.out).Find("data")->Find("signed")->AsBool(), "info must say the registry is signed: " + signedInfo.out);

    // A byte of the index changed after signing.
    const fs::path tampered = project("Tampered", { first.publicKey });
    const std::string intact = ReadText(signedRegistry / "index.json");
    FlipSum(signedRegistry / "index.json");
    refused(tampered, signedRegistry, "changed after it was signed");
    WriteText(signedRegistry / "index.json", intact);
    Require(add(tampered, signedRegistry).exitCode == 0, "the restored index must verify again");

    // Signed, but by a key the project does not trust.
    const fs::path foreignRegistry = base / "ForeignRegistry";
    PublishSigned(foreignRegistry, "sig.pkg", { stranger });
    refused(project("Foreign", { first.publicKey }), foreignRegistry, "not signed by any key this project trusts");

    // No signature at all: refused when the project has keys, used (and reported as unsigned) when it has none.
    const fs::path plainRegistry = base / "PlainRegistry";
    Publish(plainRegistry, "sig.pkg", "1.0.0", {});
    refused(project("Strict", { first.publicKey }), plainRegistry, "is not signed");
    const fs::path lenient = project("Lenient", {});
    Require(add(lenient, plainRegistry).exitCode == 0 && installed(lenient), "a project without keys must use an unsigned registry");
    const Run plainInfo = RunLine({ "package", "info", "--project", lenient.string(), "sig.pkg", "--json" });
    Require(plainInfo.exitCode == 0 && !Parse(plainInfo.out).Find("data")->Find("signed")->AsBool() &&
            RunLine({ "package", "info", "--project", lenient.string(), "sig.pkg" }).out.find("unsigned registry") != std::string::npos,
        "info must call a registry the project has no keys for unsigned: " + plainInfo.out);

    // Two keys on one index: a project that trusts only the second one installs, so a publisher can change keys.
    const fs::path pairRegistry = base / "PairRegistry";
    PublishSigned(pairRegistry, "sig.pkg", { first, second });
    const fs::path second_only = project("SecondOnly", { second.publicKey });
    Require(add(second_only, pairRegistry).exitCode == 0 && installed(second_only), "the second of two signing keys must be enough");
    // sign adds a key to a registry that already has signatures; publishing into it without a key is refused.
    const Run unkeyed = RunLine({ "package", "publish", "--registry", signedRegistry.string(), (base / "Sources" / "sig.pkg" / "1.0.0").string() });
    Require(unkeyed.exitCode != 0 && unkeyed.err.find("is signed") != std::string::npos, "publishing without the key into a signed registry must be refused: " + unkeyed.err);
    Require(RunLine({ "package", "sign", "--registry", signedRegistry.string(), "--key", first.file, "--key", second.file }).exitCode == 0, "package sign failed");
    const fs::path rotated = project("Rotated", { second.publicKey });
    Require(add(rotated, signedRegistry).exitCode == 0 && installed(rotated), "a re-signed index must carry both keys");
    const Run untrusted = RunLine({ "package", "trust", "--project", rotated.string(), "--remove", second.publicKey });
    Require(untrusted.exitCode == 0 && untrusted.out.find("no registry key is trusted") != std::string::npos, "package trust --remove must drop the key: " + untrusted.out);
}

#ifdef _WIN32
// Serves the files of a folder over http on the local machine and counts the requests by path.
class FileServer {
public:
    explicit FileServer(fs::path root) : root_{ std::move(root) } {
        WSADATA data{};
        Require(WSAStartup(MAKEWORD(2, 2), &data) == 0, "winsock could not start");
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        int length = static_cast<int>(sizeof(address));
        Require(listener_ != INVALID_SOCKET && bind(listener_, reinterpret_cast<sockaddr*>(&address), length) == 0 && listen(listener_, 8) == 0 &&
                getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) == 0,
            "the test server could not listen");
        port_ = ntohs(address.sin_port);
        thread_ = std::thread{ [this] { Serve(); } };
    }
    FileServer(const FileServer&) = delete;
    FileServer& operator=(const FileServer&) = delete;
    ~FileServer() {
        closesocket(listener_);
        thread_.join();
        WSACleanup();
    }

    [[nodiscard]] std::string Url(const std::string& path) const { return "http://127.0.0.1:" + std::to_string(port_) + "/" + path; }
    // Requests whose path ends with `suffix`.
    [[nodiscard]] int Hits(const std::string& suffix) {
        const std::lock_guard lock{ mutex_ };
        int count = 0;
        for (const auto& [path, hits] : hits_) {
            if (path.ends_with(suffix)) count += hits;
        }
        return count;
    }

private:
    void Serve() {
        for (;;) {
            const SOCKET client = accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET) return;
            std::string request;
            std::array<char, 4096> buffer{};
            while (request.find("\r\n\r\n") == std::string::npos) {
                const int got = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
                if (got <= 0) break;
                request.append(buffer.data(), static_cast<std::size_t>(got));
            }
            const std::size_t end = request.find(' ', 4U);
            const std::string path = request.rfind("GET ", 0U) == 0U && end != std::string::npos ? request.substr(4U, end - 4U) : std::string{};
            {
                const std::lock_guard lock{ mutex_ };
                ++hits_[path];
            }
            std::string body;
            const bool found = path.size() > 1U && path.find("..") == std::string::npos && fs::is_regular_file(root_ / path.substr(1U));
            if (found) body = ReadText(root_ / path.substr(1U));
            const std::string answer = std::string{ found ? "HTTP/1.1 200 OK" : "HTTP/1.1 404 Not Found" } + "\r\nContent-Length: " + std::to_string(body.size()) +
                "\r\nConnection: close\r\n\r\n" + body;
            for (std::size_t sent = 0U; sent < answer.size();) {
                const int wrote = send(client, answer.data() + sent, static_cast<int>(answer.size() - sent), 0);
                if (wrote <= 0) break;
                sent += static_cast<std::size_t>(wrote);
            }
            shutdown(client, SD_SEND);
            closesocket(client);
        }
    }

    fs::path root_;
    SOCKET listener_ = INVALID_SOCKET;
    unsigned short port_ = 0U;
    std::thread thread_;
    std::mutex mutex_;
    std::map<std::string, int> hits_;
};

// Packages from a registry served over http: the archive sum is checked, downloads are cached, unsafe archives are refused.
void RunRemoteRegistryTests(const fs::path& base) {
    const fs::path served = base / "Served";
    Publish(served, "net.dep", "2.0.0", {}, { { "Data/dep.txt", "dep\n" } });
    Publish(served, "net.pkg", "1.0.0", { { "net.dep", "^2" } }, { { "Scripts/Run.lua", "-- run\n" }, { "readme.txt", "r" } }, "Over the wire");
    Publish(served, "net.bad", "1.0.0", {}, { { "x.txt", "x" } });
    FileServer server{ served };
    const std::string url = server.Url("index.json");
    const fs::path cache = base / "Cache";
    for (const char* name : { "R1", "R2" }) Require(RunLine({ "new", "--parent", base.string(), "--name", name, "--template", "Empty" }).exitCode == 0, "creating a project failed");
    const fs::path first = base / "R1";
    const fs::path second = base / "R2";
    const auto remote = [&](const fs::path& root, std::vector<std::string> words) {
        words.insert(words.begin(), "package");
        words.insert(words.end(), { "--project", root.string(), "--cache", cache.string() });
        return RunLine(words);
    };

    const Run added = remote(first, { "add", "--registry", url, "net.pkg" });
    Require(added.exitCode == 0, "add through http failed: " + added.out + added.err);
    Require(fs::exists(first / "Assets" / "Packages" / "net.pkg" / "Scripts" / "Run.lua") && fs::exists(first / "Assets" / "Packages" / "net.dep" / "Data" / "dep.txt") &&
            remote(first, { "verify" }).exitCode == 0 && Parse(ReadText(first / "packages.json")).Find("registry")->AsString() == url,
        "packages from an http registry must be installed, verified and the address kept");
    Require(server.Hits(".kbpackage") == 2, "each archive must be downloaded once");

    // The cache serves the next project: the index is read again, the archives are not downloaded.
    const int indexReads = server.Hits("index.json");
    Require(remote(second, { "add", "--registry", url, "net.pkg" }).exitCode == 0 && server.Hits(".kbpackage") == 2 && server.Hits("index.json") > indexReads,
        "a second install must come from the cache, not from the network");
    Require(remote(second, { "search" }).out.find("net.pkg") != std::string::npos, "the registry of packages.json serves later commands");

    // A changed checksum: refused, the project is as it was.
    std::string index = ReadText(served / "index.json");
    const std::size_t sum = index.find("\"sha512\": \"", index.find("\"net.bad\"")) + 11U;
    index[sum] = index[sum] == '0' ? '1' : '0';
    WriteText(served / "index.json", index);
    const auto before = Snapshot(second);
    const Run tampered = remote(second, { "add", "net.bad" });
    Require(tampered.exitCode != 0 && tampered.err.find("does not match the checksum") != std::string::npos && Snapshot(second) == before,
        "an archive that does not match the listed sum must be refused: " + tampered.err);

    // Archives that would write outside the package folder, or are not plain files, are refused before anything is written.
    const std::string manifest = R"({"name":"NAME","version":"1.0.0"})";
    const std::vector<std::pair<std::string, kb::packages::ArchiveEntry>> hostile{
        { "net.up", { "../../../../../escaped.txt", "x" } },
        { "net.abs", { "/escaped.txt", "x" } },
        { "net.drive", { "C:/escaped.txt", "x" } },
        { "net.link", { "link", "", '2' } },
    };
    JsonValue listed = *Parse(ReadText(served / "index.json")).Find("packages");
    for (const auto& [name, entry] : hostile) {
        std::string text = manifest;
        text.replace(text.find("NAME"), 4U, name);
        std::vector<std::uint8_t> archive;
        std::string error;
        Require(kb::packages::PackEntries({ { "package.json", text }, entry }, archive, error), "the hostile test archive could not be made: " + error);
        const std::string relative = name + "/1.0.0/" + name + "-1.0.0.kbpackage";
        std::error_code ec;
        fs::create_directories((served / relative).parent_path(), ec);
        std::ofstream output{ served / relative, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size()));
        output.close();
        JsonValue row = JsonValue::MakeObject();
        row.Set("name", JsonValue::MakeString(name));
        row.Set("version", JsonValue::MakeString("1.0.0"));
        row.Set("archive", JsonValue::MakeString(relative));
        row.Set("sha512", JsonValue::MakeString(kb::security::ToHex(kb::security::Sha512(archive))));
        row.Set("size", JsonValue::MakeNumber(static_cast<double>(archive.size())));
        listed.Append(std::move(row));
    }
    JsonValue rewritten = JsonValue::MakeObject();
    rewritten.Set("indexVersion", JsonValue::MakeNumber(1.0));
    rewritten.Set("packages", std::move(listed));
    WriteText(served / "index.json", rewritten.Dump());
    for (const auto& [name, entry] : hostile) {
        const auto unchanged = Snapshot(second);
        const Run refused = remote(second, { "add", name });
        Require(refused.exitCode != 0 && refused.err.find("archive is refused") != std::string::npos && Snapshot(second) == unchanged,
            "an unsafe archive (" + name + ") must be refused: " + refused.err);
    }
    Require(!fs::exists(base / "escaped.txt") && !fs::exists(second / "escaped.txt"), "no file may be written outside the package folder");

    // Plain http to another computer is refused before any request is made.
    const Run insecure = remote(second, { "search", "--registry", "http://example.invalid/index.json" });
    Require(insecure.exitCode != 0 && insecure.err.find("https://") != std::string::npos, "plain http to another host must be refused: " + insecure.err);

    // A served index is checked against the keys of the project through index.json.sig.
    const TestKey publisher = MakeKey(base, "served");
    Require(RunLine({ "package", "sign", "--registry", served.string(), "--key", publisher.file }).exitCode == 0 &&
            RunLine({ "package", "trust", "--project", second.string(), "--add", publisher.publicKey }).exitCode == 0,
        "signing the served registry failed");
    const Run servedInfo = remote(second, { "info", "net.pkg", "--json" });
    Require(servedInfo.exitCode == 0 && Parse(servedInfo.out).Find("data")->Find("signed")->AsBool(), "a served, signed index must verify: " + servedInfo.out + servedInfo.err);
    FlipSum(served / "index.json");
    const Run servedTampered = remote(second, { "info", "net.pkg" });
    Require(servedTampered.exitCode != 0 && servedTampered.err.find("changed after it was signed") != std::string::npos, "a changed served index must be refused: " + servedTampered.err);
    std::error_code removeError;
    fs::remove(served / "index.json.sig", removeError);
    const Run servedUnsigned = remote(second, { "info", "net.pkg" });
    Require(servedUnsigned.exitCode != 0 && servedUnsigned.err.find("is not signed") != std::string::npos, "a served index without a signature must be refused: " + servedUnsigned.err);
}
#endif

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
    Require(info.Find("version")->AsString() == "1.4.0" && info.Find("versions")->Size() == 4U && info.Find("size")->AsNumber() > 0.0, "info must describe the newest version in range: " + info.Dump());

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
    // A path longer than a tar name field is split into its prefix and name.
    many.push_back({ "Deep/" + std::string(40U, 'a') + "/" + std::string(40U, 'b') + "/" + std::string(40U, 'c') + ".txt", "deep" });
    Publish(registry, "acme.big", "1.0.0", {}, many);
    Require(package({ "add", "acme.big" }).exitCode == 0 && Parse(ReadText(root / "packages.lock.json")).Find("packages")->Find("acme.big")->Find("files")->Size() == 1502U,
        "a package of many files must be installed and listed in the lock");
    WriteText(root / "Assets" / "Packages" / "acme.big" / "Data" / "Part3" / "File1003.txt", "changed");
    const Run bigBroken = package({ "verify" });
    Require(bigBroken.exitCode != 0 && bigBroken.out.find("modified acme.big/Data/Part3/File1003.txt") != std::string::npos && bigBroken.out.find("1 problem") != std::string::npos,
        "verify must find the one changed file among many: " + bigBroken.out);
#ifdef _WIN32
    RunRemoteRegistryTests(base);
#endif
    RunSignedRegistryTests(base);
    fs::remove_all(base, ec);
}
