#include "engine/security/ReleaseManifest.hpp"

#include "engine/assets/bake/AssetPackSeal.hpp"
#include "engine/security/ReleaseKeys.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>
#include <utility>


namespace kb::security {
namespace {

constexpr std::string_view kManifestHeader = "21kb-release-manifest 1";
constexpr std::string_view kSignatureDomain{ "21KB-RELEASE-MANIFEST-V1", 25U };
constexpr std::string_view kStateHeader = "21kb-release-state 1";
constexpr std::size_t kMaxManifestPathBytes = 1024U;

[[nodiscard]] std::string PortablePath(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return { reinterpret_cast<const char*>(text.data()), text.size() };
}

[[nodiscard]] std::filesystem::path NativePath(std::string_view portable) {
    return std::filesystem::path{ std::u8string{ reinterpret_cast<const char8_t*>(portable.data()), portable.size() } };
}

[[nodiscard]] bool IsValidManifestPath(std::string_view path) noexcept {
    if (path.empty() || path.size() > kMaxManifestPathBytes || path.front() == '/') {
        return false;
    }
    std::size_t start = 0U;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string_view component = path.substr(start, end - start);
        if (component.empty() || component == "." || component == "..") {
            return false;
        }
        start = end + 1U;
    }
    return std::ranges::none_of(path, [](char value) {
        return static_cast<unsigned char>(value) < 0x20U || value == '\\' || value == 0x7F;
    });
}

[[nodiscard]] std::span<const std::uint8_t> Bytes(std::string_view text) noexcept {
    return { reinterpret_cast<const std::uint8_t*>(text.data()), text.size() };
}

[[nodiscard]] Sha512Digest SignedMessage(std::string_view body) {
    Sha512Hasher hasher;
    hasher.Update(Bytes(kSignatureDomain));
    hasher.Update(Bytes(body));
    return hasher.Finish();
}

[[nodiscard]] bool ParseUInt64(std::string_view text, std::uint64_t& value) noexcept {
    if (text.empty() || (text.size() > 1U && text.front() == '0')) {
        return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

// Splits off the first space-separated field.
[[nodiscard]] std::string_view TakeField(std::string_view& line) noexcept {
    const std::size_t space = line.find(' ');
    const std::string_view field = line.substr(0U, space);
    line = space == std::string_view::npos ? std::string_view{} : line.substr(space + 1U);
    return field;
}

[[nodiscard]] bool ReadText(const std::filesystem::path& path, std::uintmax_t limit, std::string& out) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > limit) {
        return false;
    }
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{});
    return out.size() == size;
}

[[nodiscard]] ReleaseVerification Fail(ReleaseManifestStatus status, std::string detail) {
    ReleaseVerification result{};
    result.status = status;
    result.detail = std::move(detail);
    return result;
}

[[nodiscard]] ReleaseVerification LoadVerified(
    const std::filesystem::path& root,
    const Ed25519PublicKey& releaseKey,
    std::string_view expectedProductId) {
    std::string text;
    std::error_code error;
    const std::filesystem::path path = root / std::filesystem::path{ kReleaseManifestFileName };
    if (!std::filesystem::is_regular_file(path, error)) {
        return Fail(ReleaseManifestStatus::Missing, std::string{ kReleaseManifestFileName });
    }
    if (!ReadText(path, kMaxReleaseManifestBytes, text)) {
        return Fail(ReleaseManifestStatus::Malformed, std::string{ kReleaseManifestFileName });
    }
    ReleaseVerification result{};
    result.status = ParseAndVerifyReleaseManifest(text, releaseKey, result.manifest);
    if (result.status == ReleaseManifestStatus::Success && result.manifest.productId != expectedProductId) {
        result.status = ReleaseManifestStatus::ProductMismatch;
        result.detail = result.manifest.productId;
    }
    return result;
}

std::mutex& ReleaseMutex() {
    static std::mutex mutex;
    return mutex;
}

