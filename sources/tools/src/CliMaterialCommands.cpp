#include "CliCommands.hpp"
#include "CliCommon.hpp"
#include "CliMaterialShared.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/math/ColorSpace.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

// material-new / material-set / material-show: the agent's way to the material the editor's
// Create > Material makes (the same RenderMaterialAssetData, default graph and writer) and to
// the values and textures its material panel edits.

namespace kb::cli {
namespace material {
namespace {

[[nodiscard]] std::optional<float> ParseNumber(std::string_view text) {
    float value = 0.0F;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(value)) return std::nullopt;
    return value;
}

struct MaterialField {
    const char* name;
    enum class Kind { Color3, Color4, Unit, NonNegative } kind;
    float* (*access)(kb::render::RenderMaterialDesc&);
};

const std::array<MaterialField, 8> kFields{ {
    { "Base Color", MaterialField::Kind::Color4, [](kb::render::RenderMaterialDesc& d) { return d.baseColor; } },
    { "Roughness", MaterialField::Kind::Unit, [](kb::render::RenderMaterialDesc& d) { return &d.roughnessFactor; } },
    { "Metallic", MaterialField::Kind::Unit, [](kb::render::RenderMaterialDesc& d) { return &d.metallicFactor; } },
    { "Emissive Color", MaterialField::Kind::Color3, [](kb::render::RenderMaterialDesc& d) { return d.emissiveColor; } },
    { "Emissive Strength", MaterialField::Kind::NonNegative, [](kb::render::RenderMaterialDesc& d) { return &d.emissiveStrength; } },
    { "Normal Scale", MaterialField::Kind::NonNegative, [](kb::render::RenderMaterialDesc& d) { return &d.normalScale; } },
    { "Occlusion Strength", MaterialField::Kind::Unit, [](kb::render::RenderMaterialDesc& d) { return &d.occlusionStrength; } },
    { "Alpha Cutoff", MaterialField::Kind::Unit, [](kb::render::RenderMaterialDesc& d) { return &d.alphaCutoff; } },
} };

// The built-in PBR texture slots, as the editor's material panel shows them.
struct TextureField {
    const char* name;
    std::uint64_t kb::render::RenderMaterialDesc::* slot;
};

const std::array<TextureField, 5> kTextureFields{ {
    { "Base Color Texture", &kb::render::RenderMaterialDesc::albedoTextureAssetId },
    { "Normal Texture", &kb::render::RenderMaterialDesc::normalTextureAssetId },
    { "Metallic Roughness Texture", &kb::render::RenderMaterialDesc::metallicRoughnessTextureAssetId },
    { "Occlusion Texture", &kb::render::RenderMaterialDesc::occlusionTextureAssetId },
    { "Emissive Texture", &kb::render::RenderMaterialDesc::emissiveTextureAssetId },
} };

[[nodiscard]] bool IsTextureAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderTexture" || metadata.importCategory == "Texture";
}

[[nodiscard]] std::string ColorHex(const float* color) {
    char hex[16];
    const auto byte = [](float linear) {
        return static_cast<unsigned>(std::clamp(kb::math::LinearToSrgb(std::clamp(linear, 0.0F, 1.0F)) * 255.0F + 0.5F, 0.0F, 255.0F));
    };
    std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", byte(color[0]), byte(color[1]), byte(color[2]));
    return hex;
}

} // namespace

std::string Utf8Text(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string{ reinterpret_cast<const char*>(text.data()), text.size() };
}

std::string Normalized(std::string_view text) {
    std::string out;
    for (const char character : text) {
        if (character == ' ' || character == '_' || character == '-') continue;
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return out;
}

std::filesystem::path ProjectRoot(const ArgumentList& arguments, std::string_view command, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: " << command << " requires --project <dir>\n";
        return {};
    }
    std::string error;
    const std::filesystem::path projectFile =
        kb::project::ProjectManager::FindProjectFile(kb::assets::Utf8Path(*project), error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return {};
    }
    return projectFile.parent_path();
}

