#include "CliPlayerPreset.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/input/InputAssetIO.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/script/ScriptAgentProjectFiles.hpp"
#include "engine/script/ScriptBehaviourAsset.hpp"

#if KB_CLI_RENDERER
#include "Map2DEditing.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/Map2DAsset.hpp"
#endif

#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace kb::cli {
namespace {

constexpr std::string_view kMoveAction = "/Game/Input/Move.21kbinputaction";
constexpr std::string_view kJumpAction = "/Game/Input/Jump.21kbinputaction";
constexpr std::string_view kContext = "/Game/Input/PlayerInput.21kbinputcontext";

[[nodiscard]] std::filesystem::path AssetFile(const std::filesystem::path& root, std::string_view virtualPath) {
    constexpr std::string_view prefix = "/Game/";
    return root / "Assets" / std::filesystem::path{ std::string{ virtualPath.substr(prefix.size()) } };
}

// Move (Axis2D) and Jump (Bool) with keyboard and pad bindings, unless the project already has an input context.
void EnsureInput(kb::scene::Scene& scene, const std::filesystem::path& root, CommandIo io) {
    using kb::input::InputKey;
    const std::filesystem::path settingsFile = kb::project::ProjectSettingsStore::FilePath(root);
    kb::project::ProjectSettingsLoadResult loaded = kb::project::ProjectSettingsStore::Load(settingsFile);
    if (!loaded.Succeeded()) {
        io.err << "note: project settings could not be read; add the Move and Jump input actions yourself\n";
        return;
    }
    if (!loaded.settings.inputMappingContext.empty()) {
        io.out << "input: the project's input context " << loaded.settings.inputMappingContext << " is kept; it needs a Move (Axis2D) and a Jump (Bool) action\n";
        return;
    }
    const auto write = [&](std::string_view virtualPath, std::string_view name, kb::input::InputActionValueType type) {
        const std::filesystem::path file = AssetFile(root, virtualPath);
        std::error_code error;
        if (std::filesystem::exists(file, error)) return true;
        std::filesystem::create_directories(file.parent_path(), error);
        kb::input::InputActionAsset action;
        action.name = std::string{ name };
        action.valueType = type;
        return kb::input::WriteInputAction(file, action);
    };
    if (!write(kMoveAction, "Move", kb::input::InputActionValueType::Axis2D) || !write(kJumpAction, "Jump", kb::input::InputActionValueType::Bool)) {
        io.err << "note: the input actions could not be written\n";
        return;
    }
    static_cast<void>(scene.Assets().Discover());
    const auto& registry = scene.Assets().Manager().Registry();
    const kb::assets::AssetMetadata* move = registry.FindByPath(kMoveAction);
    const kb::assets::AssetMetadata* jump = registry.FindByPath(kJumpAction);
    if (move == nullptr || jump == nullptr) {
        io.err << "note: the input actions were written but not found as assets\n";
        return;
    }
    kb::input::InputMappingContextAsset context;
    kb::input::InputCompositeBinding movement;
    movement.actionId = move->id.value;
    movement.slots = {
        { .key = InputKey::D, .axis = 0U, .scale = 1.0F }, { .key = InputKey::A, .axis = 0U, .scale = -1.0F },
        { .key = InputKey::W, .axis = 1U, .scale = 1.0F }, { .key = InputKey::S, .axis = 1U, .scale = -1.0F },
        { .key = InputKey::ArrowRight, .axis = 0U, .scale = 1.0F }, { .key = InputKey::ArrowLeft, .axis = 0U, .scale = -1.0F },
        { .key = InputKey::ArrowUp, .axis = 1U, .scale = 1.0F }, { .key = InputKey::ArrowDown, .axis = 1U, .scale = -1.0F },
        { .key = InputKey::GamepadLeftStickX, .axis = 0U, .scale = 1.0F }, { .key = InputKey::GamepadLeftStickY, .axis = 1U, .scale = 1.0F },
        { .key = InputKey::GamepadDPadRight, .axis = 0U, .scale = 1.0F }, { .key = InputKey::GamepadDPadLeft, .axis = 0U, .scale = -1.0F },
        { .key = InputKey::GamepadDPadUp, .axis = 1U, .scale = 1.0F }, { .key = InputKey::GamepadDPadDown, .axis = 1U, .scale = -1.0F },
    };
    context.composites.push_back(movement);
    context.mappings.push_back(kb::input::InputKeyMapping{ .actionId = jump->id.value, .key = InputKey::Space });
    context.mappings.push_back(kb::input::InputKeyMapping{ .actionId = jump->id.value, .key = InputKey::GamepadFaceBottom });
    if (!kb::input::WriteInputMappingContext(AssetFile(root, kContext), context)) {
        io.err << "note: the input context could not be written\n";
        return;
    }
    loaded.settings.inputMappingContext = std::string{ kContext };
    std::string error;
    if (!kb::project::ProjectSettingsStore::Save(settingsFile, loaded.settings, error)) {
        io.err << "note: the project settings could not be saved: " << error << '\n';
        return;
    }
    io.out << "input: Move (WASD, arrows, pad stick and D-pad) and Jump (Space, pad A) in " << kContext << ", set as the project's input context\n";
}

#if KB_CLI_RENDERER
void CollectBelow(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::vector<kb::scene::SceneEntity>& out) {
    out.push_back(entity);
    for (const kb::scene::SceneEntity child : scene.Hierarchy().ChildEntities(entity)) CollectBelow(scene, child, out);
}

// The map camera of every map in the scene follows the player, smoothly and inside the map.
void FollowWithMapCameras(kb::scene::Scene& scene, const std::filesystem::path& root, std::string_view name, bool platformer, CommandIo io) {
    // The maps the scene uses: a Tilemap component, or, in a scene made by `map2d new`, the mesh of a Mesh Renderer.
    std::vector<kb::scene::SceneEntity> all;
    for (const kb::scene::SceneEntity top : scene.Hierarchy().RootEntities()) CollectBelow(scene, top, all);
    std::set<std::uint64_t> used;
    const auto query = std::as_const(scene).Components();
    for (const kb::scene::SceneEntity entity : all) {
        if (const auto* tilemap = query.Tilemaps().TryGet(entity)) used.insert(tilemap->mapAssetId);
        else if (const auto* mesh = query.MeshRenderers().TryGet(entity)) used.insert(mesh->meshAssetId);
    }
    const std::filesystem::path assets = root / "Assets";
    std::size_t maps = 0U;
    std::error_code walkError;
    for (std::filesystem::recursive_directory_iterator it{ assets, walkError }, end; !walkError && it != end; it.increment(walkError)) {
        if (!it->is_regular_file() || it->path().extension() != kb::assets::kMap2DAssetExtension) continue;
        const std::string virtualPath = kb::world2d::Map2DVirtualPath(assets, it->path());
        const kb::assets::AssetId id = kb::assets::MakeAssetId(
            kb::assets::NormalizeAssetPath(std::filesystem::path{ virtualPath }) + ":" + std::string{ kb::assets::kMap2DAssetType });
        if (!used.contains(id.value)) continue;
        std::string error;
        auto map = kb::assets::Map2DAssetIO::Load(it->path(), &error);
        if (!map.has_value()) continue;
        if ((map->orientation == kb::assets::Map2DOrientation::Wall) != platformer) {
            io.out << "note: " << virtualPath << " is a " << (platformer ? "top-down" : "platformer")
                   << " map; the " << (platformer ? "Platformer" : "Top Down") << " player fits a " << (platformer ? "Map Platformer" : "Map Top Down") << '\n';
        }
        map->camera.followTarget = std::string{ name };
        map->camera.smoothingSeconds = 0.15F;
        map->camera.clampToMap = true;
        if (kb::assets::Map2DAssetIO::Save(it->path(), *map, &error)) ++maps;
    }
    io.out << "camera: " << maps << " map(s) now follow '" << name << "' (kb_cli map2d set --camera-* changes it)\n";
}
#endif

} // namespace

