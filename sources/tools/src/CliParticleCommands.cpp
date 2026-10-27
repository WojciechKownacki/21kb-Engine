#include "CliCommands.hpp"
#include "CliMaterialShared.hpp"

#include "editor/ParticleAssetGateway.hpp"
#include "editor/ParticleEditorCommands.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/platform/FileSystemPath.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"

#include <charconv>
#include <optional>
#include <string>
#include <vector>

// vfx-*: particle effects (.kbvfx) from the command line through the particle editor's own
// document and commands (Unity: Particle System via script, Unreal: Niagara via Python).

namespace kb::cli {
namespace {

using kb::particle_editor::ParticleEditorCommands;
using kb::particle_editor::ParticleEditorResult;

[[nodiscard]] std::string Problem(const ParticleEditorResult& result) {
    std::string text = result.message;
    for (const kb::scene::ParticleEffectDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.message != result.message) text += "\n  " + diagnostic.propertyPath + ": " + diagnostic.message;
    }
    return text;
}

[[nodiscard]] std::optional<std::uint64_t> Number(const std::optional<std::string>& text) {
    std::uint64_t value = 0U;
    if (!text.has_value() || std::from_chars(text->data(), text->data() + text->size(), value).ec != std::errc{}) {
        return std::nullopt;
    }
    return value;
}

// Every "key=value" written over the effect's file line of that key (keys as vfx-show prints them).
[[nodiscard]] std::string ApplySets(kb::scene::ParticleEffectAsset& asset, const std::vector<std::string>& specs) {
    if (specs.empty()) return {};
    std::vector<kb::scene::ParticleEffectDiagnostic> diagnostics;
    std::optional<std::string> text = kb::scene::ParticleEffectAssetIO::Serialize(asset, diagnostics);
    if (!text.has_value()) return "the effect could not be serialized";
    for (const std::string& spec : specs) {
        const auto [key, value] = material::SplitSet(spec);
        if (key.empty()) return "--set takes key=value, got '" + spec + "'";
        const std::size_t line = text->find('\n' + key + ' ');
        if (line == std::string::npos) {
            // A field left at its default is not in the file; the parser refuses an unknown key.
            *text += key + ' ' + value + '\n';
            continue;
        }
        const std::size_t start = line + key.size() + 2U;
        const bool quoted = (*text)[start] == '"' && !value.starts_with('"');
        text->replace(start, text->find('\n', start) - start, quoted ? '"' + value + '"' : value);
    }
    kb::scene::ParticleEffectLoadResult parsed = kb::scene::ParticleEffectAssetIO::Parse(*text);
    if (!parsed.Succeeded()) {
        std::string problem = "the effect refused the change";
        for (const auto& diagnostic : parsed.diagnostics) problem += "\n  " + diagnostic.propertyPath + ": " + diagnostic.message;
        return problem;
    }
    asset = std::move(*parsed.asset);
    return {};
}

[[nodiscard]] int Save(const std::filesystem::path& file, const kb::scene::ParticleEffectAsset& asset,
                       const std::string& virtualPath, CommandIo io) {
    const ParticleEditorResult saved = kb::particle_editor::ParticleAssetGateway{}.Save(file, asset);
    if (!saved.Succeeded()) {
        io.err << "error: " << Problem(saved) << '\n';
        return 1;
    }
    io.out << virtualPath << " (" << asset.emitters.size() << " emitters)\n";
    return 0;
}

} // namespace

