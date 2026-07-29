#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/script/ScriptEvent.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
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
// `component`/`sceneId`/`playerId` are evaluated against a candidate subscription's
// OWNER entity at delivery time (a subscription with no owner, or whose
// owner is currently dead, never matches a non-default tag/component/scene/player
// filter — the same "no entity identity to match against" rule Emit's
// existing entity `target` filter already applies): `tag` via
// kb::scene::SceneTagsComponents (World.HasTag's own backing store),
// `component` via kb::script::ScriptSceneComponentApi::HasComponent (the
// same runtime, string-name-keyed query World.Has-style component checks
// already use), `sceneId` via kb::scene::SceneLoadedContent::OwningScene
// (which loaded-scene id, from Scene.Load/LIB-071, the owner's root entity
// belongs to; 0 means "not part of any explicitly loaded scene record" and
// can never match a non-zero filter). `playerId` is the owner's
// InputComponent::localUser — the engine's existing local-player identity
// used by per-player Input. An owner without InputComponent cannot match a
// player filter. `std::nullopt` means no player constraint, so value 0
// remains available for the primary local player. Remote/network player
// identity remains part of LIB-195's future gameplay lifecycle.
// `channel` is different in kind: it is
// NOT derived from the owner entity, but compared directly against the
// channel the SUBSCRIPTION itself declared at Subscribe time (see
// Subscribe's own `channel` parameter below) — a subscription with no
// channel ("") is reached only by an Emit call that also leaves `channel`
// unset, matching this struct's all-default "no constraint" behavior.
struct EventRecipientFilter {
    std::string tag;
    std::string component;
    std::uint64_t sceneId = 0U;
    std::optional<std::uint32_t> playerId;
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

// Controls only delivery to engine-owned behaviour bridge subscriptions.
// Regular native/Lua subscribers are always eligible. A backend that mirrors
// an event to both the legacy behaviour dispatcher and this bus uses
// ExcludeBehaviourBridges on the bus copy so behaviours execute exactly once.
enum class ScriptEventBusAudience : std::uint8_t {
    AllSubscribers = 0,
    ExcludeBehaviourBridges,
};

// A lifetime-safe view of one event stream. The observation remains
// readable after its ScriptEventBus is destroyed, which lets a SceneTask
// own it without retaining or dereferencing the runtime host. Sequence()
// changes once for every valid matching ScriptEventBus Emit or original
// ScriptRuntime behaviour dispatch, regardless of whether that event
// currently has ordinary subscribers.
class ScriptEventObservation final {
public:
    [[nodiscard]] std::uint64_t Sequence() const noexcept { return sequence_; }

private:
    friend class ScriptEventBus;
    std::uint64_t sequence_ = 0U;
};

// LIB-110: a point-in-time telemetry snapshot — the honest observability
// this bus previously had none of beyond the pre-existing SubscriptionCount()
// (still available separately for a cheap, allocation-free single number).
struct ScriptEventBusTelemetrySnapshot {
    // Live subscriptions right now (same value SubscriptionCount() reports).
    std::size_t subscriptionCount = 0;
    // Total Emit() calls ever made (Broadcast/DrainDeferred both funnel
    // through Emit, so their activity is already included here — one
    // instrumentation point covers all three, not three separate counters
    // that could drift apart).
    std::uint64_t emitCalls = 0;
    // Total individual subscriber callback invocations that actually ran
    // and returned normally, summed across every Emit() call ever made.
    std::uint64_t deliveredCount = 0;
    // "invalid events" — whole Emit() calls rejected outright before
    // matching any subscriber: an empty event name, or a payload exceeding
    // kMaxScriptEventArguments (LIB-108).
    std::uint64_t invalidEventCount = 0;
    // "dropped events" — EmitDeferred() calls rejected because the pending
    // deferred queue was already at kMaxPendingDeferredEvents capacity (see
    // that constant's own doc comment) — an event that was NEVER queued at
    // all, not a subscriber that merely didn't match.
    std::uint64_t droppedDeferredEventCount = 0;
    // Wall-clock cost of the most recent Emit() call, and the running total
    // across every Emit() call ever made — std::chrono::steady_clock, the
    // same clock kb::ecs::SystemScheduler/QueryState already use for their
    // own telemetry (WorldTelemetry.hpp), for the same reason (monotonic,
    // never affected by system clock adjustments).
    std::uint64_t lastEmitElapsedNanoseconds = 0;
    std::uint64_t totalEmitElapsedNanoseconds = 0;
};

// LIB-105: owned by ScriptRuntime (ScriptRuntime::Events()) — reachable from
// native code holding a ScriptExecutionContext (context.Events()) or a
// ScriptRuntime& directly, and from Lua via the bespoke `Events` table
// (PucLuaEventsApi.cpp), the same "bespoke per-backend attachment" shape
// this codebase already uses for Emit/EmitTo (ScriptExecutionContext::Emit,
// PucLuaEventApi.cpp) rather than the fixed-pin ScriptFunctionRegistry
// model, which cannot carry a callback argument or arbitrary named payload
// arguments.
//
// LIB-107: DISPATCH MODE CONTRACT. This bus has EXACTLY two delivery modes,
// and no third, ambiguous, or runtime-selectable one:
//   - SYNCHRONOUS (Emit/Broadcast): every matching subscriber is invoked
//     before the call returns, on the calling thread, in the calling call
//     stack. A subscriber that itself calls Emit/Broadcast reentrantly gets
//     that nested delivery ALSO fully synchronously, before its own Emit
//     call returns — sync stays sync under arbitrary reentrancy depth (up
//     to LIB-038's registry-wide call-depth guard, which protects the whole
//     engine against runaway reentrant chains, not something specific to
//     this bus).
//   - DEFERRED (EmitDeferred + DrainDeferred): EmitDeferred NEVER invokes
//     any subscriber itself — it only appends to `deferredEmits_` and
//     returns. Delivery happens ONLY inside a DrainDeferred call, which
//     ScriptRuntimeSceneSystem::ExecuteFrame invokes at exactly ONE
//     well-defined point per frame (DispatchDeferredEvents, after Timer/
//     Task dispatch, before behaviour lifecycle sync) — never ad-hoc,
//     never twice in the same frame. A subscriber invoked BY DrainDeferred
//     that itself calls EmitDeferred again queues into the (by then empty)
//     `deferredEmits_` for the NEXT DrainDeferred call, one frame later —
//     it can never be swept up by the CURRENT drain, because DrainDeferred
//     moves the pending list out (`std::move(deferredEmits_)`) before
//     dispatching any of it (the same "snapshot before dispatching"
//     discipline Emit's own subscriber-matching pass and LIB-102's Timer
//     fix both already use).
//
// NO IMPLICIT MIXING: there is no boolean/flag parameter anywhere on this
// class (or its Lua binding, PucLuaEventsApi.cpp) that silently switches an
// Emit into deferred behavior or an EmitDeferred into synchronous behavior
// — the two modes are ALWAYS selected by calling a differently-named
// function, never by an argument value, mirroring World.Destroy's own
// established "reject an ambiguous deferred=true rather than fake it"
// discipline (LIB-067/ScriptWorldApi.cpp) rather than inventing a NEW kind
// of implicit-mixing risk here. `kb::script::ScriptRuntime::
// DispatchEventAndDrain`/`DrainEvents` (ScriptRuntime.hpp) are a
// DIFFERENT, older, ALWAYS-synchronous mechanism (recursively draining
// ScriptExecutionContext::Emit/EmitTo's own emitted-events queue within one
// call) that predates this bus and shares no code or state with it despite
// the similarly-named "Drain" — see that function's own doc comment for
// the explicit disambiguation.
class ScriptEventBus final {
public:
    // LIB-110: bounds how many not-yet-drained events EmitDeferred can
    // queue — without this, a caller that queues every frame but never
    // drains (or drains less often than it queues) would grow
    // deferredEmits_ without bound, the same unbounded-growth risk
    // kb::scene::SceneTimers/SceneTasks's own kMaxLiveTimers-style caps
    // already guard against. A rejected EmitDeferred is counted honestly
    // via ScriptEventBusTelemetrySnapshot::droppedDeferredEventCount
    // (returned bool from EmitDeferred itself), never silently swallowed.
    // Public (not a private implementation detail) so callers/tests can
    // reason about the real capacity rather than guessing a magic number.
    static constexpr std::size_t kMaxPendingDeferredEvents = 4096U;

