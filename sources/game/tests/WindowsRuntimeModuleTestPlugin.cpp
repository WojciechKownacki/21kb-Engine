#include "engine/modules/EngineModuleExports.hpp"
#include "engine/modules/IEngineModule.hpp"
#include "engine/script/NativeScriptPlugin.hpp"

#include <cstdint>

namespace {

std::uint32_t nativeReadyCount = 0U;

void NativeReady(kb::script::ScriptExecutionContext*) {
    ++nativeReadyCount;
}

class WindowsRuntimeModuleTestPlugin final : public kb::modules::IEngineModule {
public:
    [[nodiscard]] kb::modules::EngineModuleMetadata Metadata() const override {
        return kb::modules::EngineModuleMetadata{
            .name = "Tests.PackagedWindowsRuntime",
        };
    }
};

} // namespace

KB_NATIVE_SCRIPT_PLUGIN_EXPORT bool kb_register_native_scripts(kb::script::NativeScriptPluginApi* api) {
    return api != nullptr && api->version == kb::script::kNativeScriptPluginApiVersion &&
        api->registerLifecycle != nullptr && api->registerLifecycle(api->user, "Tests.PackagedNative",
            kb::script::ScriptLifecycleEvent::Ready, &NativeReady);
}

extern "C" KB_ENGINE_MODULE_EXPORT std::uint32_t kb_native_ready_count() {
    return nativeReadyCount;
}

extern "C" KB_ENGINE_MODULE_EXPORT std::uint32_t kb_engine_module_abi_version() {
    return kb::modules::kEngineModuleAbiVersion;
}

extern "C" KB_ENGINE_MODULE_EXPORT const char* kb_engine_module_name() {
    return "Tests.PackagedWindowsRuntime";
}

extern "C" KB_ENGINE_MODULE_EXPORT kb::modules::IEngineModule* kb_create_engine_module() {
    return new WindowsRuntimeModuleTestPlugin();
}

extern "C" KB_ENGINE_MODULE_EXPORT void kb_destroy_engine_module(
    kb::modules::IEngineModule* module) {
    delete module;
}
