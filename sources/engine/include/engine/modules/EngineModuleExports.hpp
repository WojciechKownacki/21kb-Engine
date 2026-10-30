#pragma once

#include "engine/modules/IEngineModule.hpp"

#include <cstdint>
#include <array>
#include "engine/modules/EngineModuleContext.hpp"

#if defined(KB_ENGINE_MODULE_STATIC_LINK)
#define KB_ENGINE_MODULE_EXPORT
#elif defined(_WIN32)
#define KB_ENGINE_MODULE_EXPORT __declspec(dllexport)
#else
#define KB_ENGINE_MODULE_EXPORT
#endif

namespace kb::modules {

// Dynamic modules must also export the build fingerprint below.
inline constexpr std::uint32_t kEngineModuleAbiVersion = 16U;
inline constexpr const char* kEngineModuleCreateSymbol = "kb_create_engine_module";
inline constexpr const char* kEngineModuleDestroySymbol = "kb_destroy_engine_module";
inline constexpr const char* kEngineModuleAbiVersionSymbol = "kb_engine_module_abi_version";
inline constexpr const char* kEngineModuleNameSymbol = "kb_engine_module_name";

// Fixed-width data export: inspected before calling the name, factory or lifecycle.
// Exact build compatibility is intentional while the module interface uses C++ types.
struct EngineModuleBuildFingerprint {
    std::uint32_t size;
    std::uint32_t revision;
    std::array<std::uint32_t, 14> fields;
};
inline constexpr const char* kEngineModuleBuildSymbol = "kb_engine_module_build_fingerprint";
inline constexpr std::array<const char*, 14> kEngineModuleBuildFields = {
    "compiler", "compiler version", "C++ runtime", "runtime mode", "STL ABI", "iterator debug",
    "pointer size", "string size", "string alignment", "metadata size", "metadata alignment",
    "module size", "module alignment", "context size"
};
inline constexpr EngineModuleBuildFingerprint kEngineModuleBuildFingerprint = {
    sizeof(EngineModuleBuildFingerprint), 1U, {
#if defined(__clang__)
        1U, __clang_major__ * 10000U + __clang_minor__ * 100U + __clang_patchlevel__,
#elif defined(_MSC_VER)
        2U, _MSC_FULL_VER,
#elif defined(__GNUC__)
        3U, __GNUC__ * 10000U + __GNUC_MINOR__ * 100U + __GNUC_PATCHLEVEL__,
#else
        0U, 0U,
#endif
#if defined(_LIBCPP_VERSION)
        _LIBCPP_VERSION,
#elif defined(__GLIBCXX__)
        __GLIBCXX__,
#elif defined(_MSVC_STL_VERSION)
        _MSVC_STL_VERSION,
#else
        0U,
#endif
#if defined(_WIN32) && defined(_DLL) && defined(_DEBUG)
        3U,
#elif defined(_WIN32) && defined(_DLL)
        2U,
#elif defined(_WIN32) && defined(_DEBUG)
        1U,
#else
        0U,
#endif
#if defined(_GLIBCXX_USE_CXX11_ABI)
        _GLIBCXX_USE_CXX11_ABI,
#elif defined(_LIBCPP_ABI_VERSION)
        _LIBCPP_ABI_VERSION,
#else
        0U,
#endif
#if defined(_ITERATOR_DEBUG_LEVEL)
        _ITERATOR_DEBUG_LEVEL,
#elif defined(_GLIBCXX_DEBUG)
        1U,
#else
        0U,
#endif
        sizeof(void*), sizeof(std::string), alignof(std::string),
        sizeof(EngineModuleMetadata), alignof(EngineModuleMetadata),
        sizeof(IEngineModule), alignof(IEngineModule), sizeof(EngineModuleContext)
    }
};
static_assert(sizeof(EngineModuleBuildFingerprint) == 64U);

using EngineModuleAbiVersionFn = std::uint32_t (*)();
using EngineModuleNameFn = const char* (*)();
using CreateEngineModuleFn = IEngineModule* (*)();
using DestroyEngineModuleFn = void (*)(IEngineModule*);

} // namespace kb::modules

#define KB_EXPORT_ENGINE_MODULE_BUILD_FINGERPRINT \
    extern "C" KB_ENGINE_MODULE_EXPORT const kb::modules::EngineModuleBuildFingerprint \
        kb_engine_module_build_fingerprint = kb::modules::kEngineModuleBuildFingerprint;
