#include "engine/script/ScriptCollectionsApi.hpp"

#include "engine/library/EngineLibraryCollections.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptValue.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

// LIB-058: the native side of the script-facing collections. Each collection
// lives here keyed by a monotonic uint64 handle a script holds as an opaque
// Hash (ScriptValue is scalar-only — LIB-032 — so a script can never hold the
// container itself, only a handle to it, the same way Assets/Timer/Task
// handles work). One shared counter across all five maps guarantees a handle
// is globally unique, so an Array handle can never accidentally resolve
// against the Set/Map/Queue/Stack stores. The store is owned by the
// registered function callbacks (each captures a shared_ptr to it), so its
// lifetime is exactly the ScriptFunctionRegistry's — no host/PIMPL change
// and no threading through ScriptFunctionCallContext needed.
//
// Element/key/value type is Float today (Int coerces in losslessly through
// ScriptFunctionRegistry's Int->Float rule) — the dominant script-data case
// (numeric buffers) and the exact type LIB-037's maxCollectionSize bound is
// about. Additional element types are a documented incremental follow-up,
// the same way World.SetProperty* / Shared.Set.* are per-type and were
// grown one type at a time, not a facade: a Float-typed collection is a real,
// complete container, not a stub for a "real" one.
class ScriptCollectionStore final {
public:
    [[nodiscard]] std::uint64_t CreateArray() {
        const std::uint64_t handle = nextHandle_++;
        arrays_.emplace(handle, kb::library::Array<ScriptValue>{});
        return handle;
    }
    [[nodiscard]] std::uint64_t CreateSet() {
        const std::uint64_t handle = nextHandle_++;
        sets_.emplace(handle, kb::library::Set<ScriptValue>{});
        return handle;
    }
    [[nodiscard]] std::uint64_t CreateMap() {
        const std::uint64_t handle = nextHandle_++;
        maps_.emplace(handle, kb::library::Map<ScriptValue, ScriptValue>{});
        return handle;
    }
    [[nodiscard]] std::uint64_t CreateQueue() {
        const std::uint64_t handle = nextHandle_++;
        queues_.emplace(handle, kb::library::Queue<ScriptValue>{});
        return handle;
    }
    [[nodiscard]] std::uint64_t CreateStack() {
        const std::uint64_t handle = nextHandle_++;
        stacks_.emplace(handle, kb::library::Stack<ScriptValue>{});
        return handle;
    }

    [[nodiscard]] kb::library::Array<ScriptValue>* Array(std::uint64_t handle) noexcept { return Lookup(arrays_, handle); }
    [[nodiscard]] kb::library::Set<ScriptValue>* Set(std::uint64_t handle) noexcept { return Lookup(sets_, handle); }
    [[nodiscard]] kb::library::Map<ScriptValue, ScriptValue>* Map(std::uint64_t handle) noexcept { return Lookup(maps_, handle); }
    [[nodiscard]] kb::library::Queue<ScriptValue>* Queue(std::uint64_t handle) noexcept { return Lookup(queues_, handle); }
    [[nodiscard]] kb::library::Stack<ScriptValue>* Stack(std::uint64_t handle) noexcept { return Lookup(stacks_, handle); }

private:
    // Explicit return type, not auto: the accessors above call Lookup before its definition, and a deduced
    // return type is not available at that point (GCC rejects it; MSVC and Clang happen to accept it).
    template <typename Map>
    [[nodiscard]] static typename Map::mapped_type* Lookup(Map& map, std::uint64_t handle) noexcept {
        const auto found = map.find(handle);
        return found == map.end() ? nullptr : &found->second;
    }

    std::unordered_map<std::uint64_t, kb::library::Array<ScriptValue>> arrays_;
    std::unordered_map<std::uint64_t, kb::library::Set<ScriptValue>> sets_;
    std::unordered_map<std::uint64_t, kb::library::Map<ScriptValue, ScriptValue>> maps_;
    std::unordered_map<std::uint64_t, kb::library::Queue<ScriptValue>> queues_;
    std::unordered_map<std::uint64_t, kb::library::Stack<ScriptValue>> stacks_;
    std::uint64_t nextHandle_ = 1U;
};

[[nodiscard]] const ScriptValue* FindArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == name) {
            return &argument.value;
        }
    }
    return nullptr;
}

[[nodiscard]] std::uint64_t HandleArg(std::span<const ScriptFunctionArgument> arguments) {
    const ScriptValue* value = FindArg(arguments, "handle");
    return value == nullptr ? 0U : value->AsUInt64();
}

[[nodiscard]] float FloatArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? 0.0F : value->AsFloat();
}

[[nodiscard]] int IntArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? 0 : value->AsInt();
}

