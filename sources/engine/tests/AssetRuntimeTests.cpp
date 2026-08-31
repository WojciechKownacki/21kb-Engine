#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/audio/AudioClipAsset.hpp"
#include "engine/audio/AudioClipAssetLoader.hpp"
#include "engine/audio/AudioClipFormats.hpp"
#include "engine/assets/AssetImportCatalog.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetKind.hpp"
#include "engine/assets/AssetCompatibility.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/assets/ImportedAsset.hpp"
#include "engine/assets/ImportedAssetLoader.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssetMeta.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/GeometrySwarmComponent.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/script/ScriptAsset.hpp"
#include "engine/script/ScriptBehaviourAsset.hpp"
#include "engine/script/ScriptBehaviourBindingService.hpp"
#include "engine/visual/VisualGraphTypes.hpp"
#include "scene/assets/SceneAssetLoader.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <thread>
#include <vector>

namespace {

class TextAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override {
        return "Text";
    }

    [[nodiscard]] std::type_index PayloadType() const noexcept override {
        return typeid(std::string);
    }

    [[nodiscard]] std::vector<std::string> Extensions() const override {
        return { ".txt", ".text" };
    }

    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override {
        std::ifstream input{ request.resolvedPath, std::ios::binary };
        if (!input.is_open()) {
            return kb::assets::AssetLoadResult{ .asset = {}, .error = "Text file could not be opened" };
        }

        std::string content{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
        return kb::assets::AssetLoadResult{ .asset = std::make_shared<std::string>(std::move(content)), .error = {} };
    }
};

struct AsyncLoaderGate {
    std::atomic_bool entered{ false };
    std::shared_future<void> release{};
};

class GatedTextAssetLoader final : public kb::assets::IAssetLoader {
public:
    explicit GatedTextAssetLoader(std::shared_ptr<AsyncLoaderGate> gate)
        : gate_{ std::move(gate) } {}

    [[nodiscard]] std::string_view Type() const noexcept override { return "GatedText"; }
    [[nodiscard]] std::type_index PayloadType() const noexcept override { return typeid(std::string); }
    [[nodiscard]] std::vector<std::string> Extensions() const override { return { ".gated" }; }

    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override {
        gate_->entered.store(true, std::memory_order_release);
        gate_->release.wait();
        std::ifstream input{ request.resolvedPath, std::ios::binary };
        if (!input.is_open()) {
            return { .asset = {}, .error = "Gated text file could not be opened" };
        }
        std::string content{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
        return { .asset = std::make_shared<std::string>(std::move(content)), .error = {} };
    }

private:
    std::shared_ptr<AsyncLoaderGate> gate_{};
};

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_engine_asset_runtime_tests";
}

void ResetTestRoot() {
    std::error_code error;
    std::filesystem::remove_all(TestRoot(), error);
    std::filesystem::create_directories(TestRoot(), error);
    kb::tests::Require(!error, "Asset runtime test root could not be prepared");
}

void WriteTextFile(const std::filesystem::path& path, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    kb::tests::Require(!error, "Asset runtime test directory could not be created");

    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    kb::tests::Require(output.is_open(), "Asset runtime test file could not be opened");
    output << text;
    kb::tests::Require(output.good(), "Asset runtime test file could not be written");
}

void RunAssetManagerDiscoveryCacheAndManifestTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "Project";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    WriteTextFile(assetsRoot / "Text" / "Greeting.txt", "hello runtime asset");

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<TextAssetLoader>()), "Text asset loader registration failed");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Game asset mount failed");
    kb::tests::Require(manager.DiscoverMountedAssets() == 1, "Mounted asset discovery did not find the text asset");
    kb::tests::Require(manager.Registry().Count() == 1, "Asset registry did not store the discovered asset");

    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/Text/Greeting.txt");
    kb::tests::Require(metadata != nullptr, "Discovered asset could not be resolved by virtual path");
    kb::tests::Require(metadata->id.IsValid(), "Discovered asset did not receive a stable id");
    kb::tests::Require(metadata->type == "Text", "Discovered asset type was not set from its loader");
    kb::tests::Require(metadata->contentHash != 0, "Discovered asset did not receive a content hash");

    const kb::assets::AssetHandle<std::string> loaded = manager.Load<std::string>(metadata->id);
    kb::tests::Require(loaded.IsLoaded(), "Text asset did not load through the runtime asset manager");
    kb::tests::Require(*loaded.Get() == "hello runtime asset", "Text asset payload was not preserved");
    kb::tests::Require(manager.LoadedCount() == 1, "Runtime asset cache did not retain the loaded asset");

    const kb::assets::AssetId textAssetId = metadata->id;
    const std::uint64_t oldContentHash = metadata->contentHash;
    WriteTextFile(assetsRoot / "Text" / "Greeting.txt", "updated runtime asset");
    kb::tests::Require(manager.DiscoverMountedAssets() == 1, "Mounted asset rediscovery did not update the changed text asset");
    metadata = manager.Registry().Find(textAssetId);
    kb::tests::Require(metadata != nullptr && metadata->contentHash != oldContentHash, "Asset rediscovery did not refresh the content hash");
    kb::tests::Require(!manager.IsLoaded(textAssetId), "Asset rediscovery did not invalidate cached payload after content change");
    const kb::assets::AssetHandle<std::string> reloaded = manager.Load<std::string>(textAssetId);
    kb::tests::Require(reloaded.IsLoaded() && *reloaded.Get() == "updated runtime asset", "Asset manager did not reload changed file content");

    const kb::assets::AssetHandle<int> wrongType = manager.Load<int>(metadata->id);
    kb::tests::Require(!wrongType.IsLoaded(), "Asset manager accepted a mismatched typed load");
    kb::tests::Require(!manager.LastError().empty(), "Asset manager did not report a mismatched typed load error");

    kb::tests::Require(manager.Unload(metadata->id), "Runtime asset cache did not unload the asset");
    kb::tests::Require(!manager.IsLoaded(metadata->id), "Runtime asset cache still reported the asset as loaded after unload");

    const std::filesystem::path manifestPath = TestRoot() / "AssetManifest.kbassets";
    kb::tests::Require(kb::assets::AssetManifest::Save(manifestPath, manager.Registry()), "Asset manifest save failed");

    kb::assets::AssetRegistry restored;
    kb::tests::Require(kb::assets::AssetManifest::Load(manifestPath, restored), "Asset manifest load failed");
    const kb::assets::AssetMetadata* restoredMetadata = restored.FindByPath("/Game/Text/Greeting.txt");
    kb::tests::Require(restoredMetadata != nullptr, "Restored asset manifest did not index the virtual path");
    kb::tests::Require(restoredMetadata->id == metadata->id, "Restored asset manifest did not preserve stable asset id");
    kb::tests::Require(restoredMetadata->contentHash == metadata->contentHash, "Restored asset manifest did not preserve content hash");
    kb::tests::Require(restoredMetadata->importCategory == metadata->importCategory, "Restored asset manifest did not preserve import category");
}

void RunSingleAssetRefreshTest() {
    ResetTestRoot();

    const std::filesystem::path assetsRoot = TestRoot() / "RefreshProject" / "Assets";
    const std::filesystem::path assetPath = assetsRoot / "Text" / "Refresh.txt";
    WriteTextFile(assetPath, "before");

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<TextAssetLoader>()),
        "Single asset refresh loader registration failed");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot),
        "Single asset refresh mount failed");
    kb::tests::Require(manager.DiscoverMountedAssets() == 1U,
        "Single asset refresh discovery failed");
    const kb::assets::AssetMetadata* before =
        manager.Registry().FindByPath("/Game/Text/Refresh.txt");
    kb::tests::Require(before != nullptr, "Single asset refresh fixture was not registered");
    const kb::assets::AssetId id = before->id;
    const std::uint64_t previousHash = before->contentHash;
    kb::tests::Require(manager.Load<std::string>(id).IsLoaded(),
        "Single asset refresh fixture did not enter the runtime cache");

    WriteTextFile(assetPath, "after");
    kb::tests::Require(manager.RefreshAsset(id), "Single asset refresh failed");
    const kb::assets::AssetMetadata* after = manager.Registry().Find(id);
    kb::tests::Require(after != nullptr && after->contentHash != previousHash,
        "Single asset refresh did not update the content hash");
    kb::tests::Require(!manager.IsLoaded(id),
        "Single asset refresh did not invalidate the stale cached payload");
    const kb::assets::AssetHandle<std::string> reloaded = manager.Load<std::string>(id);
    kb::tests::Require(reloaded.IsLoaded() && *reloaded == "after",
        "Single asset refresh did not expose the newly written payload");
}

void RunAssetManagerRuntimePublicationTest() {
    ResetTestRoot();

    const std::filesystem::path assetsRoot = TestRoot() / "RuntimePublication" / "Assets";
    WriteTextFile(assetsRoot / "Working.txt", "canonical");
    kb::assets::AssetManager manager;
    kb::tests::Require(
        manager.RegisterLoader(std::make_unique<TextAssetLoader>()),
        "Runtime publication text loader registration failed");
    kb::tests::Require(
        manager.Mounts().Mount("Game", assetsRoot),
        "Runtime publication asset mount failed");
    kb::tests::Require(
        manager.DiscoverMountedAssets() == 1U,
        "Runtime publication fixture was not discovered");
    const kb::assets::AssetMetadata* metadata =
        manager.Registry().FindByPath("/Game/Working.txt");
    kb::tests::Require(metadata != nullptr, "Runtime publication asset metadata is missing");
    const kb::assets::AssetId id = metadata->id;
    const std::uint64_t generation = manager.LoadGeneration(id);

    kb::tests::Require(
        manager.PublishRuntimeAsset<std::string>(
            id, std::make_shared<std::string>("working copy")),
        "Matching runtime payload could not be published");
    const kb::assets::AssetHandle<std::string> preview = manager.Load<std::string>(id);
    kb::tests::Require(
        preview.IsLoaded() && *preview.Get() == "working copy",
        "A canonical load did not observe the published runtime payload");
    kb::tests::Require(
        manager.LoadGeneration(id) > generation,
        "Runtime publication did not invalidate an older async generation");
    kb::tests::Require(
        !manager.PublishRuntimeAsset<int>(id, std::make_shared<int>(7)),
        "Runtime publication accepted a payload type that does not match the loader");

    kb::tests::Require(manager.Unload(id), "Published runtime payload could not be unloaded");
    const kb::assets::AssetHandle<std::string> canonical = manager.Load<std::string>(id);
    kb::tests::Require(
        canonical.IsLoaded() && *canonical.Get() == "canonical",
        "Unloading a runtime publication did not restore canonical disk loading");
}

// LIB-155: AssetManager::LoadOpaque — the type-erased force-load the
// generic script-facing Assets.Load surface needs, because kb_engine cannot
// name a compile-time payload T for asset kinds whose C++ type lives in
// another module (kb_render's RenderMesh/Material/Texture). Proves it
// reaches the exact same cache as Load<T> (LoadedCount/IsLoaded agree) and
// fails honestly (LastError set, no crash) for every non-happy path: an
// invalid id, an unregistered id, and a registered id whose type has no
// loader.
void RunAssetManagerLoadOpaqueTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "OpaqueProject";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    WriteTextFile(assetsRoot / "Text" / "Opaque.txt", "opaque load payload");

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<TextAssetLoader>()), "Text asset loader registration failed");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Game asset mount failed");
    kb::tests::Require(manager.DiscoverMountedAssets() == 1, "Mounted asset discovery did not find the text asset");
    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/Text/Opaque.txt");
    kb::tests::Require(metadata != nullptr, "Discovered opaque-load asset could not be resolved by virtual path");
    const kb::assets::AssetId textAssetId = metadata->id;

    kb::tests::Require(!manager.LoadOpaque(kb::assets::AssetId{}), "LoadOpaque must reject an invalid asset id");
    kb::tests::Require(!manager.LastError().empty(), "LoadOpaque must report an error for an invalid asset id");

    const kb::assets::AssetId unknownId{ textAssetId.value + 999999U };
    kb::tests::Require(!manager.LoadOpaque(unknownId), "LoadOpaque must reject an id with no registered metadata");
    kb::tests::Require(!manager.LastError().empty(), "LoadOpaque must report an error for an unregistered id");

    const kb::assets::AssetId noLoaderId{ textAssetId.value + 1U };
    kb::tests::Require(manager.RegisterAsset(kb::assets::AssetMetadata{
                           .id = noLoaderId,
                           .type = "GhostType",
                           .name = "GhostAsset",
                           .virtualPath = "/Game/Text/Ghost.ghost",
                           .physicalPath = "Ghost.ghost",
                           .contentHash = 1U,
                       }),
        "Registration of the no-loader fixture asset failed");
    kb::tests::Require(!manager.LoadOpaque(noLoaderId), "LoadOpaque must reject an asset type with no registered loader");
    kb::tests::Require(!manager.LastError().empty(), "LoadOpaque must report an error when no loader is registered for the asset type");

    kb::tests::Require(!manager.IsLoaded(textAssetId), "Opaque-load asset must not be loaded before LoadOpaque is called");
    kb::tests::Require(manager.LoadOpaque(textAssetId), "LoadOpaque must succeed for a registered id with a matching loader");
    kb::tests::Require(manager.IsLoaded(textAssetId), "LoadOpaque must populate the SAME cache Load<T>/IsLoaded observe");
    kb::tests::Require(manager.LoadedCount() == 1, "LoadOpaque must add exactly one cache entry");

    kb::tests::Require(manager.LoadOpaque(textAssetId), "LoadOpaque must be idempotent for an already-cached asset");
    kb::tests::Require(manager.LoadedCount() == 1, "A second LoadOpaque on an already-cached asset must not duplicate the cache entry");

    const kb::assets::AssetHandle<std::string> viaTypedLoad = manager.Load<std::string>(textAssetId);
    kb::tests::Require(viaTypedLoad.IsLoaded() && *viaTypedLoad.Get() == "opaque load payload", "LoadOpaque's cache entry must be reachable through the existing typed Load<T> path");

    kb::tests::Require(manager.Unload(textAssetId), "Unload must remove the cache entry LoadOpaque populated");
    kb::tests::Require(!manager.IsLoaded(textAssetId), "Asset must report unloaded after Unload following a LoadOpaque");
    kb::tests::Require(manager.LoadOpaque(textAssetId), "LoadOpaque must be able to reload an asset after Unload");
    kb::tests::Require(manager.IsLoaded(textAssetId), "Reload through LoadOpaque after Unload must repopulate the cache");
}

