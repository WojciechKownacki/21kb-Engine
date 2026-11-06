#include "CliCommands.hpp"
#include "CliPlayerPreset.hpp"
#include "CliProgram.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/assets/BuiltInShapes.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/SceneAnimatorComponents.hpp"
#include "engine/scene/SceneSkeletalMeshSetup.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletonNames.hpp"
#include "engine/gameplay/AbilityAsset.hpp"
#include "engine/project/ProjectCreator.hpp"
#include "engine/scene/AIBrainComponent.hpp"
#include "RiverWaterMaterials.hpp"
#if KB_CLI_RENDERER
#include "engine/render/StaticLightmapBaker.hpp"
#include "kb/render/runtime/RuntimeRenderAssetDiscovery.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "engine/scene/SceneLightComponents.hpp"
#include <fstream>
#include <cmath>
#include "Map2DEditing.hpp"
#include "Map2DView.hpp"
#include "Sprite2DRuntime.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/assets/Map2DAsset.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#endif
#include "engine/scene/CombatantComponent.hpp"
#include "engine/scene/ContentInstanceComponent.hpp"
#include "engine/scene/Navigation.hpp"
#include "engine/scene/SocketAttachmentComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneUIDependencies.hpp"
#include "engine/scene/SceneComponentAuthoring.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/SceneCreateMenu.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneJointComponents.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/scene/SceneUIHierarchyPresets.hpp"
#include "engine/scene/SceneSprite2DComponents.hpp"
#include "engine/scene/SceneUIComponents.hpp"
#include "engine/ui/UIComponentCatalog.hpp"
#include "engine/ui/UIComponentPropertyCatalog.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "engine/script/ScriptValue.hpp"

#include <sstream>
#include <chrono>
#include <memory>
#include <thread>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <tuple>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Scene authoring for agents: the same entities and components the editor's hierarchy and Inspector
// create, through the same engine tables (SceneComponentAuthoring, ScriptSceneComponentApi).
namespace kb::cli {
namespace {

// A loaded, editable scene of a project; Save() writes it back where it came from.
// One edit of a scene at a time: held from reading the scene to writing it back, so commands an
// agent sends together wait for each other instead of writing over each other (two writers left
// the scene and its .meta out of step). The lock file goes away with its handle, even when the
// process dies.
class SceneFileLock final {
public:
    explicit SceneFileLock(const std::filesystem::path& scene) {
#if defined(_WIN32)
        const std::wstring path = scene.wstring() + L".lock";
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 60 };
        while (true) {
            handle_ = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
            if (handle_ != INVALID_HANDLE_VALUE || GetLastError() != ERROR_SHARING_VIOLATION ||
                std::chrono::steady_clock::now() > deadline) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{ 20 });
        }
#else
        descriptor_ = ::open((scene.string() + ".lock").c_str(), O_CREAT | O_RDWR, 0644);
        if (descriptor_ >= 0) static_cast<void>(::flock(descriptor_, LOCK_EX));
#endif
    }
    ~SceneFileLock() {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
        if (descriptor_ >= 0) ::close(descriptor_);
#endif
    }
    SceneFileLock(const SceneFileLock&) = delete;
    SceneFileLock& operator=(const SceneFileLock&) = delete;

private:
#if defined(_WIN32)
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int descriptor_ = -1;
#endif
};

struct SceneSession {
    kb::project::ProjectDescriptor descriptor;
    std::unique_ptr<kb::scene::Scene> scene;
    std::filesystem::path path;
    std::string name;
    std::shared_ptr<SceneFileLock> lock;
    // Kept open by a SceneEditBatch: Save() only notes the change, the batch writes the scene once.
    bool kept = false;
    bool changed = false;

    [[nodiscard]] bool Save() {
        if (kept) {
            changed = true;
            return true;
        }
        return Write();
    }
    [[nodiscard]] bool Write() { return kb::scene::SceneDocumentService::Save(*scene, path, name); }
};

using SceneSessionPtr = std::shared_ptr<SceneSession>;

// The scenes a SceneEditBatch holds open, each under the project and the --scene spellings that led to it.
struct OpenScenes {
    struct Entry {
        std::filesystem::path projectFile;
        std::vector<std::string> sceneOptions;
        SceneSessionPtr session;
    };
    std::vector<Entry> entries;
    bool keeping = false;

    [[nodiscard]] SceneSessionPtr Find(const std::filesystem::path& projectFile, const std::string& sceneOption) const {
        for (const Entry& entry : entries) {
            if (entry.projectFile == projectFile && std::ranges::find(entry.sceneOptions, sceneOption) != entry.sceneOptions.end()) {
                return entry.session;
            }
        }
        return nullptr;
    }
    // The scene file is the same one however --scene spells it (/Game/..., Assets/..., none for the default scene).
    [[nodiscard]] SceneSessionPtr FindFile(const std::filesystem::path& projectFile, const std::filesystem::path& file,
        const std::string& sceneOption) {
        for (Entry& entry : entries) {
            std::error_code ec;
            if (entry.projectFile == projectFile && std::filesystem::equivalent(entry.session->path, file, ec) && !ec) {
                entry.sceneOptions.push_back(sceneOption);
                return entry.session;
            }
        }
        return nullptr;
    }
};

thread_local OpenScenes* gOpenScenes = nullptr;

[[nodiscard]] std::filesystem::path Utf8Path(std::string_view text) {
    return std::filesystem::path{ std::u8string{ reinterpret_cast<const char8_t*>(text.data()), text.size() } };
}

[[nodiscard]] SceneSessionPtr OpenScene(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: --project <dir> is required\n";
        return nullptr;
    }
    const std::filesystem::path root = Utf8Path(*project);
    std::string error;
    const std::filesystem::path projectFile = kb::project::ProjectManager::FindProjectFile(root, error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return nullptr;
    }
    const std::string requestedScene = arguments.Option("--scene").value_or("");
    OpenScenes* const batch = gOpenScenes != nullptr && gOpenScenes->keeping ? gOpenScenes : nullptr;
    if (batch != nullptr) {
        if (SceneSessionPtr open = batch->Find(projectFile, requestedScene)) {
            // As freshly loaded: the transforms the earlier operations changed are settled.
            open->scene->Runtime().SynchronizeTransforms();
            return open;
        }
    }
    kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(projectFile);
    if (!loaded.succeeded) {
        io.err << "error: project descriptor could not be loaded: " << loaded.error << '\n';
        return nullptr;
    }

    auto session = std::make_shared<SceneSession>();
    session->descriptor = std::move(loaded.descriptor);
    session->scene = std::make_unique<kb::scene::Scene>(session->descriptor);
    if (!MountProjectAssets(*session->scene, projectFile.parent_path(), error)) {
        io.err << "error: " << error << '\n';
        return nullptr;
    }
    // Without --scene: the scene the game starts in (ProjectSettings::defaultMap).
    std::string sceneOption = requestedScene;
    if (sceneOption.empty()) {
        const kb::project::ProjectSettingsLoadResult settings =
            kb::project::ProjectSettingsStore::Load(kb::project::ProjectSettingsStore::FilePath(projectFile.parent_path()));
        sceneOption = settings.settings.defaultMap;
    }
    if (session->scene->Assets().Manager().Mounts().Resolve(Utf8Path(sceneOption)).has_value()) {
        const kb::assets::AssetMetadata* metadata =
            FindAssetByFlexiblePath(session->scene->Assets().Manager().Registry(), sceneOption);
        if (metadata == nullptr) {
            io.err << "error: scene asset was not found: " << sceneOption << '\n';
            return nullptr;
        }
        session->path = metadata->physicalPath;
    } else {
        session->path = ResolveProjectFile(Utf8Path(sceneOption), projectFile.parent_path(), error);
        if (session->path.empty()) {
            io.err << "error: " << error << '\n';
            return nullptr;
        }
    }
    // The same scene under another spelling is one open scene: a second copy would wait for the first one's lock.
    if (batch != nullptr) {
        if (SceneSessionPtr open = batch->FindFile(projectFile, session->path, requestedScene)) {
            open->scene->Runtime().SynchronizeTransforms();
            return open;
        }
    }
    session->lock = std::make_shared<SceneFileLock>(session->path);
    const kb::scene::SceneDocumentLoadResult document = kb::scene::SceneDocumentService::Load(session->path);
    if (!document.succeeded) {
        io.err << "error: could not load scene " << session->path.generic_string() << ": " << document.error << '\n';
        return nullptr;
    }
    session->name = document.document.name;
    if (!kb::scene::SceneDocumentService::LoadIntoScene(*session->scene, document.document)) {
        io.err << "error: scene " << session->path.generic_string() << " could not be instantiated\n";
        return nullptr;
    }
    if (batch != nullptr) {
        session->kept = true;
        batch->entries.push_back(OpenScenes::Entry{ .projectFile = projectFile, .sceneOptions = { requestedScene }, .session = session });
    }
    return session;
}

// Files the kept scenes have not seen (a material just made) are looked for again.
void RefreshOpenScenes() {
    if (gOpenScenes == nullptr) return;
    for (const OpenScenes::Entry& entry : gOpenScenes->entries) {
        static_cast<void>(entry.session->scene->Assets().Discover());
    }
}

void ReportAmbiguous(const kb::scene::Scene& scene, std::string_view name, std::span<const kb::scene::SceneEntity> matches,
    CommandIo io) {
    io.err << "error: " << matches.size() << " entities are named '" << name << "':";
    for (const kb::scene::SceneEntity entity : matches) {
        io.err << " '" << EntityPath(scene, entity) << "'";
    }
    io.err << ". Address one by its path (Parent/Name), or give it its own name: "
              "kb_cli entity rename --project <dir> --name '" << name << "' --occurrence <1-" << matches.size()
           << "> --to <NewName>\n";
}

// The one entity called `name` (or at that path); refuses (with the reason) none or several.
[[nodiscard]] std::optional<kb::scene::SceneEntity> FindEntity(const kb::scene::Scene& scene, std::string_view name, CommandIo io) {
    const std::vector<kb::scene::SceneEntity> matches = MatchEntities(scene, name);
    if (matches.size() == 1U) {
        return matches.front();
    }
    if (matches.empty()) {
        io.err << "error: no entity named '" << name << "'. Entities:";
        for (const kb::scene::SceneEntity entity : AllEntities(scene)) {
            io.err << " '" << EntityPath(scene, entity) << "'";
        }
        io.err << '\n';
    } else {
        ReportAmbiguous(scene, name, matches, io);
    }
    return std::nullopt;
}

// A name the CLI can address: no other child of `parent` (or root, when invalid) carries it.
[[nodiscard]] bool NameFreeUnder(const kb::scene::Scene& scene, kb::scene::SceneEntity parent, std::string_view name,
    kb::scene::SceneEntity except = {}) {
    const std::vector<kb::scene::SceneEntity> siblings =
        parent.IsValid() ? scene.Hierarchy().ChildEntities(parent) : scene.Hierarchy().RootEntities();
    for (const kb::scene::SceneEntity sibling : siblings) {
        if (sibling != except && scene.Entities().Name(sibling) == name) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::vector<float>> ParseFloats(std::string_view text, std::size_t count) {
    std::vector<float> values;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::string_view part = text.substr(0, comma);
        float value = 0.0F;
        const auto [end, code] = std::from_chars(part.data(), part.data() + part.size(), value);
        if (code != std::errc{} || end != part.data() + part.size()) {
            return std::nullopt;
        }
        values.push_back(value);
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1U);
    }
    if (values.size() != count) {
        return std::nullopt;
    }
    return values;
}

[[nodiscard]] std::string_view TypeName(kb::script::ScriptValueType type) noexcept {
    using enum kb::script::ScriptValueType;
    switch (type) {
    case Bool: return "bool";
    case Int: case Int64: case UInt32: return "int";
    case Float: case Double: return "number";
    case String: case Name: return "text";
    case Guid: case Hash: return "id";
    case Entity: return "entity";
    default: return "value";
    }
}

[[nodiscard]] std::optional<kb::script::ScriptValue> ParseValue(std::string_view text, kb::script::ScriptValueType type) {
    using enum kb::script::ScriptValueType;
    const auto number = [text]<typename T>(T& out) {
        const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), out);
        return code == std::errc{} && end == text.data() + text.size();
    };
    switch (type) {
    case Bool:
        if (text == "true" || text == "1") return kb::script::ScriptValue{ true };
        if (text == "false" || text == "0") return kb::script::ScriptValue{ false };
        return std::nullopt;
    case Int: { int value = 0; return number(value) ? std::optional{ kb::script::ScriptValue{ value } } : std::nullopt; }
    case Int64: { std::int64_t value = 0; return number(value) ? std::optional{ kb::script::ScriptValue{ value } } : std::nullopt; }
    case UInt32: { std::uint32_t value = 0; return number(value) ? std::optional{ kb::script::ScriptValue{ value } } : std::nullopt; }
    case Float: { float value = 0; return number(value) ? std::optional{ kb::script::ScriptValue{ value } } : std::nullopt; }
    case Double: { double value = 0; return number(value) ? std::optional{ kb::script::ScriptValue{ value } } : std::nullopt; }
    case String: return kb::script::ScriptValue{ std::string{ text } };
    case Name: return kb::script::ScriptValue{ std::string{ text }, Name };
    case Guid: case Hash: {
        std::uint64_t value = 0;
        return number(value) ? std::optional{ kb::script::ScriptValue{ value, type } } : std::nullopt;
    }
    default: return std::nullopt;
    }
}

[[nodiscard]] const kb::script::ScriptSceneComponentPropertyDesc* FindProperty(std::string_view component, std::string_view property) {
    for (const kb::script::ScriptSceneComponentPropertyDesc& desc : kb::script::ScriptSceneComponentApi::ComponentProperties(component)) {
        if (desc.name == property) {
            return &desc;
        }
    }
    return nullptr;
}

void ListProperties(std::string_view component, std::ostream& out) {
    for (const kb::script::ScriptSceneComponentPropertyDesc& desc : kb::script::ScriptSceneComponentApi::ComponentProperties(component)) {
        if (desc.writable) {
            out << ' ' << desc.name;
        }
    }
}