std::optional<ProjectAsset> ProjectAssetFile(
    const std::filesystem::path& projectRoot,
    const std::string& text,
    std::string_view extension,
    std::string_view kind,
    std::string_view createHint,
    CommandIo io) {
    std::filesystem::path relative = kb::assets::Utf8Path(text);
    const std::string generic = Utf8Text(relative);
    if (generic.starts_with("/Game/")) {
        relative = std::filesystem::path{ "Assets" } / kb::assets::Utf8Path(generic.substr(6U));
    }
    const std::filesystem::path file = (projectRoot / relative).lexically_normal();
    const std::filesystem::path inside = file.lexically_relative(projectRoot);
    if (inside.empty() || *inside.begin() != "Assets" || file.extension() != extension ||
        !std::filesystem::is_regular_file(file)) {
        io.err << "error: the project has no " << kind << " '" << text << "' (" << createHint << ")\n";
        return std::nullopt;
    }
    return ProjectAsset{ file, "/Game/" + Utf8Text(std::filesystem::path{ inside }.lexically_relative("Assets")) };
}

TextureResolver ProjectTextureResolver(const kb::assets::AssetRegistry& registry) {
    return [&registry](std::string_view path, std::string& error) -> std::optional<std::uint64_t> {
        if (path == "none") {
            return 0U;
        }
        const kb::assets::AssetMetadata* metadata = FindAssetByFlexiblePath(registry, kb::assets::Utf8Path(path));
        if (metadata == nullptr || !IsTextureAsset(*metadata)) {
            error = "'" + std::string{ path } + "' is not a texture of the project (an image under Assets, e.g. "
                "/Game/Textures/Albedo.png, or an imported texture)";
            return std::nullopt;
        }
        return metadata->id.value;
    };
}

std::string TexturePath(const kb::assets::AssetRegistry& registry, std::uint64_t textureId) {
    const kb::assets::AssetMetadata* metadata = registry.Find(kb::assets::AssetId{ textureId });
    return metadata != nullptr ? Utf8Text(metadata->virtualPath) : std::string{};
}

std::string BuiltinFieldList() {
    std::string list;
    for (const MaterialField& field : kFields) {
        list += (list.empty() ? "" : ", ") + std::string{ field.name };
    }
    for (const TextureField& field : kTextureFields) {
        list += ", " + std::string{ field.name };
    }
    return list;
}

std::pair<std::string, std::string> SplitSet(const std::string& spec) {
    const std::size_t equals = spec.find('=');
    if (equals == std::string::npos || equals == 0U) {
        return {};
    }
    return { spec.substr(0U, equals), spec.substr(equals + 1U) };
}

std::string ApplyBuiltinField(
    kb::render::RenderMaterialDesc& desc,
    std::string_view name,
    std::string_view text,
    const TextureResolver& textures,
    bool& known) {
    const std::string wanted = Normalized(name);
    known = true;
    if (const auto texture = std::find_if(kTextureFields.begin(), kTextureFields.end(),
            [&](const TextureField& candidate) { return Normalized(candidate.name) == wanted; });
        texture != kTextureFields.end()) {
        std::string error;
        const std::optional<std::uint64_t> id = textures ? textures(text, error) : std::nullopt;
        if (!id.has_value()) {
            return std::string{ texture->name } + ": " + (error.empty() ? "textures need the project's assets" : error);
        }
        desc.*(texture->slot) = *id;
        return {};
    }
    const auto field = std::find_if(kFields.begin(), kFields.end(),
        [&](const MaterialField& candidate) { return Normalized(candidate.name) == wanted; });
    if (field == kFields.end()) {
        known = false;
        return "the material has no field '" + std::string{ name } + "'; fields: " + BuiltinFieldList();
    }
    float* const target = field->access(desc);
    switch (field->kind) {
    case MaterialField::Kind::Color3:
    case MaterialField::Kind::Color4: {
        const std::size_t channels = field->kind == MaterialField::Kind::Color4 ? 4U : 3U;
        const std::optional<std::array<float, 4>> color = ParseColorText(text, channels);
        if (!color.has_value()) {
            return std::string{ field->name } + ": expected #RRGGBB (sRGB) or linear r,g,b in 0..1, got '" +
                std::string{ text } + "'";
        }
        std::copy_n(color->begin(), channels, target);
        return {};
    }
    case MaterialField::Kind::Unit:
    case MaterialField::Kind::NonNegative: {
        const std::optional<float> value = ParseNumber(text);
        const bool unit = field->kind == MaterialField::Kind::Unit;
        if (!value.has_value() || *value < 0.0F || (unit && *value > 1.0F)) {
            return std::string{ field->name } + ": expected a number " + (unit ? "in 0..1" : "of 0 or more") +
                ", got '" + std::string{ text } + "'";
        }
        *target = *value;
        return {};
    }
    }
    return {};
}

