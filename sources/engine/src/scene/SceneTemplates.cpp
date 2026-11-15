#include "engine/scene/SceneTemplates.hpp"

#include "engine/project/ProjectDescriptor.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/Collider2DComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneComponentAuthoring.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneCreateMenu.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/ScenePlayerPreset.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/Sprite2DComponent.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/script/ScriptAsset.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetId.hpp"

#include <array>
#include <sstream>
#include <system_error>

namespace kb::scene {
namespace {

constexpr std::array<SceneTemplateInfo, 5> kTemplates{ {
    { SceneTemplate::Basic, "Basic3D", "Camera, sun, sky and a floor - ready to press Play." },
    { SceneTemplate::Basic2D, "Basic2D", "Orthographic camera and a sprite floor with a collider, on the 2D physics." },
    { SceneTemplate::FirstPerson, "FirstPerson", "A walking character seen through its own eyes, with mouse look." },
    { SceneTemplate::ThirdPerson, "ThirdPerson", "A walking character with a camera that follows behind it." },
    { SceneTemplate::Empty, "Empty", "Nothing at all. Add objects from the Create menu." },
} };

// "Basic" is the name Basic3D had before the 2D and character templates joined it.
constexpr std::string_view kBasicAlias = "Basic";

// The controller scripts of the character templates, files of the project under Assets/Scripts.
constexpr std::string_view kFirstPersonScript = "FirstPersonCharacter.lua";
constexpr std::string_view kThirdPersonScript = "ThirdPersonCharacter.lua";

[[nodiscard]] std::string_view PlayerScript(SceneTemplate kind) noexcept {
    return kind == SceneTemplate::FirstPerson ? kFirstPersonScript : kind == SceneTemplate::ThirdPerson ? kThirdPersonScript : std::string_view{};
}

[[nodiscard]] bool EqualsIgnoreCase(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto lower = [](char character) {
            return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
        };
        if (lower(left[index]) != lower(right[index])) {
            return false;
        }
    }
    return true;
}

// The UTF-8 file stem, for the scene document's name.
[[nodiscard]] std::string Utf8Stem(const std::filesystem::path& path) {
    const std::u8string stem = path.stem().u8string();
    return std::string{ reinterpret_cast<const char*>(stem.data()), stem.size() };
}

} // namespace

std::span<const SceneTemplateInfo> SceneTemplates::All() noexcept {
    return kTemplates;
}

std::string_view SceneTemplates::Name(SceneTemplate kind) noexcept {
    for (const SceneTemplateInfo& info : kTemplates) {
        if (info.kind == kind) {
            return info.name;
        }
    }
    return {};
}

std::optional<SceneTemplate> SceneTemplates::Parse(std::string_view name) noexcept {
    if (EqualsIgnoreCase(kBasicAlias, name)) {
        return SceneTemplate::Basic;
    }
    for (const SceneTemplateInfo& info : kTemplates) {
        if (EqualsIgnoreCase(info.name, name)) {
            return info.kind;
        }
    }
    return std::nullopt;
}

