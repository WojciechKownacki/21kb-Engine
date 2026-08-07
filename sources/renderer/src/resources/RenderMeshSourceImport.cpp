#include "kb/render/resources/RenderMeshSourceImport.hpp"

#include "resources/RenderMeshGltfMaterialImporter.hpp"

#include <cgltf/cgltf.h>
#include <ufbx.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace kb::render {
namespace {

template <typename T>
[[nodiscard]] std::optional<T> Fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return std::nullopt;
}

[[nodiscard]] std::string LowerExtension(std::filesystem::path path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

[[nodiscard]] std::string Sanitize(std::string_view text, std::string_view fallback) {
    std::string output;
    output.reserve(text.size());
    for (char character : text) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (std::isalnum(value) || character == '-' || character == '_') {
            output.push_back(character);
        } else if (!output.empty() && output.back() != '_') {
            output.push_back('_');
        }
    }
    while (!output.empty() && output.back() == '_') output.pop_back();
    return output.empty() ? std::string{ fallback } : output;
}

[[nodiscard]] std::uint64_t StableHash(std::string_view text) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char character : text) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] std::string Hex8(std::uint64_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string output(8U, '0');
    for (std::size_t index = 0U; index < output.size(); ++index) {
        output[output.size() - index - 1U] = digits[value & 0xFU];
        value >>= 4U;
    }
    return output;
}

[[nodiscard]] bool IsSafeRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name()) return false;
    for (const std::filesystem::path& part : path) {
        if (part.empty() || part == "." || part == "..") return false;
    }
    return true;
}

[[nodiscard]] std::optional<char> HexByte(char high, char low) noexcept {
    const auto digit = [](char value) -> std::optional<unsigned char> {
        if (value >= '0' && value <= '9') return static_cast<unsigned char>(value - '0');
        if (value >= 'a' && value <= 'f') return static_cast<unsigned char>(value - 'a' + 10);
        if (value >= 'A' && value <= 'F') return static_cast<unsigned char>(value - 'A' + 10);
        return std::nullopt;
    };
    const auto upper = digit(high), lower = digit(low);
    if (!upper || !lower) return std::nullopt;
    return static_cast<char>((*upper << 4U) | *lower);
}

[[nodiscard]] std::optional<std::string> DecodeUriPath(std::string_view uri) {
    std::string output;
    output.reserve(uri.size());
    for (std::size_t index = 0U; index < uri.size(); ++index) {
        if (uri[index] != '%') {
            output.push_back(uri[index]);
            continue;
        }
        if (index + 2U >= uri.size()) return std::nullopt;
        const auto decoded = HexByte(uri[index + 1U], uri[index + 2U]);
        if (!decoded) return std::nullopt;
        output.push_back(*decoded);
        index += 2U;
    }
    return output;
}

[[nodiscard]] std::optional<std::vector<std::byte>> DecodeBase64(std::string_view text) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<std::byte> output;
    output.reserve((text.size() / 4U) * 3U);
    std::uint32_t accumulator = 0U;
    int bits = 0;
    for (char character : text) {
        if (character == '=') break;
        if (std::isspace(static_cast<unsigned char>(character))) continue;
        const std::size_t value = alphabet.find(character);
        if (value == std::string_view::npos) return std::nullopt;
        accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<std::byte>((accumulator >> bits) & 0xFFU));
        }
    }
    return output.empty() ? std::nullopt : std::optional<std::vector<std::byte>>{ std::move(output) };
}

[[nodiscard]] std::string ExtensionForMime(std::string_view mime) {
    if (mime == "image/png") return ".png";
    if (mime == "image/jpeg") return ".jpg";
    if (mime == "image/bmp") return ".bmp";
    if (mime == "image/tga" || mime == "image/x-tga") return ".tga";
    if (mime == "image/vnd-ms.dds") return ".dds";
    if (mime == "image/ktx") return ".ktx";
    return {};
}

struct GltfDeleter { void operator()(cgltf_data* data) const noexcept { cgltf_free(data); } };
using GltfData = std::unique_ptr<cgltf_data, GltfDeleter>;

[[nodiscard]] std::string GltfImageKey(const cgltf_image& image, std::size_t index) {
    if (image.uri != nullptr && !std::string_view{ image.uri }.starts_with("data:")) {
        const auto decoded = DecodeUriPath(image.uri);
        if (decoded && IsSafeRelativePath(std::filesystem::path{ *decoded })) return *decoded;
    }
    std::string extension = image.mime_type == nullptr ? std::string{} : ExtensionForMime(image.mime_type);
    if (extension.empty()) extension = ".png";
    const std::string name = image.name == nullptr ? "Image" : Sanitize(image.name, "Image");
    return name + "_" + std::to_string(index) + extension;
}

