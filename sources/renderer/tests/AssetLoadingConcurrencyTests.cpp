#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/assets/ImportedAssetLoader.hpp"
#include "engine/assets/bake/AssetPackWriter.hpp"
#include "engine/assets/bake/RuntimeAssetManifest.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "kb/render/bake/MeshBaker.hpp"
#include "kb/render/bake/TextureBaker.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iterator>
#include <thread>

namespace {
namespace fs = std::filesystem;
namespace bake = kb::assets::bake;
using Clock = std::chrono::steady_clock;
constexpr unsigned kGroups = 8U;
constexpr std::uint16_t kSide = 1024U;

void Require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(EXIT_FAILURE); }
}

std::vector<std::uint8_t> Read(const fs::path& path) {
    std::ifstream input{path, std::ios::binary};
    Require(input.is_open(), "Source file could not be read");
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void WriteBmp(const fs::path& path, unsigned group) {
    std::array<std::uint8_t, 54U> header{};
    const auto u32 = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned byte = 0U; byte < 4U; ++byte) header[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
    };
    header[0] = 'B'; header[1] = 'M'; header[26] = 1U; header[28] = 24U;
    u32(2U, 54U + kSide * kSide * 3U); u32(10U, 54U); u32(14U, 40U);
    u32(18U, kSide); u32(22U, kSide); u32(34U, kSide * kSide * 3U);
    std::array<std::uint8_t, kSide * 3U> row{};
    for (std::size_t pixel = 0U; pixel < row.size(); pixel += 3U) {
        row[pixel] = static_cast<std::uint8_t>(96U + group * 3U);
        row[pixel + 1U] = static_cast<std::uint8_t>(64U + group * 9U);
        row[pixel + 2U] = static_cast<std::uint8_t>(32U + group * 17U);
    }
    std::ofstream output{path, std::ios::binary};
    output.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    for (unsigned y = 0U; y < kSide; ++y) output.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    Require(output.good(), "Bitmap fixture could not be written");
}

void WriteObj(const fs::path& path, unsigned group) {
    std::ofstream output{path};
    for (unsigned triangle = 0U; triangle < 2048U; ++triangle) {
        output << "v " << triangle << ' ' << group << " 0\nv " << triangle + 1U << ' ' << group
            << " 0\nv " << triangle << ' ' << group + 1U << " 0\n";
        const unsigned first = triangle * 3U + 1U;
        output << "f " << first << ' ' << first + 1U << ' ' << first + 2U << '\n';
    }
    Require(output.good(), "Mesh fixture could not be written");
}

void Peak(std::atomic_uint& peak, unsigned value) {
    unsigned previous = peak.load();
    while (value > previous && !peak.compare_exchange_weak(previous, value)) {}
}

struct Profile {
    std::atomic_uint active{0U}, peak{0U};
    std::atomic_uint64_t gateNanoseconds{0U}, loadNanoseconds{0U};
};

class ProfiledLoader final : public kb::assets::IAssetLoader {
public:
    ProfiledLoader(std::unique_ptr<IAssetLoader> inner, Profile& profile, bool serialized)
        : inner_(std::move(inner)), profile_(profile), serialized_(serialized) {}
    std::string_view Type() const noexcept override { return inner_->Type(); }
    std::type_index PayloadType() const noexcept override { return inner_->PayloadType(); }
    std::vector<std::string> Extensions() const override { return inner_->Extensions(); }
    std::vector<std::string> BakedAssetTypes() const override { return inner_->BakedAssetTypes(); }
    bool AllowsConcurrentLoad() const noexcept override {
        gateStart_ = Clock::now();
        return !serialized_ && inner_->AllowsConcurrentLoad();
    }
    std::vector<kb::assets::AssetId> DiscoverDependencies(const kb::assets::AssetMetadata& metadata,
        const kb::assets::AssetRegistry& registry) const override { return inner_->DiscoverDependencies(metadata, registry); }
    std::optional<std::string> ValidateRuntimeDependencies(const kb::assets::AssetLoadRequest& request,
        const kb::assets::AssetRegistry& registry) const override {
        // Includes the loader mutex wait and the small manager bookkeeping interval around it.
        profile_.gateNanoseconds.fetch_add(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - gateStart_).count()));
        return inner_->ValidateRuntimeDependencies(request, registry);
    }
    kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override {
        const auto start = Clock::now();
        Peak(profile_.peak, profile_.active.fetch_add(1U) + 1U);
        auto result = inner_->Load(request);
        profile_.active.fetch_sub(1U);
        profile_.loadNanoseconds.fetch_add(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()));
        return result;
    }
