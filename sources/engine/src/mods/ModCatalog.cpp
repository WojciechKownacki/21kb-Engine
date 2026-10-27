#include "engine/mods/ModCatalog.hpp"

#include "engine/assets/bake/AssetPackReader.hpp"
#include "engine/assets/bake/AssetPackSeal.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>
#include <utility>

namespace kb::mods {
namespace {

namespace bake = kb::assets::bake;

constexpr std::uintmax_t kMaxStateFileBytes = 64U * 1024U;

[[nodiscard]] std::string ReadSmallFile(const std::filesystem::path& path, std::uintmax_t limit) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > limit) {
        return {};
    }
    std::ifstream input{ path, std::ios::binary };
    return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

// Replaces the file only once the new one is complete.
[[nodiscard]] bool WriteFileAtomically(const std::filesystem::path& path, const std::string& text, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path temp = path.string() + ".tmp";
    {
        std::ofstream output{ temp, std::ios::binary | std::ios::trunc };
        output << text;
        if (!output) {
            error = "could not write " + path.string();
            return false;
        }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        error = "could not replace " + path.string();
        return false;
    }
    return true;
}

[[nodiscard]] bool HashFile(const std::filesystem::path& path, kb::security::Sha512Digest& digest) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) {
        return false;
    }
    kb::security::Sha512Hasher hasher;
    std::vector<std::uint8_t> buffer(1U << 20U);
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            hasher.Update(std::span{ buffer.data(), static_cast<std::size_t>(count) });
        }
    }
    if (!input.eof()) {
        return false;
    }
    digest = hasher.Finish();
    return true;
}

[[nodiscard]] bool IsRegularFile(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
    return !error && status.type() == std::filesystem::file_type::regular;
}

[[nodiscard]] bool IsRealDirectory(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
    return !error && status.type() == std::filesystem::file_type::directory;
}

void Refuse(ModInfo& info, ModProblem problem, std::string detail) {
    info.problem = problem;
    info.detail = std::move(detail);
}

[[nodiscard]] std::vector<std::string> ParseEnabledState(std::string_view text) {
    std::vector<std::string> ids;
    bool header = false;
    std::size_t position = 0U;
    while (position <= text.size()) {
        const std::size_t end = std::min(text.find('\n', position), text.size());
        std::string_view line = text.substr(position, end - position);
        position = end + 1U;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.remove_suffix(1U);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (!header) {
            if (line != "21kb-mod-state 1") {
                return {};
            }
            header = true;
            continue;
        }
        constexpr std::string_view prefix = "enabled ";
        if (!line.starts_with(prefix)) {
            continue;
        }
        const std::string id{ line.substr(prefix.size()) };
        if (IsValidModId(id) && std::ranges::find(ids, id) == ids.end() && ids.size() < kMaxInstalledMods) {
            ids.push_back(id);
        }
    }
    return ids;
}

} // namespace

bool ModTrustPolicy::Trusts(const kb::security::Ed25519PublicKey& key) const noexcept {
    return std::ranges::find(signers, key) != signers.end();
}

bool ParseModTrustPolicy(std::string_view text, ModTrustPolicy& out, std::string& error) {
    ModTrustPolicy policy{};
    bool header = false;
    std::uint32_t lineNumber = 0U;
    std::size_t position = 0U;
    while (position <= text.size()) {
        const std::size_t end = std::min(text.find('\n', position), text.size());
        std::string_view line = text.substr(position, end - position);
        position = end + 1U;
        ++lineNumber;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.remove_suffix(1U);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::string where = "line " + std::to_string(lineNumber) + ": ";
        if (!header) {
            if (line != "21kb-mod-trust 1") {
                error = where + "the first line must be \"21kb-mod-trust 1\"";
                return false;
            }
            header = true;
            continue;
        }
        if (line == "allow-unsigned") {
            policy.allowUnsigned = true;
        } else if (line.starts_with("signer ")) {
            std::string_view rest = line.substr(7U);
            const std::string_view hex = rest.substr(0U, rest.find(' '));
            kb::security::Ed25519PublicKey key{};
            if (hex.size() != key.size() * 2U || !kb::security::TryParseHex(hex, key)) {
                error = where + "signer expects 64 hexadecimal digits";
                return false;
            }
            if (!policy.Trusts(key)) {
                policy.signers.push_back(key);
            }
        } else {
            error = where + "unknown line";
            return false;
        }
    }
    if (!header) {
        error = "empty";
        return false;
    }
    out = std::move(policy);
    return true;
}

