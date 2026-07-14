#include "engine/script/ScriptEventBus.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace kb::script {
namespace {

// Same lazy "check on next use, not eagerly on death" policy as
// SceneTimerService/SceneTaskService's OwnerGone (LIB-095/097/099) — no
// push-based "entity died" notification exists anywhere in the engine, so
// polling at delivery time is the only honest option. Duplicated here
// rather than shared because kb::script must not depend on kb::scene's
// private service headers (those are internal to SceneEntityDestructionService
// et al.), only Scene's public Entities() facade.
[[nodiscard]] bool OwnerGone(kb::scene::Scene& scene, kb::scene::SceneEntity owner) {
    return owner.IsValid() && (!scene.Entities().IsAlive(owner) || !scene.Entities().IsActive(owner));
}

} // namespace

EventSubscriptionHandle ScriptEventBus::Subscribe(std::string eventName, NativeEventSubscriptionCallback callback, kb::scene::SceneEntity owner) {
    if (eventName.empty() || callback == nullptr) {
        return kInvalidEventSubscriptionHandle;
    }
    const EventSubscriptionHandle handle = nextHandle_++;
    subscriptions_.push_back(Subscription{
        .handle = handle,
        .eventName = std::move(eventName),
        .owner = owner,
        .callback = std::move(callback),
    });
    return handle;
}

bool ScriptEventBus::Unsubscribe(EventSubscriptionHandle handle) noexcept {
    if (handle == kInvalidEventSubscriptionHandle) {
        return false;
    }
    const auto iterator = std::find_if(subscriptions_.begin(), subscriptions_.end(), [handle](const Subscription& subscription) {
        return subscription.handle == handle;
    });
    if (iterator == subscriptions_.end()) {
        return false;
    }
    subscriptions_.erase(iterator);
    return true;
}

ScriptEventDeliveryResult ScriptEventBus::Emit(kb::scene::Scene& scene, const ScriptEvent& event, kb::scene::SceneEntity target) {
    ScriptEventDeliveryResult result{};
    if (event.name.empty()) {
        return result;
    }
    // Snapshot the matching handles before invoking anything: a subscriber
    // callback may itself Subscribe/Unsubscribe (including unsubscribing
    // itself or a sibling scheduled later in this same pass) — mutating
    // subscriptions_ while iterating it directly would be undefined
    // behaviour. This mirrors LIB-102's "capture the fired list before
    // dispatching any handler" fix for same-phase Timer cancellation.
    std::vector<EventSubscriptionHandle> matching;
    for (const Subscription& subscription : subscriptions_) {
        if (subscription.eventName != event.name) {
            continue;
        }
        if (OwnerGone(scene, subscription.owner)) {
            continue;
        }
        if (target.IsValid() && subscription.owner != target) {
            continue;
        }
        matching.push_back(subscription.handle);
    }
    // Lazily drop dead-owner subscriptions discovered above before the next
    // loop looks any of them up.
    subscriptions_.erase(std::remove_if(subscriptions_.begin(), subscriptions_.end(), [&scene](const Subscription& subscription) {
        return OwnerGone(scene, subscription.owner);
    }),
        subscriptions_.end());
    for (const EventSubscriptionHandle handle : matching) {
        // find_if is deliberately re-resolved per iteration rather than
        // caching iterators up front, since erase() (from a callback
        // re-entering Unsubscribe) invalidates vector iterators.
        const auto iterator = std::find_if(subscriptions_.begin(), subscriptions_.end(), [handle](const Subscription& subscription) {
            return subscription.handle == handle;
        });
        if (iterator == subscriptions_.end()) {
            continue;
        }
        const NativeEventSubscriptionCallback callback = iterator->callback;
        try {
            callback(event);
            ++result.delivered;
        } catch (const std::exception& exception) {
            result.errors.push_back(std::string{ "event subscriber for \"" } + event.name + "\" threw an exception: " + exception.what());
        } catch (...) {
            result.errors.push_back(std::string{ "event subscriber for \"" } + event.name + "\" threw a non-standard exception");
        }
    }
    return result;
}

ScriptEventDeliveryResult ScriptEventBus::Broadcast(kb::scene::Scene& scene, const ScriptEvent& event) {
    return Emit(scene, event, kb::scene::SceneEntity{});
}

void ScriptEventBus::EmitDeferred(ScriptEvent event, kb::scene::SceneEntity target) {
    if (event.name.empty()) {
        return;
    }
    deferredEmits_.push_back(DeferredEmit{
        .event = std::move(event),
        .target = target,
    });
}

ScriptEventDeliveryResult ScriptEventBus::DrainDeferred(kb::scene::Scene& scene) {
    const std::vector<DeferredEmit> pending = std::move(deferredEmits_);
    deferredEmits_.clear();
    ScriptEventDeliveryResult result{};
    for (const DeferredEmit& deferred : pending) {
        ScriptEventDeliveryResult perEvent = Emit(scene, deferred.event, deferred.target);
        result.delivered += perEvent.delivered;
        for (std::string& error : perEvent.errors) {
            result.errors.push_back(std::move(error));
        }
    }
    return result;
}

std::size_t ScriptEventBus::SubscriptionCount() const noexcept {
    return subscriptions_.size();
}

} // namespace kb::script
