#include "engine/library/EngineLibraryFunctionDesc.hpp"

namespace kb::library {

namespace {

template <typename Pin>
[[nodiscard]] bool PinsMatch(const std::vector<kb::script::ScriptApiPin>& recorded, const std::vector<Pin>& real) {
    if (recorded.size() != real.size()) {
        return false;
    }
    for (std::size_t i = 0; i < recorded.size(); ++i) {
        if (recorded[i].name != real[i].name || recorded[i].type != real[i].type || recorded[i].required != real[i].required) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool AffinityMatches(LibraryThreadAffinity recorded, kb::core::ExecutionAffinity real) {
    switch (recorded) {
    case LibraryThreadAffinity::MainThread: return real == kb::core::ExecutionAffinity::MainThread;
    case LibraryThreadAffinity::WorkerSafe: return real == kb::core::ExecutionAffinity::WorkerSafe;
    case LibraryThreadAffinity::RenderThreadCommand: return real == kb::core::ExecutionAffinity::RenderCommand;
    }
    return false;
}

} // namespace

bool FunctionDescMatchesSignature(const LibraryFunctionDesc& desc, const kb::script::ScriptFunctionSignature& signature) {
    return desc.canonicalName == signature.name && AffinityMatches(desc.threadAffinity, signature.executionAffinity) &&
        PinsMatch(desc.inputs, signature.inputs) && PinsMatch(desc.outputs, signature.outputs);
}

bool FunctionDescMatchesCatalog(const LibraryFunctionDesc& desc, const kb::script::ScriptApiCatalog& catalog) {
    const kb::script::ScriptApiCatalogFunction* function = catalog.FindFunction(desc.canonicalName);
    if (function == nullptr) {
        return false;
    }
    return AffinityMatches(desc.threadAffinity, function->executionAffinity) && PinsMatch(desc.inputs, function->inputs) && PinsMatch(desc.outputs, function->outputs);
}

} // namespace kb::library