void RunAssetManagerTrueAsyncLoadTest() {
    ResetTestRoot();
    const std::filesystem::path assetsRoot = TestRoot() / "AsyncProject" / "Assets";
    WriteTextFile(assetsRoot / "Text" / "Deferred.gated", "background payload");
    WriteTextFile(assetsRoot / "Text" / "Queued.gated", "queued payload");

    std::promise<void> releasePromise;
    auto gate = std::make_shared<AsyncLoaderGate>();
    gate->release = releasePromise.get_future().share();

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<GatedTextAssetLoader>(gate)), "Gated loader registration failed");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Async project mount failed");
    kb::tests::Require(manager.DiscoverMountedAssets() == 2U, "Async asset discovery failed");
    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/Text/Deferred.gated");
    const kb::assets::AssetMetadata* queuedMetadata = manager.Registry().FindByPath("/Game/Text/Queued.gated");
    kb::tests::Require(metadata != nullptr, "Async asset metadata was not registered");
    kb::tests::Require(queuedMetadata != nullptr, "Queued async asset metadata was not registered");
    const kb::assets::AssetId id = metadata->id;

    kb::tests::Require(manager.LoadAsync<std::string>(id), "Typed async load request was rejected");
    kb::tests::Require(manager.AsyncLoadStatus(id) == kb::assets::AsyncAssetLoadStatus::Pending,
        "Async load must remain pending while the loader is blocked");
    kb::tests::Require(!manager.IsLoaded(id), "Async request committed a payload before the worker completed");

    for (std::size_t spin = 0; spin < 1000000U && !gate->entered.load(std::memory_order_acquire); ++spin) {
        std::this_thread::yield();
    }
    const bool workerEntered = gate->entered.load(std::memory_order_acquire);
    if (!workerEntered) {
        releasePromise.set_value();
    }
    kb::tests::Require(workerEntered, "Async loader never ran on the worker");
    kb::tests::Require(manager.LoadAsync<std::string>(queuedMetadata->id),
        "A second async request blocked behind the active loader instead of entering the bounded queue");
    manager.PumpAsyncLoads();
    kb::tests::Require(manager.AsyncLoadStatus(id) == kb::assets::AsyncAssetLoadStatus::Pending,
        "Owner-thread pump blocked or fabricated completion while I/O was pending");

    releasePromise.set_value();
    for (std::size_t spin = 0; spin < 1000000U && manager.AsyncLoadStatus(id) == kb::assets::AsyncAssetLoadStatus::Pending; ++spin) {
        manager.PumpAsyncLoads();
        std::this_thread::yield();
    }
    kb::tests::Require(manager.AsyncLoadStatus(id) == kb::assets::AsyncAssetLoadStatus::Completed,
        "Async worker result was not committed on the owner thread");
    for (std::size_t spin = 0; spin < 1000000U &&
            manager.AsyncLoadStatus(queuedMetadata->id) == kb::assets::AsyncAssetLoadStatus::Pending;
         ++spin) {
        manager.PumpAsyncLoads();
        std::this_thread::yield();
    }
    kb::tests::Require(manager.AsyncLoadStatus(queuedMetadata->id) == kb::assets::AsyncAssetLoadStatus::Completed,
        "The bounded async queue did not execute its second request");
    const kb::assets::AssetHandle<std::string> typedOwner = manager.AcquireLoaded<std::string>(id);
    kb::tests::Require(typedOwner.IsLoaded() && *typedOwner == "background payload",
        "Typed async acquire did not return the decoded payload");
    kb::assets::AssetOpaqueHandle owner = manager.AcquireOpaqueHandle(id);
    kb::tests::Require(owner.IsLoaded(), "Completed async payload did not expose a strong ownership handle");
    kb::tests::Require(manager.ReferenceCount(id) == 2U, "Typed and opaque async ownership were not reflected by the cache");
    kb::tests::Require(manager.Unload(id), "Async cache entry could not be unloaded");
    kb::tests::Require(owner.IsLoaded(), "Strong owner did not preserve the payload after cache unload");

    WriteTextFile(assetsRoot / "Text" / "Cancelled.gated", "must not commit");
    std::promise<void> cancelReleasePromise;
    auto cancelGate = std::make_shared<AsyncLoaderGate>();
    cancelGate->release = cancelReleasePromise.get_future().share();
    kb::assets::AssetManager cancelManager;
    kb::tests::Require(cancelManager.RegisterLoader(std::make_unique<GatedTextAssetLoader>(cancelGate)), "Cancellation loader registration failed");
    kb::tests::Require(cancelManager.Mounts().Mount("Game", assetsRoot), "Cancellation project mount failed");
    kb::tests::Require(cancelManager.DiscoverMountedAssets() == 3U, "Cancellation assets were not discovered");
    const kb::assets::AssetMetadata* cancelMetadata = cancelManager.Registry().FindByPath("/Game/Text/Cancelled.gated");
    kb::tests::Require(cancelMetadata != nullptr && cancelManager.RequestLoadAsync(cancelMetadata->id), "Cancellation request could not start");
    for (std::size_t spin = 0; spin < 1000000U && !cancelGate->entered.load(std::memory_order_acquire); ++spin) {
        std::this_thread::yield();
    }
    const bool cancellationWorkerEntered = cancelGate->entered.load(std::memory_order_acquire);
    static_cast<void>(cancelManager.Unload(cancelMetadata->id));
    cancelReleasePromise.set_value();
    kb::tests::Require(cancellationWorkerEntered, "Cancellation loader never entered its worker");
    for (std::size_t spin = 0; spin < 1000000U && cancelManager.AsyncLoadStatus(cancelMetadata->id) == kb::assets::AsyncAssetLoadStatus::Pending; ++spin) {
        cancelManager.PumpAsyncLoads();
        std::this_thread::yield();
    }
    kb::tests::Require(cancelManager.AsyncLoadStatus(cancelMetadata->id) == kb::assets::AsyncAssetLoadStatus::NotRequested &&
            !cancelManager.IsLoaded(cancelMetadata->id),
        "Unload during an async request must invalidate the worker result before owner-thread commit");
}

void RunAssetManagerAsyncLoaderReplacementTest() {
    ResetTestRoot();
    const std::filesystem::path assetsRoot = TestRoot() / "LoaderReplacementProject" / "Assets";
    WriteTextFile(assetsRoot / "Text" / "Reloaded.txt", "replacement payload");

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<TextAssetLoader>()), "Initial replacement-test loader registration failed");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Replacement-test project mount failed");
    kb::tests::Require(manager.DiscoverMountedAssets() == 1U, "Replacement-test asset discovery failed");
    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/Text/Reloaded.txt");
    kb::tests::Require(metadata != nullptr, "Replacement-test asset metadata was not registered");
    const kb::assets::AssetId id = metadata->id;

    kb::tests::Require(manager.RequestLoadAsync(id), "Replacement-test async request was rejected");
    kb::tests::Require(manager.AsyncLoadStatus(id) == kb::assets::AsyncAssetLoadStatus::Pending,
        "Replacement-test async request was not pending before loader replacement");
    kb::tests::Require(manager.RegisterLoader(std::make_unique<TextAssetLoader>()),
        "Replacement-test same-type loader registration failed");
    for (std::size_t spin = 0; spin < 1000000U && manager.AsyncLoadStatus(id) == kb::assets::AsyncAssetLoadStatus::Pending; ++spin) {
        manager.PumpAsyncLoads();
        std::this_thread::yield();
    }
    kb::tests::Require(manager.AsyncLoadStatus(id) == kb::assets::AsyncAssetLoadStatus::Completed,
        "Replacing a loader discarded an active async request instead of restarting it");
    const kb::assets::AssetHandle<std::string> loaded = manager.AcquireLoaded<std::string>(id);
    kb::tests::Require(loaded.IsLoaded() && *loaded == "replacement payload",
        "Restarted async request did not publish the replacement loader payload");
}

void RunAssetManagerNewLoaderPreservesRetainedAssetsTest() {
    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<TextAssetLoader>()),
        "Retained-asset test could not register its initial loader");
    constexpr kb::assets::AssetId textId{ 0xA551E7U };
    kb::tests::Require(manager.RegisterAsset({
            .id = textId,
            .type = "Text",
            .name = "Published before plug-in discovery",
            .virtualPath = "/Game/Published.txt",
            .runtimeLoadable = true,
        }) && manager.PublishRuntimeAsset(textId, std::make_shared<std::string>("resident")),
        "Retained-asset test could not publish its payload");

    const auto gate = std::make_shared<AsyncLoaderGate>();
    kb::tests::Require(manager.RegisterLoader(std::make_unique<GatedTextAssetLoader>(gate)),
        "Retained-asset test could not add an unrelated loader type");
    const kb::assets::AssetHandle<std::string> retained = manager.AcquireLoaded<std::string>(textId);
    kb::tests::Require(retained.IsLoaded() && *retained == "resident",
        "Adding an unrelated loader evicted a retained runtime asset");
}

// LIB-157: kb::assets::AssetKind — the single source of truth mapping each
// typed-reference kind to its concrete AssetMetadata::type string(s). Pure
// value-type test, no scene needed. Proves ToString/TryParseAssetKind round
// trip for every kind, AssetMatchesKind accepts the right type and rejects a
// foreign one, the Audio kind's dual acceptance (native AudioClip AND
// imported-media ImportedAsset+category), TryClassifyAssetKind's unambiguous
// reverse classification, and honest rejection of an unrecognised name / an
// unclassifiable asset type.
// LIB-158: the runtime cache reference-count, weak-reference and unload
// policy contract. Uses the same TextAssetLoader fixture as the LoadOpaque
// test. Proves: the default Retain policy keeps a payload resident with no
// live handle (pre-LIB-158 behaviour unchanged); ReferenceCount tracks live
// AssetHandle holders; a WeakAssetHandle observes without extending
// lifetime; ReleaseWhenUnreferenced frees the payload the moment the last
// handle drops (IsLoaded then false, a reload succeeds); PruneUnreferenced
// sweeps the dead entry; and LoadedCount stays honest across all of it.
void RunAssetCacheReferenceAndPolicyTest() {
    ResetTestRoot();
    const std::filesystem::path assetsRoot = TestRoot() / "CacheProject" / "Assets";
    WriteTextFile(assetsRoot / "Text" / "Cached.txt", "cache policy payload");

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<TextAssetLoader>()), "Text asset loader registration failed");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Game asset mount failed");
    kb::tests::Require(manager.DiscoverMountedAssets() == 1, "Mounted asset discovery did not find the text asset");
    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/Text/Cached.txt");
    kb::tests::Require(metadata != nullptr, "Cache policy fixture asset could not be resolved");
    const kb::assets::AssetId id = metadata->id;

    // Default policy is Retain: a fresh Load caches the payload; even after
    // the returned handle is dropped, the cache keeps it resident (the
    // pre-LIB-158 behaviour) and the external ReferenceCount is 0.
    kb::tests::Require(manager.UnloadPolicy(id) == kb::assets::AssetUnloadPolicy::Retain, "An uncached asset must report the default Retain policy");
    kb::tests::Require(manager.ReferenceCount(id) == 0, "ReferenceCount of an uncached asset must be 0");
    {
        const kb::assets::AssetHandle<std::string> handle = manager.Load<std::string>(id);
        kb::tests::Require(handle.IsLoaded(), "Retain Load must succeed");
        kb::tests::Require(manager.ReferenceCount(id) == 1, "One live AssetHandle must give an external ReferenceCount of 1 (the cache's own Retain reference is not counted)");
        const kb::assets::AssetHandle<std::string> second = manager.Load<std::string>(id);
        kb::tests::Require(manager.ReferenceCount(id) == 2, "A second live handle must raise the external ReferenceCount to 2");
    }
    kb::tests::Require(manager.IsLoaded(id) && manager.ReferenceCount(id) == 0, "Under Retain, the payload must stay cached after every external handle drops, with ReferenceCount back to 0");
    kb::tests::Require(manager.LoadedCount() == 1, "LoadedCount must report the single retained asset");

    // A weak handle observes without extending lifetime.
    const kb::assets::WeakAssetHandle<std::string> weak = manager.WeakHandle<std::string>(id);
    kb::tests::Require(!weak.Expired() && weak.Id() == id, "WeakHandle of a cached asset must be live and carry the asset id");
    kb::tests::Require(manager.ReferenceCount(id) == 0, "A WeakAssetHandle must NOT contribute to the strong ReferenceCount");
    {
        const kb::assets::AssetHandle<std::string> locked = weak.Lock();
        kb::tests::Require(locked.IsLoaded() && *locked.Get() == "cache policy payload", "Locking a live WeakAssetHandle must yield the payload");
        kb::tests::Require(manager.ReferenceCount(id) == 1, "A locked weak handle is a strong holder and must count toward ReferenceCount");
    }

    // Switch to ReleaseWhenUnreferenced. With no live handle right now, the
    // payload frees immediately and the entry is pruned.
    kb::tests::Require(manager.SetUnloadPolicy(id, kb::assets::AssetUnloadPolicy::ReleaseWhenUnreferenced), "SetUnloadPolicy must succeed for a cached asset");
    kb::tests::Require(!manager.IsLoaded(id), "Switching to ReleaseWhenUnreferenced with no live handle must free the payload immediately");
    kb::tests::Require(weak.Expired(), "The observing WeakAssetHandle must expire once the released payload is freed");
    kb::tests::Require(manager.LoadedCount() == 0, "LoadedCount must drop to 0 after the released payload is freed");
    kb::tests::Require(!manager.SetUnloadPolicy(id, kb::assets::AssetUnloadPolicy::Retain), "SetUnloadPolicy must fail (false) for an asset that is no longer cached");

    // Reload it, THEN move to release policy while a handle is alive: the
    // payload survives exactly as long as that handle.
    {
        const kb::assets::AssetHandle<std::string> held = manager.Load<std::string>(id);
        kb::tests::Require(held.IsLoaded(), "Reload after release must succeed");
        kb::tests::Require(manager.SetUnloadPolicy(id, kb::assets::AssetUnloadPolicy::ReleaseWhenUnreferenced), "SetUnloadPolicy(Release) with a live handle must succeed");
        kb::tests::Require(manager.UnloadPolicy(id) == kb::assets::AssetUnloadPolicy::ReleaseWhenUnreferenced, "UnloadPolicy must report the newly set policy");
        kb::tests::Require(manager.IsLoaded(id) && manager.ReferenceCount(id) == 1, "Under Release with one live handle, the asset stays loaded with ReferenceCount 1");
    }
    // The handle is gone — the payload is released, but the (now dead) map
    // entry lingers until a prune.
    kb::tests::Require(!manager.IsLoaded(id), "Under Release, dropping the last handle must free the payload");
    kb::tests::Require(manager.ReferenceCount(id) == 0, "ReferenceCount of a released asset must be 0");
    kb::tests::Require(manager.PruneUnreferenced() == 1, "PruneUnreferenced must remove exactly the one dead cache entry");
    kb::tests::Require(manager.PruneUnreferenced() == 0, "A second PruneUnreferenced must find nothing to remove");

    // A reload after release restores the default Retain policy (the entry
    // was pruned, so it is a brand-new Load).
    const kb::assets::AssetHandle<std::string> reloaded = manager.Load<std::string>(id);
    kb::tests::Require(reloaded.IsLoaded() && manager.UnloadPolicy(id) == kb::assets::AssetUnloadPolicy::Retain, "A reload after the entry was pruned must start fresh under the default Retain policy");
}

