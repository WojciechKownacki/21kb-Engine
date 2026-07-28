#include "engine/script/ScriptAssetsApi.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetKind.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTasks.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

const ScriptValue* FindArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == name) {
            return &argument.value;
        }
    }
    return nullptr;
}

[[nodiscard]] std::string ReferenceArg(std::span<const ScriptFunctionArgument> arguments) {
    const ScriptValue* value = FindArg(arguments, "reference");
    return value == nullptr ? std::string{} : value->AsString();
}

[[nodiscard]] std::uint64_t OwnerKey(const ScriptFunctionCallContext& context) noexcept {
    return context.caller.IsValid() ? context.caller.Id() : 0U;
}

void StoreOwnedAsset(kb::scene::Scene& scene, std::uint64_t owner, kb::assets::AssetOpaqueHandle handle) {
    if (handle.IsLoaded()) {
        kb::scene::SceneAccess::State(scene).scriptOwnedAssets[owner].insert_or_assign(handle.Id().value, std::move(handle));
    }
}

[[nodiscard]] bool AnyScriptOwnerHolds(const kb::scene::SceneState& state, std::uint64_t assetId) {
    for (const auto& [owner, assets] : state.scriptOwnedAssets) {
        static_cast<void>(owner);
        if (assets.contains(assetId)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool AnyScriptOwnerRequests(const kb::scene::SceneState& state, std::uint64_t assetId) {
    for (const auto& [owner, assets] : state.scriptPendingAssets) {
        static_cast<void>(owner);
        if (assets.contains(assetId)) {
            return true;
        }
    }
    return false;
}

void RemovePendingAsset(kb::scene::SceneState& state, std::uint64_t owner, std::uint64_t assetId) {
    const auto ownerIterator = state.scriptPendingAssets.find(owner);
    if (ownerIterator == state.scriptPendingAssets.end()) {
        return;
    }
    ownerIterator->second.erase(assetId);
    if (ownerIterator->second.empty()) {
        state.scriptPendingAssets.erase(ownerIterator);
    }
}

ScriptFunctionCallResult NoScene() {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { "assets api requires an active scene" } };
}

// Generic reference resolution — deliberately WITHOUT a per-kind type check
// (unlike ScriptMeshRendererApi::ResolveAssetId/ScriptAudioApi::
// ResolveClipAssetId, which reject the wrong asset kind): Assets.Find/Load/
// IsLoaded/Unload/LoadAsync manage lookup/lifecycle/ownership generically
// across every registered asset kind, the same way AssetManager::Load<T>
// and Unload/IsLoaded already do natively. Per-kind type SAFETY for
// specific gameplay slots (mesh, material, texture, ...) is LIB-157's
// typed-reference layer, layered on top of the component setters that
// already do their own isExpectedType check (MeshRenderer.SetMesh,
// Audio.Play, ...) — not duplicated here. LIB-156: this is ALSO the entire
// resolution surface for "stable id or logical/virtual project path, never
// a physical OS path" — only TryParseAssetId+Registry().Find (numeric id)
// or Registry().FindByPath (virtual path) are ever consulted; neither
// touches AssetMetadata::physicalPath or AssetManager's private
// ResolvePhysicalPath, so a physical path string can never resolve through
// any Assets.* function.
[[nodiscard]] kb::assets::AssetId ResolveReference(kb::scene::Scene& scene, std::string_view reference) {
    kb::assets::AssetId id{};
    if (kb::assets::TryParseAssetId(reference, id) && id.IsValid()) {
        return scene.Assets().Manager().Registry().Find(id) != nullptr ? id : kb::assets::AssetId{};
    }

    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath(std::filesystem::path{ reference });
    return metadata == nullptr ? kb::assets::AssetId{} : metadata->id;
}

// LIB-156: a pure metadata lookup, deliberately separate from Assets.Load —
// Find never forces the loader to run or touches AssetManager's cache, it
// only answers "does this stable id/virtual path resolve to a registered
// asset." ResolveReference (above) is the ENTIRE resolution surface: it
// only ever calls TryParseAssetId+Registry().Find (numeric id) or
// Registry().FindByPath (virtual path) — it has no code path that reads
// AssetMetadata::physicalPath or calls AssetManager's private
// ResolvePhysicalPath, so a physical OS path can never resolve here (see
// RunScriptAssetsApiTest's physical-path rejection assertions for the
// negative proof).
ScriptFunctionCallResult Find(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::assets::AssetId id = ResolveReference(*context.scene, ReferenceArg(arguments));
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "found", ScriptValue{ id.IsValid() } },
            ScriptFunctionArgument{ "asset", ScriptValue{ id.value, ScriptValueType::Hash } },
        },
        .errors = {},
    };
}

