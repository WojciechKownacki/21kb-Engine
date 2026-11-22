#include "engine/scene/SceneTemplates.hpp"

#include "engine/project/ProjectDescriptor.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/CharacterController2DComponent.hpp"
#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/ContentInstanceComponent.hpp"
#include "engine/scene/Collider2DComponent.hpp"
#include "engine/scene/Navigation.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneComponentAuthoring.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneCreateMenu.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/ScenePlayerPreset.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/Sprite2DComponent.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/script/ScriptAsset.hpp"

#include "engine/math/EngineMath.hpp"
#include "engine/navigation/NavGeometryCollector.hpp"
#include "engine/navigation/NavMeshAsset.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/PhysicsAssembly.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetId.hpp"

#include <array>
#include <sstream>
#include <system_error>
#include <tuple>

namespace kb::scene {
namespace {

constexpr std::array<SceneTemplateInfo, 8> kTemplates{ {
    { SceneTemplate::Basic, "Basic3D", "Camera, sun, sky and a floor - ready to press Play." },
    { SceneTemplate::Basic2D, "Basic2D", "Orthographic camera and a sprite floor with a collider, on the 2D physics." },
    { SceneTemplate::FirstPerson, "FirstPerson", "A walking character seen through its own eyes, with mouse look." },
    { SceneTemplate::ThirdPerson, "ThirdPerson", "A walking character with a camera that follows behind it." },
    { SceneTemplate::TopDown, "TopDown", "A view from above: click the ground and the character walks there around obstacles." },
    { SceneTemplate::Platformer2D, "Platformer2D", "A side-view 2D game: run, jump between platforms, with a camera that follows." },
    { SceneTemplate::Vehicle, "Vehicle", "A drivable four-wheeled car on a wide field, with a camera that follows." },
    { SceneTemplate::Empty, "Empty", "Nothing at all. Add objects from the Create menu." },
} };

// "Basic" is the name Basic3D had before the 2D and character templates joined it.
constexpr std::string_view kBasicAlias = "Basic";

// The controller scripts of the character templates, files of the project under Assets/Scripts.
constexpr std::string_view kFirstPersonScript = "FirstPersonCharacter.lua";
constexpr std::string_view kThirdPersonScript = "ThirdPersonCharacter.lua";
constexpr std::string_view kTopDownScript = "TopDownCharacter.lua";
constexpr std::string_view kPlatformer2DScript = "Platformer2DBox2DPlayer.lua";
constexpr std::string_view kVehicleScript = "VehicleCar.lua";

[[nodiscard]] std::string_view PlayerScript(SceneTemplate kind) noexcept {
    switch (kind) {
    case SceneTemplate::FirstPerson: return kFirstPersonScript;
    case SceneTemplate::ThirdPerson: return kThirdPersonScript;
    case SceneTemplate::TopDown: return kTopDownScript;
    case SceneTemplate::Platformer2D: return kPlatformer2DScript;
    case SceneTemplate::Vehicle: return kVehicleScript;
    case SceneTemplate::Basic:
    case SceneTemplate::Basic2D:
    case SceneTemplate::Empty: break;
    }
    return {};
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
    const std::string label = std::string{ SceneTemplates::Name(kind) } + " template: ";
    for (const auto& [item, name] : { std::pair{ "Directional Light", "Sun" }, std::pair{ "Environment", "Environment" },
             std::pair{ "Plane", "Plane" } }) {
        const SceneCreateResult created = SceneCreateMenu::Create(scene, item, name, project);
        if (!created.entity.IsValid() && !created.pluginDisabled) {
            return label + created.error;
        }
    }
    SceneEntity player;
    return SceneTemplates::AddCharacter(scene, kind, project, "Player", player);
}

std::string AddCharacterTo(Scene& scene, SceneTemplate kind, const kb::project::ProjectDescriptor& project, std::string name,
    SceneEntity& added) {
    const bool third = kind == SceneTemplate::ThirdPerson;
    const std::string label = std::string{ SceneTemplates::Name(kind) } + " character: ";
    const SceneCreateResult player = SceneCreateMenu::Create(scene, "Empty", std::move(name), project);
    if (!player.entity.IsValid()) {
        return label + player.error;
    }
    added = player.entity;
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
    // The character's camera is what the game shows, over any camera the scene had before.
    if (const CameraComponent* lens = scene.Components().Cameras().TryGet(camera.entity); lens != nullptr) {
        CameraComponent shown = *lens;
        shown.primary = true;
        shown.priority = 50;
        scene.Components().Cameras().Set(camera.entity, shown);
    }
    AddListener(scene, camera.entity, project);
    return {};
}

// Sun and sky, the lighting every lit 3D template starts with.
std::string AddSunAndSky(Scene& scene, const kb::project::ProjectDescriptor& project, std::string_view label) {
    for (const auto& [item, name] : { std::pair{ "Directional Light", "Sun" }, std::pair{ "Environment", "Environment" } }) {
        const SceneCreateResult created = SceneCreateMenu::Create(scene, item, name, project);
        if (!created.entity.IsValid() && !created.pluginDisabled) {
            return std::string{ label } + created.error;
        }
    }
    return {};
}

void SetTransform(Scene& scene, SceneEntity entity, Vec3 position, Vec3 scale, Vec3 eulerDegrees = {}) {
    if (const TransformComponent* placed = scene.Transforms().TryGet(entity); placed != nullptr) {
        TransformComponent transform = *placed;
        transform.localPosition = position;
        transform.localScale = scale;
        transform.localRotation = kb::math::FromEulerDegrees(eulerDegrees);
        scene.Transforms().Set(entity, transform);
    }
}

// The asset id of a file of the project, named by its virtual path and type before the project is opened.
[[nodiscard]] std::uint64_t ProjectAssetId(std::string_view virtualPath, std::string_view type) {
    return kb::assets::MakeAssetId(kb::assets::NormalizeAssetPath(std::filesystem::path{ std::string{ virtualPath } }) + ":" + std::string{ type }).value;
}

// A cube of the level: a box for the 3D physics and the navigation bake, or, in a 2D level, a box for the 2D physics.
std::string AddBlock(Scene& scene, const kb::project::ProjectDescriptor& project, std::string_view label, std::string_view name,
    Vec3 position, Vec3 scale, bool physics2D) {
    const SceneCreateResult block = SceneCreateMenu::Create(scene, "Cube", std::string{ name }, project);
    if (!block.entity.IsValid()) {
        return std::string{ label } + block.error;
    }
    SetTransform(scene, block.entity, position, scale);
    if (physics2D) {
        static_cast<void>(SceneComponentAuthoring::Remove(scene, block.entity, "Collider"));
        scene.Components().Colliders2D().Set(block.entity, Collider2DComponent{});
    }
    return {};
}

// A capsule body for a character the controller moves; the capsule has no collider of its own.
std::string AddBody(Scene& scene, const kb::project::ProjectDescriptor& project, std::string_view label, SceneEntity parent, Vec3 scale) {
    const SceneCreateResult body = SceneCreateMenu::Create(scene, "Capsule", "Body", project, parent);
    if (!body.entity.IsValid()) {
        return std::string{ label } + body.error;
    }
    static_cast<void>(SceneComponentAuthoring::Remove(scene, body.entity, "Collider"));
    SetTransform(scene, body.entity, Vec3{}, scale);
    return {};
}

void AttachScript(Scene& scene, SceneEntity entity, SceneTemplate kind) {
    scene.Components().Behaviours().Set(entity,
        BehaviourComponent{ .behaviourAssetId = ProjectAssetId("/Game/Scripts/" + std::string{ PlayerScript(kind) }, kb::script::ScriptAssetTypes::LuaScript),
            .backend = BehaviourBackend::Lua, .enabled = true });
}

// A view from above: a wide floor with a few blocks, a character that walks where the pointer clicks (or with Move) and
// a camera object of its own, which the character's script keeps above and behind it. WriteSceneFile bakes the floor.
std::string FillTopDown(Scene& scene, const kb::project::ProjectDescriptor& project) {
    const std::string label = "TopDown template: ";
    if (std::string problem = AddSunAndSky(scene, project, label); !problem.empty()) {
        return problem;
    }
    for (const auto& [name, position, scale] : { std::tuple{ "Floor", Vec3{ 0.0F, -0.5F, 0.0F }, Vec3{ 30.0F, 1.0F, 30.0F } },
             std::tuple{ "Block A", Vec3{ 4.0F, 1.0F, 3.0F }, Vec3{ 3.0F, 2.0F, 3.0F } },
             std::tuple{ "Block B", Vec3{ -5.0F, 1.0F, 5.0F }, Vec3{ 2.0F, 2.0F, 6.0F } },
             std::tuple{ "Block C", Vec3{ 2.0F, 1.0F, -6.0F }, Vec3{ 8.0F, 2.0F, 2.0F } } }) {
        if (std::string problem = AddBlock(scene, project, label, name, position, scale, false); !problem.empty()) {
            return problem;
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
    // Off until the first click hands the character to the navigation mesh.
    NavAgent agent{};
    agent.height = 1.8F;
    agent.maxSpeed = 5.0F;
    agent.enabled = false;
    scene.Components().NavAgents().Set(player.entity, agent);
    AttachScript(scene, player.entity, SceneTemplate::TopDown);
    if (std::string problem = AddBody(scene, project, label, player.entity, Vec3{ 0.8F, 0.9F, 0.8F }); !problem.empty()) {
        return problem;
    }
    const SceneCreateResult camera = SceneCreateMenu::Create(scene, "Camera", "Camera", project);
    if (!camera.entity.IsValid()) {
        return label + camera.error;
    }
    SetTransform(scene, camera.entity, Vec3{ 0.0F, 9.0F, -6.5F }, Vec3{ 1.0F, 1.0F, 1.0F }, Vec3{ 55.0F, 0.0F, 0.0F });
    AddListener(scene, camera.entity, project);
    return {};
}

// A side view: an orthographic camera (a child of the player, so it follows) looks along +Z at cubes that have 2D colliders.
std::string FillPlatformer2D(Scene& scene, const kb::project::ProjectDescriptor& project) {
    const std::string label = "Platformer2D template: ";
    if (std::string problem = AddSunAndSky(scene, project, label); !problem.empty()) {
        return problem;
    }
    for (const auto& [name, position, scale] : { std::tuple{ "Ground", Vec3{ 0.0F, -0.5F, 0.0F }, Vec3{ 40.0F, 1.0F, 1.0F } },
             std::tuple{ "Platform A", Vec3{ 5.0F, 2.0F, 0.0F }, Vec3{ 4.0F, 0.5F, 1.0F } },
             std::tuple{ "Platform B", Vec3{ 11.0F, 4.0F, 0.0F }, Vec3{ 4.0F, 0.5F, 1.0F } },
             std::tuple{ "Platform C", Vec3{ 17.0F, 2.5F, 0.0F }, Vec3{ 5.0F, 0.5F, 1.0F } },
             std::tuple{ "Wall", Vec3{ -8.0F, 2.0F, 0.0F }, Vec3{ 1.0F, 5.0F, 1.0F } } }) {
        if (std::string problem = AddBlock(scene, project, label, name, position, scale, true); !problem.empty()) {
            return problem;
        }
    }
    const SceneCreateResult player = SceneCreateMenu::Create(scene, "Empty", "Player", project);
    if (!player.entity.IsValid()) {
        return label + player.error;
    }
    Place(scene, player.entity, Vec3{ 0.0F, 1.5F, 0.0F });
    CharacterController2DComponent controller{};
    controller.radius = 0.35F;
    controller.height = 1.6F;
    scene.Components().CharacterControllers2D().Set(player.entity, controller);
    AttachScript(scene, player.entity, SceneTemplate::Platformer2D);
    if (std::string problem = AddBody(scene, project, label, player.entity, Vec3{ 0.7F, 0.8F, 0.7F }); !problem.empty()) {
        return problem;
    }
    const SceneCreateResult camera = SceneCreateMenu::Create(scene, "Camera", "Camera", project, player.entity);
    if (!camera.entity.IsValid()) {
        return label + camera.error;
    }
    Place(scene, camera.entity, Vec3{ 0.0F, 1.5F, -20.0F });
    if (const CameraComponent* placed = scene.Components().Cameras().TryGet(camera.entity); placed != nullptr) {
        CameraComponent lens = *placed;
        lens.projection = CameraProjection::Orthographic;
        lens.orthographicHeight = 10.0F;
        scene.Components().Cameras().Set(camera.entity, lens);
    }
    AddListener(scene, camera.entity, project);
    return {};
}

// A wide field with two blocks to drive around, and the car: an empty object the script builds the vehicle on.
std::string FillVehicle(Scene& scene, const kb::project::ProjectDescriptor& project) {
    const std::string label = "Vehicle template: ";
    if (std::string problem = AddSunAndSky(scene, project, label); !problem.empty()) {
        return problem;
    }
    for (const auto& [name, position, scale] : { std::tuple{ "Ground", Vec3{ 0.0F, -0.5F, 0.0F }, Vec3{ 120.0F, 1.0F, 120.0F } },
             std::tuple{ "Block A", Vec3{ 8.0F, 1.0F, 20.0F }, Vec3{ 4.0F, 2.0F, 4.0F } },
             std::tuple{ "Block B", Vec3{ -8.0F, 1.0F, 30.0F }, Vec3{ 4.0F, 2.0F, 4.0F } } }) {
        if (std::string problem = AddBlock(scene, project, label, name, position, scale, false); !problem.empty()) {
            return problem;
        }
    }
    const SceneCreateResult car = SceneCreateMenu::Create(scene, "Empty", "Car", project);
    if (!car.entity.IsValid()) {
        return label + car.error;
    }
    Place(scene, car.entity, Vec3{ 0.0F, 1.0F, 0.0F });
    AttachScript(scene, car.entity, SceneTemplate::Vehicle);
    // The camera hangs on a pivot that the script keeps on the car, turned the way the car faces.
    const SceneCreateResult pivot = SceneCreateMenu::Create(scene, "Empty", "CameraPivot", project);
    if (!pivot.entity.IsValid()) {
        return label + pivot.error;
    }
    Place(scene, pivot.entity, Vec3{ 0.0F, 1.0F, 0.0F });
    const SceneCreateResult camera = SceneCreateMenu::Create(scene, "Camera", "Camera", project, pivot.entity);
    if (!camera.entity.IsValid()) {
        return label + camera.error;
    }
    SetTransform(scene, camera.entity, Vec3{ 0.0F, 4.0F, -9.0F }, Vec3{ 1.0F, 1.0F, 1.0F }, Vec3{ 18.0F, 0.0F, 0.0F });
    AddListener(scene, camera.entity, project);
    return {};
}

// The virtual path of the navigation mesh beside a scene file inside the project's content root; empty outside it.
std::string NavMeshVirtualPath(const std::filesystem::path& scenePath, std::string_view contentRoot) {
    const std::filesystem::path mesh = kb::navigation::SceneNavMeshPath(scenePath);
    const std::filesystem::path rootName = kb::assets::Utf8Path(std::string{ contentRoot });
    std::filesystem::path root = mesh.parent_path();
    while (root.filename() != rootName && root.has_parent_path() && root.parent_path() != root) {
        root = root.parent_path();
    }
    if (root.filename() != rootName) {
        return {};
    }
    const std::u8string relative = mesh.lexically_relative(root).generic_u8string();
    return "/Game/" + std::string{ reinterpret_cast<const char*>(relative.data()), relative.size() };
}

} // namespace

void SceneTemplates::ConfigureProjectSettings(SceneTemplate kind, kb::project::ProjectSettings& settings) {
    switch (kind) {
    case SceneTemplate::Basic2D:
    case SceneTemplate::Platformer2D:
        // A platformer falls faster than the real world does.
        settings.simulation.gravity = { 0.0F, -20.0F, 0.0F };
        break;
    case SceneTemplate::FirstPerson:
    case SceneTemplate::ThirdPerson:
    case SceneTemplate::TopDown:
        settings.simulation.gravity = { 0.0F, -15.0F, 0.0F };
        break;
    case SceneTemplate::Vehicle:
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
    if (kind == SceneTemplate::Vehicle) {
        // The car's body, wheels and engine are this asset; the script loads it.
        const std::filesystem::path carFile = projectRoot / "Assets" / "Physics" / "Car.kbphysics";
        std::error_code fileError;
        std::filesystem::create_directories(carFile.parent_path(), fileError);
        std::string saveError;
        if (!SavePhysicsAssembly(carFile, PhysicsAssemblyAsset{ .kind = PhysicsAssemblyKind::Vehicle, .engineTorque = 300.0F }, saveError)) {
            return "The car asset could not be written: " + saveError;
        }
    }
    return {};
}

std::string SceneTemplates::AddCharacter(Scene& scene, SceneTemplate kind, const kb::project::ProjectDescriptor& project,
    std::string name, SceneEntity& player) {
    if (kind != SceneTemplate::FirstPerson && kind != SceneTemplate::ThirdPerson) {
        return "only the FirstPerson and ThirdPerson templates have a walking character";
    }
    return AddCharacterTo(scene, kind, project, std::move(name), player);
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
    if (kind == SceneTemplate::TopDown) {
        return FillTopDown(scene, project);
    }
    if (kind == SceneTemplate::Platformer2D) {
        return FillPlatformer2D(scene, project);
    }
    if (kind == SceneTemplate::Vehicle) {
        return FillVehicle(scene, project);
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
    // The top-down floor is baked into a navigation mesh beside the scene, which places it as an object.
    const std::string navMeshPath = kind == SceneTemplate::TopDown ? NavMeshVirtualPath(scenePath, project.contentRoot) : std::string{};
    if (kind == SceneTemplate::TopDown) {
        if (navMeshPath.empty()) {
            return "The TopDown template needs a scene inside the project's content folder.";
        }
        SceneObjectDesc holder{};
        holder.name = "Navigation Mesh";
        const SceneEntity entity = scene.Entities().CreateEntity(std::move(holder));
        if (!entity.IsValid()) {
            return "The navigation mesh object could not be created.";
        }
        scene.Components().ContentInstances().Set(entity,
            ContentInstanceComponent{ .assetId = ProjectAssetId(navMeshPath, kb::navigation::NavMeshAsset::AssetType), .kind = ContentInstanceKind::NavigationMesh });
    }
    if (!SceneDocumentService::Save(scene, scenePath, Utf8Stem(scenePath))) {
        return "The scene file could not be written.";
    }
    if (kind == SceneTemplate::TopDown) {
        kb::navigation::NavMeshBuildSettings settings;
        settings.renderMeshes = false;
        const kb::navigation::NavSceneBakeResult baked = kb::navigation::BakeSceneNavMesh(scenePath, settings, nullptr);
        std::string meshError;
        if (!baked.succeeded) {
            return "The navigation mesh could not be baked: " + baked.error;
        }
        if (!kb::navigation::NavMeshAssetIO::Write(kb::navigation::SceneNavMeshPath(scenePath), baked.asset, meshError)) {
            return "The navigation mesh could not be written: " + meshError;
        }
    }
    return {};
}

} // namespace kb::scene
