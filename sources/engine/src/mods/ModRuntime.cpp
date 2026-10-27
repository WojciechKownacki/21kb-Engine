#include "engine/mods/ModRuntime.hpp"

#include "engine/assets/bake/AssetPackReader.hpp"
#include "engine/assets/bake/AssetPackSeal.hpp"
#include "engine/assets/bake/AssetPackSet.hpp"

#include <algorithm>
#include <mutex>

namespace kb::mods {
namespace {

namespace bake = kb::assets::bake;

std::mutex& PublishedMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<LoadedMod>& Published() {
    static std::vector<LoadedMod> mods;
    return mods;
}

[[nodiscard]] std::string ExtensionOf(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](char value) {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
    });
    return extension;
}

// The mounts of the game's own packs, as MountSetIndex would build them.
[[nodiscard]] bool GameMounts(const std::filesystem::path& packOrIndex, std::vector<bake::RuntimeAssetPackMount>& mounts) {
    mounts.clear();
    if (ExtensionOf(packOrIndex) != bake::kAssetPackSetFileExtension) {
        mounts.push_back(bake::RuntimeAssetPackMount{ .path = packOrIndex });
        return true;
    }
    bake::AssetPackSetIndex index{};
    if (bake::ReadAssetPackSetIndex(packOrIndex, index) != bake::AssetPackSetStatus::Success) {
        return false;
    }
    for (const bake::AssetPackSetEntry& entry : index.packs) {
        mounts.push_back(bake::RuntimeAssetPackMount{
            .path = bake::ResolveAssetPackSetPath(packOrIndex, entry.path),
            .role = entry.role,
            .label = entry.label,
            .patchLevel = entry.patchLevel,
            .wrappedContentKey = entry.wrappedContentKey,
        });
    }
    return true;
}

// The game's mounts with the mods' chunks put before the first patch.
[[nodiscard]] std::vector<bake::RuntimeAssetPackMount> WithMods(
    const std::vector<bake::RuntimeAssetPackMount>& game,
    const std::vector<ModInfo>& mods,
    std::size_t& firstMod) {
    std::vector<bake::RuntimeAssetPackMount> mounts;
    firstMod = game.size();
    for (std::size_t index = 0U; index < game.size(); ++index) {
        if (game[index].role == bake::AssetPackRole::Patch) {
            firstMod = index;
            break;
        }
    }
    mounts.assign(game.begin(), game.begin() + static_cast<std::ptrdiff_t>(firstMod));
    for (const ModInfo& mod : mods) {
        bake::RuntimeAssetPackMount mount{ .path = mod.packPath, .role = bake::AssetPackRole::Chunk, .label = mod.manifest.id };
        bake::AssetPackTrust trust{};
        // What was verified when the mod was inspected is what must still be there: a pack swapped
        // for one sealed by another key is refused at mount.
        trust.requiredSigner = mod.sealSigner;
        mount.trust = trust;
        mounts.push_back(std::move(mount));
    }
    mounts.insert(mounts.end(), game.begin() + static_cast<std::ptrdiff_t>(firstMod), game.end());
    return mounts;
}

[[nodiscard]] std::string Refusal(const bake::RuntimeAssetPack& pack, bake::RuntimeAssetPackStatus status) {
    std::string text{ bake::ToString(status) };
    if (status == bake::RuntimeAssetPackStatus::ContainerRejected) {
        text += " (" + std::string{ bake::ToString(pack.ContainerStatus()) } + ")";
    }
    return "the runtime refused the pack: " + text;
}

} // namespace

