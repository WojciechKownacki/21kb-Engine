#include "engine/script/ScriptAssetsApi.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetKind.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneTasks.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <cstdint>
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

// AssetManager::LoadOpaque forces the asset into AssetManager's own cache
// through whatever loader is registered for its metadata.type, reached
// without a compile-time T — the returned `asset` Hash is the id a later
// Assets.IsLoaded/Assets.Unload call (or a typed component setter like
// MeshRenderer.SetMesh) resolves the SAME cache entry through. This is
// DELIBERATELY NOT the same guarantee as a native AssetHandle<T>: a native
// handle holds its own shared_ptr copy, so the payload survives an unrelated
// Unload(id) elsewhere (EngineLibraryOwnership.hpp's Shared semantics). The
// script-facing Hash is a bare cache-membership token with no refcount —
// AssetManager::Unload (AssetManager.cpp) is an unconditional cache erase,
// so ANY caller's Assets.Unload on the same reference evicts it for every
// other script that called Assets.Load on it too, with no notification.
// Real per-holder refcounting/weak-reference policy for the script surface
// is LIB-158's explicit, separately-scoped job — not fabricated here.
ScriptFunctionCallResult Load(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::assets::AssetId id = ResolveReference(*context.scene, ReferenceArg(arguments));
    const bool success = id.IsValid() && context.scene->Assets().Manager().LoadOpaque(id);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "success", ScriptValue{ success } },
            ScriptFunctionArgument{ "asset", ScriptValue{ success ? id.value : std::uint64_t{ 0U }, ScriptValueType::Hash } },
        },
        .errors = {},
    };
}

// LIB-155's LoadAsync<T>, honestly: AssetManager::LoadOpaque (like Load<T>
// before it) is fully synchronous — this engine has no background-streaming
// or threading infrastructure anywhere near assets (the same finding
// SceneTasks.hpp's own LIB-098 doc comment already made, which explicitly
// anticipates exactly this shape: "a plain
// `[](float){ return TaskPollResult::Completed; }` closure already covers
// it"). The task started here therefore always resolves (Completed or
// Failed) on its very first poll — one Update tick after this call, never
// synchronously within it — giving script the SAME Task.IsRunning/
// TaskCompleted/TaskFailed contract every other async-shaped operation in
// this engine uses, without pretending to background-stream the asset. Real
// multi-frame streaming would need asynchronous AssetManager support that
// does not exist anywhere in this engine (see SceneTasks.hpp/
// EngineLibraryTaskFactories.hpp) — not fabricated here. `owner` is
// context.caller directly (SceneTasks::Start treats an invalid owner as
// "broadcast," not an error, mirroring ParentEntity/TargetEntity elsewhere —
// no special-case guard needed).
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
    const std::uint64_t taskId = scene->Tasks().Start(
        [scene, id](float) -> kb::scene::TaskPollResult {
            return scene->Assets().Manager().LoadOpaque(id) ? kb::scene::TaskPollResult::Completed : kb::scene::TaskPollResult::Failed;
        },
        context.caller);
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
    const bool unloaded = id.IsValid() && context.scene->Assets().Manager().Unload(id);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "unloaded", ScriptValue{ unloaded } } },
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
    return ok;
}

} // namespace kb::script
