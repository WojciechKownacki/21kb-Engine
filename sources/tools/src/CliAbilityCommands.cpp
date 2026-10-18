#include "CliCommands.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/gameplay/AbilityAsset.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// ability: what a combatant can do (a swing, a fireball, a heal) as a small text asset. A
// Combatant takes it in a slot: component set --type Combatant --set ability1=<path>.

namespace kb::cli {
namespace {

constexpr std::string_view kUsage =
    "usage: kb_cli ability <new|set|show> --project <dir> ...\n"
    "  new   --name <Name> [--folder Assets/Abilities] [--set key=value ...]\n"
    "  set   --ability <path> --set key=value [...]\n"
    "  show  --ability <path>\n"
    "  keys: name, target (Enemy|Ally|Self), range, radius (0 = one target), amount,\n"
    "        damageType (Physical|Fire|Cold|Lightning|Poison|Healing), manaCost, cooldown (s),\n"
    "        animatorTrigger (fired on the caster's Animator, e.g. Attack)\n";

[[nodiscard]] bool ApplySets(kb::gameplay::AbilityAsset& ability, const ArgumentList& arguments, CommandIo io) {
    for (const std::string& setting : arguments.Options("--set")) {
        const std::size_t equals = setting.find('=');
        std::string error;
        if (equals == std::string::npos ||
            !kb::gameplay::AbilityAssetIO::Set(ability, setting.substr(0, equals), setting.substr(equals + 1U), &error)) {
            io.err << "error: --set " << setting << ": " << (error.empty() ? "expects key=value" : error) << '\n';
            return false;
        }
    }
    if (const std::optional<std::string> invalid = kb::gameplay::ValidateAbilityAsset(ability)) {
        io.err << "error: " << *invalid << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::filesystem::path> ProjectRoot(const ArgumentList& arguments, kb::scene::Scene& scene, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: ability requires --project <dir>\n" << kUsage;
        return std::nullopt;
    }
    const std::filesystem::path root = kb::assets::Utf8Path(*project);
    std::string error;
    if (!MountProjectAssets(scene, root, error)) {
        io.err << "error: " << error << '\n';
        return std::nullopt;
    }
    return root;
}

[[nodiscard]] const kb::assets::AssetMetadata* FindAbility(kb::scene::Scene& scene, const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> path = arguments.Option("--ability");
    const kb::assets::AssetMetadata* metadata = path.has_value()
        ? FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), kb::assets::Utf8Path(*path)) : nullptr;
    if (metadata == nullptr || metadata->type != kb::gameplay::kAbilityAssetType) {
        io.err << "error: --ability <path> must name an ability (.kbability) of the project (create one with kb_cli ability new)\n";
        return nullptr;
    }
    return metadata;
}

[[nodiscard]] int New(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> name = arguments.Option("--name");
    if (!name.has_value() || name->empty()) {
        io.err << "error: ability new requires --name <Name>\n" << kUsage;
        return 1;
    }
    kb::scene::Scene scene;
    const std::optional<std::filesystem::path> root = ProjectRoot(arguments, scene, io);
    if (!root.has_value()) return 1;
    if (const std::string problem = kb::assets::AssetFileNameError(*name); !problem.empty()) {
        io.err << "error: --name '" << *name << "': " << problem << '\n';
        return 1;
    }
    const kb::assets::ProjectAssetFolder folder =
        kb::assets::ResolveProjectAssetFolder(*root, arguments.Option("--folder").value_or("Assets/Abilities"));
    if (!folder.error.empty()) {
        io.err << "error: " << folder.error << '\n';
        return 1;
    }
    const std::filesystem::path file = folder.folder / kb::assets::Utf8Path(*name + kb::gameplay::kAbilityAssetExtension);
    if (std::filesystem::exists(file)) {
        io.err << "error: " << file.generic_string() << " exists already (change it with kb_cli ability set)\n";
        return 1;
    }
    kb::gameplay::AbilityAsset ability{ .name = *name };
    if (!ApplySets(ability, arguments, io)) return 1;
    std::filesystem::create_directories(folder.folder);
    if (!kb::gameplay::AbilityAssetIO::Save(file, ability)) {
        io.err << "error: " << file.generic_string() << " could not be written\n";
        return 1;
    }
    io.out << "created " << file.generic_string() << "\n" << kb::gameplay::AbilityAssetIO::Write(ability);
    return 0;
}

[[nodiscard]] int Set(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    if (!ProjectRoot(arguments, scene, io).has_value()) return 1;
    const kb::assets::AssetMetadata* metadata = FindAbility(scene, arguments, io);
    if (metadata == nullptr) return 1;
    std::string error;
    std::optional<kb::gameplay::AbilityAsset> ability = kb::gameplay::AbilityAssetIO::Load(metadata->physicalPath, &error);
    if (!ability.has_value()) {
        io.err << "error: " << metadata->virtualPath.generic_string() << ": " << error << '\n';
        return 1;
    }
    if (arguments.Options("--set").empty()) {
        io.err << "error: ability set needs at least one --set key=value\n" << kUsage;
        return 1;
    }
    if (!ApplySets(*ability, arguments, io)) return 1;
    if (!kb::gameplay::AbilityAssetIO::Save(metadata->physicalPath, *ability)) {
        io.err << "error: " << metadata->virtualPath.generic_string() << " could not be written\n";
        return 1;
    }
    io.out << "updated " << metadata->virtualPath.generic_string() << "\n" << kb::gameplay::AbilityAssetIO::Write(*ability);
    return 0;
}

[[nodiscard]] int Show(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    if (!ProjectRoot(arguments, scene, io).has_value()) return 1;
    const kb::assets::AssetMetadata* metadata = FindAbility(scene, arguments, io);
    if (metadata == nullptr) return 1;
    std::string error;
    const std::optional<kb::gameplay::AbilityAsset> ability = kb::gameplay::AbilityAssetIO::Load(metadata->physicalPath, &error);
    if (!ability.has_value()) {
        io.err << "error: " << metadata->virtualPath.generic_string() << ": " << error << '\n';
        return 1;
    }
    io.out << metadata->virtualPath.generic_string() << '\n' << kb::gameplay::AbilityAssetIO::Write(*ability);
    return 0;
}

} // namespace

int RunAbilityCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.empty() ? std::string{} : positionals.front();
    if (action == "new") return New(arguments, io);
    if (action == "set") return Set(arguments, io);
    if (action == "show") return Show(arguments, io);
    io.err << kUsage;
    return 1;
}

} // namespace kb::cli
