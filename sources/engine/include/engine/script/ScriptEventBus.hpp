#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/script/ScriptEvent.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::script {

// LIB-105: the engine's first real pub/sub bus, additive to (not a
// replacement of) the existing ScriptEvent/ScriptRuntime::DispatchEvent
// pipeline. That pipeline delivers an event to a BEHAVIOUR only if the
// behaviour's own asset happens to define a lifecycle-style function or
// NativeScriptBackend callback named EXACTLY the event name — at most ONE
// callback per (assetId, eventName) pair (NativeScriptBackend::RegisterEvent
// overwrites, confirmed by LIB-098's research), and only entities that are
// themselves behaviours can listen at all. ScriptEventBus lets ARBITRARY
// code (native subsystems that own no entity, and Lua scripts) register
// MULTIPLE independent listeners for the same event name, get a stable
// handle back, and unsubscribe it later — the real gap LIB-103 documented
// as "inter-system messaging... does not exist yet".
//
// Deliberately scoped to Native + Lua only. Visual Graph's ScriptValue type
// system has no "callable" alternative (LIB-032/041's closed scalar set) so
// a graph node cannot author or pass a Subscribe callback — LIB-112
// ("bridge zdarzeń gameplay do Visual Graph z typowanymi pinami") is
// explicitly the later, separate backlog item that closes this gap with a
// graph-native mechanism (event nodes with typed pins), not a proportionality
// shortcut invented here.
//
// Subscription handles are monotonically increasing and never reused within
// a bus's lifetime (same reasoning as kb::scene::TimerHandle/LIB-095: an id
// that can never repeat can never collide with a live one, so Unsubscribe
// is trivially safe against a stale or already-unsubscribed handle without a
// generation-checked registry).
using EventSubscriptionHandle = std::uint64_t;

inline constexpr EventSubscriptionHandle kInvalidEventSubscriptionHandle = 0;

// A subscriber callback receives the fully-built ScriptEvent (name, sender,
// target, arguments) exactly as Emit/EmitDeferred/Broadcast constructed it.
// Unlike NativeScriptBackend's per-behaviour NativeScriptEventCallback, a
// subscription is not attached to any one entity's dispatch — the
// subscriber's own closure already captures whatever state/context it
// needs at Subscribe time, so no ScriptExecutionContext is threaded through.
using NativeEventSubscriptionCallback = std::function<void(const ScriptEvent&)>;

// LIB-106: recipient filter criteria for Emit/Broadcast/EmitDeferred,
// generalizing the entity-only `target` parameter Emit already had. Each
// field left at its default means "no constraint on this axis" — an
// unfiltered Emit(scene, event, target) call behaves EXACTLY as it did
// before this task, so no existing caller's delivery set changes. `tag`/
// `component`/`sceneId` are evaluated against a candidate subscription's
// OWNER entity at delivery time (a subscription with no owner, or whose
// owner is currently dead, never matches a non-default tag/component/scene
// filter — the same "no entity identity to match against" rule Emit's
// existing entity `target` filter already applies): `tag` via
// kb::scene::SceneTagsComponents (World.HasTag's own backing store),
// `component` via kb::script::ScriptSceneComponentApi::HasComponent (the
// same runtime, string-name-keyed query World.Has-style component checks
// already use), `sceneId` via kb::scene::SceneLoadedContent::OwningScene
// (which loaded-scene id, from Scene.Load/LIB-071, the owner's root entity
// belongs to; 0 means "not part of any explicitly loaded scene record" and
// can never match a non-zero filter). `channel` is different in kind: it is
// NOT derived from the owner entity, but compared directly against the
// channel the SUBSCRIPTION itself declared at Subscribe time (see
// Subscribe's own `channel` parameter below) — a subscription with no
// channel ("") is reached only by an Emit call that also leaves `channel`
// unset, matching this struct's all-default "no constraint" behavior.
//
// Deliberately NOT a field here: `player`. No Player/LocalUser/PlayerId
// concept exists ANYWHERE in this engine today (confirmed by a full-repo
// grep before implementing) — LIB-195 is the task that will define
// `Player`/`PlayerController`/`Pawn`/`PlayerState` and join/leave lifecycle;
// LIB-115 is per-player Input. Adding a `player` filter now would mean
// inventing that entire concept under this much narrower "event recipient
// filter" task, the same fabrication this codebase has repeatedly refused
// to do (LIB-097/098's yield-reason deferrals, LIB-105's Visual Graph
// deferral to LIB-112). Revisit once LIB-195 exists.
struct EventRecipientFilter {
    std::string tag;
    std::string component;
    std::uint64_t sceneId = 0U;
    std::string channel;
};