[[nodiscard]] ScriptFunctionCallResult HandleResult(std::uint64_t handle) {
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "handle", ScriptValue{ handle, ScriptValueType::Hash } } }, .errors = {} };
}

[[nodiscard]] ScriptFunctionCallResult BoolResult(std::string_view pin, bool value) {
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ std::string{ pin }, ScriptValue{ value } } }, .errors = {} };
}

[[nodiscard]] ScriptFunctionCallResult IntResult(std::string_view pin, int value) {
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ std::string{ pin }, ScriptValue{ value } } }, .errors = {} };
}

// A read that either finds a value or honestly reports found=false — never a
// fabricated fallback masquerading as a real element.
[[nodiscard]] ScriptFunctionCallResult FoundValueResult(bool found, float value) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "found", ScriptValue{ found } }, ScriptFunctionArgument{ "value", ScriptValue{ found ? value : 0.0F } } },
        .errors = {},
    };
}

using StorePtr = std::shared_ptr<ScriptCollectionStore>;

[[nodiscard]] std::vector<ScriptFunctionPin> HandleIn() {
    return { ScriptFunctionPin{ "handle", ScriptValueType::Hash, true } };
}
[[nodiscard]] std::vector<ScriptFunctionPin> HandleValueIn() {
    return { ScriptFunctionPin{ "handle", ScriptValueType::Hash, true }, ScriptFunctionPin{ "value", ScriptValueType::Float, true } };
}
[[nodiscard]] std::vector<ScriptFunctionPin> HandleOut() {
    return { ScriptFunctionPin{ "handle", ScriptValueType::Hash, true } };
}
[[nodiscard]] std::vector<ScriptFunctionPin> BoolOut(std::string_view name) {
    return { ScriptFunctionPin{ std::string{ name }, ScriptValueType::Bool, true } };
}
[[nodiscard]] std::vector<ScriptFunctionPin> CountOut() {
    return { ScriptFunctionPin{ "count", ScriptValueType::Int, true } };
}
[[nodiscard]] std::vector<ScriptFunctionPin> FoundValueOut() {
    return { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "value", ScriptValueType::Float, true } };
}

bool RegisterFn(ScriptRuntimeHost& host, std::string name, std::vector<ScriptFunctionPin> inputs, std::vector<ScriptFunctionPin> outputs, ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = std::move(inputs);
    desc.signature.outputs = std::move(outputs);
    desc.callback = std::move(callback);
    return host.RegisterFunction(std::move(desc));
}

} // namespace

