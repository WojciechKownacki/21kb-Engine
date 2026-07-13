#include "engine/library/EngineLibraryLifecycle.hpp"

namespace kb::library {

const char* ToString(LibraryLifecycleContextKind kind) noexcept {
    switch (kind) {
        case LibraryLifecycleContextKind::Behaviour:
            return "Behaviour";
        case LibraryLifecycleContextKind::Fixed:
            return "Fixed";
        case LibraryLifecycleContextKind::Frame:
            return "Frame";
        case LibraryLifecycleContextKind::Render:
            return "Render";
    }
    return "Unknown";
}

} // namespace kb::library
