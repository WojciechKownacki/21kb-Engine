#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace kb::script {

// The decision logic of the LuaJIT execution budget, independent of the operating system. The thread that runs the
// entries marks them: Begin writes the entry's number and an "inside an entry" flag, End clears the flag (plain stores,
// no lock). The watchdog thread calls Poll once per tick: when the same entry is still running and the processor time
// of the runtime thread has grown past the limit since the watchdog first saw it, the platform interrupts that thread
// (Platform::Interrupt), which makes the VM run its hook as soon as it can. The time counted is the processor time
// of the thread, so a thread the system did not schedule is not charged.
//
// Platform provides: using Thread (a token another thread can use); static Thread Capture() (a token for the calling
// thread), static std::uint64_t CurrentId() (cheap, compared on every entry), static void Release(Thread); static bool ThreadTicks(Thread, std::uint64_t&);
// static void Interrupt(Thread, Core&, std::uint64_t sequence) and static constexpr std::uint64_t kTicksPerUnit (the
// processor ticks one unit of the engine's work budget is worth).
template <class Platform>
class PucLuaWatchdogCore {
public:
    using Thread = typename Platform::Thread;

    PucLuaWatchdogCore() = default;
    PucLuaWatchdogCore(const PucLuaWatchdogCore&) = delete;
    PucLuaWatchdogCore& operator=(const PucLuaWatchdogCore&) = delete;
    ~PucLuaWatchdogCore() {
        if (ownerSet_.load()) Platform::Release(owner_.load());
    }

    // Runtime thread: an entry starts, with `units` of work allowed (zero: unlimited).
    void Begin(std::uint64_t units) noexcept {
        const std::uint64_t id = Platform::CurrentId();
        if (!ownerSet_.load(std::memory_order_relaxed) || id != ownerId_) {
            ownerId_ = id;
            const Thread captured = Platform::Capture();
            const bool hadOwner = ownerSet_.load(std::memory_order_relaxed);
            const Thread previous = owner_.exchange(captured);
            if (hadOwner) Platform::Release(previous);
            ownerSet_.store(true, std::memory_order_release);
        }
        limitTicks_.store(units * Platform::kTicksPerUnit, std::memory_order_relaxed);
        sequence_.store(++currentSequence_, std::memory_order_relaxed);
        inEntry_.store(true, std::memory_order_release);
    }

    // Runtime thread: the entry ended. True when an interruption was raised for it (the caller removes the hook).
    [[nodiscard]] bool End() noexcept {
        inEntry_.store(false, std::memory_order_release);
        if (interruptedSequence_.load(std::memory_order_relaxed) != currentSequence_) return false;
        interruptedSequence_.store(0U, std::memory_order_relaxed);
        return true;
    }

    // Runtime thread (the VM hook): the running entry was interrupted.
    [[nodiscard]] bool Interrupted() const noexcept {
        return inEntry_.load(std::memory_order_relaxed) && interruptedSequence_.load(std::memory_order_relaxed) == currentSequence_;
    }

    // What interrupting the entry does: the VM call that makes the runtime thread run its hook.
    void SetRaise(void (*raise)(void*) noexcept, void* data) noexcept {
        raise_ = raise;
        raiseData_ = data;
    }

    // Tells whether the runtime thread is inside a function of the host right now. The time of a tick that ends there
    // is the host's work (a level load, an offline audio render), not the script's, and is not charged to the entry.
    void SetHostProbe(bool (*probe)(void*) noexcept, void* data) noexcept {
        hostProbe_ = probe;
        hostProbeData_ = data;
    }

    // Called by the platform while the runtime thread cannot run the entry on (stopped, or about to run a signal
    // handler): when `sequence` is still the running entry, it is recorded as interrupted and the VM is asked to stop it.
    bool Raise(std::uint64_t sequence) noexcept {
        if (!inEntry_.load(std::memory_order_acquire) || sequence_.load(std::memory_order_relaxed) != sequence) return false;
        interruptedSequence_.store(sequence, std::memory_order_relaxed);
        raise_(raiseData_);
        return true;
    }

    // For a platform that interrupts through a signal: the entry to stop is named before the signal is sent, and the
    // handler, running on the runtime thread, raises for it.
    void SetPending(std::uint64_t sequence) noexcept {
        pending_.store(sequence, std::memory_order_relaxed);
    }
    bool RaisePending() noexcept {
        return Raise(pending_.load(std::memory_order_relaxed));
    }

    // Watchdog thread: one tick. True when it interrupted the runtime thread.
    bool Poll() {
        const std::uint64_t limit = limitTicks_.load(std::memory_order_relaxed);
        if (limit == 0U || !ownerSet_.load(std::memory_order_acquire) || !inEntry_.load(std::memory_order_acquire)) {
            watchedSequence_ = 0U;
            return false;
        }
        const Thread owner = owner_.load(std::memory_order_relaxed);
        const std::uint64_t current = sequence_.load(std::memory_order_relaxed);
        std::uint64_t ticks = 0U;
        if (!Platform::ThreadTicks(owner, ticks)) return false;
        if (current != watchedSequence_) { // a new entry: its time is counted from here
            watchedSequence_ = current;
            lastTicks_ = ticks;
            used_ = 0U;
            return false;
        }
        if (hostProbe_ == nullptr || !hostProbe_(hostProbeData_)) used_ += ticks - lastTicks_;
        lastTicks_ = ticks;
        if (used_ < limit) return false;
        used_ = 0U;
        Platform::Interrupt(owner, *this, current);
        return true;
    }

    // True while the runtime thread is inside an entry (the watchdog polls at a shorter interval then).
    [[nodiscard]] bool Busy() const noexcept {
        return inEntry_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint64_t> sequence_{ 0U };
    std::atomic<bool> inEntry_{ false };
    std::atomic<std::uint64_t> interruptedSequence_{ 0U };
    std::atomic<std::uint64_t> limitTicks_{ 0U };
    std::atomic<std::uint64_t> pending_{ 0U };
    void (*raise_)(void*) noexcept = nullptr;
    void* raiseData_ = nullptr;
    bool (*hostProbe_)(void*) noexcept = nullptr;
    void* hostProbeData_ = nullptr;
    std::atomic<Thread> owner_{};
    std::atomic<bool> ownerSet_{ false };
    std::uint64_t currentSequence_ = 0U; // runtime thread only
    std::uint64_t ownerId_ = 0U;
    std::uint64_t watchedSequence_ = 0U; // watchdog thread only
    std::uint64_t lastTicks_ = 0U;
    std::uint64_t used_ = 0U;
};

} // namespace kb::script