bool ScriptCollectionsApi::Register(ScriptRuntimeHost& host) {
    const StorePtr store = std::make_shared<ScriptCollectionStore>();
    bool ok = true;

    // ---- Array (ordered, indexable) ----
    ok = RegisterFn(host, "Array.Create", {}, HandleOut(),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument>) { return HandleResult(store->CreateArray()); })
        && ok;
    ok = RegisterFn(host, "Array.Length", HandleIn(), CountOut(),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  const kb::library::Array<ScriptValue>* array = store->Array(HandleArg(args));
                  return IntResult("count", array == nullptr ? 0 : static_cast<int>(array->Count()));
              })
        && ok;
    ok = RegisterFn(host, "Array.Push", HandleValueIn(), BoolOut("pushed"),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  kb::library::Array<ScriptValue>* array = store->Array(HandleArg(args));
                  return BoolResult("pushed", array != nullptr && array->PushBack(ScriptValue{ FloatArg(args, "value") }));
              })
        && ok;
    ok = RegisterFn(host, "Array.Get",
              { ScriptFunctionPin{ "handle", ScriptValueType::Hash, true }, ScriptFunctionPin{ "index", ScriptValueType::Int, true } }, FoundValueOut(),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  const kb::library::Array<ScriptValue>* array = store->Array(HandleArg(args));
                  const int index = IntArg(args, "index");
                  const ScriptValue* value = (array != nullptr && index >= 0) ? array->GetAt(static_cast<std::size_t>(index)) : nullptr;
                  return FoundValueResult(value != nullptr, value != nullptr ? value->AsFloat() : 0.0F);
              })
        && ok;
    ok = RegisterFn(host, "Array.Set",
              { ScriptFunctionPin{ "handle", ScriptValueType::Hash, true }, ScriptFunctionPin{ "index", ScriptValueType::Int, true }, ScriptFunctionPin{ "value", ScriptValueType::Float, true } }, BoolOut("set"),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  kb::library::Array<ScriptValue>* array = store->Array(HandleArg(args));
                  const int index = IntArg(args, "index");
                  return BoolResult("set", array != nullptr && index >= 0 && array->SetAt(static_cast<std::size_t>(index), ScriptValue{ FloatArg(args, "value") }));
              })
        && ok;
    ok = RegisterFn(host, "Array.RemoveAt",
              { ScriptFunctionPin{ "handle", ScriptValueType::Hash, true }, ScriptFunctionPin{ "index", ScriptValueType::Int, true } }, BoolOut("removed"),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  kb::library::Array<ScriptValue>* array = store->Array(HandleArg(args));
                  const int index = IntArg(args, "index");
                  return BoolResult("removed", array != nullptr && index >= 0 && array->RemoveAt(static_cast<std::size_t>(index)));
              })
        && ok;
    ok = RegisterFn(host, "Array.Clear", HandleIn(), BoolOut("cleared"),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  kb::library::Array<ScriptValue>* array = store->Array(HandleArg(args));
                  if (array != nullptr) {
                      array->Clear();
                  }
                  return BoolResult("cleared", array != nullptr);
              })
        && ok;

    // ---- Set (unique membership) ----
    ok = RegisterFn(host, "Set.Create", {}, HandleOut(),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument>) { return HandleResult(store->CreateSet()); })
        && ok;
    ok = RegisterFn(host, "Set.Count", HandleIn(), CountOut(),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  const kb::library::Set<ScriptValue>* set = store->Set(HandleArg(args));
                  return IntResult("count", set == nullptr ? 0 : static_cast<int>(set->Count()));
              })
        && ok;
    ok = RegisterFn(host, "Set.Add", HandleValueIn(), BoolOut("added"),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  kb::library::Set<ScriptValue>* set = store->Set(HandleArg(args));
                  return BoolResult("added", set != nullptr && set->Insert(ScriptValue{ FloatArg(args, "value") }));
              })
        && ok;
    ok = RegisterFn(host, "Set.Contains", HandleValueIn(), BoolOut("contains"),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  const kb::library::Set<ScriptValue>* set = store->Set(HandleArg(args));
                  return BoolResult("contains", set != nullptr && set->Contains(ScriptValue{ FloatArg(args, "value") }));
              })
        && ok;
    ok = RegisterFn(host, "Set.Remove", HandleValueIn(), BoolOut("removed"),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  kb::library::Set<ScriptValue>* set = store->Set(HandleArg(args));
                  return BoolResult("removed", set != nullptr && set->Remove(ScriptValue{ FloatArg(args, "value") }));
              })
        && ok;
    ok = RegisterFn(host, "Set.Clear", HandleIn(), BoolOut("cleared"),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  kb::library::Set<ScriptValue>* set = store->Set(HandleArg(args));
                  if (set != nullptr) {
                      set->Clear();
                  }
                  return BoolResult("cleared", set != nullptr);
              })
        && ok;

    // ---- Map (key -> value) ----
    ok = RegisterFn(host, "Map.Create", {}, HandleOut(),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument>) { return HandleResult(store->CreateMap()); })
        && ok;
    ok = RegisterFn(host, "Map.Count", HandleIn(), CountOut(),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  const kb::library::Map<ScriptValue, ScriptValue>* map = store->Map(HandleArg(args));
                  return IntResult("count", map == nullptr ? 0 : static_cast<int>(map->Count()));
              })
        && ok;
    ok = RegisterFn(host, "Map.Set",
              { ScriptFunctionPin{ "handle", ScriptValueType::Hash, true }, ScriptFunctionPin{ "key", ScriptValueType::Float, true }, ScriptFunctionPin{ "value", ScriptValueType::Float, true } }, BoolOut("set"),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  kb::library::Map<ScriptValue, ScriptValue>* map = store->Map(HandleArg(args));
                  return BoolResult("set", map != nullptr && map->Set(ScriptValue{ FloatArg(args, "key") }, ScriptValue{ FloatArg(args, "value") }));
              })
        && ok;
    ok = RegisterFn(host, "Map.Get",
              { ScriptFunctionPin{ "handle", ScriptValueType::Hash, true }, ScriptFunctionPin{ "key", ScriptValueType::Float, true } }, FoundValueOut(),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  const kb::library::Map<ScriptValue, ScriptValue>* map = store->Map(HandleArg(args));
                  const ScriptValue* value = map != nullptr ? map->Find(ScriptValue{ FloatArg(args, "key") }) : nullptr;
                  return FoundValueResult(value != nullptr, value != nullptr ? value->AsFloat() : 0.0F);
              })
        && ok;
    ok = RegisterFn(host, "Map.ContainsKey",
              { ScriptFunctionPin{ "handle", ScriptValueType::Hash, true }, ScriptFunctionPin{ "key", ScriptValueType::Float, true } }, BoolOut("contains"),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  const kb::library::Map<ScriptValue, ScriptValue>* map = store->Map(HandleArg(args));
                  return BoolResult("contains", map != nullptr && map->ContainsKey(ScriptValue{ FloatArg(args, "key") }));
              })
        && ok;
    ok = RegisterFn(host, "Map.Remove",
              { ScriptFunctionPin{ "handle", ScriptValueType::Hash, true }, ScriptFunctionPin{ "key", ScriptValueType::Float, true } }, BoolOut("removed"),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  kb::library::Map<ScriptValue, ScriptValue>* map = store->Map(HandleArg(args));
                  return BoolResult("removed", map != nullptr && map->Remove(ScriptValue{ FloatArg(args, "key") }));
              })
        && ok;
    ok = RegisterFn(host, "Map.Clear", HandleIn(), BoolOut("cleared"),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  kb::library::Map<ScriptValue, ScriptValue>* map = store->Map(HandleArg(args));
                  if (map != nullptr) {
                      map->Clear();
                  }
                  return BoolResult("cleared", map != nullptr);
              })
        && ok;

    // ---- Queue (FIFO) ----
    ok = RegisterFn(host, "Queue.Create", {}, HandleOut(),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument>) { return HandleResult(store->CreateQueue()); })
        && ok;
    ok = RegisterFn(host, "Queue.Count", HandleIn(), CountOut(),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  const kb::library::Queue<ScriptValue>* queue = store->Queue(HandleArg(args));
                  return IntResult("count", queue == nullptr ? 0 : static_cast<int>(queue->Count()));
              })
        && ok;
    ok = RegisterFn(host, "Queue.Enqueue", HandleValueIn(), BoolOut("enqueued"),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  kb::library::Queue<ScriptValue>* queue = store->Queue(HandleArg(args));
                  return BoolResult("enqueued", queue != nullptr && queue->Enqueue(ScriptValue{ FloatArg(args, "value") }));
              })
        && ok;
    ok = RegisterFn(host, "Queue.Dequeue", HandleIn(), FoundValueOut(),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  kb::library::Queue<ScriptValue>* queue = store->Queue(HandleArg(args));
                  ScriptValue out;
                  const bool dequeued = queue != nullptr && queue->Dequeue(out);
                  return FoundValueResult(dequeued, dequeued ? out.AsFloat() : 0.0F);
              })
        && ok;
    ok = RegisterFn(host, "Queue.Peek", HandleIn(), FoundValueOut(),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  const kb::library::Queue<ScriptValue>* queue = store->Queue(HandleArg(args));
                  const ScriptValue* value = queue != nullptr ? queue->Peek() : nullptr;
                  return FoundValueResult(value != nullptr, value != nullptr ? value->AsFloat() : 0.0F);
              })
        && ok;
    ok = RegisterFn(host, "Queue.Clear", HandleIn(), BoolOut("cleared"),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  kb::library::Queue<ScriptValue>* queue = store->Queue(HandleArg(args));
                  if (queue != nullptr) {
                      queue->Clear();
                  }
                  return BoolResult("cleared", queue != nullptr);
              })
        && ok;

    // ---- Stack (LIFO) ----
    ok = RegisterFn(host, "Stack.Create", {}, HandleOut(),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument>) { return HandleResult(store->CreateStack()); })
        && ok;
    ok = RegisterFn(host, "Stack.Count", HandleIn(), CountOut(),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  const kb::library::Stack<ScriptValue>* stack = store->Stack(HandleArg(args));
                  return IntResult("count", stack == nullptr ? 0 : static_cast<int>(stack->Count()));
              })
        && ok;
    ok = RegisterFn(host, "Stack.Push", HandleValueIn(), BoolOut("pushed"),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  kb::library::Stack<ScriptValue>* stack = store->Stack(HandleArg(args));
                  return BoolResult("pushed", stack != nullptr && stack->Push(ScriptValue{ FloatArg(args, "value") }));
              })
        && ok;
    ok = RegisterFn(host, "Stack.Pop", HandleIn(), FoundValueOut(),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  kb::library::Stack<ScriptValue>* stack = store->Stack(HandleArg(args));
                  ScriptValue out;
                  const bool popped = stack != nullptr && stack->Pop(out);
                  return FoundValueResult(popped, popped ? out.AsFloat() : 0.0F);
              })
        && ok;
    ok = RegisterFn(host, "Stack.Top", HandleIn(), FoundValueOut(),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  const kb::library::Stack<ScriptValue>* stack = store->Stack(HandleArg(args));
                  const ScriptValue* value = stack != nullptr ? stack->Top() : nullptr;
                  return FoundValueResult(value != nullptr, value != nullptr ? value->AsFloat() : 0.0F);
              })
        && ok;
    ok = RegisterFn(host, "Stack.Clear", HandleIn(), BoolOut("cleared"),
              [store](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> args) {
                  kb::library::Stack<ScriptValue>* stack = store->Stack(HandleArg(args));
                  if (stack != nullptr) {
                      stack->Clear();
                  }
                  return BoolResult("cleared", stack != nullptr);
              })
        && ok;

    return ok;
}

} // namespace kb::script
