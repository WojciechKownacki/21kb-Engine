// The execution budget on LuaJIT: a watchdog thread. Compiled code has no instruction a counter could see, so the budget
// is a limit on the processor time of the thread that runs the entries. LuaJIT is built with its hook check in compiled
// loops (LUAJIT_ENABLE_CHECKHOOK): once the watchdog has interrupted the entry the way LuaJIT documents for a Ctrl-C
// handler (lua_sethook with a count of 1, called while the thread is stopped or from a signal handler on the thread
// itself), compiled code leaves for the interpreter and the hook raises the error at the next instruction.
#include "script/lua/LuaVm.hpp"
#include "script/lua/PucLuaRunaway.hpp"

#include "script/lua/PucLuaWatchdogCore.hpp"

// The VM's own record of what the thread is running (interpreter, compiled code, a C function) is not in its public API.
// The header is C that uses compiler extensions the strict first-party warnings reject.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
extern "C" {
#include <lj_obj.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "engine/script/PucLuaScriptRuntime.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <type_traits>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <ctime>
#include <pthread.h>
#endif

namespace kb::script {
namespace {

#if defined(_WIN32)
// The runtime thread is stopped for the check and the interruption, so the entry cannot end in between. Processor time is
// counted in cycles: an instruction of the budget is worth this many (a typical VM instruction takes a few cycles, a call
// into the engine tens).
struct WatchdogPlatform {
    using Thread = HANDLE;
    static constexpr std::uint64_t kTicksPerUnit = 30U;
    static Thread Capture() noexcept {
        HANDLE duplicate = nullptr;
        if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS) == 0) return nullptr;
        return duplicate;
    }
    // The address of the thread's environment block names the thread without a call.
    static std::uint64_t CurrentId() noexcept { return reinterpret_cast<std::uintptr_t>(NtCurrentTeb()); }
    static void Release(Thread thread) noexcept { if (thread != nullptr) CloseHandle(thread); }
    static bool ThreadTicks(Thread thread, std::uint64_t& ticks) noexcept {
        ULONG64 cycles = 0U;
        if (QueryThreadCycleTime(thread, &cycles) == 0) return false;
        ticks = cycles;
        return true;
    }
    static void Interrupt(Thread thread, PucLuaWatchdogCore<WatchdogPlatform>& core, std::uint64_t sequence) noexcept {
        if (SuspendThread(thread) == static_cast<DWORD>(-1)) return;
        static_cast<void>(core.Raise(sequence));
        ResumeThread(thread);
    }
};
using WatchdogCore = PucLuaWatchdogCore<WatchdogPlatform>;
#else
// A pthread_t is a number on Linux and Android and a pointer elsewhere.
template <class Handle>
std::uint64_t ThreadNumber(Handle handle) noexcept {
    if constexpr (std::is_pointer_v<Handle>) return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
    else return static_cast<std::uint64_t>(handle);
}

// Processor time of a thread is its CPU clock (nanoseconds); an instruction of the budget is worth this many. The thread
// is interrupted with a real-time signal whose handler runs on it and asks the VM to stop the entry.
struct WatchdogPlatform {
    using Thread = pthread_t;
    static constexpr std::uint64_t kTicksPerUnit = 10U;
    static Thread Capture() noexcept { return pthread_self(); }
    static std::uint64_t CurrentId() noexcept { return ThreadNumber(pthread_self()); }
    static void Release(Thread) noexcept {}
    static bool ThreadTicks(Thread thread, std::uint64_t& ticks) noexcept {
        clockid_t clock = 0;
        timespec now{};
        if (pthread_getcpuclockid(thread, &clock) != 0 || clock_gettime(clock, &now) != 0) return false;
        ticks = static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ULL + static_cast<std::uint64_t>(now.tv_nsec);
        return true;
    }
    static void Interrupt(Thread thread, PucLuaWatchdogCore<WatchdogPlatform>& core, std::uint64_t sequence) noexcept {
        core.SetPending(sequence);
        static_cast<void>(pthread_kill(thread, SIGRTMIN + 2));
    }
    static void InstallHandler() noexcept;
};
using WatchdogCore = PucLuaWatchdogCore<WatchdogPlatform>;

// The core of the entry the calling thread is running; the signal handler reads it.
thread_local WatchdogCore* interruptTarget = nullptr;

void InterruptHandler(int /*signal*/) {
    WatchdogCore* core = interruptTarget;
    if (core != nullptr) static_cast<void>(core->RaisePending());
}

void WatchdogPlatform::InstallHandler() noexcept {
    static const bool installed = [] {
        struct sigaction action{};
        action.sa_handler = &InterruptHandler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        return sigaction(SIGRTMIN + 2, &action, nullptr) == 0;
    }();
    static_cast<void>(installed);
}
#endif

} // namespace

