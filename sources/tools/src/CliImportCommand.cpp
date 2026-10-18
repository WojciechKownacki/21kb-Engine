#include "CliCommands.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetImportPreset.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SkeletalMeshFbxImporter.hpp"
#include "engine/scene/SkeletalMeshGltfImporter.hpp"

#if KB_CLI_RENDERER
#include "kb/render/resources/RenderMeshImportArtifacts.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// import: the editor's Import... (the same AssetImportService and, with the renderer, the same
// texture and material extraction), with the import dialog's model settings as flags or from an
// import preset (.kbimportpreset, as an Unreal Interchange pipeline asset). import-preset-new
// writes such a preset.

namespace kb::cli {
namespace {

[[nodiscard]] std::string Utf8Text(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string{ reinterpret_cast<const char*>(text.data()), text.size() };
}

[[nodiscard]] std::string FieldList() {
    std::string list;
    for (const std::string& name : kb::assets::AssetImportPresetIO::FieldNames()) {
        list += (list.empty() ? "" : ", ") + name;
    }
    return list;
}

// --preset as a project path (/Game/... or a path inside the project) or any file.
[[nodiscard]] std::optional<kb::assets::AssetImportOptions> LoadPreset(
    kb::scene::Scene& scene, const std::string& project, const std::string& text, CommandIo io) {
    std::filesystem::path path = kb::assets::Utf8Path(text);
    if (text.starts_with("/")) {
        const std::optional<std::filesystem::path> resolved = scene.Assets().Manager().Mounts().Resolve(path);
        if (!resolved.has_value()) {
            io.err << "error: --preset " << text << " is not a path of the project\n";
            return std::nullopt;
        }
        path = *resolved;
    } else {
        path = ResolveInputPath(path, kb::assets::Utf8Path(project));
    }
    if (path.extension() != kb::assets::kAssetImportPresetExtension) {
        io.err << "error: --preset takes an import preset (" << kb::assets::kAssetImportPresetExtension << "), got " << text << '\n';
        return std::nullopt;
    }
    std::string error;
    std::optional<kb::assets::AssetImportOptions> options = kb::assets::AssetImportPresetIO::Load(path, &error);
    if (!options.has_value()) {
        io.err << "error: import preset " << text << ": " << error << '\n';
    }
    return options;
}

// One flag into the options through the preset's own field parser, so a flag takes exactly
// what a preset file line takes.
[[nodiscard]] bool ApplyFlag(kb::assets::AssetImportOptions& options, const ArgumentList& arguments,
    std::string_view flag, std::string_view field, std::string_view expected, CommandIo io) {
    const std::optional<std::string> value = arguments.Option(flag);
    if (!value.has_value()) return true;
    if (!kb::assets::AssetImportPresetIO::SetField(options, field, *value)) {
        io.err << "error: " << flag << " takes " << expected << ", got '" << *value << "'\n";
        return false;
    }
    return true;
}

// What a model source holds, for --skeletal auto: FBX through ufbx, glTF and GLB through cgltf.
[[nodiscard]] kb::scene::SkeletalSourceSummary ProbeModel(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension == ".fbx") return kb::scene::SkeletalMeshFbxImporter::Probe(path);
    if (extension == ".gltf" || extension == ".glb") return kb::scene::SkeletalMeshGltfImporter::Probe(path);
    return {};
}

} // namespace