// LIB-159: AssetManager::ValidateCompatibility — registry-level dependency
// and loadability validation with readable diagnostics, no disk I/O and no
// payload loading. Uses RegisterAsset with explicit dependency lists (the
// same field AssetDiscoveryService populates via DiscoverDependencies).
void RunAssetCompatibilityValidationTest() {
    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<TextAssetLoader>()), "Text asset loader registration failed");

    const kb::assets::AssetId leafId{ 7001U };
    const kb::assets::AssetId midId{ 7002U };
    const kb::assets::AssetId rootId{ 7003U };
    const kb::assets::AssetId missingId{ 7999U };

    // leaf: a plain, loadable Text asset with no dependencies.
    kb::tests::Require(manager.RegisterAsset(kb::assets::AssetMetadata{
                           .id = leafId, .type = "Text", .name = "Leaf",
                           .virtualPath = "/Game/Text/Leaf.txt", .physicalPath = "Leaf.txt", .contentHash = 1U,
                       }),
        "Compatibility test leaf registration failed");

    // An asset that is registered and loadable and depends only on leaf -> compatible.
    kb::tests::Require(manager.RegisterAsset(kb::assets::AssetMetadata{
                           .id = midId, .type = "Text", .name = "Mid",
                           .virtualPath = "/Game/Text/Mid.txt", .physicalPath = "Mid.txt", .contentHash = 1U,
                           .dependencies = { leafId },
                       }),
        "Compatibility test mid registration failed");
    const kb::assets::AssetCompatibilityReport midReport = manager.ValidateCompatibility(midId);
    kb::tests::Require(midReport.compatible && midReport.diagnostics.empty() && manager.IsCompatible(midId), "An asset whose dependency is registered and loadable must validate as compatible");
    kb::tests::Require(midReport.FormatDiagnostics().empty(), "A compatible report must format to an empty diagnostic string");

    // Missing dependency: mid also depends on a never-registered asset.
    kb::tests::Require(manager.RegisterAsset(kb::assets::AssetMetadata{
                           .id = midId, .type = "Text", .name = "Mid",
                           .virtualPath = "/Game/Text/Mid.txt", .physicalPath = "Mid.txt", .contentHash = 2U,
                           .dependencies = { leafId, missingId },
                       }),
        "Compatibility test mid-with-missing-dep registration failed");
    const kb::assets::AssetCompatibilityReport missingReport = manager.ValidateCompatibility(midId);
    kb::tests::Require(!missingReport.compatible && missingReport.diagnostics.size() == 1U, "An asset with one missing dependency must report exactly one diagnostic");
    kb::tests::Require(missingReport.diagnostics[0].issue == kb::assets::AssetCompatibilityIssue::MissingDependency && missingReport.diagnostics[0].dependency == missingId,
        "The missing-dependency diagnostic must name the unregistered dependency id");
    kb::tests::Require(missingReport.FormatDiagnostics().find(kb::assets::ToString(missingId)) != std::string::npos && missingReport.FormatDiagnostics().find("/Game/Text/Mid.txt") != std::string::npos,
        "The readable missing-dependency diagnostic must name both the depending asset and the missing dependency");

    // Incompatible type: an asset whose type has no registered loader.
    const kb::assets::AssetId ghostId{ 7500U };
    kb::tests::Require(manager.RegisterAsset(kb::assets::AssetMetadata{
                           .id = ghostId, .type = "GhostType", .name = "Ghost",
                           .virtualPath = "/Game/Ghost.ghost", .physicalPath = "Ghost.ghost", .contentHash = 1U,
                       }),
        "Compatibility test ghost registration failed");
    const kb::assets::AssetCompatibilityReport ghostReport = manager.ValidateCompatibility(ghostId);
    kb::tests::Require(!ghostReport.compatible && ghostReport.diagnostics.size() == 1U && ghostReport.diagnostics[0].issue == kb::assets::AssetCompatibilityIssue::IncompatibleType,
        "An asset whose type has no registered loader must report an IncompatibleType diagnostic");
    kb::tests::Require(ghostReport.FormatDiagnostics().find("GhostType") != std::string::npos, "The IncompatibleType diagnostic must name the unloadable type");

    // Root not registered at all -> a single MissingDependency naming itself.
    const kb::assets::AssetCompatibilityReport unregisteredReport = manager.ValidateCompatibility(kb::assets::AssetId{ 6000U });
    kb::tests::Require(!unregisteredReport.compatible && unregisteredReport.diagnostics.size() == 1U && unregisteredReport.diagnostics[0].issue == kb::assets::AssetCompatibilityIssue::MissingDependency,
        "Validating an unregistered asset must report a single MissingDependency for the asset itself");

    // Transitive missing dependency: root -> mid(clean) -> leaf, but give leaf
    // a missing dependency several levels below the validated root.
    kb::tests::Require(manager.RegisterAsset(kb::assets::AssetMetadata{
                           .id = leafId, .type = "Text", .name = "Leaf",
                           .virtualPath = "/Game/Text/Leaf.txt", .physicalPath = "Leaf.txt", .contentHash = 2U,
                           .dependencies = { missingId },
                       }),
        "Compatibility test transitive-missing leaf update failed");
    kb::tests::Require(manager.RegisterAsset(kb::assets::AssetMetadata{
                           .id = midId, .type = "Text", .name = "Mid",
                           .virtualPath = "/Game/Text/Mid.txt", .physicalPath = "Mid.txt", .contentHash = 3U,
                           .dependencies = { leafId },
                       }),
        "Compatibility test transitive-missing mid update failed");
    kb::tests::Require(manager.RegisterAsset(kb::assets::AssetMetadata{
                           .id = rootId, .type = "Text", .name = "Root",
                           .virtualPath = "/Game/Text/Root.txt", .physicalPath = "Root.txt", .contentHash = 1U,
                           .dependencies = { midId },
                       }),
        "Compatibility test root registration failed");
    const kb::assets::AssetCompatibilityReport transitiveReport = manager.ValidateCompatibility(rootId);
    kb::tests::Require(!transitiveReport.compatible && transitiveReport.diagnostics.size() == 1U && transitiveReport.diagnostics[0].dependency == missingId,
        "ValidateCompatibility must catch a missing dependency several levels deep in the dependency closure");

    // Cycle safety: two assets that depend on each other must not loop, and
    // (both registered + loadable) must validate as compatible.
    const kb::assets::AssetId cycleA{ 7100U };
    const kb::assets::AssetId cycleB{ 7101U };
    kb::tests::Require(manager.RegisterAsset(kb::assets::AssetMetadata{
                           .id = cycleA, .type = "Text", .name = "CycleA",
                           .virtualPath = "/Game/Text/CycleA.txt", .physicalPath = "CycleA.txt", .contentHash = 1U, .dependencies = { cycleB },
                       }),
        "Compatibility test cycle A registration failed");
    kb::tests::Require(manager.RegisterAsset(kb::assets::AssetMetadata{
                           .id = cycleB, .type = "Text", .name = "CycleB",
                           .virtualPath = "/Game/Text/CycleB.txt", .physicalPath = "CycleB.txt", .contentHash = 1U, .dependencies = { cycleA },
                       }),
        "Compatibility test cycle B registration failed");
    const kb::assets::AssetCompatibilityReport cycleReport = manager.ValidateCompatibility(cycleA);
    kb::tests::Require(cycleReport.compatible, "A dependency cycle of otherwise-loadable assets must validate as compatible without looping forever");
}

void RunAssetKindClassificationTest() {
    // ToString <-> TryParseAssetKind round trip for every kind, in order.
    const kb::assets::AssetKind kinds[] = {
        kb::assets::AssetKind::Mesh, kb::assets::AssetKind::Material, kb::assets::AssetKind::Texture,
        kb::assets::AssetKind::Audio, kb::assets::AssetKind::Prefab, kb::assets::AssetKind::Scene,
        kb::assets::AssetKind::Animation, kb::assets::AssetKind::Graph, kb::assets::AssetKind::InputAction,
        kb::assets::AssetKind::InputMap,
    };
    static_assert(std::size(kinds) == kb::assets::kAssetKindCount, "AssetKind test must cover every kind");
    for (const kb::assets::AssetKind kind : kinds) {
        const std::string_view name = kb::assets::ToString(kind);
        kb::tests::Require(!name.empty(), "AssetKind::ToString must return a non-empty friendly name for every kind");
        kb::assets::AssetKind parsed{};
        kb::tests::Require(kb::assets::TryParseAssetKind(name, parsed) && parsed == kind, "AssetKind ToString/TryParseAssetKind must round trip for every kind");
    }
    kb::assets::AssetKind unusedKind{};
    kb::tests::Require(!kb::assets::TryParseAssetKind("NotAKind", unusedKind), "TryParseAssetKind must reject an unrecognised kind name");
    kb::tests::Require(!kb::assets::TryParseAssetKind("mesh", unusedKind), "TryParseAssetKind must be case-sensitive (reject a mis-cased kind name)");

    // Each kind accepts exactly its own concrete type string(s).
    const auto matches = [](std::string_view type, kb::assets::AssetKind kind, std::string importCategory = {}) {
        return kb::assets::AssetMatchesKind(kb::assets::AssetMetadata{ .type = std::string{ type }, .importCategory = std::move(importCategory) }, kind);
    };
    kb::tests::Require(matches("RenderMesh", kb::assets::AssetKind::Mesh), "AssetMatchesKind must accept RenderMesh as Mesh");
    kb::tests::Require(matches("RenderMaterial", kb::assets::AssetKind::Material) && matches("RenderMaterialInstance", kb::assets::AssetKind::Material),
        "AssetMatchesKind must accept both RenderMaterial and RenderMaterialInstance as Material");
    kb::tests::Require(!matches("RenderMaterialType", kb::assets::AssetKind::Material) && !matches("RenderMaterialGraph", kb::assets::AssetKind::Material),
        "AssetMatchesKind must NOT treat authoring-only material sub-types as the runtime Material kind");
    kb::tests::Require(matches("RenderTexture", kb::assets::AssetKind::Texture), "AssetMatchesKind must accept RenderTexture as Texture");
    kb::tests::Require(matches("AudioClip", kb::assets::AssetKind::Audio), "AssetMatchesKind must accept AudioClip as Audio");
    kb::tests::Require(matches("ImportedAsset", kb::assets::AssetKind::Audio, "Audio"), "AssetMatchesKind must accept an imported-media asset with importCategory Audio as Audio");
    kb::tests::Require(!matches("ImportedAsset", kb::assets::AssetKind::Audio, "Texture"), "AssetMatchesKind must NOT accept a non-audio ImportedAsset as Audio");
    kb::tests::Require(matches("ScenePrefab", kb::assets::AssetKind::Prefab), "AssetMatchesKind must accept ScenePrefab as Prefab");
    kb::tests::Require(matches("Scene", kb::assets::AssetKind::Scene), "AssetMatchesKind must accept Scene as Scene");
    kb::tests::Require(matches("ImportedAsset", kb::assets::AssetKind::Animation, "Animation"),
        "AssetMatchesKind must accept an imported animation payload as Animation");
    kb::tests::Require(!matches("ImportedAsset", kb::assets::AssetKind::Animation, "Audio"),
        "AssetMatchesKind must reject a non-animation imported payload as Animation");
    kb::tests::Require(matches("VisualGraph", kb::assets::AssetKind::Graph), "AssetMatchesKind must accept VisualGraph as Graph");
    kb::tests::Require(matches("InputAction", kb::assets::AssetKind::InputAction), "AssetMatchesKind must accept InputAction as InputAction");
    kb::tests::Require(matches("InputMappingContext", kb::assets::AssetKind::InputMap), "AssetMatchesKind must accept InputMappingContext as InputMap");

    // Cross-kind rejection: a mesh is not a texture, a scene is not a prefab.
    kb::tests::Require(!matches("RenderMesh", kb::assets::AssetKind::Texture), "AssetMatchesKind must reject RenderMesh as Texture");
    kb::tests::Require(!matches("Scene", kb::assets::AssetKind::Prefab) && !matches("ScenePrefab", kb::assets::AssetKind::Scene),
        "AssetMatchesKind must keep the distinct Scene and ScenePrefab types in their own kinds");

    // Reverse classification is unambiguous and honest for unknowns.
    const auto classify = [](std::string_view type, kb::assets::AssetKind& out, std::string importCategory = {}) {
        return kb::assets::TryClassifyAssetKind(kb::assets::AssetMetadata{ .type = std::string{ type }, .importCategory = std::move(importCategory) }, out);
    };
    kb::assets::AssetKind classified{};
    kb::tests::Require(classify("RenderMaterialInstance", classified) && classified == kb::assets::AssetKind::Material, "TryClassifyAssetKind must classify RenderMaterialInstance as Material");
    kb::tests::Require(classify("VisualGraph", classified) && classified == kb::assets::AssetKind::Graph, "TryClassifyAssetKind must classify VisualGraph as Graph");
    kb::tests::Require(classify("ImportedAsset", classified, "Audio") && classified == kb::assets::AssetKind::Audio, "TryClassifyAssetKind must classify an audio ImportedAsset as Audio");
    kb::tests::Require(classify("ImportedAsset", classified, "Animation") && classified == kb::assets::AssetKind::Animation,
        "TryClassifyAssetKind must classify an animation ImportedAsset as Animation");
    kb::tests::Require(!classify("LuaScript", classified), "TryClassifyAssetKind must return false for a type that is none of the typed-reference kinds (LuaScript)");
    kb::tests::Require(!classify("NativeBehaviour", classified), "TryClassifyAssetKind must return false for a NativeBehaviour asset");
    kb::tests::Require(!classify("ImportedAsset", classified, "Texture"), "TryClassifyAssetKind must return false for an ImportedAsset outside the typed categories");
}

void RunAssetDiscoveryPreservesEditorLiveOverrideTest() {
    ResetTestRoot();

    const std::filesystem::path assetsRoot = TestRoot() / "Project" / "Assets";
    const std::filesystem::path sourcePath = assetsRoot / "Text" / "Greeting.txt";
    const std::filesystem::path overridePath = TestRoot() / "WorkingGreeting.txt";
    WriteTextFile(sourcePath, "source asset");
    WriteTextFile(overridePath, "editor working copy");

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<TextAssetLoader>()), "Text asset loader registration failed for editor live override test");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Game asset mount failed for editor live override test");
    kb::tests::Require(manager.DiscoverMountedAssets() == 1, "Editor live override test did not discover the source asset");

    const kb::assets::AssetMetadata* sourceMetadata = manager.Registry().FindByPath("/Game/Text/Greeting.txt");
    kb::tests::Require(sourceMetadata != nullptr, "Editor live override test source metadata is missing");
    const kb::assets::AssetId assetId = sourceMetadata->id;

    kb::assets::AssetMetadata overrideMetadata = *sourceMetadata;
    overrideMetadata.importCategory = "EditorLiveOverride";
    overrideMetadata.physicalPath = overridePath;
    overrideMetadata.contentHash = 0x21B0602ULL;
    kb::tests::Require(manager.RegisterAsset(overrideMetadata), "Editor live override metadata was not accepted");

    WriteTextFile(sourcePath, "source changed behind editor");
    static_cast<void>(manager.DiscoverMountedAssets());

    const kb::assets::AssetMetadata* rediscovered = manager.Registry().Find(assetId);
    kb::tests::Require(rediscovered != nullptr, "Editor live override was removed by rediscovery");
    kb::tests::Require(rediscovered->physicalPath == overridePath, "Editor live override physical path was replaced by source rediscovery");
    kb::tests::Require(rediscovered->contentHash == 0x21B0602ULL, "Editor live override content hash was replaced by source rediscovery");
    kb::tests::Require(rediscovered->importCategory == "EditorLiveOverride", "Editor live override category was not preserved");
}