private:
    std::unique_ptr<IAssetLoader> inner_;
    Profile& profile_;
    bool serialized_;
    static inline thread_local Clock::time_point gateStart_{};
};

void Register(kb::assets::AssetManager& manager, Profile& profile, bool serialized) {
    Require(manager.RegisterLoader(std::make_unique<kb::assets::ImportedAssetLoader>()), "Import container registration failed");
    Require(manager.RegisterLoader(std::make_unique<ProfiledLoader>(
        std::make_unique<kb::render::RenderTextureAssetLoader>(bgfx::RendererType::Noop), profile, serialized)) &&
        manager.RegisterLoader(std::make_unique<ProfiledLoader>(
            std::make_unique<kb::render::RenderMeshAssetLoader>(), profile, serialized)), "Loader registration failed");
}

struct Asset {
    kb::assets::AssetMetadata metadata;
    unsigned group = 0U;
    fs::path source;
};

void CheckPayload(kb::assets::AssetManager& manager, const Asset& asset) {
    if (asset.metadata.type == "RenderTexture") {
        const auto texture = manager.AcquireLoaded<kb::render::RenderTextureAssetData>(asset.metadata.id);
        Require(texture.IsLoaded(), manager.AsyncLoadError(asset.metadata.id).c_str());
        Require(texture.IsLoaded() && texture->width == kSide && texture->height == kSide, "Texture dimensions changed");
        const auto& pixels = texture->gpuBlocks ? texture->gpuBlocks->blocks : texture->rgba8;
        Require(pixels.size() == static_cast<std::size_t>(kSide) * kSide * 4U, "Texture payload size changed");
        for (std::size_t pixel = 0U; pixel < pixels.size(); pixel += 4U)
            Require(pixels[pixel] == 32U + asset.group * 17U && pixels[pixel + 1U] == 64U + asset.group * 9U &&
                pixels[pixel + 2U] == 96U + asset.group * 3U && pixels[pixel + 3U] == 255U, "Concurrent texture payloads mixed");
    } else {
        const auto mesh = manager.AcquireLoaded<kb::render::RenderMeshAssetData>(asset.metadata.id);
        Require(mesh.IsLoaded(), manager.AsyncLoadError(asset.metadata.id).c_str());
        Require(mesh.IsLoaded() && !mesh->tangentVertices.empty() && !mesh->sections.empty(), "Mesh payload disappeared");
        for (const auto& vertex : mesh->tangentVertices)
            Require(vertex.y >= static_cast<float>(asset.group) - 0.0001F &&
                vertex.y <= static_cast<float>(asset.group + 1U) + 0.0001F, "Concurrent mesh payloads mixed");
    }
}