int RunImportCommand(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    const std::optional<std::string> destination = arguments.Option("--destination");
    if (!project.has_value() || !destination.has_value() || arguments.Positionals().empty()) {
        io.err << "error: import requires --project <dir> --destination /Game/<folder> <file> [more files...]\n";
        return 1;
    }

    kb::scene::Scene scene;
    std::string mountError;
    if (!MountProjectAssets(scene, *project, mountError)) {
        io.err << "error: " << mountError << '\n';
        return 1;
    }

    kb::assets::AssetImportOptions options{};
    if (const std::optional<std::string> preset = arguments.Option("--preset")) {
        std::optional<kb::assets::AssetImportOptions> loaded = LoadPreset(scene, *project, *preset, io);
        if (!loaded.has_value()) return 1;
        options = *loaded;
    }
    if (!ApplyFlag(options, arguments, "--scale", "scale", "a number above 0", io) ||
        !ApplyFlag(options, arguments, "--rotation", "rotation", "x,y,z in degrees", io) ||
        !ApplyFlag(options, arguments, "--import-materials", "importMaterials", "yes or no", io) ||
        !ApplyFlag(options, arguments, "--import-textures", "importTextures", "yes or no", io)) {
        return 1;
    }
    // --skeletal: auto (default) imports a model with a skinned mesh, or an armature with
    // animations, as a character - Skeleton, Skeletal Mesh, Animation Clips - and anything else
    // as a static mesh, as an artist's file is meant; yes and no force one or the other.
    const std::string skeletalMode = arguments.Option("--skeletal").value_or(options.mesh.importSkeletalMesh ? "yes" : "auto");
    if (skeletalMode != "auto" && skeletalMode != "yes" && skeletalMode != "no") {
        io.err << "error: --skeletal takes auto, yes or no, got '" << skeletalMode << "'\n";
        return 1;
    }
    if (options.mesh.importMaterials) {
        // As the import dialog does: materials reference the textures imported with them.
        options.mesh.importTextures = true;
    }

    std::vector<std::filesystem::path> staticSources;
    std::vector<std::filesystem::path> skeletalSources;
    for (const std::string& source : arguments.Positionals()) {
        const std::filesystem::path path = ResolveInputPath(kb::assets::Utf8Path(source), kb::assets::Utf8Path(*project));
        const bool skeletal = skeletalMode == "yes" ||
            (skeletalMode == "auto" && ProbeModel(path).Skeletal());
        (skeletal ? skeletalSources : staticSources).push_back(path);
    }
    if (!skeletalSources.empty() && !options.mesh.transform.IsIdentity()) {
        io.err << "error: --scale and --rotation apply to static models; a character keeps the source's scale "
                  "and orientation (export it in metres; the source's up axis is converted)\n";
        return 1;
    }

    kb::assets::AssetManager& manager = scene.Assets().Manager();
    const std::filesystem::path folder{ *destination };
    kb::assets::AssetImportResult result{};
#if KB_CLI_RENDERER
    result = kb::render::RenderMeshImportArtifacts::ImportStaticFiles(manager, staticSources, folder, options);
    kb::assets::AssetImportResult skeletal =
        kb::render::RenderMeshImportArtifacts::ImportSkeletalFiles(manager, skeletalSources, folder, options);
    result.items.insert(result.items.end(),
        std::make_move_iterator(skeletal.items.begin()), std::make_move_iterator(skeletal.items.end()));
#else
    if (options.mesh.importTextures || options.mesh.importMaterials || !skeletalSources.empty()) {
        io.err << "error: importing characters, textures and materials needs the renderer; this kb_cli was built with KB_BUILD_RENDERER=OFF\n";
        return 1;
    }
    result = kb::assets::AssetImportService::ImportFiles(manager, staticSources, folder, options);
#endif
    static_cast<void>(manager.DiscoverMountedAssets());
    for (const kb::assets::AssetImportItemResult& item : result.items) {
        if (item.Succeeded()) {
            const kb::assets::AssetMetadata* metadata = manager.Registry().Find(item.id);
            io.out << kb::assets::ToString(item.status) << " " << item.virtualPath.generic_string();
            if (metadata != nullptr) io.out << " (" << metadata->type << ')';
            if (item.category == kb::assets::AssetImportCategory::Model && !options.mesh.transform.IsIdentity()) {
                const kb::assets::AssetMeshImportTransform& transform = options.mesh.transform;
                io.out << " (scale " << transform.scale << ", rotation " << transform.rotationDegrees[0] << ','
                       << transform.rotationDegrees[1] << ',' << transform.rotationDegrees[2] << ')';
            }
            io.out << '\n';
            for (const kb::assets::AssetId produced : item.producedAssets) {
                if (const kb::assets::AssetMetadata* other = manager.Registry().Find(produced)) {
                    io.out << "  + " << other->virtualPath.generic_string() << " (" << other->type << ")\n";
                }
            }
        } else {
            io.err << "error: " << Utf8Text(item.sourcePath) << ": " << item.error << '\n';
        }
    }
    return result.Succeeded() ? 0 : 1;
}

int RunImportPresetNewCommand(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    const std::optional<std::string> name = arguments.Option("--name");
    if (!project.has_value() || !name.has_value()) {
        io.err << "error: import-preset-new requires --project <dir> --name <Name>\n";
        return 1;
    }
    std::string error;
    const std::filesystem::path projectFile = kb::project::ProjectManager::FindProjectFile(kb::assets::Utf8Path(*project), error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    if (const std::string problem = kb::assets::AssetFileNameError(*name); !problem.empty()) {
        io.err << "error: --name '" << *name << "': " << problem << '\n';
        return 1;
    }
    const kb::assets::ProjectAssetFolder folder = kb::assets::ResolveProjectAssetFolder(
        projectFile.parent_path(), arguments.Option("--folder").value_or("Assets/ImportPresets"));
    if (!folder.error.empty()) {
        io.err << "error: " << folder.error << '\n';
        return 1;
    }
    const std::string fileName = *name + kb::assets::kAssetImportPresetExtension;
    const std::filesystem::path file = folder.folder / kb::assets::Utf8Path(fileName);
    const std::string virtualPath = folder.virtualPath + "/" + fileName;
    if (std::filesystem::exists(file)) {
        io.err << "error: import preset already exists: " << virtualPath << '\n';
        return 1;
    }
    kb::assets::AssetImportOptions options{};
    for (const std::string& spec : arguments.Options("--set")) {
        const std::size_t equals = spec.find('=');
        if (equals == std::string::npos || equals == 0U ||
            !kb::assets::AssetImportPresetIO::SetField(options, std::string_view{ spec }.substr(0U, equals), std::string_view{ spec }.substr(equals + 1U))) {
            io.err << "error: the preset refused '" << spec << "' (field=value; fields: " << FieldList() << ")\n";
            return 1;
        }
    }
    std::error_code createError;
    std::filesystem::create_directories(folder.folder, createError);
    if (!kb::assets::AssetImportPresetIO::Save(file, options)) {
        io.err << "error: the import preset could not be written: " << Utf8Text(file) << '\n';
        return 1;
    }
    io.out << virtualPath << '\n' << kb::assets::AssetImportPresetIO::Write(options)
           << "Import with it: kb_cli import --preset " << virtualPath << " --destination /Game/<folder> <file>\n";
    return 0;
}

} // namespace kb::cli