void RunAssetManagerFolderAndRenameOperationsTest() {
    ResetTestRoot();

    const std::filesystem::path assetsRoot = TestRoot() / "Project" / "Assets";
    WriteTextFile(assetsRoot / "Text" / "Greeting.txt", "hello");

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<TextAssetLoader>()), "Text asset loader registration failed for operations test");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Game asset mount failed for operations test");
    kb::tests::Require(manager.DiscoverMountedAssets() == 1, "Operations test discovery did not find the text asset");

    kb::tests::Require(manager.CreateFolder("/Game/NewFolder"), "Asset manager did not create a mounted folder");
    kb::tests::Require(std::filesystem::is_directory(assetsRoot / "NewFolder"), "Created mounted folder does not exist on disk");
    const std::optional<std::filesystem::path> uniqueFolder = manager.CreateUniqueFolder("/Game", "NewFolder");
    kb::tests::Require(uniqueFolder.has_value() && *uniqueFolder == "/Game/NewFolder_2", "Asset manager did not create the expected unique folder path");
    kb::tests::Require(std::filesystem::is_directory(assetsRoot / "NewFolder_2"), "Unique mounted folder does not exist on disk");
    kb::tests::Require(manager.RenameFolder("/Game/NewFolder", "RenamedFolder"), "Asset manager did not rename a mounted folder");
    kb::tests::Require(std::filesystem::is_directory(assetsRoot / "RenamedFolder"), "Renamed mounted folder does not exist on disk");
    kb::tests::Require(manager.DeleteFolder("/Game/RenamedFolder"), "Asset manager did not delete an empty mounted folder");
    kb::tests::Require(!std::filesystem::exists(assetsRoot / "RenamedFolder"), "Deleted mounted folder still exists on disk");

    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/Text/Greeting.txt");
    kb::tests::Require(metadata != nullptr, "Operations test asset was not indexed before rename");
    const kb::assets::AssetId oldId = metadata->id;
    WriteTextFile(assetsRoot / "Text" / "Greeting.meta", "meta");
    kb::tests::Require(manager.RenameAsset(oldId, "RenamedGreeting"), "Asset manager did not rename an asset file");
    kb::tests::Require(std::filesystem::is_regular_file(assetsRoot / "Text" / "RenamedGreeting.meta"), "Asset manager did not rename the sidecar meta file");
    kb::tests::Require(manager.Registry().FindByPath("/Game/Text/Greeting.txt") == nullptr, "Old asset virtual path remained indexed after rename");
    metadata = manager.Registry().FindByPath("/Game/Text/RenamedGreeting.txt");
    kb::tests::Require(metadata != nullptr, "Renamed asset virtual path was not indexed");
    kb::tests::Require(manager.CreateFolder("/Game/Moved"), "Asset manager did not create a move destination folder");
    WriteTextFile(assetsRoot / "Moved" / "RenamedGreeting.txt", "existing collision");
    const kb::assets::AssetMoveResult movedAsset = manager.MoveAssetIntoFolder(metadata->id, "/Game/Moved");
    kb::tests::Require(movedAsset.succeeded, "Asset manager did not move an asset file into a mounted folder");
    kb::tests::Require(movedAsset.virtualPath == "/Game/Moved/RenamedGreeting_1.txt", "Asset manager did not report the unique moved asset path");
    kb::tests::Require(manager.Registry().FindByPath("/Game/Text/RenamedGreeting.txt") == nullptr, "Old asset virtual path remained indexed after move");
    metadata = manager.Registry().FindByPath(movedAsset.virtualPath);
    kb::tests::Require(metadata != nullptr, "Moved asset virtual path was not indexed");
    kb::tests::Require(std::filesystem::is_regular_file(assetsRoot / "Moved" / "RenamedGreeting_1.txt"), "Moved asset file does not exist on disk");
    kb::tests::Require(std::filesystem::is_regular_file(assetsRoot / "Moved" / "RenamedGreeting_1.meta"), "Moved asset sidecar meta file does not exist on disk");
    kb::tests::Require(manager.DeleteAsset(metadata->id), "Asset manager did not delete an asset file");
    kb::tests::Require(manager.Registry().FindByPath(movedAsset.virtualPath) == nullptr, "Deleted asset remained indexed");
    kb::tests::Require(!std::filesystem::exists(assetsRoot / "Moved" / "RenamedGreeting_1.meta"), "Deleted asset sidecar meta file still exists");

    kb::tests::Require(manager.CreateFolder("/Game/FolderSource"), "Asset manager did not create a source folder for move");
    WriteTextFile(assetsRoot / "FolderSource" / "Nested" / "Inside.txt", "inside folder");
    std::filesystem::create_directories(assetsRoot / "Moved" / "FolderSource");
    const kb::assets::AssetMoveResult movedFolder = manager.MoveFolderIntoFolder("/Game/FolderSource", "/Game/Moved");
    kb::tests::Require(movedFolder.succeeded, "Asset manager did not move a folder into a mounted folder");
    kb::tests::Require(movedFolder.virtualPath == "/Game/Moved/FolderSource_1", "Asset manager did not report the unique moved folder path");
    kb::tests::Require(!std::filesystem::exists(assetsRoot / "FolderSource"), "Moved source folder still exists on disk");
    kb::tests::Require(std::filesystem::is_regular_file(assetsRoot / "Moved" / "FolderSource_1" / "Nested" / "Inside.txt"), "Moved folder contents were not preserved");
    kb::tests::Require(manager.Registry().FindByPath("/Game/Moved/FolderSource_1/Nested/Inside.txt") != nullptr, "Moved folder assets were not rediscovered");
    kb::tests::Require(!manager.MoveFolder(movedFolder.virtualPath, movedFolder.virtualPath / "Nested"), "Asset manager allowed a folder to move into its own child");
}

void RunAssetImportServiceBinaryContainerTest() {
    ResetTestRoot();

    const std::filesystem::path assetsRoot = TestRoot() / "Project" / "Assets";
    const std::filesystem::path sourceRoot = TestRoot() / "External";
    WriteTextFile(sourceRoot / "Albedo.png", "texture bytes");

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<kb::assets::ImportedAssetLoader>()), "Imported asset loader registration failed");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Game asset mount failed for import test");

    const std::array<std::filesystem::path, 1> files{ sourceRoot / "Albedo.png" };
    const kb::assets::AssetImportResult result = kb::assets::AssetImportService::ImportFiles(manager, files, "/Game/Textures");
    kb::tests::Require(result.Succeeded() && result.ImportedCount() == 1U, "Asset import service did not import the source file");
    kb::tests::Require(result.CreatedCount() == 1U && result.ReusedCount() == 0U, "Asset import service did not report a created import");

    const kb::assets::AssetImportItemResult& item = result.items.front();
    kb::tests::Require(item.status == kb::assets::AssetImportItemStatus::Created, "Imported asset item did not report created status");
    kb::tests::Require(item.assetPhysicalPath.extension() == ".21kb", "Imported asset file should use the .21kb extension");
    kb::tests::Require(item.metaPhysicalPath.extension() == ".meta", "Imported asset meta file should use the .meta extension");
    kb::tests::Require(item.assetPhysicalPath.stem() == item.metaPhysicalPath.stem(), "Imported asset and meta should share the same base name");
    kb::tests::Require(std::filesystem::is_regular_file(item.assetPhysicalPath), "Imported .21kb file was not written");
    kb::tests::Require(std::filesystem::is_regular_file(item.metaPhysicalPath), "Imported .meta file was not written");
    kb::tests::Require(item.virtualPath == "/Game/Textures/Albedo.21kb", "Imported asset virtual path should point at the .21kb file");

    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(item.virtualPath);
    kb::tests::Require(metadata != nullptr && metadata->type == "ImportedAsset", "Imported asset metadata was not registered");
    kb::tests::Require(metadata != nullptr && metadata->importCategory == "Texture", "Imported asset metadata did not expose the import category");
    kb::tests::Require(metadata->contentHash == item.assetHash && metadata->contentHash != 0U, "Imported asset metadata did not store the container hash");

    const kb::assets::AssetHandle<kb::assets::ImportedAsset> loaded = manager.Load<kb::assets::ImportedAsset>(metadata->id);
    kb::tests::Require(loaded.IsLoaded(), "Imported .21kb asset did not load through the imported asset loader");
    kb::tests::Require(loaded->category == kb::assets::AssetImportCategory::Texture, "Imported asset category was not preserved");
    kb::tests::Require(loaded->sourceName == "Albedo.png", "Imported asset source name was not preserved");
    kb::tests::Require(loaded->sourceExtension == ".png", "Imported asset source extension was not preserved");
    kb::tests::Require(loaded->payload.size() == 13U, "Imported asset payload size was not preserved");

    kb::assets::AssetManager rediscovered;
    kb::tests::Require(rediscovered.RegisterLoader(std::make_unique<kb::assets::ImportedAssetLoader>()), "Imported asset rediscovery loader registration failed");
    kb::tests::Require(rediscovered.Mounts().Mount("Game", assetsRoot), "Game asset remount failed for imported asset rediscovery");
    kb::tests::Require(rediscovered.DiscoverMountedAssets() == 1U, "Imported asset rediscovery did not find the .21kb file");
    const kb::assets::AssetMetadata* rediscoveredMetadata = rediscovered.Registry().FindByPath(item.virtualPath);
    kb::tests::Require(rediscoveredMetadata != nullptr && rediscoveredMetadata->importCategory == "Texture", "Imported asset rediscovery did not read the binary category flag");
}

void RunAssetImportServiceReportsCreatedReusedMissingAndUnsupportedTest() {
    ResetTestRoot();

    const std::filesystem::path assetsRoot = TestRoot() / "Project" / "Assets";
    const std::filesystem::path sourceRoot = TestRoot() / "External";
    WriteTextFile(sourceRoot / "Albedo.png", "texture bytes");
    WriteTextFile(sourceRoot / "Readme.unsupported", "not a supported asset");

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<kb::assets::ImportedAssetLoader>()), "Imported asset report test loader registration failed");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Game asset mount failed for import report test");

    const std::array<std::filesystem::path, 1> firstFiles{ sourceRoot / "Albedo.png" };
    const kb::assets::AssetImportResult first = kb::assets::AssetImportService::ImportFiles(manager, firstFiles, "/Game/Textures");
    kb::tests::Require(first.Succeeded() && first.CreatedCount() == 1U, "Import report test could not create the initial texture asset");

    const std::array<std::filesystem::path, 3> mixedFiles{
        sourceRoot / "Albedo.png",
        sourceRoot / "Missing.png",
        sourceRoot / "Readme.unsupported",
    };
    const kb::assets::AssetImportResult report = kb::assets::AssetImportService::ImportFiles(manager, mixedFiles, "/Game/Textures");
    kb::tests::Require(!report.Succeeded(), "Mixed import report should not report complete success");
    kb::tests::Require(report.ImportedCount() == 1U, "Mixed import report should count the reused asset as imported");
    kb::tests::Require(report.CreatedCount() == 0U, "Mixed import report should not create a duplicate asset");
    kb::tests::Require(report.ReusedCount() == 1U, "Mixed import report did not count reused texture assets");
    kb::tests::Require(report.MissingCount() == 1U, "Mixed import report did not count missing texture sources");
    kb::tests::Require(report.UnsupportedCount() == 1U, "Mixed import report did not count unsupported source files");
    kb::tests::Require(report.FailedCount() == 2U, "Mixed import report did not expose failed item count");
    kb::tests::Require(report.items[0].status == kb::assets::AssetImportItemStatus::Reused, "Mixed import report did not mark the existing texture as reused");
    kb::tests::Require(report.items[0].id == first.items[0].id && report.items[0].virtualPath == first.items[0].virtualPath, "Reused import did not return the existing asset identity");
    kb::tests::Require(report.items[1].status == kb::assets::AssetImportItemStatus::Missing && report.items[1].category == kb::assets::AssetImportCategory::Texture, "Mixed import report did not classify missing texture source");
    kb::tests::Require(report.items[2].status == kb::assets::AssetImportItemStatus::Unsupported && report.items[2].category == kb::assets::AssetImportCategory::Unknown, "Mixed import report did not classify unsupported source");
    kb::tests::Require(!std::filesystem::exists(assetsRoot / "Textures" / "Albedo_1.21kb"), "Reused import should not create a duplicate texture asset");
}

void RunScenePrefabRuntimeAssetTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "PrefabProject";
    const std::filesystem::path prefabPath = projectRoot / "Assets" / "Prefabs" / "RuntimePrefab.kbprefab";
    const std::filesystem::path prefabCopyPath = projectRoot / "Assets" / "Prefabs" / "RuntimePrefabCopy.kbprefab";

    kb::scene::Scene source;
    kb::scene::SceneObject root = source.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Runtime Prefab Root" });
    const kb::scene::ScenePrefabHandle registered = source.Prefabs().CaptureRegistered(root, "RuntimePrefab");
    kb::tests::Require(registered.IsValid(), "Runtime prefab registration failed");
    kb::tests::Require(source.Prefabs().Save(registered, prefabPath), "Runtime prefab asset save failed");
    std::filesystem::copy_file(prefabPath, prefabCopyPath);

    kb::scene::Scene runtime;
    kb::tests::Require(runtime.Assets().MountProject(projectRoot), "Scene runtime asset project mount failed");
    kb::tests::Require(runtime.Assets().Discover() == 2, "Scene runtime asset discovery did not find every prefab file");

    const kb::assets::AssetHandle<kb::scene::ScenePrefab> prefab = runtime.Assets().LoadPrefab("/Game/Prefabs/RuntimePrefab.kbprefab");
    kb::tests::Require(prefab.IsLoaded(), "Scene prefab did not load through SceneAssets");
    kb::tests::Require(prefab->NodeCount() == 1, "Scene prefab runtime payload had an invalid node count");
    const kb::assets::AssetHandle<kb::scene::ScenePrefab> prefabCopy = runtime.Assets().LoadPrefab("/Game/Prefabs/RuntimePrefabCopy.kbprefab");
    kb::tests::Require(prefabCopy.IsLoaded(), "Scene prefab duplicate GUID copy did not load through SceneAssets");
    kb::tests::Require(prefabCopy->NodeCount() == 1, "Scene prefab duplicate GUID copy had an invalid node count");

    const kb::scene::ScenePrefabInstance instance = runtime.Prefabs().Instantiate(*prefab.Get());
    kb::tests::Require(instance.ObjectCount() == 1, "Runtime-loaded prefab did not instantiate");
    kb::tests::Require(runtime.Entities().Name(instance.ObjectAt(0)) == "Runtime Prefab Root", "Runtime-loaded prefab instance did not preserve the root name");
    const kb::scene::ScenePrefabInstance copyInstance = runtime.Prefabs().Instantiate(*prefabCopy.Get());
    kb::tests::Require(copyInstance.ObjectCount() == 1, "Runtime-loaded duplicate GUID prefab did not instantiate");
}

void RunSceneAudioClipAssetDiscoveryTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "AudioProject";
    const std::filesystem::path clipPath = projectRoot / "Assets" / "Audio" / "Ping.wav";
    WriteTextFile(clipPath, "audio bytes");

    kb::scene::Scene scene;
    kb::tests::Require(scene.Assets().MountProject(projectRoot), "Scene audio asset project mount failed");
    kb::tests::Require(scene.Assets().Discover() == 1U, "Scene audio asset discovery did not find the wav file");

    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Audio/Ping.wav");
    kb::tests::Require(metadata != nullptr, "Discovered audio asset could not be resolved by virtual path");
    kb::tests::Require(metadata->type == "AudioClip", "Discovered audio asset was not classified as AudioClip");

    const kb::assets::AssetHandle<kb::audio::AudioClipAsset> loaded = scene.Assets().Manager().Load<kb::audio::AudioClipAsset>(metadata->id);
    kb::tests::Require(loaded.IsLoaded(), "Audio clip asset did not load through the runtime asset manager");
    kb::tests::Require(std::filesystem::equivalent(loaded->path, clipPath), "Audio clip asset did not preserve the resolved physical path");
}

