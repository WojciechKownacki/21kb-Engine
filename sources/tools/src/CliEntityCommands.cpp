#include "CliCommands.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/assets/BuiltInShapes.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneComponentAuthoring.hpp"
#include "engine/scene/SceneCreateMenu.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "engine/script/ScriptValue.hpp"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Scene authoring for agents: the same entities and components the editor's hierarchy and Inspector
// create, through the same engine tables (SceneComponentAuthoring, ScriptSceneComponentApi).
namespace kb::cli {
namespace {

// A loaded, editable scene of a project; Save() writes it back where it came from.
struct SceneSession {
    kb::project::ProjectDescriptor descriptor;
    std::unique_ptr<kb::scene::Scene> scene;
    std::filesystem::path path;
    std::string name;

    [[nodiscard]] bool Save() { return kb::scene::SceneDocumentService::Save(*scene, path, name); }
};

[[nodiscard]] std::filesystem::path Utf8Path(std::string_view text) {
    return std::filesystem::path{ std::u8string{ reinterpret_cast<const char8_t*>(text.data()), text.size() } };
}

[[nodiscard]] std::optional<SceneSession> OpenScene(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: --project <dir> is required\n";
        return std::nullopt;
    }
    const std::filesystem::path root = Utf8Path(*project);
    std::string error;
    const std::filesystem::path projectFile = kb::project::ProjectManager::FindProjectFile(root, error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return std::nullopt;
    }
    kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(projectFile);
    if (!loaded.succeeded) {
        io.err << "error: project descriptor could not be loaded: " << loaded.error << '\n';
        return std::nullopt;
    }

