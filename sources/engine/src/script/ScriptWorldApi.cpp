#include "engine/script/ScriptWorldApi.hpp"

#include "engine/assets/AssetHandle.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneLoadedContent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabInstance.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

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

ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

ScriptFunctionCallResult NoScene() {
    return Error("world api requires an active scene");
}

[[nodiscard]] kb::scene::SceneEntity EntityArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, kb::scene::SceneEntity fallback = {}) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : kb::scene::SceneEntity{ value->AsUInt64(fallback.Id()) };
}

[[nodiscard]] std::string StringArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, std::string fallback = {}) {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? std::move(fallback) : value->AsString();
}

[[nodiscard]] float FloatArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, float fallback = 0.0F) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsFloat(fallback);
}

[[nodiscard]] int IntArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, int fallback = 0) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsInt(fallback);
}

[[nodiscard]] bool HasArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) noexcept {
    return FindArg(arguments, name) != nullptr;
}

void ApplyPositionArgs(kb::scene::TransformComponent& transform, std::span<const ScriptFunctionArgument> arguments) noexcept {
    if (HasArg(arguments, "x")) {
        transform.localPosition.x = FloatArg(arguments, "x", transform.localPosition.x);
    }
    if (HasArg(arguments, "y")) {
        transform.localPosition.y = FloatArg(arguments, "y", transform.localPosition.y);
    }
    if (HasArg(arguments, "z")) {
        transform.localPosition.z = FloatArg(arguments, "z", transform.localPosition.z);
    }
}

// LIB-066: the rotation half of a World.Spawn "pose" (position from
// ApplyPositionArgs above, rotation here) — same per-component-optional
// idiom, defaulting to whatever is already in transform.localRotation
// (identity, kb::math::Quat{}'s default, for a freshly constructed
// TransformComponent).
void ApplyRotationArgs(kb::scene::TransformComponent& transform, std::span<const ScriptFunctionArgument> arguments) noexcept {
    if (HasArg(arguments, "rotX")) {
        transform.localRotation.x = FloatArg(arguments, "rotX", transform.localRotation.x);
    }
    if (HasArg(arguments, "rotY")) {
        transform.localRotation.y = FloatArg(arguments, "rotY", transform.localRotation.y);
    }
    if (HasArg(arguments, "rotZ")) {
        transform.localRotation.z = FloatArg(arguments, "rotZ", transform.localRotation.z);
    }
    if (HasArg(arguments, "rotW")) {
        transform.localRotation.w = FloatArg(arguments, "rotW", transform.localRotation.w);
    }
}

// LIB-160: the scale half of a full spawn pose (position/rotation above),
// same per-component-optional idiom, defaulting to whatever is already in
// transform.localScale ({1,1,1} for a fresh TransformComponent).
void ApplyScaleArgs(kb::scene::TransformComponent& transform, std::span<const ScriptFunctionArgument> arguments) noexcept {
    if (HasArg(arguments, "scaleX")) {
        transform.localScale.x = FloatArg(arguments, "scaleX", transform.localScale.x);
    }
    if (HasArg(arguments, "scaleY")) {
        transform.localScale.y = FloatArg(arguments, "scaleY", transform.localScale.y);
    }
    if (HasArg(arguments, "scaleZ")) {
        transform.localScale.z = FloatArg(arguments, "scaleZ", transform.localScale.z);
    }
}

[[nodiscard]] bool HasAnyTransformArg(std::span<const ScriptFunctionArgument> arguments) noexcept {
    return HasArg(arguments, "x") || HasArg(arguments, "y") || HasArg(arguments, "z") || HasArg(arguments, "rotX") ||
        HasArg(arguments, "rotY") || HasArg(arguments, "rotZ") || HasArg(arguments, "rotW") || HasArg(arguments, "scaleX") ||
        HasArg(arguments, "scaleY") || HasArg(arguments, "scaleZ");
}

ScriptFunctionCallResult EntityResult(std::string_view pin, kb::scene::SceneEntity entity) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ std::string{ pin }, ScriptValue{ entity.Id(), ScriptValueType::Entity } } },
        .errors = {},
    };
}

ScriptFunctionCallResult BoolResult(std::string_view pin, bool value) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ std::string{ pin }, ScriptValue{ value } } },
        .errors = {},
    };
}

[[nodiscard]] std::string Trim(std::string_view value) {
    const std::size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const std::size_t end = value.find_last_not_of(" \t\r\n");
    return std::string{ value.substr(begin, end - begin + 1U) };
}

[[nodiscard]] std::vector<std::string> ParseTags(std::string_view tags) {
    std::vector<std::string> parsed;
    std::size_t tokenBegin = 0U;
    while (tokenBegin <= tags.size()) {
        const std::size_t tokenEnd = tags.find_first_of(",;", tokenBegin);
        std::string tag = Trim(tags.substr(tokenBegin, tokenEnd == std::string_view::npos ? std::string_view::npos : tokenEnd - tokenBegin));
        if (!tag.empty()) {
            parsed.push_back(std::move(tag));
        }
        if (tokenEnd == std::string_view::npos) {
            break;
        }
        tokenBegin = tokenEnd + 1U;
    }
    return parsed;
}

[[nodiscard]] std::string JoinTags(const std::vector<std::string>& tags) {
    std::string joined;
    for (const std::string& tag : tags) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += tag;
    }
    return joined;
}

[[nodiscard]] bool HasTagValue(std::string_view tags, std::string_view tag) {
    for (const std::string& existingTag : ParseTags(tags)) {
        if (existingTag == tag) {
            return true;
        }
    }
    return false;
}

