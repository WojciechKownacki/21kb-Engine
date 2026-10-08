#include "CliCommands.hpp"

#include "engine/assets/bake/AssetPackReader.hpp"
#include "engine/assets/bake/AssetPackSeal.hpp"
#include "engine/security/Crypto.hpp"
#include "engine/security/ReleaseKeys.hpp"
#include "engine/security/ReleaseManifest.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace kb::cli {
namespace {

constexpr std::uintmax_t kMaxKeyFileBytes = 64U * 1024U;

// A player executable of a release: a Windows .exe, or a Linux ELF player (which has no extension).
[[nodiscard]] bool IsPlayerExecutable(const std::filesystem::directory_entry& entry) {
    if (entry.path().extension() == ".exe") {
        return true;
    }
    std::error_code error;
    if (!entry.is_regular_file(error) || error) {
        return false;
    }
    std::ifstream input{ entry.path(), std::ios::binary };
    char magic[4]{};
    input.read(magic, sizeof(magic));
    return input.gcount() == 4 && magic[0] == '\x7F' && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
}

[[nodiscard]] bool ReadSmallFile(const std::filesystem::path& path, std::string& out, std::string& error) {
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
    if (sizeError || size > kMaxKeyFileBytes) {
        error = "could not read " + path.string();
        return false;
    }
    std::ifstream input{ path, std::ios::binary };
    out.assign(std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{});
    if (!input.good() && !input.eof()) {
        error = "could not read " + path.string();
        return false;
    }
    return true;
}

// Writes a file that must not exist yet, readable by its owner only where the platform allows.
[[nodiscard]] bool WriteNewFile(const std::filesystem::path& path, std::span<const std::uint8_t> bytes, std::string& error) {
    std::error_code existsError;
    if (std::filesystem::exists(path, existsError)) {
        error = "refusing to overwrite an existing file: " + path.string();
        return false;
    }
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), existsError);
    }
    {
        std::ofstream output{ path, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output.good()) {
            error = "could not write " + path.string();
            return false;
        }
    }
#if !defined(_WIN32)
    std::error_code permissionError;
    std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, permissionError);
#endif
    return true;
}

[[nodiscard]] bool WriteNewTextFile(const std::filesystem::path& path, const std::string& text, std::string& error) {
    return WriteNewFile(path, std::span{ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() }, error);
}

[[nodiscard]] bool LoadSigningKey(const ArgumentList& arguments, kb::security::ReleaseSigningKey& key, std::string& error) {
    const std::optional<std::string> path = arguments.Option("--key");
    if (!path.has_value()) {
        error = "--key <release signing key file> is required";
        return false;
    }
    std::string text;
    const bool decoded = ReadSmallFile(*path, text, error) && kb::security::DecodeReleaseSigningKey(text, key, error);
    kb::security::SecureWipe(std::span{ reinterpret_cast<std::uint8_t*>(text.data()), text.size() });
    return decoded;
}

[[nodiscard]] bool LoadContentKey(const ArgumentList& arguments, std::optional<kb::security::AeadKey>& key, std::string& error) {
    const std::optional<std::string> path = arguments.Option("--content-key");
    if (!path.has_value()) {
        return true;
    }
    std::string text;
    key.emplace();
    const bool decoded = ReadSmallFile(*path, text, error) && kb::security::DecodePackContentKey(text, *key, error);
    kb::security::SecureWipe(std::span{ reinterpret_cast<std::uint8_t*>(text.data()), text.size() });
    if (!decoded) {
        key.reset();
    }
    return decoded;
}

[[nodiscard]] bool LoadPublicKey(const ArgumentList& arguments, std::optional<kb::security::Ed25519PublicKey>& key, std::string& error) {
    const std::optional<std::string> text = arguments.Option("--public-key");
    if (!text.has_value()) {
        return true;
    }
    key.emplace();
    if (!kb::security::TryParseHex(*text, *key)) {
        error = "--public-key expects 64 hexadecimal digits";
        return false;
    }
    return true;
}

[[nodiscard]] int Fail(CommandIo io, const std::string& message) {
    io.err << "error: " << message << '\n';
    return 1;
}

int RunKeysGenerate(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> out = arguments.Option("--out");
    if (!out.has_value()) {
        return Fail(io, "keys generate expects --out <key file>");
    }
    const std::filesystem::path path{ *out };
    if (kb::security::IsInsideProjectOrRepository(path)) {
        return Fail(io, "refusing to write a private key inside a project or a repository: " + path.string());
    }
    kb::security::ReleaseSigningKey key;
    if (!kb::security::GenerateReleaseSigningKey(key)) {
        return Fail(io, "the system random number generator is unavailable");
    }
    std::string text = kb::security::EncodeReleaseSigningKey(key);
    std::string error;
    const bool written = WriteNewTextFile(path, text, error);
    kb::security::SecureWipe(std::span{ reinterpret_cast<std::uint8_t*>(text.data()), text.size() });
    if (!written) {
        return Fail(io, error);
    }
    io.out << kb::security::ToHex(key.publicKey) << '\n';
    return 0;
}

int RunKeysPublic(const ArgumentList& arguments, CommandIo io) {
    kb::security::ReleaseSigningKey key;
    std::string error;
    if (!LoadSigningKey(arguments, key, error)) {
        return Fail(io, error);
    }
    io.out << kb::security::ToHex(key.publicKey) << '\n';
    return 0;
}

int RunKeysContentKey(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> out = arguments.Option("--out");
    if (!out.has_value()) {
        return Fail(io, "keys content-key expects --out <file>");
    }
    kb::security::AeadKey key;
    if (!kb::security::SecureRandom(key.Span())) {
        return Fail(io, "the system random number generator is unavailable");
    }
    std::string text = kb::security::EncodePackContentKey(key);
    std::string error;
    const bool written = WriteNewTextFile(*out, text, error);
    kb::security::SecureWipe(std::span{ reinterpret_cast<std::uint8_t*>(text.data()), text.size() });
    return written ? 0 : Fail(io, error);
}

int RunKeysAnchor(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> out = arguments.Option("--out");
    const std::optional<std::string> product = arguments.Option("--product");
    if (!out.has_value() || !product.has_value()) {
        return Fail(io, "keys anchor expects --key <key file> --product <id> [--content-key <file>] --out <file>");
    }
    if (!kb::security::IsValidProductId(*product)) {
        return Fail(io, "product id must be 1-128 characters of [A-Za-z0-9._-] starting with a letter or digit");
    }
    kb::security::ReleaseSigningKey key;
    std::string error;
    if (!LoadSigningKey(arguments, key, error)) {
        return Fail(io, error);
    }
    kb::security::TrustAnchor anchor{};
    anchor.productId = *product;
    anchor.releaseKey = key.publicKey;
    anchor.saveSecret = kb::security::DeriveGameSaveSecret(key, anchor.productId);
    if (!LoadContentKey(arguments, anchor.packContentKey, error)) {
        return Fail(io, error);
    }
    std::vector<std::uint8_t> bytes = kb::security::EncodeTrustAnchor(anchor);
    const bool written = WriteNewFile(*out, bytes, error);
    kb::security::SecureWipe(bytes);
    return written ? 0 : Fail(io, error);
}

int RunPackSign(const ArgumentList& arguments, CommandIo io) {
    if (arguments.Positionals().size() != 2U) {
        return Fail(io, "pack sign expects --key <key file> [--content-key <file>] <pack.kbpack>");
    }
    kb::security::ReleaseSigningKey key;
    std::optional<kb::security::AeadKey> contentKey;
    std::string error;
    if (!LoadSigningKey(arguments, key, error) || !LoadContentKey(arguments, contentKey, error)) {
        return Fail(io, error);
    }
    const std::filesystem::path pack{ arguments.Positionals()[1] };
    if (!kb::assets::bake::SealAssetPack(pack, key, contentKey.has_value() ? &*contentKey : nullptr, error)) {
        return Fail(io, error);
    }
    io.out << "sealed " << pack.string() << " by " << kb::security::ToHex(key.publicKey)
           << (contentKey.has_value() ? " (encrypted)" : "") << '\n';
    return 0;
}

