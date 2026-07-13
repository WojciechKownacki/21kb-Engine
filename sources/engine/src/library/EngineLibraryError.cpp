#include "engine/library/EngineLibraryError.hpp"

namespace kb::library {

std::string ToString(const ScriptError& error) {
    if (error.operation.empty()) {
        return error.message;
    }
    return error.operation + ": " + error.message;
}

} // namespace kb::library
