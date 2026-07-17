#include "engine/library/EngineLibraryModuleValidation.hpp"

#include <cstdint>
#include <unordered_map>
#include <utility>

namespace kb::library {

namespace {

enum class VisitState : std::uint8_t { Unvisited, Visiting, Visited };

bool DetectCycle(
    const std::string& name,
    const std::unordered_map<std::string, const LibraryModuleDesc*>& byName,
    std::unordered_map<std::string, VisitState>& state,
    std::vector<std::string>& errors) {
    VisitState& current = state[name];
    if (current == VisitState::Visiting) {
        errors.push_back("module dependency cycle detected at '" + name + "'");
        return true;
    }
    if (current == VisitState::Visited) {
        return false;
    }
    current = VisitState::Visiting;
    const auto iter = byName.find(name);
    if (iter != byName.end()) {
        for (const std::string& dependency : iter->second->dependencies) {
            if (byName.contains(dependency) && DetectCycle(dependency, byName, state, errors)) {
                state[name] = VisitState::Visited;
                return true;
            }
        }
    }
    state[name] = VisitState::Visited;
    return false;
}

} // namespace

ModuleCatalogValidationResult ValidateModuleCatalog(std::span<const LibraryModuleDesc> modules) {
    ModuleCatalogValidationResult result;

    std::unordered_map<std::string, const LibraryModuleDesc*> byName;
    for (const LibraryModuleDesc& module : modules) {
        if (module.name.empty()) {
            result.succeeded = false;
            result.errors.emplace_back("module catalog entry has an empty name");
            continue;
        }
        if (!byName.emplace(module.name, &module).second) {
            result.succeeded = false;
            result.errors.push_back("module catalog has a duplicate module name '" + module.name + "'");
        }
    }

    for (const LibraryModuleDesc& module : modules) {
        for (const std::string& dependency : module.dependencies) {
            if (!byName.contains(dependency)) {
                result.succeeded = false;
                result.errors.push_back("module '" + module.name + "' depends on unknown module '" + dependency + "'");
            }
        }
    }

    std::unordered_map<std::string, VisitState> visitState;
    for (const LibraryModuleDesc& module : modules) {
        if (module.name.empty() || visitState[module.name] != VisitState::Unvisited) {
            continue;
        }
        std::vector<std::string> cycleErrors;
        if (DetectCycle(module.name, byName, visitState, cycleErrors)) {
            result.succeeded = false;
            for (std::string& error : cycleErrors) {
                result.errors.push_back(std::move(error));
            }
        }
    }

    std::unordered_map<std::string, std::string> functionOwner;
    for (const LibraryModuleDesc& module : modules) {
        const std::string expectedPrefix = module.name + ".";
        for (const LibraryFunctionDesc& function : module.functions) {
            const auto [iter, inserted] = functionOwner.emplace(function.canonicalName, module.name);
            if (!inserted && iter->second != module.name) {
                result.succeeded = false;
                result.errors.push_back(
                    "function '" + function.canonicalName + "' is audited by both module '" + iter->second + "' and '" + module.name + "'");
            }
            // A LibraryFunctionDesc lives inside the module that audited it,
            // but nothing about std::vector membership actually ties its
            // canonicalName to that module's own namespace prefix — a
            // copy-pasted entry (LIB-003's catalog is hand-authored) could
            // silently attribute e.g. "Physics.Raycast" to the "World"
            // module. Catch that as a catalog-integrity error, the same way
            // the duplicate-ownership check above catches two modules
            // claiming the same function.
            if (function.canonicalName.compare(0, expectedPrefix.size(), expectedPrefix) != 0) {
                result.succeeded = false;
                result.errors.push_back(
                    "function '" + function.canonicalName + "' is audited by module '" + module.name +
                    "' but its name is not prefixed with '" + expectedPrefix + "'");
            }
        }
    }

    return result;
}

} // namespace kb::library