std::string EncodeModTrustPolicy(const ModTrustPolicy& policy) {
    std::string text = "21kb-mod-trust 1\n";
    if (policy.allowUnsigned) {
        text += "allow-unsigned\n";
    }
    for (const kb::security::Ed25519PublicKey& key : policy.signers) {
        text += "signer " + kb::security::ToHex(key) + '\n';
    }
    return text;
}

std::string_view ToString(ModProblem problem) noexcept {
    switch (problem) {
    case ModProblem::None: return "None";
    case ModProblem::ManifestInvalid: return "ManifestInvalid";
    case ModProblem::FolderMismatch: return "FolderMismatch";
    case ModProblem::PackMissing: return "PackMissing";
    case ModProblem::PackHashMismatch: return "PackHashMismatch";
    case ModProblem::PackRejected: return "PackRejected";
    case ModProblem::PackNotAModChunk: return "PackNotAModChunk";
    case ModProblem::SignerMismatch: return "SignerMismatch";
    case ModProblem::Unsigned: return "Unsigned";
    case ModProblem::UntrustedSigner: return "UntrustedSigner";
    case ModProblem::ApiIncompatible: return "ApiIncompatible";
    case ModProblem::BaseMismatch: return "BaseMismatch";
    case ModProblem::DependencyMissing: return "DependencyMissing";
    case ModProblem::DependencyDisabled: return "DependencyDisabled";
    case ModProblem::DependencyVersion: return "DependencyVersion";
    case ModProblem::DependencyRefused: return "DependencyRefused";
    case ModProblem::DependencyCycle: return "DependencyCycle";
    case ModProblem::Conflict: return "Conflict";
    case ModProblem::MountRefused: return "MountRefused";
    case ModProblem::Disabled: return "Disabled";
    }
    return "Unknown";
}

std::string ModInfo::Describe() const {
    std::string text = manifest.id + ' ' + ToString(manifest.version);
    if (problem != ModProblem::None) {
        text += ": ";
        text += ToString(problem);
        if (!detail.empty()) {
            text += " (" + detail + ")";
        }
    }
    return text;
}

const ModInfo* ModPlan::FindLoad(std::string_view id) const noexcept {
    const auto found = std::ranges::find(load, id, [](const ModInfo& info) -> const std::string& { return info.manifest.id; });
    return found == load.end() ? nullptr : &*found;
}

ModInfo InspectMod(const std::filesystem::path& directory, const ModHost& host, const ModTrustPolicy& policy) {
    ModInfo info{};
    info.directory = directory;
    info.manifest.id = directory.filename().string();
    const ModManifestResult read = ReadModManifest(directory / std::string{ kModManifestFileName }, info.manifest);
    if (!read.Succeeded()) {
        info.manifest = ModManifest{};
        info.manifest.id = directory.filename().string();
        Refuse(info, ModProblem::ManifestInvalid, read.Describe());
        return info;
    }
    const ModManifest& manifest = info.manifest;
    if (directory.filename().string() != manifest.id) {
        Refuse(info, ModProblem::FolderMismatch, "the folder is " + directory.filename().string() + " but the mod is " + manifest.id);
        return info;
    }
    info.packPath = directory / manifest.pack;
    if (!IsRegularFile(info.packPath)) {
        Refuse(info, ModProblem::PackMissing, manifest.pack);
        return info;
    }
    bake::AssetPackReader reader;
    if (const bake::AssetPackReadStatus status = reader.Mount(info.packPath); status != bake::AssetPackReadStatus::Success) {
        Refuse(info, ModProblem::PackRejected, std::string{ bake::ToString(status) });
        return info;
    }
    const bake::AssetPackHeader& header = reader.Header();
    if (header.role != bake::AssetPackRole::Chunk || header.label != manifest.id) {
        Refuse(info, ModProblem::PackNotAModChunk, "the pack must be a chunk pack labelled " + manifest.id);
        return info;
    }
    if (const bake::AssetPackSeal* seal = reader.Seal(); seal != nullptr) {
        info.sealed = true;
        info.sealSigner = seal->signer;
        if (manifest.signer.has_value() && *manifest.signer != seal->signer) {
            Refuse(info, ModProblem::SignerMismatch, "the pack is sealed by another key than the manifest names");
            return info;
        }
        if (!policy.Trusts(seal->signer)) {
            Refuse(info, ModProblem::UntrustedSigner, "sealed by " + kb::security::ToHex(seal->signer));
            return info;
        }
    } else {
        if (manifest.signer.has_value()) {
            Refuse(info, ModProblem::Unsigned, "the manifest names a signer but the pack carries no seal");
            return info;
        }
        if (!policy.allowUnsigned) {
            Refuse(info, ModProblem::Unsigned, "the pack is not sealed and this player does not allow unsigned mods");
            return info;
        }
    }
    if (!Satisfies(manifest.api, host.apiVersion)) {
        Refuse(info, ModProblem::ApiIncompatible, "the mod wants API " + ToString(manifest.api) + ", this game offers " + ToString(host.apiVersion));
        return info;
    }
    if (host.baseIdentity.has_value() && header.baseIdentity != *host.baseIdentity) {
        Refuse(info, ModProblem::BaseMismatch, "the mod was cooked against another build of the game");
        return info;
    }
    kb::security::Sha512Digest digest{};
    if (!HashFile(info.packPath, digest)) {
        Refuse(info, ModProblem::PackMissing, "could not read " + manifest.pack);
        return info;
    }
    if (!kb::security::ConstantTimeEqual(digest, manifest.packSha512)) {
        Refuse(info, ModProblem::PackHashMismatch, "the pack is not the one the manifest describes");
        return info;
    }
    return info;
}