void RunAudioClipFormatCatalogTest() {
    const std::array<std::string_view, 12U> unsupported{
        ".ogg", ".aac", ".m4a", ".wma", ".aiff", ".aif",
        ".xm", ".mod", ".s3m", ".it", ".mid", ".midi",
    };
    const std::vector<std::string> catalog = kb::assets::AssetImportCatalog::SupportedSourceExtensions();
    kb::audio::AudioClipAssetLoader loader;
    const std::vector<std::string> loaderExtensions = loader.Extensions();
    kb::tests::Require(loaderExtensions.size() == kb::audio::kSupportedAudioClipExtensions.size(),
        "Audio clip loader advertised a format outside the decoder contract");
    for (const std::string_view extension : kb::audio::kSupportedAudioClipExtensions) {
        kb::tests::Require(kb::audio::IsSupportedAudioClipExtension(extension)
                && kb::assets::AssetImportCatalog::ClassifyExtension(extension) == kb::assets::AssetImportCategory::Audio
                && std::ranges::count(catalog, extension) == 1
                && std::ranges::count(loaderExtensions, extension) == 1,
            "A supported audio clip extension was not shared by catalog and loader");
    }
    kb::tests::Require(kb::audio::IsSupportedAudioClipExtension(".WAV")
            && kb::assets::AssetImportCatalog::ClassifyExtension(".FLAC") == kb::assets::AssetImportCategory::Audio,
        "Audio clip format matching must be case-insensitive");
    for (const std::string_view extension : unsupported) {
        kb::tests::Require(!kb::audio::IsSupportedAudioClipExtension(extension)
                && kb::assets::AssetImportCatalog::ClassifyExtension(extension) != kb::assets::AssetImportCategory::Audio
                && std::ranges::find(catalog, extension) == catalog.end()
                && std::ranges::find(loaderExtensions, extension) == loaderExtensions.end(),
            "An unsupported audio format remained advertised by the import or loader catalog");
    }

    ResetTestRoot();
    const std::filesystem::path supportedPath = TestRoot() / "Format.WAV";
    const std::filesystem::path unsupportedPath = TestRoot() / "Format.ogg";
    WriteTextFile(supportedPath, "format fixture");
    WriteTextFile(unsupportedPath, "format fixture");
    const kb::assets::AssetMetadata metadata{
        .id = kb::assets::AssetId{ 7001U },
        .type = "AudioClip",
        .name = "Format",
        .virtualPath = "/Game/Format.WAV",
        .physicalPath = supportedPath,
    };
    kb::tests::Require(loader.Load(kb::assets::AssetLoadRequest{ metadata, supportedPath }).Succeeded(),
        "Audio clip loader rejected a supported case-insensitive extension");
    kb::tests::Require(!loader.Load(kb::assets::AssetLoadRequest{ metadata, unsupportedPath }).Succeeded(),
        "Audio clip loader accepted an unsupported physical extension");
    kb::assets::AssetMetadata wrongType = metadata;
    wrongType.type = "Texture";
    kb::tests::Require(!loader.Load(kb::assets::AssetLoadRequest{ wrongType, supportedPath }).Succeeded(),
        "Audio clip loader accepted mismatched asset metadata");
}

void RunScriptAssetPipelineTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "ScriptProject";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    WriteTextFile(assetsRoot / "Logic" / "Player.lua",
        "-- @import Shared.Math\n"
        "-- @imported Shared.Wrong\n"
        "-- @expose speed Float = 5.5\n"
        "-- @expose lives Int = 3abc\n"
        "-- @exposed ignored Float = 1.0\n"
        "function Tick(self, dt)\nend\n");
    WriteTextFile(assetsRoot / "Logic" / "Door.native", "name DoorController\nsymbol gameplay.DoorController\napi = function Inventory.AddItem itemId:Int -> total:Int\n");
    WriteTextFile(assetsRoot / "Logic" / "Enemy.kbgraph", R"(kbgraph 1
name EnemyController
node 1 Event Tick
pin 1 Output then Void
)");

    kb::scene::Scene scene;
    kb::tests::Require(scene.Assets().MountProject(projectRoot), "Script asset project mount failed");
    kb::tests::Require(scene.Assets().Discover() == 3U, "Script asset discovery did not find Lua, native and visual graph assets");

    const kb::assets::AssetMetadata* luaMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/Player.lua");
    const kb::assets::AssetMetadata* nativeMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/Door.native");
    const kb::assets::AssetMetadata* graphMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/Enemy.kbgraph");
    kb::tests::Require(luaMetadata != nullptr && luaMetadata->type == "LuaScript", "Lua script asset metadata was not classified");
    kb::tests::Require(nativeMetadata != nullptr && nativeMetadata->type == "NativeBehaviour", "Native behaviour asset metadata was not classified");
    kb::tests::Require(graphMetadata != nullptr && graphMetadata->type == "VisualGraph", "Visual graph asset metadata was not classified");

    const kb::assets::AssetHandle<kb::script::LuaScriptAsset> luaAsset = scene.Assets().Manager().Load<kb::script::LuaScriptAsset>(luaMetadata->id);
    kb::tests::Require(luaAsset.IsLoaded() && luaAsset->source.find("Tick") != std::string::npos, "Lua script asset did not load source content");
    kb::tests::Require(luaAsset->imports.size() == 1U && luaAsset->imports[0] == "Shared.Math", "Lua script asset did not parse import metadata");
    kb::tests::Require(luaAsset->exposedVariables.size() == 2U &&
            luaAsset->exposedVariables[0].name == "speed" &&
            luaAsset->exposedVariables[0].type == kb::script::ScriptValueType::Float,
        "Lua script asset did not parse typed exposed variables");
    kb::tests::Require(luaAsset->exposedVariableDefaults.size() == 2U &&
            luaAsset->exposedVariableHasDefault.size() == 2U &&
            luaAsset->exposedVariableHasDefault[0] != 0U &&
            kb::tests::NearlyEqual(luaAsset->exposedVariableDefaults[0].AsFloat(), 5.5F),
        "Lua script asset did not parse exposed variable default value");
    kb::tests::Require(luaAsset->exposedVariables[1].name == "lives" &&
            luaAsset->exposedVariableDefaults.size() == 2U &&
            luaAsset->exposedVariableHasDefault.size() == 2U &&
            luaAsset->exposedVariableHasDefault[1] == 0U &&
            luaAsset->exposedVariableDefaults[1].AsInt() == 0,
        "Lua script asset accepted an invalid exposed variable default");

    const kb::assets::AssetHandle<kb::script::NativeBehaviourDescriptor> nativeAsset = scene.Assets().Manager().Load<kb::script::NativeBehaviourDescriptor>(nativeMetadata->id);
    kb::tests::Require(nativeAsset.IsLoaded() && nativeAsset->symbol == "gameplay.DoorController", "Native behaviour descriptor did not load symbol");
    kb::tests::Require(nativeAsset->apiDeclarations.size() == 1U && nativeAsset->apiDeclarations[0].name == "Inventory.AddItem",
        "Native behaviour descriptor did not parse API declarations");

    const kb::assets::AssetHandle<kb::visual::VisualGraphAsset> graphAsset = scene.Assets().Manager().Load<kb::visual::VisualGraphAsset>(graphMetadata->id);
    kb::tests::Require(graphAsset.IsLoaded() && graphAsset->name == "EnemyController", "Visual graph asset did not load through scene-registered loader");

    const std::optional<kb::scene::BehaviourComponent> luaBehaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*luaMetadata);
    const std::optional<kb::scene::BehaviourComponent> nativeBehaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*nativeMetadata);
    const std::optional<kb::scene::BehaviourComponent> graphBehaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*graphMetadata);
    kb::tests::Require(luaBehaviour.has_value() && luaBehaviour->backend == kb::scene::BehaviourBackend::Lua, "Lua asset did not map to Lua behaviour");
    kb::tests::Require(nativeBehaviour.has_value() && nativeBehaviour->backend == kb::scene::BehaviourBackend::Native, "Native descriptor did not map to native behaviour");
    kb::tests::Require(graphBehaviour.has_value() && graphBehaviour->backend == kb::scene::BehaviourBackend::VisualGraph, "Visual graph did not map to visual graph behaviour");

    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Scripted" });
    const kb::script::ScriptBehaviourBindingResult bound = kb::script::ScriptBehaviourBindingService::AttachMetadata(
        scene,
        object.Entity(),
        *luaMetadata,
        kb::script::ScriptBehaviourBindingOptions{
            .enabled = true,
            .tickGroup = kb::scene::BehaviourTickGroup::Input,
            .executionOrder = -10,
            .prepareRuntimeAsset = false,
        });
    kb::tests::Require(bound.Succeeded(), "Script behaviour binding service did not attach a Lua asset");
    const kb::scene::BehaviourComponent* attached = scene.Components().Behaviours().TryGet(object.Entity());
    kb::tests::Require(attached != nullptr && attached->behaviourAssetId == luaMetadata->id.value, "Script behaviour component was not attached to the entity");
    kb::tests::Require(attached->backend == kb::scene::BehaviourBackend::Lua && attached->tickGroup == kb::scene::BehaviourTickGroup::Input && attached->executionOrder == -10,
        "Script behaviour binding service did not preserve component settings");
}

// The engine-provided `Inspector` table doubles as the declaration site: a plain
// top-level `Inspector.name = default` in real Lua source (not a comment) is
// parsed into the exposed-variable schema, its type inferred from the literal.
// This replaces the rejected `@expose` comment convention.
void RunInspectorDeclarationParseTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "InspectorProject";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    WriteTextFile(assetsRoot / "Logic" / "Hero.lua",
        "Inspector.speed = 3.5\n"
        "Inspector.lives = 3\n"
        "Inspector.enabled = true\n"
        "Inspector.title = \"hero\"\n"
        "Inspector[\"dashPower\"] = 12.0\n"
        "-- Inspector.ignored = 1.0\n"
        "function Tick(self, dt)\nend\n");

    kb::scene::Scene scene;
    kb::tests::Require(scene.Assets().MountProject(projectRoot), "Inspector declaration project mount failed");
    kb::tests::Require(scene.Assets().Discover() == 1U, "Inspector declaration discovery did not find the Lua asset");

    const kb::assets::AssetMetadata* luaMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/Hero.lua");
    kb::tests::Require(luaMetadata != nullptr && luaMetadata->type == "LuaScript", "Inspector declaration Lua asset was not classified");

    const kb::assets::AssetHandle<kb::script::LuaScriptAsset> luaAsset = scene.Assets().Manager().Load<kb::script::LuaScriptAsset>(luaMetadata->id);
    kb::tests::Require(luaAsset.IsLoaded(), "Inspector declaration Lua asset did not load");

    // Five real declarations parsed in source order; the commented one ignored.
    kb::tests::Require(luaAsset->exposedVariables.size() == 5U, "Inspector declarations did not parse the expected variable count");
    kb::tests::Require(luaAsset->exposedVariables[0].name == "speed" && luaAsset->exposedVariables[0].type == kb::script::ScriptValueType::Float &&
            luaAsset->exposedVariableHasDefault[0] != 0U && kb::tests::NearlyEqual(luaAsset->exposedVariableDefaults[0].AsFloat(), 3.5F),
        "Inspector.speed did not infer a Float with its default");
    kb::tests::Require(luaAsset->exposedVariables[1].name == "lives" && luaAsset->exposedVariables[1].type == kb::script::ScriptValueType::Int &&
            luaAsset->exposedVariableDefaults[1].AsInt() == 3,
        "Inspector.lives did not infer an Int with its default");
    kb::tests::Require(luaAsset->exposedVariables[2].name == "enabled" && luaAsset->exposedVariables[2].type == kb::script::ScriptValueType::Bool &&
            luaAsset->exposedVariableDefaults[2].AsBool(),
        "Inspector.enabled did not infer a Bool with its default");
    kb::tests::Require(luaAsset->exposedVariables[3].name == "title" && luaAsset->exposedVariables[3].type == kb::script::ScriptValueType::String &&
            luaAsset->exposedVariableDefaults[3].AsString() == "hero",
        "Inspector.title did not infer a String with its default");
    kb::tests::Require(luaAsset->exposedVariables[4].name == "dashPower" && luaAsset->exposedVariables[4].type == kb::script::ScriptValueType::Float &&
            kb::tests::NearlyEqual(luaAsset->exposedVariableDefaults[4].AsFloat(), 12.0F),
        "Inspector[\"dashPower\"] bracket declaration did not infer a Float");
}

[[nodiscard]] bool ContainsAssetId(std::span<const kb::assets::AssetId> ids, kb::assets::AssetId id) noexcept {
    return std::ranges::any_of(ids, [id](kb::assets::AssetId candidate) noexcept {
        return candidate.value == id.value;
    });
}

// A scene must publish everything that has to be pulled alongside it, and every id
// it publishes must be the id the registry itself gave that asset.
//
// Turns red if: SceneAssetLoader::DiscoverDependencies is removed or falls back to
// the IAssetLoader default (the dependency list comes back empty); if the nested
// prefab is reported in the sidecar's raw MakeAssetId(guid) form instead of the
// registry's MakeAssetId(path + ":" + type) form (the registry-id membership check
// fails and the guid-derived-id check fires); or if the mesh/material entries are
// dropped or rewritten (the count and membership checks fail).
void RunSceneAssetDependencyDiscoveryTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "SceneDependencyProject";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    const std::filesystem::path sourceRoot = TestRoot() / "SceneDependencySources";
    WriteTextFile(sourceRoot / "Block.obj", "o Block\nv 0.0 0.0 0.0\n");
    WriteTextFile(sourceRoot / "Surface.mtl", "newmtl Surface\n");

    // Author the referenced assets the way the editor does, so their ids come from
    // the registry's own scheme rather than from arithmetic repeated in the test.
    kb::scene::Scene authoring;
    kb::tests::Require(authoring.Assets().MountProject(projectRoot), "Scene dependency project mount failed");

    const std::array<std::filesystem::path, 2> sources{ sourceRoot / "Block.obj", sourceRoot / "Surface.mtl" };
    const kb::assets::AssetImportResult imported =
        kb::assets::AssetImportService::ImportFiles(authoring.Assets().Manager(), sources, "/Game/Imported");
    kb::tests::Require(imported.Succeeded() && imported.CreatedCount() == 2U, "Scene dependency source assets were not imported");
    kb::tests::Require(imported.items[0].sourcePath.filename() == "Block.obj" && imported.items[1].sourcePath.filename() == "Surface.mtl",
        "Scene dependency import did not preserve the requested source order");
    const kb::assets::AssetId meshId = imported.items[0].id;
    const kb::assets::AssetId materialId = imported.items[1].id;
    const std::filesystem::path meshVirtualPath = imported.items[0].virtualPath;
    const std::filesystem::path materialVirtualPath = imported.items[1].virtualPath;

    // The nested prefab asset, written by the real prefab writer so it carries the
    // guid the scene node will reference.
    const std::filesystem::path prefabPath = assetsRoot / "Prefabs" / "Nested.kbprefab";
    const kb::scene::SceneObject prefabRoot = authoring.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Nested Root" });
    const kb::scene::ScenePrefabHandle nestedPrefab = authoring.Prefabs().CaptureRegistered(prefabRoot, "NestedPrefab");
    kb::tests::Require(nestedPrefab.IsValid(), "Nested prefab registration failed");
    kb::tests::Require(authoring.Prefabs().Save(nestedPrefab, prefabPath), "Nested prefab asset save failed");
    const std::string nestedPrefabGuid = authoring.Prefabs().Guid(nestedPrefab);
    kb::tests::Require(!nestedPrefabGuid.empty(), "Nested prefab asset was saved without a guid");

    kb::scene::SceneDocument document;
    document.name = "DependencyScene";
    document.guid = "scene:DependencyScene";
    static_cast<void>(document.worldPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = 1U,
        .name = "Mesh Node",
        .components = kb::scene::ScenePrefabNodeComponents{
            .meshRenderer = kb::scene::MeshRendererComponent{
                .meshAssetId = meshId.value,
                .materialAssetId = materialId.value,
            },
        },
    }));
    static_cast<void>(document.worldPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = 2U,
        .name = "Nested Prefab Node",
        .nestedPrefabGuid = nestedPrefabGuid,
    }));

    const std::filesystem::path scenePath = assetsRoot / "Scenes" / "DependencyScene.21kbscene";
    std::error_code sceneDirectoryError;
    std::filesystem::create_directories(scenePath.parent_path(), sceneDirectoryError);
    kb::tests::Require(!sceneDirectoryError, "Scene dependency scene directory could not be created");
    kb::tests::Require(kb::scene::SceneDocumentService::Save(document, scenePath), "Scene dependency scene asset save failed");
    kb::tests::Require(std::filesystem::is_regular_file(kb::scene::SceneAssetMetaPath(scenePath)),
        "Scene dependency scene asset did not write its .meta sidecar");

    // Rediscover from disk in a fresh scene: the reported ids have to come out of a
    // plain discovery pass, not out of the authoring scene's memory.
    kb::scene::Scene runtime;
    kb::tests::Require(runtime.Assets().MountProject(projectRoot), "Scene dependency runtime project mount failed");
    kb::tests::Require(runtime.Assets().Discover() == 4U, "Scene dependency discovery did not find the mesh, material, prefab and scene");

    const kb::assets::AssetRegistry& registry = runtime.Assets().Manager().Registry();
    const kb::assets::AssetMetadata* sceneMetadata = registry.FindByPath("/Game/Scenes/DependencyScene.21kbscene");
    kb::tests::Require(sceneMetadata != nullptr && sceneMetadata->type == "Scene", "Scene asset was not registered by discovery");
    const kb::assets::AssetMetadata* prefabMetadata = registry.FindByPath("/Game/Prefabs/Nested.kbprefab");
    kb::tests::Require(prefabMetadata != nullptr && prefabMetadata->type == "ScenePrefab", "Nested prefab asset was not registered by discovery");
    const kb::assets::AssetMetadata* meshMetadata = registry.FindByPath(meshVirtualPath);
    kb::tests::Require(meshMetadata != nullptr && meshMetadata->id.value == meshId.value, "Imported mesh id did not survive rediscovery");
    const kb::assets::AssetMetadata* materialMetadata = registry.FindByPath(materialVirtualPath);
    kb::tests::Require(materialMetadata != nullptr && materialMetadata->id.value == materialId.value, "Imported material id did not survive rediscovery");

    // Both sides of the identifier scheme, computed independently: the registry's
    // MakeAssetId(NormalizeAssetPath(virtualPath) + ":" + type) for each asset, and
    // the ids the scene loader reports for those same assets.
    const kb::assets::AssetId expectedPrefabId = kb::assets::MakeAssetId(
        kb::assets::NormalizeAssetPath(prefabMetadata->virtualPath) + ":" + prefabMetadata->type);
    kb::tests::Require(prefabMetadata->id.value == expectedPrefabId.value, "Registry prefab id did not follow the path-and-type scheme");
    const kb::assets::AssetId expectedMeshId = kb::assets::MakeAssetId(
        kb::assets::NormalizeAssetPath(meshMetadata->virtualPath) + ":" + meshMetadata->type);
    kb::tests::Require(meshMetadata->id.value == expectedMeshId.value, "Registry mesh id did not follow the path-and-type scheme");

    const std::span<const kb::assets::AssetId> dependencies{ sceneMetadata->dependencies };
    kb::tests::Require(dependencies.size() == 3U, "Scene dependency discovery did not report exactly the mesh, material and nested prefab");
    kb::tests::Require(ContainsAssetId(dependencies, meshId), "Scene dependency discovery did not report the mesh asset");
    kb::tests::Require(ContainsAssetId(dependencies, materialId), "Scene dependency discovery did not report the material asset");
    kb::tests::Require(ContainsAssetId(dependencies, expectedPrefabId), "Scene dependency discovery did not report the nested prefab under its registry id");
    kb::tests::Require(!ContainsAssetId(dependencies, kb::assets::MakeAssetId(nestedPrefabGuid)),
        "Scene dependency discovery still reports the nested prefab under its guid-derived id");
    for (const kb::assets::AssetId dependency : dependencies) {
        kb::tests::Require(registry.Find(dependency) != nullptr, "Scene dependency discovery reported an id the registry cannot resolve");
    }
}

