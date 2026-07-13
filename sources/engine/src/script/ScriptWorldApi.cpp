#include "engine/script/ScriptWorldApi.hpp"

#include "engine/assets/AssetHandle.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabInstance.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <algorithm>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

const ScriptValue* FindArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == name) {
            return &argument.value;
        }
    }
    return nullptr;
}

ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

ScriptFunctionCallResult NoScene() {
    return Error("world api requires an active scene");
}

[[nodiscard]] kb::scene::SceneEntity EntityArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, kb::scene::SceneEntity fallback = {}) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : kb::scene::SceneEntity{ value->AsUInt64(fallback.Id()) };
}

[[nodiscard]] std::string StringArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, std::string fallback = {}) {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? std::move(fallback) : value->AsString();
}

[[nodiscard]] float FloatArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, float fallback = 0.0F) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsFloat(fallback);
}

[[nodiscard]] bool HasArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) noexcept {
    return FindArg(arguments, name) != nullptr;
}

void ApplyPositionArgs(kb::scene::TransformComponent& transform, std::span<const ScriptFunctionArgument> arguments) noexcept {
    if (HasArg(arguments, "x")) {
        transform.localPosition.x = FloatArg(arguments, "x", transform.localPosition.x);
    }
    if (HasArg(arguments, "y")) {
        transform.localPosition.y = FloatArg(arguments, "y", transform.localPosition.y);
    }
    if (HasArg(arguments, "z")) {
        transform.localPosition.z = FloatArg(arguments, "z", transform.localPosition.z);
    }
}

ScriptFunctionCallResult EntityResult(std::string_view pin, kb::scene::SceneEntity entity) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ std::string{ pin }, ScriptValue{ entity.Id(), ScriptValueType::Entity } } },
        .errors = {},
    };
}

ScriptFunctionCallResult BoolResult(std::string_view pin, bool value) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ std::string{ pin }, ScriptValue{ value } } },
        .errors = {},
    };
}

[[nodiscard]] std::string Trim(std::string_view value) {
    const std::size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const std::size_t end = value.find_last_not_of(" \t\r\n");
    return std::string{ value.substr(begin, end - begin + 1U) };
}

[[nodiscard]] std::vector<std::string> ParseTags(std::string_view tags) {
    std::vector<std::string> parsed;
    std::size_t tokenBegin = 0U;
    while (tokenBegin <= tags.size()) {
        const std::size_t tokenEnd = tags.find_first_of(",;", tokenBegin);
        std::string tag = Trim(tags.substr(tokenBegin, tokenEnd == std::string_view::npos ? std::string_view::npos : tokenEnd - tokenBegin));
        if (!tag.empty()) {
            parsed.push_back(std::move(tag));
        }
        if (tokenEnd == std::string_view::npos) {
            break;
        }
        tokenBegin = tokenEnd + 1U;
    }
    return parsed;
}

[[nodiscard]] std::string JoinTags(const std::vector<std::string>& tags) {
    std::string joined;
    for (const std::string& tag : tags) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += tag;
    }
    return joined;
}

[[nodiscard]] bool HasTagValue(std::string_view tags, std::string_view tag) {
    for (const std::string& existingTag : ParseTags(tags)) {
        if (existingTag == tag) {
            return true;
        }
    }
    return false;
}

struct FindByNameContext {
    kb::scene::Scene* scene = nullptr;
    std::string_view name;
    kb::scene::SceneEntity found{};
};

void FindByNameVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent&, void* rawContext) {
    auto* context = static_cast<FindByNameContext*>(rawContext);
    if (context == nullptr || context->scene == nullptr || context->found.IsValid()) {
        return;
    }
    if (context->scene->Entities().Name(entity) == context->name) {
        context->found = entity;
    }
}

ScriptFunctionCallResult FindByName(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const std::string name = StringArg(arguments, "name");
    FindByNameContext findContext{
        .scene = context.scene,
        .name = name,
        .found = {},
    };
    context.scene->Transforms().ForEach(&FindByNameVisitor, &findContext);
    return EntityResult("entity", findContext.found);
}

ScriptFunctionCallResult Exists(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return BoolResult("exists", context.scene->Entities().IsAlive(EntityArg(arguments, "entity")));
}

ScriptFunctionCallResult Name(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    const std::string name = entity.IsValid() && context.scene->Entities().IsAlive(entity) ? context.scene->Entities().Name(entity) : std::string{};
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "name", ScriptValue{ name } } },
        .errors = {},
    };
}

// LIB-065: the current scene's runtime instance id — an opaque identifier
// (like Hash), not an arithmetic quantity, and NOT the same kind of thing
// as kb::library::SceneRef (an on-disk SceneDocument asset handle); this
// is the currently-executing, in-memory world.
ScriptFunctionCallResult Current(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "world", ScriptValue{ context.scene->Id(), ScriptValueType::Hash } } },
        .errors = {},
    };
}