// One `name=value`: a property of `component` as ScriptSceneComponentApi names it, or a vector
// written once (`localPosition=0,2,-8`, `color=1,0.9,0.8`), or Transform's `rotation` in degrees.
[[nodiscard]] bool ApplySetting(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::string_view component,
    std::string_view setting, CommandIo io) {
    component = kb::scene::SceneComponentAuthoring::CanonicalName(component);
    const std::size_t equals = setting.find('=');
    if (equals == std::string_view::npos || equals == 0U) {
        io.err << "error: --set expects name=value, got '" << setting << "'\n";
        return false;
    }
    const std::string_view name = setting.substr(0, equals);
    const std::string_view value = setting.substr(equals + 1U);

    // A character: a Skeletal Mesh given as the mesh makes the entity draw it deformed by its
    // skeleton (Deformed Geometry and Skeleton Binding with it), as dropping it in the editor does.
    if ((component == "MeshRenderer" && name == "mesh") || (component == "DeformedGeometry" && name == "skeletalMesh")) {
        const kb::assets::AssetMetadata* metadata =
            FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), Utf8Path(value));
        if (metadata != nullptr && metadata->type == kb::scene::kSkeletalMeshAssetType) {
            const kb::scene::SkeletalMeshSetupResult attached =
                kb::scene::SceneSkeletalMeshSetup::Attach(scene, entity, metadata->id);
            if (!attached.succeeded) io.err << "error: " << name << " '" << value << "': " << attached.error << '\n';
            return attached.succeeded;
        }
        if (component == "DeformedGeometry") {
            io.err << "error: skeletalMesh '" << value << "' is not a Skeletal Mesh (.kbskeletalmesh) of the project\n";
            return false;
        }
    }

    // A Combatant's ability slot by the ability's path (ability1=/Game/Abilities/Fireball.kbability).
    if (component == "Combatant" && name.starts_with("ability") && !value.empty() && !std::isdigit(static_cast<unsigned char>(value.front()))) {
        std::uint64_t id = 0U;
        if (value != "none") {
            const kb::assets::AssetMetadata* metadata = FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), Utf8Path(value));
            if (metadata == nullptr || metadata->type != kb::gameplay::kAbilityAssetType) {
                io.err << "error: " << name << " '" << value << "' is not an ability (.kbability) of the project (create one with kb_cli ability new)\n";
                return false;
            }
            id = metadata->id.value;
        }
        return ApplySetting(scene, entity, component, std::string{ name } + "=" + std::to_string(id), io);
    }

    // The Animator's controller by its path (/Game/Characters/Hero.kbanimcontroller).
    if (component == "Animator" && name == "controller") {
        kb::scene::Animator* animator = scene.Components().Animators().TryGet(entity);
        const kb::assets::AssetMetadata* metadata =
            value == "none" ? nullptr : FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), Utf8Path(value));
        if (animator == nullptr) {
            io.err << "error: the entity has no Animator\n";
            return false;
        }
        if (value != "none" && (metadata == nullptr || metadata->type != kb::scene::kAnimatorControllerAssetType)) {
            io.err << "error: controller '" << value << "' is not an Animator Controller (.kbanimcontroller) of the project"
                   << " (create one with kb_cli animator new)\n";
            return false;
        }
        animator->controllerAssetId = metadata == nullptr ? 0U : metadata->id.value;
        scene.Components().Animators().MarkModified(entity);
        return true;
    }

    // Asset references by path: an engine shape (/Engine/Shapes/Plane, or just Plane) or a project
    // asset (/Game/Models/Car.obj). The id is the asset's, not something the author types.
    if (component == "MeshRenderer" && (name == "mesh" || name == "material")) {
        std::uint64_t id = 0U;
        if (const kb::assets::BuiltInShapeDesc* shape = kb::assets::FindBuiltInShape(value); shape != nullptr && name == "mesh") {
            id = kb::assets::BuiltInShapeId(shape->shape).value;
        } else if (value != "none") {
            const kb::assets::AssetMetadata* metadata =
                FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), Utf8Path(value));
            const std::string_view wanted = name == "mesh" ? "RenderMesh" : "RenderMaterial";
            if (metadata == nullptr || metadata->type.find(wanted) == std::string::npos) {
                io.err << "error: " << name << " '" << value << "' is not a " << wanted << " asset"
                       << (name == "mesh" ? " (engine shapes: Cube Sphere Capsule Cylinder Cone Plane Quad)" : "") << '\n';
                return false;
            }
            id = metadata->id.value;
        }
        kb::scene::MeshRendererComponent renderer = *scene.Components().MeshRenderers().TryGet(entity);
        (name == "mesh" ? renderer.meshAssetId : renderer.materialAssetId) = id;
        scene.Components().MeshRenderers().Set(entity, renderer);
        return true;
    }

    // A particle effect by its path (/Game/Effects/Sparks.kbvfx), as the Mesh Renderer's mesh. Choosing
    // an effect enables the component, as the editor's asset field does.
    if (component == "Particle Effect" && name == "effectAssetId" && value.starts_with('/')) {
        const kb::assets::AssetMetadata* metadata =
            FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), Utf8Path(value));
        if (metadata == nullptr || metadata->type != kb::scene::kParticleEffectAssetType) {
            io.err << "error: effectAssetId '" << value << "' is not a particle effect (.kbvfx) of the project\n";
            return false;
        }
        kb::scene::ParticleEffectComponent* effect = scene.Components().ParticleEffects().TryGet(entity);
        if (effect == nullptr) {
            io.err << "error: the entity has no Particle Effect\n";
            return false;
        }
        effect->effectAssetId = metadata->id.value;
        effect->enabled = true;
        return true;
    }

    // An authored asset reference by its project path, as the Mesh Renderer's.
    const kb::scene::UIComponentDescriptor* ui = kb::scene::FindUIComponentDescriptor(component);
    const auto* uiProperty = ui != nullptr ? kb::scene::FindUIComponentProperty(ui->type, name) : nullptr;
    if (((component == "AudioSource" && name == "clipAssetId") ||
        ((component == "Ambient Radiance" || component == "WorldBackdrop") && name == "environmentAssetId") ||
        (component == "ContentInstance" && name == "assetId") ||
        (component == "River" && (name == "materialAssetId" || name == "waterfallMaterialAssetId" || name == "mistEffectAssetId")) ||
        (uiProperty != nullptr && uiProperty->type == kb::scene::UIComponentPropertyType::Asset)) && value.starts_with('/')) {
        const kb::assets::AssetMetadata* metadata = FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), Utf8Path(value));
        if (metadata == nullptr) {
            io.err << "error: " << component << '.' << name << " '" << value << "' is not an asset of the project\n";
            return false;
        }
        if (component == "ContentInstance") {
            constexpr std::array<std::string_view,5> types{"ScenePrefab","Scene","WorldFragment","World","NavMesh"};
            const auto* instance=scene.Components().ContentInstances().TryGet(entity);
            const auto kind=instance?static_cast<std::size_t>(instance->kind):types.size();
            if (kind>=types.size() || metadata->type!=types[kind]) {
                io.err << "error: ContentInstance.assetId must match its source kind; set kind before assetId\n";
                return false;
            }
        }
        const auto set = kb::script::ScriptSceneComponentApi::SetProperty(scene, entity, component, name,
            kb::script::ScriptValue{ metadata->id.value, kb::script::ScriptValueType::Hash });
        if (!set.succeeded) {
            io.err << "error: " << component << '.' << name << ": " << set.error << '\n';
            return false;
        }
        return true;
    }

    // A Sprite 2D's picture and material by their project paths. Choosing the picture also records its size in pixels,
    // so the sprite can be cut into cells without opening the image at run time.
    if (component == "Sprite2D" && (name == "image" || name == "material" || ((name == "imageAssetId" || name == "materialAssetId") && value.starts_with('/')))) {
#if KB_CLI_RENDERER
        const bool picture = name == "image" || name == "imageAssetId";
        kb::scene::Sprite2DComponent* sprite = scene.Components().Sprites2D().TryGet(entity);
        if (sprite == nullptr) {
            io.err << "error: the entity has no Sprite 2D (use component add --type Sprite2D)\n";
            return false;
        }
        const kb::assets::AssetMetadata* metadata = nullptr;
        if (value != "none") {
            metadata = FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), Utf8Path(value));
            if (metadata == nullptr || metadata->type.find(picture ? "Texture" : "RenderMaterial") == std::string::npos) {
                io.err << "error: " << name << " '" << value << "' is not a " << (picture ? "picture (an image of the project)" : "material (.kbmat) of the project") << '\n';
                return false;
            }
        }
        if (picture) return kb::world2d::AssignSprite2DImage(scene, entity, metadata);
        sprite->materialAssetId = metadata == nullptr ? 0U : metadata->id.value;
        scene.Components().Sprites2D().MarkModified(entity);
        return true;
#else
        io.err << "error: Sprite2D asset selection requires a CLI built with renderer support\n";
        return false;
