#pragma once

#include "engine/modules/IEngineModule.hpp"

#include <cstdint>

#if defined(KB_ENGINE_MODULE_STATIC_LINK)
#define KB_ENGINE_MODULE_EXPORT
#elif defined(_WIN32)
#define KB_ENGINE_MODULE_EXPORT __declspec(dllexport)
#else
#define KB_ENGINE_MODULE_EXPORT
#endif

namespace kb::modules {

inline constexpr std::uint32_t kEngineModuleAbiVersion = 1U;
inline constexpr const char* kEngineModuleCreateSymbol = "kb_create_engine_module";
inline constexpr const char* kEngineModuleDestroySymbol = "kb_destroy_engine_module";
inline constexpr const char* kEngineModuleAbiVersionSymbol = "kb_engine_module_abi_version";
inline constexpr const char* kEngineModuleNameSymbol = "kb_engine_module_name";

using EngineModuleAbiVersionFn = std::uint32_t (*)();
using EngineModuleNameFn = const char* (*)();
using CreateEngineModuleFn = IEngineModule* (*)();
using DestroyEngineModuleFn = void (*)(IEngineModule*);

} // namespace kb::modules