ScriptFunctionCallResult IsPlaying(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return BoolResult("playing", context.scene->Runtime().IsPlaying());
}

// LIB-065: monotonic since scene creation (kb::scene::SceneRuntime::
// FrameIndex/FixedStepIndex never reset, unlike LastFixedStepCount).
ScriptFunctionCallResult FrameIndex(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "frame", ScriptValue{ static_cast<std::int64_t>(context.scene->Runtime().FrameIndex()) } } },
        .errors = {},
    };
}

ScriptFunctionCallResult FixedStepIndex(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "step", ScriptValue{ static_cast<std::int64_t>(context.scene->Runtime().FixedStepIndex()) } } },
        .errors = {},
    };
}

ScriptFunctionCallResult Spawn(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    kb::scene::SceneObjectDesc desc;
    desc.name = StringArg(arguments, "name", "Entity");
    ApplyPositionArgs(desc.transform, arguments);
    const kb::scene::SceneEntity parent = EntityArg(arguments, "parent");
    if (parent.IsValid() && context.scene->Entities().IsAlive(parent)) {
        desc.parent = context.scene->Entities().Object(parent);
    }
    const kb::scene::SceneEntity entity = context.scene->Entities().CreateEntity(std::move(desc));
    return EntityResult("entity", entity);
}

ScriptFunctionCallResult Destroy(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    const bool existed = entity.IsValid() && context.scene->Entities().IsAlive(entity);
    if (existed) {
        context.scene->Entities().Destroy(entity);
    }
    return BoolResult("destroyed", existed);
}

ScriptFunctionCallResult SetTag(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    const std::string tag = StringArg(arguments, "tag");
    const ScriptValue* enabledValue = FindArg(arguments, "enabled");
    const bool enabled = enabledValue == nullptr || enabledValue->AsBool(true);
    const std::string normalizedTag = Trim(tag);
    if (!entity.IsValid() || !context.scene->Entities().IsAlive(entity) || normalizedTag.empty()) {
        return BoolResult("tagged", false);
    }

    kb::scene::SceneTagsComponents tagsComponents = context.scene->Components().Tags();
    std::vector<std::string> tags;
    if (const kb::scene::TagsComponent* current = tagsComponents.TryGet(entity)) {
        tags = ParseTags(kb::scene::TagsText(*current));
    }
    const auto existing = std::find(tags.begin(), tags.end(), normalizedTag);
    if (enabled) {
        if (existing == tags.end()) {
            tags.push_back(normalizedTag);
        }
    } else if (existing != tags.end()) {
        tags.erase(existing);
    }

    if (tags.empty()) {
        tagsComponents.Remove(entity);
    } else {
        kb::scene::TagsComponent tagsComponent;
        kb::scene::SetTagsText(tagsComponent, JoinTags(tags));
        tagsComponents.Set(entity, tagsComponent);
    }
    return BoolResult("tagged", true);
}

ScriptFunctionCallResult HasTag(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    const std::string tag = Trim(StringArg(arguments, "tag"));
    const kb::scene::TagsComponent* tags = context.scene->Components().Tags().TryGet(entity);
    const bool tagged = tags != nullptr && HasTagValue(kb::scene::TagsText(*tags), tag);
    return BoolResult("tagged", tagged);
}

struct FindByTagContext {
    kb::scene::Scene* scene = nullptr;
    std::string_view tag;
    kb::scene::SceneEntity found{};
};

void FindByTagVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent&, void* rawContext) {
    auto* context = static_cast<FindByTagContext*>(rawContext);
    if (context == nullptr || context->scene == nullptr || context->found.IsValid()) {
        return;
    }
    const kb::scene::TagsComponent* tags = context->scene->Components().Tags().TryGet(entity);
    if (tags != nullptr && HasTagValue(kb::scene::TagsText(*tags), context->tag)) {
        context->found = entity;
    }
}

ScriptFunctionCallResult FindByTag(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const std::string tag = Trim(StringArg(arguments, "tag"));
    FindByTagContext findContext{
        .scene = context.scene,
        .tag = tag,
        .found = {},
    };
    context.scene->Transforms().ForEach(&FindByTagVisitor, &findContext);
    return EntityResult("entity", findContext.found);
}

ScriptFunctionCallResult SetParent(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    const kb::scene::SceneEntity parent = EntityArg(arguments, "parent");
    if (!entity.IsValid() || !context.scene->Entities().IsAlive(entity)) {
        return BoolResult("parented", false);
    }
    const bool parented = context.scene->Hierarchy().SetParent(entity, parent);
    return BoolResult("parented", parented);
}

