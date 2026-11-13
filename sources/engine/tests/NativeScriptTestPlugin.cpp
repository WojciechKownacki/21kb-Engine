#include "engine/script/NativeScriptPlugin.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/script/ScriptValue.hpp"

namespace {

constexpr const char* kSymbol = "tests.NativePlugin";

void OnTick(kb::script::ScriptExecutionContext* context) {
    if (context == nullptr) {
        return;
    }
    static_cast<void>(context->SetSharedValue("nativePlugin.Tick", kb::script::ScriptValue{ 1 }));
    context->Emit("NativePluginTicked");
}

void OnPing(kb::script::ScriptExecutionContext* context, const kb::script::ScriptEvent* event) {
    if (context == nullptr || event == nullptr) {
        return;
    }
    static_cast<void>(context->SetSharedValue("nativePlugin.Event", kb::script::ScriptValue{ static_cast<int>(event->arguments.size()) }));
}

} // namespace

KB_NATIVE_SCRIPT_PLUGIN_EXPORT bool kb_register_native_scripts(kb::script::NativeScriptPluginApi* api) {
    if (api == nullptr || api->version != kb::script::kNativeScriptPluginApiVersion || api->registerLifecycle == nullptr || api->registerEvent == nullptr) {
        return false;
    }
    return api->registerLifecycle(api->user, kSymbol, kb::script::ScriptLifecycleEvent::Tick, &OnTick) &&
        api->registerEvent(api->user, kSymbol, "NativePluginPing", &OnPing);
}

KB_NATIVE_SCRIPT_PLUGIN_EXPORT bool kb_register_native_scripts_partial_failure(kb::script::NativeScriptPluginApi* api) {
    if (api == nullptr || api->version != kb::script::kNativeScriptPluginApiVersion || api->registerLifecycle == nullptr) {
        return false;
    }
    static_cast<void>(api->registerLifecycle(api->user, "tests.NativePluginPartialFailure", kb::script::ScriptLifecycleEvent::Tick, &OnTick));
    return false;
}

namespace {

// A script class with state: its object is made by this library, carried over a reload through Serialize, and
// destroyed by this library's code before the library is unmapped.
class StatefulObject {
public:
    void Tick(kb::script::ScriptExecutionContext* context) {
        ++ticks_;
        static_cast<void>(context->SetSharedValue("nativeObject.Ticks", kb::script::ScriptValue{ ticks_ }));
    }

    void Serialize(kb::script::NativeScriptArchive& archive) {
        archive.Field("ticks", ticks_);
    }

private:
    int ticks_ = 0;
};

// Members but no Serialize: the load result says so, the editor tells the author.
class ForgetfulObject {
public:
    void Tick(kb::script::ScriptExecutionContext*) { ++ticks_; }

private:
    int ticks_ = 0;
};

} // namespace

KB_NATIVE_SCRIPT_PLUGIN_EXPORT bool kb_register_native_object_script(kb::script::NativeScriptPluginApi* api) {
    return api != nullptr && api->version == kb::script::kNativeScriptPluginApiVersion &&
        kb::script::RegisterNativeScript<StatefulObject>(api, "tests.NativePluginObject") &&
        kb::script::RegisterNativeScript<ForgetfulObject>(api, "tests.NativePluginForgetful");
}