int RunPackVerify(const ArgumentList& arguments, CommandIo io) {
    if (arguments.Positionals().size() != 2U) {
        return Fail(io, "pack verify expects [--anchor <file> | --public-key <hex> [--content-key <file>]] <pack.kbpack>");
    }
    kb::assets::bake::AssetPackTrust trust{};
    std::string error;
    if (const std::optional<std::string> anchorPath = arguments.Option("--anchor"); anchorPath.has_value()) {
        std::string bytes;
        kb::security::TrustAnchor anchor{};
        if (!ReadSmallFile(*anchorPath, bytes, error) ||
            !kb::security::DecodeTrustAnchor(
                std::span{ reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size() }, anchor, error)) {
            return Fail(io, error);
        }
        trust.requiredSigner = anchor.releaseKey;
        trust.contentKey = std::move(anchor.packContentKey);
    } else if (!LoadPublicKey(arguments, trust.requiredSigner, error) || !LoadContentKey(arguments, trust.contentKey, error)) {
        return Fail(io, error);
    }
    const std::filesystem::path pack{ arguments.Positionals()[1] };
    kb::assets::bake::AssetPackReader reader;
    if (const auto status = reader.Mount(pack, kb::assets::bake::AssetPackAccess::Ranged, trust);
        status != kb::assets::bake::AssetPackReadStatus::Success) {
        return Fail(io, "pack refused: " + std::string{ kb::assets::bake::ToString(status) });
    }
    if (reader.Seal() == nullptr) {
        return Fail(io, "pack is not signed");
    }
    std::size_t blockCount = 0U;
    std::vector<std::uint8_t> bytes;
    for (const kb::assets::bake::AssetPackArtifactEntry& artifact : reader.Artifacts()) {
        for (const kb::assets::bake::AssetPackBlockEntry& block : artifact.blocks) {
            if (const auto status = reader.ReadBlock(artifact, block.name, bytes);
                status != kb::assets::bake::AssetPackReadStatus::Success) {
                return Fail(io, "block " + artifact.key.ToString() + "/" + block.name + " refused: " +
                        std::string{ kb::assets::bake::ToString(status) });
            }
            ++blockCount;
        }
    }
    io.out << "OK " << pack.string() << ": " << blockCount << " blocks sealed by "
           << kb::security::ToHex(reader.Seal()->signer) << (reader.Seal()->encrypted ? " (encrypted)" : "")
           << (trust.requiredSigner.has_value() ? "" : " (signer not checked against an expected key)") << '\n';
    return 0;
}

int RunReleaseSign(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> directory = arguments.Option("--dir");
    const std::optional<std::string> product = arguments.Option("--product");
    const std::optional<std::string> version = arguments.Option("--content-version");
    const std::optional<std::string> release = arguments.Option("--release");
    if (!directory.has_value() || !product.has_value() || !version.has_value() || !release.has_value()) {
        return Fail(io, "release sign expects --key <key file> --dir <release directory> --product <id> "
                        "--content-version <version> --release <number> [--anti-rollback]");
    }
    kb::security::ReleaseManifest manifest{};
    manifest.productId = *product;
    manifest.contentVersion = *version;
    manifest.antiRollback = arguments.Flag("--anti-rollback");
    const auto [end, parseError] = std::from_chars(release->data(), release->data() + release->size(), manifest.releaseNumber);
    if (parseError != std::errc{} || end != release->data() + release->size()) {
        return Fail(io, "--release expects a non-negative whole number");
    }
    if (!kb::security::IsValidProductId(manifest.productId) || !kb::security::IsValidContentVersion(manifest.contentVersion)) {
        return Fail(io, "product id or content version contains unsupported characters");
    }
    kb::security::ReleaseSigningKey key;
    std::string error;
    const std::filesystem::path root{ *directory };
    if (!LoadSigningKey(arguments, key, error) || !kb::security::BuildReleaseManifest(root, manifest, error)) {
        return Fail(io, error);
    }
    const std::string text = kb::security::SignReleaseManifest(manifest, key);
    const std::filesystem::path out = root / std::filesystem::path{ kb::security::kReleaseManifestFileName };
    if (!WriteNewTextFile(out, text, error)) {
        return Fail(io, error);
    }
    io.out << "signed " << manifest.files.size() << " files of " << manifest.productId << ' ' << manifest.contentVersion
           << " release " << manifest.releaseNumber << '\n';
    return 0;
}

