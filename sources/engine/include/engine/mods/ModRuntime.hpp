#pragma once

#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/mods/ModCatalog.hpp"

#include <filesystem>
#include <string>
#include <vector>

// Mounting the mods of a game beside its own content, and telling scripts what loaded.
namespace kb::mods {

// One mod that is mounted, as scripts and the game see it.
struct LoadedMod {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string entryScene;
    // Whether the pack carries a seal (a trusted one: unsealed packs load only when the player allows it).
    bool sealed = false;

    [[nodiscard]] bool operator==(const LoadedMod&) const noexcept = default;
};

struct ModMountReport {
    // The mods that are part of the mounted pack, in mount order.
    std::vector<ModInfo> loaded;
    // Enabled mods that could not load, each with the reason.
    std::vector<ModInfo> refused;
    std::vector<ModInfo> disabled;
};

// Mounts the game's own packs (`packOrIndex`: a pack set index or a single base pack, read the way
// a game reads them, with `trust`) together with the mods `catalog` can load, into `pack`. A mod
// pack is mounted as a chunk after the game's chunks and before its patches, each under the trust of
// its own seal (never the release's). A mod the runtime refuses to mount (it redefines an asset,
// misses what it depends on...) is left out and reported, and the rest mount. If even the game
// alone does not mount, the status says why. `pack` is mounted on success and left unmounted on failure.
//
// `gameIdentity` is the catalogue identity of the game's base pack (container 0 of a pack that is
// already mounted from the same files); mods cooked against another build are refused with it.
[[nodiscard]] kb::assets::bake::RuntimeAssetPackStatus MountPackWithMods(
    kb::assets::bake::RuntimeAssetPack& pack,
    const std::filesystem::path& packOrIndex,
    const kb::assets::bake::BakeTargetProfile& profile,
    kb::assets::bake::AssetPackAccess access,
    const kb::assets::bake::AssetPackTrust& trust,
    const ModCatalog& catalog,
    const ModTrustPolicy& policy,
    const kb::assets::bake::AssetBakeDigest& gameIdentity,
    ModMountReport& report);

// The catalogue identity of the game's base pack, read from a pack set index or a base pack: what a
// mod is cooked against and what ModHost::baseIdentity compares with.
[[nodiscard]] bool ReadGameIdentity(
    const std::filesystem::path& packOrIndex,
    const kb::assets::bake::AssetPackTrust& trust,
    kb::assets::bake::AssetBakeDigest& identity,
    std::string& error);

[[nodiscard]] std::vector<LoadedMod> LoadedModsOf(const ModMountReport& report);

// The mods the running game has mounted, for the `Mods` script functions. Set by the host once
// the pack is mounted; empty until then. Thread-safe.
void PublishLoadedMods(std::vector<LoadedMod> mods);
[[nodiscard]] std::vector<LoadedMod> PublishedLoadedMods();

} // namespace kb::mods