bool IsPlayerPreset(std::string_view item) noexcept {
    return item == "Platformer Player" || item == "Top Down Player";
}

kb::scene::SceneEntity AddPlayerPreset(kb::scene::Scene& scene, const std::filesystem::path& projectRoot,
    std::string_view item, std::string_view name, CommandIo io) {
    const bool platformer = item == "Platformer Player";
    const std::string_view fileName = platformer ? "Platformer2DPlayer.lua" : "TopDown2DPlayer.lua";
    const std::filesystem::path scriptFile = projectRoot / "Assets" / "Scripts" / std::string{ fileName };
    std::error_code fileError;
    if (!std::filesystem::exists(scriptFile, fileError)) {
        std::filesystem::create_directories(scriptFile.parent_path(), fileError);
        std::ofstream out{ scriptFile, std::ios::binary };
        out << kb::script::ScriptAgentProjectFiles::PresetScript(fileName);
        if (!out) {
            io.err << "error: " << scriptFile.generic_string() << " could not be written\n";
            return {};
        }
    }
    EnsureInput(scene, projectRoot, io);
    static_cast<void>(scene.Assets().Discover());
    const std::string virtualPath = "/Game/Scripts/" + std::string{ fileName };
    const kb::assets::AssetMetadata* script = scene.Assets().Manager().Registry().FindByPath(virtualPath);
    const std::optional<kb::scene::BehaviourComponent> behaviour =
        script != nullptr ? kb::script::ScriptBehaviourAsset::CreateComponent(*script, true) : std::nullopt;
    if (!behaviour.has_value()) {
        io.err << "error: " << virtualPath << " is not a behaviour asset\n";
        return {};
    }

    kb::scene::SceneObjectDesc desc{};
    desc.name = std::string{ name };
    // A platformer drops in from above; a top-down player stands in a tile's height, without gravity.
    desc.transform.localPosition = platformer ? kb::scene::Vec3{ 0.0F, 2.0F, 0.0F } : kb::scene::Vec3{ 0.0F, 0.5F, 0.0F };
    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(std::move(desc));
    if (!entity.IsValid()) {
        io.err << "error: the player could not be created\n";
        return {};
    }
    kb::scene::CharacterControllerComponent controller{};
    controller.radius = platformer ? 0.35F : 0.3F;
    controller.height = platformer ? 1.6F : 0.8F;
    controller.useGravity = platformer;
    scene.Components().CharacterControllers().Set(entity, controller);
    scene.Components().Behaviours().Set(entity, *behaviour);
    io.out << "script: " << virtualPath << " (a plain Lua file of the project; its @expose lines are the tunables)\n";
#if KB_CLI_RENDERER
    FollowWithMapCameras(scene, projectRoot, name, platformer, io);
#else
    io.out << "note: this kb_cli has no renderer, so no map camera was set to follow the player\n";
#endif
    return entity;
}

} // namespace kb::cli
