#include "engine/library/EngineLibraryModule.hpp"

#include "engine/library/EngineLibraryModuleValidation.hpp"
#include "engine/script/ScriptAudioApi.hpp"
#include "engine/script/ScriptInputApi.hpp"
#include "engine/script/ScriptMathApi.hpp"
#include "engine/script/ScriptPhysicsApi.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptSceneApi.hpp"
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
        // LIB-071: added after the original seven — Scene.* tracks
        // loaded-document content (SceneState::loadedScenes) inside the
        // one live kb::scene::Scene, via SceneLoadedContentService.
        LibraryModuleDesc{
            .name = "Scene",
            .ownerRuntime = "kb::scene::SceneLoadedContentService",
            .Register = &kb::script::ScriptSceneApi::Register,
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
        // LIB-028: the startup report still names every module the catalog
        // was going to attempt — all "not installed", with the shared
        // validation failure as the reason — rather than being left empty
        // just because nothing individually failed to Register().
        result.report.reserve(modules.size());
        for (const LibraryModuleDesc& module : modules) {
            result.report.push_back(EngineLibraryModuleReportEntry{
                .name = module.name,
                .version = module.version,
                .ownerRuntime = module.ownerRuntime,
                .capability = module.capability,
                .installed = false,
                .reason = "module catalog failed validation",
            });
        }
        return result;
    }

    // RegisterFunction (called by each module below) mirrors every function
    // into the Lua function table and a Visual Graph CallNative node, so one
    // Register() call per module covers Native, Lua and Visual Graph.
    result.report.reserve(modules.size());
    for (const LibraryModuleDesc& module : modules) {
        EngineLibraryModuleReportEntry entry{
            .name = module.name,
            .version = module.version,
            .ownerRuntime = module.ownerRuntime,
            .capability = module.capability,
        };
        if (!module.capability) {
            entry.installed = false;
            entry.reason = module.disabledReason.empty() ? "capability unavailable in this build" : module.disabledReason;
            result.report.push_back(std::move(entry));
            continue;
        }
        if (module.Register == nullptr || !module.Register(host)) {
            entry.installed = false;
            entry.reason = "script API could not be fully registered";
            result.succeeded = false;
            result.diagnostics.emplace_back(module.name + " script API could not be fully registered");
        } else {
            entry.installed = true;
        }
        result.report.push_back(std::move(entry));
    }

    return result;
}

std::string FormatStartupReport(const std::vector<EngineLibraryModuleReportEntry>& report) {
    std::string text = "kb::library startup report (" + std::to_string(report.size()) + " module" + (report.size() == 1U ? "" : "s") + "):\n";
    for (const EngineLibraryModuleReportEntry& entry : report) {
        text += "  ";
        text += entry.installed ? "[installed] " : "[disabled]  ";
        text += entry.name;
        text += " v";
        text += std::to_string(entry.version.major);
        text += '.';
        text += std::to_string(entry.version.minor);
        text += '.';
        text += std::to_string(entry.version.patch);
        text += " (";
        text += entry.ownerRuntime;
        text += ')';
        if (!entry.installed) {
            text += " — ";
            text += entry.reason;
        }
        text += '\n';
    }
    return text;
}

} // namespace kb::library