// LIB-157: a TYPED reference resolve — succeeds only when the reference
// resolves AND the resolved asset is of the requested kind. `kind` is a
// friendly AssetKind name ("Mesh", "Material", "Texture", "Audio",
// "Prefab", "Scene", "Graph", "InputAction", "InputMap"). An unknown kind
// string is a malformed request (a caller typo), so it is an honest error,
// NOT a silent found=false — otherwise a misspelled kind would masquerade
// as "asset not of that kind." A resolvable reference of the wrong kind, by
// contrast, is a legitimate answer: found=false. Kind resolution is a pure
// AssetMetadata::type tag check (AssetKind), so this works uniformly for
// every kind including the kb_render-owned mesh/material/texture, with zero
// kb_render dependency.
ScriptFunctionCallResult FindTyped(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const ScriptValue* kindValue = FindArg(arguments, "kind");
    const std::string kindName = kindValue == nullptr ? std::string{} : kindValue->AsString();
    kb::assets::AssetKind kind{};
    if (!kb::assets::TryParseAssetKind(kindName, kind)) {
        return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { "unknown asset kind: " + kindName } };
    }

    const kb::assets::AssetId id = ResolveReference(*context.scene, ReferenceArg(arguments));
    const kb::assets::AssetMetadata* metadata = id.IsValid() ? context.scene->Assets().Manager().Registry().Find(id) : nullptr;
    const bool found = metadata != nullptr && kb::assets::AssetMatchesKind(*metadata, kind);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "found", ScriptValue{ found } },
            ScriptFunctionArgument{ "asset", ScriptValue{ found ? id.value : std::uint64_t{ 0U }, ScriptValueType::Hash } },
        },
        .errors = {},
    };
}

// LIB-157: the reverse of FindTyped — classifies a resolvable reference into
// its single AssetKind. found=false (with kind="") for an unresolvable
// reference OR a resolvable asset that is none of the recognised kinds (a
// LuaScript, NativeBehaviour, non-audio ImportedAsset, ...) — an honest "I
// don't have a typed name for this," never a fabricated guess.
ScriptFunctionCallResult KindOf(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::assets::AssetId id = ResolveReference(*context.scene, ReferenceArg(arguments));
    const kb::assets::AssetMetadata* metadata = id.IsValid() ? context.scene->Assets().Manager().Registry().Find(id) : nullptr;
    kb::assets::AssetKind kind{};
    const bool classified = metadata != nullptr && kb::assets::TryClassifyAssetKind(*metadata, kind);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "found", ScriptValue{ classified } },
            ScriptFunctionArgument{ "kind", ScriptValue{ classified ? std::string{ kb::assets::ToString(kind) } : std::string{} } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult IsLoaded(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::assets::AssetId id = ResolveReference(*context.scene, ReferenceArg(arguments));
    const bool loaded = id.IsValid() && context.scene->Assets().Manager().IsLoaded(id);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "loaded", ScriptValue{ loaded } } },
        .errors = {},
    };
}

// The returned Hash is stable identity; SceneState retains the type-erased
// strong payload per caller. Unload releases only that caller's ownership and
// evicts the cache after the last script owner/request disappears.
ScriptFunctionCallResult Load(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::assets::AssetId id = ResolveReference(*context.scene, ReferenceArg(arguments));
    kb::assets::AssetOpaqueHandle handle =
        id.IsValid() ? context.scene->Assets().Manager().LoadOpaqueHandle(id) : kb::assets::AssetOpaqueHandle{};
    const bool success = handle.IsLoaded();
    if (success) {
        StoreOwnedAsset(*context.scene, OwnerKey(context), std::move(handle));
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "success", ScriptValue{ success } },
            ScriptFunctionArgument{ "asset", ScriptValue{ success ? id.value : std::uint64_t{ 0U }, ScriptValueType::Hash } },
        },
        .errors = {},
    };
}

