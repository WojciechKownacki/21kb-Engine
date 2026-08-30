#include "engine/assets/bake/AssetBakeKey.hpp"
#include "engine/assets/bake/AssetPackWriter.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: kb_asset_pack_fixture_generator <target-profile> <output.kbpack>\n";
        return 2;
    }

    kb::assets::bake::BakeTargetProfile profile{};
    if (!kb::assets::bake::TryFindBakeTargetProfile(argv[1], profile)) {
        std::cerr << "unknown target profile: " << argv[1] << '\n';
        return 2;
    }

    constexpr std::string_view payload =
        "21kb Android packaging verification artifact\n";
    const std::span<const std::uint8_t> payloadBytes{
        reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size() };

    kb::assets::bake::AssetBakeKey key{};
    key.sourceContentHash = kb::assets::bake::HashBakeBytes(payloadBytes);
    key.bakerId = "PackageVerification";
    key.bakerVersion = "1";
    key.targetProfileId = profile.identifier;
    key.targetProfileHash =
        kb::assets::bake::BakeTargetProfileFingerprint(profile);
    key.settingsHash = 1U;

    kb::assets::bake::AssetPackWriter writer{
        std::filesystem::path{ argv[2] }, profile };
    const kb::assets::bake::BakedAssetDescriptor descriptor{
        .key = std::move(key),
        .assetTypeId = "RuntimeManifest",
    };
    if (writer.BeginAsset(descriptor) !=
            kb::assets::bake::BakedAssetSinkStatus::Success ||
        writer.WritePrimaryBlock(
            payloadBytes, profile.packageBlockAlignmentBytes) !=
            kb::assets::bake::BakedAssetSinkStatus::Success ||
        writer.CommitAsset() != kb::assets::bake::BakedAssetSinkStatus::Success ||
        writer.Finish() != kb::assets::bake::BakedAssetSinkStatus::Success) {
        std::cerr << "could not write a validated package fixture\n";
        return 1;
    }
    return 0;
}
