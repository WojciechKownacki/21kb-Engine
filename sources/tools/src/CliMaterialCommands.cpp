#include "CliCommands.hpp"
#include "CliCommon.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/math/ColorSpace.hpp"
#include "engine/project/ProjectManager.hpp"
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
// the values its material panel edits.

namespace kb::cli {
namespace {

constexpr const char* kMaterialExtension = ".kbmat";

[[nodiscard]] std::string Utf8Text(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string{ reinterpret_cast<const char*>(text.data()), text.size() };
}

[[nodiscard]] std::string Normalized(std::string_view text) {
    std::string out;
    for (const char character : text) {
        if (character == ' ' || character == '_' || character == '-') continue;
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return out;
}

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

[[nodiscard]] std::string FieldList() {
    std::string list;
    for (const MaterialField& field : kFields) {
        list += (list.empty() ? "" : ", ") + std::string{ field.name };
    }
    return list;
}

// One "Field=value" applied to `desc`; the reason it was refused, or empty.
[[nodiscard]] std::string ApplySet(kb::render::RenderMaterialDesc& desc, const std::string& spec) {
    const std::size_t equals = spec.find('=');
    if (equals == std::string::npos || equals == 0U) {
        return "--set takes Field=value (e.g. --set \"Base Color=#C8202A\"), got '" + spec + "'";
    }
    const std::string wanted = Normalized(std::string_view{ spec }.substr(0U, equals));
    const std::string_view text = std::string_view{ spec }.substr(equals + 1U);
    const auto field = std::find_if(kFields.begin(), kFields.end(),
        [&](const MaterialField& candidate) { return Normalized(candidate.name) == wanted; });
    if (field == kFields.end()) {
        return "the material has no field '" + spec.substr(0U, equals) + "'; fields: " + FieldList();
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

void PrintMaterial(const kb::render::RenderMaterialAssetData& material, const std::string& virtualPath, CommandIo io) {
    const kb::render::RenderMaterialDesc& d = material.desc;
    char hex[16];
    const auto byte = [](float linear) {
        return static_cast<unsigned>(std::clamp(kb::math::LinearToSrgb(std::clamp(linear, 0.0F, 1.0F)) * 255.0F + 0.5F, 0.0F, 255.0F));
    };
    std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", byte(d.baseColor[0]), byte(d.baseColor[1]), byte(d.baseColor[2]));
    io.out << virtualPath << '\n'
           << "  Base Color = " << hex << " (linear " << d.baseColor[0] << ',' << d.baseColor[1] << ',' << d.baseColor[2]
           << ", alpha " << d.baseColor[3] << ")\n"
           << "  Roughness = " << d.roughnessFactor << "\n"
           << "  Metallic = " << d.metallicFactor << "\n"
           << "  Emissive Strength = " << d.emissiveStrength << "\n";
}

[[nodiscard]] std::filesystem::path ProjectRoot(const ArgumentList& arguments, std::string_view command, CommandIo io) {
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

// --material as a virtual path (/Game/...) or a path inside the project.
[[nodiscard]] std::optional<std::pair<std::filesystem::path, std::string>> MaterialFile(
    const std::filesystem::path& projectRoot, const std::string& text, CommandIo io) {
    std::filesystem::path relative = kb::assets::Utf8Path(text);
    std::string generic = Utf8Text(relative);
    if (generic.starts_with("/Game/")) {
        relative = std::filesystem::path{ "Assets" } / kb::assets::Utf8Path(generic.substr(6U));
    }
    const std::filesystem::path file = (projectRoot / relative).lexically_normal();
    const std::filesystem::path inside = file.lexically_relative(projectRoot);
    if (inside.empty() || *inside.begin() != "Assets" || file.extension() != kMaterialExtension ||
        !std::filesystem::is_regular_file(file)) {
        io.err << "error: the project has no material '" << text << "' (create one with kb_cli material-new)\n";
        return std::nullopt;
    }
    return std::make_pair(file, "/Game/" + Utf8Text(std::filesystem::path{ inside }.lexically_relative("Assets")));
}

} // namespace

int RunMaterialNewCommand(const ArgumentList& arguments, CommandIo io) {
    const std::filesystem::path projectRoot = ProjectRoot(arguments, "material-new", io);
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
    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    for (const std::string& spec : arguments.Options("--set")) {
        if (const std::string problem = ApplySet(material.desc, spec); !problem.empty()) {
            io.err << "error: " << problem << '\n';
            return 1;
        }
    }
    std::error_code error;
    std::filesystem::create_directories(folder.folder, error);
    if (!kb::render::RenderMaterialAssetWriter::Save(file, material)) {
        io.err << "error: the material could not be written: " << Utf8Text(file) << '\n';
        return 1;
    }
    PrintMaterial(material, virtualPath, io);
    io.out << "Use it in a Mesh Renderer: --set material=" << virtualPath << '\n';
    return 0;
}

int RunMaterialSetCommand(const ArgumentList& arguments, CommandIo io) {
    const std::filesystem::path projectRoot = ProjectRoot(arguments, "material-set", io);
    const std::optional<std::string> materialText = arguments.Option("--material");
    const std::vector<std::string> sets = arguments.Options("--set");
    if (projectRoot.empty() || !materialText.has_value() || sets.empty()) {
        if (!projectRoot.empty()) {
            io.err << "error: material-set requires --project <dir> --material /Game/<path>.kbmat --set Field=value\n";
        }
        return 1;
    }
    const auto located = MaterialFile(projectRoot, *materialText, io);
    if (!located.has_value()) return 1;
    std::optional<kb::render::RenderMaterialAssetData> material = kb::render::RenderMaterialAssetLoader::LoadMaterial(located->first);
    if (!material.has_value()) {
        io.err << "error: the material could not be read: " << located->second << '\n';
        return 1;
    }
    for (const std::string& spec : sets) {
        if (const std::string problem = ApplySet(material->desc, spec); !problem.empty()) {
            io.err << "error: " << problem << '\n';
            return 1;
        }
    }
    if (!kb::render::RenderMaterialAssetWriter::Save(located->first, *material)) {
        io.err << "error: the material could not be written: " << located->second << '\n';
        return 1;
    }
    PrintMaterial(*material, located->second, io);
    return 0;
}

int RunMaterialShowCommand(const ArgumentList& arguments, CommandIo io) {
    const std::filesystem::path projectRoot = ProjectRoot(arguments, "material-show", io);
    const std::optional<std::string> materialText = arguments.Option("--material");
    if (projectRoot.empty() || !materialText.has_value()) {
        if (!projectRoot.empty()) io.err << "error: material-show requires --project <dir> --material /Game/<path>.kbmat\n";
        return 1;
    }
    const auto located = MaterialFile(projectRoot, *materialText, io);
    if (!located.has_value()) return 1;
    const std::optional<kb::render::RenderMaterialAssetData> material =
        kb::render::RenderMaterialAssetLoader::LoadMaterial(located->first);
    if (!material.has_value()) {
        io.err << "error: the material could not be read: " << located->second << '\n';
        return 1;
    }
    PrintMaterial(*material, located->second, io);
    return 0;
}

} // namespace kb::cli