struct PucLuaRunaway::Impl {
    lua_State* state = nullptr;
    PucLuaScriptRuntime* runtime = nullptr;
    WatchdogCore core;
    std::atomic<bool> stop{ false };
    std::thread thread;

    ~Impl() {
        stop.store(true);
        if (thread.joinable()) thread.join();
    }

    // The watchdog ticks every millisecond while entries run and backs off when the runtime thread has been idle for a
    // while (a phone does not wake a thread a thousand times a second for nothing).
    void Run() {
        std::uint32_t idleTicks = 0U;
        while (!stop.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(idleTicks > 64U ? std::chrono::milliseconds(8) : std::chrono::milliseconds(1));
            static_cast<void>(core.Poll());
            idleTicks = core.Busy() ? 0U : idleTicks + 1U;
        }
    }
};

namespace {

// The hook raises the error at the first instruction after the interruption, and at every later one while the entry
// runs, so a script cannot catch the error and carry on.
void WatchdogHook(lua_State* state, lua_Debug* record) {
    auto* impl = static_cast<PucLuaRunaway::Impl*>(LuaStateSlot(state)->runaway);
    if (impl == nullptr || !impl->core.Interrupted()) { // an interruption of an entry that has ended
        lua_sethook(state, nullptr, 0, 0);
        return;
    }
    // The Suspend policy ends the call's turn instead: the hook suspends the entry (LuaJIT lets a hook yield), and the next
    // call of that entry goes on where it stopped.
    if (impl->runtime->ExecutionBudgetPolicy() == kb::core::BudgetExceededPolicy::Suspend && lua_isyieldable(state) != 0) {
        lua_sethook(state, nullptr, 0, 0);
        static_cast<void>(lua_yield(state, 0));
        return;
    }
    // A hook is not a frame, so the position luaL_error would add is the caller's; the record names the line that ran.
    if (lua_getinfo(state, "Sl", record) != 0 && record->currentline > 0) {
        lua_pushfstring(state, "%s:%d: lua execution budget exceeded", record->short_src, record->currentline);
        lua_error(state);
    }
    luaL_error(state, "lua execution budget exceeded");
}

// A sample read from the watchdog thread: the field is one word the VM stores on every switch between Lua and C.
bool InHostFunction(void* data) noexcept {
    const auto* impl = static_cast<const PucLuaRunaway::Impl*>(data);
    return G(impl->state)->vmstate == ~LJ_VMST_C;
}

void RaiseInterruption(void* data) noexcept {
    auto* impl = static_cast<PucLuaRunaway::Impl*>(data);
    lua_sethook(impl->state, &WatchdogHook, LUA_MASKCOUNT, 1);
}

} // namespace

PucLuaRunaway::PucLuaRunaway(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

PucLuaRunaway::~PucLuaRunaway() {
    if (impl_ != nullptr) LuaStateSlot(impl_->state)->runaway = nullptr;
}

std::unique_ptr<PucLuaRunaway> PucLuaRunaway::Create(lua_State* state, PucLuaScriptRuntime& runtime) {
    auto impl = std::make_unique<Impl>();
    impl->state = state;
    impl->runtime = &runtime;
    impl->core.SetRaise(&RaiseInterruption, impl.get());
    impl->core.SetHostProbe(&InHostFunction, impl.get());
    LuaStateSlot(state)->runaway = impl.get();
    return std::unique_ptr<PucLuaRunaway>(new PucLuaRunaway(std::move(impl)));
}

void PucLuaRunaway::Arm(std::size_t limit) noexcept {
    Impl& impl = *impl_;
    if (limit != 0U && !impl.thread.joinable()) {
#if !defined(_WIN32)
        WatchdogPlatform::InstallHandler();
#endif
        impl.thread = std::thread{ [&impl] { impl.Run(); } };
    }
#if !defined(_WIN32)
    interruptTarget = &impl.core;
#endif
    impl.core.Begin(limit);
}

void PucLuaRunaway::Disarm() noexcept {
    Impl& impl = *impl_;
    if (impl.core.End()) { // the interruption came too late to stop anything
        lua_sethook(impl.state, nullptr, 0, 0);
    }
}

void PucLuaRunaway::Rearm(lua_State* state) noexcept {
    const PucLuaOwnerSlot* slot = LuaStateSlot(state);
    auto* impl = slot != nullptr ? static_cast<Impl*>(slot->runaway) : nullptr;
    if (impl != nullptr && impl->core.Interrupted()) lua_sethook(state, &WatchdogHook, LUA_MASKCOUNT, 1);
}

void PucLuaRunaway::OnCountHook(lua_State* state, PucLuaScriptRuntime& /*runtime*/, lua_Hook /*hook*/) {
    // Only the watchdog sets a count hook on LuaJIT, with its own hook function.
    lua_sethook(state, nullptr, 0, 0);
}

} // namespace kb::script