// Loader I/O/decode runs on AssetManager's bounded worker queue. The SceneTask only
// polls and commits a ready result on the owner thread, then transfers a
// strong type-erased handle into the caller's scene-owned lifetime table.
ScriptFunctionCallResult LoadAsync(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::assets::AssetId id = ResolveReference(*context.scene, ReferenceArg(arguments));
    if (!id.IsValid()) {
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {
                ScriptFunctionArgument{ "started", ScriptValue{ false } },
                ScriptFunctionArgument{ "task", ScriptValue{ std::uint64_t{ 0U }, ScriptValueType::Hash } },
            },
            .errors = {},
        };
    }

    kb::scene::Scene* scene = context.scene;
    kb::assets::AssetManager& manager = scene->Assets().Manager();
    if (!manager.RequestLoadAsync(id)) {
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {
                ScriptFunctionArgument{ "started", ScriptValue{ false } },
                ScriptFunctionArgument{ "task", ScriptValue{ std::uint64_t{ 0U }, ScriptValueType::Hash } },
            },
            .errors = {},
        };
    }
    const std::uint64_t owner = OwnerKey(context);
    kb::scene::SceneAccess::State(*scene).scriptPendingAssets[owner].insert(id.value);
    const std::uint64_t taskId = scene->Tasks().Start(
        [scene, id, owner](float) -> kb::scene::TaskPollResult {
            kb::assets::AssetManager& assets = scene->Assets().Manager();
            assets.PumpAsyncLoads();
            switch (assets.AsyncLoadStatus(id)) {
            case kb::assets::AsyncAssetLoadStatus::Pending:
                return kb::scene::TaskPollResult::Running;
            case kb::assets::AsyncAssetLoadStatus::Completed: {
                kb::assets::AssetOpaqueHandle handle = assets.AcquireOpaqueHandle(id);
                if (!handle.IsLoaded()) {
                    RemovePendingAsset(kb::scene::SceneAccess::State(*scene), owner, id.value);
                    return kb::scene::TaskPollResult::Failed;
                }
                StoreOwnedAsset(*scene, owner, std::move(handle));
                RemovePendingAsset(kb::scene::SceneAccess::State(*scene), owner, id.value);
                return kb::scene::TaskPollResult::Completed;
            }
            case kb::assets::AsyncAssetLoadStatus::Failed:
            case kb::assets::AsyncAssetLoadStatus::NotRequested:
            default:
                RemovePendingAsset(kb::scene::SceneAccess::State(*scene), owner, id.value);
                return kb::scene::TaskPollResult::Failed;
            }
        },
        context.caller);
    if (taskId == 0U) {
        kb::scene::SceneState& state = kb::scene::SceneAccess::State(*scene);
        RemovePendingAsset(state, owner, id.value);
        if (!AnyScriptOwnerHolds(state, id.value) && !AnyScriptOwnerRequests(state, id.value)) {
            static_cast<void>(manager.Unload(id));
        }
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "started", ScriptValue{ taskId != 0U } },
            ScriptFunctionArgument{ "task", ScriptValue{ taskId, ScriptValueType::Hash } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult Unload(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::assets::AssetId id = ResolveReference(*context.scene, ReferenceArg(arguments));
    bool unloaded = false;
    if (id.IsValid()) {
        kb::scene::SceneState& state = kb::scene::SceneAccess::State(*context.scene);
        const std::uint64_t owner = OwnerKey(context);
        const auto ownerIterator = state.scriptOwnedAssets.find(OwnerKey(context));
        if (ownerIterator != state.scriptOwnedAssets.end()) {
            unloaded = ownerIterator->second.erase(id.value) > 0U;
            if (ownerIterator->second.empty()) {
                state.scriptOwnedAssets.erase(ownerIterator);
            }
        }
        const auto pendingOwner = state.scriptPendingAssets.find(owner);
        if (pendingOwner != state.scriptPendingAssets.end()) {
            unloaded = pendingOwner->second.erase(id.value) > 0U || unloaded;
            if (pendingOwner->second.empty()) {
                state.scriptPendingAssets.erase(pendingOwner);
            }
        }
        if (unloaded && !AnyScriptOwnerHolds(state, id.value) && !AnyScriptOwnerRequests(state, id.value)) {
            static_cast<void>(context.scene->Assets().Manager().Unload(id));
        }
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "unloaded", ScriptValue{ unloaded } } },
        .errors = {},
    };
}