bake::RuntimeAssetPackStatus MountPackWithMods(
    bake::RuntimeAssetPack& pack,
    const std::filesystem::path& packOrIndex,
    const bake::BakeTargetProfile& profile,
    bake::AssetPackAccess access,
    const bake::AssetPackTrust& trust,
    const ModCatalog& catalog,
    const ModTrustPolicy& policy,
    const bake::AssetBakeDigest& gameIdentity,
    ModMountReport& report) {
    report = {};
    std::vector<bake::RuntimeAssetPackMount> game;
    if (!GameMounts(packOrIndex, game)) {
        return bake::RuntimeAssetPackStatus::PackSetInvalid;
    }
    ModHost host{};
    host.baseIdentity = gameIdentity;
    std::vector<std::pair<std::string, std::string>> excluded;
    for (std::size_t attempt = 0U; attempt <= kMaxInstalledMods + 1U; ++attempt) {
        ModPlan plan = catalog.Resolve(host, policy, excluded);
        std::size_t firstMod = 0U;
        const std::vector<bake::RuntimeAssetPackMount> mounts = WithMods(game, plan.load, firstMod);
        bake::RuntimeAssetPack& candidate = pack;
        const bake::RuntimeAssetPackStatus status = candidate.MountSet(mounts, profile, access, trust);
        if (status == bake::RuntimeAssetPackStatus::Success) {
            report.loaded = std::move(plan.load);
            report.refused = std::move(plan.refused);
            report.disabled = std::move(plan.disabled);
            return status;
        }
        if (plan.load.empty()) {
            // The game's own packs do not mount.
            return status;
        }
        const std::uint32_t refused = candidate.RefusedContainer();
        const std::size_t modIndex = refused >= firstMod ? refused - firstMod : plan.load.size();
        if (modIndex < plan.load.size()) {
            excluded.emplace_back(plan.load[modIndex].manifest.id, Refusal(candidate, status));
            continue;
        }
        // The runtime blamed no mod (a missing dependency is reported against the base). Find
        // the mods that do not mount with the game alone; if every one does, the last one is
        // the one that does not fit with the others.
        bool found = false;
        for (const ModInfo& mod : plan.load) {
            std::size_t ignored = 0U;
            const std::vector<bake::RuntimeAssetPackMount> single = WithMods(game, { mod }, ignored);
            bake::RuntimeAssetPack alone;
            if (const bake::RuntimeAssetPackStatus aloneStatus = alone.MountSet(single, profile, access, trust);
                aloneStatus != bake::RuntimeAssetPackStatus::Success) {
                excluded.emplace_back(mod.manifest.id, Refusal(alone, aloneStatus));
                found = true;
            }
        }
        if (!found) {
            excluded.emplace_back(plan.load.back().manifest.id, Refusal(candidate, status));
        }
    }
    return bake::RuntimeAssetPackStatus::PackSetInvalid;
}

bool ReadGameIdentity(
    const std::filesystem::path& packOrIndex,
    const bake::AssetPackTrust& trust,
    bake::AssetBakeDigest& identity,
    std::string& error) {
    std::vector<bake::RuntimeAssetPackMount> game;
    if (!GameMounts(packOrIndex, game) || game.empty()) {
        error = packOrIndex.string() + " is not a pack or a readable pack set index";
        return false;
    }
    bake::AssetPackReader reader;
    bake::AssetPackTrust baseTrust = trust;
    if (game.front().wrappedContentKey.has_value() && trust.contentKey.has_value()) {
        kb::security::AeadKey key{};
        if (bake::UnwrapAssetPackContentKey(*trust.contentKey, *game.front().wrappedContentKey, key)) {
            baseTrust.contentKey = key;
        }
    }
    if (const bake::AssetPackReadStatus status = reader.Mount(game.front().path, bake::AssetPackAccess::Ranged, baseTrust);
        status != bake::AssetPackReadStatus::Success) {
        error = "the game's base pack does not mount: " + std::string{ bake::ToString(status) };
        return false;
    }
    identity = reader.CatalogIdentity();
    return true;
}

std::vector<LoadedMod> LoadedModsOf(const ModMountReport& report) {
    std::vector<LoadedMod> mods;
    mods.reserve(report.loaded.size());
    for (const ModInfo& info : report.loaded) {
        mods.push_back(LoadedMod{
            .id = info.manifest.id,
            .name = info.manifest.name,
            .version = ToString(info.manifest.version),
            .author = info.manifest.author,
            .description = info.manifest.description,
            .entryScene = info.manifest.entryScene,
            .sealed = info.sealed,
        });
    }
    return mods;
}

void PublishLoadedMods(std::vector<LoadedMod> mods) {
    const std::lock_guard lock{ PublishedMutex() };
    Published() = std::move(mods);
}

std::vector<LoadedMod> PublishedLoadedMods() {
    const std::lock_guard lock{ PublishedMutex() };
    return Published();
}

} // namespace kb::mods