std::shared_ptr<const InstalledRelease>& ReleaseSlot() {
    static std::shared_ptr<const InstalledRelease> release;
    return release;
}

} // namespace

const ReleaseManifestFile* ReleaseManifest::FindFile(std::string_view path) const noexcept {
    const auto found = std::ranges::lower_bound(files, path, {}, [](const ReleaseManifestFile& file) -> std::string_view {
        return file.path;
    });
    return found != files.end() && found->path == path ? &*found : nullptr;
}

const ReleaseManifestPackSeal* ReleaseManifest::FindPack(std::string_view path) const noexcept {
    const auto found = std::ranges::find(packs, path, &ReleaseManifestPackSeal::path);
    return found != packs.end() ? &*found : nullptr;
}

std::string_view ToString(ReleaseManifestStatus status) noexcept {
    switch (status) {
    case ReleaseManifestStatus::Success: return "Success";
    case ReleaseManifestStatus::Missing: return "Missing";
    case ReleaseManifestStatus::Malformed: return "Malformed";
    case ReleaseManifestStatus::SignatureInvalid: return "SignatureInvalid";
    case ReleaseManifestStatus::ProductMismatch: return "ProductMismatch";
    case ReleaseManifestStatus::FileMissing: return "FileMissing";
    case ReleaseManifestStatus::FileModified: return "FileModified";
    case ReleaseManifestStatus::UnlistedFile: return "UnlistedFile";
    case ReleaseManifestStatus::RolledBack: return "RolledBack";
    case ReleaseManifestStatus::StateUnavailable: return "StateUnavailable";
    }
    return "Unknown";
}

bool IsValidContentVersion(std::string_view version) noexcept {
    return !version.empty() && version.size() <= 64U && std::ranges::all_of(version, [](char value) {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
            value == '.' || value == '_' || value == '+' || value == '-';
    });
}

bool IsCriticalReleaseFile(const std::filesystem::path& relativePath) {
    std::string extension = PortablePath(relativePath.extension());
    std::ranges::transform(extension, extension.begin(), [](char value) {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
    });
    return extension == ".exe" || extension == ".dll" || extension == ".so" || extension == ".dylib" ||
        extension == ".kbpack";
}

bool HashFileSha512(const std::filesystem::path& path, Sha512Digest& digest, std::uint64_t& size) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        return false;
    }
    Sha512Hasher hasher;
    std::vector<std::uint8_t> buffer(1U << 20U);
    size = 0U;
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = input.gcount();
        if (read <= 0) {
            break;
        }
        hasher.Update(std::span{ buffer.data(), static_cast<std::size_t>(read) });
        size += static_cast<std::uint64_t>(read);
    }
    if (input.bad()) {
        return false;
    }
    digest = hasher.Finish();
    return true;
}

bool BuildReleaseManifest(const std::filesystem::path& root, ReleaseManifest& manifest, std::string& error) {
    manifest.files.clear();
    manifest.packs.clear();
    std::error_code iterationError;
    for (std::filesystem::recursive_directory_iterator iterator{ root, iterationError }, end;
         !iterationError && iterator != end; iterator.increment(iterationError)) {
        const std::filesystem::directory_entry& entry = *iterator;
        std::error_code statusError;
        if (entry.is_symlink(statusError)) {
            error = "release contains a symbolic link: " + PortablePath(entry.path());
            return false;
        }
        if (entry.is_directory(statusError)) {
            continue;
        }
        const std::string relative = PortablePath(entry.path().lexically_relative(root));
        if (relative == kReleaseManifestFileName) {
            continue;
        }
        if (!entry.is_regular_file(statusError) || !IsValidManifestPath(relative) ||
            relative.find(' ') == 0U) {
            error = "release contains a file that cannot be listed: " + relative;
            return false;
        }
        ReleaseManifestFile file{};
        file.path = relative;
        if (!HashFileSha512(entry.path(), file.sha512, file.size)) {
            error = "release file could not be hashed: " + relative;
            return false;
        }
        if (entry.path().extension() == kb::assets::bake::kAssetPackFileExtension) {
            ReleaseManifestPackSeal pack{};
            pack.path = relative;
            if (!kb::assets::bake::ReadAssetPackSealDigest(entry.path(), pack.sealDigest, error)) {
                error = relative + ": " + error;
                return false;
            }
            manifest.packs.push_back(std::move(pack));
        }
        manifest.files.push_back(std::move(file));
    }
    if (iterationError) {
        error = "release directory could not be listed: " + iterationError.message();
        return false;
    }
    std::ranges::sort(manifest.files, {}, &ReleaseManifestFile::path);
    std::ranges::sort(manifest.packs, {}, &ReleaseManifestPackSeal::path);
    return true;
}

