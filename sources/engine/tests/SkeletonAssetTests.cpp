#include "TestSupport.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SkeletonAsset.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"

#include <filesystem>

namespace kb::tests {

void RunSkeletonAssetTests() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb-skeleton-asset-tests";
    std::filesystem::remove_all(root);
    const std::filesystem::path path = root / "Assets" / "Characters" / "Hero.kbskeleton";

    kb::scene::SkeletonAsset skeleton{};
    skeleton.bones = {
        {
            .id = 0x1001U,
            .parentIndex = -1,
            .name = "Root",
            .referencePose = {},
            .inverseBind = {},
        },
        {
            .id = 0x1002U,
            .parentIndex = 0,
            .name = "Spine",
            .referencePose = { .position = { 0.0F, 1.0F, 0.0F } },
            .inverseBind = { .columns = {
                { 1.0F, 0.0F, 0.0F, 0.0F },
                { 0.0F, 1.0F, 0.0F, 0.0F },
                { 0.0F, 0.0F, 1.0F, 0.0F },
                { 0.0F, -1.0F, 0.0F, 1.0F },
            } },
        },
    };

    const kb::scene::SkeletonAssetValidationResult validation = kb::scene::ValidateSkeletonAsset(skeleton);
    Require(validation.valid, "Canonical SkeletonAsset rejected a valid hierarchy");
    const std::uint64_t signature = kb::scene::SkeletonCompatibilitySignature(skeleton);
    Require(signature != 0U, "Valid SkeletonAsset did not produce a compatibility signature");
    Require(kb::scene::SkeletonAssetIO::Save(path, skeleton), "SkeletonAsset could not be saved through production IO");
    const auto loaded = kb::scene::SkeletonAssetIO::Load(path);
    Require(loaded.has_value() && loaded->bones.size() == 2U &&
                loaded->bones[1].id == skeleton.bones[1].id &&
                loaded->bones[1].parentIndex == 0 &&
                loaded->bones[1].name == "Spine" &&
                NearlyEqual(loaded->bones[1].referencePose.position.y, 1.0F) &&
                NearlyEqual(loaded->bones[1].inverseBind.columns[3].y, -1.0F),
        "SkeletonAsset round trip lost canonical bone data");
    Require(kb::scene::SkeletonCompatibilitySignature(*loaded) == signature,
        "SkeletonAsset compatibility signature changed after round trip");

    kb::scene::SkeletonAsset invalid = skeleton;
    invalid.bones[1].parentIndex = 1;
    Require(!kb::scene::ValidateSkeletonAsset(invalid).valid &&
                !kb::scene::SkeletonAssetIO::Save(root / "Assets" / "Characters" / "Invalid.kbskeleton", invalid),
        "SkeletonAsset accepted a non-canonical parent hierarchy");

    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(root), "SkeletonAsset project mount failed");
    Require(scene.Assets().Discover() == 1U, "SkeletonAsset discovery did not register the canonical asset");
    const kb::assets::AssetMetadata* metadata =
        scene.Assets().Manager().Registry().FindByPath("/Game/Characters/Hero.kbskeleton");
    Require(metadata != nullptr && metadata->type == kb::scene::kSkeletonAssetType,
        "SkeletonAsset was not classified by the production asset registry");
    const auto runtimeAsset = scene.Assets().Manager().Load<kb::scene::SkeletonAsset>(metadata->id);
    Require(runtimeAsset.IsLoaded() && kb::scene::SkeletonCompatibilitySignature(*runtimeAsset) == signature,
        "SkeletonAsset loader did not publish canonical data to runtime");
    Require(scene.Assets().Manager().Unload(metadata->id), "SkeletonAsset could not be unloaded from runtime");
    const auto reloadedRuntimeAsset = scene.Assets().Manager().Load<kb::scene::SkeletonAsset>(metadata->id);
    Require(reloadedRuntimeAsset.IsLoaded() && kb::scene::SkeletonCompatibilitySignature(*reloadedRuntimeAsset) == signature,
        "SkeletonAsset runtime reload did not rebuild canonical asset data");

    std::filesystem::remove_all(root);
}

} // namespace kb::tests
