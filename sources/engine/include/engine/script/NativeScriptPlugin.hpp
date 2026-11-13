#pragma once

#include "engine/script/ScriptEvent.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/script/ScriptLifecycle.hpp"
#include "engine/script/ScriptValue.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace kb::script {

inline constexpr std::uint32_t kNativeScriptPluginApiVersion = 8U;
inline constexpr const char* kNativeScriptPluginDefaultEntryPoint = "kb_register_native_scripts";

using NativeScriptPluginLifecycleProc = void (*)(ScriptExecutionContext* context);
using NativeScriptPluginEventProc = void (*)(ScriptExecutionContext* context, const ScriptEvent* event);

struct NativeScriptSavedField {
    std::string name;
    ScriptValue value;
};

// The values a script object keeps across a rebuilt module. The script lists them in one place, its
// Serialize(NativeScriptArchive&) member, and the same list serves to save them before the old module goes and
// to restore them into the object the new module creates:
//
//     void Serialize(kb::script::NativeScriptArchive& archive) {
//         archive.Field("laps", laps);
//         archive.Field("label", label);
//     }
//
// A field the new version of the class no longer lists is dropped; one it lists anew keeps its default.
class NativeScriptArchive final {
public:
    NativeScriptArchive(std::vector<NativeScriptSavedField>& values, bool loading) noexcept
        : values_(&values), loading_(loading) {}

    [[nodiscard]] bool Loading() const noexcept { return loading_; }

    template <typename Value>
    void Field(std::string_view name, Value& value) {
        if (loading_) {
            for (const NativeScriptSavedField& saved : *values_) {
                if (saved.name == name) {
                    Read(saved.value, value);
                    return;
                }
            }
        } else {
            values_->push_back(NativeScriptSavedField{ std::string{ name }, ScriptValue{ Convert(value) } });
        }
    }

private:
    template <typename Value>
    static auto Convert(const Value& value) {
        if constexpr (std::is_enum_v<Value>) {
            return static_cast<int>(value);
        } else {
            static_assert(std::is_same_v<Value, bool> || std::is_same_v<Value, float> || std::is_same_v<Value, double> ||
                              std::is_same_v<Value, int> || std::is_same_v<Value, std::int64_t> ||
                              std::is_same_v<Value, std::uint32_t> || std::is_same_v<Value, std::string>,
                "a saved field is a bool, int, float, double, std::int64_t, std::uint32_t, std::string or an enum");
            return value;
        }
    }

    template <typename Value>
    static void Read(const ScriptValue& saved, Value& value) {
        if constexpr (std::is_same_v<Value, bool>) value = saved.AsBool(value);
        else if constexpr (std::is_same_v<Value, int>) value = saved.AsInt(value);
        else if constexpr (std::is_same_v<Value, float>) value = saved.AsFloat(value);
        else if constexpr (std::is_same_v<Value, double>) value = saved.AsDouble(value);
        else if constexpr (std::is_same_v<Value, std::int64_t>) value = saved.AsInt64(value);
        else if constexpr (std::is_same_v<Value, std::uint32_t>) value = saved.AsUInt32(value);
        else if constexpr (std::is_same_v<Value, std::string>) value = saved.AsString();
        else if constexpr (std::is_enum_v<Value>) value = static_cast<Value>(saved.AsInt(static_cast<int>(value)));
    }

    std::vector<NativeScriptSavedField>* values_;
    bool loading_;
};

// A script class as the host sees it: made once for each object the script is attached to, called through
// the hooks it defines, saved and restored through its Serialize member when a rebuilt module replaces this one.
struct NativeScriptClassApi {
    void* (*create)() = nullptr;
    void (*destroy)(void* object) = nullptr;
    void (*call)(void* object, ScriptLifecycleEvent event, ScriptExecutionContext* context) = nullptr;
    // Bit (1 << event) for each hook the class defines.
    std::uint32_t hooks = 0U;
    // Null when the class keeps nothing across a rebuild.
    void (*fields)(void* object, NativeScriptArchive* archive) = nullptr;
    // The class has data members of its own. With no `fields` they start over in a rebuilt module, which the
    // editor tells the author once.
    bool hasState = false;
};

struct NativeScriptPluginApi {
    std::uint32_t version = kNativeScriptPluginApiVersion;
    void* user = nullptr;
    bool (*registerLifecycle)(void* user, const char* symbol, ScriptLifecycleEvent event, NativeScriptPluginLifecycleProc callback) = nullptr;
    bool (*registerEvent)(void* user, const char* symbol, const char* eventName, NativeScriptPluginEventProc callback) = nullptr;
    bool (*registerClass)(void* user, const char* symbol, const NativeScriptClassApi* script) = nullptr;
};