// LIB-158: the number of live strong holders (native AssetHandle/AssetRef)
// of a cached asset — 0 when the asset is not cached or its payload was
// released. Assets.Load/LoadAsync retain a type-erased strong handle for
// each script caller, so those owners and native AssetHandle/AssetRef owners
// are all reflected in this count. A script's bare reference string or Hash
// remains non-owning.
ScriptFunctionCallResult RefCount(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::assets::AssetId id = ResolveReference(*context.scene, ReferenceArg(arguments));
    const std::size_t count = id.IsValid() ? context.scene->Assets().Manager().ReferenceCount(id) : 0U;
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "count", ScriptValue{ static_cast<int>(count) } } },
        .errors = {},
    };
}

// LIB-158: set the cache retention policy of an already-loaded asset. An
// unknown policy name is a malformed request (honest error), mirroring
// Assets.FindTyped's unknown-kind handling; a valid policy on a not-cached
// (or already-released) asset is a legitimate applied=false, not an error.
ScriptFunctionCallResult SetUnloadPolicy(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const ScriptValue* policyValue = FindArg(arguments, "policy");
    const std::string policyName = policyValue == nullptr ? std::string{} : policyValue->AsString();
    kb::assets::AssetUnloadPolicy policy{};
    if (!kb::assets::TryParseAssetUnloadPolicy(policyName, policy)) {
        return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { "unknown asset unload policy: " + policyName } };
    }
    const kb::assets::AssetId id = ResolveReference(*context.scene, ReferenceArg(arguments));
    const bool applied = id.IsValid() && context.scene->Assets().Manager().SetUnloadPolicy(id, policy);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "applied", ScriptValue{ applied } } },
        .errors = {},
    };
}

// LIB-158: report the retention policy of a cached asset. `cached` honestly
// distinguishes "not cached" (policy reports the Retain default a fresh Load
// would use) from a real cached entry.
ScriptFunctionCallResult UnloadPolicy(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::assets::AssetId id = ResolveReference(*context.scene, ReferenceArg(arguments));
    kb::assets::AssetManager& manager = context.scene->Assets().Manager();
    const bool cached = id.IsValid() && manager.IsLoaded(id);
    const kb::assets::AssetUnloadPolicy policy = id.IsValid() ? manager.UnloadPolicy(id) : kb::assets::AssetUnloadPolicy::Retain;
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "cached", ScriptValue{ cached } },
            ScriptFunctionArgument{ "policy", ScriptValue{ std::string{ kb::assets::ToString(policy) } } },
        },
        .errors = {},
    };
}

// LIB-158: sweep cache entries whose payload was already released under
// ReleaseWhenUnreferenced, returning how many dead entries were removed.
ScriptFunctionCallResult PruneUnreferenced(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    static_cast<void>(arguments);
    if (context.scene == nullptr) {
        return NoScene();
    }
    const std::size_t removed = context.scene->Assets().Manager().PruneUnreferenced();
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "removed", ScriptValue{ static_cast<int>(removed) } } },
        .errors = {},
    };
}

// LIB-159: validate an asset and its whole declared dependency closure
// WITHOUT loading anything, returning a readable diagnostic string a tool or
// script can surface directly. An unresolvable reference is a legitimate
// validation answer (compatible=false with a "could not be resolved"
// diagnostic), NOT a call error — the whole point of Validate is to report
// problems, so it never itself fails on a bad reference.
ScriptFunctionCallResult Validate(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const std::string reference = ReferenceArg(arguments);
    const kb::assets::AssetId id = ResolveReference(*context.scene, reference);
    if (!id.IsValid()) {
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {
                ScriptFunctionArgument{ "compatible", ScriptValue{ false } },
                ScriptFunctionArgument{ "issueCount", ScriptValue{ 1 } },
                ScriptFunctionArgument{ "diagnostics", ScriptValue{ "Reference \"" + reference + "\" could not be resolved to a registered asset" } },
            },
            .errors = {},
        };
    }

    const kb::assets::AssetCompatibilityReport report = context.scene->Assets().Manager().ValidateCompatibility(id);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "compatible", ScriptValue{ report.compatible } },
            ScriptFunctionArgument{ "issueCount", ScriptValue{ static_cast<int>(report.diagnostics.size()) } },
            ScriptFunctionArgument{ "diagnostics", ScriptValue{ report.FormatDiagnostics() } },
        },
        .errors = {},
    };
}