struct FindByNameContext {
    kb::scene::Scene* scene = nullptr;
    std::string_view name;
    kb::scene::SceneEntity found{};
};

void FindByNameVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent&, void* rawContext) {
    auto* context = static_cast<FindByNameContext*>(rawContext);
    if (context == nullptr || context->scene == nullptr || context->found.IsValid()) {
        return;
    }
    if (context->scene->Entities().Name(entity) == context->name) {
        context->found = entity;
    }
}

// LIB-069 (documented cost): O(n) in the scene's entity count — a linear
// scan over every entity via Transforms().ForEach, early-exiting the
// instant a match is found (best case O(1) for the first entity created,
// worst case O(n) when the match is last or absent). No name index
// exists; a scene with many entities calling this every frame should
// cache the returned handle instead of re-searching.
ScriptFunctionCallResult FindByName(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const std::string name = StringArg(arguments, "name");
    FindByNameContext findContext{
        .scene = context.scene,
        .name = name,
        .found = {},
    };
    context.scene->Transforms().ForEach(&FindByNameVisitor, &findContext);
    return EntityResult("entity", findContext.found);
}

ScriptFunctionCallResult Exists(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return BoolResult("exists", context.scene->Entities().IsAlive(EntityArg(arguments, "entity")));
}

// LIB-068: an entity is active by default and stays that way until an
// explicit SetActive(false) — kb::scene::SceneEntityService::IsActive
// also folds in IsAlive, so a dead/never-existed entity reports inactive
// rather than throwing, the same "never crash on a stale handle" contract
// every other World.* query in this file already follows.
ScriptFunctionCallResult IsActive(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return BoolResult("active", context.scene->Entities().IsActive(EntityArg(arguments, "entity")));
}

ScriptFunctionCallResult SetActive(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    const bool alive = entity.IsValid() && context.scene->Entities().IsAlive(entity);
    if (alive) {
        const ScriptValue* activeValue = FindArg(arguments, "active");
        context.scene->Entities().SetActive(entity, activeValue == nullptr || activeValue->AsBool(true));
    }
    return BoolResult("set", alive);
}

// LIB-072: the persistent/gameplay scene boundary — an entity marked
// persistent survives a non-additive Scene.Load (SceneDocumentService::
// ClearSceneRoots skips persistent roots and, by cascade, their whole
// hierarchy). Only meaningful on ROOT entities — see
// kb::scene::SceneState::persistentEntities' comment for why marking a
// non-root child persistent has no protective effect on its own.
ScriptFunctionCallResult IsPersistent(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return BoolResult("persistent", context.scene->Entities().IsPersistent(EntityArg(arguments, "entity")));
}

ScriptFunctionCallResult SetPersistent(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    const bool alive = entity.IsValid() && context.scene->Entities().IsAlive(entity);
    if (alive) {
        const ScriptValue* persistentValue = FindArg(arguments, "persistent");
        context.scene->Entities().SetPersistent(entity, persistentValue == nullptr || persistentValue->AsBool(true));
    }
    return BoolResult("set", alive);
}

ScriptFunctionCallResult Name(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    const std::string name = entity.IsValid() && context.scene->Entities().IsAlive(entity) ? context.scene->Entities().Name(entity) : std::string{};
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "name", ScriptValue{ name } } },
        .errors = {},
    };
}

// LIB-065: the current scene's runtime instance id — an opaque identifier
// (like Hash), not an arithmetic quantity, and NOT the same kind of thing
// as kb::library::SceneRef (an on-disk SceneDocument asset handle); this
// is the currently-executing, in-memory world.
ScriptFunctionCallResult Current(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "world", ScriptValue{ context.scene->Id(), ScriptValueType::Hash } } },
        .errors = {},
    };
}

ScriptFunctionCallResult IsPlaying(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return BoolResult("playing", context.scene->Runtime().IsPlaying());
}

// LIB-065: monotonic since scene creation (kb::scene::SceneRuntime::
// FrameIndex/FixedStepIndex never reset, unlike LastFixedStepCount).
ScriptFunctionCallResult FrameIndex(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "frame", ScriptValue{ static_cast<std::int64_t>(context.scene->Runtime().FrameIndex()) } } },
        .errors = {},
    };
}

ScriptFunctionCallResult FixedStepIndex(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "step", ScriptValue{ static_cast<std::int64_t>(context.scene->Runtime().FixedStepIndex()) } } },
        .errors = {},
    };
}

