#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace kb::library {

// LIB-109: "Signal<T> for local observation without a global bus" — the
// NATIVE C++-only counterpart to kb::script::ScriptEventBus (LIB-105).
// Where the bus is ONE global, string-name-keyed, ScriptValue-typed
// registry reachable from Native/Lua, a Signal<Args...> is a compile-time
// TYPED, per-INSTANCE observer list: a class that wants to notify
// listeners simply owns a `Signal<Args...>` member (e.g. `Signal<float>
// onHealthChanged;`), with no name lookup, no ScriptValue boxing, and no
// dependency on a Scene/ScriptRuntime existing at all — useful for
// internal engine wiring between native C++ objects that have nothing to
// do with scripts or entities.
//
// Deliberately NATIVE-ONLY, with no Lua/Visual Graph binding — and unlike
// ScriptEventBus this is not a scope reduction to revisit later: a
// Signal<Args...>'s slot type is `std::function<void(Args...)>` for
// ARBITRARY compile-time C++ types, which has no representation in
// kb::script::ScriptValue's closed runtime type set (LIB-032/041) at all.
// This is the exact same fundamental boundary LIB-105's own Visual Graph
// deferral already documented (ScriptValue has no "callable" alternative),
// applied to a type that was never meant to cross the script boundary in
// the first place — the task's own title says "without a global bus",
// i.e. explicitly NOT the script-facing pub/sub system.
//
// Reentrancy contract mirrors ScriptEventBus::Emit exactly (LIB-102/
// LIB-040's "snapshot before dispatching" discipline): the set of slots
// invoked by one Emit() call is captured BEFORE any slot runs, so a slot
// that Connects a new one mid-Emit never sees that new slot fire within
// the SAME Emit call (it fires starting the NEXT Emit), and a slot that
// Disconnects itself or a sibling mid-Emit is always safe (a
// disconnected-mid-dispatch slot is simply skipped, never invoked twice or
// left dangling).
template <typename... Args>
class Signal final {
public:
    using SlotId = std::uint64_t;
    using SlotFunction = std::function<void(Args...)>;

    static constexpr SlotId kInvalidSlotId = 0U;

    // Returns kInvalidSlotId (never a valid id) if `slot` is empty. Ids are
    // monotonically increasing and never reused within a Signal's lifetime
    // (the same "an id that can never repeat can never collide with a live
    // one" reasoning kb::scene::TimerHandle/LIB-095 and kb::script::
    // EventSubscriptionHandle/LIB-105 already use), so Disconnect is
    // trivially safe against a stale or already-disconnected id.
    [[nodiscard]] SlotId Connect(SlotFunction slot) {
        if (!slot) {
            return kInvalidSlotId;
        }
        const SlotId id = nextId_++;
        slots_.push_back(Connection{ .id = id, .slot = std::move(slot) });
        return id;
    }

    // Idempotent — false if `id` names no currently connected slot.
    bool Disconnect(SlotId id) noexcept {
        if (id == kInvalidSlotId) {
            return false;
        }
        const auto iterator = std::find_if(slots_.begin(), slots_.end(), [id](const Connection& connection) { return connection.id == id; });
        if (iterator == slots_.end()) {
            return false;
        }
        slots_.erase(iterator);
        return true;
    }

    // Invokes every currently connected slot, in connection order, with a
    // COPY of `args` per slot (each listener needs its own values — moving
    // into one slot would leave the rest with moved-from arguments). See
    // the class doc comment above for the full reentrancy contract.
    void Emit(Args... args) const {
        std::vector<SlotId> matching;
        matching.reserve(slots_.size());
        for (const Connection& connection : slots_) {
            matching.push_back(connection.id);
        }
        for (const SlotId id : matching) {
            const auto iterator = std::find_if(slots_.begin(), slots_.end(), [id](const Connection& connection) { return connection.id == id; });
            if (iterator == slots_.end()) {
                continue;
            }
            // Copy the slot out before invoking it (mirrors ScriptEventBus::
            // Emit's identical precaution) — a slot may itself Connect/
            // Disconnect on THIS SAME Signal, which reallocates `slots_`;
            // invoking directly through `iterator` would then be calling
            // through a dangling reference into freed memory the very
            // instant the callback's own body (if small-buffer-optimized
            // inline in the vector element) is still executing.
            const SlotFunction slot = iterator->slot;
            slot(args...);
        }
    }

    [[nodiscard]] std::size_t SlotCount() const noexcept {
        return slots_.size();
    }

private:
    struct Connection {
        SlotId id = kInvalidSlotId;
        SlotFunction slot;
    };

    std::vector<Connection> slots_;
    SlotId nextId_ = 1;
};

} // namespace kb::library