[[nodiscard]] const cgltf_image* ImageOf(const cgltf_texture_view& view) noexcept {
    return view.texture == nullptr ? nullptr : view.texture->image;
}

void SetGltfMaterialTexturePaths(
    RenderMeshEmbeddedMaterial& output,
    const cgltf_material& material,
    const cgltf_data& data) {
    const auto key = [&](const cgltf_texture_view& view) -> std::string {
        const cgltf_image* image = ImageOf(view);
        if (image == nullptr) return {};
        const std::ptrdiff_t index = image - data.images;
        return index < 0 || static_cast<cgltf_size>(index) >= data.images_count
            ? std::string{}
            : GltfImageKey(*image, static_cast<std::size_t>(index));
    };
    output.albedoTexturePath = key(material.pbr_metallic_roughness.base_color_texture);
    output.normalTexturePath = key(material.normal_texture);
    output.metallicRoughnessTexturePath = key(material.pbr_metallic_roughness.metallic_roughness_texture);
    output.occlusionTexturePath = key(material.occlusion_texture);
    output.emissiveTexturePath = key(material.emissive_texture);
}

[[nodiscard]] std::optional<RenderMeshSourceTexture> GltfTexture(
    const cgltf_image& image,
    std::size_t index,
    const std::filesystem::path& sourcePath,
    std::string* error) {
    RenderMeshSourceTexture texture{};
    texture.key = GltfImageKey(image, index);
    texture.extension = LowerExtension(texture.key);
    if (image.buffer_view != nullptr && image.buffer_view->buffer != nullptr &&
        image.buffer_view->buffer->data != nullptr && image.buffer_view->size != 0U) {
        const auto* bytes = static_cast<const std::byte*>(image.buffer_view->buffer->data) + image.buffer_view->offset;
        texture.embeddedBytes.assign(bytes, bytes + image.buffer_view->size);
        return texture;
    }
    if (image.uri == nullptr) return Fail<RenderMeshSourceTexture>(error,
        "glTF image has neither a URI nor embedded buffer data.");
    const std::string_view uri{ image.uri };
    if (uri.starts_with("data:")) {
        const std::size_t comma = uri.find(',');
        if (comma == std::string_view::npos || uri.substr(0U, comma).find(";base64") == std::string_view::npos) {
            return Fail<RenderMeshSourceTexture>(error, "glTF image data URI is not base64 encoded.");
        }
        auto decoded = DecodeBase64(uri.substr(comma + 1U));
        if (!decoded) return Fail<RenderMeshSourceTexture>(error, "glTF image data URI is invalid.");
        texture.embeddedBytes = std::move(*decoded);
        return texture;
    }
    const auto decoded = DecodeUriPath(uri);
    if (!decoded || !IsSafeRelativePath(std::filesystem::path{ *decoded })) {
        return Fail<RenderMeshSourceTexture>(error, "glTF image URI is not a safe relative path.");
    }
    texture.sourcePath = (sourcePath.parent_path() / std::filesystem::path{ *decoded }).lexically_normal();
    return texture;
}

[[nodiscard]] std::optional<RenderMeshSourceImportManifest> InspectGltf(
    const std::filesystem::path& sourcePath,
    std::string* error) {
    cgltf_options options{};
    cgltf_data* raw = nullptr;
    if (cgltf_parse_file(&options, sourcePath.string().c_str(), &raw) != cgltf_result_success || raw == nullptr) {
        return Fail<RenderMeshSourceImportManifest>(error, "Mesh material inspection could not parse glTF source.");
    }
    GltfData data{ raw };
    if (cgltf_load_buffers(&options, data.get(), sourcePath.string().c_str()) != cgltf_result_success) {
        return Fail<RenderMeshSourceImportManifest>(error, "Mesh material inspection could not load glTF buffers.");
    }
    RenderMeshSourceImportManifest manifest{};
    manifest.materials.reserve(data->materials_count);
    for (cgltf_size index = 0U; index < data->materials_count; ++index) {
        const cgltf_material& material = data->materials[index];
        const std::string name = material.name == nullptr || material.name[0] == '\0'
            ? "Material_" + std::to_string(index)
            : std::string{ material.name };
        RenderMeshEmbeddedMaterial embedded = RenderMeshGltfMaterialImporter::BuildEmbeddedMaterial(name, &material);
        SetGltfMaterialTexturePaths(embedded, material, *data);
        manifest.materials.push_back(std::move(embedded));
    }
    manifest.textures.reserve(data->images_count);
    for (cgltf_size index = 0U; index < data->images_count; ++index) {
        auto texture = GltfTexture(data->images[index], static_cast<std::size_t>(index), sourcePath, error);
        if (!texture) return std::nullopt;
        manifest.textures.push_back(std::move(*texture));
    }
    return manifest;
}