// A scene file whose sidecar is absent must not break discovery or be credited
// with dependencies it cannot prove, but it must fail compatibility validation
// so a cooker cannot publish the empty dependency closure.
//
// Turns red if: DiscoverDependencies dereferences or trusts an unread sidecar (the
// process dies, or the scene stops being registered); or if it invents dependencies
// from anywhere but the sidecar (the empty-list check fails).
void RunSceneAssetDependencyWithoutSidecarTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "SidecarlessSceneProject";
    const std::filesystem::path scenePath = projectRoot / "Assets" / "Scenes" / "Sidecarless.21kbscene";

    kb::scene::SceneDocument document;
    document.name = "Sidecarless";
    document.guid = "scene:Sidecarless";
    static_cast<void>(document.worldPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = 1U,
        .name = "Mesh Node",
        .components = kb::scene::ScenePrefabNodeComponents{
            .meshRenderer = kb::scene::MeshRendererComponent{
                .meshAssetId = 0x1234U,
                .materialAssetId = 0x5678U,
            },
        },
    }));

    std::error_code sceneDirectoryError;
    std::filesystem::create_directories(scenePath.parent_path(), sceneDirectoryError);
    kb::tests::Require(!sceneDirectoryError, "Sidecarless scene directory could not be created");
    kb::tests::Require(kb::scene::SceneDocumentService::Save(document, scenePath), "Sidecarless scene asset save failed");

    std::error_code metaRemoveError;
    kb::tests::Require(std::filesystem::remove(kb::scene::SceneAssetMetaPath(scenePath), metaRemoveError) && !metaRemoveError,
        "Sidecarless scene test could not remove the .meta sidecar");

    kb::scene::Scene runtime;
    kb::tests::Require(runtime.Assets().MountProject(projectRoot), "Sidecarless scene project mount failed");
    kb::tests::Require(runtime.Assets().Discover() == 1U, "Sidecarless scene discovery did not find the scene file");

    const kb::assets::AssetMetadata* sceneMetadata =
        runtime.Assets().Manager().Registry().FindByPath("/Game/Scenes/Sidecarless.21kbscene");
    kb::tests::Require(sceneMetadata != nullptr && sceneMetadata->type == "Scene", "Sidecarless scene asset was not registered by discovery");
    kb::tests::Require(sceneMetadata->dependencies.empty(), "Sidecarless scene asset reported dependencies without a sidecar");
    const kb::assets::AssetCompatibilityReport report =
        runtime.Assets().Manager().ValidateCompatibility(sceneMetadata->id);
    const std::string diagnostics = report.FormatDiagnostics();
    kb::tests::Require(!report.compatible, "A sidecarless scene passed compatibility validation");
    kb::tests::Require(diagnostics.find("/Game/Scenes/Sidecarless.21kbscene") != std::string::npos &&
            diagnostics.find("no readable scene metadata sidecar") != std::string::npos &&
            diagnostics.find("could not be opened") != std::string::npos,
        "A sidecarless scene validation error did not identify the scene and metadata failure");
}

void RunPackagedSceneDoesNotRequireLooseMetaTest() {
    const kb::assets::AssetMetadata metadata{
        .id = kb::assets::AssetId{ 0x5343454E45U },
        .type = "Scene",
        .name = "PackagedScene",
        .virtualPath = "/Game/Scenes/Packaged.21kbscene",
        .sourceExtension = ".21kbscene",
        .contentHash = 1U,
    };
    const auto runtimePack = std::make_shared<kb::assets::bake::RuntimeAssetPack>();
    const kb::assets::AssetLoadRequest request{
        .metadata = metadata,
        .resolvedPath = {},
        .runtimePack = runtimePack,
    };
    const kb::assets::AssetRegistry registry;
    kb::scene::SceneAssetLoader loader;
    kb::tests::Require(
        !loader.ValidateRuntimeDependencies(request, registry).has_value(),
        "A packaged scene was rejected because its authored .meta sidecar was not deployed");
}


[[nodiscard]] std::string ReadTextFileContents(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    kb::tests::Require(input.is_open(), "Asset runtime test file could not be read");
    return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

void ReplaceAllOccurrences(std::string& text, std::string_view from, std::string_view to) {
    std::size_t position = text.find(from);
    while (position != std::string::npos) {
        text.replace(position, from.size(), to);
        position = text.find(from, position + to.size());
    }
}

[[nodiscard]] kb::assets::AssetId RegistryIdFor(std::string_view virtualPath, std::string_view type) {
    return kb::assets::MakeAssetId(kb::assets::NormalizeAssetPath(std::string{ virtualPath }) + ":" + std::string{ type });
}

// Copying a .kbprefab copies its guid, so two prefab FILES can declare the same
// guid with different contents. Discovery resolves the scene's nested-prefab
// reference by "lowest registry id wins" (SceneAssetLoader.cpp
// BuildNestedPrefabGuidIndex); the prefab runtime resolves the very same guid by
// "first loaded owns the guid" (ScenePrefabRegistrationService::RegisterLoaded -
// a second file with the same guid and different content is re-registered under a
// freshly generated guid). The two rules are unrelated, so the file the dependency
// graph names and the file the runtime instantiates can be different files, and
// nothing anywhere reports the collision.
//
// Turns red if: the two rules disagree for a given pair of colliding files - which
// is exactly the state that makes a cooked build ship one prefab and run another.
void RunSceneAssetNestedPrefabGuidCollisionTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "GuidCollisionProject";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";

    kb::scene::Scene authoring;
    kb::tests::Require(authoring.Assets().MountProject(projectRoot), "Guid collision project mount failed");
    const kb::scene::SceneObject root = authoring.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Alpha Root" });
    const kb::scene::ScenePrefabHandle handle = authoring.Prefabs().CaptureRegistered(root, "AlphaPrefab");
    kb::tests::Require(handle.IsValid(), "Guid collision prefab registration failed");
    const std::filesystem::path alphaPath = assetsRoot / "Prefabs" / "Alpha.kbprefab";
    kb::tests::Require(authoring.Prefabs().Save(handle, alphaPath), "Guid collision prefab save failed");
    const std::string guid = authoring.Prefabs().Guid(handle);
    kb::tests::Require(!guid.empty(), "Guid collision prefab was saved without a guid");

    // The copy keeps the guid and changes the content - a copied-then-edited prefab.
    std::string betaText = ReadTextFileContents(alphaPath);
    ReplaceAllOccurrences(betaText, "AlphaPrefab", "BetaPrefab");
    ReplaceAllOccurrences(betaText, "Alpha Root", "Beta Root");
    const std::filesystem::path betaPath = assetsRoot / "Prefabs" / "Beta.kbprefab";
    WriteTextFile(betaPath, betaText);

    kb::scene::SceneDocument document;
    document.name = "CollisionScene";
    document.guid = "scene:CollisionScene";
    static_cast<void>(document.worldPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = 1U,
        .name = "Nested Prefab Node",
        .nestedPrefabGuid = guid,
    }));
    const std::filesystem::path scenePath = assetsRoot / "Scenes" / "CollisionScene.21kbscene";
    kb::tests::Require(kb::scene::SceneDocumentService::Save(document, scenePath), "Guid collision scene save failed");

    kb::scene::Scene runtime;
    kb::tests::Require(runtime.Assets().MountProject(projectRoot), "Guid collision runtime mount failed");
    static_cast<void>(runtime.Assets().Discover());
    const kb::assets::AssetMetadata* sceneMetadata =
        runtime.Assets().Manager().Registry().FindByPath("/Game/Scenes/CollisionScene.21kbscene");
    kb::tests::Require(sceneMetadata != nullptr, "Guid collision scene asset was not registered");

    const kb::assets::AssetId betaId = RegistryIdFor("/Game/Prefabs/Beta.kbprefab", "ScenePrefab");
    const bool dependencyNamesBeta = sceneMetadata->dependencies.size() == 1U &&
        sceneMetadata->dependencies[0].value == betaId.value;

    // Load Beta first: the prefab runtime binds the scene node's guid to Beta.
    kb::scene::Scene betaFirst;
    kb::tests::Require(betaFirst.Assets().MountProject(projectRoot), "Guid collision runtime-order mount failed");
    const kb::scene::ScenePrefabHandle betaHandle = betaFirst.Prefabs().Load(betaPath);
    const kb::scene::ScenePrefabHandle alphaHandle = betaFirst.Prefabs().Load(alphaPath);
    kb::tests::Require(betaHandle.IsValid() && alphaHandle.IsValid(), "Guid collision prefab load failed");
    const kb::scene::ScenePrefab bound = betaFirst.Prefabs().Get(betaHandle);
    const bool runtimeResolvesToBeta = betaFirst.Prefabs().Guid(betaHandle) == guid &&
        !bound.Nodes().empty() && bound.Nodes()[0].name == "Beta Root";

    kb::tests::Require(dependencyNamesBeta == runtimeResolvesToBeta,
        "Colliding prefab guids: the prefab the dependency graph names is not the prefab the runtime resolves that guid to");
}

// A scene node can reference assets through components the sidecar's dependency
// collector never reads (SceneAssetWriter.cpp CollectDependencies covers only
// audioMixer, meshRenderer mesh/material/materialSlot, nestedPrefab, audioClip,
// behaviour and particleEffect). Everything else - GeometrySwarm's mesh and
// material, SkeletonBinding's skeleton, DrawD3DeformedGeometry's skeletal mesh and
// material slots, WorldBackdrop's environment, ContentInstance's asset, Animator's
// controller - leaves no entry at all, so the scene is cooked without them.
//
// Turns red if a scene that renders a GeometrySwarm reports no dependency on the
// mesh and material that swarm draws.
void RunSceneAssetDependencyCoversEveryComponentTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "ComponentRolesProject";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    const std::filesystem::path sourceRoot = TestRoot() / "ComponentRolesSources";
    WriteTextFile(sourceRoot / "Swarm.obj", "o Swarm\nv 0.0 0.0 0.0\n");
    WriteTextFile(sourceRoot / "SwarmMat.mtl", "newmtl SwarmMat\n");

    kb::scene::Scene authoring;
    kb::tests::Require(authoring.Assets().MountProject(projectRoot), "Component roles project mount failed");
    const std::array<std::filesystem::path, 2> sources{ sourceRoot / "Swarm.obj", sourceRoot / "SwarmMat.mtl" };
    const kb::assets::AssetImportResult imported =
        kb::assets::AssetImportService::ImportFiles(authoring.Assets().Manager(), sources, "/Game/Imported");
    kb::tests::Require(imported.Succeeded() && imported.CreatedCount() == 2U, "Component roles source assets were not imported");
    const kb::assets::AssetId swarmMeshId = imported.items[0].id;
    const kb::assets::AssetId swarmMaterialId = imported.items[1].id;

    kb::scene::ScenePrefabNodeComponents components{};
    kb::scene::GeometrySwarmComponent swarm{};
    swarm.meshAssetId = swarmMeshId.value;
    swarm.materialAssetId = swarmMaterialId.value;
    swarm.enabled = true;
    components.geometrySwarm = swarm;

    kb::scene::SceneDocument document;
    document.name = "ComponentRolesScene";
    document.guid = "scene:ComponentRolesScene";
    static_cast<void>(document.worldPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = 1U,
        .name = "Swarm Node",
        .components = components,
    }));
    const std::filesystem::path scenePath = assetsRoot / "Scenes" / "ComponentRolesScene.21kbscene";
    kb::tests::Require(kb::scene::SceneDocumentService::Save(document, scenePath), "Component roles scene save failed");

    kb::scene::Scene runtime;
    kb::tests::Require(runtime.Assets().MountProject(projectRoot), "Component roles runtime mount failed");
    static_cast<void>(runtime.Assets().Discover());
    const kb::assets::AssetMetadata* sceneMetadata =
        runtime.Assets().Manager().Registry().FindByPath("/Game/Scenes/ComponentRolesScene.21kbscene");
    kb::tests::Require(sceneMetadata != nullptr, "Component roles scene asset was not registered");
    kb::tests::Require(ContainsAssetId(sceneMetadata->dependencies, swarmMeshId),
        "A scene node's GeometrySwarm mesh never reaches the dependency graph");
    kb::tests::Require(ContainsAssetId(sceneMetadata->dependencies, swarmMaterialId),
        "A scene node's GeometrySwarm material never reaches the dependency graph");
}