#endif
    }

    // The other body of a joint, by its name (empty = the world), as the editor's Connected Body.
    if (component == "Joint" && name == "connectedEntity") {
        kb::scene::JointComponent* joint = scene.Components().Joints().TryGet(entity);
        if (joint == nullptr) {
            io.err << "error: the entity has no Joint to connect\n";
            return false;
        }
        kb::scene::SceneEntity other{};
        if (!value.empty()) {
            const std::optional<kb::scene::SceneEntity> found = FindEntity(scene, value, io);
            if (!found.has_value()) return false;
            if (*found == entity) {
                io.err << "error: a joint cannot connect an entity to itself\n";
                return false;
            }
            other = *found;
        }
        joint->connectedEntity = other;
        scene.Components().Joints().MarkModified(entity);
        return true;
    }

    // SocketAttachment's offset as Transform's is written: position, rotation (degrees). The
    // object's size is its Transform's scale.
    if (component == "SocketAttachment" && name == "position") {
        return ApplySetting(scene, entity, component, "offset." + std::string{ name } + "=" + std::string{ value }, io);
    }
    if (component == "SocketAttachment" && name == "rotation") {
        const std::optional<std::vector<float>> degrees = ParseFloats(value, 3U);
        if (!degrees.has_value()) {
            io.err << "error: rotation expects three angles in degrees, x,y,z\n";
            return false;
        }
        const kb::math::Quat rotation = kb::math::FromEulerDegrees(kb::math::Vec3{ (*degrees)[0], (*degrees)[1], (*degrees)[2] });
        return ApplySetting(scene, entity, component, "offset.rotation=" + std::to_string(rotation.x) + "," + std::to_string(rotation.y) +
            "," + std::to_string(rotation.z) + "," + std::to_string(rotation.w), io);
    }

    if (component == "Transform" && name == "rotation") {
        const std::optional<std::vector<float>> degrees = ParseFloats(value, 3U);
        if (!degrees.has_value()) {
            io.err << "error: rotation expects three angles in degrees, x,y,z\n";
            return false;
        }
        const kb::math::Quat rotation = kb::math::FromEulerDegrees(kb::math::Vec3{ (*degrees)[0], (*degrees)[1], (*degrees)[2] });
        const float parts[] = { rotation.x, rotation.y, rotation.z, rotation.w };
        const char* axes[] = { "localRotation.x", "localRotation.y", "localRotation.z", "localRotation.w" };
        for (int axis = 0; axis < 4; ++axis) {
            const auto set = kb::script::ScriptSceneComponentApi::SetProperty(scene, entity, component, axes[axis], kb::script::ScriptValue{ parts[axis] });
            if (!set.succeeded) {
                io.err << "error: rotation: " << set.error << '\n';
                return false;
            }
        }
        return true;
    }

    std::vector<std::pair<std::string, std::string>> writes;
    if (FindProperty(component, name) != nullptr) {
        writes.emplace_back(std::string{ name }, std::string{ value });
    } else {
        // A vector property written at once: name.x, name.y, name.z(, name.w). A colour may be
        // written #RRGGBB, as a colour picker shows it (sRGB); the property stores it linear.
        std::string linearColor;
        std::string_view vectorText = value;
        if (!value.empty() && value.front() == '#') {
            const std::optional<std::array<float, 4>> color = ParseColorText(value, 3U);
            if (!color.has_value()) {
                io.err << "error: " << component << '.' << name << " expects #RRGGBB, got '" << value << "'\n";
                return false;
            }
            linearColor = std::to_string((*color)[0]) + "," + std::to_string((*color)[1]) + "," + std::to_string((*color)[2]);
            vectorText = linearColor;
        }
        std::vector<std::string_view> parts;
        for (std::string_view rest = vectorText; ;) {
            const std::size_t comma = rest.find(',');
            parts.push_back(rest.substr(0, comma));
            if (comma == std::string_view::npos) break;
            rest = rest.substr(comma + 1U);
        }
        const char* axes[] = { ".x", ".y", ".z", ".w" };
        for (std::size_t axis = 0; axis < parts.size() && axis < 4U; ++axis) {
            if (FindProperty(component, std::string{ name } + axes[axis]) == nullptr) {
                writes.clear();
                break;
            }
            writes.emplace_back(std::string{ name } + axes[axis], std::string{ parts[axis] });
        }
        if (writes.empty() || parts.size() > 4U) {
            io.err << "error: " << component << " has no property '" << name << "'. Properties:";
            ListProperties(component, io.err);
            io.err << '\n';
            return false;
        }
    }
    for (const auto& [property, text] : writes) {
        const kb::script::ScriptSceneComponentPropertyDesc* desc = FindProperty(component, property);
        if (!desc->writable) {
            io.err << "error: " << component << '.' << property << " is read-only\n";
            return false;
        }
        std::optional<kb::script::ScriptValue> parsed = ParseValue(text, desc->type);
        // An object by its name or path (Menu/PlayButton); none clears the reference.
        if (desc->type == kb::script::ScriptValueType::Entity) {
            if (text == "none" || text.empty()) {
                parsed = kb::script::ScriptValue{ std::uint64_t{ 0U }, kb::script::ScriptValueType::Entity };
            } else {
                const std::optional<kb::scene::SceneEntity> referenced = FindEntity(scene, text, io);
                if (!referenced.has_value()) return false;
                parsed = kb::script::ScriptValue{ referenced->Id(), kb::script::ScriptValueType::Entity };
            }
        }
        const auto named = std::find(desc->enumNames.begin(), desc->enumNames.end(), text);
        if (!parsed.has_value() && named != desc->enumNames.end()) {
            parsed = kb::script::ScriptValue{ static_cast<int>(named - desc->enumNames.begin()) };
        }
        if (!parsed.has_value()) {
            io.err << "error: " << component << '.' << property << " expects " << TypeName(desc->type) << ", got '" << text << "'";
            for (std::size_t index = 0; index < desc->enumNames.size(); ++index) {
                io.err << (index == 0U ? " (" : ", ") << desc->enumNames[index];
            }
            io.err << (desc->enumNames.empty() ? "\n" : ")\n");
            return false;
        }
        const auto set = kb::script::ScriptSceneComponentApi::SetProperty(scene, entity, component, property, *parsed);
        if (!set.succeeded) {
            io.err << "error: " << component << '.' << property << ": " << set.error << '\n';
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ApplySettings(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::string_view component,
    const ArgumentList& arguments, CommandIo io) {
    for (const std::string& setting : arguments.Options("--set")) {
        if (!ApplySetting(scene, entity, component, setting, io)) {
            return false;
        }
    }
    return true;
}

// The scene as the project names it (/Game/Scenes/Main.21kbscene): short, and the same on every
// machine, unlike the file's place on disk.
[[nodiscard]] std::string ProjectScenePath(const std::filesystem::path& path) {
    const std::string text = path.generic_string();
    const std::size_t assets = text.rfind("/Assets/");
    return assets == std::string::npos ? text : "/Game/" + text.substr(assets + 8U);
}

[[nodiscard]] int Saved(SceneSession& session, std::string_view what, CommandIo io) {
    if (!session.Save()) {
        io.err << "error: scene " << session.path.generic_string() << " could not be written\n";
        return 1;
    }
    io.out << what << " (" << ProjectScenePath(session.path) << ")\n";
    return 0;
}

// --position, --rotation, --scale, then --look-at: turns the entity's forward (+Z) toward a point
// "x,y,z" or another entity, keeping +Y up - aiming a camera or a light without working out angles.
[[nodiscard]] bool ApplyTransformOptions(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const ArgumentList& arguments, CommandIo io) {
    for (const auto& [option, property] : { std::pair{ "--position", "localPosition" }, std::pair{ "--rotation", "rotation" },
             std::pair{ "--scale", "localScale" } }) {
        if (const std::optional<std::string> value = arguments.Option(option); value.has_value() &&
            !ApplySetting(scene, entity, "Transform", std::string{ property } + "=" + *value, io)) {
            return false;
        }
    }
    const std::optional<std::string> lookAt = arguments.Option("--look-at");
    if (!lookAt.has_value()) return true;
    scene.Runtime().SynchronizeTransforms();
    std::optional<kb::math::Vec3> target = ParseVec3(*lookAt);
    if (!target.has_value()) {
        const std::optional<kb::scene::SceneEntity> other = FindEntity(scene, *lookAt, io);
        if (!other.has_value()) return false;
        target = scene.Transforms().Get(*other).worldPosition;
    }
    const kb::scene::TransformComponent self = scene.Transforms().Get(entity);
    const kb::math::Vec3 forward = *target - self.worldPosition;
    if (kb::math::Length(forward) < 1e-4F) {
        io.err << "error: --look-at " << *lookAt << " is where the entity stands; give a point it can face\n";
        return false;
    }
    // Straight down or up has no "up" from +Y; keep the image's top toward +Z then, as rotation 90,0,0 does.
    const bool vertical = std::abs(forward.y) > 0.999F * kb::math::Length(forward);
    const kb::math::Vec3 up = vertical ? kb::math::Vec3{ 0.0F, 0.0F, forward.y < 0.0F ? 1.0F : -1.0F } : kb::math::Vec3{ 0.0F, 1.0F, 0.0F };
    kb::math::Quat rotation = kb::math::LookRotation(forward, up);
    if (const kb::scene::SceneEntity parent = scene.Hierarchy().Parent(entity); parent.IsValid()) {
        rotation = kb::math::Inverse(scene.Transforms().Get(parent).worldRotation) * rotation;
    }
    const kb::math::Vec3 degrees = kb::math::ToEulerDegrees(rotation);
    return ApplySetting(scene, entity, "Transform",
        "rotation=" + FormatCliNumber(degrees.x) + ',' + FormatCliNumber(degrees.y) + ',' + FormatCliNumber(degrees.z), io);
}

[[nodiscard]] int EntityAdd(const ArgumentList& arguments, CommandIo io);

// --count 10 --offset 0,1,0: a row, a stack or a tower in one command - Name1..Name10, each one
// offset further from --position.
[[nodiscard]] int EntityAddMany(const ArgumentList& arguments, CommandIo io) {
    const std::string countText = arguments.Option("--count").value_or("");
    int count = 0;
    const auto [end, code] = std::from_chars(countText.data(), countText.data() + countText.size(), count);
    const std::optional<kb::math::Vec3> offset = ParseVec3(arguments.Option("--offset").value_or("0,1,0"));
    const std::optional<kb::math::Vec3> start = ParseVec3(arguments.Option("--position").value_or("0,0,0"));
    const std::optional<std::string> name = arguments.Option("--name").has_value() ? arguments.Option("--name") : arguments.Option("--create");
    if (code != std::errc{} || end != countText.data() + countText.size() || count < 1 || count > 1000 || !offset || !start || !name) {
        io.err << "error: --count expects 1..1000 with --name (or --create), and --offset/--position x,y,z\n";
        return 1;
    }
    const ArgumentList single = arguments.WithoutOption("--count").WithoutOption("--offset");
    std::ostringstream quiet;
    for (int index = 0; index < count; ++index) {
        const kb::math::Vec3 at = *start + *offset * static_cast<float>(index);
        const ArgumentList one = single.WithOption("--name", *name + std::to_string(index + 1))
            .WithOption("--position", FormatCliNumber(at.x) + ',' + FormatCliNumber(at.y) + ',' + FormatCliNumber(at.z));
        if (EntityAdd(one, CommandIo{ .out = quiet, .err = io.err }) != 0) {
            io.err << "error: stopped at " << *name << index + 1 << "; the ones before it were added\n";
            return 1;
        }
    }
    io.out << "added " << count << " entities " << *name << "1.." << *name << count << " from " << arguments.Option("--position").value_or("0,0,0")
           << " every " << arguments.Option("--offset").value_or("0,1,0") << '\n';
    return 0;
}

[[nodiscard]] int EntityAdd(const ArgumentList& arguments, CommandIo io) {
    if (arguments.Option("--count").has_value()) return EntityAddMany(arguments, io);
    const std::optional<std::string> create = arguments.Option("--create");
    const kb::scene::SceneCreateItem* createItem = create.has_value() ? kb::scene::SceneCreateMenu::Find(*create) : nullptr;
    const auto* uiPreset = create.has_value() && createItem == nullptr
        ? kb::scene::FindUIComponentPreset(*create) : nullptr;
    const auto* screenTemplate = create.has_value() && createItem == nullptr && uiPreset == nullptr
        ? kb::scene::FindUIScreenTemplate(*create) : nullptr;
    const bool playerPreset = create.has_value() && IsPlayerPreset(*create);
    const std::optional<std::string> physics = arguments.Option("--physics");
    if (physics.has_value() && (!playerPreset || (*physics != "2d" && *physics != "3d"))) {
        io.err << "error: --physics 2d|3d chooses the physics of --create 'Platformer Player' or 'Top Down Player'\n";
        return 1;
    }
    const std::optional<std::string> name = arguments.Option("--name").has_value() ? arguments.Option("--name")
        : createItem != nullptr ? std::optional<std::string>{ std::string{ createItem->name } }
        : playerPreset ? std::optional<std::string>{ "Player" } : create;
    if (!name.has_value() || name->empty()) {
        io.err << "error: entity add requires --name <Name> or --create <Item>\n";
        return 1;
    }
    // --color makes (or recolours) a material named after the entity before the scene is opened,
    // so the scene's asset registry already sees it: a coloured shape in one command.
    std::optional<std::string> material = arguments.Option("--material");
#if KB_CLI_RENDERER
    if (const std::optional<std::string> color = arguments.Option("--color"); color.has_value()) {
        if (material.has_value()) {
            io.err << "error: pass --color or --material, not both\n";
            return 1;
        }
        const std::string project = arguments.Option("--project").value_or("");
        const std::string materialName = kb::assets::AssetFileNameError(*name).empty() ? *name : std::string{ "Material" };
        const std::string virtualPath = "/Game/Materials/" + materialName + ".kbmat";
        const bool exists = std::filesystem::exists(Utf8Path(project) / "Assets" / "Materials" / Utf8Path(materialName + ".kbmat"));
        const std::vector<std::string> words = exists
            ? std::vector<std::string>{ "--project", project, "--material", virtualPath, "--set", "Base Color=" + *color }
            : std::vector<std::string>{ "--project", project, "--name", materialName, "--set", "Base Color=" + *color };
        std::ostringstream quiet;
        const ArgumentList materialArguments{ words };
        const CommandIo materialIo{ .out = quiet, .err = io.err };
        if ((exists ? RunMaterialSetCommand(materialArguments, materialIo) : RunMaterialNewCommand(materialArguments, materialIo)) != 0) {
            return 1;
        }
        material = virtualPath;
        RefreshOpenScenes();
    }
#endif
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    kb::scene::Scene& scene = *session->scene;

    std::optional<kb::scene::SceneEntity> parent;
    if (const std::optional<std::string> parentName = arguments.Option("--parent"); parentName.has_value()) {
        parent = FindEntity(scene, *parentName, io);
        if (!parent.has_value()) return 1;
    }
    // Two objects with one name could no longer be told apart by --name or --entity.
    if (!NameFreeUnder(scene, parent.value_or(kb::scene::SceneEntity{}), *name)) {
        io.err << "error: an entity named '" << *name << "' already exists "
               << (parent.has_value() ? "under '" + EntityPath(scene, *parent) + "'" : std::string{ "at the scene root" })
               << ". Pass another --name.\n";
        return 1;
    }
    kb::scene::SceneEntity entity{};
    if (playerPreset) {
        entity = AddPlayerPreset(scene, Utf8Path(arguments.Option("--project").value_or("")), *create, *name, io, !physics.has_value() || *physics == "2d");
        if (!entity.IsValid()) return 1;
        if (parent.has_value() && !scene.Hierarchy().SetParent(entity, *parent)) {
            io.err << "error: '" << *name << "' could not be parented\n";
            return 1;
        }
    } else if (uiPreset != nullptr || screenTemplate != nullptr) {
        const auto parentObject = parent ? scene.Entities().Object(*parent) : kb::scene::SceneObject{};
        std::vector<kb::scene::SceneEntity> parts;
        if (screenTemplate != nullptr) {
            entity = kb::scene::CreateUIScreen(scene, screenTemplate->id, parentObject, *name, &parts);
        } else if (kb::scene::IsUIHierarchyPreset(uiPreset->preset)) {
            entity = kb::scene::CreateUIHierarchy(scene, uiPreset->preset, parentObject, *name, &parts);
        } else {
            entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{.name = *name, .parent = parentObject});
            if (entity.IsValid()) kb::scene::ApplySceneUIComponents(scene.Components().UI(), entity,
                kb::scene::BuildUIComponentPreset(uiPreset->preset));
            parts.push_back(entity);
        }
        if (!entity.IsValid()) {
            io.err << "error: UI preset could not be created\n";
            return 1;
        }
        // As the editor's Create menu does: a font for every Text and a Canvas above the widget.
        for (const kb::scene::SceneEntity part : parts) {
            if (const kb::scene::UIDependencyResult completed = kb::scene::CompleteUIDependencies(scene, part); !completed.succeeded) {
                io.err << "error: " << completed.error << '\n';
                return 1;
            }
        }
    } else if (create.has_value()) {
        const kb::scene::SceneCreateResult created =
            kb::scene::SceneCreateMenu::Create(scene, *create, *name, session->descriptor, parent.value_or(kb::scene::SceneEntity{}));
        if (!created.entity.IsValid()) {
            io.err << "error: " << created.error << '\n';
            return 1;
        }
        entity = created.entity;
    } else {
        kb::scene::SceneObjectDesc desc{};
        desc.name = *name;
        entity = scene.Entities().CreateEntity(std::move(desc));
        if (!entity.IsValid()) {
            io.err << "error: entity could not be created\n";
            return 1;
        }
        if (parent.has_value() && !scene.Hierarchy().SetParent(entity, *parent)) {
            io.err << "error: '" << *name << "' could not be parented\n";
            return 1;
        }
    }
    if (!ApplyTransformOptions(scene, entity, arguments, io)) return 1;
    // A shape, its material and its physics in one command instead of three.
    std::string added = "added entity '" + *name + "'";
    if (material.has_value()) {
        if (scene.Components().MeshRenderers().TryGet(entity) == nullptr) {
            io.err << "error: --material and --color need an entity with a Mesh Renderer (a shape from --create)\n";
            return 1;
        }
        if (!ApplySetting(scene, entity, "MeshRenderer", "material=" + *material, io)) {
            return 1;
        }
        added += " with material " + *material;
    }
    if (const std::optional<std::string> body = arguments.Option("--body"); body.has_value()) {
        if (!kb::scene::SceneComponentAuthoring::Has(scene, entity, "Rigidbody")) {
            const kb::scene::SceneComponentAddResult rigidbody =
                kb::scene::SceneComponentAuthoring::Add(scene, entity, "Rigidbody", session->descriptor);
            if (rigidbody.status != kb::scene::SceneComponentAddStatus::Added) {
                const kb::scene::SceneComponentAddAdvice advice =
                    kb::scene::SceneComponentAuthoring::AddAdvice(rigidbody.status, rigidbody.kind, "Rigidbody", *name);
                io.err << "error: " << advice.cause << ". Fix: " << advice.commandLineRemedy << '\n';
                return 1;
            }
        }
        if (!ApplySetting(scene, entity, "Rigidbody", "bodyType=" + *body, io)) {
            return 1;
        }
        added += std::string{ material.has_value() ? " and a " : " with a " } + *body + " Rigidbody";
    }
    // 2D physics (Box2D): a Body 2D and a Collider 2D whose 1x1 box (a circle for a Sphere, a
    // capsule for a Capsule) follows the entity's scale - a platform is --scale 5,1,1 --body2d Static.
    if (const std::optional<std::string> body2D = arguments.Option("--body2d"); body2D.has_value()) {
        const std::string shape = create == std::optional<std::string>{ "Sphere" } ? "Circle" : create == std::optional<std::string>{ "Capsule" } ? "Capsule" : "Box";
        for (const char* component : { "Body2D", "Collider2D" }) {
            if (kb::scene::SceneComponentAuthoring::Has(scene, entity, component)) continue;
            const kb::scene::SceneComponentAddResult result = kb::scene::SceneComponentAuthoring::Add(scene, entity, component, session->descriptor);
            if (result.status != kb::scene::SceneComponentAddStatus::Added) {
                const kb::scene::SceneComponentAddAdvice advice = kb::scene::SceneComponentAuthoring::AddAdvice(result.status, result.kind, component, *name);
                io.err << "error: " << advice.cause << ". Fix: " << advice.commandLineRemedy << '\n';
                return 1;
            }
        }
        if (!ApplySetting(scene, entity, "Body2D", "type=" + *body2D, io) || !ApplySetting(scene, entity, "Collider2D", "shape=" + shape, io)) {
            return 1;
        }
        added += " with a " + *body2D + " Body 2D and a " + shape + " Collider 2D";
    }
    return Saved(*session, added, io);
}

[[nodiscard]] int EntityRemove(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> name = arguments.Option("--name");
    if (!name.has_value()) {
        io.err << "error: entity remove requires --name <Name>\n";
        return 1;
    }
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    const std::optional<kb::scene::SceneEntity> entity = FindEntity(*session->scene, *name, io);
    if (!entity.has_value()) return 1;
    session->scene->Entities().Destroy(*entity);
    return Saved(*session, "removed entity '" + *name + "' and its children", io);
}

// prefab new: saves an entity and its children as /Game/Prefabs/<Name>.kbprefab and links the
// entity to it, as the editor's Create Prefab; scripts then spawn copies with World.InstantiatePrefab.
[[nodiscard]] int PrefabNew(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> entityName = arguments.Option("--from-entity");
    if (!entityName.has_value()) {
        io.err << "error: prefab new requires --from-entity <Name> [--name <PrefabName>] [--folder Assets/Prefabs]\n";
        return 1;
    }
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    kb::scene::Scene& scene = *session->scene;
    const std::optional<kb::scene::SceneEntity> entity = FindEntity(scene, *entityName, io);
    if (!entity.has_value()) return 1;
    const std::string name = arguments.Option("--name").value_or(scene.Entities().Name(*entity));
    if (const std::string error = kb::assets::AssetFileNameError(name); !error.empty()) {
        io.err << "error: '" << name << "' cannot name a prefab: " << error << ". Pass --name.\n";
        return 1;
    }
    const std::filesystem::path root = Utf8Path(arguments.Option("--project").value_or(""));
    const std::string folder = arguments.Option("--folder").value_or("Assets/Prefabs");
    const std::filesystem::path file = root / Utf8Path(folder) / Utf8Path(name + ".kbprefab");
    std::error_code error;
    if (std::filesystem::exists(file, error)) {
        io.err << "error: " << ProjectScenePath(file) << " already exists; pass another --name\n";
        return 1;
    }
    std::filesystem::create_directories(file.parent_path(), error);
    if (!scene.Prefabs().CreateAsset(scene.Entities().Object(*entity), name, file).IsValid()) {
        io.err << "error: the prefab could not be written: " << scene.Assets().Manager().LastError() << '\n';
        return 1;
    }
    return Saved(*session, "saved '" + *entityName + "' as prefab " + ProjectScenePath(file) +
        " and linked it; place more with prefab place, spawn with World.InstantiatePrefab", io);
}

// prefab place: adds an instance of a prefab to the scene, as dragging it into the Scene View.
[[nodiscard]] int PrefabPlace(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> prefab = arguments.Option("--prefab");
    if (!prefab.has_value()) {
        io.err << "error: prefab place requires --prefab /Game/<path>.kbprefab [--name <Name>] [--parent <Name>] [--position x,y,z] ...\n";
        return 1;
    }
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    kb::scene::Scene& scene = *session->scene;
    static_cast<void>(scene.Assets().Discover());
    const kb::assets::AssetMetadata* metadata = FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), Utf8Path(*prefab));
    const std::optional<std::filesystem::path> file = metadata != nullptr && metadata->physicalPath.extension() == ".kbprefab"
        ? std::optional{ metadata->physicalPath } : std::nullopt;
    if (!file.has_value()) {
        io.err << "error: no prefab " << *prefab << " in the project\n";
        return 1;
    }
    kb::scene::SceneObject parent{};
    if (const std::optional<std::string> parentName = arguments.Option("--parent"); parentName.has_value()) {
        const std::optional<kb::scene::SceneEntity> found = FindEntity(scene, *parentName, io);
        if (!found.has_value()) return 1;
        parent = scene.Entities().Object(*found);
    }
    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().Load(*file);
    const kb::scene::ScenePrefabInstance instance = handle.IsValid()
        ? scene.Prefabs().Instantiate(handle, kb::scene::ScenePrefabInstantiationSettings{ .parent = parent }) : kb::scene::ScenePrefabInstance{};
    if (instance.Empty()) {
        io.err << "error: " << *prefab << " could not be placed: " << scene.Assets().Manager().LastError() << '\n';
        return 1;
    }
    const kb::scene::SceneEntity entity = instance.RootObject().Entity();
    if (const std::optional<std::string> name = arguments.Option("--name"); name.has_value()) {
        if (!NameFreeUnder(scene, scene.Hierarchy().Parent(entity), *name)) {
            io.err << "error: an entity named '" << *name << "' already exists there. Pass another --name.\n";
            return 1;
        }
        scene.Entities().SetName(entity, *name);
    }
    if (!ApplyTransformOptions(scene, entity, arguments, io)) return 1;
    return Saved(*session, "placed " + *prefab + " as '" + scene.Entities().Name(entity) + "'", io);
}

[[nodiscard]] int RunPrefabAction(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.empty() ? std::string{} : positionals.front();
    if (action == "new") return PrefabNew(arguments, io);
    if (action == "place") return PrefabPlace(arguments, io);
    io.err << "error: prefab expects new or place\n";
    return 1;
}