// LIB-066: World.Spawn(prefab?, pose, parent?) — "prefab" is optional
// (empty means "spawn a blank entity", the pre-LIB-066 behavior, kept
// byte-for-byte via its own branch below so existing callers are
// unaffected); "pose" decomposes into the existing x/y/z position pins
// plus new rotX/rotY/rotZ/rotW rotation pins (kb::math::Pose{position,
// rotation}, LIB-042's decomposition idiom already established for
// Vec3/Quat elsewhere in this codebase).
//
// The "defined flush": ONLY when an external parent was actually assigned
// does this call SynchronizeTransforms() before returning, so the
// returned handle's WORLD position/rotation reflects the new parent
// immediately rather than staying stale until the next scheduled
// hierarchy sync. A root spawn (no parent) needs no flush at all — its
// world transform trivially equals its local transform, identical to
// every other unparented entity in this engine — and skipping the sync in
// that case matters: SynchronizeTransforms() is otherwise only ever
// called from Scene::Runtime().Update()'s own fixed points; calling it
// unconditionally from inside a script function turned out to disturb
// state an in-flight Tick() elsewhere in the same frame depends on (a
// physics raycast run later in the same Tick started hitting the wrong
// collider once every Spawn — even unparented ones — force-synced).
// LIB-071: the parent a World.Spawn should use. An explicit `parent`
// argument is honored exactly as before (a dead one falls through to a
// detached root, unchanged). ONLY when the caller passes no `parent` at all
// does the spawn default into the ACTIVE loaded scene's root (Scene.SetActive
// / SceneLoadedContent::ActiveSceneRoot) — so Scene.SetActive genuinely
// steers where new content lands, instead of being a purely observable flag.
// With no loaded/active scene, ActiveSceneRoot() is invalid and the spawn is
// a root, identical to the pre-LIB-071 default.
[[nodiscard]] kb::scene::SceneEntity ResolveSpawnParent(kb::scene::Scene& scene, std::span<const ScriptFunctionArgument> arguments) {
    if (HasArg(arguments, "parent")) {
        return EntityArg(arguments, "parent");
    }
    return scene.LoadedContent().ActiveSceneRoot();
}

ScriptFunctionCallResult Spawn(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const std::string prefabPath = StringArg(arguments, "prefab");
    kb::scene::SceneEntity entity{};
    bool parented = false;
    if (prefabPath.empty()) {
        kb::scene::SceneObjectDesc desc;
        desc.name = StringArg(arguments, "name", "Entity");
        ApplyPositionArgs(desc.transform, arguments);
        ApplyRotationArgs(desc.transform, arguments);
        const kb::scene::SceneEntity parent = ResolveSpawnParent(*context.scene, arguments);
        parented = parent.IsValid() && context.scene->Entities().IsAlive(parent);
        if (parented) {
            desc.parent = context.scene->Entities().Object(parent);
        }
        entity = context.scene->Entities().CreateEntity(std::move(desc));
    } else {
        kb::library::PrefabRef prefab = context.scene->Assets().LoadPrefab(std::filesystem::path{ prefabPath });
        if (!prefab.IsLoaded()) {
            return Error("prefab asset could not be loaded");
        }
        const kb::scene::ScenePrefabInstance instance = context.scene->Prefabs().Instantiate(*prefab.Get());
        entity = instance.Empty() ? kb::scene::SceneEntity{} : instance.ObjectAt(0).Entity();
        if (!entity.IsValid()) {
            return Error("prefab instantiation produced no root entity");
        }
        if (HasArg(arguments, "x") || HasArg(arguments, "y") || HasArg(arguments, "z") || HasArg(arguments, "rotX") || HasArg(arguments, "rotY") ||
            HasArg(arguments, "rotZ") || HasArg(arguments, "rotW")) {
            kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
            ApplyPositionArgs(transform, arguments);
            ApplyRotationArgs(transform, arguments);
            context.scene->Transforms().Set(entity, transform);
        }
        const kb::scene::SceneEntity parent = ResolveSpawnParent(*context.scene, arguments);
        parented = parent.IsValid() && context.scene->Entities().IsAlive(parent);
        if (parented) {
            // Matches the blank-entity path above, which also has no
            // separate failure signal for a rejected parent assignment.
            static_cast<void>(context.scene->Hierarchy().SetParent(entity, parent));
        }
    }
    if (parented) {
        context.scene->Runtime().SynchronizeTransforms();
    }
    return EntityResult("entity", entity);
}

// LIB-067: idempotent by construction — a repeat call on an already-dead
// (or never-alive) entity takes the existed==false branch, returns
// destroyed=false, and never touches the world again; calling it any number
// of times leaves the world in the same state as calling it once.
//
// deferred=true queues the entity for destruction at the next frame playback
// point (SceneEntities::DrainDeferredDestroys, run once per frame by
// ScriptRuntimeSceneSystem::ExecuteFrame) instead of destroying it inline.
// This is the safe way to honor the flag: a script that destroys the entity
// it is currently iterating over — or any entity whose components a later
// behaviour this frame still reads — must not have the storage yanked out
// mid-frame. The drain re-checks liveness per handle, so a deferred destroy
// racing an immediate destroy of the same entity, a stale generation, or a
// duplicate request is a harmless no-op (no CommandBuffer stale-handle throw).
// `destroyed` reports whether the entity was alive at the call (i.e. there
// was something to destroy / queue), mirroring the immediate path — for a
// deferred call the actual teardown lands at the frame boundary.
ScriptFunctionCallResult Destroy(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    const bool existed = entity.IsValid() && context.scene->Entities().IsAlive(entity);
    const ScriptValue* deferredArg = FindArg(arguments, "deferred");
    const bool deferred = deferredArg != nullptr && deferredArg->AsBool(false);
    if (existed) {
        if (deferred) {
            context.scene->Entities().QueueDeferredDestroy(entity);
        } else {
            context.scene->Entities().Destroy(entity);
        }
    }
    return BoolResult("destroyed", existed);
}

