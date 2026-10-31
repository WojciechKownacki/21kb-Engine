#include "TestSupport.hpp"

#include "engine/assets/bake/AssetPackSeal.hpp"
#include "engine/assets/bake/AssetPackWriter.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/mods/ModCatalog.hpp"
#include "engine/mods/ModManifest.hpp"
#include "engine/mods/ModVersion.hpp"
#include "engine/security/ReleaseKeys.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace kb::tests {
namespace {

namespace bake = kb::assets::bake;
namespace mods = kb::mods;

void Check(bool condition, const std::string& message) {
    Require(condition, message.c_str());
}

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_engine_mod_tests";
}

void Purge(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove_all(path, error);
}

[[nodiscard]] mods::ModVersion V(std::uint32_t major, std::uint32_t minor, std::uint32_t patch) {
    return mods::ModVersion{ major, minor, patch };
}

[[nodiscard]] bool Allows(std::string_view range, mods::ModVersion version) {
    mods::ModVersionRange parsed;
    return mods::ParseModVersionRange(range, parsed) && mods::Satisfies(parsed, version);
}

// Red when: a version parses with a leading zero, a missing part or a sign, a range reads wrongly,
// or the caret and tilde shorthands drift from their meaning.
void VersionsAndRanges() {
    mods::ModVersion version;
    Check(mods::ParseModVersion("1.2.3", version) && version == V(1, 2, 3), "1.2.3 parses");
    Check(mods::ToString(V(10, 0, 7)) == "10.0.7", "versions print as major.minor.patch");
    for (const std::string_view bad : { "", "1", "1.2", "1.2.3.4", "01.2.3", "1.02.3", "1.2.-3", "a.b.c", "1.2.1000000", " 1.2.3", "1.2.3 " }) {
        Check(!mods::ParseModVersion(bad, version), "a malformed version is refused: " + std::string{ bad });
    }
    Check(V(1, 2, 3) < V(1, 10, 0) && V(2, 0, 0) > V(1, 99, 99), "versions compare by number, not by text");

    Check(Allows("*", V(0, 0, 1)), "* allows any version");
    Check(Allows("1.2.3", V(1, 2, 3)) && !Allows("1.2.3", V(1, 2, 4)), "a bare version is exact");
    Check(Allows(">=1.2.0 <2.0.0", V(1, 9, 9)) && !Allows(">=1.2.0 <2.0.0", V(2, 0, 0)) && !Allows(">=1.2.0 <2.0.0", V(1, 1, 9)), "a range needs every comparator");
    Check(Allows("^1.2.0", V(1, 9, 0)) && !Allows("^1.2.0", V(2, 0, 0)) && !Allows("^1.2.0", V(1, 1, 0)), "^ keeps the major version");
    Check(Allows("~1.2.0", V(1, 2, 9)) && !Allows("~1.2.0", V(1, 3, 0)), "~ keeps the minor version");
    Check(Allows(">1.0.0", V(1, 0, 1)) && !Allows(">1.0.0", V(1, 0, 0)) && Allows("<=1.0.0", V(1, 0, 0)) && !Allows("<1.0.0", V(1, 0, 0)), "strict and loose bounds differ");
    mods::ModVersionRange range;
    for (const std::string_view bad : { "", ">=", ">=1.2", "* >=1.0.0", "1.0.0 >=banana", ">=1.0.0 <2.0.0 >=1.0.0 <2.0.0 >=1.0.0 <2.0.0 >=1.0.0 <2.0.0 >=1.0.0" }) {
        Check(!mods::ParseModVersionRange(bad, range), "a malformed range is refused: " + std::string{ bad });
    }
    Check(mods::ParseModVersionRange("^1.2.0", range) && mods::ToString(range) == ">=1.2.0 <2.0.0", "a range prints canonically");
}

constexpr const char* kGoodManifest =
    "21kb-mod 1\n"
    "# comment\n"
    "id acme.crates\n"
    "name Crate Pack\n"
    "version 1.2.0\n"
    "author Acme\n"
    "description More crates.\n"
    "api ^1.0.0\n"
    "requires acme.core >=1.0.0 <2.0.0\n"
    "requires acme.extras\n"
    "conflicts acme.oldcrates\n"
    "entry /Game/Mods/Crates/Main.21kbscene\n"
    "pack Crates.kbpack\n"
    "pack-sha512 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\n"
    "signer 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\n";

