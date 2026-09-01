#include "engine/modules/EngineModuleExports.hpp"
#include "engine/modules/IEngineModule.hpp"

#include <cstdint>

namespace {

class WindowsRuntimeModuleTestPlugin final : public kb::modules::IEngineModule {
public:
    [[nodiscard]] kb::modules::EngineModuleMetadata Metadata() const override {
        return kb::modules::EngineModuleMetadata{
            .name = "Tests.PackagedWindowsRuntime",
        };
    }
};

} // namespace

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