// The scene now names its nested prefab, but a graph is only useful if it
// continues: ScenePrefabAssetLoader declares no DiscoverDependencies at all, so a
// prefab's own mesh and material are not edges and a cook that follows the graph
// from a scene still stops one hop short of the art.
//
// Turns red while a registered prefab asset reports an empty dependency list
// although the prefab file references a registered mesh.
void RunScenePrefabAssetDependencyDiscoveryTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "PrefabEdgeProject";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    const std::filesystem::path sourceRoot = TestRoot() / "PrefabEdgeSources";
    WriteTextFile(sourceRoot / "Block.obj", "o Block\nv 0.0 0.0 0.0\n");

    kb::scene::Scene authoring;
    kb::tests::Require(authoring.Assets().MountProject(projectRoot), "Prefab edge project mount failed");
    const std::array<std::filesystem::path, 1> sources{ sourceRoot / "Block.obj" };
    const kb::assets::AssetImportResult imported =
        kb::assets::AssetImportService::ImportFiles(authoring.Assets().Manager(), sources, "/Game/Imported");
    kb::tests::Require(imported.Succeeded() && imported.CreatedCount() == 1U, "Prefab edge mesh was not imported");
    const kb::assets::AssetId meshId = imported.items[0].id;

    const kb::scene::SceneObject prefabRoot =
        authoring.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Art Root" });
    kb::scene::MeshRendererComponent meshRenderer{};
    meshRenderer.meshAssetId = meshId.value;
    authoring.Components().MeshRenderers().Set(prefabRoot.Entity(), meshRenderer);
    const kb::scene::ScenePrefabHandle handle = authoring.Prefabs().CaptureRegistered(prefabRoot, "ArtPrefab");
    kb::tests::Require(handle.IsValid(), "Prefab edge prefab registration failed");
    kb::tests::Require(authoring.Prefabs().Save(handle, assetsRoot / "Prefabs" / "Art.kbprefab"),
        "Prefab edge prefab save failed");

    kb::scene::Scene runtime;
    kb::tests::Require(runtime.Assets().MountProject(projectRoot), "Prefab edge runtime mount failed");
    static_cast<void>(runtime.Assets().Discover());
    const kb::assets::AssetMetadata* prefabMetadata =
        runtime.Assets().Manager().Registry().FindByPath("/Game/Prefabs/Art.kbprefab");
    kb::tests::Require(prefabMetadata != nullptr, "Prefab edge prefab asset was not registered");
    kb::tests::Require(ContainsAssetId(prefabMetadata->dependencies, meshId),
        "A prefab asset does not report the mesh it renders - the dependency graph stops at the prefab");
}

void RunMissingNestedPrefabDependencyValidationTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "MissingNestedPrefabProject";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    const std::string missingGuid = "missing:nested-prefab-guid";

    kb::scene::Scene authoring;
    kb::tests::Require(authoring.Assets().MountProject(projectRoot),
        "Missing nested prefab project mount failed");

    kb::scene::ScenePrefab outerPrefab;
    static_cast<void>(outerPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = 1U,
        .name = "Missing Nested Prefab Node",
        .nestedPrefabGuid = missingGuid,
    }));
    const kb::scene::ScenePrefabHandle outerHandle =
        authoring.Prefabs().Register("OuterPrefab", std::move(outerPrefab));
    kb::tests::Require(outerHandle.IsValid() &&
            authoring.Prefabs().Save(outerHandle, assetsRoot / "Prefabs" / "Outer.kbprefab"),
        "Missing nested prefab outer asset save failed");

    kb::scene::SceneDocument document;
    document.name = "MissingNestedPrefabScene";
    document.guid = "scene:MissingNestedPrefabScene";
    static_cast<void>(document.worldPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = 1U,
        .name = "Missing Nested Prefab Node",
        .nestedPrefabGuid = missingGuid,
    }));
    kb::tests::Require(kb::scene::SceneDocumentService::Save(
            document, assetsRoot / "Scenes" / "MissingNestedPrefab.21kbscene"),
        "Missing nested prefab scene save failed");

    kb::scene::Scene runtime;
    kb::tests::Require(runtime.Assets().MountProject(projectRoot),
        "Missing nested prefab runtime project mount failed");
    static_cast<void>(runtime.Assets().Discover());
    const kb::assets::AssetRegistry& registry = runtime.Assets().Manager().Registry();
    for (const std::string_view virtualPath : {
             std::string_view{ "/Game/Scenes/MissingNestedPrefab.21kbscene" },
             std::string_view{ "/Game/Prefabs/Outer.kbprefab" },
         }) {
        const kb::assets::AssetMetadata* metadata = registry.FindByPath(virtualPath);
        kb::tests::Require(metadata != nullptr,
            "Missing nested prefab fixture asset was not registered");
        kb::tests::Require(metadata->dependencies.empty(),
            "An unresolved prefab guid produced a dependency edge to a nonexistent asset");
        const kb::assets::AssetCompatibilityReport report =
            runtime.Assets().Manager().ValidateCompatibility(metadata->id);
        const std::string diagnostics = report.FormatDiagnostics();
        kb::tests::Require(!report.compatible &&
                diagnostics.find(std::string{ virtualPath }) != std::string::npos &&
                diagnostics.find("no registered ScenePrefab asset declares") != std::string::npos,
            "An unresolved nested prefab guid passed compatibility validation without a clear diagnostic");
    }
}

// A sidecar that is empty, truncated, wrongly magicked or filled with noise must
// not take discovery down with it or conjure dependencies, but each form must be
// rejected by compatibility validation before a cooker can use the closure.
//
// Turns red if a damaged sidecar crashes discovery, costs the scene its
// registration, or yields dependency entries.
void RunSceneAssetDependencyDamagedSidecarTest() {
    const std::array<std::string, 5> payloads{
        std::string{},
        std::string{ "garbage-not-a-meta-file" },
        std::string{ "21KBMETA" },
        std::string(4096U, '\xAB'),
        std::string{ "\x00\x01\x02\x03", 4U },
    };
    for (const std::string& payload : payloads) {
        ResetTestRoot();
        const std::filesystem::path projectRoot = TestRoot() / "DamagedSidecarProject";
        const std::filesystem::path scenePath = projectRoot / "Assets" / "Scenes" / "Damaged.21kbscene";

        kb::scene::SceneDocument document;
        document.name = "Damaged";
        document.guid = "scene:Damaged";
        static_cast<void>(document.worldPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .stableId = 1U, .name = "Node" }));
        kb::tests::Require(kb::scene::SceneDocumentService::Save(document, scenePath), "Damaged sidecar scene save failed");
        WriteTextFile(kb::scene::SceneAssetMetaPath(scenePath), payload);

        kb::scene::Scene runtime;
        kb::tests::Require(runtime.Assets().MountProject(projectRoot), "Damaged sidecar project mount failed");
        static_cast<void>(runtime.Assets().Discover());
        const kb::assets::AssetMetadata* sceneMetadata =
            runtime.Assets().Manager().Registry().FindByPath("/Game/Scenes/Damaged.21kbscene");
        kb::tests::Require(sceneMetadata != nullptr, "A damaged sidecar cost the scene its registration");
        kb::tests::Require(sceneMetadata->dependencies.empty(), "A damaged sidecar produced dependency entries");
        const kb::assets::AssetCompatibilityReport report =
            runtime.Assets().Manager().ValidateCompatibility(sceneMetadata->id);
        const std::string diagnostics = report.FormatDiagnostics();
        kb::tests::Require(!report.compatible, "A damaged scene sidecar passed compatibility validation");
        kb::tests::Require(diagnostics.find("/Game/Scenes/Damaged.21kbscene") != std::string::npos &&
                diagnostics.find("no readable scene metadata sidecar") != std::string::npos,
            "A damaged scene sidecar validation error did not identify the scene and metadata failure");
    }
}

void RunSceneAssetDependencyStaleSidecarTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "StaleSidecarProject";
    const std::filesystem::path scenePath =
        projectRoot / "Assets" / "Scenes" / "Stale.21kbscene";
    const std::filesystem::path metaPath = kb::scene::SceneAssetMetaPath(scenePath);
    const std::filesystem::path oldMetaPath = TestRoot() / "Stale.original.meta";

    kb::scene::SceneDocument original;
    original.name = "Stale";
    original.guid = "scene:Stale";
    static_cast<void>(original.worldPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = 1U,
        .name = "Node",
    }));
    kb::tests::Require(kb::scene::SceneDocumentService::Save(original, scenePath),
        "Stale sidecar original scene save failed");
    std::error_code copyError;
    kb::tests::Require(std::filesystem::copy_file(metaPath, oldMetaPath, copyError) && !copyError,
        "Stale sidecar original metadata backup failed");

    kb::scene::SceneDocument updated;
    updated.name = original.name;
    updated.guid = original.guid;
    static_cast<void>(updated.worldPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = 1U,
        .name = "Node",
        .components = kb::scene::ScenePrefabNodeComponents{
            .meshRenderer = kb::scene::MeshRendererComponent{
                .meshAssetId = 0x1234U,
                .materialAssetId = 0x5678U,
            },
        },
    }));
    kb::tests::Require(kb::scene::SceneDocumentService::Save(updated, scenePath),
        "Stale sidecar updated scene save failed");
    copyError.clear();
    kb::tests::Require(std::filesystem::copy_file(
            oldMetaPath, metaPath, std::filesystem::copy_options::overwrite_existing, copyError) && !copyError,
        "Stale sidecar metadata restore failed");

    kb::scene::Scene runtime;
    kb::tests::Require(runtime.Assets().MountProject(projectRoot), "Stale sidecar project mount failed");
    static_cast<void>(runtime.Assets().Discover());
    const kb::assets::AssetMetadata* sceneMetadata =
        runtime.Assets().Manager().Registry().FindByPath("/Game/Scenes/Stale.21kbscene");
    kb::tests::Require(sceneMetadata != nullptr, "A stale sidecar cost the scene its registration");
    kb::tests::Require(sceneMetadata->dependencies.empty(),
        "The stale sidecar fixture did not expose its outdated dependency closure");
    const kb::assets::AssetCompatibilityReport report =
        runtime.Assets().Manager().ValidateCompatibility(sceneMetadata->id);
    const std::string diagnostics = report.FormatDiagnostics();
    kb::tests::Require(!report.compatible, "A well-formed but stale scene sidecar passed compatibility validation");
    kb::tests::Require(diagnostics.find("/Game/Scenes/Stale.21kbscene") != std::string::npos &&
            diagnostics.find("integrity does not match") != std::string::npos,
        "A stale scene sidecar validation error did not identify the scene integrity mismatch");
}

void RunSceneAssetDependencyChangesAfterDiscoveryTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "ChangedAfterDiscoveryProject";
    const std::filesystem::path scenePath =
        projectRoot / "Assets" / "Scenes" / "Changing.21kbscene";

    kb::scene::SceneDocument original;
    original.name = "Changing";
    original.guid = "scene:Changing";
    static_cast<void>(original.worldPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = 1U,
        .name = "Node",
    }));
    kb::tests::Require(kb::scene::SceneDocumentService::Save(original, scenePath),
        "Changed-after-discovery original scene save failed");

    kb::scene::Scene runtime;
    kb::tests::Require(runtime.Assets().MountProject(projectRoot),
        "Changed-after-discovery project mount failed");
    static_cast<void>(runtime.Assets().Discover());
    const kb::assets::AssetMetadata* sceneMetadata =
        runtime.Assets().Manager().Registry().FindByPath("/Game/Scenes/Changing.21kbscene");
    kb::tests::Require(sceneMetadata != nullptr && sceneMetadata->dependencies.empty(),
        "Changed-after-discovery original dependency snapshot was not empty");
    const kb::assets::AssetId sceneId = sceneMetadata->id;

    kb::scene::SceneDocument updated;
    updated.name = original.name;
    updated.guid = original.guid;
    static_cast<void>(updated.worldPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = 1U,
        .name = "Node",
        .components = kb::scene::ScenePrefabNodeComponents{
            .meshRenderer = kb::scene::MeshRendererComponent{
                .meshAssetId = 0x1234U,
                .materialAssetId = 0x5678U,
            },
        },
    }));
    kb::tests::Require(kb::scene::SceneDocumentService::Save(updated, scenePath),
        "Changed-after-discovery updated scene save failed");

    const kb::assets::AssetCompatibilityReport report =
        runtime.Assets().Manager().ValidateCompatibility(sceneId);
    const std::string diagnostics = report.FormatDiagnostics();
    kb::tests::Require(!report.compatible,
        "A scene changed after dependency discovery passed compatibility validation");
    kb::tests::Require(diagnostics.find("/Game/Scenes/Changing.21kbscene") != std::string::npos &&
            diagnostics.find("changed after asset discovery") != std::string::npos,
        "A scene changed after discovery did not request a clean retry");
}

// Discovery registers every asset before it refreshes any dependency list
// (AssetDiscoveryService.cpp: the dependency pass runs over registry.All() after
// the scan loop), so a scene scanned before its prefab must still get the edge.
//
// Turns red if dependency discovery is ever moved into the scan loop, where a
// scene would resolve its nested-prefab guid against a half-filled registry and
// silently drop the reference.
void RunSceneAssetDependencyIgnoresScanOrderTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "ScanOrderProject";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";

    kb::scene::Scene authoring;
    kb::tests::Require(authoring.Assets().MountProject(projectRoot), "Scan order project mount failed");
    const kb::scene::SceneObject root = authoring.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Late Root" });
    const kb::scene::ScenePrefabHandle handle = authoring.Prefabs().CaptureRegistered(root, "LatePrefab");
    kb::tests::Require(handle.IsValid(), "Scan order prefab registration failed");
    // "ZZZ" sorts after "AAA", so the directory walk reaches the scene first.
    kb::tests::Require(authoring.Prefabs().Save(handle, assetsRoot / "ZZZ" / "Late.kbprefab"), "Scan order prefab save failed");
    const std::string guid = authoring.Prefabs().Guid(handle);

    kb::scene::SceneDocument document;
    document.name = "ScanOrderScene";
    document.guid = "scene:ScanOrderScene";
    static_cast<void>(document.worldPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = 1U,
        .name = "Nested Prefab Node",
        .nestedPrefabGuid = guid,
    }));
    kb::tests::Require(kb::scene::SceneDocumentService::Save(document, assetsRoot / "AAA" / "ScanOrder.21kbscene"),
        "Scan order scene save failed");

    kb::scene::Scene runtime;
    kb::tests::Require(runtime.Assets().MountProject(projectRoot), "Scan order runtime mount failed");
    static_cast<void>(runtime.Assets().Discover());
    const kb::assets::AssetRegistry& registry = runtime.Assets().Manager().Registry();
    const kb::assets::AssetMetadata* sceneMetadata = registry.FindByPath("/Game/AAA/ScanOrder.21kbscene");
    const kb::assets::AssetMetadata* prefabMetadata = registry.FindByPath("/Game/ZZZ/Late.kbprefab");
    kb::tests::Require(sceneMetadata != nullptr && prefabMetadata != nullptr, "Scan order assets were not registered");
    kb::tests::Require(ContainsAssetId(sceneMetadata->dependencies, prefabMetadata->id),
        "A scene scanned before its nested prefab lost the dependency edge");
}