namespace {

void Place(Scene& scene, SceneEntity entity, Vec3 position) {
    if (const TransformComponent* placed = scene.Transforms().TryGet(entity); placed != nullptr) {
        TransformComponent transform = *placed;
        transform.localPosition = position;
        scene.Transforms().Set(entity, transform);
    }
}

// The listener sits on the main camera; without one a new scene plays no sound.
void AddListener(Scene& scene, SceneEntity entity, const kb::project::ProjectDescriptor& project) {
    if (const SceneComponentKind* listener = SceneComponentAuthoring::Find("AudioListener");
        listener != nullptr && SceneComponentAuthoring::PluginEnabled(project, *listener)) {
        static_cast<void>(SceneComponentAuthoring::Add(scene, entity, "AudioListener", project));
    }
}

// An orthographic camera and a floor made of a sprite and a collider, both on the 2D components.
std::string FillBasic2D(Scene& scene, const kb::project::ProjectDescriptor& project) {
    const SceneCreateResult camera = SceneCreateMenu::Create(scene, "Camera", "Main Camera", project);
    if (!camera.entity.IsValid()) {
        return "Basic2D template: " + camera.error;
    }
    Place(scene, camera.entity, Vec3{ 0.0F, 2.0F, -10.0F });
    if (const CameraComponent* placed = scene.Components().Cameras().TryGet(camera.entity); placed != nullptr) {
        CameraComponent lens = *placed;
        lens.projection = CameraProjection::Orthographic;
        lens.orthographicHeight = 10.0F;
        scene.Components().Cameras().Set(camera.entity, lens);
    }
    AddListener(scene, camera.entity, project);

    const SceneCreateResult floor = SceneCreateMenu::Create(scene, "Sprite 2D", "Floor", project);
    if (!floor.entity.IsValid()) {
        return "Basic2D template: " + floor.error;
    }
    Place(scene, floor.entity, Vec3{ 0.0F, -0.5F, 0.0F });
    Sprite2DComponent sprite{};
    sprite.pixelsPerUnit = 32.0F;
    sprite.tintR = 0.35F;
    sprite.tintG = 0.55F;
    sprite.tintB = 0.35F;
    scene.Components().Sprites2D().Set(floor.entity, sprite);
    Collider2DComponent collider{};
    collider.width = 12.0F;
    collider.height = 1.0F;
    scene.Components().Colliders2D().Set(floor.entity, collider);
    return {};
}

// A character with a controller script, and the camera it looks through (first person) or that follows behind it
// (third person). The script finds the camera by the child's name, so the names here belong to the script files.
std::string FillCharacter(Scene& scene, SceneTemplate kind, const kb::project::ProjectDescriptor& project) {
    const bool third = kind == SceneTemplate::ThirdPerson;
    const std::string label = std::string{ SceneTemplates::Name(kind) } + " template: ";
    for (const auto& [item, name] : { std::pair{ "Directional Light", "Sun" }, std::pair{ "Environment", "Environment" },
             std::pair{ "Plane", "Plane" } }) {
        const SceneCreateResult created = SceneCreateMenu::Create(scene, item, name, project);
        if (!created.entity.IsValid() && !created.pluginDisabled) {
            return label + created.error;
        }
    }

    const SceneCreateResult player = SceneCreateMenu::Create(scene, "Empty", "Player", project);
    if (!player.entity.IsValid()) {
        return label + player.error;
    }
    Place(scene, player.entity, Vec3{ 0.0F, 1.0F, 0.0F });
    CharacterControllerComponent controller{};
    controller.radius = 0.4F;
    controller.height = 1.8F;
    scene.Components().CharacterControllers().Set(player.entity, controller);
    // The script is a file of the project; its asset id follows its virtual path and type, so the scene can name it
    // before the project is ever opened.
    const std::string scriptPath = "/Game/Scripts/" + std::string{ PlayerScript(kind) };
    const kb::assets::AssetId scriptId = kb::assets::MakeAssetId(
        kb::assets::NormalizeAssetPath(std::filesystem::path{ scriptPath }) + ":" + std::string{ kb::script::ScriptAssetTypes::LuaScript });
    scene.Components().Behaviours().Set(player.entity,
        BehaviourComponent{ .behaviourAssetId = scriptId.value, .backend = BehaviourBackend::Lua, .enabled = true });

    SceneEntity cameraParent = player.entity;
    Vec3 cameraPosition{ 0.0F, 0.7F, 0.0F };
    if (third) {
        // A visible body, without the collider the Create menu gives a capsule: the character controller is the body.
        const SceneCreateResult body = SceneCreateMenu::Create(scene, "Capsule", "Body", project, player.entity);
        if (!body.entity.IsValid()) {
            return label + body.error;
        }
        static_cast<void>(SceneComponentAuthoring::Remove(scene, body.entity, "Collider"));
        if (const TransformComponent* shaped = scene.Transforms().TryGet(body.entity); shaped != nullptr) {
            TransformComponent transform = *shaped;
            transform.localScale = Vec3{ 0.8F, 0.9F, 0.8F };
            scene.Transforms().Set(body.entity, transform);
        }
        const SceneCreateResult pivot = SceneCreateMenu::Create(scene, "Empty", "CameraPivot", project, player.entity);
        if (!pivot.entity.IsValid()) {
            return label + pivot.error;
        }
        Place(scene, pivot.entity, Vec3{ 0.0F, 0.6F, 0.0F });
        cameraParent = pivot.entity;
        cameraPosition = Vec3{ 0.0F, 0.0F, -4.0F };
    }
    const SceneCreateResult camera = SceneCreateMenu::Create(scene, "Camera", "Camera", project, cameraParent);
    if (!camera.entity.IsValid()) {
        return label + camera.error;
    }
    Place(scene, camera.entity, cameraPosition);
    AddListener(scene, camera.entity, project);
    return {};
}

} // namespace

void SceneTemplates::ConfigureProjectSettings(SceneTemplate kind, kb::project::ProjectSettings& settings) {
    switch (kind) {
    case SceneTemplate::Basic2D:
        // A platformer falls faster than the real world does.
        settings.simulation.gravity = { 0.0F, -20.0F, 0.0F };
        break;
    case SceneTemplate::FirstPerson:
    case SceneTemplate::ThirdPerson:
        settings.simulation.gravity = { 0.0F, -15.0F, 0.0F };
        break;
    case SceneTemplate::Basic:
    case SceneTemplate::Empty:
        break;
    }
}