[[nodiscard]] std::string Replace(std::string text, std::string_view from, std::string_view to) {
    const std::size_t at = text.find(from);
    Check(at != std::string::npos, "the test text contains " + std::string{ from });
    text.replace(at, from.size(), to);
    return text;
}

[[nodiscard]] mods::ModManifestStatus StatusOf(const std::string& text) {
    mods::ModManifest manifest;
    return mods::ParseModManifest(text, manifest).status;
}

// Red when: a field is lost, a manifest that is not exactly right is accepted, or a key that would
// load native code gets through under any name.
void ManifestParsing() {
    mods::ModManifest manifest;
    const mods::ModManifestResult result = mods::ParseModManifest(kGoodManifest, manifest);
    Check(result.Succeeded(), "a good manifest parses: " + result.Describe());
    Check(manifest.id == "acme.crates" && manifest.name == "Crate Pack" && manifest.version == V(1, 2, 0) && manifest.author == "Acme" &&
            manifest.description == "More crates." && manifest.pack == "Crates.kbpack" && manifest.entryScene == "/Game/Mods/Crates/Main.21kbscene",
        "the manifest fields are read");
    Check(manifest.dependencies.size() == 2U && manifest.dependencies[0].id == "acme.core" && Allows(mods::ToString(manifest.dependencies[0].range), V(1, 5, 0)) &&
            manifest.dependencies[1].range.comparators.empty() && manifest.conflicts.size() == 1U && manifest.signer.has_value() && (*manifest.signer)[0] == 0x00U &&
            (*manifest.signer)[31] == 0xFFU && manifest.packSha512[1] == 0x11U,
        "dependencies, conflicts, the signer and the pack hash are read");

    const std::string encoded = mods::EncodeModManifest(manifest);
    mods::ModManifest again;
    Check(!encoded.empty() && mods::ParseModManifest(encoded, again).Succeeded() && again == manifest, "a manifest survives encoding");
    Check(mods::EncodeModManifest(again) == encoded, "the encoding is canonical");

    const std::string good = kGoodManifest;
    Check(StatusOf("id x\n") == mods::ModManifestStatus::NotAManifest, "the header comes first");
    Check(StatusOf("21kb-mod 2\n") == mods::ModManifestStatus::UnsupportedVersion, "another manifest version is refused");
    Check(StatusOf("") == mods::ModManifestStatus::NotAManifest, "an empty manifest is refused");
    Check(StatusOf(good + "wat 1\n") == mods::ModManifestStatus::UnknownKey, "an unknown key is refused");
    Check(StatusOf(good + "name Again\n") == mods::ModManifestStatus::DuplicateKey, "a repeated key is refused");
    Check(StatusOf(Replace(good, "id acme.crates", "id Acme.Crates")) == mods::ModManifestStatus::InvalidValue, "an id has no capital letters");
    Check(StatusOf(Replace(good, "id acme.crates", "id " + std::string(49U, 'a'))) == mods::ModManifestStatus::InvalidValue, "an id is at most 48 characters");
    Check(StatusOf(Replace(good, "id acme.crates", "id ../evil")) == mods::ModManifestStatus::InvalidValue, "an id is not a path");
    Check(StatusOf(Replace(good, "version 1.2.0", "version 1.2")) == mods::ModManifestStatus::InvalidValue, "a version has three parts");
    Check(StatusOf(Replace(good, "pack Crates.kbpack", "pack ../Crates.kbpack")) == mods::ModManifestStatus::InvalidValue, "a pack is not in another folder");
    Check(StatusOf(Replace(good, "pack Crates.kbpack", "pack Crates.dll")) == mods::ModManifestStatus::InvalidValue, "a pack is a .kbpack");
    Check(StatusOf(Replace(good, "entry /Game/Mods/Crates/Main.21kbscene", "entry /Game/../Main.21kbscene")) == mods::ModManifestStatus::InvalidValue, "an entry scene stays in the project");
    Check(StatusOf(Replace(good, "pack-sha512 0011", "pack-sha512 00")) == mods::ModManifestStatus::InvalidValue, "the pack hash is 128 digits");
    Check(StatusOf(Replace(good, "requires acme.extras", "requires acme.core")) == mods::ModManifestStatus::InvalidValue, "a mod is required once");
    Check(StatusOf(Replace(good, "requires acme.extras", "requires acme.crates")) == mods::ModManifestStatus::InvalidValue, "a mod cannot require itself");
    Check(StatusOf(Replace(good, "conflicts acme.oldcrates", "conflicts acme.extras")) == mods::ModManifestStatus::InvalidValue, "a mod cannot both require and conflict");
    Check(StatusOf(Replace(good, "conflicts acme.oldcrates", "conflicts acme.crates")) == mods::ModManifestStatus::InvalidValue, "a mod cannot conflict with itself");
    Check(StatusOf(Replace(good, "name Crate Pack\n", "")) == mods::ModManifestStatus::MissingField, "a name is required");
    Check(StatusOf(Replace(good, "pack Crates.kbpack\n", "")) == mods::ModManifestStatus::MissingField, "a pack is required");
    Check(StatusOf(Replace(good, "version 1.2.0\n", "")) == mods::ModManifestStatus::MissingField, "a version is required");
    Check(StatusOf(std::string(mods::kMaxModManifestBytes + 1U, ' ')) == mods::ModManifestStatus::TooLarge, "a huge manifest is refused");
    for (const char* key : { "native", "plugin", "plugins", "library", "libraries", "dll", "module", "executable", "binary" }) {
        const mods::ModManifestResult native = mods::ParseModManifest(good + key + " Crates.dll\n", manifest);
        Check(native.status == mods::ModManifestStatus::NativeCodeRefused && native.line == 16U && native.Describe().find("native code") != std::string::npos,
            std::string{ "a manifest key that would load native code is refused: " } + key);
    }
    // CRLF files from Windows editors parse the same.
    std::string crlf;
    for (const char character : good) {
        if (character == '\n') crlf += '\r';
        crlf += character;
    }
    Check(StatusOf(crlf) == mods::ModManifestStatus::Success, "a manifest with Windows line ends parses");
}