ScriptFunctionCallResult SetTag(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    const std::string tag = StringArg(arguments, "tag");
    const ScriptValue* enabledValue = FindArg(arguments, "enabled");
    const bool enabled = enabledValue == nullptr || enabledValue->AsBool(true);
    const std::string normalizedTag = Trim(tag);
    if (!entity.IsValid() || !context.scene->Entities().IsAlive(entity) || normalizedTag.empty()) {
        return BoolResult("tagged", false);
    }

    kb::scene::SceneTagsComponents tagsComponents = context.scene->Components().Tags();
    std::vector<std::string> tags;
    if (const kb::scene::TagsComponent* current = tagsComponents.TryGet(entity)) {
        tags = ParseTags(kb::scene::TagsText(*current));
    }
    const auto existing = std::find(tags.begin(), tags.end(), normalizedTag);
    if (enabled) {
        if (existing == tags.end()) {
            tags.push_back(normalizedTag);
        }
    } else if (existing != tags.end()) {
        tags.erase(existing);
    }

    if (tags.empty()) {
        tagsComponents.Remove(entity);
    } else {
        kb::scene::TagsComponent tagsComponent;
        kb::scene::SetTagsText(tagsComponent, JoinTags(tags));
        tagsComponents.Set(entity, tagsComponent);
    }
    return BoolResult("tagged", true);
}

ScriptFunctionCallResult HasTag(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    const std::string tag = Trim(StringArg(arguments, "tag"));
    const kb::scene::TagsComponent* tags = context.scene->Components().Tags().TryGet(entity);
    const bool tagged = tags != nullptr && HasTagValue(kb::scene::TagsText(*tags), tag);
    return BoolResult("tagged", tagged);
}

struct FindByTagContext {
    kb::scene::Scene* scene = nullptr;
    std::string_view tag;
    kb::scene::SceneEntity found{};
};

void FindByTagVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent&, void* rawContext) {
    auto* context = static_cast<FindByTagContext*>(rawContext);
    if (context == nullptr || context->scene == nullptr || context->found.IsValid()) {
        return;
    }
    const kb::scene::TagsComponent* tags = context->scene->Components().Tags().TryGet(entity);
    if (tags != nullptr && HasTagValue(kb::scene::TagsText(*tags), context->tag)) {
        context->found = entity;
    }
}

// LIB-069 (documented cost): same O(n) linear scan as FindByName above —
// no tag index exists, every entity's TagsComponent is checked in turn
// until the first match, early-exiting there.
ScriptFunctionCallResult FindByTag(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const std::string tag = Trim(StringArg(arguments, "tag"));
    FindByTagContext findContext{
        .scene = context.scene,
        .tag = tag,
        .found = {},
    };
    context.scene->Transforms().ForEach(&FindByTagVisitor, &findContext);
    return EntityResult("entity", findContext.found);
}

struct FindAllByTagContext {
    kb::scene::Scene* scene = nullptr;
    std::string_view tag;
    int skip = 0;
    int matchIndex = 0;
    kb::scene::SceneEntity found{};
};

void FindAllByTagVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent&, void* rawContext) {
    auto* context = static_cast<FindAllByTagContext*>(rawContext);
    if (context == nullptr || context->scene == nullptr || context->found.IsValid()) {
        return;
    }
    const kb::scene::TagsComponent* tags = context->scene->Components().Tags().TryGet(entity);
    if (tags == nullptr || !HasTagValue(kb::scene::TagsText(*tags), context->tag)) {
        return;
    }
    if (context->matchIndex == context->skip) {
        context->found = entity;
        return;
    }
    ++context->matchIndex;
}

// LIB-069: FindAllByTag has no dedicated collection type crossing the
// script boundary (kb::script::ScriptValue is purely scalar; a real
// collection-handle bridge is a separate, larger, still-undecided
// architectural piece — see others/_temp.md's LIB-058 note). Instead it's
// an index-based iterator: call repeatedly with an increasing `skip`
// (0, 1, 2, ...) — same "entity" output/invalid-entity-means-not-found
// convention FindByName/FindByTag above already use — until the returned
// entity is invalid, meaning no (skip+1)-th match exists. This mirrors
// the pattern already established by RandomStream's {value, newState}
// idiom (LIB-051): the caller explicitly threads state (here, the skip
// count) across calls rather than the engine holding an iterator alive
// only on its own side of the boundary.
//
// Documented cost: EACH call is an independent O(n) linear scan from the
// start of the scene (Transforms().ForEach), early-exiting once it
// reaches the (skip+1)-th match — so retrieving all m matches out of n
// entities costs O(n*m) in total, not O(n). This is a real, honest
// trade-off against building the collection-crossing-boundary
// infrastructure FindAllByTag would need to do better (a single O(n)
// scan producing a script-visible list) — acceptable for the occasional
// "find every enemy" query this API targets, not for a hot per-frame path
// over a scene with many thousands of tagged entities.
ScriptFunctionCallResult FindAllByTag(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const std::string tag = Trim(StringArg(arguments, "tag"));
    const int skip = IntArg(arguments, "skip", 0);
    FindAllByTagContext findContext{
        .scene = context.scene,
        .tag = tag,
        .skip = skip < 0 ? 0 : skip,
        .matchIndex = 0,
        .found = {},
    };
    context.scene->Transforms().ForEach(&FindAllByTagVisitor, &findContext);
    return EntityResult("entity", findContext.found);
}

ScriptFunctionCallResult SetParent(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    const kb::scene::SceneEntity parent = EntityArg(arguments, "parent");
    if (!entity.IsValid() || !context.scene->Entities().IsAlive(entity)) {
        return BoolResult("parented", false);
    }
    const bool parented = context.scene->Hierarchy().SetParent(entity, parent);
    return BoolResult("parented", parented);
}