    // LIB-111: reentrancy guard — a subscriber that (directly, or through a
    // chain of other subscribers Emit-ing each other) calls Emit again on
    // this SAME bus increments an internal depth counter; past this many
    // nested Emit calls, Emit() rejects the call with an error instead of
    // recursing until the C++ call stack overflows. Mirrors kb::script::
    // ScriptFunctionRegistry's own kMaxCallDepth=64 guard (LIB-038) exactly
    // — same value, same reasoning (comfortably covers legitimate nested
    // gameplay event chains while catching runaway self-recursive Emit long
    // before the stack is actually at risk) — discovered as a genuine,
    // previously-unguarded gap while writing this task's own "recursive
    // emit" test, the same class of latent stack-overflow risk LIB-101
    // found and fixed for Visual Graph's control-flow executor.
    static constexpr std::size_t kMaxEmitDepth = 64U;

    // LIB-106: `channel` declares which channel THIS subscription listens
    // on ("" is the default channel) — see EventRecipientFilter::channel's
    // own doc comment for the full matching rule against Emit's filter.
    [[nodiscard]] EventSubscriptionHandle Subscribe(std::string eventName, NativeEventSubscriptionCallback callback, kb::scene::SceneEntity owner = {}, std::string channel = {});
    // Engine integration point for backends whose behaviour event entries
    // are also reachable through ScriptRuntime's original dispatcher.
    [[nodiscard]] EventSubscriptionHandle SubscribeBehaviourBridge(std::string eventName, NativeEventSubscriptionCallback callback, kb::scene::SceneEntity owner);
    bool Unsubscribe(EventSubscriptionHandle handle) noexcept;

