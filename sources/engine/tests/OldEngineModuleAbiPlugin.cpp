#include "engine/modules/EngineModuleExports.hpp"

#include <cstdlib>
#include <cstdint>
#include <fstream>

static_assert(kb::modules::kEngineModuleAbiVersion > 1U);
#ifndef KB_TEST_ABI_VARIANT
#define KB_TEST_ABI_VARIANT 0
#endif
#if KB_TEST_ABI_VARIANT >= 2
extern "C" KB_ENGINE_MODULE_EXPORT const kb::modules::EngineModuleBuildFingerprint kb_engine_module_build_fingerprint = [] {
    auto fingerprint = kb::modules::kEngineModuleBuildFingerprint;
#if KB_TEST_ABI_VARIANT == 2
    ++fingerprint.fields[1]; // Compiler version.
#elif KB_TEST_ABI_VARIANT == 3
    ++fingerprint.fields[9]; // Metadata layout.
#elif KB_TEST_ABI_VARIANT == 4
    ++fingerprint.fields[3]; // Runtime mode.
#elif KB_TEST_ABI_VARIANT == 5
    --fingerprint.size; // Descriptor layout.
#endif
    return fingerprint;
}();
#endif

extern "C" KB_ENGINE_MODULE_EXPORT std::uint32_t kb_engine_module_abi_version() {
    #if KB_TEST_ABI_VARIANT == 0
    return kb::modules::kEngineModuleAbiVersion - 1U;
#elif KB_TEST_ABI_VARIANT == 1
    return kb::modules::kEngineModuleAbiVersion + 1U;
#else
    return kb::modules::kEngineModuleAbiVersion;
#endif
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
