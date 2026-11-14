#include "TestSupport.hpp"

#include "script/lua/PucLuaMemoryArena.hpp"
#include "script/lua/PucLuaWatchdogCore.hpp"

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/script/LuaDebugSession.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptLanguageSubset.hpp"
#include "engine/script/ScriptEvent.hpp"
#include "engine/script/ScriptRuntime.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptRuntimeSceneSystem.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <cstring>
#include <string_view>
#include <vector>

namespace kb::tests {

void RunScriptVmTests();

namespace {

constexpr kb::assets::AssetId kVmAsset{ 9702U };
constexpr float kVmStep = 1.0F / 60.0F;

// A scene whose Lua behaviours run on the script VM of the build, through the host's own runtime.
struct VmRig {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::script::ScriptRuntimeSceneSystem system{ host.Runtime() };

    explicit VmRig(const char* source) {
        Require(host.Succeeded(), "script VM test host did not initialize");
        const kb::script::PucLuaLoadResult loaded = host.LuaRuntime().LoadScript(kVmAsset, source, "Test.lua");
        Require(loaded.succeeded, loaded.error.c_str());
    }

    kb::scene::SceneEntity Add() {
        const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Scripted" });
        scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
            .behaviourAssetId = kVmAsset.value, .backend = kb::scene::BehaviourBackend::Lua, .enabled = true });
        return object.Entity();
    }

    const kb::script::ScriptRuntimeExecutionResult& Frame() {
        return system.ExecuteFrame(scene, kVmStep);
    }

    const kb::script::ScriptRuntimeExecutionResult& CleanFrame() {
        const kb::script::ScriptRuntimeExecutionResult& result = Frame();
        Require(result.Succeeded(), result.diagnostics.empty() ? "script VM test frame failed" : result.diagnostics.front().message.c_str());
        return result;
    }
};

[[nodiscard]] bool Contains(const std::string& text, std::string_view part) {
    return text.find(part) != std::string::npos;
}

[[nodiscard]] std::string TextOf(const kb::script::ScriptEvent& event, std::string_view name) {
    for (const kb::script::ScriptEventArgument& argument : event.arguments) {
        if (argument.name == name) return argument.value.AsString();
    }
    return {};
}

[[nodiscard]] std::vector<std::string> TextsOf(const kb::script::ScriptRuntimeExecutionResult& result, std::string_view eventName, std::string_view argument) {
    std::vector<std::string> texts;
    for (const kb::script::ScriptEvent& event : result.emittedEvents) {
        if (event.name == eventName) texts.push_back(TextOf(event, argument));
    }
    return texts;
}

