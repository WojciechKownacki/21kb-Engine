#include "engine/modules/EngineModuleExports.hpp"

#include <exception>
#include <fstream>

namespace {

void Trace(const char* message) {
    std::ofstream output{ KB_MODULE_FAULT_TRACE, std::ios::app };
    output << message << '\n';
}

class PluginError final : public std::exception {
public:
    ~PluginError() override { Trace("exception destroyed"); }
    const char* what() const noexcept override { return "module fixture failure"; }
};

class FaultModule final : public kb::modules::IEngineModule {
public:
    ~FaultModule() override { Trace("module destroyed"); }
    kb::modules::EngineModuleMetadata Metadata() const override {
        Trace("metadata entered");
#if KB_MODULE_FAULT == 2
        throw PluginError{};
#else
        return { "Tests.WrongIdentity", 1U, {}, kb::modules::EngineModuleLoadingPhase::Default };
#endif
    }
};

} // namespace

#ifdef KB_EXPORT_ENGINE_MODULE_BUILD_FINGERPRINT
KB_EXPORT_ENGINE_MODULE_BUILD_FINGERPRINT
#endif

extern "C" KB_ENGINE_MODULE_EXPORT std::uint32_t kb_engine_module_abi_version() {
    return kb::modules::kEngineModuleAbiVersion;
}

extern "C" KB_ENGINE_MODULE_EXPORT const char* kb_engine_module_name() {
    return "Tests.ModuleFault";
}

extern "C" KB_ENGINE_MODULE_EXPORT kb::modules::IEngineModule* kb_create_engine_module() {
    Trace("factory entered");
#if KB_MODULE_FAULT == 1
    throw PluginError{};
#else
    return new FaultModule{};
#endif
}

extern "C" KB_ENGINE_MODULE_EXPORT void kb_destroy_engine_module(kb::modules::IEngineModule* module) {
    delete module;
}
