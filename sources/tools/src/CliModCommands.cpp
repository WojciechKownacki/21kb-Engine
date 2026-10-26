#include "CliCommands.hpp"

#include "engine/assets/bake/AssetPackSeal.hpp"
#include "engine/assets/bake/AssetPackTools.hpp"
#include "engine/mods/ModCatalog.hpp"
#include "engine/mods/ModRuntime.hpp"
#include "engine/security/Crypto.hpp"
#include "engine/security/ReleaseKeys.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

// kb_cli mod: build a mod from a cook, and install, list, enable, disable, remove and verify the mods of a game.
// See docs/modding.md.
namespace kb::cli {
namespace {

namespace bake = kb::assets::bake;
namespace mods = kb::mods;

[[nodiscard]] int Fail(CommandIo io, const std::string& message) {
    io.err << "error: " << message << '\n';
    return 1;
}

[[nodiscard]] std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

// The folder of installed mods: --mods <dir>.
[[nodiscard]] std::optional<mods::ModCatalog> OpenCatalog(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> root = arguments.Option("--mods");
    if (!root.has_value() || root->empty()) {
        io.err << "error: --mods <the game's Mods folder> is required\n";
        return std::nullopt;
    }
    return mods::ModCatalog{ *root };
}

// What the mods are checked against: the game's base pack (--game, with --current-release for a
// release whose packs are encrypted), and the trust of the Mods folder plus what the command line adds.
struct Setup {
    mods::ModHost host;
    mods::ModTrustPolicy policy;
};

[[nodiscard]] bool LoadSetup(const ArgumentList& arguments, const mods::ModCatalog& catalog, bool requireGame, Setup& setup, std::string& error) {
    setup.policy = catalog.ReadTrustPolicy();
    if (arguments.Flag("--allow-unsigned")) {
        setup.policy.allowUnsigned = true;
    }
    for (const std::string& key : arguments.Options("--trust")) {
        kb::security::Ed25519PublicKey parsed{};
        if (key.size() != parsed.size() * 2U || !kb::security::TryParseHex(key, parsed)) {
            error = "--trust expects 64 hexadecimal digits";
            return false;
        }
        if (!setup.policy.Trusts(parsed)) {
            setup.policy.signers.push_back(parsed);
        }
    }
    const std::optional<std::string> game = arguments.Option("--game");
    if (!game.has_value()) {
        if (requireGame) {
            error = "--game <Game.kbpackset | Game.kbpack> is required";
            return false;
        }
        return true;
    }
    bake::AssetPackTrust trust{};
    if (const std::optional<std::string> release = arguments.Option("--current-release"); release.has_value()) {
        kb::security::TrustAnchorLookup lookup = kb::security::FindReleaseTrustAnchor(*release);
        if (lookup.state != kb::security::TrustAnchorLookup::State::Present) {
            error = "no player in " + *release + " carries a trust anchor";
            return false;
        }
        trust.contentKey = std::move(lookup.anchor.packContentKey);
    }
    bake::AssetBakeDigest identity{};
    if (!mods::ReadGameIdentity(*game, trust, identity, error)) {
        return false;
    }
    setup.host.baseIdentity = identity;
    return true;
}

void PrintInfo(CommandIo io, const mods::ModInfo& info) {
    io.out << info.manifest.id << ' ' << mods::ToString(info.manifest.version) << "  " << info.manifest.name;
    if (info.problem != mods::ModProblem::None) {
        io.out << "  [" << mods::ToString(info.problem) << ": " << info.detail << "]";
    } else {
        io.out << (info.sealed ? "  sealed by " + kb::security::ToHex(*info.sealSigner).substr(0U, 16U) : std::string{ "  unsigned" });
    }
    io.out << '\n';
}

[[nodiscard]] bool ParseLevelOption(const std::optional<std::string>& text, int& level, std::string& error) {
    level = 9;
    if (!text.has_value()) {
        return true;
    }
    const auto [end, parseError] = std::from_chars(text->data(), text->data() + text->size(), level);
    if (parseError != std::errc{} || end != text->data() + text->size() || level < 0 || level > 19) {
        error = "--level expects 0 (no compression) to 19";
        return false;
    }
    return true;
}

int RunModBuild(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> base = arguments.Option("--game");
    const std::optional<std::string> cook = arguments.Option("--cook");
    const std::optional<std::string> output = arguments.Option("--output");
    const std::optional<std::string> id = arguments.Option("--id");
    const std::optional<std::string> version = arguments.Option("--version");
    if (!base.has_value() || !cook.has_value() || !output.has_value() || !id.has_value() || !version.has_value()) {
        return Fail(io, "mod build expects --game <Game.kbpackset | Game.kbpack> --cook <cook of the mod's project.kbpack> --id <mod id> --version <x.y.z> "
                        "--output <folder> [--name <name>] [--author <text>] [--description <text>] [--api <range>] [--requires <id>[@<range>]]... "
                        "[--conflicts <id>]... [--entry <virtual path of a scene>] [--key <signing key file>] [--level <0-19>] [--current-release <dir>]");
    }
    std::string error;
    mods::ModManifest manifest;
    manifest.id = *id;
    manifest.name = arguments.Option("--name").value_or(*id);
    manifest.author = arguments.Option("--author").value_or("");
    manifest.description = arguments.Option("--description").value_or("");
    manifest.entryScene = arguments.Option("--entry").value_or("");
    if (!mods::IsValidModId(*id)) {
        return Fail(io, "--id: lower case letters, digits, '.', '_' and '-', at most 48 characters");
    }
    if (!mods::ParseModVersion(*version, manifest.version)) {
        return Fail(io, "--version expects major.minor.patch");
    }
    if (!mods::ParseModVersionRange(arguments.Option("--api").value_or(std::string{ "^" } + mods::ToString(mods::kModApiVersion)), manifest.api)) {
        return Fail(io, "--api expects a version range such as ^1.0.0");
    }
    for (const std::string& text : arguments.Options("--requires")) {
        mods::ModDependency dependency;
        const std::size_t at = text.find('@');
        dependency.id = text.substr(0U, at);
        std::string range = at == std::string::npos ? std::string{ "*" } : text.substr(at + 1U);
        std::ranges::replace(range, ',', ' ');
        if (!mods::IsValidModId(dependency.id) || !mods::ParseModVersionRange(range, dependency.range)) {
            return Fail(io, "--requires expects <mod id>[@<range>], with commas between the comparators of a range: " + text);
        }
        manifest.dependencies.push_back(std::move(dependency));
    }
    manifest.conflicts = arguments.Options("--conflicts");
    manifest.pack = *id + std::string{ bake::kAssetPackFileExtension };
    if (!mods::IsValidModPackFileName(manifest.pack)) {
        return Fail(io, "the mod id does not make a valid pack file name");
    }
    int level = 0;
    if (!ParseLevelOption(arguments.Option("--level"), level, error)) {
        return Fail(io, error);
    }
    std::optional<kb::security::ReleaseSigningKey> key;
    if (const std::optional<std::string> keyPath = arguments.Option("--key"); keyPath.has_value()) {
        key.emplace();
        if (!kb::security::DecodeReleaseSigningKey(ReadText(*keyPath), *key, error)) {
            return Fail(io, "--key: " + error);
        }
    }

    bake::AssetPackModChunkRequest request{};
    request.base = *base;
    request.next = *cook;
    request.label = *id;
    request.compression = level == 0 ? bake::AssetPackBlockCompression::None : bake::AssetPackBlockCompression::Zstd;
    request.compressionLevel = level == 0 ? 9 : level;
    if (const std::optional<std::string> release = arguments.Option("--current-release"); release.has_value()) {
        kb::security::TrustAnchorLookup lookup = kb::security::FindReleaseTrustAnchor(*release);
        if (lookup.state != kb::security::TrustAnchorLookup::State::Present) {
            return Fail(io, "no player in " + *release + " carries a trust anchor");
        }
        request.baseTrust.requiredSigner = lookup.anchor.releaseKey;
        request.baseTrust.contentKey = std::move(lookup.anchor.packContentKey);
    }
    const std::filesystem::path folder{ *output };
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    request.output = folder / manifest.pack;
    std::filesystem::remove(request.output, ec);
    bake::AssetPackModChunkReport report{};
    if (!bake::BuildAssetPackModChunk(request, report, error)) {
        return Fail(io, error);
    }
    if (key.has_value() && !bake::SealAssetPack(request.output, *key, nullptr, error)) {
        return Fail(io, "sealing the pack: " + error);
    }
    if (!mods::BindModManifestToPack(folder, manifest, error)) {
        return Fail(io, error);
    }
    const std::string text = mods::EncodeModManifest(manifest);
    if (text.empty()) {
        return Fail(io, "the manifest is not valid (check the entry scene and the dependency list)");
    }
    {
        std::ofstream manifestFile{ folder / std::string{ mods::kModManifestFileName }, std::ios::binary | std::ios::trunc };
        manifestFile << text;
        if (!manifestFile) {
            return Fail(io, "could not write " + std::string{ mods::kModManifestFileName });
        }
    }
    io.out << "mod " << manifest.id << ' ' << mods::ToString(manifest.version) << ": " << report.addedAssets << " asset(s) added, "
           << report.skippedExistingAssets << " of the game's own left out, " << (key.has_value() ? "sealed" : "not sealed") << '\n'
           << "wrote " << (folder / manifest.pack).string() << " and " << (folder / std::string{ mods::kModManifestFileName }).string() << '\n';
    return 0;
}

int RunModInstall(const ArgumentList& arguments, CommandIo io) {
    if (arguments.Positionals().size() != 2U) {
        return Fail(io, "mod install expects --mods <Mods folder> [--game <Game.kbpackset | Game.kbpack>] [--trust <key>]... [--allow-unsigned] [--replace] <mod folder>");
    }
    const std::optional<mods::ModCatalog> opened = OpenCatalog(arguments, io);
    if (!opened.has_value()) {
        return 1;
    }
    mods::ModCatalog catalog = *opened;
    Setup setup;
    std::string error;
    if (!LoadSetup(arguments, catalog, false, setup, error)) {
        return Fail(io, error);
    }
    mods::ModInfo installed;
    if (!catalog.Install(arguments.Positionals()[1], setup.host, setup.policy, arguments.Flag("--replace"), installed, error)) {
        return Fail(io, error);
    }
    io.out << "installed ";
    PrintInfo(io, installed);
    return 0;
}

int RunModList(const ArgumentList& arguments, CommandIo io) {
    const std::optional<mods::ModCatalog> catalog = OpenCatalog(arguments, io);
    if (!catalog.has_value()) {
        return 1;
    }
    Setup setup;
    std::string error;
    if (!LoadSetup(arguments, *catalog, false, setup, error)) {
        return Fail(io, error);
    }
    const std::vector<mods::ModInfo> infos = catalog->List(setup.host, setup.policy);
    for (const mods::ModInfo& info : infos) {
        io.out << (info.enabled ? "[x] " : "[ ] ");
        PrintInfo(io, info);
    }
    io.out << infos.size() << " mod(s) installed, " << std::ranges::count_if(infos, [](const mods::ModInfo& info) { return info.enabled; }) << " enabled\n";
    return 0;
}

int RunModVerify(const ArgumentList& arguments, CommandIo io) {
    const std::optional<mods::ModCatalog> catalog = OpenCatalog(arguments, io);
    if (!catalog.has_value()) {
        return 1;
    }
    Setup setup;
    std::string error;
    if (!LoadSetup(arguments, *catalog, true, setup, error)) {
        return Fail(io, error);
    }
    const mods::ModPlan plan = catalog->Resolve(setup.host, setup.policy);
    for (const mods::ModInfo& info : plan.load) {
        io.out << "load     ";
        PrintInfo(io, info);
    }
    for (const mods::ModInfo& info : plan.refused) {
        io.out << "refused  ";
        PrintInfo(io, info);
    }
    for (const mods::ModInfo& info : plan.disabled) {
        io.out << "disabled ";
        PrintInfo(io, info);
    }
    io.out << plan.load.size() << " would load, " << plan.refused.size() << " refused, " << plan.disabled.size() << " disabled\n";
    return plan.refused.empty() ? 0 : 1;
}

int RunModToggle(const ArgumentList& arguments, CommandIo io, bool enable) {
    if (arguments.Positionals().size() != 2U) {
        return Fail(io, std::string{ "mod " } + (enable ? "enable" : "disable") + " expects --mods <Mods folder> <mod id>");
    }
    std::optional<mods::ModCatalog> catalog = OpenCatalog(arguments, io);
    if (!catalog.has_value()) {
        return 1;
    }
    std::string error;
    if (!catalog->SetEnabled(arguments.Positionals()[1], enable, error)) {
        return Fail(io, error);
    }
    io.out << (enable ? "enabled " : "disabled ") << arguments.Positionals()[1] << '\n';
    return 0;
}

int RunModRemove(const ArgumentList& arguments, CommandIo io) {
    if (arguments.Positionals().size() != 2U) {
        return Fail(io, "mod remove expects --mods <Mods folder> <mod id>");
    }
    std::optional<mods::ModCatalog> catalog = OpenCatalog(arguments, io);
    if (!catalog.has_value()) {
        return 1;
    }
    std::string error;
    if (!catalog->Remove(arguments.Positionals()[1], error)) {
        return Fail(io, error);
    }
    io.out << "removed " << arguments.Positionals()[1] << '\n';
    return 0;
}

int RunModTrust(const ArgumentList& arguments, CommandIo io) {
    const std::optional<mods::ModCatalog> catalog = OpenCatalog(arguments, io);
    if (!catalog.has_value()) {
        return 1;
    }
    mods::ModTrustPolicy policy = catalog->ReadTrustPolicy();
    std::string error;
    bool changed = false;
    for (const std::string& text : arguments.Options("--add")) {
        kb::security::Ed25519PublicKey key{};
        if (text.size() != key.size() * 2U || !kb::security::TryParseHex(text, key)) {
            return Fail(io, "--add expects the 64 hexadecimal digits of a public key");
        }
        if (!policy.Trusts(key)) {
            policy.signers.push_back(key);
            changed = true;
        }
    }
    for (const std::string& text : arguments.Options("--remove")) {
        kb::security::Ed25519PublicKey key{};
        if (text.size() != key.size() * 2U || !kb::security::TryParseHex(text, key)) {
            return Fail(io, "--remove expects the 64 hexadecimal digits of a public key");
        }
        const auto found = std::ranges::find(policy.signers, key);
        if (found != policy.signers.end()) {
            policy.signers.erase(found);
            changed = true;
        }
    }
    if (arguments.Flag("--allow-unsigned") && !policy.allowUnsigned) {
        policy.allowUnsigned = true;
        changed = true;
    }
    if (arguments.Flag("--deny-unsigned") && policy.allowUnsigned) {
        policy.allowUnsigned = false;
        changed = true;
    }
    if (changed && !catalog->WriteTrustPolicy(policy, error)) {
        return Fail(io, error);
    }
    io.out << "unsigned mods: " << (policy.allowUnsigned ? "allowed" : "refused") << '\n';
    for (const kb::security::Ed25519PublicKey& key : policy.signers) {
        io.out << "trusted signer " << kb::security::ToHex(key) << '\n';
    }
    return 0;
}

int RunModInfo(const ArgumentList& arguments, CommandIo io) {
    if (arguments.Positionals().size() != 2U) {
        return Fail(io, "mod info expects [--game <Game.kbpackset | Game.kbpack>] [--trust <key>]... [--allow-unsigned] <mod folder>");
    }
    Setup setup;
    std::string error;
    // The trust file of the Mods folder the mod folder lies in, when it lies in one.
    const mods::ModCatalog scratch{ std::filesystem::path{ arguments.Positionals()[1] }.parent_path() };
    if (!LoadSetup(arguments, scratch, false, setup, error)) {
        return Fail(io, error);
    }
    const mods::ModInfo info = mods::InspectMod(arguments.Positionals()[1], setup.host, setup.policy);
    PrintInfo(io, info);
    if (info.problem == mods::ModProblem::None) {
        io.out << "  needs: ";
        if (info.manifest.dependencies.empty()) {
            io.out << "nothing";
        }
        for (const mods::ModDependency& dependency : info.manifest.dependencies) {
            io.out << dependency.id << ' ' << mods::ToString(dependency.range) << "; ";
        }
        io.out << "\n  entry scene: " << (info.manifest.entryScene.empty() ? std::string{ "none" } : info.manifest.entryScene) << '\n';
    }
    return info.problem == mods::ModProblem::None ? 0 : 1;
}

} // namespace

int RunModCommand(const ArgumentList& arguments, CommandIo io) {
    const std::string action = arguments.Positionals().empty() ? std::string{} : arguments.Positionals().front();
    if (action == "build") {
        return RunModBuild(arguments, io);
    }
    if (action == "install") {
        return RunModInstall(arguments, io);
    }
    if (action == "list") {
        return RunModList(arguments, io);
    }
    if (action == "verify") {
        return RunModVerify(arguments, io);
    }
    if (action == "enable") {
        return RunModToggle(arguments, io, true);
    }
    if (action == "disable") {
        return RunModToggle(arguments, io, false);
    }
    if (action == "remove") {
        return RunModRemove(arguments, io);
    }
    if (action == "trust") {
        return RunModTrust(arguments, io);
    }
    if (action == "info") {
        return RunModInfo(arguments, io);
    }
    return Fail(io, "mod expects build, install, list, verify, enable, disable, remove, trust or info");
}

} // namespace kb::cli