// LIB-019: the ENTIRE safe asset-property surface — deliberately just the
// two AssetMetadata fields that are both meaningful to a script and safe to
// expose (never AssetMetadata::physicalPath, for the exact LIB-156 reason
// ResolveReference above never resolves one: an OS path is not something a
// sandboxed script should ever see). Both read-only: renaming or retyping
// an asset from script is not a safe operation this task adds. Kept as a
// small, hand-maintained array (mirroring ScriptSceneComponentApi's own
// per-component property tables) rather than reflected, since AssetMetadata
// has exactly two fields worth this treatment today.
constexpr ScriptSceneComponentPropertyDesc kAssetProperties[] = {
    { "virtualPath", ScriptValueType::String, false },
    { "type", ScriptValueType::String, false },
};

// LIB-019: the second half of "LibraryPropertyDesc dla bezpiecznych pól
// komponentów I assetów" — ScriptSceneComponentApi::GetProperty already
// covers components; this is the asset equivalent, same generic
// name-string shape, same ResolveReference (LIB-155/156) used by every
// other Assets.* function. An unresolvable reference is a legitimate
// found=false (the asset just doesn't exist); an unrecognized property
// name is an honest error (a caller typo), mirroring Assets.FindTyped's
// unknown-kind handling and ScriptSceneComponentApi::GetProperty's own
// error-string return for an unknown component property.
ScriptFunctionCallResult GetProperty(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const ScriptValue* propertyValue = FindArg(arguments, "property");
    const std::string propertyName = propertyValue == nullptr ? std::string{} : propertyValue->AsString();
    bool knownProperty = false;
    for (const ScriptSceneComponentPropertyDesc& candidate : kAssetProperties) {
        if (candidate.name == propertyName) {
            knownProperty = true;
            break;
        }
    }
    if (!knownProperty) {
        return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { "unknown asset property: " + propertyName } };
    }

    const kb::assets::AssetId id = ResolveReference(*context.scene, ReferenceArg(arguments));
    const kb::assets::AssetMetadata* metadata = id.IsValid() ? context.scene->Assets().Manager().Registry().Find(id) : nullptr;
    if (metadata == nullptr) {
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = { ScriptFunctionArgument{ "found", ScriptValue{ false } }, ScriptFunctionArgument{ "value", ScriptValue{ std::string{} } } },
            .errors = {},
        };
    }

    const std::string value = propertyName == "virtualPath" ? metadata->virtualPath.generic_string() : metadata->type;
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "found", ScriptValue{ true } }, ScriptFunctionArgument{ "value", ScriptValue{ value } } },
        .errors = {},
    };
}

bool RegisterFunction(ScriptRuntimeHost& host, std::string name, std::vector<ScriptFunctionPin> inputs, std::vector<ScriptFunctionPin> outputs, ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = std::move(inputs);
    desc.signature.outputs = std::move(outputs);
    desc.callback = std::move(callback);
    return host.RegisterFunction(std::move(desc));
}

} // namespace

