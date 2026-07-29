#include "engine/library/EngineLibraryManifest.hpp"

#include "engine/script/ScriptApiExport.hpp"

#include <cstdint>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>

namespace kb::library {
namespace {

[[nodiscard]] std::string MarkdownPinList(const std::vector<kb::script::ScriptApiPin>& pins) {
    if (pins.empty()) {
        return "—";
    }
    std::string text;
    for (const kb::script::ScriptApiPin& pin : pins) {
        if (!text.empty()) {
            text += ", ";
        }
        text += pin.name + ": " + kb::script::ToString(pin.type);
        if (!pin.required) {
            text += '?';
        }
    }
    return text;
}

[[nodiscard]] std::string MarkdownFunctionRow(const kb::script::ScriptApiCatalogFunction& function) {
    return "| `" + function.name + "` | " + function.description + " | " + MarkdownPinList(function.inputs) + " | " +
        MarkdownPinList(function.outputs) + " |";
}

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
        .catalog = catalog,
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

std::string ToReferenceMarkdown(const ApiManifest& manifest) {
    std::string markdown = kb::script::ScriptApiExport::ToMarkdown(manifest.catalog);
    markdown += "## API manifest\n\n";
    markdown += "- Version: `" + ToString(manifest.version) + "`\n";
    markdown += "- Hash: `" + manifest.manifestHash + "`\n";
    if (!manifest.specialApis.empty()) {
        markdown += "- Restricted APIs:\n";
        for (const LibraryApiSurfaceManifestEntry& api : manifest.specialApis) {
            markdown += "  - `" + std::string{ api.canonicalName } + "` — " + std::string{ api.description } + "\n";
        }
    }
    markdown += '\n';
    return markdown;
}

ApiReferenceValidationResult ValidateReferenceMarkdown(const ApiManifest& manifest, std::string_view markdown) {
    ApiReferenceValidationResult result;
    const std::string manifestVersion = "- Version: `" + ToString(manifest.version) + "`";
    if (markdown.find(manifestVersion) == std::string_view::npos) {
        result.errors.push_back("reference is missing the manifest API version");
    }
    const std::string manifestHash = "- Hash: `" + manifest.manifestHash + "`";
    if (markdown.find(manifestHash) == std::string_view::npos) {
        result.errors.push_back("reference is missing the manifest hash");
    }
    for (const kb::script::ScriptApiCatalogFunction& function : manifest.catalog.functions) {
        if (markdown.find(MarkdownFunctionRow(function)) == std::string_view::npos) {
            result.errors.push_back("reference function row diverged from manifest: " + function.name);
        }
        const std::string anchor = "<a id=\"" + kb::script::ScriptApiDocumentationAnchor(function.name) + "\"></a>";
        if (markdown.find(anchor) == std::string_view::npos) {
            result.errors.push_back("reference function anchor diverged from manifest: " + function.name);
        }
    }
    return result;
}

} // namespace kb::library