void TrustPolicyFiles() {
    mods::ModTrustPolicy policy;
    std::string error;
    const std::string key(64U, 'a');
    Check(mods::ParseModTrustPolicy("21kb-mod-trust 1\nallow-unsigned\nsigner " + key + " Acme\n", policy, error) && policy.allowUnsigned && policy.signers.size() == 1U, "a trust file parses: " + error);
    mods::ModTrustPolicy again;
    Check(mods::ParseModTrustPolicy(mods::EncodeModTrustPolicy(policy), again, error) && again == policy, "a trust file survives encoding");
    Check(policy.Trusts(policy.signers.front()) && !policy.Trusts(kb::security::Ed25519PublicKey{}), "trust is by key");
    Check(!mods::ParseModTrustPolicy("allow-unsigned\n", policy, error), "a trust file starts with its header");
    Check(!mods::ParseModTrustPolicy("21kb-mod-trust 1\nsigner abc\n", policy, error), "a short key is refused");
    Check(!mods::ParseModTrustPolicy("21kb-mod-trust 1\ntrust-everyone\n", policy, error), "an unknown line is refused");
}

// ---- Mod fixtures: real chunk packs, optionally sealed ----

struct ModSpec {
    std::string id;
    mods::ModVersion version{ 1U, 0U, 0U };
    std::vector<std::pair<std::string, std::string>> needs;
    std::vector<std::string> conflicts;
    const kb::security::ReleaseSigningKey* sealWith = nullptr;
    bake::AssetPackRole role = bake::AssetPackRole::Chunk;
    std::string label;
    std::string api = "^1.0.0";
    bake::AssetBakeDigest base{};
    bool manifestNamesSigner = true;
};

[[nodiscard]] bake::AssetBakeDigest BaseIdentity(std::uint8_t seed) {
    bake::AssetBakeDigest digest{};
    digest.high = 0x0101010101010101ULL * seed;
    digest.low = seed;
    return digest;
}

void WriteText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output << text;
}