bool ScriptAssetsApi::Register(ScriptRuntimeHost& host) {
    bool ok = true;
    ok = RegisterFunction(host, "Assets.Find",
              { ScriptFunctionPin{ "reference", ScriptValueType::String, true } },
              { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "asset", ScriptValueType::Hash, true } }, &Find)
        && ok;
    ok = RegisterFunction(host, "Assets.FindTyped",
              { ScriptFunctionPin{ "reference", ScriptValueType::String, true }, ScriptFunctionPin{ "kind", ScriptValueType::String, true } },
              { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "asset", ScriptValueType::Hash, true } }, &FindTyped)
        && ok;
    ok = RegisterFunction(host, "Assets.KindOf",
              { ScriptFunctionPin{ "reference", ScriptValueType::String, true } },
              { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "kind", ScriptValueType::String, true } }, &KindOf)
        && ok;
    ok = RegisterFunction(host, "Assets.IsLoaded",
              { ScriptFunctionPin{ "reference", ScriptValueType::String, true } },
              { ScriptFunctionPin{ "loaded", ScriptValueType::Bool, true } }, &IsLoaded)
        && ok;
    ok = RegisterFunction(host, "Assets.Load",
              { ScriptFunctionPin{ "reference", ScriptValueType::String, true } },
              { ScriptFunctionPin{ "success", ScriptValueType::Bool, true }, ScriptFunctionPin{ "asset", ScriptValueType::Hash, true } }, &Load)
        && ok;
    ok = RegisterFunction(host, "Assets.LoadAsync",
              { ScriptFunctionPin{ "reference", ScriptValueType::String, true } },
              { ScriptFunctionPin{ "started", ScriptValueType::Bool, true }, ScriptFunctionPin{ "task", ScriptValueType::Hash, true } }, &LoadAsync)
        && ok;
    ok = RegisterFunction(host, "Assets.Unload",
              { ScriptFunctionPin{ "reference", ScriptValueType::String, true } },
              { ScriptFunctionPin{ "unloaded", ScriptValueType::Bool, true } }, &Unload)
        && ok;
    ok = RegisterFunction(host, "Assets.RefCount",
              { ScriptFunctionPin{ "reference", ScriptValueType::String, true } },
              { ScriptFunctionPin{ "count", ScriptValueType::Int, true } }, &RefCount)
        && ok;
    ok = RegisterFunction(host, "Assets.SetUnloadPolicy",
              { ScriptFunctionPin{ "reference", ScriptValueType::String, true }, ScriptFunctionPin{ "policy", ScriptValueType::String, true } },
              { ScriptFunctionPin{ "applied", ScriptValueType::Bool, true } }, &SetUnloadPolicy)
        && ok;
    ok = RegisterFunction(host, "Assets.UnloadPolicy",
              { ScriptFunctionPin{ "reference", ScriptValueType::String, true } },
              { ScriptFunctionPin{ "cached", ScriptValueType::Bool, true }, ScriptFunctionPin{ "policy", ScriptValueType::String, true } }, &UnloadPolicy)
        && ok;
    ok = RegisterFunction(host, "Assets.PruneUnreferenced",
              {},
              { ScriptFunctionPin{ "removed", ScriptValueType::Int, true } }, &PruneUnreferenced)
        && ok;
    ok = RegisterFunction(host, "Assets.Validate",
              { ScriptFunctionPin{ "reference", ScriptValueType::String, true } },
              { ScriptFunctionPin{ "compatible", ScriptValueType::Bool, true }, ScriptFunctionPin{ "issueCount", ScriptValueType::Int, true }, ScriptFunctionPin{ "diagnostics", ScriptValueType::String, true } }, &Validate)
        && ok;
    ok = RegisterFunction(host, "Assets.GetProperty",
              { ScriptFunctionPin{ "reference", ScriptValueType::String, true }, ScriptFunctionPin{ "property", ScriptValueType::String, true } },
              { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "value", ScriptValueType::String, true } }, &GetProperty)
        && ok;
    return ok;
}

void ScriptAssetsApi::ReleaseDeadOwnerHandles(kb::scene::Scene& scene) {
    kb::scene::SceneState& state = kb::scene::SceneAccess::State(scene);
    std::vector<std::uint64_t> releasedAssets;
    for (auto ownerIterator = state.scriptOwnedAssets.begin(); ownerIterator != state.scriptOwnedAssets.end();) {
        const std::uint64_t owner = ownerIterator->first;
        if (owner == 0U || scene.Entities().IsAlive(kb::scene::SceneEntity{ owner })) {
            ++ownerIterator;
            continue;
        }
        for (const auto& [assetId, handle] : ownerIterator->second) {
            static_cast<void>(handle);
            releasedAssets.push_back(assetId);
        }
        ownerIterator = state.scriptOwnedAssets.erase(ownerIterator);
    }
    for (auto ownerIterator = state.scriptPendingAssets.begin(); ownerIterator != state.scriptPendingAssets.end();) {
        const std::uint64_t owner = ownerIterator->first;
        if (owner == 0U || scene.Entities().IsAlive(kb::scene::SceneEntity{ owner })) {
            ++ownerIterator;
            continue;
        }
        releasedAssets.insert(releasedAssets.end(), ownerIterator->second.begin(), ownerIterator->second.end());
        ownerIterator = state.scriptPendingAssets.erase(ownerIterator);
    }
    std::ranges::sort(releasedAssets);
    releasedAssets.erase(std::unique(releasedAssets.begin(), releasedAssets.end()), releasedAssets.end());
    for (const std::uint64_t assetId : releasedAssets) {
        if (!AnyScriptOwnerHolds(state, assetId) && !AnyScriptOwnerRequests(state, assetId)) {
            static_cast<void>(scene.Assets().Manager().Unload(kb::assets::AssetId{ assetId }));
        }
    }
}

std::span<const ScriptSceneComponentPropertyDesc> ScriptAssetsApi::AssetProperties() noexcept {
    return kAssetProperties;
}

} // namespace kb::script