std::string SignReleaseManifest(ReleaseManifest& manifest, const ReleaseSigningKey& key) {
    std::string body{ kManifestHeader };
    body += "\nproduct " + manifest.productId;
    body += "\ncontent-version " + manifest.contentVersion;
    body += "\nrelease " + std::to_string(manifest.releaseNumber);
    body += manifest.antiRollback ? "\nanti-rollback 1" : "\nanti-rollback 0";
    for (const ReleaseManifestFile& file : manifest.files) {
        body += "\nfile " + ToHex(file.sha512) + ' ' + std::to_string(file.size) + ' ' + file.path;
    }
    for (const ReleaseManifestPackSeal& pack : manifest.packs) {
        body += "\nseal " + ToHex(pack.sealDigest) + ' ' + pack.path;
    }
    body += '\n';
    manifest.signature = Ed25519Sign(key.secretKey, SignedMessage(body));
    return body + "signature " + ToHex(manifest.signature) + '\n';
}

ReleaseManifestStatus ParseAndVerifyReleaseManifest(
    std::string_view text,
    const Ed25519PublicKey& releaseKey,
    ReleaseManifest& out) {
    // The signature line is last; everything before it is the signed body.
    constexpr std::string_view kSignaturePrefix = "signature ";
    const std::size_t signatureLine = text.rfind("\nsignature ");
    if (signatureLine == std::string_view::npos || text.size() > kMaxReleaseManifestBytes) {
        return ReleaseManifestStatus::Malformed;
    }
    const std::string_view body = text.substr(0U, signatureLine + 1U);
    std::string_view signatureText = text.substr(signatureLine + 1U + kSignaturePrefix.size());
    if (!signatureText.ends_with('\n')) {
        return ReleaseManifestStatus::Malformed;
    }
    signatureText.remove_suffix(1U);
    ReleaseManifest manifest{};
    if (!TryParseHex(signatureText, manifest.signature)) {
        return ReleaseManifestStatus::Malformed;
    }
    if (!Ed25519Verify(manifest.signature, releaseKey, SignedMessage(body))) {
        return ReleaseManifestStatus::SignatureInvalid;
    }

    // Only a body the release key vouches for is parsed, and it is still parsed strictly.
    std::vector<std::string_view> lines;
    for (std::string_view rest = body; !rest.empty();) {
        const std::size_t end = rest.find('\n');
        lines.push_back(rest.substr(0U, end));
        rest.remove_prefix(end + 1U);
    }
    if (lines.size() < 5U || lines[0] != kManifestHeader) {
        return ReleaseManifestStatus::Malformed;
    }
    const auto field = [](std::string_view line, std::string_view name, std::string_view& value) {
        if (!line.starts_with(name) || line.size() <= name.size() || line[name.size()] != ' ') {
            return false;
        }
        value = line.substr(name.size() + 1U);
        return true;
    };
    std::string_view value;
    std::string_view antiRollback;
    std::string_view release;
    if (!field(lines[1], "product", value) || !IsValidProductId(value)) {
        return ReleaseManifestStatus::Malformed;
    }
    manifest.productId = value;
    if (!field(lines[2], "content-version", value) || !IsValidContentVersion(value)) {
        return ReleaseManifestStatus::Malformed;
    }
    manifest.contentVersion = value;
    if (!field(lines[3], "release", release) || !ParseUInt64(release, manifest.releaseNumber) ||
        !field(lines[4], "anti-rollback", antiRollback) || (antiRollback != "0" && antiRollback != "1")) {
        return ReleaseManifestStatus::Malformed;
    }
    manifest.antiRollback = antiRollback == "1";
    for (std::size_t index = 5U; index < lines.size(); ++index) {
        std::string_view line = lines[index];
        const std::string_view kind = TakeField(line);
        if (kind == "file" && manifest.packs.empty()) {
            ReleaseManifestFile file{};
            const std::string_view digest = TakeField(line);
            const std::string_view size = TakeField(line);
            if (!TryParseHex(digest, file.sha512) || !ParseUInt64(size, file.size) || !IsValidManifestPath(line) ||
                (!manifest.files.empty() && manifest.files.back().path >= line)) {
                return ReleaseManifestStatus::Malformed;
            }
            file.path = line;
            manifest.files.push_back(std::move(file));
        } else if (kind == "seal") {
            ReleaseManifestPackSeal pack{};
            const std::string_view digest = TakeField(line);
            if (!TryParseHex(digest, pack.sealDigest) || manifest.FindFile(line) == nullptr ||
                (!manifest.packs.empty() && manifest.packs.back().path >= line)) {
                return ReleaseManifestStatus::Malformed;
            }
            pack.path = line;
            manifest.packs.push_back(std::move(pack));
        } else {
            return ReleaseManifestStatus::Malformed;
        }
    }
    out = std::move(manifest);
    return ReleaseManifestStatus::Success;
}

