#include "engine/library/EngineLibraryManifest.hpp"

#include "engine/script/ScriptApiExport.hpp"

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace kb::library {

std::string ComputeApiManifestHash(std::string_view content) noexcept {
    // FNV-1a 64-bit: simple, dependency-free, deterministic across
    // platforms/builds for identical input bytes.
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const unsigned char byte : content) {
        hash ^= byte;
        hash *= 0x100000001b3ULL;
    }
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

ApiManifest BuildApiManifest(const kb::script::ScriptApiCatalog& catalog) {
    return ApiManifest{
        .version = kEngineLibraryApiVersion,
        .manifestHash = ComputeApiManifestHash(kb::script::ScriptApiExport::ToJson(catalog)),
    };
}

std::string ToJson(const ApiManifest& manifest) {
    return "{\"version\":\"" + ToString(manifest.version) + "\",\"hash\":\"" + manifest.manifestHash + "\"}\n";
}

} // namespace kb::library