[[nodiscard]] std::filesystem::path MakeMod(const std::filesystem::path& root, const ModSpec& spec) {
    const std::filesystem::path folder = root / spec.id;
    std::filesystem::create_directories(folder);
    const std::filesystem::path pack = folder / "Content.kbpack";
    {
        bake::AssetPackWriterOptions options;
        options.role = spec.role;
        if (spec.role == bake::AssetPackRole::Chunk) {
            options.label = spec.label.empty() ? spec.id : spec.label;
            options.baseIdentity = spec.base == bake::AssetBakeDigest{} ? BaseIdentity(9) : spec.base;
        }
        bake::AssetPackWriter writer{ pack, bake::WindowsX64BakeTargetProfile(), options };
        bake::BakedAssetDescriptor descriptor;
        descriptor.key.sourceContentHash = std::hash<std::string>{}(spec.id) | 1U;
        descriptor.key.bakerId = "Blob";
        descriptor.key.bakerVersion = "1";
        descriptor.key.targetProfileId = "Windows.x64";
        descriptor.key.targetProfileHash = bake::BakeTargetProfileFingerprint(bake::WindowsX64BakeTargetProfile());
        descriptor.key.settingsHash = 7U;
        descriptor.assetTypeId = "Blob";
        const std::vector<std::uint8_t> bytes(300U, static_cast<std::uint8_t>(spec.id.size()));
        Require(writer.BeginAsset(descriptor) == bake::BakedAssetSinkStatus::Success && writer.WritePrimaryBlock(bytes, 1U) == bake::BakedAssetSinkStatus::Success &&
                writer.CommitAsset() == bake::BakedAssetSinkStatus::Success && writer.Finish() == bake::BakedAssetSinkStatus::Success,
            "the mod fixture pack is written");
    }
    if (spec.sealWith != nullptr) {
        std::string error;
        Check(bake::SealAssetPack(pack, *spec.sealWith, nullptr, error), "the mod fixture pack is sealed: " + error);
    }
    mods::ModManifest manifest;
    manifest.id = spec.id;
    manifest.name = "Mod " + spec.id;
    manifest.version = spec.version;
    Require(mods::ParseModVersionRange(spec.api, manifest.api), "the fixture api range parses");
    for (const auto& [id, range] : spec.needs) {
        mods::ModDependency dependency;
        dependency.id = id;
        Require(mods::ParseModVersionRange(range, dependency.range), "the fixture dependency range parses");
        manifest.dependencies.push_back(std::move(dependency));
    }
    manifest.conflicts = spec.conflicts;
    manifest.pack = "Content.kbpack";
    std::string error;
    Check(mods::BindModManifestToPack(folder, manifest, error), "the manifest binds to its pack: " + error);
    if (!spec.manifestNamesSigner) {
        manifest.signer.reset();
    }
    const std::string text = mods::EncodeModManifest(manifest);
    Check(!text.empty(), "the fixture manifest encodes");
    WriteText(folder / std::string{ mods::kModManifestFileName }, text);
    return folder;
}

[[nodiscard]] kb::security::ReleaseSigningKey NewKey() {
    kb::security::ReleaseSigningKey key{};
    Require(kb::security::GenerateReleaseSigningKey(key), "a signing key is generated");
    return key;
}

[[nodiscard]] mods::ModTrustPolicy Trusting(const kb::security::ReleaseSigningKey& key) {
    mods::ModTrustPolicy policy;
    policy.signers.push_back(key.publicKey);
    return policy;
}

[[nodiscard]] mods::ModProblem ProblemOf(const mods::ModPlan& plan, std::string_view id) {
    for (const mods::ModInfo& info : plan.refused) {
        if (info.manifest.id == id) return info.problem;
    }
    for (const mods::ModInfo& info : plan.disabled) {
        if (info.manifest.id == id) return mods::ModProblem::Disabled;
    }
    return plan.FindLoad(id) != nullptr ? mods::ModProblem::None : mods::ModProblem::ManifestInvalid;
}

[[nodiscard]] std::vector<std::string> LoadOrder(const mods::ModPlan& plan) {
    std::vector<std::string> ids;
    for (const mods::ModInfo& info : plan.load) ids.push_back(info.manifest.id);
    return ids;
}

