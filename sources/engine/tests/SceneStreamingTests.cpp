#include "TestSupport.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneLoadedContent.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/SceneStreamingService.hpp"
#include "engine/assets/bake/AssetPackWriter.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/assets/bake/RuntimeAssetManifest.hpp"
#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>
#include <iostream>
#include <unordered_map>
#include "engine/ecs/World.hpp"

namespace kb::tests {

namespace {
struct StreamingProviderProbe {
    std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> resources;
    std::size_t created = 0U;
    std::size_t activated = 0U;
    std::size_t removed = 0U;
    static void Mesh(kb::ecs::Entity entity, kb::ecs::ComponentEventKind event,
        const kb::scene::MeshRendererComponent*, void* context) {
        auto& probe = *static_cast<StreamingProviderProbe*>(context);
        if (event == kb::ecs::ComponentEventKind::Added) {
            probe.resources.emplace(entity.Id(), std::vector<std::uint8_t>(256U * 1024U, 42U));
            ++probe.created;
        } else if (event == kb::ecs::ComponentEventKind::Removed) {
            probe.resources.erase(entity.Id());
            ++probe.removed;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    static void Body(kb::ecs::Entity, kb::ecs::ComponentEventKind,
        const kb::scene::RigidbodyComponent*, void* context) {
        ++static_cast<StreamingProviderProbe*>(context)->activated;
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
};

void VerifyStreamingProviderBudgetsAndCancellation(const std::filesystem::path& path) {
    using namespace kb::scene;
    SceneDocument document;
    document.name = "Provider streaming";
    document.guid = "scene:provider-streaming";
    for (unsigned i = 0; i < 8; ++i) {
        ScenePrefabNodeDesc node;
        node.name = "Resource" + std::to_string(i);
        node.components.meshRenderer = MeshRendererComponent{.meshAssetId = 42U};
        node.components.rigidbody = RigidbodyComponent{};
        static_cast<void>(document.worldPrefab.AddNode(std::move(node)));
    }
    Require(SceneDocumentService::Save(document, path), "Provider streaming fixture must save");
    // Cancel while loading, after creation, during activation, and during unload.
    // The final case completes normally, including every resource callback.
    for (unsigned phase = 0; phase < 5; ++phase) {
        StreamingProviderProbe probe;
        Scene scene;
        auto& world = SceneAccess::State(scene).world;
        const auto added = world.ObserveComponent<MeshRendererComponent>(kb::ecs::ComponentEventKind::Added,
            &StreamingProviderProbe::Mesh, &probe);
        const auto removed = world.ObserveComponent<MeshRendererComponent>(kb::ecs::ComponentEventKind::Removed,
            &StreamingProviderProbe::Mesh, &probe);
        const auto body = world.ObserveComponent<RigidbodyComponent>(kb::ecs::ComponentEventKind::Added,
            &StreamingProviderProbe::Body, &probe);
        scene.LoadedContent().ConfigureStreaming({.maxPendingLoads = 2U, .maxOperationsPerFrame = 64U,
            .maxMillisecondsPerFrame = 0.5F});
        const auto id = scene.LoadedContent().LoadAsync(path);
        Require(id != 0U, "Provider streaming must start");
        if (phase == 0) SceneStreamingService::CancelPending(scene);
        bool stopped = phase == 0;
        bool repeatedUnload = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        while (std::chrono::steady_clock::now() < deadline) {
            const auto beforeActivated = probe.activated;
            const auto beforeRemoved = probe.removed;
            static_cast<void>(scene.Runtime().Update(0.0F));
            const auto stats = scene.LoadedContent().StreamingStats();
            Require(stats.operations <= 64U, "Provider callbacks must preserve the operation budget");
            Require(probe.activated - beforeActivated <= 1U && probe.removed - beforeRemoved <= 1U,
                "Time budget must be checked between costly activation/destruction callbacks");
            if (probe.activated != beforeActivated || probe.removed != beforeRemoved)
                Require(stats.milliseconds >= 1.0 && stats.overBudgetFrames > 0U,
                    "An indivisible provider overrun must be included in streaming diagnostics");
            const auto status = scene.LoadedContent().Status(id);
            Require(status != SceneLoadStatus::Failed, "Provider streaming unexpectedly failed");
            if (!stopped && ((phase == 1 && probe.created > 0U && probe.activated == 0U) ||
                            (phase == 2 && probe.activated > 0U && probe.activated < 8U))) {
                SceneStreamingService::CancelPending(scene);
                stopped = true;
            }
            if (!stopped && phase >= 3 && status == SceneLoadStatus::Ready) {
                Require(scene.LoadedContent().UnloadAsync(id), "Completed provider scene must unload");
                stopped = true;
            }
            if (phase == 3 && status == SceneLoadStatus::Unloading && !repeatedUnload) {
                SceneStreamingService::CancelPending(scene);
                Require(scene.LoadedContent().UnloadAsync(id), "Repeated unload must be safe");
                repeatedUnload = true;
            }
            if (stopped && (status == SceneLoadStatus::Cancelled || status == SceneLoadStatus::Unknown)) break;
            std::this_thread::yield();
        }
        Require(stopped && (scene.LoadedContent().Status(id) == (phase < 3 ? SceneLoadStatus::Cancelled : SceneLoadStatus::Unknown)),
            "Cancellation or unload did not reach its correct terminal state");
        Require(scene.Entities().Count() == 0U && probe.resources.empty() && probe.created == probe.removed &&
                SceneAccess::State(scene).inactiveEntities.empty(), "Provider resources or staged entities survived cancellation");
        if (phase != 0) Require(scene.LoadedContent().StreamingStats().overBudgetFrames > 0U,
            "Slow resource creation must report its indivisible batch overrun");
        if (phase >= 3) Require(probe.created == 8U && probe.activated == 8U, "The complete provider lifecycle was not exercised");
        Require(phase != 3 || repeatedUnload, "Cancellation during unload was not exercised");
        std::cout << "scene_provider_streaming phase=" << phase << " created=" << probe.created
                  << " activated=" << probe.activated << " removed=" << probe.removed
                  << " over_budget=" << scene.LoadedContent().StreamingStats().overBudgetFrames
                  << " longest_ms=" << scene.LoadedContent().StreamingStats().longestFrameMilliseconds << '\n';
        world.DestroyObserver(added);
        world.DestroyObserver(removed);
        world.DestroyObserver(body);
    }
    std::filesystem::remove(path);
}
}

void RunSceneStreamingTests() {
    using namespace kb::scene;
    SceneDocument document;
    document.name = "Streaming test";
    document.guid = "scene:streaming-test";
    for (std::uint32_t index = 0U; index < 513U; ++index) {
        ScenePrefabNodeDesc node;
        node.name = "Node" + std::to_string(index);
        node.parentNode = index == 0U ? ScenePrefabNodeDesc::NoParent : 0U;
        node.transform.localPosition.x = static_cast<float>(index);
        node.components.meshRenderer = MeshRendererComponent{.meshAssetId = 42U};
        if (index == 0U) node.components.rigidbody = RigidbodyComponent{};
        if (index == 512U) {
            node.components.rigidbody = RigidbodyComponent{};
            node.components.joint = ScenePrefabJointComponent{.connectedNodeStableId = 1U};
        }
        static_cast<void>(document.worldPrefab.AddNode(std::move(node)));
    }
    const auto path = std::filesystem::temp_directory_path() /
        ("kb_stream_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".21kbscene");
    VerifyStreamingProviderBudgetsAndCancellation(path.string() + ".providers.21kbscene");
    Require(SceneDocumentService::Save(document, path), "Streaming fixture must save");
    Scene privateScene{SceneMode::PrefabPrivate};
    Require(privateScene.LoadedContent().LoadAsync(path) == 0U,
        "Streaming must reject edit-only scenes whose runtime cannot pump jobs");
    Scene scene;
    const auto parent = scene.Entities().CreateEntity();
    scene.LoadedContent().ConfigureStreaming({.maxPendingLoads = 2U, .maxOperationsPerFrame = 7U, .maxMillisecondsPerFrame = 20.0F});
    auto wait = [&](std::uint64_t id, SceneLoadStatus status) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
        while (scene.LoadedContent().Status(id) != status && std::chrono::steady_clock::now() < deadline) {
            static_cast<void>(scene.Runtime().Update(0.0F));
            Require(scene.LoadedContent().StreamingStats().operations <= 7U, "Streaming must respect the global operation limit");
            Require(scene.LoadedContent().Status(id) != SceneLoadStatus::Failed, "Streaming unexpectedly failed");
            std::this_thread::yield();
        }
        Require(scene.LoadedContent().Status(id) == status, "Streaming transition timed out");
    };
    for (std::size_t cycle = 0U; cycle < 3U; ++cycle) {
        const auto id = scene.LoadedContent().LoadAsync(path, parent);
        Require(id != 0U && !scene.LoadedContent().Exists(id), "Async load must return a pending id without synchronous activation");
        wait(id, SceneLoadStatus::Ready);
        Require(scene.Entities().Count() == 515U && scene.LoadedContent().Progress(id) == 1.0F, "Async load must preserve the complete prefab");
        const auto owner = scene.Hierarchy().ChildAt(parent, 0U);
        const auto root = scene.Hierarchy().ChildAt(owner, 0U);
        const auto last = scene.Hierarchy().ChildAt(root, 511U);
        Require(scene.LoadedContent().OwningScene(last) == id, "Loaded ownership must work beneath an external parent");
        Require(scene.Components().Joints().TryGet(last)->connectedEntity == root, "Cross-batch joint references must resolve");
        Require(NearlyEqual(scene.Transforms().Get(last).worldPosition.x, 512.0F), "Streaming must preserve parent transforms");
        const auto spawned = scene.Entities().CreateEntity(SceneObjectDesc{.parent = scene.Entities().Object(last)});
        Require(scene.LoadedContent().UnloadAsync(id), "Async unload must accept a loaded id");
        wait(id, SceneLoadStatus::Unknown);
        Require(scene.Entities().Count() == 1U && !scene.Entities().IsAlive(spawned), "Unload must include runtime-created descendants and preserve its external parent");
    }
    // A budget no step can keep: streaming still moves forward by a step every frame (it never starves) and says how
    // many frames went over the limit and how long the longest was.
    scene.LoadedContent().ConfigureStreaming({.maxPendingLoads = 2U, .maxOperationsPerFrame = 1U, .maxMillisecondsPerFrame = 0.000000001F});
    {
        const auto tight = scene.LoadedContent().LoadAsync(path, parent);
        const auto tightDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
        while (scene.LoadedContent().Status(tight) != SceneLoadStatus::Ready && std::chrono::steady_clock::now() < tightDeadline) {
            static_cast<void>(scene.Runtime().Update(0.0F));
            Require(scene.LoadedContent().StreamingStats().operations <= 1U,
                "Minimum streaming progress must preserve the operation budget");
            std::this_thread::yield();
        }
        Require(scene.LoadedContent().Status(tight) == SceneLoadStatus::Ready && scene.Entities().Count() == 515U,
            "A load under an impossible time budget must still finish");
        const SceneStreamingStats tightStats = scene.LoadedContent().StreamingStats();
        Require(tightStats.overBudgetFrames > 0U && tightStats.longestFrameMilliseconds > 0.000000001,
            "Frames over the time budget must be counted");
        Require(scene.LoadedContent().UnloadAsync(tight), "The tight-budget scene must unload");
        const auto unloadDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
        while (scene.LoadedContent().Status(tight) != SceneLoadStatus::Unknown && std::chrono::steady_clock::now() < unloadDeadline) {
            static_cast<void>(scene.Runtime().Update(0.0F));
            std::this_thread::yield();
        }
        Require(scene.Entities().Count() == 1U, "The tight-budget scene must unload completely");
    }
    scene.LoadedContent().ConfigureStreaming({.maxPendingLoads = 2U, .maxOperationsPerFrame = 7U, .maxMillisecondsPerFrame = 20.0F});
    const auto cancelled = scene.LoadedContent().LoadAsync(path);
    Require(scene.LoadedContent().UnloadAsync(cancelled), "Pending load must be cancellable");
    wait(cancelled, SceneLoadStatus::Cancelled);
    Require(scene.Entities().Count() == 1U, "Cancelled load must not leave entities");
    const auto partial = scene.LoadedContent().LoadAsync(path);
    const auto partialDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (scene.Entities().Count() < 10U && std::chrono::steady_clock::now() < partialDeadline) {
        static_cast<void>(scene.Runtime().Update(0.0F));
        std::this_thread::yield();
    }
    Require(scene.Entities().Count() >= 10U && scene.LoadedContent().UnloadAsync(partial), "Partially created content must cancel");
    wait(partial, SceneLoadStatus::Cancelled);
    Require(scene.Entities().Count() == 1U && SceneAccess::State(scene).inactiveEntities.empty(),
        "Cancelling staged objects must release inactive entity bookkeeping");
    const auto destroyedOwner = scene.Entities().CreateEntity();
    const auto orphan = scene.LoadedContent().LoadAsync(path, destroyedOwner);
    scene.Entities().Destroy(destroyedOwner);
    wait(orphan, SceneLoadStatus::Cancelled);
    const auto invalid = scene.LoadedContent().LoadAsync(path.string() + ".missing");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (scene.LoadedContent().Status(invalid) != SceneLoadStatus::Failed && std::chrono::steady_clock::now() < deadline) {
        static_cast<void>(scene.Runtime().Update(0.0F));
        std::this_thread::yield();
    }
    Require(scene.LoadedContent().Status(invalid) == SceneLoadStatus::Failed && !scene.LoadedContent().Error(invalid).empty(), "Failed loads must report a diagnostic");
    // Registered sources share the existing async asset worker and a weak
    // decoded cache. Two requests for one source must retain separate ownership.
    const auto directory = path.parent_path() / path.stem();
    const auto registeredPath = directory / "Shared.21kbscene";
    auto registeredDocument = document;
    for (std::uint32_t index = 0U; index < registeredDocument.worldPrefab.NodeCount(); ++index)
        registeredDocument.worldPrefab.TryGetMutableNode(index)->components.meshRenderer.reset();
    Require(SceneDocumentService::Save(registeredDocument, registeredPath), "Registered streaming fixture must save");
    auto& manager = scene.Assets().Manager();
    Require(manager.Mounts().Mount("Game", directory) && manager.DiscoverMountedAssets() == 1U,
        "Registered streaming fixture must discover");
    const auto cacheBaseline = manager.LoadedCount();
    const auto first = scene.LoadedContent().LoadAsync("/Game/Shared.21kbscene");
    const auto second = scene.LoadedContent().LoadAsync("/Game/Shared.21kbscene");
    Require(first != 0U && second != 0U && first != second &&
        scene.LoadedContent().LoadAsync("/Game/Shared.21kbscene") == 0U, "Concurrent streaming requests must have unique ids and a bounded queue");
    wait(first, SceneLoadStatus::Ready);
    wait(second, SceneLoadStatus::Ready);
    Require(scene.Entities().Count() == 1029U, "Shared streaming sources must create independent complete instances");
    Require(scene.LoadedContent().UnloadAsync(first) && scene.LoadedContent().UnloadAsync(second), "Shared streaming instances must unload independently");
    wait(first, SceneLoadStatus::Unknown);
    wait(second, SceneLoadStatus::Unknown);
    Require(scene.Entities().Count() == 1U, "Shared streaming instances must release all owned entities");
    const auto registeredId = manager.Registry().FindByPath("/Game/Shared.21kbscene")->id;
    for (unsigned cycle = 0U; cycle < 16U; ++cycle) {
        auto retained = manager.Load<SceneDocument>(registeredId);
        Require(retained.IsLoaded(), "Repeated streaming source must load");
        const auto weak = manager.WeakHandle<SceneDocument>(registeredId);
        const auto left = scene.LoadedContent().LoadAsync("/Game/Shared.21kbscene");
        const auto right = scene.LoadedContent().LoadAsync("/Game/Shared.21kbscene");
        Require(left != 0U && right != 0U && left != right, "Repeated shared scene loads must start independently");
        wait(left, SceneLoadStatus::Ready);
        wait(right, SceneLoadStatus::Ready);
        Require(manager.AcquireLoaded<SceneDocument>(registeredId).Get() == retained.Get() &&
            manager.UnloadPolicy(registeredId) == kb::assets::AssetUnloadPolicy::ReleaseWhenUnreferenced &&
            manager.LoadedCount() == cacheBaseline + 1U,
            "Both scene instances must reuse one weakly cached source payload");
        Require(scene.LoadedContent().UnloadAsync(left), "First shared scene must unload");
        wait(left, SceneLoadStatus::Unknown);
        Require(scene.LoadedContent().Exists(right) && scene.Entities().Count() == 515U &&
            !weak.Expired() && retained->worldPrefab.NodeCount() == 513U,
            "Unloading one scene must preserve the other instance and external source owner");
        retained = {};
        const auto releaseDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while ((!weak.Expired() || manager.LoadedCount() != cacheBaseline) && std::chrono::steady_clock::now() < releaseDeadline) {
            static_cast<void>(scene.Runtime().Update(0.0F));
            std::this_thread::yield();
        }
        static_cast<void>(manager.PruneUnreferenced());
        Require(weak.Expired() && manager.LoadedCount() == cacheBaseline && manager.ReferenceCount(registeredId) == 0U,
            "Retired scene sources must return to the payload baseline after the last external owner releases");
        Require(scene.LoadedContent().Exists(right) && scene.Entities().Count() == 515U &&
            scene.LoadedContent().UnloadAsync(right), "A live scene instance must survive releasing its decoded source");
        wait(right, SceneLoadStatus::Unknown);
        Require(scene.Entities().Count() == 1U && manager.LoadedCount() == cacheBaseline,
            "Repeated scene transitions must not accumulate entities or cached payloads");
    }
    scene.LoadedContent().ConfigureStreaming({.maxPendingLoads = 1U, .maxOperationsPerFrame = 7U, .maxMillisecondsPerFrame = 20.0F});
    const auto cancelledRegistered = scene.LoadedContent().LoadAsync("/Game/Shared.21kbscene");
    Require(cancelledRegistered != 0U && scene.LoadedContent().UnloadAsync(cancelledRegistered),
        "Registered asset decode must be cancellable before publication");
    Require(scene.LoadedContent().LoadAsync(path) == 0U,
        "Cancellation must not bypass the pending work limit before retirement");
    scene.LoadedContent().ConfigureStreaming({.maxPendingLoads = 2U, .maxOperationsPerFrame = 7U, .maxMillisecondsPerFrame = 20.0F});
    wait(cancelledRegistered, SceneLoadStatus::Cancelled);
    Require(scene.LoadedContent().LoadAsync(path.string() + ".another-missing") != 0U,
        "Starting another request must preserve ownership of cancelled pending asset publication");
    const auto retireDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
    bool retired = false;
    while (!retired && std::chrono::steady_clock::now() < retireDeadline) {
        static_cast<void>(scene.Runtime().Update(0.0F));
        retired = manager.AsyncLoadStatus(registeredId) == kb::assets::AsyncAssetLoadStatus::NotRequested &&
            !manager.AcquireLoaded<SceneDocument>(registeredId).IsLoaded();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    Require(retired && scene.Entities().Count() == 1U, "Cancelled late asset publication must retire decoded content without spawning entities");
    // Exercise the same loader through a pathless package, without a renderer
    // shader cooker or loose-file fallback masking a broken runtime load.
    namespace bake = kb::assets::bake;
    const auto profile = bake::WindowsX64BakeTargetProfile();
    const auto packPath = directory / "Streaming.kbpack";
    bake::AssetPackWriter writer{packPath, profile};
    auto store = [&](std::span<const std::uint8_t> bytes, std::string_view type) {
        const bake::AssetBakeKey key{.sourceContentHash = bake::HashBakeBytes(bytes),
            .bakerId = "StreamingTest", .bakerVersion = "1", .targetProfileId = std::string{profile.identifier},
            .targetProfileHash = bake::BakeTargetProfileFingerprint(profile)};
        Require(writer.BeginAsset({.key = key, .assetTypeId = std::string{type}}) == bake::BakedAssetSinkStatus::Success &&
            writer.WritePrimaryBlock(bytes, profile.packageBlockAlignmentBytes) == bake::BakedAssetSinkStatus::Success &&
            writer.CommitAsset() == bake::BakedAssetSinkStatus::Success, "Streaming package artifact must store");
        return key.Digest();
    };
    std::ifstream input{registeredPath, std::ios::binary};
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    input.close();
    std::vector<std::uint8_t> blob;
    Require(bake::EncodeRuntimeSourceBlob(bytes, blob), "Streaming package source must encode");
    const auto digest = store(blob, bake::kSourceAssetTypeId);
    bake::RuntimeAssetManifest manifest{.targetProfileId = std::string{profile.identifier},
        .targetProfileHash = bake::BakeTargetProfileFingerprint(profile)};
    manifest.descriptor.targetPlatforms = {"Windows"};
    manifest.settings.name = "StreamingTest";
    manifest.settings.defaultMap = "/Game/Shared.21kbscene";
    const auto assetId = manager.Registry().FindByPath(manifest.settings.defaultMap)->id;
    manifest.assets.push_back({.id = assetId, .type = "Scene", .name = "Shared",
        .virtualPath = manifest.settings.defaultMap, .sourceExtension = ".21kbscene", .contentHash = bake::HashBakeBytes(bytes),
        .artifacts = {{.digest = digest, .encoding = bake::RuntimeArtifactEncoding::SourceBytes}}});
    std::vector<std::uint8_t> manifestBytes;
    Require(bake::EncodeRuntimeAssetManifest(manifest, manifestBytes) == bake::RuntimeAssetManifestStatus::Success,
        "Streaming package manifest must encode");
    static_cast<void>(store(manifestBytes, bake::kRuntimeManifestAssetTypeId));
    Require(writer.Finish() == bake::BakedAssetSinkStatus::Success, "Streaming package must publish");
    auto pack = std::make_shared<bake::RuntimeAssetPack>();
    Require(pack->Mount(packPath, profile) == bake::RuntimeAssetPackStatus::Success && manager.MountRuntimePack(pack),
        "Streaming package must mount");
    std::filesystem::remove(registeredPath);
    const auto packaged = scene.LoadedContent().LoadAsync(manifest.settings.defaultMap);
    Require(packaged != 0U, "Pathless package scene must accept streaming");
    wait(packaged, SceneLoadStatus::Ready);
    Require(scene.Entities().Count() == 515U, "Pathless streaming must preserve every entity");
    Require(scene.LoadedContent().UnloadAsync(packaged), "Pathless streaming scene must unload");
    wait(packaged, SceneLoadStatus::Unknown);
    Require(scene.Entities().Count() == 1U, "Pathless streaming must release every owned entity");
    pack->Unmount();
    std::filesystem::remove_all(directory);
    std::filesystem::remove(path);
}
}