// The `bit` library gives the same numbers on both VMs: 32-bit signed results, counts taken modulo 32, operands rounded
// and wrapped the same way.
void RunBitLibraryTest() {
    VmRig rig(R"(
function Tick(self, dt)
  local r = {}
  r[#r + 1] = bit.band(0xFF00, 0x0FF0)
  r[#r + 1] = bit.bor(1, 2, 8)
  r[#r + 1] = bit.bxor(5, 3)
  r[#r + 1] = bit.bnot(0)
  r[#r + 1] = bit.lshift(1, 31)
  r[#r + 1] = bit.lshift(1, 32)
  r[#r + 1] = bit.rshift(-1, 28)
  r[#r + 1] = bit.arshift(-256, 4)
  r[#r + 1] = bit.rol(0x80000001, 1)
  r[#r + 1] = bit.ror(1, 1)
  r[#r + 1] = bit.bswap(0x01020304)
  r[#r + 1] = bit.tobit(2 ^ 32 + 5)
  r[#r + 1] = bit.tobit(0xFFFFFFFF)
  r[#r + 1] = bit.tobit(1.5)
  r[#r + 1] = bit.band(2 ^ 31, 1)
  r[#r + 1] = bit.tohex(255)
  r[#r + 1] = bit.tohex(-1, 4)
  r[#r + 1] = bit.tohex(255, -4)
  local joined = {}
  for i = 1, #r do joined[i] = tostring(r[i]) end
  Emit('Bits', { text = table.concat(joined, ',') })
end
)");
    static_cast<void>(rig.Add());
    const std::vector<std::string> texts = TextsOf(rig.CleanFrame(), "Bits", "text");
    Require(texts.size() == 1U && texts.front() == "3840,11,6,-1,-2147483648,1,15,-16,3,-2147483648,67305985,5,-1,2,0,000000ff,ffff,00FF",
        "The bit library must give the numbers of the 32-bit semantics both VMs share");
}

// What a script written for both VMs may rely on: `unpack`, the string and table library, and 53-bit integer arithmetic.
void RunSharedSubsetTest() {
    VmRig rig(R"(
function Tick(self, dt)
  local a, b, c = unpack({ 10, 20, 30 })
  local parts = {}
  for word in string.gmatch("one two three", "%a+") do parts[#parts + 1] = word end
  local big = 2 ^ 52 + 1
  Emit('Subset', { a = a, c = c, words = table.concat(parts, "-"), big = string.format("%d", big), mod = (-7) % 3, floor = math.floor(7 / 2) })
end
)");
    static_cast<void>(rig.Add());
    const kb::script::ScriptRuntimeExecutionResult& result = rig.CleanFrame();
    Require(result.emittedEvents.size() == 1U, "the subset script must run");
    const kb::script::ScriptEvent& event = result.emittedEvents.front();
    Require(TextOf(event, "words") == "one-two-three" && TextOf(event, "big") == "4503599627370497", "The shared string and number functions must agree");
    Require(event.arguments.size() == 6U, "the subset script must report all its values");
}

// A call the fast engine paths do not cover is the C API call: its errors read the same, at the script's line.
void RunHotCallErrorsTest() {
    VmRig rig(R"(
local function attempt(f, ...)
  local ok, err = pcall(f, ...)
  return ok and "ok" or err
end
function Tick(self, dt)
  local r = {}
  r[#r + 1] = attempt(function() return (Transform.GetPositionXYZ("abc")) end)
  r[#r + 1] = attempt(function() return (Transform.GetPositionXYZ(1.5)) end)
  r[#r + 1] = attempt(function() return (Transform.SetPosition(self.entity, "x", 0, 0)) end)
  r[#r + 1] = attempt(function() return (Input.IsPressed(nil)) end)
  r[#r + 1] = attempt(function() return (Input.Value("jump", 1.5)) end)
  r[#r + 1] = attempt(function() return (Input.Vector2XY({})) end)
  r[#r + 1] = tostring(Transform.GetPositionXYZ("1"))
  Emit('Errors', { a = r[1], b = r[2], c = r[3], d = r[4], e = r[5], f = r[6], g = r[7] })
end
)");
    static_cast<void>(rig.Add());
    const kb::script::ScriptRuntimeExecutionResult& result = rig.CleanFrame();
    Require(result.emittedEvents.size() == 1U, "the error script must run");
    const kb::script::ScriptEvent& event = result.emittedEvents.front();
    const std::string actual = TextOf(event, "a") + " | " + TextOf(event, "b") + " | " + TextOf(event, "c") + " | " + TextOf(event, "d") + " | " + TextOf(event, "e") + " | " + TextOf(event, "f") + " | " + TextOf(event, "g");
    Require(Contains(TextOf(event, "a"), "Test.lua:8:") && Contains(TextOf(event, "a"), "bad argument #1 (integer expected, got string)"), ("a wrong entity must be reported like the C API reports it: " + actual).c_str());
    Require(Contains(TextOf(event, "b"), "Test.lua:9:") && Contains(TextOf(event, "b"), "number has no integer representation"), "a fractional entity must be reported like the C API reports it");
    Require(Contains(TextOf(event, "c"), "Test.lua:10:") && Contains(TextOf(event, "c"), "bad argument"), "a wrong coordinate must be reported like the C API reports it");
    Require(Contains(TextOf(event, "d"), "Test.lua:11:") && Contains(TextOf(event, "d"), "bad argument #1 (string expected"), "a missing action must be reported like the C API reports it");
    Require(Contains(TextOf(event, "e"), "Test.lua:12:") && Contains(TextOf(event, "e"), "number has no integer representation"), "a fractional player must be reported like the C API reports it");
    Require(Contains(TextOf(event, "f"), "Test.lua:13:") && Contains(TextOf(event, "f"), "bad argument #1 (string expected"), "a table as the action must be reported like the C API reports it");
    Require(TextOf(event, "g") == "nil", "a numeric string is an entity as in the C API, which finds none");
}

// The table forms of the position and of a 2D input read through the fast path give the same values and the same shape as
// the C API functions: x, y, z (no w), x and y only, nil for an entity that is gone.
void RunHotTableFormsTest() {
    VmRig rig(R"(
function Tick(self, dt)
  local p = Transform.GetPosition(self.entity)
  local x, y, z = Transform.GetPositionXYZ(self.entity)
  local v = Input.Vector2("nothing")
  local mx, my = Input.Vector2XY("nothing")
  local gone = Transform.GetPosition(4000000)
  Emit('Forms', {
    position = (p.x == x and p.y == y and p.z == z) and 'same' or 'different',
    input = (v.x == mx and v.y == my) and 'same' or 'different',
    gone = gone == nil and 'nil' or 'value',
    shape = (p.w == nil and v.z == nil) and 'plain' or 'extra' })
end
)");
    const kb::scene::SceneEntity entity = rig.Add();
    kb::scene::TransformComponent transform = rig.scene.Transforms().Get(entity);
    transform.localPosition = kb::scene::Vec3{ 1.5F, -2.0F, 4.25F };
    rig.scene.Transforms().Set(entity, transform);
    const kb::script::ScriptRuntimeExecutionResult& result = rig.CleanFrame();
    Require(result.emittedEvents.size() == 1U, "the table forms script must run");
    const kb::script::ScriptEvent& event = result.emittedEvents.front();
    Require(TextOf(event, "position") == "same" && TextOf(event, "input") == "same" && TextOf(event, "gone") == "nil" && TextOf(event, "shape") == "plain",
        "The table forms of the position and of 2D input must match the other forms");
}

// A script that never returns is stopped by the budget, also when it catches the error, and the next entry runs.
void RunBudgetTest() {
    VmRig rig("function Tick(self, dt)\n  if self.entity % 2 == 0 then\n    pcall(function() while true do end end)\n    while true do end\n  end\n  Emit('Ran')\nend\n");
    kb::script::ScriptExecutionBudgetSettings settings;
    settings.luaInstructionsPerBehaviour = 200'000U;
    rig.host.LuaRuntime().SetExecutionBudgetSettings(settings);
    for (int index = 0; index < 4; ++index) static_cast<void>(rig.Add());
    for (int frame = 0; frame < 20; ++frame) {
        const kb::script::ScriptRuntimeExecutionResult& result = rig.Frame();
        Require(!result.diagnostics.empty() && Contains(result.diagnostics.front().message, "budget"), "A loop that never ends must be stopped by the execution budget");
        Require(!result.emittedEvents.empty(), "The objects that stay within the budget must still run");
        const std::string& trace = result.diagnostics.front().stackTrace;
        Require(Contains(trace, "Test.lua:3:") || Contains(trace, "Test.lua:4:"), ("The trace of a stopped loop must name its line: " + trace).c_str());
    }
    const kb::script::PucLuaLoadResult load = rig.host.LuaRuntime().LoadScript(kb::assets::AssetId{ 9703U }, "while true do end\n", "Spin.lua");
    Require(!load.succeeded && Contains(load.error, "budget"), "Code at the top of a script runs under the budget too");
    settings.luaInstructionsPerBehaviour = 0U;
    rig.host.LuaRuntime().SetExecutionBudgetSettings(settings);
    Require(!rig.host.LuaRuntime().IsExecutionBudgetEnabled(), "A zero budget must turn the limit off");
}

// The editor warns about what only one of the two script VMs has: the operators, the attributes, the wide literals and the
// library functions, with their lines, and leaves strings, comments, fields and the shared forms alone.
void RunLanguageSubsetTest() {
    const std::vector<kb::script::ScriptSubsetFinding> findings = kb::script::FindOutOfSubsetConstructs(
        "local a = 7 // 2\n"
        "local b = x & 1\n"
        "local c <const> = 5\n"
        "local d = math.type(a)\n"
        "local e = table.unpack({})\n"
        "-- a // b & c\n"
        "local s = 'a // b'\n"
        "local t = [[ x | y ]]\n"
        "local u = 5 ~= 6\n"
        "local big = 9007199254740993\n"
        "local ok = bit.band(a, 1) .. unpack({ 1 })\n"
        "local g = a.math.type\n"
        "local sh = 1 << 2\n"
        "--[==[ long\n comment & | ]==]\n"
        "local last = ~a\n");
    std::vector<std::uint32_t> lines;
    for (const kb::script::ScriptSubsetFinding& finding : findings) lines.push_back(finding.line);
    Require(lines == std::vector<std::uint32_t>({ 1U, 2U, 3U, 4U, 5U, 10U, 13U, 16U }), "The subset check must name each construct of one VM only, by line");
    Require(!findings.empty() && Contains(findings.front().message, "math.floor"), "The subset check must say what to write instead");
    Require(kb::script::FindOutOfSubsetConstructs("local x = bit.bor(1, 2) + 0xFFFFFFFF + 2^53\nfor i = 1, 3 do goto continue ::continue:: end\n").empty(),
        "The shared forms must pass the subset check");
}

// The debugger workflow of a script with a local declared on the line before the breakpoint, through the protocol the editor and
// kb_cli use: pause at the line, read the stack and the locals, evaluate with the new local, step into, over and out, continue.
void RunDebuggerWorkflowTest() {
    VmRig rig(
        "local function Add(value)\n local increment = 2\n return value + increment\nend\n"
        "function Tick(self, dt)\n local counter = 40\n local result = Add(counter)\n if result ~= 42 then error('debug execution changed') end\nend\n");
    kb::script::PucLuaScriptRuntime& lua = rig.host.LuaRuntime();
    kb::script::LuaDebugSession session(lua);
    const auto run = [&](const std::string& body) { return session.Execute("{\"schema\":\"21kb.lua-debug/v1\",\"command\":" + body + "}"); };
    Require(run("\"connect\"").Find("succeeded")->AsBool(), "the debug session did not connect");
    kb::script::PucLuaDebugSettings settings;
    settings.breakpoints.push_back(kb::script::PucLuaDebugBreakpoint{ .chunkName = "Test.lua", .line = 7, .enabled = true });
    lua.SetDebugSettings(settings);
    static_cast<void>(rig.Add());
    const auto pausedAt = [&](int line) { return lua.IsDebugPaused() && lua.LastDebugPause().line == line; };
    static_cast<void>(rig.CleanFrame());
    Require(pausedAt(7), "the breakpoint must pause the script at its line");
    const kb::script::PucLuaDebugPauseSnapshot paused = lua.InspectDebugPause();
    bool counter = false;
    for (const kb::script::PucLuaDebugVariableSnapshot& local : paused.callStack.front().locals) counter = counter || (local.name == "counter" && local.value == "40");
    Require(counter, "the locals of the paused frame must include the one declared on the line before");
    const auto evaluated = run("\"evaluate\",\"expression\":\"counter + 2\"");
    Require(evaluated.Find("succeeded")->AsBool() && evaluated.Find("result")->Find("value")->AsString() == "42", "an expression with the local declared on the line before must evaluate");
    Require(!run("\"evaluate\",\"expression\":\"counter +\"").Find("succeeded")->AsBool(), "an invalid expression must be refused");
    Require(run("\"step_into\"").Find("succeeded")->AsBool(), "step_into failed");
    static_cast<void>(rig.CleanFrame());
    Require(pausedAt(2) && lua.InspectDebugPause().callStack.size() >= 2U, "step into must stop inside the called function");
    const auto caller = run("\"evaluate\",\"frame\":1,\"expression\":\"counter\"");
    Require(caller.Find("succeeded")->AsBool() && caller.Find("result")->Find("value")->AsString() == "40", "the frame of the caller must keep its locals");
    Require(run("\"step_over\"").Find("succeeded")->AsBool(), "step_over failed");
    static_cast<void>(rig.CleanFrame());
    Require(pausedAt(3), "step over must stop on the next line");
    Require(run("\"step_out\"").Find("succeeded")->AsBool(), "step_out failed");
    static_cast<void>(rig.CleanFrame());
    Require(pausedAt(8), "step out must stop in the caller after the call");
    Require(run("\"continue\"").Find("succeeded")->AsBool(), "continue failed");
    static_cast<void>(rig.CleanFrame()); // the suspended call runs to its end
    static_cast<void>(rig.CleanFrame()); // the next call reaches the breakpoint again
    Require(pausedAt(7), "after the script ran to its end the breakpoint must pause the next call");
    lua.SetDebugSettings({});
    lua.ResumeDebugExecution();
    lua.ClearDebugPause();
    static_cast<void>(rig.CleanFrame());
    Require(!lua.IsDebugPaused() && lua.SuspendedCoroutineCount() == 0U, "with the breakpoints gone nothing may stay suspended");
}

// The decision logic of the budget on a fake platform: an entry is charged only for processor time it used itself.
struct FakePlatform {
    using Thread = int;
    static constexpr std::uint64_t kTicksPerUnit = 10U;
    static inline std::uint64_t ticks = 0U;
    static inline int interruptions = 0;
    static Thread Capture() noexcept { return 1; }
    static std::uint64_t CurrentId() noexcept { return 1U; }
    static void Release(Thread) noexcept {}
    static bool ThreadTicks(Thread, std::uint64_t& out) noexcept { out = ticks; return true; }
    static void Interrupt(Thread, kb::script::PucLuaWatchdogCore<FakePlatform>& core, std::uint64_t sequence) noexcept {
        ++interruptions;
        static_cast<void>(core.Raise(sequence));
    }
};

void RunWatchdogCoreTest() {
    kb::script::PucLuaWatchdogCore<FakePlatform> core;
    static int raised = 0;
    core.SetRaise([](void*) noexcept { ++raised; }, nullptr);
    FakePlatform::ticks = 1'000U;
    // No limit: never interrupted.
    core.Begin(0U);
    FakePlatform::ticks += 1'000'000U;
    Require(!core.Poll() && !core.Poll(), "an unlimited entry must never be interrupted");
    static_cast<void>(core.End());
    // A limit of 100 units is 1000 ticks: below it nothing happens, past it the entry is interrupted once per limit.
    core.Begin(100U);
    Require(!core.Poll(), "the first look at an entry only starts its clock");
    FakePlatform::ticks += 400U;
    Require(!core.Poll() && !core.Interrupted(), "an entry under its limit must not be interrupted");
    FakePlatform::ticks += 700U;
    Require(core.Poll() && core.Interrupted() && raised == 1 && FakePlatform::interruptions == 1, "an entry past its limit must be interrupted");
    Require(core.End(), "an interrupted entry must report it when it ends, so its hook can be removed");
    // The next entry starts a new count; time the thread used between entries is not charged to it.
    core.Begin(100U);
    Require(!core.Poll(), "a new entry must start counting from its first look");
    FakePlatform::ticks += 600U;
    Require(!core.Poll() && !core.Interrupted(), "time used before the entry's first look must not be charged to it");
    static_cast<void>(core.End());
    // Time the thread spends inside a function of the host is not the entry's.
    static bool inHost = false;
    core.SetHostProbe([](void*) noexcept { return inHost; }, nullptr);
    core.Begin(100U);
    static_cast<void>(core.Poll());
    inHost = true;
    FakePlatform::ticks += 50'000U;
    Require(!core.Poll() && !core.Interrupted(), "time inside a host function must not be charged to the entry");
    inHost = false;
    FakePlatform::ticks += 1'200U;
    Require(core.Poll() && core.Interrupted(), "the entry's own time is still charged after a host function");
    static_cast<void>(core.End());
    core.SetHostProbe(nullptr, nullptr);
    // An entry that ended before the interruption lands is not interrupted.
    core.Begin(100U);
    static_cast<void>(core.Poll());
    FakePlatform::ticks += 5'000U;
    static_cast<void>(core.End());
    Require(!core.Poll() && !core.Raise(1U), "an interruption for an entry that has ended must do nothing");
}

#if defined(KB_SCRIPT_VM_LUAJIT)

// A script that spins is compiled to machine code after a few passes; the budget must still interrupt it.
void RunCompiledBudgetTest() {
    VmRig rig(R"(
local n = 0
function Tick(self, dt)
  for i = 1, 200 do n = n + 1 end
  while true do n = n + 1 end
end
)");
    kb::script::ScriptExecutionBudgetSettings settings;
    settings.luaInstructionsPerBehaviour = 100'000U;
    rig.host.LuaRuntime().SetExecutionBudgetSettings(settings);
    static_cast<void>(rig.Add());
    for (int frame = 0; frame < 4; ++frame) {
        const kb::script::ScriptRuntimeExecutionResult& result = rig.Frame();
        Require(!result.diagnostics.empty() && Contains(result.diagnostics.front().message, "budget"), "A loop that compiled code runs must be stopped by the execution budget");
    }
}

// Many objects with a few hostile ones: every hostile entry is stopped and none of the others is, frame after frame.
void RunBudgetStressTest() {
    VmRig rig(R"(
function Tick(self, dt)
  if self.entity % 25 == 0 then
    local n = 0
    while true do n = n + 1 end
  end
  local total = 0
  for i = 1, 200 do total = total + math.sqrt(i) end
  Emit('Ran', { total = total })
end
)");
    kb::script::ScriptExecutionBudgetSettings settings;
    settings.luaInstructionsPerBehaviour = 200'000U;
    rig.host.LuaRuntime().SetExecutionBudgetSettings(settings);
    std::size_t hostile = 0U;
    for (int index = 0; index < 100; ++index) {
        if (rig.Add().Id() % 25U == 0U) ++hostile;
    }
    Require(hostile > 0U, "The stress test needs hostile objects");
    for (int frame = 0; frame < 25; ++frame) {
        const kb::script::ScriptRuntimeExecutionResult& result = rig.Frame();
        Require(result.diagnostics.size() == hostile, "Exactly the hostile entries must fail");
        Require(result.emittedEvents.size() == 100U - hostile, "Every other entry must run");
    }
}

[[nodiscard]] double MillisecondsOfFrames(VmRig& rig, int frames) {
    const auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < frames; ++frame) static_cast<void>(rig.CleanFrame());
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// A debugger with a breakpoint puts the VM's line hook on, which keeps scripts in the interpreter; once the breakpoint is
// gone the compiler is back (the same loop runs several times faster again).
void RunDebuggerCompilerTest() {
    VmRig rig(R"(
function Tick(self, dt)
  local total = 0
  for i = 1, 300000 do total = total + (i % 7) * 0.5 end
  self.total = total
end
)");
    static_cast<void>(rig.Add());
    kb::script::ScriptExecutionBudgetSettings settings;
    settings.luaInstructionsPerBehaviour = 0U;
    rig.host.LuaRuntime().SetExecutionBudgetSettings(settings);
    for (int frame = 0; frame < 4; ++frame) static_cast<void>(rig.CleanFrame()); // compiles
    const double compiled = MillisecondsOfFrames(rig, 10);
    kb::script::PucLuaDebugSettings debug;
    debug.breakpoints.push_back(kb::script::PucLuaDebugBreakpoint{ .chunkName = "Other.lua", .line = 3, .enabled = true });
    rig.host.LuaRuntime().SetDebugSettings(debug);
    const double attached = MillisecondsOfFrames(rig, 10);
    rig.host.LuaRuntime().SetDebugSettings({});
    for (int frame = 0; frame < 4; ++frame) static_cast<void>(rig.CleanFrame());
    const double detached = MillisecondsOfFrames(rig, 10);
    Require(attached > compiled * 2.0, "With a breakpoint set the script must run in the interpreter");
    Require(detached * 2.0 < attached, "After the debugger detaches the compiler must be back");
}

void Place(kb::scene::Scene& scene, kb::scene::SceneEntity entity, kb::scene::Vec3 position) {
    kb::scene::TransformComponent transform = scene.Transforms().Get(entity);
    transform.localPosition = position;
    scene.Transforms().Set(entity, transform);
}

// Compiled code and the interpreter give the same positions to the last bit over many frames (the compiler must not change
// what arithmetic means, or replays and lock-step peers would diverge). A debugger with a breakpoint keeps one of the two
// runtimes in the interpreter.
void RunCompilerDeterminismTest() {
    constexpr const char* kScript = R"(
local speed = 2.0
function Tick(self, dt)
  local x, y, z = Transform.GetPositionXYZ(self.entity)
  if Input.IsPressed("jump") then speed = -speed end
  local len = math.sqrt(x * x + y * y + z * z) + 1.0
  local d = (x / len) * 0.6 + (z / len) * 0.8
  Transform.SetPosition(self.entity, x + 0.6 * speed * dt + math.sin(x * 0.01) * 0.001, y + d * dt, z + 0.8 * speed * dt + (len % 3) * 0.0001)
end
)";
    VmRig interpreted(kScript);
    VmRig compiled(kScript);
    kb::script::PucLuaDebugSettings debug;
    debug.breakpoints.push_back(kb::script::PucLuaDebugBreakpoint{ .chunkName = "Other.lua", .line = 3, .enabled = true });
    interpreted.host.LuaRuntime().SetDebugSettings(debug);
    std::vector<kb::scene::SceneEntity> a;
    std::vector<kb::scene::SceneEntity> b;
    for (int index = 0; index < 16; ++index) {
        const kb::scene::Vec3 start{ static_cast<float>(index) * 1.7F, 0.25F * static_cast<float>(index), 3.0F + static_cast<float>(index) };
        a.push_back(interpreted.Add());
        Place(interpreted.scene, a.back(), start);
        b.push_back(compiled.Add());
        Place(compiled.scene, b.back(), start);
    }
    for (int frame = 0; frame < 300; ++frame) {
        static_cast<void>(interpreted.CleanFrame());
        static_cast<void>(compiled.CleanFrame());
    }
    for (std::size_t index = 0U; index < a.size(); ++index) {
        const kb::scene::Vec3 first = interpreted.scene.Transforms().Get(a[index]).localPosition;
        const kb::scene::Vec3 second = compiled.scene.Transforms().Get(b[index]).localPosition;
        Require(first.x == second.x && first.y == second.y && first.z == second.z, "Compiled and interpreted code must give the same positions bit for bit");
    }
}

// Compiled code and a state restored from a checkpoint: the scripts go on from the saved state and give what an
// uninterrupted run gives.
void RunCheckpointCompilerTest() {
    constexpr const char* kScript = R"(
function Tick(self, dt)
  self.count = (self.count or 0) + 1
  local total = 0
  for i = 1, 2000 do total = total + (i % 5) * 0.25 end
  Emit('Count', { n = self.count, total = total })
end
)";
    VmRig rig(kScript);
    static_cast<void>(rig.Add());
    for (int frame = 0; frame < 60; ++frame) static_cast<void>(rig.CleanFrame()); // hot: compiled
    std::string error;
    const std::unique_ptr<kb::core::RuntimeStateSnapshot> snapshot = rig.host.LuaRuntime().CaptureRuntimeState(error);
    Require(snapshot != nullptr, error.c_str());
    for (int frame = 0; frame < 40; ++frame) static_cast<void>(rig.CleanFrame());
    Require(rig.host.LuaRuntime().RestoreRuntimeState(*snapshot, error), error.c_str());
    const kb::script::ScriptRuntimeExecutionResult& after = rig.CleanFrame();
    Require(after.emittedEvents.size() == 1U && TextsOf(after, "Count", "total").size() == 1U, "the object must run after the restore");
    for (const kb::script::ScriptEventArgument& argument : after.emittedEvents.front().arguments) {
        if (argument.name == "n") Require(argument.value.AsInt() == 61, "the restored script must continue from the saved state");
    }
    for (int frame = 0; frame < 60; ++frame) static_cast<void>(rig.CleanFrame()); // compiles again
}

// LuaJIT holds every collectable address in 47 bits; platforms that tag malloc pointers get memory from LuaJIT's allocator.
void RunVmAllocatorHeapTest() {
    kb::script::PucLuaRawHeap heap{ true };
    std::vector<std::pair<void*, std::size_t>> blocks;
    for (std::size_t index = 0U; index < 2000U; ++index) {
        const std::size_t size = 16U + (index * 37U) % 3000U;
        void* block = heap.Resize(nullptr, 0U, size);
        Require(block != nullptr && (reinterpret_cast<std::uintptr_t>(block) >> 47U) == 0U, "the VM allocator must return addresses below 2^47");
        std::memset(block, static_cast<int>(index & 0xFFU), size);
        blocks.emplace_back(block, size);
    }
    for (std::size_t index = 0U; index < blocks.size(); ++index) {
        auto& [block, size] = blocks[index];
        const std::size_t grown = size + 4000U;
        block = heap.Resize(block, size, grown);
        Require(block != nullptr && (reinterpret_cast<std::uintptr_t>(block) >> 47U) == 0U, "a grown block must stay below 2^47");
        Require(static_cast<unsigned char*>(block)[size - 1U] == static_cast<unsigned char>(index & 0xFFU), "growing must keep the contents");
        size = grown;
    }
    for (auto& [block, size] : blocks) static_cast<void>(heap.Resize(block, size, 0U));
}

#endif

} // namespace

void RunScriptVmTests() {
    RunBitLibraryTest();
    RunSharedSubsetTest();
    RunHotCallErrorsTest();
    RunHotTableFormsTest();
    RunBudgetTest();
    RunDebuggerWorkflowTest();
    RunWatchdogCoreTest();
    RunLanguageSubsetTest();
#if defined(KB_SCRIPT_VM_LUAJIT)
    RunCompiledBudgetTest();
    RunBudgetStressTest();
    RunDebuggerCompilerTest();
    RunCompilerDeterminismTest();
    RunCheckpointCompilerTest();
    RunVmAllocatorHeapTest();
#endif
}

} // namespace kb::tests