void FlipByte(const std::filesystem::path& path, std::uintmax_t offset) {
    std::fstream file{ path, std::ios::binary | std::ios::in | std::ios::out };
    file.seekg(static_cast<std::streamoff>(offset));
    char value = 0;
    file.read(&value, 1);
    value = static_cast<char>(value ^ 0x40);
    file.seekp(static_cast<std::streamoff>(offset));
    file.write(&value, 1);
}

// Red when: a mod that should not load is inspected clean, or one that should is refused for the
// wrong reason. Every case is aimed at its own ModProblem, not merely at "refused".
void InspectingMods() {
    const std::filesystem::path root = TestRoot() / "inspect";
    Purge(root);
    std::filesystem::create_directories(root);
    const kb::security::ReleaseSigningKey author = NewKey();
    const kb::security::ReleaseSigningKey stranger = NewKey();
    const mods::ModHost host;
    const mods::ModTrustPolicy trusting = Trusting(author);

    const std::filesystem::path good = MakeMod(root, ModSpec{ .id = "good", .sealWith = &author });
    const mods::ModInfo info = mods::InspectMod(good, host, trusting);
    Check(info.problem == mods::ModProblem::None && info.sealed && info.sealSigner == author.publicKey && info.manifest.signer == author.publicKey, "a sealed mod by a trusted author is clean: " + info.Describe());

    Check(mods::InspectMod(good, host, mods::ModTrustPolicy{}).problem == mods::ModProblem::UntrustedSigner, "a seal nobody trusts is refused");
    Check(mods::InspectMod(good, host, Trusting(stranger)).problem == mods::ModProblem::UntrustedSigner, "a seal by another key is refused");

    const std::filesystem::path unsigned_ = MakeMod(root, ModSpec{ .id = "plain" });
    Check(mods::InspectMod(unsigned_, host, trusting).problem == mods::ModProblem::Unsigned, "an unsealed mod is refused by default");
    mods::ModTrustPolicy lax = trusting;
    lax.allowUnsigned = true;
    const mods::ModInfo plain = mods::InspectMod(unsigned_, host, lax);
    Check(plain.problem == mods::ModProblem::None && !plain.sealed, "an unsealed mod loads only when the player allows it");

    // A manifest that names a signer cannot be satisfied by stripping the seal.
    const std::filesystem::path stripped = MakeMod(root, ModSpec{ .id = "stripped" });
    {
        mods::ModManifest manifest;
        Require(mods::ReadModManifest(stripped / "Mod.kbmod", manifest).Succeeded(), "stripped manifest reads");
        manifest.signer = author.publicKey;
        WriteText(stripped / "Mod.kbmod", mods::EncodeModManifest(manifest));
    }
    Check(mods::InspectMod(stripped, host, lax).problem == mods::ModProblem::Unsigned, "a mod whose seal was stripped is refused even when unsigned mods are allowed");

    // A seal by a different key than the manifest names.
    const std::filesystem::path swapped = MakeMod(root, ModSpec{ .id = "swapped", .sealWith = &stranger });
    {
        mods::ModManifest manifest;
        Require(mods::ReadModManifest(swapped / "Mod.kbmod", manifest).Succeeded(), "swapped manifest reads");
        manifest.signer = author.publicKey;
        WriteText(swapped / "Mod.kbmod", mods::EncodeModManifest(manifest));
    }
    Check(mods::InspectMod(swapped, host, trusting).problem == mods::ModProblem::SignerMismatch, "a manifest that names another signer than the seal is refused");

    // Tampering: the catalogue is signed, so a change there fails at mount; a payload change keeps the
    // catalogue valid and is caught by the manifest's hash (and by the block digest when read).
    const std::filesystem::path catalogue = MakeMod(root, ModSpec{ .id = "catalogue", .sealWith = &author });
    FlipByte(catalogue / "Content.kbpack", 40U);
    Check(mods::InspectMod(catalogue, host, trusting).problem == mods::ModProblem::PackRejected, "a pack with a changed header does not mount");
    const std::filesystem::path payload = MakeMod(root, ModSpec{ .id = "payload", .sealWith = &author });
    FlipByte(payload / "Content.kbpack", std::filesystem::file_size(payload / "Content.kbpack") - 400U);
    const mods::ModProblem payloadProblem = mods::InspectMod(payload, host, trusting).problem;
    Check(payloadProblem == mods::ModProblem::PackHashMismatch || payloadProblem == mods::ModProblem::PackRejected, "a pack with a changed payload is refused");
    const std::filesystem::path truncated = MakeMod(root, ModSpec{ .id = "truncated", .sealWith = &author });
    std::filesystem::resize_file(truncated / "Content.kbpack", 100U);
    Check(mods::InspectMod(truncated, host, trusting).problem == mods::ModProblem::PackRejected, "a truncated pack is refused");
    const std::filesystem::path garbage = MakeMod(root, ModSpec{ .id = "garbage", .sealWith = &author });
    WriteText(garbage / "Content.kbpack", "not a pack at all");
    Check(mods::InspectMod(garbage, host, trusting).problem == mods::ModProblem::PackRejected, "a file that is not a pack is refused");

    const std::filesystem::path missing = MakeMod(root, ModSpec{ .id = "missing", .sealWith = &author });
    std::filesystem::remove(missing / "Content.kbpack");
    Check(mods::InspectMod(missing, host, trusting).problem == mods::ModProblem::PackMissing, "a missing pack is refused");

    const std::filesystem::path wrongRole = MakeMod(root, ModSpec{ .id = "wrongrole", .sealWith = &author, .role = bake::AssetPackRole::Base });
    Check(mods::InspectMod(wrongRole, host, trusting).problem == mods::ModProblem::PackNotAModChunk, "a base pack is not a mod");
    const std::filesystem::path wrongLabel = MakeMod(root, ModSpec{ .id = "wronglabel", .sealWith = &author, .label = "other" });
    Check(mods::InspectMod(wrongLabel, host, trusting).problem == mods::ModProblem::PackNotAModChunk, "a pack labelled for another mod is refused");

    const std::filesystem::path newer = MakeMod(root, ModSpec{ .id = "newer", .sealWith = &author, .api = ">=2.0.0" });
    Check(mods::InspectMod(newer, host, trusting).problem == mods::ModProblem::ApiIncompatible, "a mod for a newer mod interface is refused");

    const std::filesystem::path forBase = MakeMod(root, ModSpec{ .id = "forbase", .sealWith = &author, .base = BaseIdentity(1) });
    mods::ModHost sameBase;
    sameBase.baseIdentity = BaseIdentity(1);
    mods::ModHost otherBase;
    otherBase.baseIdentity = BaseIdentity(2);
    Check(mods::InspectMod(forBase, sameBase, trusting).problem == mods::ModProblem::None, "a mod cooked against this build loads");
    Check(mods::InspectMod(forBase, otherBase, trusting).problem == mods::ModProblem::BaseMismatch, "a mod cooked against another build of the game is refused");

    const std::filesystem::path renamed = root / "renamed";
    std::filesystem::copy(good, renamed, std::filesystem::copy_options::recursive);
    Check(mods::InspectMod(renamed, host, trusting).problem == mods::ModProblem::FolderMismatch, "a folder not named after its mod is refused");

    const std::filesystem::path native = MakeMod(root, ModSpec{ .id = "native", .sealWith = &author });
    {
        std::ifstream input{ native / "Mod.kbmod", std::ios::binary };
        std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
        input.close();
        WriteText(native / "Mod.kbmod", text + "plugin Evil.dll\n");
    }
    const mods::ModInfo nativeInfo = mods::InspectMod(native, host, trusting);
    Check(nativeInfo.problem == mods::ModProblem::ManifestInvalid && nativeInfo.detail.find("native code") != std::string::npos, "a mod that names native code is refused with the reason");
}