// LIB-070 ("data overrides"): researched before implementing — the ONLY
// existing generic property setter reachable from script (Self.SetProperty,
// PucLuaSelfApi.cpp) is hand-wired Lua-only sugar hardcoded to the calling
// behaviour's own Self entity, not through ScriptFunctionRegistry, and
// cannot target an arbitrary entity (like a freshly instantiated prefab's
// root, which is never the caller's own Self). kb::scene::ScenePrefabs::
// Instantiate() itself has no override-value parameter, and the separate
// ScenePrefabOverride* system is editor authoring-time divergence
// tracking (diff a live instance against its source prefab template for
// revert support) — it takes no VALUE to apply, only a property PATH, so
// it cannot inject an override either. kb::script::ScriptSceneComponentApi
// ::SetProperty IS already fully generic (any entity, not just Self) at
// the C++ level — the gap is purely that no script-callable wrapper
// exposes it for an explicit entity. World.SetPropertyBool/Int/Float/
// String/Entity below close that gap, through the same ScriptFunctionRegistry
// path every other World.* function uses (Native+Lua+Visual Graph
// uniformly, unlike Self.SetProperty's Lua-only wiring).
//
// World.SetProperty* remains useful for live mutations. Atomic instantiate
// parameters use a caller-owned CreatePrefabParameters/SetPrefabParameter*
// transaction consumed by World.InstantiatePrefab; any invalid entry rolls
// back the whole new hierarchy before it becomes observable to a frame.
[[nodiscard]] std::string ComponentArg(std::span<const ScriptFunctionArgument> arguments) {
    return StringArg(arguments, "component");
}

[[nodiscard]] std::string PropertyArg(std::span<const ScriptFunctionArgument> arguments) {
    return StringArg(arguments, "property");
}

template <typename ValueGetter>
ScriptFunctionCallResult SetEntityProperty(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments, ValueGetter&& valueGetter) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!entity.IsValid() || !context.scene->Entities().IsAlive(entity)) {
        return BoolResult("set", false);
    }
    const ScriptSceneComponentMutationResult result =
        ScriptSceneComponentApi::SetProperty(*context.scene, entity, ComponentArg(arguments), PropertyArg(arguments), valueGetter());
    return BoolResult("set", result.succeeded);
}

ScriptFunctionCallResult SetPropertyBool(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return SetEntityProperty(context, arguments, [&] { return ScriptValue{ FindArg(arguments, "value") != nullptr && FindArg(arguments, "value")->AsBool(false) }; });
}

ScriptFunctionCallResult SetPropertyInt(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return SetEntityProperty(context, arguments, [&] { return ScriptValue{ IntArg(arguments, "value", 0) }; });
}

ScriptFunctionCallResult SetPropertyFloat(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return SetEntityProperty(context, arguments, [&] { return ScriptValue{ FloatArg(arguments, "value", 0.0F) }; });
}

ScriptFunctionCallResult SetPropertyString(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return SetEntityProperty(context, arguments, [&] { return ScriptValue{ StringArg(arguments, "value") }; });
}

ScriptFunctionCallResult SetPropertyEntity(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return SetEntityProperty(context, arguments, [&] { return ScriptValue{ EntityArg(arguments, "value").Id(), ScriptValueType::Entity }; });
}

[[nodiscard]] std::uint64_t ContextOwner(const ScriptFunctionCallContext& context) noexcept {
    return context.caller.IsValid() ? context.caller.Id() : 0U;
}

ScriptFunctionCallResult CreatePrefabParameters(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    kb::scene::SceneState& state = kb::scene::SceneAccess::State(*context.scene);
    std::uint64_t id = state.nextScriptPrefabParameterSetId++;
    if (id == 0U) {
        id = state.nextScriptPrefabParameterSetId++;
    }
    state.scriptPrefabParameterSets.emplace(id, kb::scene::SceneState::ScriptPrefabParameterSet{
        .owner = context.caller,
        .parameters = {},
    });
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "parameters", ScriptValue{ id, ScriptValueType::Hash } } },
        .errors = {},
    };
}

template <typename ValueFactory>
ScriptFunctionCallResult SetPrefabParameter(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments,
    ValueFactory&& valueFactory) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const ScriptValue* idValue = FindArg(arguments, "parameters");
    const std::uint64_t id = idValue == nullptr ? 0U : idValue->AsUInt64();
    const int node = IntArg(arguments, "node", 0);
    const std::string component = ComponentArg(arguments);
    const std::string property = PropertyArg(arguments);
    kb::scene::SceneState& state = kb::scene::SceneAccess::State(*context.scene);
    const auto iterator = state.scriptPrefabParameterSets.find(id);
    if (id == 0U || iterator == state.scriptPrefabParameterSets.end() ||
        iterator->second.owner.Id() != ContextOwner(context) || node < 0 ||
        component.empty() || property.empty()) {
        return BoolResult("set", false);
    }
    iterator->second.parameters.push_back(kb::scene::SceneState::ScriptPrefabParameter{
        .nodeIndex = static_cast<std::uint32_t>(node),
        .component = component,
        .property = property,
        .value = valueFactory(),
    });
    return BoolResult("set", true);
}

ScriptFunctionCallResult SetPrefabParameterBool(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return SetPrefabParameter(context, arguments, [&] { return ScriptValue{ FindArg(arguments, "value") != nullptr && FindArg(arguments, "value")->AsBool(false) }; });
}

ScriptFunctionCallResult SetPrefabParameterInt(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return SetPrefabParameter(context, arguments, [&] { return ScriptValue{ IntArg(arguments, "value", 0) }; });
}

ScriptFunctionCallResult SetPrefabParameterFloat(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return SetPrefabParameter(context, arguments, [&] { return ScriptValue{ FloatArg(arguments, "value", 0.0F) }; });
}