void LoadBatch(const fs::path& assetsRoot, const std::shared_ptr<bake::RuntimeAssetPack>& pack,
    const std::vector<Asset>& assets, bool serialized, const char* name) {
    Profile profile;
    kb::assets::AssetManager manager;
    Register(manager, profile, serialized);
    Require(manager.SetAsyncLoadConcurrency(2U), "Load concurrency could not be configured");
    if (pack) Require(manager.MountRuntimePack(pack), "Runtime pack could not be registered");
    else {
        Require(manager.Mounts().Mount("Game", assetsRoot) && manager.DiscoverMountedAssets() == assets.size(), "Imported assets were not discovered");
        // Image containers are generic imports; request their typed runtime texture view.
        for (const auto& asset : assets) if (asset.metadata.type == "RenderTexture") {
            const auto* discovered = manager.Registry().FindByPath(asset.metadata.virtualPath);
            Require(discovered && discovered->importCategory == "Texture", "Imported texture identity changed");
            auto metadata = *discovered;
            Require(manager.Registry().Remove(metadata.id), "Imported view removal failed");
            metadata.id = asset.metadata.id;
            metadata.type = "RenderTexture";
            Require(manager.RegisterAsset(std::move(metadata)), "Runtime texture view registration failed");
        }
    }
    const auto start = Clock::now();
    for (const auto& asset : assets) Require(manager.RequestLoadAsync(asset.metadata.id), "Load request failed");
    const auto deadline = start + std::chrono::seconds(30);
    for (;;) {
        manager.PumpAsyncLoads();
        const bool pending = std::ranges::any_of(assets, [&](const auto& asset) {
            return manager.AsyncLoadStatus(asset.metadata.id) == kb::assets::AsyncAssetLoadStatus::Pending;
        });
        if (!pending) break;
        Require(Clock::now() < deadline, "Concurrent asset load timed out");
        std::this_thread::yield();
    }
    const double milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    for (const auto& asset : assets) CheckPayload(manager, asset);
    const auto stats = manager.AsyncLoadConcurrency();
    Require(profile.peak == (serialized ? 1U : 2U) && stats.peakSimultaneousLoads == profile.peak,
        "Actual decoder concurrency does not match the loader contract");
    Require(serialized ? stats.serializedLoads == assets.size() : stats.concurrentLoads == assets.size(), "Loader gate counts changed");
    std::printf("{\"case\":\"%s\",\"assets\":%zu,\"elapsedMs\":%.4f,\"decoderPeak\":%u,\"gateWaitNs\":%llu,\"loaderWallNs\":%llu}\n",
        name, assets.size(), milliseconds, profile.peak.load(), static_cast<unsigned long long>(profile.gateNanoseconds.load()),
        static_cast<unsigned long long>(profile.loadNanoseconds.load()));
}

void WriteArtifact(bake::AssetPackWriter& writer, const bake::BakeTargetProfile& profile,
    const bake::AssetBakeKey& key, std::string_view type, std::span<const std::uint8_t> bytes) {
    Require(writer.BeginAsset({.key = key, .assetTypeId = std::string{type}}) == bake::BakedAssetSinkStatus::Success &&
        writer.WritePrimaryBlock(bytes, profile.packageBlockAlignmentBytes) == bake::BakedAssetSinkStatus::Success &&
        writer.CommitAsset() == bake::BakedAssetSinkStatus::Success, "Package artifact could not be written");
}

