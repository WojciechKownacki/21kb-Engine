#include "engine/library/EngineLibraryFunctionId.hpp"

namespace kb::library {

LibraryFunctionId ComputeLibraryFunctionId(std::string_view canonicalName) noexcept {
    // FNV-1a 64-bit — same algorithm as ComputeApiManifestHash
    // (EngineLibraryManifest.cpp) and kb::assets::MakeAssetId, kept as an
    // independent implementation here rather than a shared dependency:
    // this identifies a function within kb::library's own contract, not an
    // asset or a manifest blob.
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const unsigned char byte : canonicalName) {
        hash ^= byte;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

} // namespace kb::library
