#include "engine/script/ScriptEventBus.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneLoadedContent.hpp"
#include "engine/scene/SceneTagsComponents.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"

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

// LIB-106: identical tag-membership parsing rules as ScriptWorldApi.cpp's
// ParseTags/HasTagValue (comma-or-semicolon-separated, trimmed) — World.
// SetTag is the only writer of this format, so a subscriber's tag filter
// must read it the exact same way. Duplicated locally rather than shared
// via a new header for a ~10-line helper, the same proportionality already
// applied to OwnerGone above.
[[nodiscard]] bool HasTagValue(std::string_view tags, std::string_view tag) noexcept {
    std::size_t tokenBegin = 0U;
    while (tokenBegin <= tags.size()) {
        const std::size_t tokenEnd = tags.find_first_of(",;", tokenBegin);
        std::string_view token = tags.substr(tokenBegin, tokenEnd == std::string_view::npos ? std::string_view::npos : tokenEnd - tokenBegin);
        const std::size_t begin = token.find_first_not_of(" \t\r\n");
        if (begin != std::string_view::npos) {
            const std::size_t end = token.find_last_not_of(" \t\r\n");
            if (token.substr(begin, end - begin + 1U) == tag) {
                return true;
            }
        }
        if (tokenEnd == std::string_view::npos) {
            break;
        }
        tokenBegin = tokenEnd + 1U;
    }
    return false;
}

// LIB-106: whether a subscription with the given `owner`/`channel` satisfies
// every non-default axis of `filter` — tag/component/scene are evaluated
// against the OWNER ENTITY (a subscription with no owner, or a filter axis
// requiring one, never matches — the same "no entity identity to match
// against" rule Emit's entity `target` already applies), `channel` against
// the subscription's OWN declared channel (see EventRecipientFilter's doc
// comment). Called only in the up-front snapshot pass (matching Emit's
// existing tag/component/scene-agnostic scope before this task) — NOT
// re-checked per invocation the way LIB-040's OwnerGone recheck is, since
// tag/component mutating mid-dispatch is a much lower-stakes race than an
// entity dying mid-dispatch and re-checking it would be new scope beyond
// this task's ask.
[[nodiscard]] bool MatchesFilter(kb::scene::Scene& scene, kb::scene::SceneEntity owner, const std::string& channel, const EventRecipientFilter& filter) {
    if (!filter.channel.empty() && channel != filter.channel) {
        return false;
    }
    const bool needsOwner = !filter.tag.empty() || !filter.component.empty() || filter.sceneId != 0U;
    if (!needsOwner) {
        return true;
    }
    if (!owner.IsValid()) {
        return false;
    }
    if (!filter.tag.empty()) {
        const kb::scene::TagsComponent* tags = scene.Components().Tags().TryGet(owner);
        if (tags == nullptr || !HasTagValue(kb::scene::TagsText(*tags), filter.tag)) {
            return false;
        }
    }
    if (!filter.component.empty() && !ScriptSceneComponentApi::HasComponent(scene, owner, filter.component)) {
        return false;
    }
    if (filter.sceneId != 0U && scene.LoadedContent().OwningScene(owner) != filter.sceneId) {
        return false;
    }
    return true;
}

} // namespace

EventSubscriptionHandle ScriptEventBus::Subscribe(std::string eventName, NativeEventSubscriptionCallback callback, kb::scene::SceneEntity owner, std::string channel) {
    if (eventName.empty() || callback == nullptr) {
        return kInvalidEventSubscriptionHandle;
    }
    const EventSubscriptionHandle handle = nextHandle_++;
    subscriptions_.push_back(Subscription{
        .handle = handle,
        .eventName = std::move(eventName),
        .owner = owner,
        .channel = std::move(channel),
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

ScriptEventDeliveryResult ScriptEventBus::Emit(kb::scene::Scene& scene, const ScriptEvent& event, kb::scene::SceneEntity target, const EventRecipientFilter& filter) {
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
        if (!MatchesFilter(scene, subscription.owner, subscription.channel, filter)) {
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
        // LIB-040: re-check owner liveness right here, not just once in the
        // snapshot pass above — an EARLIER subscriber in this SAME Emit call
        // may have destroyed or deactivated a LATER subscriber's owner
        // (World.Destroy is synchronous, ScriptEventBus.hpp's class comment)
        // before this iteration runs. Without this recheck a subscriber
        // whose owner just died in this exact dispatch batch would still
        // fire once more — the same class of staleness LIB-102 already
        // fixed for same-phase Timer cancellation, here for reentrant
        // destroy instead of reentrant Cancel.
        if (OwnerGone(scene, iterator->owner)) {
            subscriptions_.erase(iterator);
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

ScriptEventDeliveryResult ScriptEventBus::Broadcast(kb::scene::Scene& scene, const ScriptEvent& event, const EventRecipientFilter& filter) {
    return Emit(scene, event, kb::scene::SceneEntity{}, filter);
}

void ScriptEventBus::EmitDeferred(ScriptEvent event, kb::scene::SceneEntity target, EventRecipientFilter filter) {
    if (event.name.empty()) {
        return;
    }
    deferredEmits_.push_back(DeferredEmit{
        .event = std::move(event),
        .target = target,
        .filter = std::move(filter),
    });
}

ScriptEventDeliveryResult ScriptEventBus::DrainDeferred(kb::scene::Scene& scene) {
    const std::vector<DeferredEmit> pending = std::move(deferredEmits_);
    deferredEmits_.clear();
    ScriptEventDeliveryResult result{};
    for (const DeferredEmit& deferred : pending) {
        ScriptEventDeliveryResult perEvent = Emit(scene, deferred.event, deferred.target, deferred.filter);
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