std::shared_ptr<bake::RuntimeAssetPack> Cook(const fs::path& root, const std::vector<Asset>& assets) {
    const auto profile = bake::WindowsX64BakeTargetProfile();
    const auto path = root / "Assets.kbpack";
    bake::AssetPackWriter writer{path, profile};
    bake::RuntimeAssetManifest manifest{.targetProfileId = std::string{profile.identifier},
        .targetProfileHash = bake::BakeTargetProfileFingerprint(profile)};
    manifest.settings.name = "AssetLoading";
    manifest.settings.defaultMap = "/Game/Scenes/Main.21kbscene";
    manifest.descriptor.targetPlatforms = {"Windows.x64"};
    const auto key = [&](std::string_view type, std::span<const std::uint8_t> bytes) {
        return bake::AssetBakeKey{.sourceContentHash = bake::HashBakeBytes(bytes), .bakerId = std::string{type},
            .bakerVersion = "1", .targetProfileId = std::string{profile.identifier}, .targetProfileHash = manifest.targetProfileHash};
    };
    kb::scene::SceneDocument scene;
    scene.name = "AssetLoading";
    scene.guid = "scene:AssetLoading";
    Require(kb::scene::SceneDocumentService::Save(scene, root / "Main.21kbscene"), "Scene fixture could not be saved");
    const auto sceneBytes = Read(root / "Main.21kbscene");
    const auto sceneKey = key("RuntimeSource", sceneBytes);
    std::vector<std::uint8_t> blob;
    Require(bake::EncodeRuntimeSourceBlob(sceneBytes, blob), "Scene source could not be encoded");
    WriteArtifact(writer, profile, sceneKey, bake::kSourceAssetTypeId, blob);
    manifest.assets.push_back({.id = kb::assets::MakeAssetId("/Game/Scenes/Main.21kbscene:Scene"), .type = "Scene", .name = "Main",
        .virtualPath = manifest.settings.defaultMap, .sourceExtension = ".21kbscene", .contentHash = sceneKey.sourceContentHash,
        .artifacts = {{.digest = sceneKey.Digest(), .encoding = bake::RuntimeArtifactEncoding::SourceBytes}}});
    for (const auto& asset : assets) {
        bake::RuntimeAssetManifestEntry entry{.id = asset.metadata.id, .type = asset.metadata.type,
            .name = asset.metadata.name, .virtualPath = asset.metadata.virtualPath.generic_string(), .sourceExtension = asset.source.extension().string()};
        if (asset.metadata.type == "RenderTexture") {
            const auto output = kb::render::bake::BakeTextureBytes(Read(asset.source), {.preserveAtlas = true}, profile,
                bake::TextureCompressionFamily::BlockCompressedBaseline, writer);
            Require(output.status == kb::render::bake::TextureBakeStatus::Success, "Texture bake failed");
            entry.contentHash = output.key.sourceContentHash;
            entry.artifacts = {{.digest = output.key.Digest(), .encoding = bake::RuntimeArtifactEncoding::BakedTexture, .qualifier = "rgba8-atlas"}};
        } else {
            kb::render::RenderMeshAssetLoader loader;
            const auto loaded = loader.Load({.metadata = asset.metadata, .resolvedPath = asset.metadata.physicalPath});
            Require(loaded.Succeeded(), "Mesh bake source could not be loaded");
            const auto output = kb::render::bake::BakeMesh(*std::static_pointer_cast<kb::render::RenderMeshAssetData>(loaded.asset), profile, writer);
            Require(output.status == kb::render::bake::MeshBakeStatus::Success, "Mesh bake failed");
            entry.contentHash = output.key.sourceContentHash;
            entry.artifacts = {{.digest = output.key.Digest(), .encoding = bake::RuntimeArtifactEncoding::BakedMesh}};
        }
        manifest.assets.push_back(std::move(entry));
    }
    const auto manifestStatus = bake::EncodeRuntimeAssetManifest(manifest, blob);
    Require(manifestStatus == bake::RuntimeAssetManifestStatus::Success,
        ("Package manifest is invalid: " + std::string{bake::ToString(manifestStatus)}).c_str());
    WriteArtifact(writer, profile, key("RuntimeManifest", blob), bake::kRuntimeManifestAssetTypeId, blob);
    Require(writer.Finish() == bake::BakedAssetSinkStatus::Success, "Package could not be completed");
    auto pack = std::make_shared<bake::RuntimeAssetPack>();
    const auto mountStatus = pack->Mount(path, profile);
    Require(mountStatus == bake::RuntimeAssetPackStatus::Success,
        ("Cooked assets could not be mounted: " + std::string{bake::ToString(mountStatus)}).c_str());
    return pack;
}
} // namespace

