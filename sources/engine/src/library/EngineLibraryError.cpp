#include "engine/library/EngineLibraryError.hpp"

namespace kb::library {

const char* ToString(LibraryErrorCode code) noexcept {
    switch (code) {
        case LibraryErrorCode::InvalidHandle:
            return "InvalidHandle";
        case LibraryErrorCode::InactiveWorld:
            return "InactiveWorld";
        case LibraryErrorCode::UnavailableCapability:
            return "UnavailableCapability";
        case LibraryErrorCode::Permission:
            return "Permission";
        case LibraryErrorCode::InvalidArgument:
            return "InvalidArgument";
        case LibraryErrorCode::Timeout:
            return "Timeout";
    }
    return "InvalidArgument";
}

std::string ToString(const ScriptError& error) {
    const std::string prefix = std::string{ "[" } + ToString(error.code) + "] ";
    if (error.operation.empty()) {
        return prefix + error.message;
    }
    return prefix + error.operation + ": " + error.message;
}

} // namespace kb::library