ScriptFunctionCallResult InstantiatePrefab(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const std::string prefabPath = StringArg(arguments, "prefab");
    if (prefabPath.empty()) {
        return Error("prefab path is empty");
    }
    kb::assets::AssetHandle<kb::scene::ScenePrefab> prefab = context.scene->Assets().LoadPrefab(std::filesystem::path{ prefabPath });
    if (!prefab.IsLoaded()) {
        return Error("prefab asset could not be loaded");
    }
    const kb::scene::ScenePrefabInstance instance = context.scene->Prefabs().Instantiate(*prefab.Get());
    const kb::scene::SceneEntity root = instance.Empty() ? kb::scene::SceneEntity{} : instance.ObjectAt(0).Entity();
    if (root.IsValid() && (HasArg(arguments, "x") || HasArg(arguments, "y") || HasArg(arguments, "z"))) {
        kb::scene::TransformComponent transform = context.scene->Transforms().Get(root);
        ApplyPositionArgs(transform, arguments);
        context.scene->Transforms().Set(root, transform);
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "entity", ScriptValue{ root.Id(), ScriptValueType::Entity } },
            ScriptFunctionArgument{ "count", ScriptValue{ static_cast<int>(instance.ObjectCount()) } },
        },
        .errors = {},
    };
}

bool RegisterFunction(
    ScriptRuntimeHost& host,
    std::string name,
    std::vector<ScriptFunctionPin> inputs,
    std::vector<ScriptFunctionPin> outputs,
    ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = std::move(inputs);
    desc.signature.outputs = std::move(outputs);
    desc.callback = std::move(callback);
    return host.RegisterFunction(std::move(desc));
}

} // namespace

bool ScriptWorldApi::Register(ScriptRuntimeHost& host) {
    bool ok = true;
    ok = RegisterFunction(host, "World.Current",
        {},
        { ScriptFunctionPin{ "world", ScriptValueType::Hash, true } },
        &Current) && ok;
    ok = RegisterFunction(host, "World.IsPlaying",
        {},
        { ScriptFunctionPin{ "playing", ScriptValueType::Bool, true } },
        &IsPlaying) && ok;
    ok = RegisterFunction(host, "World.FrameIndex",
        {},
        { ScriptFunctionPin{ "frame", ScriptValueType::Int64, true } },
        &FrameIndex) && ok;
    ok = RegisterFunction(host, "World.FixedStepIndex",
        {},
        { ScriptFunctionPin{ "step", ScriptValueType::Int64, true } },
        &FixedStepIndex) && ok;
    ok = RegisterFunction(host, "World.FindByName",
        { ScriptFunctionPin{ "name", ScriptValueType::String, true } },
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        &FindByName) && ok;
    ok = RegisterFunction(host, "World.Exists",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        { ScriptFunctionPin{ "exists", ScriptValueType::Bool, true } },
        &Exists) && ok;
    ok = RegisterFunction(host, "World.Name",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        { ScriptFunctionPin{ "name", ScriptValueType::String, true } },
        &Name) && ok;
    ok = RegisterFunction(host, "World.Spawn",
        {
            ScriptFunctionPin{ "name", ScriptValueType::String, false },
            ScriptFunctionPin{ "parent", ScriptValueType::Entity, false },
            ScriptFunctionPin{ "x", ScriptValueType::Float, false },
            ScriptFunctionPin{ "y", ScriptValueType::Float, false },
            ScriptFunctionPin{ "z", ScriptValueType::Float, false },
        },
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        &Spawn) && ok;
    ok = RegisterFunction(host, "World.Destroy",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        { ScriptFunctionPin{ "destroyed", ScriptValueType::Bool, true } },
        &Destroy) && ok;
    ok = RegisterFunction(host, "World.SetTag",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true }, ScriptFunctionPin{ "tag", ScriptValueType::String, true }, ScriptFunctionPin{ "enabled", ScriptValueType::Bool, false } },
        { ScriptFunctionPin{ "tagged", ScriptValueType::Bool, true } },
        &SetTag) && ok;
    ok = RegisterFunction(host, "World.HasTag",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true }, ScriptFunctionPin{ "tag", ScriptValueType::String, true } },
        { ScriptFunctionPin{ "tagged", ScriptValueType::Bool, true } },
        &HasTag) && ok;
    ok = RegisterFunction(host, "World.FindByTag",
        { ScriptFunctionPin{ "tag", ScriptValueType::String, true } },
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        &FindByTag) && ok;
    ok = RegisterFunction(host, "World.SetParent",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true }, ScriptFunctionPin{ "parent", ScriptValueType::Entity, false } },
        { ScriptFunctionPin{ "parented", ScriptValueType::Bool, true } },
        &SetParent) && ok;
    ok = RegisterFunction(host, "World.InstantiatePrefab",
        {
            ScriptFunctionPin{ "prefab", ScriptValueType::String, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, false },
            ScriptFunctionPin{ "y", ScriptValueType::Float, false },
            ScriptFunctionPin{ "z", ScriptValueType::Float, false },
        },
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true }, ScriptFunctionPin{ "count", ScriptValueType::Int, true } },
        &InstantiatePrefab) && ok;
    return ok;
}

} // namespace kb::script