ScriptFunctionCallResult SetPrefabParameterString(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return SetPrefabParameter(context, arguments, [&] { return ScriptValue{ StringArg(arguments, "value") }; });
}

ScriptFunctionCallResult SetPrefabParameterEntity(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return SetPrefabParameter(context, arguments, [&] { return ScriptValue{ EntityArg(arguments, "value").Id(), ScriptValueType::Entity }; });
}

ScriptFunctionCallResult InstantiatePrefab(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const std::string prefabPath = StringArg(arguments, "prefab");
    if (prefabPath.empty()) {
        return Error("prefab path is empty");
    }
    kb::library::PrefabRef prefab = context.scene->Assets().LoadPrefab(std::filesystem::path{ prefabPath });
    if (!prefab.IsLoaded()) {
        return Error("prefab asset could not be loaded");
    }
    kb::scene::SceneState& state = kb::scene::SceneAccess::State(*context.scene);
    const ScriptValue* parameterSetValue = FindArg(arguments, "parameters");
    const std::uint64_t parameterSetId = parameterSetValue == nullptr ? 0U : parameterSetValue->AsUInt64();
    std::vector<kb::scene::SceneState::ScriptPrefabParameter> parameters;
    if (parameterSetId != 0U) {
        const auto parameterSet = state.scriptPrefabParameterSets.find(parameterSetId);
        if (parameterSet == state.scriptPrefabParameterSets.end() ||
            parameterSet->second.owner.Id() != ContextOwner(context)) {
            return Error("prefab parameter set is invalid or belongs to another caller");
        }
        parameters = std::move(parameterSet->second.parameters);
        state.scriptPrefabParameterSets.erase(parameterSet);
    }

    const kb::scene::ScenePrefabInstance instance = context.scene->Prefabs().Instantiate(*prefab.Get());
    const kb::scene::SceneEntity root = instance.Empty() ? kb::scene::SceneEntity{} : instance.ObjectAt(0).Entity();
    if (!root.IsValid()) {
        return Error("prefab instantiation produced no root entity");
    }
    for (const kb::scene::SceneState::ScriptPrefabParameter& parameter : parameters) {
        const kb::scene::SceneObject target = instance.ObjectAt(parameter.nodeIndex);
        const ScriptSceneComponentMutationResult applied = target.IsValid()
            ? ScriptSceneComponentApi::SetProperty(*context.scene, target.Entity(), parameter.component, parameter.property, parameter.value)
            : ScriptSceneComponentMutationResult{ .succeeded = false, .error = "prefab parameter node is out of range" };
        if (!applied.succeeded) {
            context.scene->Entities().Destroy(instance.Objects());
            return Error("prefab parameter '" + parameter.component + "." + parameter.property + "' failed: " + applied.error);
        }
    }
    // LIB-160: full spawn transform (position + rotation + scale) applied to
    // the root, matching World.Spawn's established post-instantiate pattern
    // (extended here with scale). Any subset of pins may be supplied.
    if (root.IsValid() && HasAnyTransformArg(arguments)) {
        kb::scene::TransformComponent transform = context.scene->Transforms().Get(root);
        ApplyPositionArgs(transform, arguments);
        ApplyRotationArgs(transform, arguments);
        ApplyScaleArgs(transform, arguments);
        context.scene->Transforms().Set(root, transform);
    }
    // LIB-160: parent the instantiated root under a given entity, mirroring
    // World.Spawn's SetParent + SynchronizeTransforms pattern (the native
    // ScenePrefabInstantiationSettings::parent capability, reached the same
    // proven way World.Spawn already does rather than a separate settings
    // path). A rejected/invalid parent is silently ignored, exactly as
    // World.Spawn's own parent path (no separate failure signal).
    if (root.IsValid()) {
        const kb::scene::SceneEntity parent = EntityArg(arguments, "parent");
        if (parent.IsValid() && context.scene->Entities().IsAlive(parent)) {
            static_cast<void>(context.scene->Hierarchy().SetParent(root, parent));
            context.scene->Runtime().SynchronizeTransforms();
        }
    }
    // LIB-160: completion callback — queue an entity-local "OnPrefabInstantiated"
    // event to the CALLER (the script that requested the spawn), carrying the
    // root and object count. Drained next frame by ScriptRuntimeSceneSystem.
    // A synchronous instantiate has no async result to await; this is the
    // decoupled push channel (mirror TaskCompleted), not a fabricated async
    // completion. No-op when the caller is not a live entity.
    context.scene->Prefabs().QueueInstantiatedEvent(context.caller, root, instance.ObjectCount());
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "entity", ScriptValue{ root.Id(), ScriptValueType::Entity } },
            ScriptFunctionArgument{ "count", ScriptValue{ static_cast<int>(instance.ObjectCount()) } },
        },
        .errors = {},
    };
}

bool RegisterFunction(
    ScriptRuntimeHost& host,
    std::string name,
    std::vector<ScriptFunctionPin> inputs,
    std::vector<ScriptFunctionPin> outputs,
    ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = std::move(inputs);
    desc.signature.outputs = std::move(outputs);
    desc.callback = std::move(callback);
    return host.RegisterFunction(std::move(desc));
}

} // namespace