bool BindModManifestToPack(const std::filesystem::path& directory, ModManifest& manifest, std::string& error) {
    const std::filesystem::path pack = directory / manifest.pack;
    if (!IsValidModPackFileName(manifest.pack) || !IsRegularFile(pack)) {
        error = "the pack " + manifest.pack + " is not a file in " + directory.string();
        return false;
    }
    bake::AssetPackReader reader;
    if (const bake::AssetPackReadStatus status = reader.Mount(pack); status != bake::AssetPackReadStatus::Success) {
        error = "the pack does not mount: " + std::string{ bake::ToString(status) };
        return false;
    }
    if (!HashFile(pack, manifest.packSha512)) {
        error = "could not read " + pack.string();
        return false;
    }
    manifest.signer.reset();
    if (const bake::AssetPackSeal* seal = reader.Seal(); seal != nullptr) {
        manifest.signer = seal->signer;
    }
    return true;
}

ModCatalog::ModCatalog(std::filesystem::path root) : root_(std::move(root)) {}

std::vector<std::string> ModCatalog::EnabledIds() const {
    return ParseEnabledState(ReadSmallFile(root_ / std::string{ kModStateFileName }, kMaxStateFileBytes));
}

bool ModCatalog::WriteEnabled(const std::vector<std::string>& ids, std::string& error) const {
    std::string text = "21kb-mod-state 1\n";
    for (const std::string& id : ids) {
        text += "enabled " + id + '\n';
    }
    return WriteFileAtomically(root_ / std::string{ kModStateFileName }, text, error);
}

ModTrustPolicy ModCatalog::ReadTrustPolicy() const {
    ModTrustPolicy policy{};
    const std::string text = ReadSmallFile(root_ / std::string{ kModTrustFileName }, kMaxStateFileBytes);
    std::string error;
    if (!text.empty() && !ParseModTrustPolicy(text, policy, error)) {
        return ModTrustPolicy{};
    }
    return policy;
}

bool ModCatalog::WriteTrustPolicy(const ModTrustPolicy& policy, std::string& error) const {
    return WriteFileAtomically(root_ / std::string{ kModTrustFileName }, EncodeModTrustPolicy(policy), error);
}

std::vector<ModInfo> ModCatalog::List(const ModHost& host, const ModTrustPolicy& policy) const {
    std::vector<std::filesystem::path> folders;
    std::error_code error;
    for (std::filesystem::directory_iterator it{ root_, error }, end; !error && it != end; it.increment(error)) {
        const std::string name = it->path().filename().string();
        if (!name.empty() && name.front() != '.' && IsRealDirectory(it->path()) && folders.size() < kMaxInstalledMods) {
            folders.push_back(it->path());
        }
    }
    std::ranges::sort(folders);
    const std::vector<std::string> enabled = EnabledIds();
    std::vector<ModInfo> infos;
    infos.reserve(folders.size());
    for (const std::filesystem::path& folder : folders) {
        ModInfo info = InspectMod(folder, host, policy);
        info.enabled = std::ranges::find(enabled, info.manifest.id) != enabled.end();
        infos.push_back(std::move(info));
    }
    const auto orderOf = [&enabled](const ModInfo& info) {
        const auto found = std::ranges::find(enabled, info.manifest.id);
        return found == enabled.end() ? enabled.size() : static_cast<std::size_t>(found - enabled.begin());
    };
    std::ranges::stable_sort(infos, [&orderOf](const ModInfo& lhs, const ModInfo& rhs) { return orderOf(lhs) < orderOf(rhs); });
    return infos;
}

