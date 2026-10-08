#include "engine/assets/GltfExternalResources.hpp"

#include <cgltf/cgltf.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <system_error>
#include <utility>

namespace kb::assets {
namespace {

// A glTF buffer is addressed with 32-bit offsets by every importer here; nothing larger is read.
constexpr std::uintmax_t kMaximumExternalBufferBytes = std::uintmax_t{ 1U } << 31U;

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

[[nodiscard]] std::optional<unsigned char> HexDigit(char value) noexcept {
    if (value >= '0' && value <= '9') return static_cast<unsigned char>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<unsigned char>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return static_cast<unsigned char>(value - 'A' + 10);
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> PercentDecode(std::string_view text) {
    std::string output;
    output.reserve(text.size());
    for (std::size_t index = 0U; index < text.size(); ++index) {
        if (text[index] != '%') {
            output.push_back(text[index]);
            continue;
        }
        if (index + 2U >= text.size()) return std::nullopt;
        const auto high = HexDigit(text[index + 1U]);
        const auto low = HexDigit(text[index + 2U]);
        if (!high || !low) return std::nullopt;
        output.push_back(static_cast<char>((*high << 4U) | *low));
        index += 2U;
    }
    return output;
}

[[nodiscard]] bool IsDataUri(std::string_view uri) noexcept {
    return uri.size() >= 5U && uri.substr(0U, 5U) == "data:";
}

// `relative` (already checked by GltfRelativeResourcePath) under `directory`, provided the file
// it resolves to -- after links -- still lies inside `directory`.
[[nodiscard]] std::optional<std::filesystem::path> ContainedFile(
    const std::filesystem::path& directory,
    const std::filesystem::path& relative,
    std::string* error) {
    std::error_code fileError;
    const std::filesystem::path base = std::filesystem::weakly_canonical(directory, fileError);
    if (fileError) {
        Fail(error, "glTF source directory could not be resolved.");
        return std::nullopt;
    }
    const std::filesystem::path target = std::filesystem::weakly_canonical(directory / relative, fileError);
    if (fileError) {
        Fail(error, "glTF resource could not be resolved: " + relative.generic_string());
        return std::nullopt;
    }
    const std::filesystem::path inside = target.lexically_relative(base);
    if (inside.empty() || inside.is_absolute() || *inside.begin() == "..") {
        Fail(error, "glTF resource resolves outside the source directory: " + relative.generic_string());
        return std::nullopt;
    }
    if (!std::filesystem::is_regular_file(target, fileError) || fileError) {
        Fail(error, "glTF resource file does not exist: " + relative.generic_string());
        return std::nullopt;
    }
    return target;
}

[[nodiscard]] std::optional<std::vector<std::byte>> ReadContainedFile(
    const std::filesystem::path& directory,
    const std::filesystem::path& relative,
    std::uintmax_t minimumBytes,
    std::string* error) {
    const std::optional<std::filesystem::path> file = ContainedFile(directory, relative, error);
    if (!file) return std::nullopt;
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(*file, sizeError);
    if (sizeError || size > kMaximumExternalBufferBytes) {
        Fail(error, "glTF resource file is too large to read: " + relative.generic_string());
        return std::nullopt;
    }
    if (size < minimumBytes) {
        Fail(error, "glTF resource file is shorter than the buffer it backs: " + relative.generic_string());
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    std::ifstream input{ *file, std::ios::binary };
    if (!input.is_open() ||
        !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        Fail(error, "glTF resource file could not be read: " + relative.generic_string());
        return std::nullopt;
    }
    return bytes;
}

struct ParsedGltfDeleter {
    void operator()(cgltf_data* data) const noexcept { cgltf_free(data); }
};

} // namespace

std::optional<std::filesystem::path> GltfRelativeResourcePath(std::string_view uri) {
    if (uri.empty() || IsDataUri(uri)) return std::nullopt;
    // A colon is a scheme ("file:", "http:"), a drive ("C:") or an NTFS stream; a backslash is a
    // Windows separator the URI syntax does not have; '?' and '#' start a query or fragment.
    if (uri.find_first_of(":\\?#") != std::string_view::npos) return std::nullopt;
    const std::optional<std::string> decoded = PercentDecode(uri);
    if (!decoded || decoded->empty() || decoded->front() == '/' ||
        decoded->find_first_of(std::string_view{ ":\\\0", 3U }) != std::string::npos) {
        return std::nullopt;
    }
    std::filesystem::path path;
    std::string_view rest{ *decoded };
    while (true) {
        const std::size_t slash = rest.find('/');
        const std::string_view segment = rest.substr(0U, slash);
        if (segment.empty() || segment == "..") return std::nullopt;
        if (segment != ".") path /= std::filesystem::path{ std::string{ segment } };
        if (slash == std::string_view::npos) break;
        rest.remove_prefix(slash + 1U);
    }
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return std::nullopt;
    return path;
}

std::optional<std::filesystem::path> ResolveGltfResourceFile(
    const std::filesystem::path& sourceDirectory,
    std::string_view uri,
    std::string* error) {
    const std::optional<std::filesystem::path> relative = GltfRelativeResourcePath(uri);
    if (!relative) {
        Fail(error, "glTF resource URI is not a relative file inside the source directory.");
        return std::nullopt;
    }
    const std::filesystem::path candidate = (sourceDirectory / *relative).lexically_normal();
    std::error_code existsError;
    if (!std::filesystem::exists(candidate, existsError) || existsError) {
        return candidate;
    }
    if (!ContainedFile(sourceDirectory, *relative, error)) {
        return std::nullopt;
    }
    return candidate;
}

bool LoadGltfBuffers(
    const cgltf_options& options,
    cgltf_data& data,
    const std::filesystem::path& sourceDirectory,
    std::span<const ImportedAssetResource> embedded,
    std::string* error) {
    for (cgltf_size index = 0U; index < data.buffers_count; ++index) {
        cgltf_buffer& buffer = data.buffers[index];
        if (buffer.data != nullptr || buffer.uri == nullptr || IsDataUri(buffer.uri)) continue;
        const std::optional<std::filesystem::path> relative = GltfRelativeResourcePath(buffer.uri);
        if (!relative) return Fail(error, "glTF buffer URI is not a relative file inside the source directory.");
        const std::string key = relative->generic_string();
        std::optional<std::vector<std::byte>> fileBytes;
        std::span<const std::byte> bytes;
        const auto carried = std::ranges::find_if(embedded, [&key](const ImportedAssetResource& resource) {
            return resource.uri == key;
        });
        if (carried != embedded.end()) {
            bytes = carried->bytes;
        } else if (!sourceDirectory.empty()) {
            fileBytes = ReadContainedFile(sourceDirectory, *relative, buffer.size, error);
            if (!fileBytes) return false;
            bytes = *fileBytes;
        } else {
            return Fail(error, "glTF buffer is neither embedded nor resolvable from a source directory: " + key);
        }
        if (bytes.size() < buffer.size) return Fail(error, "glTF buffer data is shorter than its declared size: " + key);
        // cgltf_free releases the block through the document's allocator.
        void* const memory = data.memory.alloc_func(data.memory.user_data, buffer.size == 0U ? 1U : buffer.size);
        if (memory == nullptr) return Fail(error, "glTF buffer could not be allocated: " + key);
        if (buffer.size != 0U) std::memcpy(memory, bytes.data(), buffer.size);
        buffer.data = memory;
        buffer.data_free_method = cgltf_data_free_method_memory_free;
    }
    // Every file buffer is loaded; what remains is the GLB chunk and data URIs, which need no path.
    if (cgltf_load_buffers(&options, &data, nullptr) != cgltf_result_success) {
        return Fail(error, "glTF buffers could not be loaded.");
    }
    return true;
}

std::optional<std::vector<ImportedAssetResource>> CollectGltfExternalBuffers(
    std::span<const std::uint8_t> documentBytes,
    const std::filesystem::path& sourceDirectory,
    std::string* error) {
    if (documentBytes.empty()) {
        Fail(error, "glTF document is empty.");
        return std::nullopt;
    }
    cgltf_options options{};
    cgltf_data* raw = nullptr;
    if (cgltf_parse(&options, documentBytes.data(), documentBytes.size(), &raw) != cgltf_result_success || raw == nullptr) {
        Fail(error, "glTF document could not be parsed.");
        return std::nullopt;
    }
    const std::unique_ptr<cgltf_data, ParsedGltfDeleter> data{ raw };
    std::vector<ImportedAssetResource> resources;
    for (cgltf_size index = 0U; index < data->buffers_count; ++index) {
        const cgltf_buffer& buffer = data->buffers[index];
        if (buffer.uri == nullptr || IsDataUri(buffer.uri)) continue;
        const std::optional<std::filesystem::path> relative = GltfRelativeResourcePath(buffer.uri);
        if (!relative) {
            Fail(error, "glTF buffer URI is not a relative file inside the source directory.");
            return std::nullopt;
        }
        const std::string key = relative->generic_string();
        const auto existing = std::ranges::find_if(resources, [&key](const ImportedAssetResource& resource) {
            return resource.uri == key;
        });
        if (existing != resources.end()) {
            if (existing->bytes.size() < buffer.size) {
                Fail(error, "glTF resource file is shorter than the buffer it backs: " + key);
                return std::nullopt;
            }
            continue;
        }
        std::optional<std::vector<std::byte>> bytes = ReadContainedFile(sourceDirectory, *relative, buffer.size, error);
        if (!bytes) return std::nullopt;
        resources.push_back(ImportedAssetResource{ .uri = key, .bytes = std::move(*bytes) });
    }
    return resources;
}

std::string GltfMaterialSlotName(const cgltf_data& data, std::size_t materialIndex) {
    if (materialIndex >= data.materials_count) return {};
    const cgltf_material& material = data.materials[materialIndex];
    if (material.name != nullptr && material.name[0] != '\0') return std::string{ material.name };
    const auto namedExplicitly = [&data](std::string_view candidate) {
        for (cgltf_size index = 0U; index < data.materials_count; ++index) {
            const char* const name = data.materials[index].name;
            if (name != nullptr && candidate == name) return true;
        }
        return false;
    };
    std::string name = "Material_" + std::to_string(materialIndex);
    while (namedExplicitly(name)) name += "_unnamed";
    return name;
}

} // namespace kb::assets