    SceneSession session{ .descriptor = loaded.descriptor, .scene = std::make_unique<kb::scene::Scene>(loaded.descriptor) };
    if (!MountProjectAssets(*session.scene, projectFile.parent_path(), error)) {
        io.err << "error: " << error << '\n';
        return std::nullopt;
    }
    // Without --scene: the scene the game starts in (ProjectSettings::defaultMap).
    std::string sceneOption = arguments.Option("--scene").value_or("");
    if (sceneOption.empty()) {
        const kb::project::ProjectSettingsLoadResult settings =
            kb::project::ProjectSettingsStore::Load(kb::project::ProjectSettingsStore::FilePath(projectFile.parent_path()));
        sceneOption = settings.settings.defaultMap;
    }
    if (!sceneOption.empty() && sceneOption.front() == '/') {
        const kb::assets::AssetMetadata* metadata =
            FindAssetByFlexiblePath(session.scene->Assets().Manager().Registry(), sceneOption);
        if (metadata == nullptr) {
            io.err << "error: scene asset was not found: " << sceneOption << '\n';
            return std::nullopt;
        }
        session.path = metadata->physicalPath;
    } else {
        session.path = ResolveInputPath(Utf8Path(sceneOption), projectFile.parent_path());
    }
    const kb::scene::SceneDocumentLoadResult document = kb::scene::SceneDocumentService::Load(session.path);
    if (!document.succeeded) {
        io.err << "error: could not load scene " << session.path.generic_string() << ": " << document.error << '\n';
        return std::nullopt;
    }
    session.name = document.document.name;
    if (!kb::scene::SceneDocumentService::LoadIntoScene(*session.scene, document.document)) {
        io.err << "error: scene " << session.path.generic_string() << " could not be instantiated\n";
        return std::nullopt;
    }
    return session;
}

void CollectEntities(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::vector<kb::scene::SceneEntity>& out) {
    out.push_back(entity);
    for (const kb::scene::SceneEntity child : scene.Hierarchy().ChildEntities(entity)) {
        CollectEntities(scene, child, out);
    }
}

[[nodiscard]] std::vector<kb::scene::SceneEntity> AllEntities(const kb::scene::Scene& scene) {
    std::vector<kb::scene::SceneEntity> all;
    for (const kb::scene::SceneEntity root : scene.Hierarchy().RootEntities()) {
        CollectEntities(scene, root, all);
    }
    return all;
}

// The one entity called `name`; refuses (with the reason) none or several.
[[nodiscard]] std::optional<kb::scene::SceneEntity> FindEntity(const kb::scene::Scene& scene, std::string_view name, CommandIo io) {
    std::vector<kb::scene::SceneEntity> matches;
    for (const kb::scene::SceneEntity entity : AllEntities(scene)) {
        if (scene.Entities().Name(entity) == name) {
            matches.push_back(entity);
        }
    }
    if (matches.size() == 1U) {
        return matches.front();
    }
    if (matches.empty()) {
        io.err << "error: no entity named '" << name << "'. Entities:";
        for (const kb::scene::SceneEntity entity : AllEntities(scene)) {
            io.err << " '" << scene.Entities().Name(entity) << "'";
        }
        io.err << '\n';
    } else {
        io.err << "error: " << matches.size() << " entities are named '" << name << "'; rename one first\n";
    }
    return std::nullopt;
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
    const std::size_t equals = setting.find('=');
    if (equals == std::string_view::npos || equals == 0U) {
        io.err << "error: --set expects name=value, got '" << setting << "'\n";
        return false;
    }
    const std::string_view name = setting.substr(0, equals);
    const std::string_view value = setting.substr(equals + 1U);

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
        // A vector property written at once: name.x, name.y, name.z(, name.w).
        std::vector<std::string_view> parts;
        for (std::string_view rest = value; ;) {
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
        const std::optional<kb::script::ScriptValue> parsed = ParseValue(text, desc->type);
        if (!parsed.has_value()) {
            io.err << "error: " << component << '.' << property << " expects " << TypeName(desc->type) << ", got '" << text << "'\n";
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

[[nodiscard]] int Saved(SceneSession& session, std::string_view what, CommandIo io) {
    if (!session.Save()) {
        io.err << "error: scene " << session.path.generic_string() << " could not be written\n";
        return 1;
    }
    io.out << what << " (" << session.path.generic_string() << ")\n";
    return 0;
}

[[nodiscard]] int EntityAdd(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> create = arguments.Option("--create");
    const kb::scene::SceneCreateItem* createItem = create.has_value() ? kb::scene::SceneCreateMenu::Find(*create) : nullptr;
    const std::optional<std::string> name = arguments.Option("--name").has_value() ? arguments.Option("--name")
        : createItem != nullptr ? std::optional<std::string>{ std::string{ createItem->name } } : create;
    if (!name.has_value() || name->empty()) {
        io.err << "error: entity add requires --name <Name> or --create <Item>\n";
        return 1;
    }
    std::optional<SceneSession> session = OpenScene(arguments, io);
    if (!session.has_value()) return 1;
    kb::scene::Scene& scene = *session->scene;

    std::optional<kb::scene::SceneEntity> parent;
    if (const std::optional<std::string> parentName = arguments.Option("--parent"); parentName.has_value()) {
        parent = FindEntity(scene, *parentName, io);
        if (!parent.has_value()) return 1;
    }
    kb::scene::SceneEntity entity{};
    if (create.has_value()) {
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
    for (const auto& [option, property] : { std::pair{ "--position", "localPosition" }, std::pair{ "--rotation", "rotation" },
             std::pair{ "--scale", "localScale" } }) {
        if (const std::optional<std::string> value = arguments.Option(option); value.has_value() &&
            !ApplySetting(scene, entity, "Transform", std::string{ property } + "=" + *value, io)) {
            return 1;
        }
    }
    return Saved(*session, "added entity '" + *name + "'", io);
}

[[nodiscard]] int EntityRemove(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> name = arguments.Option("--name");
    if (!name.has_value()) {
        io.err << "error: entity remove requires --name <Name>\n";
        return 1;
    }
    std::optional<SceneSession> session = OpenScene(arguments, io);
    if (!session.has_value()) return 1;
    const std::optional<kb::scene::SceneEntity> entity = FindEntity(*session->scene, *name, io);
    if (!entity.has_value()) return 1;
    session->scene->Entities().Destroy(*entity);
    return Saved(*session, "removed entity '" + *name + "' and its children", io);
}

[[nodiscard]] int ComponentCommand(std::string_view action, const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> entityName = arguments.Option("--entity");
    const std::optional<std::string> type = arguments.Option("--type");
    if (!entityName.has_value() || !type.has_value()) {
        io.err << "error: component " << action << " requires --entity <Name> --type <Component>\n";
        return 1;
    }
    std::optional<SceneSession> session = OpenScene(arguments, io);
    if (!session.has_value()) return 1;
    kb::scene::Scene& scene = *session->scene;
    const std::optional<kb::scene::SceneEntity> entity = FindEntity(scene, *entityName, io);
    if (!entity.has_value()) return 1;

    if (action == "add") {
        const kb::scene::SceneComponentAddResult added =
            kb::scene::SceneComponentAuthoring::Add(scene, *entity, *type, session->descriptor);
        switch (added.status) {
        case kb::scene::SceneComponentAddStatus::Added:
            break;
        case kb::scene::SceneComponentAddStatus::AlreadyPresent:
            io.err << "error: '" << *entityName << "' already has " << added.kind->displayName << " (use component set)\n";
            return 1;
        case kb::scene::SceneComponentAddStatus::PluginDisabled:
            io.err << "error: " << added.kind->displayName << " needs the project plugin " << added.kind->requiredPlugin << '\n';
            return 1;
        case kb::scene::SceneComponentAddStatus::Rejected:
            io.err << "error: " << added.kind->displayName << " could not be added to '" << *entityName << "'\n";
            return 1;
        case kb::scene::SceneComponentAddStatus::UnknownComponent:
        default:
            io.err << "error: unknown component '" << *type << "'. Components:";
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

} // namespace

int RunEntityCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.empty() ? std::string{} : positionals.front();
    if (action == "add") return EntityAdd(arguments, io);
    if (action == "remove") return EntityRemove(arguments, io);
    io.err << "error: entity expects add or remove\n";
    return 1;
}

int RunComponentCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    return ComponentCommand(positionals.empty() ? std::string_view{} : std::string_view{ positionals.front() }, arguments, io);
}

int RunSchemaCommand(const ArgumentList& arguments, CommandIo io) {
    static_cast<void>(arguments);
    io.out << "Transform: rotation (degrees x,y,z)";
    ListProperties("Transform", io.out);
    io.out << "\nMeshRenderer references: mesh=<asset path | engine shape> material=<asset path | none>\n";
    io.out << "Engine shapes:";
    for (const kb::assets::BuiltInShapeDesc& shape : kb::assets::BuiltInShapes()) io.out << ' ' << shape.virtualPath;
    io.out << "\nentity add --create items:";
    for (const kb::scene::SceneCreateItem& item : kb::scene::SceneCreateMenu::Items()) io.out << " '" << item.id << '\'';
    io.out << '\n';
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