ModPlan ModCatalog::Resolve(
    const ModHost& host,
    const ModTrustPolicy& policy,
    const std::vector<std::pair<std::string, std::string>>& excluded) const {
    ModPlan plan{};
    std::vector<ModInfo> infos = List(host, policy);
    std::vector<ModInfo> candidates;
    for (ModInfo& info : infos) {
        if (!info.enabled) {
            plan.disabled.push_back(info);
            continue;
        }
        const auto drop = std::ranges::find(excluded, info.manifest.id, &std::pair<std::string, std::string>::first);
        if (drop != excluded.end() && info.problem == ModProblem::None) {
            Refuse(info, ModProblem::MountRefused, drop->second);
        }
        if (info.problem != ModProblem::None) {
            plan.refused.push_back(info);
        } else {
            candidates.push_back(info);
        }
    }
    const auto find = [](const std::vector<ModInfo>& list, std::string_view id) -> const ModInfo* {
        const auto found = std::ranges::find(list, id, [](const ModInfo& info) -> const std::string& { return info.manifest.id; });
        return found == list.end() ? nullptr : &*found;
    };

    // Drop, one at a time and from the start, the first candidate that needs something it cannot
    // have; dropping one can orphan the mods that need it, so look again until nothing changes.
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t index = 0U; index < candidates.size() && !changed; ++index) {
            ModInfo candidate = candidates[index];
            ModProblem problem = ModProblem::None;
            std::string detail;
            for (const ModDependency& dependency : candidate.manifest.dependencies) {
                if (const ModInfo* present = find(candidates, dependency.id); present != nullptr) {
                    if (!Satisfies(dependency.range, present->manifest.version)) {
                        problem = ModProblem::DependencyVersion;
                        detail = dependency.id + " " + ToString(present->manifest.version) + " is installed, " + ToString(dependency.range) + " is needed";
                    }
                } else if (const ModInfo* disabled = find(plan.disabled, dependency.id); disabled != nullptr) {
                    problem = ModProblem::DependencyDisabled;
                    detail = dependency.id + " is installed but disabled";
                } else if (const ModInfo* refused = find(plan.refused, dependency.id); refused != nullptr) {
                    problem = ModProblem::DependencyRefused;
                    detail = dependency.id + " cannot load: " + std::string{ ToString(refused->problem) };
                } else {
                    problem = ModProblem::DependencyMissing;
                    detail = dependency.id + " " + ToString(dependency.range) + " is not installed";
                }
                if (problem != ModProblem::None) {
                    break;
                }
            }
            if (problem == ModProblem::None) {
                for (std::size_t earlier = 0U; earlier < index && problem == ModProblem::None; ++earlier) {
                    const ModInfo& other = candidates[earlier];
                    if (std::ranges::find(candidate.manifest.conflicts, other.manifest.id) != candidate.manifest.conflicts.end() ||
                        std::ranges::find(other.manifest.conflicts, candidate.manifest.id) != other.manifest.conflicts.end()) {
                        problem = ModProblem::Conflict;
                        detail = "conflicts with " + other.manifest.id;
                    }
                }
            }
            if (problem != ModProblem::None) {
                Refuse(candidate, problem, detail);
                plan.refused.push_back(std::move(candidate));
                candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(index));
                changed = true;
            }
        }
    }

    // Dependencies first; among the mods that are ready, the one enabled earliest goes first.
    std::vector<bool> placed(candidates.size(), false);
    for (std::size_t round = 0U; round < candidates.size(); ++round) {
        std::size_t pick = candidates.size();
        for (std::size_t index = 0U; index < candidates.size() && pick == candidates.size(); ++index) {
            if (placed[index]) {
                continue;
            }
            bool ready = true;
            for (const ModDependency& dependency : candidates[index].manifest.dependencies) {
                const auto at = std::ranges::find(candidates, dependency.id, [](const ModInfo& info) -> const std::string& { return info.manifest.id; });
                if (at != candidates.end() && !placed[static_cast<std::size_t>(at - candidates.begin())]) {
                    ready = false;
                    break;
                }
            }
            if (ready) {
                pick = index;
            }
        }
        if (pick == candidates.size()) {
            break;
        }
        placed[pick] = true;
        plan.load.push_back(candidates[pick]);
    }
    for (std::size_t index = 0U; index < candidates.size(); ++index) {
        if (!placed[index]) {
            ModInfo cyclic = candidates[index];
            Refuse(cyclic, ModProblem::DependencyCycle, "part of, or needs, a cycle of required mods");
            plan.refused.push_back(std::move(cyclic));
        }
    }
    return plan;
}

