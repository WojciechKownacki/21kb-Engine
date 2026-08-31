#include "RendererTestSupport.hpp"

#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/assets/bake/AssetPackWriter.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetManifest.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "kb/render/bake/RuntimeAssetPackValidation.hpp"
#include "kb/render/ShaderManifest.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ranges>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace kb::render::tests {
namespace {

namespace asset_bake = kb::assets::bake;

#if !defined(KB_TEST_AUDIO_ASSET_DIR)
#error "KB_TEST_AUDIO_ASSET_DIR must name the production audio fixtures"
#endif

void AppendU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

[[nodiscard]] std::vector<std::uint8_t> BgfxShaderBlob(char stage) {
    std::vector<std::uint8_t> blob{
        static_cast<std::uint8_t>(stage),
        static_cast<std::uint8_t>('S'),
        static_cast<std::uint8_t>('H'),
        11U,
    };
    AppendU32(blob, 0x11111111U);
    AppendU32(blob, 0x22222222U);
    AppendU16(blob, 0U);
    const std::vector<std::uint8_t> bytecode{ 0xdeU, 0xadU, 0xbeU, 0xefU };
    AppendU32(blob, static_cast<std::uint32_t>(bytecode.size()));
    blob.insert(blob.end(), bytecode.begin(), bytecode.end());
    blob.push_back(0U);
    blob.push_back(0U);
    return blob;
}

[[nodiscard]] asset_bake::AssetBakeDigest StoreArtifact(
    asset_bake::AssetPackWriter& writer,
    const asset_bake::BakeTargetProfile& profile,
    std::span<const std::uint8_t> bytes,
    std::string_view type,
    std::string_view settings) {
    const asset_bake::AssetBakeKey key{
        .sourceContentHash = asset_bake::HashBakeBytes(bytes),
        .bakerId = "RuntimePackValidationTest",
        .bakerVersion = "1",
        .targetProfileId = std::string{ profile.identifier },
        .targetProfileHash = asset_bake::BakeTargetProfileFingerprint(profile),
        .settingsHash = asset_bake::HashBakeText(settings),
    };
    Require(writer.BeginAsset(asset_bake::BakedAssetDescriptor{
                .key = key,
                .assetTypeId = std::string{ type },
            }) == asset_bake::BakedAssetSinkStatus::Success &&
            writer.WritePrimaryBlock(bytes, profile.packageBlockAlignmentBytes) ==
                asset_bake::BakedAssetSinkStatus::Success &&
            writer.CommitAsset() == asset_bake::BakedAssetSinkStatus::Success,
        "Runtime pack validation fixture could not store an artifact");
    return key.Digest();
}

[[nodiscard]] asset_bake::AssetBakeDigest StoreSource(
    asset_bake::AssetPackWriter& writer,
    const asset_bake::BakeTargetProfile& profile,
    std::span<const std::uint8_t> bytes,
    std::string_view settings) {
    std::vector<std::uint8_t> envelope;
    Require(asset_bake::EncodeRuntimeSourceBlob(bytes, envelope),
        "Runtime pack validation fixture could not encode source bytes");
    return StoreArtifact(
        writer, profile, envelope, asset_bake::kSourceAssetTypeId, settings);
}

[[nodiscard]] std::vector<std::uint8_t> SceneBytes(
    const std::filesystem::path& root,
    std::string_view name) {
    const std::filesystem::path path = root / (std::string{ name } + ".21kbscene");
    kb::scene::Scene scene;
    Require(kb::scene::SceneDocumentService::Save(scene, path, std::string{ name }),
        "Runtime pack validation fixture could not serialize its default Scene");
    std::ifstream input{ path, std::ios::binary };
    Require(input.is_open(), "Runtime pack validation fixture could not reopen its default Scene");
    return std::vector<std::uint8_t>{
        std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

[[nodiscard]] std::vector<std::uint8_t> FileBytes(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    Require(input.is_open(), "Runtime pack validation fixture could not open a source file");
    return std::vector<std::uint8_t>{
        std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

template <typename Write>
[[nodiscard]] std::vector<std::uint8_t> DocumentBytes(Write&& write) {
    std::ostringstream output;
    std::forward<Write>(write)(output);
    const std::string text = output.str();
    return std::vector<std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(text.data()),
        reinterpret_cast<const std::uint8_t*>(text.data()) + text.size() };
}

void AppendSourceAsset(
    asset_bake::RuntimeAssetManifest& manifest,
    asset_bake::AssetPackWriter& writer,
    const asset_bake::BakeTargetProfile& profile,
    kb::assets::AssetId id,
    std::string type,
    std::string path,
    std::string extension,
    const std::vector<std::uint8_t>& bytes,
    std::vector<kb::assets::AssetId> dependencies = {}) {
    const asset_bake::AssetBakeDigest digest = StoreSource(writer, profile, bytes, path);
    manifest.assets.push_back(asset_bake::RuntimeAssetManifestEntry{
        .id = id,
        .type = std::move(type),
        .name = std::filesystem::path{ path }.stem().string(),
        .virtualPath = std::move(path),
        .sourceExtension = std::move(extension),
        .contentHash = asset_bake::HashBakeBytes(bytes),
        .dependencies = std::move(dependencies),
        .artifacts = { asset_bake::RuntimeArtifactReference{
            .digest = digest,
            .encoding = asset_bake::RuntimeArtifactEncoding::SourceBytes,
        } },
    });
}

[[nodiscard]] std::shared_ptr<asset_bake::RuntimeAssetPack> FinishAndMount(
    asset_bake::RuntimeAssetManifest manifest,
    asset_bake::AssetPackWriter& writer,
    const asset_bake::BakeTargetProfile& profile,
    const std::filesystem::path& packPath) {
    std::vector<std::uint8_t> manifestBytes;
    Require(asset_bake::EncodeRuntimeAssetManifest(manifest, manifestBytes) ==
            asset_bake::RuntimeAssetManifestStatus::Success,
        "Runtime pack validation fixture manifest could not be encoded");
    static_cast<void>(StoreArtifact(
        writer,
        profile,
        manifestBytes,
        asset_bake::kRuntimeManifestAssetTypeId,
        "runtime-manifest"));
    Require(writer.Finish() == asset_bake::BakedAssetSinkStatus::Success,
        "Runtime pack validation fixture could not publish its package");

    auto pack = std::make_shared<asset_bake::RuntimeAssetPack>();
    Require(pack->Mount(packPath, profile) == asset_bake::RuntimeAssetPackStatus::Success,
        "Runtime pack validation fixture could not mount its package");
    return pack;
}

[[nodiscard]] asset_bake::RuntimeAssetManifest BaseManifest(
    const asset_bake::BakeTargetProfile& profile,
    std::string defaultMap,
    std::string name) {
    asset_bake::RuntimeAssetManifest manifest{
        .targetProfileId = std::string{ profile.identifier },
        .targetProfileHash = asset_bake::BakeTargetProfileFingerprint(profile),
    };
    manifest.descriptor.targetPlatforms = { "Windows" };
    manifest.settings.name = std::move(name);
    manifest.settings.defaultMap = std::move(defaultMap);
    return manifest;
}

void AppendRequiredFixedShaders(
    asset_bake::RuntimeAssetManifest& manifest,
    asset_bake::AssetPackWriter& writer,
    const asset_bake::BakeTargetProfile& profile) {
    for (std::uint32_t backendIndex = 0U;
         backendIndex < asset_bake::kShaderBakeBackendCount;
         ++backendIndex) {
        const auto backend = static_cast<asset_bake::ShaderBakeBackend>(backendIndex);
        if (!asset_bake::HasShaderBakeBackend(profile.shaderBackends, backend)) {
            continue;
        }
        const std::vector<std::string_view> required = RequiredPackagedShaderNames(
            PackagedGameShaderFeatures(backend));
        for (const std::string_view name : required) {
            const auto shader = std::ranges::find(RequiredShaderManifest(), name,
                &ShaderManifestEntry::name);
            Require(shader != RequiredShaderManifest().end(),
                "Runtime audio validation fixture references an unknown fixed shader");
            const char stage = shader->stage == ShaderStage::Vertex
                ? 'V'
                : shader->stage == ShaderStage::Fragment ? 'F' : 'C';
            const std::vector<std::uint8_t> bytes = BgfxShaderBlob(stage);
            const std::string virtualPath = "/Engine/Shaders/" +
                std::string{ asset_bake::ShaderBakePlatformName(profile.shaderPlatform) } + "/" +
                std::string{ asset_bake::ShaderBakeBackendName(backend) } + "/" +
                std::string{ name } + ".bin";
            const asset_bake::AssetBakeDigest digest =
                StoreSource(writer, profile, bytes, virtualPath);
            manifest.auxiliaryFiles.push_back(asset_bake::RuntimeAuxiliaryFileEntry{
                .virtualPath = virtualPath,
                .contentHash = asset_bake::HashBakeBytes(bytes),
                .artifactDigest = digest,
            });
        }
    }
}

struct AudioPackAsset final {
    std::string extension;
    std::string name;
    std::vector<std::uint8_t> bytes;
    bool imported = false;
};

void AppendAudioAsset(
    asset_bake::RuntimeAssetManifest& manifest,
    asset_bake::AssetPackWriter& writer,
    const asset_bake::BakeTargetProfile& profile,
    const std::filesystem::path& root,
    const AudioPackAsset& audio) {
    if (!audio.imported) {
        const std::string virtualPath = "/Game/Audio/" + audio.name + audio.extension;
        AppendSourceAsset(
            manifest,
            writer,
            profile,
            kb::assets::MakeAssetId(virtualPath + ":AudioClip"),
            "AudioClip",
            virtualPath,
            audio.extension,
            audio.bytes);
        return;
    }

    const std::filesystem::path importRoot = root / ("import-" + audio.name);
    const std::filesystem::path sourcePath = importRoot / (audio.name + audio.extension);
    std::error_code error;
    std::filesystem::create_directories(importRoot, error);
    Require(!error, "Runtime imported-audio fixture directory could not be created");
    {
        std::ofstream output{ sourcePath, std::ios::binary | std::ios::trunc };
        Require(output.is_open(), "Runtime imported-audio fixture could not be opened");
        output.write(reinterpret_cast<const char*>(audio.bytes.data()),
            static_cast<std::streamsize>(audio.bytes.size()));
        Require(output.good(), "Runtime imported-audio fixture could not be written");
    }
    kb::scene::Scene importScene;
    Require(importScene.Assets().MountProject(importRoot / "Project"),
        "Runtime imported-audio fixture project could not be mounted");
    const std::array<std::filesystem::path, 1U> sources{ sourcePath };
    const kb::assets::AssetImportResult imported = kb::assets::AssetImportService::ImportFiles(
        importScene.Assets().Manager(), sources, "/Game/Audio");
    Require(imported.Succeeded() && imported.items.size() == 1U,
        "Runtime imported-audio fixture could not enter the asset pipeline");
    const kb::assets::AssetImportItemResult& item = imported.items.front();
    AppendSourceAsset(
        manifest,
        writer,
        profile,
        item.id,
        "ImportedAsset",
        item.virtualPath.generic_string(),
        item.assetPhysicalPath.extension().generic_string(),
        FileBytes(item.assetPhysicalPath));
    auto entry = std::ranges::find(manifest.assets, item.id,
        &asset_bake::RuntimeAssetManifestEntry::id);
    Require(entry != manifest.assets.end(),
        "Runtime imported-audio fixture disappeared from its manifest");
    entry->importCategory = "Audio";
}

[[nodiscard]] RuntimeAssetPackValidationResult ValidateAudioPack(
    const std::filesystem::path& root,
    std::span<const AudioPackAsset> audioAssets) {
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Runtime audio validation fixture directory could not be created");

    const asset_bake::BakeTargetProfile profile = asset_bake::WebGlWasm32BakeTargetProfile();
    const std::filesystem::path packPath = root / "audio.kbpack";
    const std::string scenePath = "/Game/Scenes/Main.21kbscene";
    asset_bake::RuntimeAssetManifest manifest =
        BaseManifest(profile, scenePath, "AudioSemanticValidation");
    manifest.descriptor.targetPlatforms = { "WebGL" };
    asset_bake::AssetPackWriter writer{ packPath, profile };
    AppendSourceAsset(
        manifest,
        writer,
        profile,
        kb::assets::MakeAssetId(scenePath + ":Scene"),
        "Scene",
        scenePath,
        ".21kbscene",
        SceneBytes(root, "Main"));
    for (const AudioPackAsset& audio : audioAssets) {
        AppendAudioAsset(manifest, writer, profile, root, audio);
    }
    AppendRequiredFixedShaders(manifest, writer, profile);

    const std::shared_ptr<asset_bake::RuntimeAssetPack> pack =
        FinishAndMount(std::move(manifest), writer, profile, packPath);
    const RuntimeAssetPackValidationResult validation = ValidateRuntimeAssetPack(pack, profile);
    pack->Unmount();
    std::filesystem::remove_all(root, error);
    return validation;
}

void RuntimeAudioSourceMustBeDecodeReady() {
    struct Fixture final {
        std::string_view extension;
        std::string_view filename;
    };
    constexpr std::array<Fixture, 3U> fixtures{
        Fixture{ .extension = ".wav", .filename = "tone.wav" },
        Fixture{ .extension = ".flac", .filename = "tone.flac" },
        Fixture{ .extension = ".mp3", .filename = "tone.mp3" },
    };
    const std::filesystem::path fixtureRoot{ KB_TEST_AUDIO_ASSET_DIR };
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "21kb_runtime_pack_audio_validation";

    std::vector<AudioPackAsset> valid;
    valid.reserve(fixtures.size() * 2U);
    for (const Fixture& fixture : fixtures) {
        const std::vector<std::uint8_t> bytes = FileBytes(fixtureRoot / fixture.filename);
        valid.push_back(AudioPackAsset{
            .extension = std::string{ fixture.extension },
            .name = "DirectGood" + std::string{ fixture.extension.substr(1U) },
            .bytes = bytes,
        });
        valid.push_back(AudioPackAsset{
            .extension = std::string{ fixture.extension },
            .name = "ImportedGood" + std::string{ fixture.extension.substr(1U) },
            .bytes = bytes,
            .imported = true,
        });
    }
    Require(ValidateAudioPack(root / "valid", valid).Succeeded(),
        "Runtime pack validation rejected decode-ready WAV/FLAC/MP3 payloads");

    const std::vector<std::uint8_t> corrupt(32U, 0U);
    for (const Fixture& fixture : fixtures) {
        for (const bool imported : { false, true }) {
            const std::string name = (imported ? "ImportedBad" : "DirectBad") +
                std::string{ fixture.extension.substr(1U) };
            const std::array<AudioPackAsset, 1U> bad{ AudioPackAsset{
                .extension = std::string{ fixture.extension },
                .name = name,
                .bytes = corrupt,
                .imported = imported,
            } };
            const RuntimeAssetPackValidationResult validation = ValidateAudioPack(
                root / name, bad);
            Require(!validation.Succeeded() &&
                    validation.error.find("audio source payload is not decode-ready") !=
                        std::string::npos,
                "Runtime pack validation accepted a corrupt WAV/FLAC/MP3 payload");
        }
    }
}

void RuntimeLoadableSourceMustDecodeSemantically() {
    const asset_bake::BakeTargetProfile profile = asset_bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "21kb_runtime_pack_semantic_validation";
    const std::filesystem::path packPath = root / "semantic-invalid.kbpack";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Runtime pack semantic validation fixture directory could not be created");

    const std::string scenePath = "/Game/Scenes/Main.21kbscene";
    const std::string inputPath = "/Game/Input/Broken.kbinputaction";
    asset_bake::RuntimeAssetManifest manifest =
        BaseManifest(profile, scenePath, "SemanticInvalid");
    asset_bake::AssetPackWriter writer{ packPath, profile };
    const std::vector<std::uint8_t> sceneBytes = SceneBytes(root, "Main");
    AppendSourceAsset(
        manifest,
        writer,
        profile,
        kb::assets::MakeAssetId(scenePath + ":Scene"),
        "Scene",
        scenePath,
        ".21kbscene",
        sceneBytes);
    const std::vector<std::uint8_t> brokenInput{ 'n', 'o', 't', '-', 'a', 'n', '-', 'i', 'n', 'p', 'u', 't' };
    AppendSourceAsset(
        manifest,
        writer,
        profile,
        kb::assets::MakeAssetId(inputPath + ":InputAction"),
        "InputAction",
        inputPath,
        ".kbinputaction",
        brokenInput);

    const std::shared_ptr<asset_bake::RuntimeAssetPack> pack =
        FinishAndMount(std::move(manifest), writer, profile, packPath);
    const RuntimeAssetPackValidationResult validation =
        ValidateRuntimeAssetPack(pack, profile);
    Require(!validation.Succeeded() &&
            validation.error.find("semantically loadable") != std::string::npos &&
            validation.error.find(inputPath) != std::string::npos,
        "Runtime pack validation accepted a malformed non-default runtime SourceBytes asset");
    pack->Unmount();
    std::filesystem::remove_all(root, error);
}

void VertexDomainGraphMustContainCompleteVertexShaderMatrix() {
    const asset_bake::BakeTargetProfile profile = asset_bake::WindowsX64BakeTargetProfile();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "21kb_runtime_pack_graph_shader_validation";
    const std::filesystem::path packPath = root / "missing-vertex.kbpack";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Runtime graph shader validation fixture directory could not be created");

    const std::string scenePath = "/Game/Scenes/Main.21kbscene";
    const std::string materialPath = "/Game/Materials/Wind.kbmat";
    const std::string graphPath = "/Game/Materials/Wind.kbmaterialgraph";
    const kb::assets::AssetId materialId =
        kb::assets::MakeAssetId(materialPath + ":RenderMaterial");
    const kb::assets::AssetId graphId =
        kb::assets::MakeAssetId(graphPath + ":" + std::string{ kRenderMaterialGraphAssetType });

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.storageModel = "material-graph-asset";
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantVector,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.1 0 0" },
    });
    RenderMaterialGraphLink wpo{
        .fromNodeId = 2U,
        .fromPinId = RenderMaterialGraphStablePinId(
            RenderMaterialGraphNodeKind::ConstantVector, "xyz", true),
        .fromPin = "xyz",
        .toNodeId = 1U,
        .toPinId = RenderMaterialGraphStablePinId(
            RenderMaterialGraphNodeKind::MaterialOutput, "worldPositionOffset", false),
        .toPin = "worldPositionOffset",
    };
    wpo.id = MakeRenderMaterialGraphLinkId(wpo);
    graph.links.push_back(wpo);

    RenderMaterialAssetData material{};
    material.graphSourceAssetId = graphId.value;
    material.graphSourceAssetPath = graphPath;
    const std::vector<std::uint8_t> materialBytes = DocumentBytes(
        [&material](std::ostream& output) { RenderMaterialAssetWriter::Write(output, material); });
    const std::vector<std::uint8_t> graphBytes = DocumentBytes(
        [&graph](std::ostream& output) { WriteRenderMaterialGraphDocument(output, graph); });

    asset_bake::RuntimeAssetManifest manifest =
        BaseManifest(profile, scenePath, "MissingVertexShaders");
    asset_bake::AssetPackWriter writer{ packPath, profile };
    AppendSourceAsset(
        manifest,
        writer,
        profile,
        kb::assets::MakeAssetId(scenePath + ":Scene"),
        "Scene",
        scenePath,
        ".21kbscene",
        SceneBytes(root, "Main"));
    AppendSourceAsset(
        manifest,
        writer,
        profile,
        graphId,
        std::string{ kRenderMaterialGraphAssetType },
        graphPath,
        std::string{ kRenderMaterialGraphAssetExtension },
        graphBytes);
    AppendSourceAsset(
        manifest,
        writer,
        profile,
        materialId,
        "RenderMaterial",
        materialPath,
        ".kbmat",
        materialBytes,
        { graphId });

    const std::vector<std::uint8_t> shaderBytes = BgfxShaderBlob('F');
    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(
        graph,
        RenderMaterialGraphBuildContext{
            .assetId = materialId.value,
            .sourcePath = materialPath,
        });
    Require(compiled.Succeeded(),
        "Runtime graph shader fixture graph did not compile");
    const std::uint64_t graphHash = compiled.shader.sourceHash;
    const std::uint64_t variantKey = ComputeRenderMaterialGraphVariantKey(compiled.shader);
    const std::string platform =
        std::string{ asset_bake::ShaderBakePlatformName(profile.shaderPlatform) };
    for (std::uint32_t backendIndex = 0U;
         backendIndex < asset_bake::kShaderBakeBackendCount;
         ++backendIndex) {
        const auto backend = static_cast<asset_bake::ShaderBakeBackend>(backendIndex);
        if (!asset_bake::HasShaderBakeBackend(profile.shaderBackends, backend)) {
            continue;
        }
        for (const std::string_view pass :
             { std::string_view{ "BaseOpaque" }, std::string_view{ "GBuffer" },
               std::string_view{ "ShadowDepth" }, std::string_view{ "BaseTransparent" } }) {
            const std::string qualifier = std::to_string(graphHash) + ":" +
                std::to_string(variantKey) + ":" + std::string{ pass } + ":" +
                std::string{ asset_bake::ShaderBakeBackendName(backend) } + ":" +
                platform + ":fragment";
            const asset_bake::AssetBakeDigest digest = StoreArtifact(
                writer,
                profile,
                shaderBytes,
                asset_bake::kMaterialShaderAssetTypeId,
                qualifier);
            auto materialEntry = std::ranges::find(
                manifest.assets, materialId, &asset_bake::RuntimeAssetManifestEntry::id);
            Require(materialEntry != manifest.assets.end(),
                "Runtime graph shader fixture lost its material manifest entry");
            materialEntry->artifacts.push_back(asset_bake::RuntimeArtifactReference{
                .digest = digest,
                .encoding = asset_bake::RuntimeArtifactEncoding::MaterialShader,
                .qualifier = qualifier,
            });
        }
    }

    const std::shared_ptr<asset_bake::RuntimeAssetPack> pack =
        FinishAndMount(std::move(manifest), writer, profile, packPath);
    const RuntimeAssetPackValidationResult validation =
        ValidateRuntimeAssetPack(pack, profile);
    Require(!validation.Succeeded() &&
            validation.error.find("missing a required vertex shader") != std::string::npos &&
            validation.error.find(materialPath) != std::string::npos,
        "Runtime pack validation accepted a vertex-domain material without vertex shaders");
    pack->Unmount();
    std::filesystem::remove_all(root, error);
}

} // namespace

void RunRuntimeAssetPackValidationTests() {
    RuntimeLoadableSourceMustDecodeSemantically();
    RuntimeAudioSourceMustBeDecodeReady();
    VertexDomainGraphMustContainCompleteVertexShaderMatrix();
}

} // namespace kb::render::tests