// Red when: an install leaves a half-installed mod, replaces without being told to, or loses the
// state file's order; or removing and disabling do not do what they say.
void InstallingMods() {
    const std::filesystem::path root = TestRoot() / "install";
    Purge(root);
    const std::filesystem::path source = root / "source";
    const std::filesystem::path installed = root / "Mods";
    std::filesystem::create_directories(source);
    const kb::security::ReleaseSigningKey author = NewKey();
    const mods::ModTrustPolicy trusting = Trusting(author);
    const mods::ModHost host;
    mods::ModCatalog catalog{ installed };
    mods::ModInfo info;
    std::string error;

    const std::filesystem::path one = MakeMod(source, ModSpec{ .id = "one", .sealWith = &author });
    Check(catalog.Install(one, host, trusting, false, info, error) && info.enabled && info.problem == mods::ModProblem::None, "a good mod installs and is enabled: " + error);
    Check(std::filesystem::exists(installed / "one" / "Mod.kbmod") && std::filesystem::exists(installed / "one" / "Content.kbpack") && !std::filesystem::exists(installed / ".staging"),
        "the installed mod is its two files and staging is gone");
    Check(!catalog.Install(one, host, trusting, false, info, error) && error.find("already installed") != std::string::npos, "the same mod does not install twice");
    Check(catalog.Install(one, host, trusting, true, info, error), "a mod can be replaced when asked: " + error);

    const std::filesystem::path two = MakeMod(source, ModSpec{ .id = "two", .sealWith = &author });
    Check(catalog.Install(two, host, trusting, false, info, error), "a second mod installs");
    Check(catalog.EnabledIds() == std::vector<std::string>({ "one", "two" }), "mods are enabled in install order");

    const std::filesystem::path bad = MakeMod(source, ModSpec{ .id = "bad", .sealWith = &author });
    FlipByte(bad / "Content.kbpack", 40U);
    Check(!catalog.Install(bad, host, trusting, false, info, error) && !std::filesystem::exists(installed / "bad") && !std::filesystem::exists(installed / ".staging"),
        "a damaged mod does not install and leaves nothing behind");
    const std::filesystem::path untrusted = MakeMod(source, ModSpec{ .id = "untrusted" });
    Check(!catalog.Install(untrusted, host, trusting, false, info, error) && error.find("Unsigned") != std::string::npos, "an unsigned mod does not install under the default policy: " + error);

    Check(catalog.SetEnabled("one", false, error) && catalog.EnabledIds() == std::vector<std::string>({ "two" }), "a mod can be disabled");
    mods::ModPlan plan = catalog.Resolve(host, trusting);
    Check(LoadOrder(plan) == std::vector<std::string>({ "two" }) && ProblemOf(plan, "one") == mods::ModProblem::Disabled, "a disabled mod is installed and not loaded");
    Check(catalog.SetEnabled("one", true, error) && catalog.EnabledIds() == std::vector<std::string>({ "two", "one" }), "enabling puts a mod last");
    Check(!catalog.SetEnabled("nothing", true, error), "a mod that is not installed cannot be enabled");
    Check(catalog.Remove("two", error) && !std::filesystem::exists(installed / "two") && catalog.EnabledIds() == std::vector<std::string>({ "one" }), "a mod can be removed");
    Check(!catalog.Remove("../one", error), "a removal cannot name a path");

    mods::ModTrustPolicy policy;
    Check(catalog.WriteTrustPolicy(trusting, error) && catalog.ReadTrustPolicy() == trusting, "the trust policy is kept in the catalogue: " + error);
    WriteText(installed / "Mods.trust", "damaged");
    Check(catalog.ReadTrustPolicy() == policy, "a damaged trust file trusts nobody");
}