int main() {
    bgfx::Init renderer;
    renderer.type = bgfx::RendererType::Noop;
    Require(bgfx::init(renderer), "Headless renderer initialization failed");
    const auto temporary = fs::canonical(fs::temp_directory_path());
    const auto root = temporary / ("21kb_asset_loading_" + std::to_string(Clock::now().time_since_epoch().count()));
    const auto assetsRoot = root / "Assets";
    fs::create_directories(root / "Sources");
    std::array<std::vector<kb::assets::AssetImportItemResult>, kGroups> imported;
    const auto importStart = Clock::now();
    std::array<std::jthread, 2U> importers;
    for (unsigned worker = 0U; worker < importers.size(); ++worker) importers[worker] = std::jthread([&, worker] {
        for (unsigned group = worker; group < kGroups; group += static_cast<unsigned>(importers.size())) {
            const auto folder = assetsRoot / ("Group" + std::to_string(group));
            fs::create_directories(folder);
            const std::array files{root / "Sources" / ("Image" + std::to_string(group) + ".bmp"), root / "Sources" / ("Mesh" + std::to_string(group) + ".obj")};
            WriteBmp(files[0], group); WriteObj(files[1], group);
            kb::assets::AssetManager manager;
            Require(manager.Mounts().Mount("Game", assetsRoot), "Import destination could not be mounted");
            const auto result = kb::assets::AssetImportService::ImportFiles(manager, files, "/Game/Group" + std::to_string(group));
            Require(result.Succeeded() && result.items.size() == 2U, "Parallel independent import failed");
            imported[group] = result.items;
        }
    });
    for (auto& worker : importers) worker.join();
    std::printf("{\"case\":\"independent-imports\",\"workers\":2,\"assets\":16,\"elapsedMs\":%.4f}\n",
        std::chrono::duration<double, std::milli>(Clock::now() - importStart).count());
    std::vector<Asset> assets;
    {
        Profile profile;
        kb::assets::AssetManager manager;
        Register(manager, profile, false);
        Require(manager.Mounts().Mount("Game", assetsRoot) && manager.DiscoverMountedAssets() == kGroups * 2U, "Imported registry could not be rebuilt");
        for (unsigned group = 0U; group < kGroups; ++group) for (const auto& item : imported[group]) {
            const auto* metadata = manager.Registry().Find(item.id);
            Require(metadata != nullptr, "Imported asset identity was lost");
            auto runtimeMetadata = *metadata;
            if (runtimeMetadata.importCategory == "Texture") {
                runtimeMetadata.type = "RenderTexture";
                runtimeMetadata.id = kb::assets::MakeAssetId(runtimeMetadata.virtualPath.generic_string() + ":RenderTexture");
            }
            assets.push_back({std::move(runtimeMetadata), group, item.sourcePath});
        }
    }
    {
        kb::render::RenderMeshAssetLoader loader;
        kb::assets::AssetMetadata metadata{.type = "RenderMesh", .virtualPath = "/Game/Cancelled.obj", .sourceExtension = ".obj"};
        auto cancellation = std::make_shared<kb::assets::AssetLoadCancellation>();
        cancellation->requested = true;
        const auto result = loader.Load({.metadata = metadata, .resolvedPath = root / "Sources/Mesh0.obj", .cancellation = cancellation});
        Require(!result.Succeeded() && cancellation->observed.load() && result.error.find("cancelled") != std::string::npos,
            "OBJ cancellation did not stop its reader");
    }
    // Different physical paths keep both comparisons cold in the decoded texture cache.
    const auto parallelRoot = root / "ParallelAssets";
    fs::copy(assetsRoot, parallelRoot, fs::copy_options::recursive);
    LoadBatch(assetsRoot, {}, assets, true, "loose-serialized-cold");
    LoadBatch(parallelRoot, {}, assets, false, "loose-concurrent-cold");
    auto pack = Cook(root, assets);
    Require(assetsRoot.parent_path() == root && root.parent_path() == temporary, "Source isolation escaped the fixture root");
    fs::rename(assetsRoot, root / "AssetsUnavailable");
    fs::rename(parallelRoot, root / "ParallelAssetsUnavailable");
    fs::rename(root / "Sources", root / "SourcesUnavailable");
    LoadBatch({}, pack, assets, false, "cooked-concurrent-source-hidden");
    pack.reset();
    bgfx::shutdown();
    Require(root.parent_path() == temporary && root.filename().string().starts_with("21kb_asset_loading_"), "Cleanup escaped the fixture root");
    fs::remove_all(root);
    std::puts("{\"result\":\"PASS\",\"sourceUnavailable\":true}");
}