    // Returns a lifetime-safe sequence observation for `eventName`.
    // `recipient` invalid observes broadcasts and targeted emits alike;
    // a valid recipient observes broadcasts plus emits targeted to exactly
    // that entity. Empty names and capacity exhaustion return nullptr.
    // This is deliberately not a callback subscription: tasks can be
    // cancelled or outlive the host without leaving a callback behind.
    [[nodiscard]] std::shared_ptr<const ScriptEventObservation> Observe(
        std::string eventName,
        kb::scene::SceneEntity recipient = {});

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
    // player/channel axes — see EventRecipientFilter's doc comment; left at its
    // default, delivery is identical to before this parameter existed.
    ScriptEventDeliveryResult Emit(
        kb::scene::Scene& scene,
        const ScriptEvent& event,
        kb::scene::SceneEntity target = {},
        const EventRecipientFilter& filter = {},
        ScriptEventBusAudience audience = ScriptEventBusAudience::AllSubscribers);

    // Always reaches every live subscription matching event.name,
    // regardless of any owner — the same delivery Emit performs when called
    // with no target, given a distinct, self-documenting name for callers
    // (especially Lua/native call sites) that want to make "no targeting,
    // full stop" explicit rather than relying on a default argument.
    // `filter` (LIB-106) applies exactly as it does on Emit.
    ScriptEventDeliveryResult Broadcast(kb::scene::Scene& scene, const ScriptEvent& event, const EventRecipientFilter& filter = {});

    // Batch entry point for a caller-owned contiguous event buffer. Events
    // retain Emit's exact synchronous ordering and validation; the bus does
    // not construct or retain an input collection. Errors are appended to
    // the supplied result so callers may reserve its storage up front.
    void EmitBatch(
        kb::scene::Scene& scene,
        std::span<const ScriptEvent> events,
        ScriptEventDeliveryResult& result,
        const EventRecipientFilter& filter = {},
        ScriptEventBusAudience audience = ScriptEventBusAudience::AllSubscribers);

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
    //
    // LIB-110: returns false (and honestly drops the event — see
    // kMaxPendingDeferredEvents's own doc comment, not marked [[nodiscard]]
    // so every pre-existing call site, none of which checked a return value
    // because none existed before this task, still compiles unchanged) if
    // the pending queue is already at capacity.
    bool EmitDeferred(ScriptEvent event, kb::scene::SceneEntity target = {}, EventRecipientFilter filter = {});

    // Delivers every event queued by EmitDeferred since the last drain, in
    // FIFO order, and clears the queue. Returns the number of subscriber
    // callbacks invoked across all drained events.
    ScriptEventDeliveryResult DrainDeferred(kb::scene::Scene& scene);

    [[nodiscard]] std::size_t SubscriptionCount() const noexcept;

    // LIB-110: a cheap, allocation-free point-in-time snapshot of this
    // bus's own observability counters — subscription count, dispatch
    // duration, and dropped/invalid events. See ScriptEventBusTelemetrySnapshot's
    // own doc comment for exactly what each field means.
    [[nodiscard]] ScriptEventBusTelemetrySnapshot Telemetry() const noexcept;

private:
    friend class ScriptRuntime;

    struct ObservationNameHasher {
        using is_transparent = void;

        [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }
    };

    using ObservationRecipients = std::unordered_map<std::uint64_t, std::weak_ptr<ScriptEventObservation>>;

    struct Subscription {
        EventSubscriptionHandle handle = kInvalidEventSubscriptionHandle;
        std::string eventName;
        kb::scene::SceneEntity owner{};
        std::string channel;
        bool behaviourBridge = false;
        NativeEventSubscriptionCallback callback;
    };

    struct DeferredEmit {
        ScriptEvent event;
        kb::scene::SceneEntity target{};
        EventRecipientFilter filter{};
    };

    void NotifyObservations(std::string_view eventName, kb::scene::SceneEntity target);
    [[nodiscard]] EventSubscriptionHandle SubscribeInternal(
        std::string eventName,
        NativeEventSubscriptionCallback callback,
        kb::scene::SceneEntity owner,
        std::string channel,
        bool behaviourBridge);

    std::vector<Subscription> subscriptions_;
    std::vector<DeferredEmit> deferredEmits_;
    std::unordered_map<std::string, ObservationRecipients, ObservationNameHasher, std::equal_to<>> observations_;
    EventSubscriptionHandle nextHandle_ = 1;
    ScriptEventBusTelemetrySnapshot telemetry_{};
    std::size_t emitDepth_ = 0;
};

} // namespace kb::script
