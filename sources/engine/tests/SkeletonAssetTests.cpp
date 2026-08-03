#include "TestSupport.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SkeletonAsset.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"

#include <filesystem>
#include <fstream>

namespace {

[[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    return { std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

void WriteTextFile(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

} // namespace

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
    skeleton.sockets = {
        {
            .name = "Weapon",
            .boneId = 0x1002U,
            .localTransform = { .position = { 0.25F, 0.0F, 0.0F } },
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
                NearlyEqual(loaded->bones[1].inverseBind.columns[3].y, -1.0F) &&
                loaded->sockets.size() == 1U && loaded->sockets[0].name == "Weapon" &&
                loaded->sockets[0].boneId == 0x1002U &&
                NearlyEqual(loaded->sockets[0].localTransform.position.x, 0.25F),
        "SkeletonAsset round trip lost canonical bone data");
    Require(kb::scene::SkeletonCompatibilitySignature(*loaded) == signature,
        "SkeletonAsset compatibility signature changed after round trip");
    const std::filesystem::path deterministicPath =
        root / "RoundTrip" / "HeroCopy.kbskeleton";
    Require(kb::scene::SkeletonAssetIO::Save(deterministicPath, skeleton) &&
            ReadTextFile(path) == ReadTextFile(deterministicPath),
        "SkeletonAsset serialization is not deterministic");
    const std::string serialized = ReadTextFile(path);
    const std::size_t headerEnd = serialized.find('\n');
    Require(headerEnd != std::string::npos,
        "SkeletonAsset serialization has no schema header");
    const std::filesystem::path legacyPath =
        root / "RoundTrip" / "Legacy.kbskeleton";
    WriteTextFile(legacyPath, std::string_view{ serialized }.substr(headerEnd + 1U));
    Require(kb::scene::SkeletonAssetIO::Load(legacyPath).has_value(),
        "SkeletonAsset legacy migration failed");
    const std::filesystem::path corruptPath =
        root / "RoundTrip" / "Corrupt.kbskeleton";
    WriteTextFile(corruptPath, "21kb Skeleton 99\n");
    std::string corruptError;
    Require(!kb::scene::SkeletonAssetIO::Load(corruptPath, &corruptError).has_value() &&
            corruptError.find("line 1") != std::string::npos,
        "SkeletonAsset accepted an unsupported schema version without a diagnostic");

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
