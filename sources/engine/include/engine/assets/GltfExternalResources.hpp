#pragma once

#include "engine/assets/ImportedAsset.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct cgltf_data;
struct cgltf_options;

namespace kb::assets {

// The file a glTF document references by URI (a buffer or an image), as a path relative to the
// document's directory. Accepted: a percent-encoded relative reference made of plain path
// segments ("." segments are dropped). Refused: any scheme ("file:", "http:", ...), an absolute
// path, a drive or UNC root, a backslash, a query or fragment, an empty or ".." segment, and
// anything that decodes to a NUL, a colon or a backslash.
// Data URIs are not file references; callers decode them separately.
[[nodiscard]] std::optional<std::filesystem::path> GltfRelativeResourcePath(std::string_view uri);

// The file `uri` names under `sourceDirectory` (an image of the document, for instance). Fails on
// a URI GltfRelativeResourcePath refuses and on an existing file that resolves outside the
// directory; a file that does not exist is returned as its path, for the caller to report.
[[nodiscard]] std::optional<std::filesystem::path> ResolveGltfResourceFile(
    const std::filesystem::path& sourceDirectory,
    std::string_view uri,
    std::string* error);

// Loads every buffer of a parsed glTF document. A buffer given by a relative file URI is taken
// from `embedded` (matched by its normalized relative path, as written by an import) or else
// read from `sourceDirectory`; with an empty `sourceDirectory` only embedded buffers resolve.
// A file that resolves outside `sourceDirectory` (through a link, for instance) is refused.
// GLB binary chunks and base64 data URIs load as cgltf loads them.
[[nodiscard]] bool LoadGltfBuffers(
    const cgltf_options& options,
    cgltf_data& data,
    const std::filesystem::path& sourceDirectory,
    std::span<const ImportedAssetResource> embedded,
    std::string* error);

// The external buffer files a glTF document (JSON or GLB) references, read from
// `sourceDirectory`, so an import can carry them with the document. Empty when every buffer is
// embedded. Fails on an unsafe URI or a missing or short file.
[[nodiscard]] std::optional<std::vector<ImportedAssetResource>> CollectGltfExternalBuffers(
    std::span<const std::uint8_t> documentBytes,
    const std::filesystem::path& sourceDirectory,
    std::string* error);

// The material slot name of material `materialIndex`: its own name, or for an unnamed material
// "Material_<index>" -- extended with "_unnamed" for as long as another material of the document
// is explicitly called that -- so every material keeps a distinct name that stays the same for
// the same document.
[[nodiscard]] std::string GltfMaterialSlotName(const cgltf_data& data, std::size_t materialIndex);

} // namespace kb::assets