void PrintBuiltinValues(const kb::render::RenderMaterialDesc& d, const kb::assets::AssetRegistry* registry, CommandIo io) {
    io.out << "  Base Color = " << ColorHex(d.baseColor) << " (linear " << d.baseColor[0] << ',' << d.baseColor[1] << ','
           << d.baseColor[2] << ", alpha " << d.baseColor[3] << ")\n"
           << "  Roughness = " << d.roughnessFactor << "\n"
           << "  Metallic = " << d.metallicFactor << "\n"
           << "  Emissive Strength = " << d.emissiveStrength << "\n";
    for (const TextureField& field : kTextureFields) {
        const std::uint64_t id = d.*(field.slot);
        if (id == 0U) continue;
        const std::string path = registry != nullptr ? TexturePath(*registry, id) : std::string{};
        io.out << "  " << field.name << " = " << (path.empty() ? "missing texture " + std::to_string(id) : path) << "\n";
    }
}

} // namespace material

namespace {

constexpr const char* kMaterialExtension = ".kbmat";

// Every --set applied to `desc`; the reason one was refused, or empty.
[[nodiscard]] std::string ApplySets(
    kb::render::RenderMaterialDesc& desc,
    const std::vector<std::string>& specs,
    const material::TextureResolver& textures) {
    for (const std::string& spec : specs) {
        const auto [name, value] = material::SplitSet(spec);
        if (name.empty()) {
            return "--set takes Field=value (e.g. --set \"Base Color=#C8202A\"), got '" + spec + "'";
        }
        bool known = false;
        if (const std::string problem = material::ApplyBuiltinField(desc, name, value, textures, known); !problem.empty()) {
            return problem;
        }
    }
    return {};
}

[[nodiscard]] bool MountProject(kb::scene::Scene& scene, const std::filesystem::path& projectRoot, CommandIo io) {
    std::string error;
    if (!MountProjectAssets(scene, projectRoot, error)) {
        io.err << "error: " << error << '\n';
        return false;
    }
    return true;
}

void PrintMaterial(const kb::render::RenderMaterialAssetData& asset, const std::string& virtualPath,
    const kb::assets::AssetRegistry& registry, CommandIo io) {
    io.out << virtualPath << '\n';
    material::PrintBuiltinValues(asset.desc, &registry, io);
    if (kb::render::HasGraphAuthoringData(asset.graph)) {
        io.out << "  (a graph of " << asset.graph.nodes.size() << " nodes renders this material: kb_cli material-graph show)\n";
    }
}

} // namespace