// entity set: moves, turns, scales or aims an existing entity in one command.
[[nodiscard]] int EntitySet(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> name = arguments.Option("--name");
    if (!name.has_value()) {
        io.err << "error: entity set requires --name <Name> and any of --position, --rotation, --scale, --look-at\n";
        return 1;
    }
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    kb::scene::Scene& scene = *session->scene;
    const std::optional<kb::scene::SceneEntity> entity = FindEntity(scene, *name, io);
    if (!entity.has_value()) return 1;
    // --parent <Name> moves it under another entity, --parent none to the scene root, as dragging it
    // in the Hierarchy does.
    if (const std::optional<std::string> parentName = arguments.Option("--parent"); parentName.has_value()) {
        kb::scene::SceneEntity parent{};
        if (*parentName != "none") {
            const std::optional<kb::scene::SceneEntity> found = FindEntity(scene, *parentName, io);
            if (!found.has_value()) return 1;
            parent = *found;
        }
        for (kb::scene::SceneEntity above = parent; above.IsValid(); above = scene.Hierarchy().Parent(above)) {
            if (above == *entity) {
                io.err << "error: '" << *name << "' cannot go under itself or one of its children\n";
                return 1;
            }
        }
        if (!NameFreeUnder(scene, parent, scene.Entities().Name(*entity))) {
            io.err << "error: an entity named '" << scene.Entities().Name(*entity) << "' is already there; rename one first\n";
            return 1;
        }
        if (!scene.Hierarchy().SetParent(*entity, parent)) {
            io.err << "error: '" << *name << "' could not be moved under '" << *parentName << "'\n";
            return 1;
        }
    }
    if (!ApplyTransformOptions(scene, *entity, arguments, io)) return 1;
    const kb::scene::TransformComponent transform = scene.Transforms().Get(*entity);
    const kb::math::Vec3 degrees = kb::math::ToEulerDegrees(transform.localRotation);
    return Saved(*session, "set '" + *name + "': localPosition=" + FormatCliNumber(transform.localPosition.x) + ',' +
        FormatCliNumber(transform.localPosition.y) + ',' + FormatCliNumber(transform.localPosition.z) + " rotation=" +
        FormatCliNumber(degrees.x) + ',' + FormatCliNumber(degrees.y) + ',' + FormatCliNumber(degrees.z), io);
}

[[nodiscard]] int EntityRename(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> name = arguments.Option("--name");
    const std::optional<std::string> newName = arguments.Option("--to");
    if (!name.has_value() || !newName.has_value() || newName->empty()) {
        io.err << "error: entity rename requires --name <Name|Parent/Name> --to <NewName> [--occurrence <n>]\n";
        return 1;
    }
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    kb::scene::Scene& scene = *session->scene;
    const std::vector<kb::scene::SceneEntity> matches = MatchEntities(scene, *name);
    kb::scene::SceneEntity entity{};
    if (const std::optional<std::string> occurrence = arguments.Option("--occurrence"); occurrence.has_value()) {
        std::size_t index = 0;
        const auto [end, code] = std::from_chars(occurrence->data(), occurrence->data() + occurrence->size(), index);
        if (code != std::errc{} || end != occurrence->data() + occurrence->size() || index == 0U || index > matches.size()) {
            io.err << "error: --occurrence must be 1.." << matches.size() << " for '" << *name << "'\n";
            return 1;
        }
        entity = matches[index - 1U];
    } else {
        const std::optional<kb::scene::SceneEntity> found = FindEntity(scene, *name, io);
        if (!found.has_value()) return 1;
        entity = *found;
    }
    if (!NameFreeUnder(scene, scene.Hierarchy().Parent(entity), *newName, entity)) {
        io.err << "error: an entity named '" << *newName << "' already exists next to it. Pass another --to.\n";
        return 1;
    }
    const std::string before = EntityPath(scene, entity);
    scene.Entities().SetName(entity, *newName);
    return Saved(*session, "renamed entity '" + before + "' to '" + *newName + "'", io);
}

// The skeleton the character entity (or the one skinned entity below it) is posed with.
[[nodiscard]] std::optional<kb::scene::SceneEntity> FindSkinnedEntity(const kb::scene::Scene& scene, kb::scene::SceneEntity character) {
    if (scene.Components().SkeletonBindings().Has(character)) return character;
    std::vector<kb::scene::SceneEntity> below;
    CollectEntities(scene, character, below);
    std::optional<kb::scene::SceneEntity> found;
    for (const kb::scene::SceneEntity entity : below) {
        if (!scene.Components().SkeletonBindings().Has(entity)) continue;
        if (found.has_value()) return std::nullopt;
        found = entity;
    }
    return found;
}

// `name` as a socket of the skeleton, else as a bone; written another way ("hand_r" for "hand.R")
// it finds the one socket, else the one bone, it normalizes to - as the runtime does.
[[nodiscard]] std::optional<std::string> ResolveSocketName(const kb::scene::SkeletonAsset& skeleton, std::string_view name, CommandIo io) {
    std::vector<std::string_view> names;
    for (const kb::scene::SkeletonSocket& socket : skeleton.sockets) names.push_back(socket.name);
    for (const kb::scene::SkeletonBone& bone : skeleton.bones) names.push_back(bone.name);
    if (std::ranges::find(names, name) != names.end()) return std::string{ name };
    const std::string wanted = kb::scene::NormalizeSkeletonName(name);
    std::vector<std::string_view> folded;
    std::vector<std::string_view> similar;
    const auto collect = [&](std::span<const std::string_view> candidates) {
        for (const std::string_view candidate : candidates) {
            const std::string text = kb::scene::NormalizeSkeletonName(candidate);
            if (text == wanted) folded.push_back(candidate);
            else if (!wanted.empty() && (text.find(wanted) != std::string::npos || wanted.find(text) != std::string::npos)) similar.push_back(candidate);
        }
    };
    // Sockets first, then bones, as the runtime looks them up.
    collect(std::span<const std::string_view>{ names }.first(skeleton.sockets.size()));
    if (folded.size() == 1U) return std::string{ folded.front() };
    if (folded.empty()) {
        collect(std::span<const std::string_view>{ names }.subspan(skeleton.sockets.size()));
        if (folded.size() == 1U) return std::string{ folded.front() };
    }
    similar.insert(similar.begin(), folded.begin(), folded.end());
    io.err << "error: the character's skeleton has no socket or bone named '" << name << "'";
    if (!similar.empty()) {
        io.err << ". Did you mean:";
        for (std::size_t index = 0U; index < similar.size() && index < 8U; ++index) io.err << " '" << similar[index] << "'";
    }
    io.err << " (all names: kb_cli skeleton show --skeleton <its .kbskeletalmesh>; add one: kb_cli skeleton socket add)\n";
    return std::nullopt;
}

[[nodiscard]] bool IsBelow(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, kb::scene::SceneEntity ancestor) {
    for (kb::scene::SceneEntity current = scene.Hierarchy().Parent(entity); current.IsValid(); current = scene.Hierarchy().Parent(current)) {
        if (current == ancestor) return true;
    }
    return false;
}

// entity attach: hangs an object on a character's socket (or bone) - parented under the
// character and held there every frame after animation, as Unreal's AttachToComponent.
[[nodiscard]] int EntityAttach(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> name = arguments.Option("--name");
    const std::optional<std::string> characterName = arguments.Option("--to");
    const std::optional<std::string> socketName = arguments.Option("--socket");
    if (!name.has_value() || !characterName.has_value() || !socketName.has_value()) {
        io.err << "error: entity attach requires --name <Object> --to <Character> --socket <Socket|Bone>"
                  " [--position x,y,z] [--rotation x,y,z (degrees)] [--scale x,y,z]\n";
        return 1;
    }
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    kb::scene::Scene& scene = *session->scene;
    const std::optional<kb::scene::SceneEntity> object = FindEntity(scene, *name, io);
    if (!object.has_value()) return 1;
    const std::optional<kb::scene::SceneEntity> character = FindEntity(scene, *characterName, io);
    if (!character.has_value()) return 1;
    if (*object == *character || IsBelow(scene, *character, *object)) {
        io.err << "error: '" << *name << "' cannot hang on itself or on an object below it\n";
        return 1;
    }
    const std::optional<kb::scene::SceneEntity> skinned = FindSkinnedEntity(scene, *character);
    if (!skinned.has_value()) {
        io.err << "error: '" << *characterName << "' has no skeleton (or several skinned objects below it; name the one to use)."
                  " Give it a character first: component set --entity " << *characterName
               << " --type MeshRenderer --set mesh=<.kbskeletalmesh>\n";
        return 1;
    }
    const kb::scene::SkeletonBindingComponent& binding = *scene.Components().SkeletonBindings().TryGet(*skinned);
    const kb::assets::AssetMetadata* skeletonMetadata =
        scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ binding.skeletonAssetId });
    std::string error;
    const std::optional<kb::scene::SkeletonAsset> skeleton = skeletonMetadata == nullptr ? std::nullopt
        : kb::scene::SkeletonAssetIO::Load(skeletonMetadata->physicalPath, &error);
    if (!skeleton.has_value()) {
        io.err << "error: the skeleton of '" << EntityPath(scene, *skinned) << "' could not be read" << (error.empty() ? "" : ": " + error) << '\n';
        return 1;
    }
    const std::optional<std::string> socket = ResolveSocketName(*skeleton, *socketName, io);
    if (!socket.has_value()) return 1;

    if (!IsBelow(scene, *object, *skinned)) {
        if (!NameFreeUnder(scene, *skinned, scene.Entities().Name(*object), *object)) {
            io.err << "error: '" << EntityPath(scene, *skinned) << "' already has a child named '" << scene.Entities().Name(*object)
                   << "'. Rename one first: kb_cli entity rename\n";
            return 1;
        }
        if (!scene.Hierarchy().SetParent(*object, *skinned)) {
            io.err << "error: '" << *name << "' could not be parented under '" << EntityPath(scene, *skinned) << "'\n";
            return 1;
        }
    }
    kb::scene::SocketAttachmentComponent attachment = kb::scene::MakeSocketAttachment(*socket);
    if (const kb::scene::SocketAttachmentComponent* existing = scene.Components().SocketAttachments().TryGet(*object)) {
        attachment.offset = existing->offset;
    }
    if (!scene.Components().SocketAttachments().Set(*object, attachment)) {
        io.err << "error: the attachment could not be stored\n";
        return 1;
    }
    for (const std::string_view option : { std::string_view{ "--position" }, std::string_view{ "--rotation" } }) {
        if (const std::optional<std::string> value = arguments.Option(std::string{ option }); value.has_value() &&
            !ApplySetting(scene, *object, "SocketAttachment", std::string{ option.substr(2U) } + "=" + *value, io)) {
            return 1;
        }
    }
    if (const std::optional<std::string> scale = arguments.Option("--scale"); scale.has_value() &&
        !ApplySetting(scene, *object, "Transform", "localScale=" + *scale, io)) {
        return 1;
    }
    return Saved(*session, "attached '" + EntityPath(scene, *object) + "' to socket '" + *socket + "' of '" +
        EntityPath(scene, *skinned) + "'", io);
}

[[nodiscard]] int EntityDetach(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> name = arguments.Option("--name");
    if (!name.has_value()) {
        io.err << "error: entity detach requires --name <Object>\n";
        return 1;
    }
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    const std::optional<kb::scene::SceneEntity> object = FindEntity(*session->scene, *name, io);
    if (!object.has_value()) return 1;
    if (!session->scene->Components().SocketAttachments().Has(*object)) {
        io.err << "error: '" << *name << "' is not attached to a socket\n";
        return 1;
    }
    session->scene->Components().SocketAttachments().Remove(*object);
    return Saved(*session, "detached '" + *name + "' (it stays where it is, under the same parent)", io);
}

// Text as `entity show` prints it: bare when it is one word, else quoted.
[[nodiscard]] std::string ShownText(std::string_view text) {
    if (!text.empty() && text.find_first_of(" \t\"=") == std::string_view::npos) return std::string{ text };
    std::string quoted{ '"' };
    for (const char character : text) {
        if (character == '"' || character == '\\') quoted += '\\';
        quoted += character;
    }
    return quoted + '"';
}

[[nodiscard]] std::string ShownTriple(double x, double y, double z) {
    return FormatCliNumber(x) + "," + FormatCliNumber(y) + "," + FormatCliNumber(z);
}

// An asset reference as `component set` takes it: /Engine/Shapes/... or /Game/..., none when unset.
[[nodiscard]] std::string ShownAsset(kb::scene::Scene& scene, std::uint64_t id) {
    if (id == 0U) return "none";
    for (const kb::assets::BuiltInShapeDesc& shape : kb::assets::BuiltInShapes()) {
        if (kb::assets::BuiltInShapeId(shape.shape).value == id) return std::string{ shape.virtualPath };
    }
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ id });
    return metadata != nullptr ? metadata->virtualPath.generic_string() : std::to_string(id);
}

[[nodiscard]] bool IsAssetProperty(std::string_view name) noexcept {
    return name.ends_with("AssetId") || name == "assetId" || name.starts_with("ability");
}

[[nodiscard]] std::string ShownValue(kb::scene::Scene& scene, const kb::script::ScriptSceneComponentPropertyDesc& desc,
    const kb::script::ScriptValue& value) {
    using enum kb::script::ScriptValueType;
    switch (value.Type()) {
    case Bool: return value.AsBool() ? "true" : "false";
    case Int: {
        const int number = value.AsInt();
        if (number >= 0 && static_cast<std::size_t>(number) < desc.enumNames.size()) {
            return std::string{ desc.enumNames[static_cast<std::size_t>(number)] };
        }
        return std::to_string(number);
    }
    case Int64: return std::to_string(value.AsInt64());
    case UInt32: return std::to_string(value.AsUInt32());
    case Float: return FormatCliNumber(value.AsFloat());
    case Double: return FormatCliNumber(value.AsDouble());
    case String: case Name: case Guid: return ShownText(value.AsString());
    case Hash: return IsAssetProperty(desc.name) ? ShownAsset(scene, value.AsUInt64()) : std::to_string(value.AsUInt64());
    case Entity: {
        const kb::scene::SceneEntity other{ value.AsUInt64() };
        return other.IsValid() && scene.Entities().IsAlive(other) ? ShownText(EntityPath(scene, other)) : std::string{ "none" };
    }
    default: return "?";
    }
}

// One line: `Rigidbody: bodyType=Dynamic mass=1 ...`, vectors once (`color=1,0.5,0.25`) and assets by
// path, in the names and forms `component set --set` accepts.
void PrintComponent(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::string_view component, std::ostream& out) {
    std::string line{ component };
    line += ':';
    if (component == "MeshRenderer") {
        const kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(entity);
        line += " mesh=" + ShownAsset(scene, renderer->meshAssetId) + " material=" + ShownAsset(scene, renderer->materialAssetId);
    }
    if (component == "Behaviour") {
        const kb::scene::BehaviourComponent* behaviour = scene.Components().Behaviours().TryGet(entity);
        line += " script=" + ShownAsset(scene, behaviour->behaviourAssetId);
    }
    std::string vector;
    for (const kb::script::ScriptSceneComponentPropertyDesc& desc : kb::script::ScriptSceneComponentApi::ComponentProperties(component)) {
        const kb::script::ScriptSceneComponentPropertyResult read =
            kb::script::ScriptSceneComponentApi::GetProperty(scene, entity, component, desc.name);
        if (!read.succeeded) continue;
        const std::string value = ShownValue(scene, desc, read.value);
        const std::string_view name = desc.name;
        const std::size_t dot = name.rfind('.');
        if (dot != std::string_view::npos && dot + 2U == name.size() && std::string_view{ "xyzw" }.find(name.back()) != std::string_view::npos) {
            const std::string_view base = name.substr(0, dot);
            if (name.back() != 'x' && vector == base) {
                line += ',' + value;
                continue;
            }
            vector = base;
            line += ' ' + std::string{ base } + '=' + value;
            continue;
        }
        vector.clear();
        line += ' ' + std::string{ name } + '=' + value;
    }
    out << line << '\n';
}

