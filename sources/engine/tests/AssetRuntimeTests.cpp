#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/audio/AudioClipAsset.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetKind.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/ImportedAsset.hpp"
#include "engine/assets/ImportedAssetLoader.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/script/ScriptAsset.hpp"
#include "engine/script/ScriptBehaviourAsset.hpp"
#include "engine/script/ScriptBehaviourBindingService.hpp"
#include "engine/visual/VisualGraphTypes.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
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
        kb::assets::AssetKind::Graph, kb::assets::AssetKind::InputAction, kb::assets::AssetKind::InputMap,
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
    kb::tests::Require(!classify("LuaScript", classified), "TryClassifyAssetKind must return false for a type that is none of the typed-reference kinds (LuaScript)");
    kb::tests::Require(!classify("NativeBehaviour", classified), "TryClassifyAssetKind must return false for a NativeBehaviour asset");
    kb::tests::Require(!classify("ImportedAsset", classified, "Texture"), "TryClassifyAssetKind must return false for a non-audio ImportedAsset (not one of the typed kinds)");
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

} // namespace

namespace kb::tests {

void RunAssetRuntimeTests() {
    RunAssetManagerDiscoveryCacheAndManifestTest();
    RunAssetManagerLoadOpaqueTest();
    RunAssetCacheReferenceAndPolicyTest();
    RunAssetCompatibilityValidationTest();
    RunAssetKindClassificationTest();
    RunAssetDiscoveryPreservesEditorLiveOverrideTest();
    RunAssetManagerFolderAndRenameOperationsTest();
    RunAssetImportServiceBinaryContainerTest();
    RunAssetImportServiceReportsCreatedReusedMissingAndUnsupportedTest();
    RunScenePrefabRuntimeAssetTest();
    RunSceneAudioClipAssetDiscoveryTest();
    RunScriptAssetPipelineTest();
    RunInspectorDeclarationParseTest();
}

} // namespace kb::tests