struct FbxDeleter { void operator()(ufbx_scene* scene) const noexcept { ufbx_free_scene(scene); } };
using FbxScene = std::unique_ptr<ufbx_scene, FbxDeleter>;

[[nodiscard]] std::string UfbxString(ufbx_string value) {
    return value.data == nullptr ? std::string{} : std::string{ value.data, value.length };
}

[[nodiscard]] const ufbx_texture* FileTexture(const ufbx_material_map& map) noexcept {
    if (!map.texture_enabled || map.texture == nullptr || map.texture->file_textures.count == 0U) return nullptr;
    return map.texture->file_textures.data[0];
}

[[nodiscard]] std::string FbxTextureKey(const ufbx_texture& texture) {
    std::filesystem::path filename{ UfbxString(texture.relative_filename) };
    if (filename.filename().empty()) filename = std::filesystem::path{ UfbxString(texture.filename) }.filename();
    std::string extension = LowerExtension(filename);
    if (extension.empty()) extension = ".png";
    const std::string stem = Sanitize(filename.stem().string(), Sanitize(UfbxString(texture.name), "Texture"));
    return stem + "_" + std::to_string(texture.typed_id) + extension;
}

[[nodiscard]] std::string TextureKey(const ufbx_material_map& map) {
    const ufbx_texture* texture = FileTexture(map);
    return texture == nullptr ? std::string{} : FbxTextureKey(*texture);
}

[[nodiscard]] RenderMeshEmbeddedMaterial FbxMaterial(const ufbx_material& material, std::string* error) {
    RenderMeshEmbeddedMaterial output{};
    output.name = UfbxString(material.name);
    if (output.name.empty()) output.name = "Material_" + std::to_string(material.typed_id);
    const ufbx_material_map& base = material.pbr.base_color.has_value
        ? material.pbr.base_color : material.fbx.diffuse_color;
    if (base.has_value) {
        output.desc.baseColor[0] = static_cast<float>(base.value_vec4.x);
        output.desc.baseColor[1] = static_cast<float>(base.value_vec4.y);
        output.desc.baseColor[2] = static_cast<float>(base.value_vec4.z);
        if (base.value_components >= 4U) output.desc.baseColor[3] = static_cast<float>(base.value_vec4.w);
    }
    if (material.pbr.metalness.has_value) output.desc.metallicFactor = static_cast<float>(material.pbr.metalness.value_real);
    if (material.pbr.roughness.has_value) output.desc.roughnessFactor = static_cast<float>(material.pbr.roughness.value_real);
    if (material.pbr.emission_color.has_value) {
        output.desc.emissiveColor[0] = static_cast<float>(material.pbr.emission_color.value_vec3.x);
        output.desc.emissiveColor[1] = static_cast<float>(material.pbr.emission_color.value_vec3.y);
        output.desc.emissiveColor[2] = static_cast<float>(material.pbr.emission_color.value_vec3.z);
    }
    if (material.pbr.opacity.has_value) output.desc.baseColor[3] = static_cast<float>(material.pbr.opacity.value_real);
    output.desc.doubleSided = material.features.double_sided.enabled;
    output.albedoTexturePath = TextureKey(base);
    output.normalTexturePath = TextureKey(material.pbr.normal_map.texture != nullptr
        ? material.pbr.normal_map : material.fbx.normal_map);
    output.emissiveTexturePath = TextureKey(material.pbr.emission_color);
    output.occlusionTexturePath = TextureKey(material.pbr.ambient_occlusion);
    const ufbx_texture* metalness = FileTexture(material.pbr.metalness);
    const ufbx_texture* roughness = FileTexture(material.pbr.roughness);
    if (metalness != nullptr && roughness != nullptr &&
        FbxTextureKey(*metalness) != FbxTextureKey(*roughness)) {
        if (error != nullptr) *error = "FBX material uses separate metalness and roughness images; the runtime requires a packed metallic-roughness texture.";
        return {};
    }
    const ufbx_texture* packed = metalness != nullptr ? metalness : roughness;
    if (packed != nullptr) output.metallicRoughnessTexturePath = FbxTextureKey(*packed);
    return output;
}