// What an agent needs instead of reading the scene file: where the entity is, what holds it, and
// every component's values.
void PrintEntity(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::ostream& out) {
    scene.Runtime().SynchronizeTransforms(); // world translations are fresh after the transform sync
    out << "entity: " << EntityPath(scene, entity) << '\n';
    const kb::scene::SceneEntity parent = scene.Hierarchy().Parent(entity);
    out << "parent: " << (parent.IsValid() ? EntityPath(scene, parent) : std::string{ "none" }) << '\n';
    const std::vector<kb::scene::SceneEntity> children = scene.Hierarchy().ChildEntities(entity);
    out << "children:" << (children.empty() ? " none" : "");
    for (std::size_t index = 0U; index < children.size(); ++index) {
        out << (index == 0U ? " " : ", ") << scene.Entities().Name(children[index]);
    }
    out << '\n';
    if (const kb::scene::TransformComponent* transform = scene.Transforms().TryGet(entity); transform != nullptr) {
        const kb::math::DVec3 local = scene.Transforms().LocalTranslation(entity, *transform);
        const kb::math::DVec3 world = scene.Transforms().WorldTranslation(entity, *transform);
        const kb::math::Vec3 degrees = kb::math::ToEulerDegrees(transform->localRotation);
        out << "Transform: localPosition=" << ShownTriple(local.x, local.y, local.z)
            << " rotation=" << ShownTriple(degrees.x, degrees.y, degrees.z)
            << " localScale=" << ShownTriple(transform->localScale.x, transform->localScale.y, transform->localScale.z)
            << " worldPosition=" << ShownTriple(world.x, world.y, world.z) << '\n';
    }
    std::vector<std::string_view> shown;
    for (const kb::scene::SceneComponentKind& kind : kb::scene::SceneComponentAuthoring::Kinds()) {
        if (!kb::scene::SceneComponentAuthoring::Has(scene, entity, kind.id)) continue;
        PrintComponent(scene, entity, kind.id, out);
        shown.push_back(kind.id);
    }
    // Behaviour and UI components are scriptable but not in the Inspector's add table; Visibility,
    // on every entity, is left out.
    for (const std::string_view listed : kb::script::ScriptSceneComponentApi::ComponentNames()) {
        const std::string_view name = kb::scene::SceneComponentAuthoring::CanonicalName(listed);
        if (name == "Transform" || name == "Visibility" || std::ranges::find(shown, name) != shown.end() ||
            !kb::script::ScriptSceneComponentApi::HasComponent(scene, entity, name)) continue;
        PrintComponent(scene, entity, name, out);
        shown.push_back(name);
    }
}

[[nodiscard]] int EntityShow(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> name = arguments.Option("--name");
    if (!name.has_value()) {
        io.err << "error: entity show requires --name <Name|Parent/Name>\n";
        return 1;
    }
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    const std::optional<kb::scene::SceneEntity> entity = FindEntity(*session->scene, *name, io);
    if (!entity.has_value()) return 1;
    PrintEntity(*session->scene, *entity, io.out);
    return 0; // read only: the scene is not saved
}

[[nodiscard]] int ComponentCommand(std::string_view action, const ArgumentList& arguments, CommandIo io) {
    if (action != "add" && action != "set" && action != "remove") {
        io.err << "error: component expects add, set or remove\n";
        return 1;
    }
    const std::optional<std::string> entityName = arguments.Option("--entity");
    const std::optional<std::string> type = arguments.Option("--type");
    if (!entityName.has_value() || !type.has_value()) {
        io.err << "error: component " << action << " requires --entity <Name> --type <Component>\n";
        return 1;
    }
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    kb::scene::Scene& scene = *session->scene;
    const std::optional<kb::scene::SceneEntity> entity = FindEntity(scene, *entityName, io);
    if (!entity.has_value()) return 1;

    // UI components (Rect Transform, Canvas, Image, ...) by their names, as the editor's Add Component adds them.
    if (const kb::scene::UIComponentDescriptor* ui = kb::scene::FindUIComponentDescriptor(*type); ui != nullptr && action != "remove") {
        kb::scene::UIComponentSet components = kb::scene::CaptureSceneUIComponents(scene.Components().UI(), *entity);
        if (action == "add" && !kb::scene::AddUIComponent(components, ui->type)) {
            io.err << "error: " << ui->displayName << " could not be added to '" << *entityName
                   << "' (it is there already, or it needs a Rect Transform first)\n";
            return 1;
        }
        if (action == "set" && !kb::scene::HasUIComponent(components, ui->type)) {
            io.err << "error: '" << *entityName << "' has no " << ui->displayName << " (use component add)\n";
            return 1;
        }
        kb::scene::SynchronizeSceneUIComponents(scene.Components().UI(), *entity, components);
        if (!ApplySettings(scene, *entity, *type, arguments, io)) return 1;
        return Saved(*session, (action == "add" ? "added " : "updated ") + std::string{ ui->displayName } + " on '" + *entityName + "'", io);
    }
    if (action == "add") {
        const kb::scene::SceneComponentAddResult added =
            kb::scene::SceneComponentAuthoring::Add(scene, *entity, *type, session->descriptor);
        switch (added.status) {
        case kb::scene::SceneComponentAddStatus::Added:
            break;
        case kb::scene::SceneComponentAddStatus::AlreadyPresent:
        case kb::scene::SceneComponentAddStatus::PluginDisabled:
        case kb::scene::SceneComponentAddStatus::Rejected: {
            const kb::scene::SceneComponentAddAdvice advice =
                kb::scene::SceneComponentAuthoring::AddAdvice(added.status, added.kind, *type, *entityName);
            io.err << "error: " << advice.cause << ". Fix: " << advice.commandLineRemedy << '\n';
            return 1;
        }
        case kb::scene::SceneComponentAddStatus::UnknownComponent:
        default:
            io.err << "error: unknown component '" << *type << "'. Fix: kb_cli schema lists the components. Components:";
            for (const kb::scene::SceneComponentKind& kind : kb::scene::SceneComponentAuthoring::Kinds()) io.err << " '" << kind.id << "'";
            io.err << '\n';
            return 1;
        }
        if (!ApplySettings(scene, *entity, *type, arguments, io)) return 1;
        return Saved(*session, "added " + std::string{ added.kind->displayName } + " to '" + *entityName + "'", io);
    }
    if (action == "set") {
        const bool transform = *type == "Transform";
        if (!transform && !kb::scene::SceneComponentAuthoring::Has(scene, *entity, *type)) {
            io.err << "error: '" << *entityName << "' has no " << *type << " (use component add)\n";
            return 1;
        }
        if (arguments.Options("--set").empty()) {
            io.err << "error: component set needs at least one --set name=value\n";
            return 1;
        }
        if (!ApplySettings(scene, *entity, *type, arguments, io)) return 1;
        return Saved(*session, "updated " + *type + " on '" + *entityName + "'", io);
    }
    if (action == "remove") {
        if (!kb::scene::SceneComponentAuthoring::Remove(scene, *entity, *type)) {
            io.err << "error: '" << *entityName << "' has no removable " << *type << '\n';
            return 1;
        }
        return Saved(*session, "removed " + *type + " from '" + *entityName + "'", io);
    }
    io.err << "error: component expects add, set or remove\n";
    return 1;
}

// ---- ai: a whole creature's AI from one command ----

constexpr std::string_view kAiUsage =
    "usage: kb_cli ai <presets|make|rule|show> --project <dir> [--scene <path>] ...\n"
    "  presets                          what each preset does\n"
    "  make  --entity <Name> [--preset Melee|Ranged|Guard|Coward|Wanderer] [--team <n>] [--health <n>]\n"
    "        [--mana <n>] [--speed <m/s>] [--sight <m>] [--ability1 <.kbability> ... --ability8 <path>]\n"
    "  rule  --entity <Name> --rule <1-8> --when <Condition>[=value] --do <Action>[=value]   (--when Never removes it)\n"
    "  show  --entity <Name>\n"
    "  conditions: Always SeesEnemy KnowsEnemy EnemyCloserThan=m EnemyFartherThan=m HealthBelowPercent=p\n"
    "              ManaAbovePercent=p AbilityReady=slot FarFromHome=m Never\n"
    "  actions:    Idle Wander ReturnHome Chase UseAbility=slot KeepDistance=m Flee\n";

[[nodiscard]] std::string_view PresetSummary(kb::scene::AIBrainPreset preset) {
    switch (preset) {
    case kb::scene::AIBrainPreset::Melee: return "fights up close with ability 1 (a plain hit when empty), goes home when it loses the enemy, wanders";
    case kb::scene::AIBrainPreset::Ranged: return "keeps 8 m from enemies closer than 5 m and uses ability 1 (a caster, an archer)";
    case kb::scene::AIBrainPreset::Guard: return "stands at its post and attacks enemies within 12 m with ability 1";
    case kb::scene::AIBrainPreset::Coward: return "runs from enemies and wanders otherwise (animals, villagers)";
    case kb::scene::AIBrainPreset::Wanderer: return "only strolls around its home (critters, ambient life)";
    case kb::scene::AIBrainPreset::Custom: return "the rules as written";
    }
    return "";
}

// Projects made before Gameplay.AI existed get it, as new projects have it.
[[nodiscard]] bool EnsureDefaultPlugin(const ArgumentList& arguments, std::string_view pluginName, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: --project <dir> is required\n";
        return false;
    }
    std::string error;
    const std::filesystem::path projectFile = kb::project::ProjectManager::FindProjectFile(Utf8Path(*project), error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return false;
    }
    kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(projectFile);
    if (!loaded.succeeded) {
        io.err << "error: project descriptor could not be loaded: " << loaded.error << '\n';
        return false;
    }
    for (kb::project::ProjectPluginReference& plugin : loaded.descriptor.plugins) {
        if (plugin.name != pluginName) continue;
        if (plugin.enabled) return true;
        plugin.enabled = true;
        if (!kb::project::ProjectManager::SaveProject(projectFile, loaded.descriptor)) {
            io.err << "error: the project descriptor could not be written\n";
            return false;
        }
        io.out << "enabled the " << pluginName << " plugin in the project\n";
        return true;
    }
    for (const kb::project::ProjectPluginReference& plugin : kb::project::ProjectCreator::DefaultDescriptor().plugins) {
        if (plugin.name == pluginName) loaded.descriptor.plugins.push_back(plugin);
    }
    if (!kb::project::ProjectManager::SaveProject(projectFile, loaded.descriptor)) {
        io.err << "error: the project descriptor could not be written\n";
        return false;
    }
    io.out << "added the " << pluginName << " plugin to the project\n";
    return true;
}

[[nodiscard]] bool EnsureAiPlugin(const ArgumentList& arguments, CommandIo io) {
    return EnsureDefaultPlugin(arguments, "Gameplay.AI", io);
}