int RunMaterialNewCommand(const ArgumentList& arguments, CommandIo io) {
    const std::filesystem::path projectRoot = material::ProjectRoot(arguments, "material-new", io);
    const std::optional<std::string> name = arguments.Option("--name");
    if (projectRoot.empty() || !name.has_value()) {
        if (!projectRoot.empty()) io.err << "error: material-new requires --project <dir> --name <Name>\n";
        return 1;
    }
    if (const std::string problem = kb::assets::AssetFileNameError(*name); !problem.empty()) {
        io.err << "error: --name '" << *name << "': " << problem << '\n';
        return 1;
    }
    const kb::assets::ProjectAssetFolder folder =
        kb::assets::ResolveProjectAssetFolder(projectRoot, arguments.Option("--folder").value_or("Assets/Materials"));
    if (!folder.error.empty()) {
        io.err << "error: " << folder.error << '\n';
        return 1;
    }
    const std::filesystem::path file = folder.folder / kb::assets::Utf8Path(*name + kMaterialExtension);
    const std::string virtualPath = folder.virtualPath + "/" + *name + kMaterialExtension;
    if (std::filesystem::exists(file)) {
        io.err << "error: material already exists: " << virtualPath << " (change it with kb_cli material-set)\n";
        return 1;
    }
    kb::scene::Scene scene;
    if (!MountProject(scene, projectRoot, io)) return 1;
    const kb::assets::AssetRegistry& registry = scene.Assets().Manager().Registry();
    kb::render::RenderMaterialAssetData asset{};
    asset.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    if (const std::string problem = ApplySets(asset.desc, arguments.Options("--set"), material::ProjectTextureResolver(registry));
        !problem.empty()) {
        io.err << "error: " << problem << '\n';
        return 1;
    }
    std::error_code error;
    std::filesystem::create_directories(folder.folder, error);
    if (!kb::render::RenderMaterialAssetWriter::Save(file, asset)) {
        io.err << "error: the material could not be written: " << material::Utf8Text(file) << '\n';
        return 1;
    }
    PrintMaterial(asset, virtualPath, registry, io);
    io.out << "Use it in a Mesh Renderer: --set material=" << virtualPath << '\n';
    return 0;
}

int RunMaterialSetCommand(const ArgumentList& arguments, CommandIo io) {
    const std::filesystem::path projectRoot = material::ProjectRoot(arguments, "material-set", io);
    const std::optional<std::string> materialText = arguments.Option("--material");
    const std::vector<std::string> sets = arguments.Options("--set");
    if (projectRoot.empty() || !materialText.has_value() || sets.empty()) {
        if (!projectRoot.empty()) {
            io.err << "error: material-set requires --project <dir> --material /Game/<path>.kbmat --set Field=value\n";
        }
        return 1;
    }
    const std::optional<material::ProjectAsset> located = material::ProjectAssetFile(
        projectRoot, *materialText, kMaterialExtension, "material", "create one with kb_cli material-new", io);
    if (!located.has_value()) return 1;
    std::optional<kb::render::RenderMaterialAssetData> asset = kb::render::RenderMaterialAssetLoader::LoadMaterial(located->file);
    if (!asset.has_value()) {
        io.err << "error: the material could not be read: " << located->virtualPath << '\n';
        return 1;
    }
    kb::scene::Scene scene;
    if (!MountProject(scene, projectRoot, io)) return 1;
    const kb::assets::AssetRegistry& registry = scene.Assets().Manager().Registry();
    if (const std::string problem = ApplySets(asset->desc, sets, material::ProjectTextureResolver(registry)); !problem.empty()) {
        io.err << "error: " << problem << '\n';
        return 1;
    }
    if (!kb::render::RenderMaterialAssetWriter::Save(located->file, *asset)) {
        io.err << "error: the material could not be written: " << located->virtualPath << '\n';
        return 1;
    }
    PrintMaterial(*asset, located->virtualPath, registry, io);
    return 0;
}

int RunMaterialShowCommand(const ArgumentList& arguments, CommandIo io) {
    const std::filesystem::path projectRoot = material::ProjectRoot(arguments, "material-show", io);
    const std::optional<std::string> materialText = arguments.Option("--material");
    if (projectRoot.empty() || !materialText.has_value()) {
        if (!projectRoot.empty()) io.err << "error: material-show requires --project <dir> --material /Game/<path>.kbmat\n";
        return 1;
    }
    const std::optional<material::ProjectAsset> located = material::ProjectAssetFile(
        projectRoot, *materialText, kMaterialExtension, "material", "create one with kb_cli material-new", io);
    if (!located.has_value()) return 1;
    const std::optional<kb::render::RenderMaterialAssetData> asset =
        kb::render::RenderMaterialAssetLoader::LoadMaterial(located->file);
    if (!asset.has_value()) {
        io.err << "error: the material could not be read: " << located->virtualPath << '\n';
        return 1;
    }
    kb::scene::Scene scene;
    if (!MountProject(scene, projectRoot, io)) return 1;
    PrintMaterial(*asset, located->virtualPath, scene.Assets().Manager().Registry(), io);
    return 0;
}

} // namespace kb::cli