bool ModCatalog::Install(
    const std::filesystem::path& source,
    const ModHost& host,
    const ModTrustPolicy& policy,
    bool replace,
    ModInfo& installed,
    std::string& error) {
    const ModInfo inspected = InspectMod(source, host, policy);
    // A source folder may be named anything; only the manifest decides the id. Inspect it under its
    // id once staged, so the folder rule holds for what is installed.
    ModManifest manifest{};
    if (const ModManifestResult read = ReadModManifest(source / std::string{ kModManifestFileName }, manifest); !read.Succeeded()) {
        error = "Mod.kbmod: " + read.Describe();
        return false;
    }
    std::error_code ec;
    if (std::filesystem::exists(root_ / manifest.id, ec) && !replace) {
        ModManifest existing{};
        const bool known = ReadModManifest(root_ / manifest.id / std::string{ kModManifestFileName }, existing).Succeeded();
        error = manifest.id + (known ? " " + ToString(existing.version) : std::string{}) + " is already installed; replace it to install " + ToString(manifest.version);
        return false;
    }
    if (!IsRealDirectory(source)) {
        error = source.string() + " is not a folder";
        return false;
    }
    if (inspected.problem != ModProblem::None && inspected.problem != ModProblem::FolderMismatch) {
        error = inspected.Describe();
        return false;
    }
    const std::filesystem::path staging = root_ / ".staging";
    const std::filesystem::path staged = staging / manifest.id;
    std::filesystem::remove_all(staging, ec);
    std::filesystem::create_directories(staged, ec);
    if (ec) {
        error = "could not create " + staged.string();
        return false;
    }
    const auto copy = [&](const std::filesystem::path& name) {
        std::filesystem::copy_file(source / name, staged / name, std::filesystem::copy_options::overwrite_existing, ec);
        return !ec;
    };
    if (!copy(std::string{ kModManifestFileName }) || !copy(manifest.pack)) {
        error = "could not copy the mod: " + ec.message();
        std::filesystem::remove_all(staging, ec);
        return false;
    }
    ModInfo check = InspectMod(staged, host, policy);
    if (check.problem != ModProblem::None) {
        error = "the copied mod does not verify: " + check.Describe();
        std::filesystem::remove_all(staging, ec);
        return false;
    }
    const std::filesystem::path target = root_ / manifest.id;
    const std::filesystem::path old = staging / (manifest.id + ".old");
    const bool hadOld = std::filesystem::exists(target, ec);
    if (hadOld) {
        std::filesystem::rename(target, old, ec);
        if (ec) {
            error = "could not replace " + target.string();
            std::filesystem::remove_all(staging, ec);
            return false;
        }
    }
    std::filesystem::rename(staged, target, ec);
    if (ec) {
        error = "could not install into " + target.string();
        if (hadOld) {
            std::filesystem::rename(old, target, ec);
        }
        std::filesystem::remove_all(staging, ec);
        return false;
    }
    std::filesystem::remove_all(staging, ec);
    std::vector<std::string> enabled = EnabledIds();
    if (std::ranges::find(enabled, manifest.id) == enabled.end()) {
        enabled.push_back(manifest.id);
        if (!WriteEnabled(enabled, error)) {
            return false;
        }
    }
    installed = InspectMod(target, host, policy);
    installed.enabled = true;
    return true;
}

bool ModCatalog::Remove(std::string_view id, std::string& error) {
    if (!IsValidModId(id) || !IsRealDirectory(root_ / std::string{ id })) {
        error = std::string{ id } + " is not installed";
        return false;
    }
    std::error_code ec;
    std::filesystem::remove_all(root_ / std::string{ id }, ec);
    if (ec) {
        error = "could not remove " + std::string{ id } + ": " + ec.message();
        return false;
    }
    std::vector<std::string> enabled = EnabledIds();
    if (const auto found = std::ranges::find(enabled, std::string{ id }); found != enabled.end()) {
        enabled.erase(found);
        return WriteEnabled(enabled, error);
    }
    return true;
}

bool ModCatalog::SetEnabled(std::string_view id, bool enabled, std::string& error) {
    if (!IsValidModId(id) || !IsRealDirectory(root_ / std::string{ id })) {
        error = std::string{ id } + " is not installed";
        return false;
    }
    std::vector<std::string> ids = EnabledIds();
    const auto found = std::ranges::find(ids, std::string{ id });
    if (enabled) {
        if (found != ids.end()) {
            return true;
        }
        ids.emplace_back(id);
    } else {
        if (found == ids.end()) {
            return true;
        }
        ids.erase(found);
    }
    return WriteEnabled(ids, error);
}

} // namespace kb::mods
