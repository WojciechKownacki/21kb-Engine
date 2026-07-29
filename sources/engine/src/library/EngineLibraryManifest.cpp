#include "engine/library/EngineLibraryManifest.hpp"

#include "engine/script/ScriptApiExport.hpp"

#include <cstdint>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>

namespace kb::library {
namespace {

void AppendSpecialApisJson(std::string& json, const std::vector<LibraryApiSurfaceManifestEntry>& apis) {
    json += "\"specialApis\":[";
    for (std::size_t index = 0; index < apis.size(); ++index) {
        const LibraryApiSurfaceManifestEntry& api = apis[index];
        if (index != 0U) {
            json += ',';
        }
        json += "{\"name\":\"" + std::string{ api.canonicalName } + "\",\"availability\":" + std::to_string(api.availability) +
            ",\"description\":\"" + std::string{ api.description } + "\"}";
    }
    json += ']';
}

} // namespace

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
    ApiManifest manifest{
        .version = kEngineLibraryApiVersion,
        .specialApis = { kLibrarySpecialApiSurfaces.begin(), kLibrarySpecialApiSurfaces.end() },
    };
    std::string hashContent = kb::script::ScriptApiExport::ToJson(catalog);
    AppendSpecialApisJson(hashContent, manifest.specialApis);
    manifest.manifestHash = ComputeApiManifestHash(hashContent);
    return manifest;
}

std::string ToJson(const ApiManifest& manifest) {
    std::string json = "{\"version\":\"" + ToString(manifest.version) + "\",\"hash\":\"" + manifest.manifestHash + "\",";
    AppendSpecialApisJson(json, manifest.specialApis);
    json += "}\n";
    return json;
}

} // namespace kb::library
