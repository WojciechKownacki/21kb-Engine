#include "engine/library/EngineLibraryOwnership.hpp"

namespace kb::library {

const char* ToString(LibraryOwnership ownership) noexcept {
    switch (ownership) {
        case LibraryOwnership::Owned:
            return "Owned";
        case LibraryOwnership::Borrowed:
            return "Borrowed";
        case LibraryOwnership::Shared:
            return "Shared";
        case LibraryOwnership::Weak:
            return "Weak";
    }
    return "Unknown";
}

} // namespace kb::library