// Emit/Broadcast/DrainDeferred bypass ScriptRuntime::DispatchEvent entirely
// (subscriptions are not behaviours), so they cannot rely on that
// pipeline's ScriptDiagnostic vector to surface a misbehaving subscriber —
// a throwing native callback or an erroring Lua handler is caught here
// (never allowed to abort delivery to the OTHER subscribers, the same
// resilience NativeScriptBackend::InvokeNativeCallback already guarantees
// for the old per-behaviour path) and reported back through `errors`
// instead of being silently swallowed.
struct ScriptEventDeliveryResult {
    std::size_t delivered = 0;
    std::vector<std::string> errors;
};

// LIB-105: owned by ScriptRuntime (ScriptRuntime::Events()) — reachable from
// native code holding a ScriptExecutionContext (context.Events()) or a
// ScriptRuntime& directly, and from Lua via the bespoke `Events` table
// (PucLuaEventsApi.cpp), the same "bespoke per-backend attachment" shape
// this codebase already uses for Emit/EmitTo (ScriptExecutionContext::Emit,
// PucLuaEventApi.cpp) rather than the fixed-pin ScriptFunctionRegistry
// model, which cannot carry a callback argument or arbitrary named payload
// arguments.
class ScriptEventBus final {
public:
    // LIB-106: `channel` declares which channel THIS subscription listens
    // on ("" is the default channel) — see EventRecipientFilter::channel's
    // own doc comment for the full matching rule against Emit's filter.
    [[nodiscard]] EventSubscriptionHandle Subscribe(std::string eventName, NativeEventSubscriptionCallback callback, kb::scene::SceneEntity owner = {}, std::string channel = {});
    bool Unsubscribe(EventSubscriptionHandle handle) noexcept;

    // Synchronous: invokes every live subscription matching event.name
    // right now, before returning. `target` invalid (default) reaches every
    // matching subscription regardless of owner; `target` valid reaches
    // only subscriptions whose owner equals it (entity-local, mirrors
    // LIB-103's taxonomy) — a subscription with no owner never matches a
    // targeted Emit, since it has no entity identity to match against.
    // Lazily drops (and skips firing) any subscription whose owner has
    // died or been deactivated since it was registered, same policy as
    // Timer/Task's OwnerGone check (LIB-095/097/099). `filter` (LIB-106)
    // narrows the recipient set further along the tag/component/scene/
    // channel axes — see EventRecipientFilter's doc comment; left at its
    // default, delivery is identical to before this parameter existed.
    ScriptEventDeliveryResult Emit(kb::scene::Scene& scene, const ScriptEvent& event, kb::scene::SceneEntity target = {}, const EventRecipientFilter& filter = {});

    // Always reaches every live subscription matching event.name,
    // regardless of any owner — the same delivery Emit performs when called
    // with no target, given a distinct, self-documenting name for callers
    // (especially Lua/native call sites) that want to make "no targeting,
    // full stop" explicit rather than relying on a default argument.
    // `filter` (LIB-106) applies exactly as it does on Emit.
    ScriptEventDeliveryResult Broadcast(kb::scene::Scene& scene, const ScriptEvent& event, const EventRecipientFilter& filter = {});

    // Queues `event` for delivery at the next DrainDeferred() call instead
    // of firing inline — DrainDeferred is called once per frame by
    // ScriptRuntimeSceneSystem::ExecuteFrame, the same frame-boundary sync
    // point Timer/Task/scene-lifecycle events already drain at. This is a
    // real, observable timing difference from Emit (now vs next drain), not
    // a renamed duplicate: code calling EmitDeferred during dispatch of one
    // event will never see it delivered within that same call stack.
    // `filter` (LIB-106) is stored alongside `target` and re-applied at the
    // eventual DrainDeferred-driven Emit, exactly as if Emit(filter) had
    // been called directly at drain time.
    void EmitDeferred(ScriptEvent event, kb::scene::SceneEntity target = {}, EventRecipientFilter filter = {});

    // Delivers every event queued by EmitDeferred since the last drain, in
    // FIFO order, and clears the queue. Returns the number of subscriber
    // callbacks invoked across all drained events.
    ScriptEventDeliveryResult DrainDeferred(kb::scene::Scene& scene);

    [[nodiscard]] std::size_t SubscriptionCount() const noexcept;

private:
    struct Subscription {
        EventSubscriptionHandle handle = kInvalidEventSubscriptionHandle;
        std::string eventName;
        kb::scene::SceneEntity owner{};
        std::string channel;
        NativeEventSubscriptionCallback callback;
    };

    struct DeferredEmit {
        ScriptEvent event;
        kb::scene::SceneEntity target{};
        EventRecipientFilter filter{};
    };

    std::vector<Subscription> subscriptions_;
    std::vector<DeferredEmit> deferredEmits_;
    EventSubscriptionHandle nextHandle_ = 1;
};

} // namespace kb::script
