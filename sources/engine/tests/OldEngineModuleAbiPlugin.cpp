#include "engine/modules/EngineModuleExports.hpp"

#include <cstdlib>
#include <cstdint>
#include <fstream>

static_assert(kb::modules::kEngineModuleAbiVersion > 1U);

extern "C" KB_ENGINE_MODULE_EXPORT std::uint32_t kb_engine_module_abi_version() {
    return kb::modules::kEngineModuleAbiVersion - 1U;
}

extern "C" KB_ENGINE_MODULE_EXPORT const char* kb_engine_module_name() {
    return "Tests.OldEngineModuleAbi";
}

extern "C" KB_ENGINE_MODULE_EXPORT kb::modules::IEngineModule* kb_create_engine_module() {
#if defined(_WIN32)
    char* markerPath = nullptr;
    std::size_t markerPathLength = 0U;
    static_cast<void>(_dupenv_s(&markerPath, &markerPathLength, "KB_OLD_ENGINE_MODULE_ABI_CREATE_MARKER"));
#else
    const char* markerPath = std::getenv("KB_OLD_ENGINE_MODULE_ABI_CREATE_MARKER");
#endif
    if (markerPath != nullptr && markerPath[0] != '\0') {
        std::ofstream marker{markerPath, std::ios::binary | std::ios::trunc};
        marker << "create called";
    }
#if defined(_WIN32)
    std::free(markerPath);
#endif
    return nullptr;
}

extern "C" KB_ENGINE_MODULE_EXPORT void kb_destroy_engine_module(kb::modules::IEngineModule*) {}
