#include "engine/library/EngineLibraryModule.hpp"

#include "engine/library/EngineLibraryModuleValidation.hpp"
#include "engine/script/ScriptAudioApi.hpp"
#include "engine/script/ScriptInputApi.hpp"
#include "engine/script/ScriptMathApi.hpp"
#include "engine/script/ScriptPhysicsApi.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptTimeApi.hpp"
#include "engine/script/ScriptTransformApi.hpp"
#include "engine/script/ScriptWorldApi.hpp"

namespace kb::library {

const std::vector<LibraryModuleDesc>& EngineLibraryModule::Catalog() {
    // The kb::library domain module set, in the exact order
    // ScriptRuntimeHost::RegisterDefaultBackends() registered them before
    // this catalog existed. ownerRuntime names the real subsystem behind
    // each module's state (verified against the module's own .cpp, not
    // guessed): Input reads kb::input::InputSubsystem; Audio plays through
    // kb::audio::AudioPlayback; World creates/destroys through
    // kb::scene::SceneEntities; Time reads the per-dispatch delta already
    // carried on ScriptExecutionContext; Physics.Raycast today walks
    // kb::scene::SceneTransforms directly (no physics-engine query yet —
    // see LIB-123..134); Transform reads/writes kb::scene::SceneTransforms;
    // Math (LIB-045) has no backing runtime subsystem at all — every
    // Math.* function is a pure computation over its own arguments (see
    // kb::math::Clamp/Lerp/... in EngineMath.hpp), so ownerRuntime names
    // that fact explicitly rather than pointing at a scene/runtime type
    // that isn't actually involved. None of the seven depend on each
    // other at the registration level, so dependencies is empty for all
    // of them; capability is unconditionally true because every module's
    // Register() is compiled into this build.
    static const std::vector<LibraryModuleDesc> kCatalog{
        LibraryModuleDesc{
            .name = "Input",
            .ownerRuntime = "kb::input::InputSubsystem",
            .Register = &kb::script::ScriptInputApi::Register,
        },
        LibraryModuleDesc{
            .name = "Audio",
            .ownerRuntime = "kb::audio::AudioPlayback",
            .Register = &kb::script::ScriptAudioApi::Register,
        },
        LibraryModuleDesc{
            .name = "World",
            .ownerRuntime = "kb::scene::SceneEntities",
            .Register = &kb::script::ScriptWorldApi::Register,
            // Pilot for LIB-017: World.Exists (ScriptWorldApi.cpp) is a
            // pure query over kb::scene::SceneEntities::IsAlive — same
            // scene state and entity id always yield the same bool, it
            // never depends on wall time, and it produces no ScriptError
            // for a missing/invalid entity (it just returns false). The
            // rest of this module's functions are not yet audited; see
            // LibraryFunctionDesc's comment for what that means.
            .functions = {
                LibraryFunctionDesc{
                    .canonicalName = "World.Exists",
                    .threadAffinity = LibraryThreadAffinity::MainThread,
                    .determinism = LibraryDeterminism::Deterministic,
                    .canFail = false,
                },
            },
        },
        LibraryModuleDesc{
            .name = "Time",
            .ownerRuntime = "kb::script::ScriptExecutionContext",
            .Register = &kb::script::ScriptTimeApi::Register,
        },
        LibraryModuleDesc{
            .name = "Physics",
            .ownerRuntime = "kb::scene::SceneTransforms",
            .Register = &kb::script::ScriptPhysicsApi::Register,
        },
        LibraryModuleDesc{
            .name = "Transform",
            .ownerRuntime = "kb::scene::SceneTransforms",
            .Register = &kb::script::ScriptTransformApi::Register,
        },
        LibraryModuleDesc{
            .name = "Math",
            .ownerRuntime = "kb::math (stateless — pure functions, no backing runtime subsystem)",
            .Register = &kb::script::ScriptMathApi::Register,
        },
    };
    return kCatalog;
}

EngineLibraryModuleResult EngineLibraryModule::Install(kb::script::ScriptRuntimeHost& host) {
    return InstallModules(host, Catalog());
}

EngineLibraryModuleResult EngineLibraryModule::InstallModules(kb::script::ScriptRuntimeHost& host, std::span<const LibraryModuleDesc> modules) {
    EngineLibraryModuleResult result;

    // LIB-020: validate the catalog itself (duplicate/unknown module names,
    // dependency cycles, a function audited by two modules at once) before
    // registering anything. A catalog that fails validation registers
    // nothing rather than partially registering an inconsistent set.
    const ModuleCatalogValidationResult validation = ValidateModuleCatalog(modules);
    if (!validation.succeeded) {
        result.succeeded = false;
        result.diagnostics = validation.errors;
        return result;
    }

    // RegisterFunction (called by each module below) mirrors every function
    // into the Lua function table and a Visual Graph CallNative node, so one
    // Register() call per module covers Native, Lua and Visual Graph.
    for (const LibraryModuleDesc& module : modules) {
        if (!module.capability) {
            continue;
        }
        if (module.Register == nullptr || !module.Register(host)) {
            result.succeeded = false;
            result.diagnostics.emplace_back(module.name + " script API could not be fully registered");
        }
    }

    return result;
}

} // namespace kb::library