std::string SceneTemplates::WriteProjectFiles(const std::filesystem::path& projectRoot, SceneTemplate kind,
    const kb::project::ProjectDescriptor& project) {
    const std::string_view script = PlayerScript(kind);
    if (script.empty()) {
        return {};
    }
    // The input assets are found as assets of the mounted project, so the scene that writes them mounts it.
    Scene scene{ project };
    if (!scene.Assets().MountProject(projectRoot)) {
        return "The project could not be opened to write the " + std::string{ Name(kind) } + " files.";
    }
    std::ostringstream notes;
    if (!WritePlayerPresetFiles(scene, projectRoot, script, PlayerPresetOutput{ notes, notes })) {
        return notes.str();
    }
    return {};
}

std::string SceneTemplates::Fill(Scene& scene, SceneTemplate kind, const kb::project::ProjectDescriptor& project) {
    if (kind == SceneTemplate::Empty) {
        return {};
    }
    if (kind == SceneTemplate::Basic2D) {
        return FillBasic2D(scene, project);
    }
    if (kind == SceneTemplate::FirstPerson || kind == SceneTemplate::ThirdPerson) {
        return FillCharacter(scene, kind, project);
    }

    // The camera stands behind the middle of the scene and looks along +Z, the engine's forward:
    // the floor and the sky's horizon are in the shot.
    const SceneCreateResult camera = SceneCreateMenu::Create(scene, "Camera", "Main Camera", project);
    if (!camera.entity.IsValid()) {
        return "Basic template: " + camera.error;
    }
    if (const TransformComponent* placed = scene.Transforms().TryGet(camera.entity); placed != nullptr) {
        TransformComponent transform = *placed;
        transform.localPosition = Vec3{ 0.0F, 2.0F, -8.0F };
        scene.Transforms().Set(camera.entity, transform);
    }
    // The Main Camera carries the listener; without one a new scene plays no sound.
    if (const SceneComponentKind* listener = SceneComponentAuthoring::Find("AudioListener");
        listener != nullptr && SceneComponentAuthoring::PluginEnabled(project, *listener)) {
        static_cast<void>(SceneComponentAuthoring::Add(scene, camera.entity, "AudioListener", project));
    }

    // The sun from above at an angle (the Create menu's Directional Light), the procedural sky
    // with its ambient light, and the 10 x 10 m floor - without them the game view is black.
    // An item whose project plugin is off is left out, as the listener above.
    for (const auto& [item, name] : { std::pair{ "Directional Light", "Sun" }, std::pair{ "Environment", "Environment" },
             std::pair{ "Plane", "Plane" } }) {
        const SceneCreateResult created = SceneCreateMenu::Create(scene, item, name, project);
        if (!created.entity.IsValid() && !created.pluginDisabled) {
            return "Basic template: " + created.error;
        }
    }
    return {};
}

NewScenePath SceneTemplates::ResolveNewScene(const std::filesystem::path& projectRoot, std::string_view folder,
    std::string_view name) {
    if (std::string problem = kb::assets::AssetFileNameError(name); !problem.empty()) {
        return { {}, {}, std::move(problem) };
    }
    kb::assets::ProjectAssetFolder target = kb::assets::ResolveProjectAssetFolder(projectRoot, folder);
    if (!target.error.empty()) {
        return { {}, {}, std::move(target.error) };
    }
    const std::string fileName = std::string{ name } + std::string{ ".21kbscene" };
    std::filesystem::path file = target.folder / kb::assets::Utf8Path(fileName);
    std::string virtualPath = target.virtualPath + "/" + fileName;
    std::error_code error;
    if (std::filesystem::exists(file, error)) {
        return { {}, {}, "A scene named " + std::string{ name } + " already exists there. Choose another name." };
    }
    return { std::move(file), std::move(virtualPath), {} };
}

std::string SceneTemplates::WriteSceneFile(const std::filesystem::path& scenePath, SceneTemplate kind,
    const kb::project::ProjectDescriptor& project, std::uint64_t audioMixerAssetId) {
    std::error_code error;
    if (std::filesystem::exists(scenePath, error)) {
        return "A scene named " + Utf8Stem(scenePath) + " already exists. Choose another name.";
    }
    std::filesystem::create_directories(scenePath.parent_path(), error);
    if (error) {
        return "The scene folder could not be created: " + error.message();
    }
    Scene scene{ project };
    if (std::string filled = Fill(scene, kind, project); !filled.empty()) {
        return filled;
    }
    if (audioMixerAssetId != 0U) {
        SceneAudioMixerAccess::SetActiveMixer(scene, audioMixerAssetId);
    }
    if (!SceneDocumentService::Save(scene, scenePath, Utf8Stem(scenePath))) {
        return "The scene file could not be written.";
    }
    return {};
}

} // namespace kb::scene