ReleaseVerification VerifyReleaseDirectory(
    const std::filesystem::path& root,
    const Ed25519PublicKey& releaseKey,
    std::string_view expectedProductId) {
    ReleaseVerification result = LoadVerified(root, releaseKey, expectedProductId);
    if (result.status != ReleaseManifestStatus::Success) {
        return result;
    }
    ReleaseManifest present{};
    std::string error;
    if (!BuildReleaseManifest(root, present, error)) {
        return Fail(ReleaseManifestStatus::UnlistedFile, error);
    }
    for (const ReleaseManifestFile& file : present.files) {
        const ReleaseManifestFile* listed = result.manifest.FindFile(file.path);
        if (listed == nullptr && !IsCriticalReleaseFile(NativePath(file.path))) {
            result.uncovered.push_back(file.path);
            continue;
        }
        if (listed == nullptr) {
            return Fail(ReleaseManifestStatus::UnlistedFile, file.path);
        }
        if (listed->size != file.size || !ConstantTimeEqual(listed->sha512, file.sha512)) {
            return Fail(ReleaseManifestStatus::FileModified, file.path);
        }
    }
    for (const ReleaseManifestFile& file : result.manifest.files) {
        if (present.FindFile(file.path) == nullptr) {
            return Fail(ReleaseManifestStatus::FileMissing, file.path);
        }
    }
    for (const ReleaseManifestPackSeal& pack : result.manifest.packs) {
        const ReleaseManifestPackSeal* found = present.FindPack(pack.path);
        if (found == nullptr || !ConstantTimeEqual(found->sealDigest, pack.sealDigest)) {
            return Fail(ReleaseManifestStatus::FileModified, pack.path);
        }
    }
    return result;
}