// The rule a contested prefab guid resolves by, pinned from both ends.
//
// A guid names an identity, so exactly one file may declare it: while that holds,
// a scene's nested-prefab reference resolves to that one prefab asset. Once a
// second file declares the same guid the reference resolves to NOTHING - in the
// dependency graph and in the prefab runtime alike - and the collision is
// reported by name instead of a file being chosen.
//
// Turns red if "resolves to nothing" is ever traded back for a rule that picks a
// winner: any such rule (lowest registry id, highest, first scanned, first
// loaded) makes the contested scene report an edge again, and any rule that
// depends on load order makes the two runtime orders below disagree. It also
// turns red if a UNIQUE guid stops resolving, so the fix cannot degenerate into
// "nested prefabs are never edges".
void RunSceneAssetNestedPrefabGuidRuleTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "GuidRuleProject";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    const std::filesystem::path alphaPath = assetsRoot / "Prefabs" / "Alpha.kbprefab";
    const std::filesystem::path betaPath = assetsRoot / "Prefabs" / "Beta.kbprefab";

    kb::scene::Scene authoring;
    kb::tests::Require(authoring.Assets().MountProject(projectRoot), "Guid rule project mount failed");
    const kb::scene::SceneObject root = authoring.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Alpha Root" });
    const kb::scene::ScenePrefabHandle handle = authoring.Prefabs().CaptureRegistered(root, "AlphaPrefab");
    kb::tests::Require(handle.IsValid(), "Guid rule prefab registration failed");
    kb::tests::Require(authoring.Prefabs().Save(handle, alphaPath), "Guid rule prefab save failed");
    const std::string guid = authoring.Prefabs().Guid(handle);
    kb::tests::Require(!guid.empty(), "Guid rule prefab was saved without a guid");

    kb::scene::SceneDocument document;
    document.name = "GuidRuleScene";
    document.guid = "scene:GuidRuleScene";
    static_cast<void>(document.worldPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = 1U,
        .name = "Nested Prefab Node",
        .nestedPrefabGuid = guid,
    }));
    const std::filesystem::path scenePath = assetsRoot / "Scenes" / "GuidRuleScene.21kbscene";
    kb::tests::Require(kb::scene::SceneDocumentService::Save(document, scenePath), "Guid rule scene save failed");

    const kb::assets::AssetId alphaId = RegistryIdFor("/Game/Prefabs/Alpha.kbprefab", "ScenePrefab");
    const kb::assets::AssetId betaId = RegistryIdFor("/Game/Prefabs/Beta.kbprefab", "ScenePrefab");

    // One file declares the guid: the reference resolves, and to that file.
    kb::scene::Scene unique;
    kb::tests::Require(unique.Assets().MountProject(projectRoot), "Guid rule unique-claim mount failed");
    static_cast<void>(unique.Assets().Discover());
    const kb::assets::AssetMetadata* uniqueScene =
        unique.Assets().Manager().Registry().FindByPath("/Game/Scenes/GuidRuleScene.21kbscene");
    kb::tests::Require(uniqueScene != nullptr, "Guid rule scene was not registered");
    kb::tests::Require(uniqueScene->dependencies.size() == 1U && uniqueScene->dependencies[0].value == alphaId.value,
        "A prefab guid declared by exactly one asset must resolve to that asset");
    kb::tests::Require(unique.Assets().Manager().ValidateCompatibility(uniqueScene->id).compatible,
        "A scene nesting an unambiguously identified prefab must validate");

    // The copy keeps the guid and changes the content - a copied-then-edited prefab.
    std::string betaText = ReadTextFileContents(alphaPath);
    ReplaceAllOccurrences(betaText, "AlphaPrefab", "BetaPrefab");
    ReplaceAllOccurrences(betaText, "Alpha Root", "Beta Root");
    WriteTextFile(betaPath, betaText);

    kb::scene::ScenePrefab outerPrefab;
    static_cast<void>(outerPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = 1U,
        .name = "Contested Nested Prefab Node",
        .nestedPrefabGuid = guid,
    }));
    const kb::scene::ScenePrefabHandle outerHandle =
        authoring.Prefabs().Register("OuterPrefab", std::move(outerPrefab));
    kb::tests::Require(outerHandle.IsValid() && authoring.Prefabs().Save(
            outerHandle, assetsRoot / "Prefabs" / "Outer.kbprefab"),
        "Guid rule outer prefab save failed");

    kb::scene::Scene contested;
    kb::tests::Require(contested.Assets().MountProject(projectRoot), "Guid rule contested-claim mount failed");
    static_cast<void>(contested.Assets().Discover());
    const kb::assets::AssetMetadata* contestedScene =
        contested.Assets().Manager().Registry().FindByPath("/Game/Scenes/GuidRuleScene.21kbscene");
    kb::tests::Require(contestedScene != nullptr, "Guid rule scene lost its registration to the collision");
    kb::tests::Require(contestedScene->dependencies.empty(),
        "A prefab guid two files declare must name neither of them in the dependency graph");
    kb::tests::Require(!ContainsAssetId(contestedScene->dependencies, alphaId) &&
            !ContainsAssetId(contestedScene->dependencies, betaId),
        "A contested prefab guid still picked a winner in the dependency graph");

    // ...and the collision is reported, naming both files rather than staying silent.
    const kb::assets::AssetCompatibilityReport report =
        contested.Assets().Manager().ValidateCompatibility(contestedScene->id);
    const std::string diagnostics = report.FormatDiagnostics();
    kb::tests::Require(!report.compatible, "A contested prefab guid was not reported at all");
    kb::tests::Require(diagnostics.find("/Game/Prefabs/Alpha.kbprefab") != std::string::npos &&
            diagnostics.find("/Game/Prefabs/Beta.kbprefab") != std::string::npos,
        "The prefab guid collision diagnostic does not name both colliding files");

    const kb::assets::AssetMetadata* contestedOuter =
        contested.Assets().Manager().Registry().FindByPath("/Game/Prefabs/Outer.kbprefab");
    kb::tests::Require(contestedOuter != nullptr && contestedOuter->dependencies.empty(),
        "A prefab nesting a contested guid unexpectedly chose one claimant");
    const kb::assets::AssetCompatibilityReport outerReport =
        contested.Assets().Manager().ValidateCompatibility(contestedOuter->id);
    const std::string outerDiagnostics = outerReport.FormatDiagnostics();
    kb::tests::Require(!outerReport.compatible &&
            outerDiagnostics.find("/Game/Prefabs/Alpha.kbprefab") != std::string::npos &&
            outerDiagnostics.find("/Game/Prefabs/Beta.kbprefab") != std::string::npos,
        "A prefab nesting a contested guid passed validation or failed to name both claimants");

    // The prefab runtime resolves the contested guid the same way - to nothing -
    // and does so whichever file is loaded first.
    for (const bool loadBetaFirst : { false, true }) {
        kb::scene::Scene runtime;
        kb::tests::Require(runtime.Assets().MountProject(projectRoot), "Guid rule runtime mount failed");
        const kb::scene::ScenePrefabHandle first = runtime.Prefabs().Load(loadBetaFirst ? betaPath : alphaPath);
        const kb::scene::ScenePrefabHandle second = runtime.Prefabs().Load(loadBetaFirst ? alphaPath : betaPath);
        kb::tests::Require(first.IsValid() && second.IsValid(),
            "A prefab guid collision must not cost either file its prefab record");
        kb::tests::Require(runtime.Prefabs().Guid(first) != guid && runtime.Prefabs().Guid(second) != guid,
            "A contested prefab guid still names a prefab at runtime, so load order decides which one");
    }

    // Deleting the duplicate returns the identity to its owner: the rule is a
    // refusal to guess, not a permanent loss of the edge.
    std::error_code removeError;
    kb::tests::Require(std::filesystem::remove(betaPath, removeError) && !removeError,
        "Guid rule test could not remove the duplicate prefab");
    kb::scene::Scene repaired;
    kb::tests::Require(repaired.Assets().MountProject(projectRoot), "Guid rule repaired mount failed");
    static_cast<void>(repaired.Assets().Discover());
    const kb::assets::AssetMetadata* repairedScene =
        repaired.Assets().Manager().Registry().FindByPath("/Game/Scenes/GuidRuleScene.21kbscene");
    kb::tests::Require(repairedScene != nullptr &&
            repairedScene->dependencies.size() == 1U &&
            repairedScene->dependencies[0].value == alphaId.value,
        "Removing the duplicate prefab did not restore the nested-prefab edge");
    static_cast<void>(betaId);
}

// Discovery must cost the same order of work whether or not the scenes nest
// prefabs. Resolving a nested-prefab guid needs an index over every prefab
// asset's guid; building that index per SCENE makes a discovery pass
// O(scenes x prefabs) file opens, which is invisible on a toy project and turns
// a re-discovery cycle on a real one into tens of seconds.
//
// The threshold is a RATIO against an identical project whose scenes nest
// nothing, not a wall-clock budget, so it does not depend on the machine. The
// per-scene index rebuild measured 10x on a 200x200 project and grew with
// project size; building it once per pass measures at parity, so 4x separates
// the two with room to spare on a loaded machine.
void RunSceneAssetDiscoveryStaysLinearInNestedPrefabsTest() {
    ResetTestRoot();
    constexpr std::size_t kPrefabCount = 150U;
    constexpr std::size_t kSceneCount = 150U;
    constexpr double kMaximumNestedOverheadRatio = 3.0;

    // Both projects hold the SAME number of prefab and scene files, so the cost of
    // scanning, hashing and parsing them cancels in the ratio and what is left is
    // exactly the price of resolving the nested-prefab references.
    const auto buildProject = [](const std::filesystem::path& projectRoot, bool nested) {
        const std::filesystem::path assetsRoot = projectRoot / "Assets";
        kb::scene::Scene authoring;
        kb::tests::Require(authoring.Assets().MountProject(projectRoot), "Discovery scaling project mount failed");

        std::vector<std::string> guids;
        guids.reserve(kPrefabCount);
        for (std::size_t index = 0U; index < kPrefabCount; ++index) {
            const std::string suffix = std::to_string(index);
            const kb::scene::SceneObject prefabRoot =
                authoring.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Prefab Root " + suffix });
            const kb::scene::ScenePrefabHandle prefabHandle =
                authoring.Prefabs().CaptureRegistered(prefabRoot, "Prefab" + suffix);
            kb::tests::Require(prefabHandle.IsValid(), "Discovery scaling prefab registration failed");
            kb::tests::Require(
                authoring.Prefabs().Save(prefabHandle, assetsRoot / "Prefabs" / ("Prefab" + suffix + ".kbprefab")),
                "Discovery scaling prefab save failed");
            guids.push_back(authoring.Prefabs().Guid(prefabHandle));
            kb::tests::Require(!guids.back().empty(), "Discovery scaling prefab was saved without a guid");
        }

        for (std::size_t index = 0U; index < kSceneCount; ++index) {
            const std::string suffix = std::to_string(index);
            kb::scene::SceneDocument document;
            document.name = "Scene" + suffix;
            document.guid = "scene:Scene" + suffix;
            kb::scene::ScenePrefabNodeDesc node{ .stableId = 1U, .name = "Node " + suffix };
            if (nested) {
                node.nestedPrefabGuid = guids[index % kPrefabCount];
            }
            static_cast<void>(document.worldPrefab.AddNode(node));
            kb::tests::Require(
                kb::scene::SceneDocumentService::Save(document, assetsRoot / "Scenes" / ("Scene" + suffix + ".21kbscene")),
                "Discovery scaling scene save failed");
        }
    };

    // The first pass warms the file cache and fills the registry; the second is the
    // steady-state re-discovery cycle the runtime actually repeats.
    const auto measureRediscovery = [](kb::scene::Scene& runtime, const std::filesystem::path& projectRoot) {
        kb::tests::Require(runtime.Assets().MountProject(projectRoot), "Discovery scaling runtime mount failed");
        kb::tests::Require(runtime.Assets().Discover() == kPrefabCount + kSceneCount,
            "Discovery scaling project did not register every prefab and scene");
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        static_cast<void>(runtime.Assets().Discover());
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };

    const std::filesystem::path flatRoot = TestRoot() / "DiscoveryScalingFlat";
    const std::filesystem::path nestedRoot = TestRoot() / "DiscoveryScalingNested";
    buildProject(flatRoot, false);
    buildProject(nestedRoot, true);

    kb::scene::Scene flatRuntime;
    kb::scene::Scene nestedRuntime;
    const double flatMilliseconds = measureRediscovery(flatRuntime, flatRoot);
    const double nestedMilliseconds = measureRediscovery(nestedRuntime, nestedRoot);

    // A pass that resolved nothing would make the ratio meaningless, so prove the
    // edges are actually there before trusting the timing.
    const kb::assets::AssetRegistry& nestedRegistry = nestedRuntime.Assets().Manager().Registry();
    for (const std::size_t index : { std::size_t{ 0U }, kSceneCount / 2U, kSceneCount - 1U }) {
        const std::string suffix = std::to_string(index);
        const kb::assets::AssetMetadata* scene =
            nestedRegistry.FindByPath("/Game/Scenes/Scene" + suffix + ".21kbscene");
        const kb::assets::AssetMetadata* prefab =
            nestedRegistry.FindByPath("/Game/Prefabs/Prefab" + std::to_string(index % kPrefabCount) + ".kbprefab");
        kb::tests::Require(scene != nullptr && prefab != nullptr, "Discovery scaling assets were not registered");
        kb::tests::Require(scene->dependencies.size() == 1U && scene->dependencies[0].value == prefab->id.value,
            "Discovery scaling scene did not resolve its nested prefab, so the timing proves nothing");
    }

    const double ratio = nestedMilliseconds / (flatMilliseconds > 0.001 ? flatMilliseconds : 0.001);
    const std::string overspend = "Nesting a prefab in every scene made discovery cost " + std::to_string(ratio) +
        "x a nesting-free project of the same size (" + std::to_string(nestedMilliseconds) + " ms vs " +
        std::to_string(flatMilliseconds) + " ms): the prefab guid index is being rebuilt per scene again";
    kb::tests::Require(ratio <= kMaximumNestedOverheadRatio, overspend.c_str());
}

} // namespace

namespace kb::tests {

void RunAssetRuntimeTests() {
    RunAssetManagerDiscoveryCacheAndManifestTest();
    RunSingleAssetRefreshTest();
    RunAssetManagerRuntimePublicationTest();
    RunAssetManagerLoadOpaqueTest();
    RunAssetManagerTrueAsyncLoadTest();
    RunAssetManagerAsyncLoaderReplacementTest();
    RunAssetManagerNewLoaderPreservesRetainedAssetsTest();
    RunAssetCacheReferenceAndPolicyTest();
    RunAssetCompatibilityValidationTest();
    RunAssetKindClassificationTest();
    RunAssetDiscoveryPreservesEditorLiveOverrideTest();
    RunAssetManagerFolderAndRenameOperationsTest();
    RunAssetImportServiceBinaryContainerTest();
    RunAssetImportServiceReportsCreatedReusedMissingAndUnsupportedTest();
    RunScenePrefabRuntimeAssetTest();
    RunSceneAssetDependencyDiscoveryTest();
    RunSceneAssetDependencyWithoutSidecarTest();
    RunPackagedSceneDoesNotRequireLooseMetaTest();
    RunSceneAssetDependencyDamagedSidecarTest();
    RunSceneAssetDependencyStaleSidecarTest();
    RunSceneAssetDependencyChangesAfterDiscoveryTest();
    RunSceneAssetDependencyIgnoresScanOrderTest();
    RunSceneAssetNestedPrefabGuidCollisionTest();
    RunSceneAssetDependencyCoversEveryComponentTest();
    RunScenePrefabAssetDependencyDiscoveryTest();
    RunMissingNestedPrefabDependencyValidationTest();
    RunSceneAssetNestedPrefabGuidRuleTest();
    RunSceneAssetDiscoveryStaysLinearInNestedPrefabsTest();
    RunSceneAudioClipAssetDiscoveryTest();
    RunAudioClipFormatCatalogTest();
    RunScriptAssetPipelineTest();
    RunInspectorDeclarationParseTest();
}

} // namespace kb::tests