// Red when: load order ignores dependencies, a missing, disabled, refused or too old dependency
// lets its dependant load, a conflict is not settled by order, or a cycle loads.
void ResolvingMods() {
    const std::filesystem::path root = TestRoot() / "resolve";
    Purge(root);
    const std::filesystem::path source = root / "source";
    const std::filesystem::path installed = root / "Mods";
    std::filesystem::create_directories(source);
    const kb::security::ReleaseSigningKey author = NewKey();
    mods::ModTrustPolicy trusting = Trusting(author);
    const mods::ModHost host;
    mods::ModCatalog catalog{ installed };
    mods::ModInfo info;
    std::string error;
    const auto install = [&](ModSpec spec) {
        spec.sealWith = &author;
        Check(catalog.Install(MakeMod(source, spec), host, trusting, false, info, error), "installs " + spec.id + ": " + error);
    };

    // Enabled in the order app, lib, base; the dependencies must still come first.
    install(ModSpec{ .id = "app", .needs = { { "lib", ">=1.0.0" } } });
    install(ModSpec{ .id = "lib", .version = { 1, 5, 0 }, .needs = { { "base", "^2.0.0" } } });
    install(ModSpec{ .id = "base", .version = { 2, 1, 0 } });
    mods::ModPlan plan = catalog.Resolve(host, trusting);
    Check(LoadOrder(plan) == std::vector<std::string>({ "base", "lib", "app" }) && plan.refused.empty(), "dependencies load first");

    // Independent mods keep the order they were enabled in.
    install(ModSpec{ .id = "zeta" });
    install(ModSpec{ .id = "alpha" });
    plan = catalog.Resolve(host, trusting);
    Check(LoadOrder(plan) == std::vector<std::string>({ "base", "lib", "app", "zeta", "alpha" }), "independent mods keep their enabled order");

    // Too old a dependency, then a disabled one: the dependants are refused, the others still load.
    install(ModSpec{ .id = "needsnew", .needs = { { "base", ">=3.0.0" } } });
    plan = catalog.Resolve(host, trusting);
    Check(ProblemOf(plan, "needsnew") == mods::ModProblem::DependencyVersion && plan.FindLoad("base") != nullptr, "a dependency in the wrong version is named");
    Check(catalog.SetEnabled("base", false, error), "base is disabled");
    plan = catalog.Resolve(host, trusting);
    Check(ProblemOf(plan, "lib") == mods::ModProblem::DependencyDisabled && ProblemOf(plan, "app") == mods::ModProblem::DependencyRefused &&
            ProblemOf(plan, "needsnew") == mods::ModProblem::DependencyDisabled && plan.FindLoad("zeta") != nullptr && plan.FindLoad("alpha") != nullptr,
        "disabling a mod refuses the mods that need it, through the chain, and no others");
    Check(catalog.SetEnabled("base", true, error), "base is enabled again");

    install(ModSpec{ .id = "lonely", .needs = { { "ghost", "*" } } });
    plan = catalog.Resolve(host, trusting);
    Check(ProblemOf(plan, "lonely") == mods::ModProblem::DependencyMissing, "a dependency that is not installed is named");

    // A conflict is settled by order: the earlier mod loads.
    install(ModSpec{ .id = "rival", .conflicts = { "zeta" } });
    plan = catalog.Resolve(host, trusting);
    Check(ProblemOf(plan, "rival") == mods::ModProblem::Conflict && plan.FindLoad("zeta") != nullptr, "of two conflicting mods the earlier loads");
    Check(catalog.SetEnabled("zeta", false, error) && catalog.SetEnabled("zeta", true, error), "zeta moves to the end");
    plan = catalog.Resolve(host, trusting);
    Check(ProblemOf(plan, "zeta") == mods::ModProblem::Conflict && plan.FindLoad("rival") != nullptr, "re-enabling a mod puts it behind the one it conflicts with");

    // A cycle loads nothing of it.
    install(ModSpec{ .id = "ping", .needs = { { "pong", "*" } } });
    install(ModSpec{ .id = "pong", .needs = { { "ping", "*" } } });
    plan = catalog.Resolve(host, trusting);
    Check(ProblemOf(plan, "ping") == mods::ModProblem::DependencyCycle && ProblemOf(plan, "pong") == mods::ModProblem::DependencyCycle, "required mods in a cycle do not load");

    // A mod the runtime refused is excluded with its reason, and its dependants follow.
    plan = catalog.Resolve(host, trusting, { { "base", "the runtime refused the pack: ManifestDuplicate" } });
    Check(ProblemOf(plan, "base") == mods::ModProblem::MountRefused && ProblemOf(plan, "lib") == mods::ModProblem::DependencyRefused &&
            ProblemOf(plan, "app") == mods::ModProblem::DependencyRefused,
        "an excluded mod and what needs it do not load");

    // A tampered mod is refused at start and the rest load.
    FlipByte(installed / "alpha" / "Content.kbpack", 40U);
    plan = catalog.Resolve(host, trusting);
    Check(ProblemOf(plan, "alpha") == mods::ModProblem::PackRejected && plan.FindLoad("base") != nullptr, "a mod damaged after install is refused and the others still load");
}

} // namespace

void RunModTests() {
    VersionsAndRanges();
    ManifestParsing();
    TrustPolicyFiles();
    InspectingMods();
    InstallingMods();
    ResolvingMods();
    Purge(TestRoot());
}

} // namespace kb::tests