ReleaseVerification VerifyInstalledRelease(
    const std::filesystem::path& root,
    const Ed25519PublicKey& releaseKey,
    std::string_view expectedProductId,
    std::span<const std::filesystem::path> hashNow) {
    ReleaseVerification result = LoadVerified(root, releaseKey, expectedProductId);
    if (result.status != ReleaseManifestStatus::Success) {
        return result;
    }
    std::error_code iterationError;
    for (std::filesystem::recursive_directory_iterator iterator{ root, iterationError }, end;
         !iterationError && iterator != end; iterator.increment(iterationError)) {
        std::error_code statusError;
        if (iterator->is_directory(statusError) && !iterator->is_symlink(statusError)) {
            continue;
        }
        const std::filesystem::path relative = iterator->path().lexically_relative(root);
        if (IsCriticalReleaseFile(relative) && result.manifest.FindFile(PortablePath(relative)) == nullptr) {
            return Fail(ReleaseManifestStatus::UnlistedFile, PortablePath(relative));
        }
    }
    if (iterationError) {
        return Fail(ReleaseManifestStatus::FileMissing, "release directory could not be listed");
    }
    for (const ReleaseManifestFile& file : result.manifest.files) {
        if (!IsCriticalReleaseFile(NativePath(file.path))) {
            continue;
        }
        std::error_code sizeError;
        const std::filesystem::path path = root / NativePath(file.path);
        const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
        if (sizeError || !std::filesystem::is_regular_file(std::filesystem::symlink_status(path, sizeError))) {
            return Fail(ReleaseManifestStatus::FileMissing, file.path);
        }
        if (size != file.size) {
            return Fail(ReleaseManifestStatus::FileModified, file.path);
        }
    }
    for (const std::filesystem::path& path : hashNow) {
        const std::string relative = PortablePath(path.lexically_relative(root));
        const ReleaseManifestFile* listed = result.manifest.FindFile(relative);
        Sha512Digest digest{};
        std::uint64_t size = 0U;
        if (listed == nullptr) {
            return Fail(ReleaseManifestStatus::UnlistedFile, relative);
        }
        if (!HashFileSha512(path, digest, size) || size != listed->size || !ConstantTimeEqual(digest, listed->sha512)) {
            return Fail(ReleaseManifestStatus::FileModified, relative);
        }
    }
    return result;
}

ReleaseManifestStatus EnforceReleaseAntiRollback(const std::filesystem::path& stateRoot, const ReleaseManifest& manifest) {
    if (stateRoot.empty()) {
        return ReleaseManifestStatus::StateUnavailable;
    }
    const std::filesystem::path path = stateRoot / "release.state";
    std::uint64_t highest = 0U;
    std::string text;
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        std::string_view rest;
        if (ReadText(path, 4096U, text)) {
            rest = text;
        }
        const std::string expected = std::string{ kStateHeader } + "\nproduct " + manifest.productId + "\nhighest ";
        if (!rest.starts_with(expected) || !rest.ends_with('\n') ||
            !ParseUInt64(rest.substr(expected.size(), rest.size() - expected.size() - 1U), highest)) {
            return ReleaseManifestStatus::StateUnavailable;
        }
    }
    if (manifest.releaseNumber < highest) {
        return ReleaseManifestStatus::RolledBack;
    }
    if (manifest.releaseNumber == highest && !text.empty()) {
        return ReleaseManifestStatus::Success;
    }
    std::filesystem::create_directories(stateRoot, error);
    const std::filesystem::path temporary = stateRoot / "release.state.tmp";
    {
        std::ofstream output{ temporary, std::ios::binary | std::ios::trunc };
        output << kStateHeader << "\nproduct " << manifest.productId << "\nhighest " << manifest.releaseNumber << '\n';
        if (!output.good()) {
            return ReleaseManifestStatus::StateUnavailable;
        }
    }
    std::filesystem::rename(temporary, path, error);
    return error ? ReleaseManifestStatus::StateUnavailable : ReleaseManifestStatus::Success;
}

void InstallVerifiedRelease(std::shared_ptr<const InstalledRelease> release) {
    const std::scoped_lock lock{ ReleaseMutex() };
    ReleaseSlot() = std::move(release);
}

std::shared_ptr<const InstalledRelease> CurrentVerifiedRelease() {
    const std::scoped_lock lock{ ReleaseMutex() };
    return ReleaseSlot();
}

} // namespace kb::security