#if KB_CLI_RENDERER
[[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> ParseTileSize(std::string_view text) {
    const std::size_t separator = text.find_first_of("xX,");
    if (separator == std::string_view::npos) return std::nullopt;
    const std::string_view first = text.substr(0U, separator);
    const std::string_view second = text.substr(separator + 1U);
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    const auto a = std::from_chars(first.data(), first.data() + first.size(), width);
    const auto b = std::from_chars(second.data(), second.data() + second.size(), height);
    if (a.ec != std::errc{} || b.ec != std::errc{} || a.ptr != first.data() + first.size() || b.ptr != second.data() + second.size() ||
        width == 0U || height == 0U) {
        return std::nullopt;
    }
    return std::pair{ width, height };
}

[[nodiscard]] std::filesystem::path ProjectAssetsRoot(const ArgumentList& arguments) {
    std::string error;
    const std::filesystem::path projectFile =
        kb::project::ProjectManager::FindProjectFile(Utf8Path(arguments.Option("--project").value_or("")), error);
    return projectFile.empty() ? std::filesystem::path{} : std::filesystem::absolute(projectFile.parent_path() / "Assets").lexically_normal();
}

// map2d new: a World 2D Map entity (its map file, its Mesh Renderer, its Map Camera), as the
// editor's Add Component > World 2D Map makes it.
[[nodiscard]] int Map2DNew(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> name = arguments.Option("--name");
    if (!name.has_value() || name->empty()) {
        io.err << "error: map2d new requires --name <Entity>\n";
        return 1;
    }
    kb::assets::Map2DAsset map = kb::assets::MakeEmptyMap2DAsset();
    if (const std::optional<std::string> size = arguments.Option("--size"); size.has_value()) {
        const auto parsed = ParseTileSize(*size);
        if (!parsed.has_value() || parsed->first > kb::assets::Map2DAsset::MaximumSize || parsed->second > kb::assets::Map2DAsset::MaximumSize) {
            io.err << "error: --size takes <width>x<height> in tiles (up to 4096)\n";
            return 1;
        }
        map.width = parsed->first;
        map.height = parsed->second;
    }
    if (const std::optional<std::string> view = arguments.Option("--view"); view.has_value()) {
        const auto parsed = ParseTileSize(*view);
        if (!parsed.has_value() || parsed->first > 255U || parsed->second > 255U) {
            io.err << "error: --view takes <width>x<height> in tiles (the client box, e.g. 15x11)\n";
            return 1;
        }
        map.viewWidth = parsed->first;
        map.viewHeight = parsed->second;
    }
    if (const auto topology = arguments.Option("--topology")) {
        if (*topology != "rectangular" && *topology != "isometric" && *topology != "hexagonal") {
            io.err << "error: --topology takes rectangular, isometric or hexagonal\n";
            return 1;
        }
        map.topology = *topology == "hexagonal" ? kb::assets::Map2DTopology::Hexagonal :
            *topology == "isometric" ? kb::assets::Map2DTopology::Isometric : kb::assets::Map2DTopology::Rectangular;
    }
    if (const std::optional<std::string> orientation = arguments.Option("--orientation"); orientation.has_value()) {
        if (*orientation != "flat" && *orientation != "wall") {
            io.err << "error: --orientation takes flat or wall\n";
            return 1;
        }
        map.orientation = *orientation == "wall" ? kb::assets::Map2DOrientation::Wall : kb::assets::Map2DOrientation::Flat;
    }
    if (!EnsureDefaultPlugin(arguments, "World.Map2D", io)) return 1;
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    kb::scene::Scene& scene = *session->scene;
    if (!NameFreeUnder(scene, kb::scene::SceneEntity{}, *name)) {
        io.err << "error: an entity named '" << *name << "' already exists at the scene root. Pass another --name.\n";
        return 1;
    }
    const std::filesystem::path assetsRoot = ProjectAssetsRoot(arguments);
    const std::filesystem::path file = assetsRoot / "Maps" / Utf8Path(*name + std::string{ kb::assets::kMap2DAssetExtension });
    std::error_code fileError;
    if (std::filesystem::exists(file, fileError)) {
        io.err << "error: the map " << file.generic_string() << " already exists\n";
        return 1;
    }
    std::string error;
    if (!kb::assets::Map2DAssetIO::Save(file, map, &error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    std::filesystem::create_directories(assetsRoot / Utf8Path(kb::world2d::kMap2DSpritesFolder), fileError);
    // The id asset discovery gives the map (the editor's renderer loads it as a render mesh).
    const std::string virtualPath = kb::world2d::Map2DVirtualPath(assetsRoot, file);
    const kb::assets::AssetId mapId = kb::assets::MakeAssetId(
        kb::assets::NormalizeAssetPath(std::filesystem::path{ virtualPath }) + ":" + std::string{ kb::assets::kMap2DAssetType });
    kb::scene::SceneObjectDesc desc{};
    desc.name = *name;
    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(std::move(desc));
    kb::world2d::SetUpMap2DComponents(scene, entity, mapId.value, map);
    kb::world2d::Map2DViewResult view{};
    if (!kb::world2d::SetUpMap2DView(scene, entity, map, assetsRoot, &view, &error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    io.out << "map: " << virtualPath << " (" << map.width << " x " << map.height << " tiles, "
           << kb::assets::Map2DOrientationName(map.orientation) << ", client box " << map.viewWidth << " x " << map.viewHeight << ")\n"
           << "Map Camera: orthographic, primary for Play" << (view.profileAssigned ? "; scene profile /Game/Maps/PP_Map2D.kbppfx" : "") << '\n'
           << "Put sprite sheets into Assets/" << kb::world2d::kMap2DSpritesFolder << ", then paint: kb_cli map2d paint --map " << virtualPath << '\n';
    return Saved(*session, "added World 2D Map '" + *name + "'", io);
}

// map2d view: the Map Camera of a map entity, made again for the map's current settings.
[[nodiscard]] int Map2DView(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> name = arguments.Option("--entity");
    if (!name.has_value()) {
        io.err << "error: map2d view requires --entity <Name>\n";
        return 1;
    }
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    kb::scene::Scene& scene = *session->scene;
    const std::optional<kb::scene::SceneEntity> entity = FindEntity(scene, *name, io);
    if (!entity.has_value()) return 1;
    const kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(*entity);
    const std::filesystem::path assetsRoot = ProjectAssetsRoot(arguments);
    // The map file whose id the Mesh Renderer draws.
    std::filesystem::path mapFile;
    std::error_code walkError;
    for (std::filesystem::recursive_directory_iterator it{ assetsRoot, walkError }, end; renderer != nullptr && !walkError && it != end; it.increment(walkError)) {
        if (it->path().extension() != kb::assets::kMap2DAssetExtension) continue;
        const std::string virtualPath = kb::world2d::Map2DVirtualPath(assetsRoot, it->path());
        if (kb::assets::MakeAssetId(kb::assets::NormalizeAssetPath(std::filesystem::path{ virtualPath }) + ":" +
                std::string{ kb::assets::kMap2DAssetType }).value == renderer->meshAssetId) {
            mapFile = it->path();
            break;
        }
    }
    std::string error;
    const std::optional<kb::assets::Map2DAsset> map = mapFile.empty() ? std::nullopt : kb::assets::Map2DAssetIO::Load(mapFile, &error);
    if (!map.has_value()) {
        io.err << "error: '" << *name << "' is not a World 2D Map" << (error.empty() ? std::string{} : ": " + error) << '\n';
        return 1;
    }
    kb::world2d::Map2DViewResult view{};
    if (!kb::world2d::SetUpMap2DView(scene, *entity, *map, assetsRoot, &view, &error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    if (!kb::world2d::SetUpMap2DCollision(scene, *entity, *map, nullptr, &error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    return Saved(*session, std::string{ view.cameraCreated ? "added" : "updated" } + " the Map Camera of '" + *name + "'", io);
}
#endif

[[nodiscard]] std::string AbilityName(kb::scene::Scene& scene, const kb::scene::CombatantComponent* combatant, float slot) {
    const int index = static_cast<int>(slot) - 1;
    if (combatant == nullptr || index < 0 || index >= static_cast<int>(kb::scene::kCombatantAbilitySlots) || combatant->abilities[index] == 0U) {
        return "empty: a plain 10-damage hit";
    }
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ combatant->abilities[index] });
    return metadata == nullptr ? std::string{ "missing ability" } : metadata->virtualPath.stem().string();
}

void PrintBrain(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::ostream& out) {
    const kb::scene::AIBrainComponent* brain = scene.Components().AIBrains().TryGet(entity);
    const kb::scene::CombatantComponent* combatant = scene.Components().Combatants().TryGet(entity);
    if (brain == nullptr) {
        out << "no AI Brain\n";
        return;
    }
    out << "preset " << kb::scene::kAIBrainPresetNames[static_cast<std::size_t>(brain->preset)] << ", speed " << brain->moveSpeed
        << " m/s, wanders " << brain->wanderRadius << " m, leash " << brain->leashDistance << " m" << (brain->enabled ? "" : ", disabled") << '\n';
    if (combatant != nullptr) {
        out << "combatant: team " << combatant->team << ", health " << combatant->health << '/' << combatant->maximumHealth << ", mana "
            << combatant->mana << '/' << combatant->maximumMana << ", sees " << combatant->sightRange << " m, hears " << combatant->hearingRange << " m\n";
    } else {
        out << "warning: no Combatant - it neither senses enemies nor fights (kb_cli ai make adds one)\n";
    }
    for (std::size_t index = 0U; index < kb::scene::kAIBrainRuleCount; ++index) {
        const kb::scene::AIRule& rule = brain->rules[index];
        if (rule.when == kb::scene::AICondition::Never) continue;
        out << "  " << index + 1U << ". when " << kb::scene::kAIConditionNames[static_cast<std::size_t>(rule.when)];
        if (kb::scene::AIConditionTakesValue(rule.when)) out << ' ' << rule.whenValue;
        out << " -> " << kb::scene::kAIActionNames[static_cast<std::size_t>(rule.action)];
        if (rule.action == kb::scene::AIAction::UseAbility) out << ' ' << rule.actionValue << " (" << AbilityName(scene, combatant, rule.actionValue) << ')';
        else if (kb::scene::AIActionTakesValue(rule.action)) out << ' ' << rule.actionValue << " m";
        out << '\n';
    }
}

template <typename Enum, std::size_t Count>
[[nodiscard]] std::optional<Enum> NameOf(std::string_view text, const std::array<std::string_view, Count>& names) {
    for (std::size_t index = 0U; index < Count; ++index) {
        if (names[index] == text) return static_cast<Enum>(index);
    }
    return std::nullopt;
}

template <std::size_t Count>
void ListNames(const std::array<std::string_view, Count>& names, std::ostream& out) {
    for (const std::string_view name : names) out << ' ' << name;
}

[[nodiscard]] int AiMake(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> entityName = arguments.Option("--entity");
    if (!entityName.has_value()) {
        io.err << "error: ai make requires --entity <Name>\n" << kAiUsage;
        return 1;
    }
    const std::string presetName = arguments.Option("--preset").value_or("Melee");
    const std::optional<kb::scene::AIBrainPreset> preset = NameOf<kb::scene::AIBrainPreset>(presetName, kb::scene::kAIBrainPresetNames);
    if (!preset.has_value()) {
        io.err << "error: unknown preset '" << presetName << "'. Presets:";
        ListNames(kb::scene::kAIBrainPresetNames, io.err);
        io.err << '\n';
        return 1;
    }
    if (!EnsureAiPlugin(arguments, io)) return 1;
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    kb::scene::Scene& scene = *session->scene;
    const std::optional<kb::scene::SceneEntity> entity = FindEntity(scene, *entityName, io);
    if (!entity.has_value()) return 1;

    // The body that fights: monsters (team 2) are the players' (team 1) enemies; animals have no
    // team (0): nobody hunts them and they flee from anyone with a team.
    const bool fights = *preset == kb::scene::AIBrainPreset::Melee || *preset == kb::scene::AIBrainPreset::Ranged ||
        *preset == kb::scene::AIBrainPreset::Guard;
    if (!scene.Components().Combatants().Has(*entity)) {
        kb::scene::CombatantComponent combatant{};
        combatant.team = fights ? 2U : 0U;
        if (*preset == kb::scene::AIBrainPreset::Ranged) combatant.sightRange = 20.0F;
        if (!scene.Components().Combatants().Set(*entity, combatant)) {
            io.err << "error: the Combatant could not be added\n";
            return 1;
        }
    }
    kb::scene::AIBrainComponent brain = scene.Components().AIBrains().Has(*entity)
        ? *scene.Components().AIBrains().TryGet(*entity) : kb::scene::AIBrainComponent{};
    kb::scene::ApplyAIBrainPreset(brain, *preset);
    if (!scene.Components().AIBrains().Set(*entity, brain)) {
        io.err << "error: the AI Brain could not be added\n";
        return 1;
    }
    for (const auto& [option, setting, component] : {
             std::tuple{ "--team", "team", "Combatant" }, std::tuple{ "--health", "maximumHealth", "Combatant" },
             std::tuple{ "--mana", "maximumMana", "Combatant" }, std::tuple{ "--sight", "sightRange", "Combatant" },
             std::tuple{ "--speed", "moveSpeed", "AIBrain" } }) {
        if (const std::optional<std::string> value = arguments.Option(option); value.has_value() &&
            !ApplySetting(scene, *entity, component, std::string{ setting } + "=" + *value, io)) {
            return 1;
        }
    }
    for (int slot = 1; slot <= static_cast<int>(kb::scene::kCombatantAbilitySlots); ++slot) {
        const std::string option = "--ability" + std::to_string(slot);
        if (const std::optional<std::string> value = arguments.Option(option); value.has_value() &&
            !ApplySetting(scene, *entity, "Combatant", "ability" + std::to_string(slot) + "=" + *value, io)) {
            return 1;
        }
    }
    if (!session->Save()) {
        io.err << "error: scene " << session->path.generic_string() << " could not be written\n";
        return 1;
    }
    io.out << "'" << EntityPath(scene, *entity) << "' is a " << presetName << ": " << PresetSummary(*preset) << '\n';
    PrintBrain(scene, *entity, io.out);
    if (scene.Components().CharacterControllers().Has(*entity) && scene.Components().Colliders().Has(*entity)) {
        io.out << "warning: '" << *entityName << "' has a Collider and a Character Controller: the controller collides with its own "
                  "collider and jitters; remove the Collider (component remove --type Collider), the controller is its body\n";
    }
    if (*preset == kb::scene::AIBrainPreset::Ranged) {
        const kb::scene::CombatantComponent* combatant = scene.Components().Combatants().TryGet(*entity);
        const kb::assets::AssetMetadata* ability = combatant == nullptr || combatant->abilities[0] == 0U ? nullptr
            : scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ combatant->abilities[0] });
        const std::optional<kb::gameplay::AbilityAsset> loaded = ability == nullptr ? std::nullopt : kb::gameplay::AbilityAssetIO::Load(ability->physicalPath);
        if (!loaded.has_value() || loaded->range < 6.0F) {
            io.out << "warning: a Ranged creature needs an ability with a range of 6 m or more in slot 1 (--ability1); "
                      "without one it walks up for a plain hit and backs off again\n";
        }
    }
    if (!scene.Components().NavAgents().Has(*entity)) {
        io.out << "note: without a NavAgent it walks straight at its target; add one (component add --type NavAgent) and bake a navmesh to walk around walls\n";
    }
    return 0;
}

[[nodiscard]] int AiRule(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> entityName = arguments.Option("--entity");
    const std::optional<std::string> ruleText = arguments.Option("--rule");
    const std::optional<std::string> when = arguments.Option("--when");
    const std::optional<std::string> action = arguments.Option("--do");
    int ruleNumber = 0;
    if (!entityName.has_value() || !ruleText.has_value() || !when.has_value() ||
        std::from_chars(ruleText->data(), ruleText->data() + ruleText->size(), ruleNumber).ec != std::errc{} ||
        ruleNumber < 1 || ruleNumber > static_cast<int>(kb::scene::kAIBrainRuleCount)) {
        io.err << "error: ai rule requires --entity <Name> --rule <1-8> --when <Condition>[=value] --do <Action>[=value]\n" << kAiUsage;
        return 1;
    }
    const auto split = [](const std::string& text) {
        const std::size_t equals = text.find('=');
        return std::pair{ text.substr(0, equals), equals == std::string::npos ? std::string{} : text.substr(equals + 1U) };
    };
    const auto [conditionName, conditionValue] = split(*when);
    const auto [actionName, actionValue] = split(action.value_or("Idle"));
    const std::optional<kb::scene::AICondition> condition = NameOf<kb::scene::AICondition>(conditionName, kb::scene::kAIConditionNames);
    const std::optional<kb::scene::AIAction> doing = NameOf<kb::scene::AIAction>(actionName, kb::scene::kAIActionNames);
    if (!condition.has_value() || !doing.has_value()) {
        io.err << "error: " << (condition.has_value() ? "unknown action '" + actionName + "'" : "unknown condition '" + conditionName + "'") << ". Conditions:";
        ListNames(kb::scene::kAIConditionNames, io.err);
        io.err << ". Actions:";
        ListNames(kb::scene::kAIActionNames, io.err);
        io.err << '\n';
        return 1;
    }
    float whenNumber = 0.0F;
    float actionNumber = *doing == kb::scene::AIAction::UseAbility ? 1.0F : 0.0F;
    if ((!conditionValue.empty() && std::from_chars(conditionValue.data(), conditionValue.data() + conditionValue.size(), whenNumber).ec != std::errc{}) ||
        (!actionValue.empty() && std::from_chars(actionValue.data(), actionValue.data() + actionValue.size(), actionNumber).ec != std::errc{})) {
        io.err << "error: a value after '=' must be a number\n";
        return 1;
    }
    if (kb::scene::AIConditionTakesValue(*condition) && conditionValue.empty()) {
        io.err << "error: " << conditionName << " needs a value, e.g. " << conditionName << "=5\n";
        return 1;
    }
    if (!EnsureAiPlugin(arguments, io)) return 1;
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    kb::scene::Scene& scene = *session->scene;
    const std::optional<kb::scene::SceneEntity> entity = FindEntity(scene, *entityName, io);
    if (!entity.has_value()) return 1;
    kb::scene::AIBrainComponent brain = scene.Components().AIBrains().Has(*entity)
        ? *scene.Components().AIBrains().TryGet(*entity) : kb::scene::AIBrainComponent{};
    brain.preset = kb::scene::AIBrainPreset::Custom;
    brain.rules[static_cast<std::size_t>(ruleNumber - 1)] = kb::scene::AIRule{ *condition, whenNumber, *doing, actionNumber };
    if (!kb::scene::IsAIBrainComponentValid(brain) || !scene.Components().AIBrains().Set(*entity, brain)) {
        io.err << "error: the rule is not valid (values are not negative; an ability slot is 1 to 8)\n";
        return 1;
    }
    if (!session->Save()) {
        io.err << "error: scene " << session->path.generic_string() << " could not be written\n";
        return 1;
    }
    PrintBrain(scene, *entity, io.out);
    return 0;
}

[[nodiscard]] int AiShow(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> entityName = arguments.Option("--entity");
    if (!entityName.has_value()) {
        io.err << "error: ai show requires --entity <Name>\n";
        return 1;
    }
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    const std::optional<kb::scene::SceneEntity> entity = FindEntity(*session->scene, *entityName, io);
    if (!entity.has_value()) return 1;
    PrintBrain(*session->scene, *entity, io.out);
    return 0;
}

// ---- rivers: terrain river add|move|remove|list ---------------------------------------------------------

constexpr std::string_view kRiverOptions[]{ "project", "scene", "name", "points", "index", "to", "all", "position", "parent", "bank-width",
    "waterfall-slope", "material", "fall-material", "mist-effect", "help", "json", "dry-run", "command-timeout-ms", "command-cancel-file" };

[[nodiscard]] bool RiverOptionsKnown(const ArgumentList& arguments, CommandIo io) {
    std::vector<std::string> names;
    for (const std::string_view option : kRiverOptions) names.push_back("--" + std::string{ option });
    const std::vector<std::string_view> known{ names.begin(), names.end() };
    const std::vector<std::string> unknown = arguments.UnknownOptions(known);
    if (unknown.empty()) return true;
    io.err << "error: terrain river has no option '" << unknown.front() << "'; kb_cli help terrain lists them\n";
    return false;
}

// The entity --name, which must carry a River.
[[nodiscard]] std::optional<kb::scene::SceneEntity> FindRiver(const kb::scene::Scene& scene, const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> name = arguments.Option("--name");
    if (!name.has_value()) {
        io.err << "error: terrain river needs --name <Entity> (the entity that carries the River)\n";
        return std::nullopt;
    }
    const std::optional<kb::scene::SceneEntity> entity = FindEntity(scene, *name, io);
    if (!entity.has_value()) return std::nullopt;
    if (!scene.Components().Rivers().Has(*entity)) {
        io.err << "error: '" << *name << "' has no River; terrain river add --name " << *name << " --points \"x,y,z;x,y,z\" gives it one\n";
        return std::nullopt;
    }
    return entity;
}

[[nodiscard]] std::string RiverPointLine(std::uint32_t index, const kb::scene::RiverPoint& point) {
    std::ostringstream text;
    text << index << ": " << FormatCliNumber(point.position.x) << ',' << FormatCliNumber(point.position.y) << ',' << FormatCliNumber(point.position.z)
         << " width " << FormatCliNumber(point.width) << " depth " << FormatCliNumber(point.depth) << " speed " << FormatCliNumber(point.speed);
    return text.str();
}

[[nodiscard]] bool ReadRiverFloat(const ArgumentList& arguments, std::string_view option, float low, float high, float& value, CommandIo io) {
    const std::optional<std::string> text = arguments.Option("--" + std::string{ option });
    if (!text.has_value()) return true;
    float parsed = 0.0F;
    const auto [end, code] = std::from_chars(text->data(), text->data() + text->size(), parsed);
    if (code != std::errc{} || end != text->data() + text->size() || !std::isfinite(parsed) || parsed < low || parsed > high) {
        io.err << "error: --" << option << " takes a number from " << FormatCliNumber(low) << " to " << FormatCliNumber(high) << '\n';
        return false;
    }
    value = parsed;
    return true;
}

[[nodiscard]] bool ReadRiverWholeNumber(const ArgumentList& arguments, std::string_view option, std::uint32_t& value, CommandIo io) {
    const std::optional<std::string> text = arguments.Option("--" + std::string{ option });
    if (!text.has_value()) return true;
    std::uint32_t parsed = 0U;
    const auto [end, code] = std::from_chars(text->data(), text->data() + text->size(), parsed);
    if (code != std::errc{} || end != text->data() + text->size()) {
        io.err << "error: --" << option << " takes a whole number (the 0-based number of a point; terrain river list shows them)\n";
        return false;
    }
    value = parsed;
    return true;
}

// The settings that are not points: --bank-width, --waterfall-slope, --material, --fall-material and --mist-effect.
[[nodiscard]] bool ApplyRiverSettings(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const ArgumentList& arguments, CommandIo io) {
    kb::scene::RiverComponent river = *scene.Components().Rivers().TryGet(entity);
    if (!ReadRiverFloat(arguments, "bank-width", 0.0F, 10000.0F, river.bankWidth, io) ||
        !ReadRiverFloat(arguments, "waterfall-slope", 5.0F, 89.0F, river.waterfallSlopeDegrees, io)) {
        return false;
    }
    scene.Components().Rivers().Set(entity, river);
    for (const auto& [option, property] : { std::pair{ "--material", "materialAssetId" }, std::pair{ "--fall-material", "waterfallMaterialAssetId" },
             std::pair{ "--mist-effect", "mistEffectAssetId" } }) {
        const std::optional<std::string> path = arguments.Option(option);
        if (path.has_value() && !ApplySetting(scene, entity, "River", std::string{ property } + "=" + *path, io)) return false;
    }
    return true;
}

// A river with no water material gets the engine's: written into the project's Materials folder (kept when it is
// there already) and set by path, as --material would.
[[nodiscard]] bool GiveRiverDefaultMaterials(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const ArgumentList& arguments, CommandIo io) {
    const kb::scene::RiverComponent& river = *scene.Components().Rivers().TryGet(entity);
    if (river.materialAssetId != 0U && river.waterfallMaterialAssetId != 0U && river.mistEffectAssetId != 0U) return true;
    std::string error;
    const std::filesystem::path projectFile = kb::project::ProjectManager::FindProjectFile(Utf8Path(arguments.Option("--project").value_or("")), error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return false;
    }
    kb::terrain_editor::RiverWaterMaterials files;
    if (!kb::terrain_editor::EnsureRiverWaterMaterials(projectFile.parent_path() / "Assets" / "Materials", files, error)) {
        io.err << "error: " << error << '\n';
        return false;
    }
    static_cast<void>(scene.Assets().Discover());
    const bool streamMissing = river.materialAssetId == 0U;
    const bool fallMissing = river.waterfallMaterialAssetId == 0U;
    const bool mistMissing = river.mistEffectAssetId == 0U;
    return (!streamMissing || ApplySetting(scene, entity, "River", "materialAssetId=/Game/Materials/" + files.stream.filename().string(), io)) &&
        (!fallMissing || ApplySetting(scene, entity, "River", "waterfallMaterialAssetId=/Game/Materials/" + files.waterfall.filename().string(), io)) &&
        (!mistMissing || ApplySetting(scene, entity, "River", "mistEffectAssetId=/Game/Materials/" + files.mist.filename().string(), io));
}

[[nodiscard]] int RiverAdd(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> name = arguments.Option("--name");
    const std::optional<std::string> pointsText = arguments.Option("--points");
    if (!name.has_value() || !pointsText.has_value()) {
        io.err << "error: terrain river add needs --name <Entity> and --points \"x,y,z[,width,depth,speed];...\" (a new river needs 2 or more)\n";
        return 1;
    }
    std::vector<kb::scene::RiverPoint> added;
    std::string error;
    if (!kb::scene::ParseRiverPointList(*pointsText, added, error) || added.empty()) {
        io.err << "error: --points: " << (error.empty() ? "no points given" : error) << '\n';
        return 1;
    }
    std::uint32_t index = kb::scene::RiverComponent::MaxPoints;
    if (!ReadRiverWholeNumber(arguments, "index", index, io)) return 1;
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    kb::scene::Scene& scene = *session->scene;
    std::optional<kb::scene::SceneEntity> parent;
    if (const std::optional<std::string> parentName = arguments.Option("--parent"); parentName.has_value()) {
        parent = FindEntity(scene, *parentName, io);
        if (!parent.has_value()) return 1;
    }
    kb::scene::SceneEntity entity{};
    kb::scene::RiverComponent river{};
    std::vector<kb::scene::RiverPoint> points;
    bool created = false;
    if (MatchEntities(scene, *name).empty()) {
        if (added.size() < 2U) {
            io.err << "error: a new river needs 2 or more points; give --points \"x,y,z;x,y,z\"\n";
            return 1;
        }
        if (!NameFreeUnder(scene, parent.value_or(kb::scene::SceneEntity{}), *name)) {
            io.err << "error: an entity named '" << *name << "' already exists there. Pass another --name.\n";
            return 1;
        }
        entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = *name,
            .parent = parent ? scene.Entities().Object(*parent) : kb::scene::SceneObject{} });
        if (!entity.IsValid()) {
            io.err << "error: the river entity could not be created\n";
            return 1;
        }
        if (!ApplyTransformOptions(scene, entity, arguments, io)) return 1;
        created = true;
    } else {
        const std::optional<kb::scene::SceneEntity> found = FindEntity(scene, *name, io);
        if (!found.has_value()) return 1;
        entity = *found;
        if (const kb::scene::RiverComponent* existing = scene.Components().Rivers().TryGet(entity); existing != nullptr) {
            river = *existing;
            points.assign(existing->points.begin(), existing->points.begin() + existing->pointCount);
        }
    }
    const std::size_t at = std::min<std::size_t>(index, points.size());
    points.insert(points.begin() + static_cast<std::ptrdiff_t>(at), added.begin(), added.end());
    if (!kb::scene::SetRiverPoints(river, points, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    scene.Components().Rivers().Set(entity, river);
    if (!ApplyRiverSettings(scene, entity, arguments, io) || !GiveRiverDefaultMaterials(scene, entity, arguments, io)) return 1;
    return Saved(*session, std::string{ created ? "created river '" : "river '" } + *name + "': added " + std::to_string(added.size()) +
        (added.size() == 1U ? " point at " : " points from ") + std::to_string(at) + ", " + std::to_string(points.size()) + " in all", io);
}

[[nodiscard]] int RiverMove(const ArgumentList& arguments, CommandIo io) {
    std::uint32_t index = 0U;
    const std::optional<std::string> to = arguments.Option("--to");
    if (!arguments.Option("--index").has_value() || !to.has_value()) {
        io.err << "error: terrain river move needs --name <Entity> --index <n> --to \"x,y,z[,width,depth,speed]\"\n";
        return 1;
    }
    if (!ReadRiverWholeNumber(arguments, "index", index, io)) return 1;
    std::vector<kb::scene::RiverPoint> target;
    std::string error;
    if (!kb::scene::ParseRiverPointList(*to, target, error) || target.size() != 1U) {
        io.err << "error: --to takes one point: " << (error.empty() ? "x,y,z[,width,depth,speed]" : error) << '\n';
        return 1;
    }
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    kb::scene::Scene& scene = *session->scene;
    const std::optional<kb::scene::SceneEntity> entity = FindRiver(scene, arguments, io);
    if (!entity.has_value()) return 1;
    kb::scene::RiverComponent river = *scene.Components().Rivers().TryGet(*entity);
    if (index >= river.pointCount) {
        io.err << "error: the river has " << river.pointCount << " points (0.." << river.pointCount - 1U << "); --index " << index << " is not one\n";
        return 1;
    }
    // Three numbers move the point and keep its width, depth and speed.
    if (std::ranges::count(*to, ',') == 2) {
        target.front().width = river.points[index].width;
        target.front().depth = river.points[index].depth;
        target.front().speed = river.points[index].speed;
    }
    river.points[index] = target.front();
    scene.Components().Rivers().Set(*entity, river);
    return Saved(*session, "moved point " + RiverPointLine(index, river.points[index]), io);
}

[[nodiscard]] int RiverRemove(const ArgumentList& arguments, CommandIo io) {
    const bool all = arguments.Flag("--all");
    if (all == arguments.Option("--index").has_value()) {
        io.err << "error: terrain river remove needs --index <n> (one point) or --all (the whole river and its entity)\n";
        return 1;
    }
    std::uint32_t index = 0U;
    if (!all && !ReadRiverWholeNumber(arguments, "index", index, io)) return 1;
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    kb::scene::Scene& scene = *session->scene;
    const std::optional<kb::scene::SceneEntity> entity = FindRiver(scene, arguments, io);
    if (!entity.has_value()) return 1;
    if (all) {
        scene.Entities().Destroy(*entity);
        return Saved(*session, "removed river '" + EntityPath(scene, *entity) + "' and its entity", io);
    }
    kb::scene::RiverComponent river = *scene.Components().Rivers().TryGet(*entity);
    if (index >= river.pointCount) {
        io.err << "error: the river has " << river.pointCount << " points (0.." << river.pointCount - 1U << "); --index " << index << " is not one\n";
        return 1;
    }
    std::vector<kb::scene::RiverPoint> points{ river.points.begin(), river.points.begin() + river.pointCount };
    points.erase(points.begin() + index);
    std::string error;
    if (!kb::scene::SetRiverPoints(river, points, error)) {
        io.err << "error: " << error << " (--all removes the whole river)\n";
        return 1;
    }
    scene.Components().Rivers().Set(*entity, river);
    return Saved(*session, "removed point " + std::to_string(index) + "; " + std::to_string(points.size()) + " points remain", io);
}

[[nodiscard]] int RiverList(const ArgumentList& arguments, CommandIo io) {
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return 1;
    const kb::scene::Scene& scene = *session->scene;
    std::vector<kb::scene::SceneEntity> rivers;
    if (arguments.Option("--name").has_value()) {
        const std::optional<kb::scene::SceneEntity> entity = FindRiver(scene, arguments, io);
        if (!entity.has_value()) return 1;
        rivers.push_back(*entity);
    } else {
        for (const kb::scene::SceneEntity entity : AllEntities(scene)) {
            if (scene.Components().Rivers().Has(entity)) rivers.push_back(entity);
        }
    }
    io.out << rivers.size() << (rivers.size() == 1U ? " river\n" : " rivers\n");
    for (const kb::scene::SceneEntity entity : rivers) {
        const kb::scene::RiverComponent& river = *scene.Components().Rivers().TryGet(entity);
        kb::scene::RiverSurface surface;
        const bool built = kb::scene::BuildRiverSurface(river, surface);
        io.out << "river '" << EntityPath(scene, entity) << "': " << river.pointCount << " points, bank " << FormatCliNumber(river.bankWidth)
               << ", waterfall slope " << FormatCliNumber(river.waterfallSlopeDegrees) << " degrees, "
               << (built ? std::to_string(surface.waterfalls.size()) : std::string{ "?" }) << " waterfalls, length "
               << (built ? FormatCliNumber(surface.samples.back().distance) : std::string{ "?" }) << '\n';
        for (std::uint32_t index = 0U; index < river.pointCount; ++index) io.out << "  " << RiverPointLine(index, river.points[index]) << '\n';
        if (!built) continue;
        for (const kb::scene::RiverWaterfall& fall : surface.waterfalls) {
            const kb::scene::Vec3 top = surface.samples[fall.first].position;
            io.out << "  waterfall at " << FormatCliNumber(top.x) << ',' << FormatCliNumber(top.y) << ',' << FormatCliNumber(top.z) << ": drops "
                   << FormatCliNumber(fall.topHeight - fall.bottomHeight) << '\n';
        }
    }
    return 0;
}

} // namespace

int RunRiverSceneCommand(std::string_view action, const ArgumentList& arguments, CommandIo io) {
    if (!RiverOptionsKnown(arguments, io)) return 1;
    if (action == "add") return RiverAdd(arguments, io);
    if (action == "move") return RiverMove(arguments, io);
    if (action == "remove") return RiverRemove(arguments, io);
    if (action == "list") return RiverList(arguments, io);
    io.err << "error: terrain river takes add, move, remove, list or carve\n";
    return 1;
}

std::optional<RiverCarveSource> LoadRiverForCarve(const ArgumentList& arguments, const std::filesystem::path& terrainFile, CommandIo io) {
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return std::nullopt;
    kb::scene::Scene& scene = *session->scene;
    const std::optional<kb::scene::SceneEntity> entity = FindRiver(scene, arguments, io);
    if (!entity.has_value()) return std::nullopt;
    scene.Runtime().SynchronizeTransforms();
    const kb::assets::AssetMetadata* terrain = FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), terrainFile);
    std::optional<kb::scene::SceneEntity> terrainEntity;
    if (terrain != nullptr) {
        for (const kb::scene::SceneEntity candidate : AllEntities(scene)) {
            const kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(candidate);
            if (renderer != nullptr && renderer->meshAssetId == terrain->id.value) {
                terrainEntity = candidate;
                break;
            }
        }
    }
    if (!terrainEntity.has_value()) {
        io.err << "error: no entity of the scene draws " << terrainFile.generic_string()
               << "; place the terrain first (terrain new --name <Entity>), so that the river is cut where the terrain stands\n";
        return std::nullopt;
    }
    const auto placement = [&scene](kb::scene::SceneEntity of) {
        const kb::scene::TransformComponent* transform = scene.Transforms().TryGet(of);
        return transform == nullptr ? kb::scene::RiverPlacement{} : kb::scene::RiverPlacement{ transform->worldPosition, transform->worldRotation, transform->worldScale };
    };
    return RiverCarveSource{ .name = arguments.Option("--name").value(), .river = *scene.Components().Rivers().TryGet(*entity),
        .riverPlacement = placement(*entity), .terrainPlacement = placement(*terrainEntity) };
}


#if KB_CLI_RENDERER
#if KB_CLI_RENDERER
int RunSceneLightmapCommand(const ArgumentList& arguments, CommandIo io) {
    const auto name=arguments.Option("--entity"), output=arguments.Option("--out");
    if(!name||!output) { io.err << "error: scene-lightmap requires --project --entity <receiver> --out /Game/path.tga [--resolution 128]\n";return 1; }
    const SceneSessionPtr session=OpenScene(arguments,io); if(!session) return 1;
    auto& scene=*session->scene; const auto receiver=FindEntity(scene,*name,io);if(!receiver) return 1;
    // Use the asset-creation resolver: outputs do not need to exist yet.
    std::string pathError;
    const auto projectFile=kb::project::ProjectManager::FindProjectFile(Utf8Path(arguments.Option("--project").value_or("")),pathError);
    const auto outputPath=Utf8Path(*output);
    const auto parentText=outputPath.parent_path().generic_u8string();
    const auto filename=outputPath.filename().generic_u8string();
    const auto folder=kb::assets::ResolveProjectAssetFolder(projectFile.parent_path(),
        std::string{reinterpret_cast<const char*>(parentText.data()),parentText.size()});
    const auto nameError=kb::assets::AssetFileNameError(std::string{reinterpret_cast<const char*>(filename.data()),filename.size()});
    if(projectFile.empty()||!folder.error.empty()||!nameError.empty()||outputPath.extension()!=".tga") {
        io.err << "error: --out must name a new .tga asset in Assets: " << pathError << folder.error << nameError << '\n';return 1;
    }
    const auto texturePath=folder.folder/outputPath.filename();
    const auto content=FindProjectContentRoot(arguments.Option("--project"),session->path,pathError);
    if(!content) {io.err << "error: " << pathError << '\n';return 1;}
    const auto relative=texturePath.lexically_relative(*content);
    if(relative.empty()||*relative.begin()=="..") {io.err << "error: --out must be inside Assets\n";return 1;}
    auto materialPath=texturePath;materialPath.replace_extension(".kbmat");
    if(std::filesystem::exists(texturePath)||std::filesystem::exists(materialPath)) {io.err << "error: lightmap output already exists; choose a new asset path\n";return 1;}
    kb::render::StaticLightmapSettings settings;
    if(const auto resolution=arguments.Option("--resolution")) {
        unsigned value=0;const auto [end,error]=std::from_chars(resolution->data(),resolution->data()+resolution->size(),value);
        if(error!=std::errc{}||end!=resolution->data()+resolution->size()||value<4||value>1024) {io.err << "error: resolution must be 4..1024\n";return 1;}
        settings.resolution=static_cast<std::uint16_t>(value);
    }
    const auto* receiverMesh=scene.Components().MeshRenderers().TryGet(*receiver);
    if(!receiverMesh||receiverMesh->materialSlotOverrideCount!=0||receiverMesh->materialInstanceHandle!=0) {io.err << "error: receiver needs one static mesh material\n";return 1;}
    kb::render::RuntimeRenderAssetDiscovery discovery;discovery.SetDiscoveryEnabled(false);discovery.Ensure(scene,0);
    auto& assets=scene.Assets().Manager();
    kb::render::RenderMaterialAssetData material;
    if(receiverMesh->materialAssetId!=0) {
        const auto original=assets.Load<kb::render::RenderMaterialAssetData>(kb::assets::AssetId{receiverMesh->materialAssetId});
        if(!original.IsLoaded()) {io.err << "error: receiver material cannot be loaded\n";return 1;}
        material=*original.Shared();
        if(material.materialType!="builtin.pbr"||!material.graph.nodes.empty()||material.desc.metallicFactor!=0||
            material.desc.alphaMode!=kb::render::RenderMaterialAlphaMode::Opaque||material.desc.transmissionFactor!=0||
            material.desc.emissiveTextureAssetId!=0||!material.emissiveTexturePath.empty()||material.desc.normalTextureAssetId!=0||
            !material.normalTexturePath.empty()||material.desc.occlusionTextureAssetId!=0||!material.occlusionTexturePath.empty()||
            material.desc.clearcoatFactor!=0||material.desc.anisotropyStrength!=0||material.desc.sheenColor[0]!=0||
            material.desc.sheenColor[1]!=0||material.desc.sheenColor[2]!=0||
            (material.desc.emissiveStrength>0&&(material.desc.emissiveColor[0]!=0||material.desc.emissiveColor[1]!=0||material.desc.emissiveColor[2]!=0))) {io.err << "error: bake receiver must use opaque matte builtin PBR without emission or normal maps\n";return 1;}
    }
    scene.Runtime().SynchronizeTransforms();
    std::vector<kb::render::LightmapTriangle> triangles,occluders;
    std::vector<kb::render::LightmapLight> lights;
    for(const auto entity:AllEntities(scene)) {
        if(!scene.Entities().IsActive(entity)) continue;
        const auto* transform=scene.Transforms().TryGet(entity);if(!transform) continue;
        const auto position=scene.Transforms().WorldTranslation(entity,*transform);
        if(std::max({std::abs(position.x),std::abs(position.y),std::abs(position.z)})>1000000.0) {io.err << "error: bake geometry exceeds supported world bounds\n";return 1;}
        const kb::math::Vec3 translation{static_cast<float>(position.x),static_cast<float>(position.y),static_cast<float>(position.z)};
        if(const auto* light=scene.Components().Lights().TryGet(entity);light&&light->mobility==kb::scene::LightMobility::Static) {
            if((light->kind!=kb::scene::LightKind::Point&&light->kind!=kb::scene::LightKind::Directional)||light->useColorTemperature) {io.err << "error: lightmap supports static point/directional lights with RGB colour\n";return 1;}
            lights.push_back({.position=translation,.direction=kb::math::Rotate(transform->worldRotation,kb::math::Vec3{0,0,1}),
                .color=light->color,.intensity=light->intensity,.range=light->range,.directional=light->kind==kb::scene::LightKind::Directional,
                .castsShadow=light->castsShadow});
        }
        const auto* mesh=scene.Components().MeshRenderers().TryGet(entity);
        if(!mesh||(entity!=*receiver&&!mesh->castsShadow)) continue;
        const auto source=assets.Load<kb::render::RenderMeshAssetData>(kb::assets::AssetId{mesh->meshAssetId});
        if(!source.IsLoaded()) {io.err << "error: static bake mesh cannot be loaded\n";return 1;}
        const auto& data=*source.Shared();
        if(transform->worldScale.x==0||transform->worldScale.y==0||transform->worldScale.z==0) {io.err << "error: bake mesh has zero scale\n";return 1;}
        const auto vertex=[&](std::uint32_t index) {
            kb::render::LightmapVertex out;
            const auto copy=[&](const auto& v) {
                const auto p=kb::math::Rotate(transform->worldRotation,kb::math::Vec3{v.x*transform->worldScale.x,v.y*transform->worldScale.y,v.z*transform->worldScale.z});
                out.position={translation.x+p.x,translation.y+p.y,translation.z+p.z};
                out.normal=kb::math::Rotate(transform->worldRotation,kb::math::Vec3{v.nx/transform->worldScale.x,v.ny/transform->worldScale.y,v.nz/transform->worldScale.z});out.u=v.u;out.v=v.v;
            };
            if(data.vertices.empty()) copy(data.tangentVertices.at(index));else copy(data.vertices.at(index));return out;
        };
        const auto indexCount=data.indices32.empty()?data.indices16.size():data.indices32.size();
        const auto vertexCount=data.vertices.empty()?data.tangentVertices.size():data.vertices.size();
        for(const auto& section:data.sections) {
            if(section.lodLevel!=0) continue;
            if(section.terrainLayerIndex!=UINT8_MAX||section.indexCount%3!=0||static_cast<std::size_t>(section.indexStart)+section.indexCount>indexCount) {io.err << "error: unsupported or invalid bake geometry\n";return 1;}
            for(std::size_t index=section.indexStart;index<section.indexStart+section.indexCount;index+=3) {
                kb::render::LightmapTriangle triangle;
                for(unsigned corner=0;corner<3;++corner) {
                    const auto local=data.indices32.empty()?data.indices16[index+corner]:data.indices32[index+corner];
                    const auto global=static_cast<std::size_t>(section.vertexStart)+local;
                    if(global>=vertexCount) {io.err << "error: invalid bake vertex index\n";return 1;}
                    triangle[corner]=vertex(static_cast<std::uint32_t>(global));
                }
                if(entity==*receiver) triangles.push_back(triangle);
                if(mesh->castsShadow) occluders.push_back(triangle);
            }
        }
    }
    if(lights.empty()) {io.err << "error: no static point/directional light to bake\n";return 1;}
    const auto baked=kb::render::BakeStaticLightmap(triangles,occluders,lights,settings);
    if(!baked.Succeeded()) {io.err << "error: " << baked.error << '\n';return 1;}
    std::error_code code;std::filesystem::create_directories(texturePath.parent_path(),code);
    if(code) {io.err << "error: cannot create lightmap directory\n";return 1;}
    // Uncompressed top-origin BGRA TGA: the ordinary texture loader and cooker own its runtime format.
    std::array<unsigned char,18> header{};header[2]=2;header[12]=static_cast<unsigned char>(settings.resolution&255);
    header[13]=static_cast<unsigned char>(settings.resolution>>8);header[14]=header[12];header[15]=header[13];header[16]=32;header[17]=0x28;
    std::ofstream image(texturePath,std::ios::binary);image.write(reinterpret_cast<const char*>(header.data()),header.size());
    for(std::size_t pixel=0;pixel<baked.rgba.size();pixel+=4) {
        const char bgra[]{static_cast<char>(baked.rgba[pixel+2]),static_cast<char>(baked.rgba[pixel+1]),static_cast<char>(baked.rgba[pixel]),static_cast<char>(baked.rgba[pixel+3])};image.write(bgra,4);
    }
    image.close();
    const auto removeOutputs=[&] {std::error_code ignored;std::filesystem::remove(texturePath,ignored);std::filesystem::remove(materialPath,ignored);};
    material.desc.bakedLightmap=true;material.desc.emissiveTextureAssetId=0;material.emissiveTexturePath="/Game/"+relative.generic_string();
    if(!image||!kb::render::RenderMaterialAssetWriter::Save(materialPath,material)) {removeOutputs();io.err << "error: cannot save baked lightmap assets\n";return 1;}
    static_cast<void>(assets.DiscoverMountedAssets());
    const auto materialRelative=materialPath.lexically_relative(*content);
    if(!ApplySetting(scene,*receiver,"MeshRenderer","material=/Game/"+materialRelative.generic_string(),io)||!session->Save()) {
        removeOutputs();io.err << "error: could not assign baked material to scene\n";return 1;
    }
    io.out << "baked " << baked.coveredTexels << " texels, " << baked.shadowedSamples << " occluded samples into /Game/" << relative.generic_string()
        << "; assigned " << materialRelative.generic_string() << "; static lights are retained for other receivers\n";
    return 0;
}
#endif

int RunMap2DSceneCommand(std::string_view action, const ArgumentList& arguments, CommandIo io) {
    if (action == "new") return Map2DNew(arguments, io);
    if (action == "view") return Map2DView(arguments, io);
    io.err << "error: map2d scene actions are new and view\n";
    return 1;
}
#endif

struct SceneEditBatch::State : OpenScenes {};

SceneEditBatch::SceneEditBatch() : state_(std::make_unique<State>()) { gOpenScenes = state_.get(); }

SceneEditBatch::~SceneEditBatch() { gOpenScenes = nullptr; }

bool SceneEditBatch::Prepare(std::string_view command, std::string& error) {
    state_->keeping = command == "entity" || command == "component";
    return state_->keeping || Commit(error);
}

bool SceneEditBatch::Commit(std::string& error) {
    state_->keeping = false;
    std::vector<OpenScenes::Entry> entries = std::move(state_->entries);
    state_->entries.clear();
    for (const OpenScenes::Entry& entry : entries) {
        if (entry.session->changed && !entry.session->Write()) {
            error = "scene " + entry.session->path.generic_string() + " could not be written";
            return false;
        }
    }
    return true;
}

void SceneEditBatch::Discard() noexcept {
    state_->keeping = false;
    state_->entries.clear();
}

int RunEntityCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.empty() ? std::string{} : positionals.front();
    if (action == "add") return EntityAdd(arguments, io);
    if (action == "set") return EntitySet(arguments, io);
    if (action == "remove") return EntityRemove(arguments, io);
    if (action == "rename") return EntityRename(arguments, io);
    if (action == "attach") return EntityAttach(arguments, io);
    if (action == "detach") return EntityDetach(arguments, io);
    if (action == "show") return EntityShow(arguments, io);
    io.err << "error: entity expects add, set, remove, rename, attach, detach or show\n";
    return 1;
}

int RunPrefabCommand(const ArgumentList& arguments, CommandIo io) {
    return RunPrefabAction(arguments, io);
}

int RunComponentCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    return ComponentCommand(positionals.empty() ? std::string_view{} : std::string_view{ positionals.front() }, arguments, io);
}

bool PlaceSceneNavMesh(const ArgumentList& arguments, const std::filesystem::path& navMesh, CommandIo io) {
    const SceneSessionPtr session = OpenScene(arguments, io);
    if (!session) return false;
    kb::scene::Scene& scene = *session->scene;
    static_cast<void>(scene.Assets().Discover());
    const kb::assets::AssetMetadata* metadata = FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), navMesh);
    if (metadata == nullptr) {
        io.err << "error: the navigation mesh " << navMesh.generic_string() << " is not inside the project's content\n";
        return false;
    }
    for (const kb::scene::SceneEntity root : scene.Hierarchy().RootEntities()) {
        const kb::scene::ContentInstanceComponent* content = scene.Components().ContentInstances().TryGet(root);
        if (content != nullptr && content->kind == kb::scene::ContentInstanceKind::NavigationMesh && content->assetId == metadata->id.value) {
            return true;
        }
    }
    kb::scene::SceneObjectDesc desc{};
    desc.name = "Navigation Mesh";
    const kb::scene::SceneEntity holder = scene.Entities().CreateEntity(std::move(desc));
    if (!holder.IsValid()) {
        io.err << "error: the navigation mesh could not be placed in the scene\n";
        return false;
    }
    scene.Components().ContentInstances().Set(holder, kb::scene::ContentInstanceComponent{
        .assetId = metadata->id.value, .kind = kb::scene::ContentInstanceKind::NavigationMesh });
    return Saved(*session, "placed the navigation mesh in the scene as 'Navigation Mesh'", io) == 0;
}

int RunAiCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.empty() ? std::string{} : positionals.front();
    if (action == "presets") {
        for (std::size_t index = 1U; index < kb::scene::kAIBrainPresetCount; ++index) {
            io.out << kb::scene::kAIBrainPresetNames[index] << ": " << PresetSummary(static_cast<kb::scene::AIBrainPreset>(index)) << '\n';
        }
        return 0;
    }
    if (action == "make") return AiMake(arguments, io);
    if (action == "rule") return AiRule(arguments, io);
    if (action == "show") return AiShow(arguments, io);
    io.err << kAiUsage;
    return 1;
}

int RunSchemaCommand(const ArgumentList& arguments, CommandIo io) {
    // One component's properties when asked for (by name or display name): what an agent needs
    // before setting them, without every other component.
    if (const std::optional<std::string> wanted = arguments.Option("--component"); wanted.has_value()) {
        const std::string_view name = kb::scene::SceneComponentAuthoring::CanonicalName(*wanted);
        for (const kb::scene::SceneComponentKind& kind : kb::scene::SceneComponentAuthoring::Kinds()) {
            if (kind.id != name && kind.displayName != *wanted) continue;
            io.out << kind.id;
            if (kind.displayName != kind.id) io.out << " (" << kind.displayName << ')';
            if (!kind.requiredPlugin.empty()) io.out << " [plugin " << kind.requiredPlugin << ']';
            io.out << ':';
            ListProperties(kind.id, io.out);
            io.out << '\n';
            if (kind.id == "Transform") io.out << "Transform rotation is in degrees: --set rotation=x,y,z\n";
            if (kind.id == "MeshRenderer") io.out << "MeshRenderer references: mesh=<asset path | engine shape> material=<asset path | none>\n";
            if (kind.id == "Animator") io.out << "Animator references: controller=<asset path of a .kbanimcontroller>\n";
            return 0;
        }
        // Transform and the UI components (Text, Canvas, ...) are not in the add table but are set the same way.
        const std::span<const kb::script::ScriptSceneComponentPropertyDesc> properties =
            kb::script::ScriptSceneComponentApi::ComponentProperties(name);
        if (!properties.empty()) {
            io.out << name << ':';
            ListProperties(name, io.out);
            io.out << '\n';
            if (name == "Transform") io.out << "Transform rotation is in degrees: --set rotation=x,y,z\n";
            return 0;
        }
        io.err << "error: no component called '" << *wanted << "'; schema without --component lists them all\n";
        return 1;
    }
    // Every component with every property is long; by default the names, one component's
    // properties on request (--component), or all of them with --all.
    if (!arguments.Flag("--all")) {
        io.out << "Components (their properties: component_schema with component, or kb_cli schema --component <Name>): Transform";
        for (const kb::scene::SceneComponentKind& kind : kb::scene::SceneComponentAuthoring::Kinds()) {
            io.out << ' ' << kind.id;
        }
        io.out << "\nUI components:";
        for (const kb::scene::UIComponentDescriptor& component : kb::scene::UIComponentCatalog()) io.out << " '" << component.displayName << '\'';
        io.out << "\nentity add --create items:";
        for (const kb::scene::SceneCreateItem& item : kb::scene::SceneCreateMenu::Items()) io.out << " '" << item.id << '\'';
        for (const auto& preset : kb::scene::UIComponentPresetCatalog()) io.out << " '" << preset.name << '\'';
        io.out << '\n';
        io.out << "MeshRenderer references: mesh=<asset path | engine shape> material=<asset path | none>\n";
        return 0;
    }
    io.out << "Transform: rotation (degrees x,y,z)";
    ListProperties("Transform", io.out);
    io.out << "\nMeshRenderer references: mesh=<asset path | engine shape> material=<asset path | none>\n";
    io.out << "ContentInstance references: assetId=<asset path>; set kind first (0 prefab, 1 scene, 2 fragment, 3 world, 4 navigation).\n";
    io.out << "Engine shapes:";
    for (const kb::assets::BuiltInShapeDesc& shape : kb::assets::BuiltInShapes()) io.out << ' ' << shape.virtualPath;
    io.out << "\nentity add --create items:";
    for (const kb::scene::SceneCreateItem& item : kb::scene::SceneCreateMenu::Items()) io.out << " '" << item.id << '\'';
    for (const auto& preset : kb::scene::UIComponentPresetCatalog()) io.out << " '" << preset.name << '\'';
    io.out << " 'Platformer Player' 'Top Down Player'\n";
    for (const kb::scene::SceneComponentKind& kind : kb::scene::SceneComponentAuthoring::Kinds()) {
        io.out << kind.id;
        if (kind.displayName != kind.id) io.out << " (" << kind.displayName << ')';
        if (!kind.requiredPlugin.empty()) io.out << " [plugin " << kind.requiredPlugin << ']';
        io.out << ':';
        ListProperties(kind.id, io.out);
        io.out << '\n';
    }
    return 0;
}

} // namespace kb::cli