int RunVfxCommand(std::string_view command, const ArgumentList& arguments, CommandIo io) {
    const std::filesystem::path projectRoot = material::ProjectRoot(arguments, command, io);
    if (projectRoot.empty()) return 1;
    if (command == "vfx-new") {
        const std::optional<std::string> name = arguments.Option("--name");
        if (!name.has_value()) {
            io.err << "error: vfx-new requires --project <dir> --name <Name>\n";
            return 1;
        }
        if (const std::string problem = kb::assets::AssetFileNameError(*name); !problem.empty()) {
            io.err << "error: --name '" << *name << "': " << problem << '\n';
            return 1;
        }
        const kb::assets::ProjectAssetFolder folder =
            kb::assets::ResolveProjectAssetFolder(projectRoot, arguments.Option("--folder").value_or("Assets/Particles"));
        if (!folder.error.empty()) {
            io.err << "error: " << folder.error << '\n';
            return 1;
        }
        const std::string fileName = *name + kb::scene::kParticleEffectAssetExtension;
        const std::filesystem::path file = folder.folder / kb::assets::Utf8Path(fileName);
        if (std::filesystem::exists(file)) {
            io.err << "error: particle effect already exists: " << folder.virtualPath << '/' << fileName << '\n';
            return 1;
        }
        kb::scene::ParticleEffectAsset asset = ParticleEditorCommands::NewEffect(*name);
        if (const std::string problem = ApplySets(asset, arguments.Options("--set")); !problem.empty()) {
            io.err << "error: " << problem << '\n';
            return 1;
        }
        std::error_code error;
        std::filesystem::create_directories(folder.folder, error);
        const auto virtualPath = folder.virtualPath + '/' + fileName;
        const auto source = kb::platform::RunningExecutablePath().parent_path() /
            "Content/21kbParticle/Materials/DefaultParticle.kbmat";
        const auto saved = kb::particle_editor::ParticleAssetGateway{}.SaveNewProjectEffect(file, virtualPath, asset, source);
        if (!saved.Succeeded()) {
            io.err << "error: " << Problem(saved) << '\n';
            return 1;
        }
        io.out << virtualPath << " (" << asset.emitters.size() << " emitters)\n";
        return 0;
    }

    const std::optional<std::string> effectText = arguments.Option("--effect");
    if (!effectText.has_value()) {
        io.err << "error: " << command << " requires --project <dir> --effect /Game/<path>.kbvfx\n";
        return 1;
    }
    const auto effect = material::ProjectAssetFile(projectRoot, *effectText, kb::scene::kParticleEffectAssetExtension,
                                                   "particle effect", "kb_cli vfx-new", io);
    if (!effect.has_value()) return 1;
    kb::particle_editor::ParticleAssetLoadResult loaded = kb::particle_editor::ParticleAssetGateway{}.Load(effect->file);
    if (!loaded.asset.has_value()) {
        io.err << "error: " << Problem(loaded.result) << '\n';
        return 1;
    }
    if (command == "vfx-show") {
        std::vector<kb::scene::ParticleEffectDiagnostic> diagnostics;
        io.out << kb::scene::ParticleEffectAssetIO::Serialize(*loaded.asset, diagnostics).value_or("");
        return 0;
    }
    if (command == "vfx-set") {
        const std::vector<std::string> sets = arguments.Options("--set");
        const std::string problem = sets.empty() ? "vfx-set requires --set key=value" : ApplySets(*loaded.asset, sets);
        if (!problem.empty()) {
            io.err << "error: " << problem << '\n';
            return 1;
        }
        return Save(effect->file, *loaded.asset, effect->virtualPath, io);
    }

    kb::particle_editor::ParticleEditorDocument document;
    kb::particle_editor::ParticleEditorWorkspaceState workspace;
    static_cast<void>(document.Create(*loaded.asset));
    const std::optional<std::uint64_t> emitter = Number(arguments.Option("--emitter"));
    ParticleEditorResult result;
    if (command == "vfx-emitter-add") {
        const auto& emitters = document.Asset().emitters;
        result = ParticleEditorCommands::AddEmitter(document, workspace, emitters.empty()
            ? ParticleEditorCommands::NewEffect({}).emitters.front().output.material : emitters.back().output.material);
        const std::optional<std::string> name = arguments.Option("--name");
        if (result.Succeeded() && name.has_value()) {
            result = ParticleEditorCommands::RenameEmitter(document, workspace, document.Asset().emitters.back().emitterId, *name);
        }
    } else if (!emitter.has_value()) {
        io.err << "error: " << command << " requires --emitter <id> (the emitter's id as vfx-show prints it)\n";
        return 1;
    } else if (command == "vfx-emitter-remove") {
        result = ParticleEditorCommands::RemoveEmitter(document, workspace, static_cast<kb::scene::ParticleStableId>(*emitter));
    } else if (command == "vfx-module-add") {
        const std::optional<kb::scene::ParticleModuleType> type =
            kb::scene::ParticleEffectAssetIO::ModuleTypeFromName(arguments.Option("--type").value_or(""));
        if (!type.has_value()) {
            io.err << "error: vfx-module-add requires --type <Gravity|Wind|Drag|Noise|TorusSwirl|Separation|...>\n";
            return 1;
        }
        result = ParticleEditorCommands::AddModule(document, workspace, static_cast<kb::scene::ParticleStableId>(*emitter),
            *type, static_cast<kb::scene::ParticleStableId>(Number(arguments.Option("--target")).value_or(0U)));
    } else {
        const std::optional<std::uint64_t> module = Number(arguments.Option("--module"));
        if (!module.has_value()) {
            io.err << "error: vfx-module-remove requires --module <id>\n";
            return 1;
        }
        result = ParticleEditorCommands::RemoveModule(document, workspace, static_cast<kb::scene::ParticleStableId>(*emitter),
            static_cast<kb::scene::ParticleStableId>(*module));
    }
    if (!result.Succeeded()) {
        io.err << "error: " << Problem(result) << '\n';
        return 1;
    }
    kb::scene::ParticleEffectAsset asset = document.Asset();
    if (const std::string problem = ApplySets(asset, arguments.Options("--set")); !problem.empty()) {
        io.err << "error: " << problem << '\n';
        return 1;
    }
    return Save(effect->file, asset, effect->virtualPath, io);
}

} // namespace kb::cli
