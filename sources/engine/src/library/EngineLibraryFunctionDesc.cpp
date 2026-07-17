#include "engine/library/EngineLibraryFunctionDesc.hpp"

namespace kb::library {

namespace {

[[nodiscard]] bool PinsMatch(const std::vector<kb::script::ScriptApiPin>& recorded, const std::vector<kb::script::ScriptApiPin>& real) {
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

} // namespace

bool FunctionDescMatchesCatalog(const LibraryFunctionDesc& desc, const kb::script::ScriptApiCatalog& catalog) {
    const kb::script::ScriptApiCatalogFunction* function = catalog.FindFunction(desc.canonicalName);
    if (function == nullptr) {
        return false;
    }
    return PinsMatch(desc.inputs, function->inputs) && PinsMatch(desc.outputs, function->outputs);
}

} // namespace kb::library