[[nodiscard]] std::optional<RenderMeshSourceTexture> FbxTexture(
    const ufbx_texture& source,
    std::string* error) {
    RenderMeshSourceTexture output{};
    output.key = FbxTextureKey(source);
    output.extension = LowerExtension(output.key);
    const ufbx_blob content = source.content.size != 0U
        ? source.content
        : source.video != nullptr ? source.video->content : ufbx_blob{};
    if (content.data != nullptr && content.size != 0U) {
        const auto* begin = static_cast<const std::byte*>(content.data);
        output.embeddedBytes.assign(begin, begin + content.size);
        return output;
    }
    const std::string filename = UfbxString(source.filename);
    if (filename.empty()) return Fail<RenderMeshSourceTexture>(error,
        "FBX file texture has neither embedded content nor a resolvable filename.");
    output.sourcePath = std::filesystem::path{ filename }.lexically_normal();
    return output;
}

[[nodiscard]] std::optional<RenderMeshSourceImportManifest> InspectFbx(
    const std::filesystem::path& sourcePath,
    std::string* error) {
    ufbx_load_opts options{};
    options.load_external_files = false;
    options.use_blender_pbr_material = true;
    ufbx_error loadError{};
    FbxScene scene{ ufbx_load_file(sourcePath.string().c_str(), &options, &loadError) };
    if (!scene) return Fail<RenderMeshSourceImportManifest>(error,
        "Mesh material inspection could not parse FBX source.");
    RenderMeshSourceImportManifest manifest{};
    manifest.materials.reserve(scene->materials.count);
    for (std::size_t index = 0U; index < scene->materials.count; ++index) {
        std::string materialError;
        RenderMeshEmbeddedMaterial material = FbxMaterial(*scene->materials.data[index], &materialError);
        if (!materialError.empty()) return Fail<RenderMeshSourceImportManifest>(error, std::move(materialError));
        manifest.materials.push_back(std::move(material));
    }
    std::unordered_map<std::uint32_t, bool> seen;
    for (const RenderMeshEmbeddedMaterial& material : manifest.materials) {
        const std::array<const std::string*, 5U> keys{
            &material.albedoTexturePath, &material.normalTexturePath,
            &material.metallicRoughnessTexturePath, &material.occlusionTexturePath,
            &material.emissiveTexturePath,
        };
        for (const std::string* key : keys) {
            if (key->empty()) continue;
            for (std::size_t index = 0U; index < scene->textures.count; ++index) {
                const ufbx_texture* texture = scene->textures.data[index];
                if (texture == nullptr || FbxTextureKey(*texture) != *key || !seen.emplace(texture->typed_id, true).second) continue;
                auto imported = FbxTexture(*texture, error);
                if (!imported) return std::nullopt;
                manifest.textures.push_back(std::move(*imported));
                break;
            }
        }
    }
    return manifest;
}

} // namespace

std::optional<RenderMeshSourceImportManifest> RenderMeshSourceImport::Inspect(
    const std::filesystem::path& sourcePath,
    std::string* error) {
    if (error != nullptr) error->clear();
    const std::string extension = LowerExtension(sourcePath);
    if (extension == ".gltf" || extension == ".glb") return InspectGltf(sourcePath, error);
    if (extension == ".fbx") return InspectFbx(sourcePath, error);
    return Fail<RenderMeshSourceImportManifest>(error,
        "Mesh material inspection supports FBX, glTF, and GLB sources only.");
}

std::filesystem::path RenderMeshSourceImport::GeneratedMaterialVirtualPath(
    const std::filesystem::path& meshVirtualPath,
    std::string_view sourceName,
    std::string_view materialName) {
    const std::filesystem::path folder = meshVirtualPath.parent_path().empty()
        ? std::filesystem::path{ "/Game" }
        : meshVirtualPath.parent_path();
    const std::string source = Sanitize(std::filesystem::path{ sourceName }.stem().string(), "Mesh");
    const std::string material = Sanitize(materialName, "Material");
    const std::string identity = source + ":" + std::string{ materialName };
    return folder / "Materials" / (source + "_" + material + "_" + Hex8(StableHash(identity)) + ".kbmat");
}

} // namespace kb::render
