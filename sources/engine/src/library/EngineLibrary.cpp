#include "engine/library/EngineLibrary.hpp"

namespace kb::library {

std::string ToString(const LibraryApiVersion& version) {
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
        std::to_string(version.patch);
}

} // namespace kb::library