void ScriptWorldApi::ReleaseDeadPrefabParameterSets(kb::scene::Scene& scene) {
    kb::scene::SceneState& state = kb::scene::SceneAccess::State(scene);
    for (auto iterator = state.scriptPrefabParameterSets.begin(); iterator != state.scriptPrefabParameterSets.end();) {
        const kb::scene::SceneEntity owner = iterator->second.owner;
        if (owner.IsValid() && !scene.Entities().IsAlive(owner)) {
            iterator = state.scriptPrefabParameterSets.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

bool ScriptWorldApi::Register(ScriptRuntimeHost& host) {
    bool ok = true;
    ok = RegisterFunction(host, "World.Current",
        {},
        { ScriptFunctionPin{ "world", ScriptValueType::Hash, true } },
        &Current) && ok;
    ok = RegisterFunction(host, "World.IsPlaying",
        {},
        { ScriptFunctionPin{ "playing", ScriptValueType::Bool, true } },
        &IsPlaying) && ok;
    ok = RegisterFunction(host, "World.FrameIndex",
        {},
        { ScriptFunctionPin{ "frame", ScriptValueType::Int64, true } },
        &FrameIndex) && ok;
    ok = RegisterFunction(host, "World.FixedStepIndex",
        {},
        { ScriptFunctionPin{ "step", ScriptValueType::Int64, true } },
        &FixedStepIndex) && ok;
    ok = RegisterFunction(host, "World.FindByName",
        { ScriptFunctionPin{ "name", ScriptValueType::String, true } },
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        &FindByName) && ok;
    ok = RegisterFunction(host, "World.Exists",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        { ScriptFunctionPin{ "exists", ScriptValueType::Bool, true } },
        &Exists) && ok;
    ok = RegisterFunction(host, "World.IsActive",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        { ScriptFunctionPin{ "active", ScriptValueType::Bool, true } },
        &IsActive) && ok;
    ok = RegisterFunction(host, "World.SetActive",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true }, ScriptFunctionPin{ "active", ScriptValueType::Bool, false } },
        { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } },
        &SetActive) && ok;
    ok = RegisterFunction(host, "World.IsPersistent",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        { ScriptFunctionPin{ "persistent", ScriptValueType::Bool, true } },
        &IsPersistent) && ok;
    ok = RegisterFunction(host, "World.SetPersistent",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true }, ScriptFunctionPin{ "persistent", ScriptValueType::Bool, false } },
        { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } },
        &SetPersistent) && ok;
    ok = RegisterFunction(host, "World.Name",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        { ScriptFunctionPin{ "name", ScriptValueType::String, true } },
        &Name) && ok;
    ok = RegisterFunction(host, "World.Spawn",
        {
            ScriptFunctionPin{ "name", ScriptValueType::String, false },
            ScriptFunctionPin{ "prefab", ScriptValueType::String, false },
            ScriptFunctionPin{ "parent", ScriptValueType::Entity, false },
            ScriptFunctionPin{ "x", ScriptValueType::Float, false },
            ScriptFunctionPin{ "y", ScriptValueType::Float, false },
            ScriptFunctionPin{ "z", ScriptValueType::Float, false },
            ScriptFunctionPin{ "rotX", ScriptValueType::Float, false },
            ScriptFunctionPin{ "rotY", ScriptValueType::Float, false },
            ScriptFunctionPin{ "rotZ", ScriptValueType::Float, false },
            ScriptFunctionPin{ "rotW", ScriptValueType::Float, false },
        },
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        &Spawn) && ok;
    ok = RegisterFunction(host, "World.Destroy",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true }, ScriptFunctionPin{ "deferred", ScriptValueType::Bool, false } },
        { ScriptFunctionPin{ "destroyed", ScriptValueType::Bool, true } },
        &Destroy) && ok;
    ok = RegisterFunction(host, "World.SetTag",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true }, ScriptFunctionPin{ "tag", ScriptValueType::String, true }, ScriptFunctionPin{ "enabled", ScriptValueType::Bool, false } },
        { ScriptFunctionPin{ "tagged", ScriptValueType::Bool, true } },
        &SetTag) && ok;
    ok = RegisterFunction(host, "World.HasTag",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true }, ScriptFunctionPin{ "tag", ScriptValueType::String, true } },
        { ScriptFunctionPin{ "tagged", ScriptValueType::Bool, true } },
        &HasTag) && ok;
    ok = RegisterFunction(host, "World.FindByTag",
        { ScriptFunctionPin{ "tag", ScriptValueType::String, true } },
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        &FindByTag) && ok;
    ok = RegisterFunction(host, "World.FindAllByTag",
        { ScriptFunctionPin{ "tag", ScriptValueType::String, true }, ScriptFunctionPin{ "skip", ScriptValueType::Int, false } },
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        &FindAllByTag) && ok;
    ok = RegisterFunction(host, "World.SetParent",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true }, ScriptFunctionPin{ "parent", ScriptValueType::Entity, false } },
        { ScriptFunctionPin{ "parented", ScriptValueType::Bool, true } },
        &SetParent) && ok;
    ok = RegisterFunction(host, "World.CreatePrefabParameters", {},
        { ScriptFunctionPin{ "parameters", ScriptValueType::Hash, true } },
        &CreatePrefabParameters) && ok;
    ok = RegisterFunction(host, "World.SetPrefabParameterBool",
        { ScriptFunctionPin{ "parameters", ScriptValueType::Hash, true }, ScriptFunctionPin{ "node", ScriptValueType::Int, false },
            ScriptFunctionPin{ "component", ScriptValueType::String, true }, ScriptFunctionPin{ "property", ScriptValueType::String, true },
            ScriptFunctionPin{ "value", ScriptValueType::Bool, true } },
        { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetPrefabParameterBool) && ok;
    ok = RegisterFunction(host, "World.SetPrefabParameterInt",
        { ScriptFunctionPin{ "parameters", ScriptValueType::Hash, true }, ScriptFunctionPin{ "node", ScriptValueType::Int, false },
            ScriptFunctionPin{ "component", ScriptValueType::String, true }, ScriptFunctionPin{ "property", ScriptValueType::String, true },
            ScriptFunctionPin{ "value", ScriptValueType::Int, true } },
        { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetPrefabParameterInt) && ok;
    ok = RegisterFunction(host, "World.SetPrefabParameterFloat",
        { ScriptFunctionPin{ "parameters", ScriptValueType::Hash, true }, ScriptFunctionPin{ "node", ScriptValueType::Int, false },
            ScriptFunctionPin{ "component", ScriptValueType::String, true }, ScriptFunctionPin{ "property", ScriptValueType::String, true },
            ScriptFunctionPin{ "value", ScriptValueType::Float, true } },
        { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetPrefabParameterFloat) && ok;
    ok = RegisterFunction(host, "World.SetPrefabParameterString",
        { ScriptFunctionPin{ "parameters", ScriptValueType::Hash, true }, ScriptFunctionPin{ "node", ScriptValueType::Int, false },
            ScriptFunctionPin{ "component", ScriptValueType::String, true }, ScriptFunctionPin{ "property", ScriptValueType::String, true },
            ScriptFunctionPin{ "value", ScriptValueType::String, true } },
        { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetPrefabParameterString) && ok;
    ok = RegisterFunction(host, "World.SetPrefabParameterEntity",
        { ScriptFunctionPin{ "parameters", ScriptValueType::Hash, true }, ScriptFunctionPin{ "node", ScriptValueType::Int, false },
            ScriptFunctionPin{ "component", ScriptValueType::String, true }, ScriptFunctionPin{ "property", ScriptValueType::String, true },
            ScriptFunctionPin{ "value", ScriptValueType::Entity, true } },
        { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetPrefabParameterEntity) && ok;
    ok = RegisterFunction(host, "World.InstantiatePrefab",
        {
            ScriptFunctionPin{ "prefab", ScriptValueType::String, true },
            ScriptFunctionPin{ "parameters", ScriptValueType::Hash, false },
            ScriptFunctionPin{ "parent", ScriptValueType::Entity, false },
            ScriptFunctionPin{ "x", ScriptValueType::Float, false },
            ScriptFunctionPin{ "y", ScriptValueType::Float, false },
            ScriptFunctionPin{ "z", ScriptValueType::Float, false },
            ScriptFunctionPin{ "rotX", ScriptValueType::Float, false },
            ScriptFunctionPin{ "rotY", ScriptValueType::Float, false },
            ScriptFunctionPin{ "rotZ", ScriptValueType::Float, false },
            ScriptFunctionPin{ "rotW", ScriptValueType::Float, false },
            ScriptFunctionPin{ "scaleX", ScriptValueType::Float, false },
            ScriptFunctionPin{ "scaleY", ScriptValueType::Float, false },
            ScriptFunctionPin{ "scaleZ", ScriptValueType::Float, false },
        },
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true }, ScriptFunctionPin{ "count", ScriptValueType::Int, true } },
        &InstantiatePrefab) && ok;
    // LIB-070: data-override family — set a property on ANY entity (unlike
    // the pre-existing Self.SetProperty Lua sugar, hardcoded to the
    // calling behaviour's own entity), reachable from Native/Lua/Visual
    // Graph alike since these go through the standard ScriptFunctionRegistry
    // path, not Self.SetProperty's separate hand-wired VisualGraph
    // SetProperty-node-kind machinery.
    ok = RegisterFunction(host, "World.SetPropertyBool",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true }, ScriptFunctionPin{ "component", ScriptValueType::String, true },
            ScriptFunctionPin{ "property", ScriptValueType::String, true }, ScriptFunctionPin{ "value", ScriptValueType::Bool, true } },
        { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } },
        &SetPropertyBool) && ok;
    ok = RegisterFunction(host, "World.SetPropertyInt",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true }, ScriptFunctionPin{ "component", ScriptValueType::String, true },
            ScriptFunctionPin{ "property", ScriptValueType::String, true }, ScriptFunctionPin{ "value", ScriptValueType::Int, true } },
        { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } },
        &SetPropertyInt) && ok;
    ok = RegisterFunction(host, "World.SetPropertyFloat",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true }, ScriptFunctionPin{ "component", ScriptValueType::String, true },
            ScriptFunctionPin{ "property", ScriptValueType::String, true }, ScriptFunctionPin{ "value", ScriptValueType::Float, true } },
        { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } },
        &SetPropertyFloat) && ok;
    ok = RegisterFunction(host, "World.SetPropertyString",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true }, ScriptFunctionPin{ "component", ScriptValueType::String, true },
            ScriptFunctionPin{ "property", ScriptValueType::String, true }, ScriptFunctionPin{ "value", ScriptValueType::String, true } },
        { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } },
        &SetPropertyString) && ok;
    ok = RegisterFunction(host, "World.SetPropertyEntity",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true }, ScriptFunctionPin{ "component", ScriptValueType::String, true },
            ScriptFunctionPin{ "property", ScriptValueType::String, true }, ScriptFunctionPin{ "value", ScriptValueType::Entity, true } },
        { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } },
        &SetPropertyEntity) && ok;
    return ok;
}

} // namespace kb::script