int RunReleaseVerify(const ArgumentList& arguments, CommandIo io) {
    if (arguments.Positionals().size() != 2U) {
        return Fail(io, "release verify expects [--anchor <file>] <release directory>");
    }
    const std::filesystem::path root{ arguments.Positionals()[1] };
    kb::security::TrustAnchor anchor{};
    std::string error;
    if (const std::optional<std::string> anchorPath = arguments.Option("--anchor"); anchorPath.has_value()) {
        std::string bytes;
        if (!ReadSmallFile(*anchorPath, bytes, error) ||
            !kb::security::DecodeTrustAnchor(
                std::span{ reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size() }, anchor, error)) {
            return Fail(io, error);
        }
    } else {
        // The release key is the one the shipped player carries, not one the caller supplies.
        bool found = false;
        std::error_code iterationError;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{ root, iterationError }) {
            if (!IsPlayerExecutable(entry)) {
                continue;
            }
            kb::security::TrustAnchorLookup lookup = kb::security::ReadTrustAnchorFromExecutable(entry.path());
            if (lookup.state == kb::security::TrustAnchorLookup::State::Invalid) {
                return Fail(io, entry.path().filename().string() + ": " + lookup.error);
            }
            if (lookup.state == kb::security::TrustAnchorLookup::State::Present) {
                anchor = std::move(lookup.anchor);
                found = true;
                break;
            }
        }
        if (!found) {
            return Fail(io, "no player in the release carries a trust anchor; pass --anchor <file>");
        }
    }
    const kb::security::ReleaseVerification verification =
        kb::security::VerifyReleaseDirectory(root, anchor.releaseKey, anchor.productId);
    if (verification.status != kb::security::ReleaseManifestStatus::Success) {
        return Fail(io, "release refused: " + std::string{ kb::security::ToString(verification.status) } +
                (verification.detail.empty() ? std::string{} : " (" + verification.detail + ")"));
    }
    for (const std::string& file : verification.uncovered) {
        io.out << "note: not covered by the release manifest: " << file << '\n';
    }
    io.out << "OK " << verification.manifest.productId << ' ' << verification.manifest.contentVersion << " release "
           << verification.manifest.releaseNumber << ": " << verification.manifest.files.size() << " files verified\n";
    return 0;
}

} // namespace

int RunReleaseCommand(const ArgumentList& arguments, CommandIo io) {
    const std::string action = arguments.Positionals().empty() ? std::string{} : arguments.Positionals().front();
    if (action == "sign") {
        return RunReleaseSign(arguments, io);
    }
    if (action == "verify") {
        return RunReleaseVerify(arguments, io);
    }
    return Fail(io, "release expects sign or verify");
}

int RunKeysCommand(const ArgumentList& arguments, CommandIo io) {
    const std::string action = arguments.Positionals().empty() ? std::string{} : arguments.Positionals().front();
    if (action == "generate") {
        return RunKeysGenerate(arguments, io);
    }
    if (action == "public") {
        return RunKeysPublic(arguments, io);
    }
    if (action == "content-key") {
        return RunKeysContentKey(arguments, io);
    }
    if (action == "anchor") {
        return RunKeysAnchor(arguments, io);
    }
    return Fail(io, "keys expects generate, public, content-key or anchor");
}

int RunPackCommand(const ArgumentList& arguments, CommandIo io) {
    const std::string action = arguments.Positionals().empty() ? std::string{} : arguments.Positionals().front();
    if (action == "sign") {
        return RunPackSign(arguments, io);
    }
    if (action == "verify") {
        return RunPackVerify(arguments, io);
    }
    return Fail(io, "pack expects sign or verify");
}

} // namespace kb::cli