using NativeScriptPluginRegisterProc = bool (*)(NativeScriptPluginApi* api);

#define KB_NATIVE_SCRIPT_HOOKS(X) \
    X(Created) X(Activated) X(Ready) X(FixedTick) X(Tick) X(LateTick) X(BeforeRender) X(AfterRender) X(Deactivated) X(Destroyed)

// Registers a script class the way a Lua script's functions are found by name: a member called Created,
// Activated, Ready, FixedTick, Tick, LateTick, BeforeRender, AfterRender, Deactivated or Destroyed that takes a
// ScriptExecutionContext* is called at that moment for every object the script is attached to. A class whose
// hooks are all static has no state of its own. A class with ordinary member hooks is made once for each
// object (when its Created hook is due), keeps its member variables from call to call, is destroyed with the
// object, and is made again, with the values its Serialize member lists, when the module is rebuilt while the
// game runs. A class that defines no hook is refused.
template <typename Script>
[[nodiscard]] bool RegisterNativeScript(NativeScriptPluginApi* api, const char* symbol) {
    using Context = ScriptExecutionContext*;
    if (api == nullptr || symbol == nullptr || symbol[0] == '\0') {
        return false;
    }
    constexpr bool kHasMemberHook = false
#define KB_NATIVE_SCRIPT_CHECK(Name) \
        || (!requires { Script::Name(std::declval<Context>()); } && requires(Script& object) { object.Name(std::declval<Context>()); })
        KB_NATIVE_SCRIPT_HOOKS(KB_NATIVE_SCRIPT_CHECK)
#undef KB_NATIVE_SCRIPT_CHECK
        ;
    if constexpr (kHasMemberHook) {
        static_assert(std::is_default_constructible_v<Script>, "a script class with member hooks needs a default constructor");
        if (api->registerClass == nullptr) {
            return false;
        }
        NativeScriptClassApi script;
        script.create = []() -> void* { return new Script(); };
        script.destroy = [](void* object) { delete static_cast<Script*>(object); };
#define KB_NATIVE_SCRIPT_MASK(Name) \
        if constexpr (requires { Script::Name(std::declval<Context>()); } || requires(Script& object) { object.Name(std::declval<Context>()); }) { \
            script.hooks |= 1U << static_cast<unsigned>(ScriptLifecycleEvent::Name); \
        }
        KB_NATIVE_SCRIPT_HOOKS(KB_NATIVE_SCRIPT_MASK)
#undef KB_NATIVE_SCRIPT_MASK
        script.call = [](void* object, ScriptLifecycleEvent event, ScriptExecutionContext* context) {
            switch (event) {
#define KB_NATIVE_SCRIPT_CALL(Name) \
            case ScriptLifecycleEvent::Name: \
                if constexpr (requires { Script::Name(std::declval<Context>()); }) { \
                    Script::Name(context); \
                } else if constexpr (requires(Script& instance) { instance.Name(std::declval<Context>()); }) { \
                    static_cast<Script*>(object)->Name(context); \
                } \
                break;
            KB_NATIVE_SCRIPT_HOOKS(KB_NATIVE_SCRIPT_CALL)
#undef KB_NATIVE_SCRIPT_CALL
            }
            static_cast<void>(object);
            static_cast<void>(context);
        };
        script.hasState = !std::is_empty_v<Script>;
        if constexpr (requires(Script& object, NativeScriptArchive& archive) { object.Serialize(archive); }) {
            script.fields = [](void* object, NativeScriptArchive* archive) { static_cast<Script*>(object)->Serialize(*archive); };
        }
        return api->registerClass(api->user, symbol, &script);
    } else {
        if (api->registerLifecycle == nullptr) {
            return false;
        }
        bool registered = false;
        bool succeeded = true;
        const auto add = [&](ScriptLifecycleEvent event, NativeScriptPluginLifecycleProc callback) {
            registered = true;
            succeeded = api->registerLifecycle(api->user, symbol, event, callback) && succeeded;
        };
#define KB_NATIVE_SCRIPT_ADD(Name) \
        if constexpr (requires { Script::Name(std::declval<Context>()); }) add(ScriptLifecycleEvent::Name, &Script::Name);
        KB_NATIVE_SCRIPT_HOOKS(KB_NATIVE_SCRIPT_ADD)
#undef KB_NATIVE_SCRIPT_ADD
        return registered && succeeded;
    }
}

#undef KB_NATIVE_SCRIPT_HOOKS

} // namespace kb::script

#if defined(_WIN32)
#define KB_NATIVE_SCRIPT_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define KB_NATIVE_SCRIPT_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif
