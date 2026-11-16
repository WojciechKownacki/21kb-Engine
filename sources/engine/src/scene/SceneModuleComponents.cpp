#include "engine/scene/SceneModuleComponents.hpp"

#include "ecs/world/WorldInternalAccess.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentAuthoring.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneHistoryService.hpp"
#include "scene/SceneState.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace kb::scene {
namespace {

namespace IO = SceneAssetBinaryIO;
using kb::ecs::ComponentFieldType;
using kb::ecs::WorldInternalAccess;

constexpr std::uint32_t kMaxFields = 4096U;
constexpr std::size_t kMaxDiagnostics = 64U;

struct CatalogStore {
    std::mutex mutex;
    std::deque<SceneModuleComponentInfo> infos; // a deque keeps every description at its address
    std::unordered_map<std::string, const SceneModuleComponentInfo*> latest;
    std::size_t generation = 0U;
};

[[nodiscard]] CatalogStore& Store() {
    static CatalogStore store;
    return store;
}

void AddProperties(SceneModuleComponentInfo& info, const SceneModuleComponentField& field) {
    const std::string label{ field.label.empty() ? field.name : field.label };
    std::size_t lanes = 1U;
    switch (field.type) {
    case ComponentFieldType::Vec2Float32: lanes = 2U; break;
    case ComponentFieldType::Vec3Float32: lanes = 3U; break;
    case ComponentFieldType::Vec4Float32: lanes = 4U; break;
    case ComponentFieldType::Bytes: return; // raw bytes have no script or Inspector value
    default: break;
    }
    if (lanes == 1U) {
        info.properties.push_back({ std::string{ field.name }, label, std::string{ field.tooltip }, field.type, field.offset });
        return;
    }
    constexpr std::array<const char*, 4U> axes{ "x", "y", "z", "w" };
    for (std::size_t lane = 0U; lane < lanes; ++lane) {
        std::string upper = axes[lane];
        upper[0] = static_cast<char>(upper[0] - 'a' + 'A');
        info.properties.push_back({ std::string{ field.name } + '.' + axes[lane], label + ' ' + upper, std::string{ field.tooltip },
            ComponentFieldType::Float32, field.offset + lane * sizeof(float) });
    }
}

// Field values by name and type. Reading never trusts a size: a truncated or oversized value fails the decode.
void WriteValue(std::vector<std::uint8_t>& bytes, const kb::ecs::SerializedComponentField& field) {
    {
        switch (field.type) {
        case ComponentFieldType::Bool: IO::WriteBool(bytes, std::get<bool>(field.value)); break;
        case ComponentFieldType::Int32:
        case ComponentFieldType::Enum32: IO::WriteInt32(bytes, std::get<std::int32_t>(field.value)); break;
        case ComponentFieldType::UInt32: IO::WriteUInt32(bytes, std::get<std::uint32_t>(field.value)); break;
        case ComponentFieldType::Float32: IO::WriteFloat(bytes, std::get<float>(field.value)); break;
        case ComponentFieldType::Float64: IO::WriteDouble(bytes, std::get<double>(field.value)); break;
        case ComponentFieldType::Vec2Float32:
            for (const float lane : std::get<std::array<float, 2U>>(field.value)) IO::WriteFloat(bytes, lane);
            break;
        case ComponentFieldType::Vec3Float32:
            for (const float lane : std::get<std::array<float, 3U>>(field.value)) IO::WriteFloat(bytes, lane);
            break;
        case ComponentFieldType::Vec4Float32:
            for (const float lane : std::get<std::array<float, 4U>>(field.value)) IO::WriteFloat(bytes, lane);
            break;
        case ComponentFieldType::Bytes: {
            const auto& value = std::get<std::vector<std::byte>>(field.value);
            IO::WriteUInt32(bytes, static_cast<std::uint32_t>(value.size()));
            IO::WriteRaw(bytes, value.data(), value.size());
            break;
        }
        }
    }
}

[[nodiscard]] std::vector<std::uint8_t> Encode(const kb::ecs::SerializedComponent& component) {
    std::vector<std::uint8_t> bytes;
    IO::WriteUInt32(bytes, static_cast<std::uint32_t>(component.fields.size()));
    for (const kb::ecs::SerializedComponentField& field : component.fields) {
        IO::WriteString(bytes, field.name);
        IO::WriteUInt8(bytes, static_cast<std::uint8_t>(field.type));
        WriteValue(bytes, field);
    }
    return bytes;
}

template <std::size_t N>
[[nodiscard]] bool ReadLanes(IO::ByteReader& reader, kb::ecs::ComponentFieldValue& value) {
    std::array<float, N> lanes{};
    for (float& lane : lanes) {
        if (!reader.ReadFloat(lane)) return false;
    }
    value = lanes;
    return true;
}

[[nodiscard]] bool ReadValue(IO::ByteReader& reader, ComponentFieldType type, kb::ecs::ComponentFieldValue& value) {
    switch (type) {
    case ComponentFieldType::Bool: { bool v = false; if (!reader.ReadBool(v)) return false; value = v; return true; }
    case ComponentFieldType::Int32:
    case ComponentFieldType::Enum32: { std::int32_t v = 0; if (!reader.ReadInt32(v)) return false; value = v; return true; }
    case ComponentFieldType::UInt32: { std::uint32_t v = 0U; if (!reader.ReadUInt32(v)) return false; value = v; return true; }
    case ComponentFieldType::Float32: { float v = 0.0F; if (!reader.ReadFloat(v)) return false; value = v; return true; }
    case ComponentFieldType::Float64: { double v = 0.0; if (!reader.ReadDouble(v)) return false; value = v; return true; }
    case ComponentFieldType::Vec2Float32: return ReadLanes<2U>(reader, value);
    case ComponentFieldType::Vec3Float32: return ReadLanes<3U>(reader, value);
    case ComponentFieldType::Vec4Float32: return ReadLanes<4U>(reader, value);
    case ComponentFieldType::Bytes: {
        std::uint32_t size = 0U;
        if (!reader.ReadUInt32(size) || size > SceneAssetFormat::MaxStringBytes) return false;
        std::vector<std::byte> bytes(size);
        if (size != 0U && !reader.ReadRaw(bytes.data(), size)) return false;
        value = std::move(bytes);
        return true;
    }
    }
    return false;
}

[[nodiscard]] bool Decode(const SceneModuleComponentData& data, kb::ecs::SerializedComponent& component) {
    IO::ByteReader reader{ data.bytes };
    std::uint32_t count = 0U;
    if (!reader.ReadUInt32(count) || count > kMaxFields) return false;
    component = {};
    component.componentName = data.name;
    component.fields.resize(count);
    for (kb::ecs::SerializedComponentField& field : component.fields) {
        std::uint8_t type = 0U;
        if (!reader.ReadString(field.name) || !reader.ReadUInt8(type) || type > static_cast<std::uint8_t>(ComponentFieldType::Bytes)) return false;
        field.type = static_cast<ComponentFieldType>(type);
        if (!ReadValue(reader, field.type, field.value)) return false;
    }
    return reader.Exhausted();
}

[[nodiscard]] std::vector<std::uint8_t> ValueBytes(const kb::ecs::SerializedComponentField& field) {
    std::vector<std::uint8_t> bytes;
    WriteValue(bytes, field);
    return bytes;
}

void Note(SceneState& state, std::string message) {
    if (state.moduleComponentDiagnostics.size() < kMaxDiagnostics) {
        state.moduleComponentDiagnostics.push_back(std::move(message));
    }
}

// Turns saved values into a live component. A value that does not fit the registered fields changes nothing.
[[nodiscard]] bool Apply(SceneState& state, SceneEntity entity, const SceneModuleComponentData& data) {
    kb::ecs::SerializedComponent component;
    return Decode(data, component) && state.world.ApplySerializedComponent(entity, component);
}

} // namespace

const SceneModuleComponentInfo* SceneModuleComponentCatalog::Find(std::string_view name) noexcept {
    CatalogStore& store = Store();
    const std::lock_guard lock{ store.mutex };
    const auto found = store.latest.find(std::string{ name });
    return found == store.latest.end() ? nullptr : found->second;
}

std::vector<const SceneModuleComponentInfo*> SceneModuleComponentCatalog::All() {
    CatalogStore& store = Store();
    const std::lock_guard lock{ store.mutex };
    std::vector<const SceneModuleComponentInfo*> all;
    all.reserve(store.latest.size());
    for (const auto& entry : store.latest) all.push_back(entry.second);
    std::ranges::sort(all, {}, &SceneModuleComponentInfo::name);
    return all;
}

std::size_t SceneModuleComponentCatalog::Generation() noexcept {
    CatalogStore& store = Store();
    const std::lock_guard lock{ store.mutex };
    return store.generation;
}

const SceneModuleComponentInfo& SceneModuleComponentCatalog::Add(SceneModuleComponentInfo info) {
    CatalogStore& store = Store();
    const std::lock_guard lock{ store.mutex };
    if (const auto found = store.latest.find(info.name); found != store.latest.end() && *found->second == info) {
        return *found->second;
    }
    const SceneModuleComponentInfo& stored = store.infos.emplace_back(std::move(info));
    store.latest[stored.name] = &stored;
    ++store.generation;
    return stored;
}

SceneModuleComponents::SceneModuleComponents(Scene& scene) noexcept
    : scene_(scene) {}

std::vector<kb::ecs::ComponentFieldDesc> SceneModuleComponents::ReflectionFields(const SceneModuleComponentDesc& desc) {
    std::vector<kb::ecs::ComponentFieldDesc> fields;
    fields.reserve(desc.fields.size());
    for (const SceneModuleComponentField& field : desc.fields) {
        fields.push_back({ field.name, field.type, field.offset, field.size });
    }
    return fields;
}

bool SceneModuleComponents::Activate(const SceneModuleComponentDesc& desc, const std::type_info& type, std::size_t size, std::size_t alignment, const void* defaults) {
    // A prefab override names a component and a field as "<component>/<field>", so a name cannot hold a slash.
    if (desc.name.empty() || desc.name.find('/') != std::string_view::npos || SceneComponentAuthoring::IsBuiltIn(desc.name)) {
        return false;
    }
    SceneState& state = SceneAccess::State(scene_);
    const kb::ecs::ComponentId id = WorldInternalAccess::RegisterComponent(state.world, std::type_index{ type }, desc.name, size, alignment);
    const std::vector<kb::ecs::ComponentFieldDesc> reflected = ReflectionFields(desc);
    if (id == 0 || WorldInternalAccess::RegisterComponentReflection(state.world, id, desc.name, size, reflected) == nullptr) {
        return false;
    }
    SceneModuleComponentInfo info{ .name = std::string{ desc.name }, .label = std::string{ desc.label.empty() ? desc.name : desc.label },
        .category = std::string{ desc.category } };
    info.defaults.assign(static_cast<const std::byte*>(defaults), static_cast<const std::byte*>(defaults) + size);
    for (const SceneModuleComponentField& field : desc.fields) {
        AddProperties(info, field);
    }
    const std::string name = info.name;
    state.moduleComponentTypes[name] = { &SceneModuleComponentCatalog::Add(std::move(info)), id };

    // Values saved while no module provided this type become live components.
    for (auto opaque = state.opaqueModuleComponents.begin(); opaque != state.opaqueModuleComponents.end();) {
        const SceneEntity entity{ opaque->first };
        std::erase_if(opaque->second, [&](const SceneModuleComponentData& data) {
            if (data.name != desc.name) return false;
            if (Apply(state, entity, data)) return true;
            Note(state, "Module component '" + data.name + "' kept as saved: its values do not fit the registered fields.");
            return false;
        });
        opaque = opaque->second.empty() ? state.opaqueModuleComponents.erase(opaque) : std::next(opaque);
    }
    return true;
}

bool SceneModuleComponents::Unregister(std::string_view name) {
    SceneState& state = SceneAccess::State(scene_);
    const auto type = state.moduleComponentTypes.find(std::string{ name });
    if (type == state.moduleComponentTypes.end()) {
        return false;
    }
    const kb::ecs::ComponentId id = type->second.id;
    std::vector<SceneEntity> holders;
    WorldInternalAccess::ForEachComponent(state.world, id, state.world.Reflection(id)->Size(),
        [](SceneEntity entity, const void*, void* context) { static_cast<std::vector<SceneEntity>*>(context)->push_back(entity); }, &holders);
    for (const SceneEntity entity : holders) {
        kb::ecs::SerializedComponent component;
        if (state.world.SerializeComponent(entity, id, component)) {
            state.opaqueModuleComponents[entity.Id()].push_back({ std::string{ name }, Encode(component) });
        }
        WorldInternalAccess::RemoveComponent(state.world, entity, id);
    }
    state.moduleComponentTypes.erase(type);
    return true;
}

bool SceneModuleComponents::IsRegistered(std::string_view name) const {
    return SceneAccess::State(scene_).moduleComponentTypes.contains(std::string{ name });
}

std::vector<SceneModuleComponentData> SceneModuleComponents::Capture(SceneEntity entity) const {
    const SceneState& state = SceneAccess::State(scene_);
    std::vector<SceneModuleComponentData> components;
    if (const auto opaque = state.opaqueModuleComponents.find(entity.Id()); opaque != state.opaqueModuleComponents.end()) {
        components = opaque->second;
    }
    for (const auto& [name, type] : state.moduleComponentTypes) {
        kb::ecs::SerializedComponent component;
        if (WorldInternalAccess::HasComponent(state.world, entity, type.id) && state.world.SerializeComponent(entity, type.id, component)) {
            components.push_back({ name, Encode(component) });
        }
    }
    std::ranges::sort(components, {}, &SceneModuleComponentData::name);
    return components;
}

void SceneModuleComponents::Restore(SceneEntity entity, std::span<const SceneModuleComponentData> components) {
    SceneState& state = SceneAccess::State(scene_);
    if (components.empty() && state.moduleComponentTypes.empty() && state.opaqueModuleComponents.empty()) {
        return;
    }
    SceneHistoryService::NoteObjectChanging(scene_, entity);
    state.opaqueModuleComponents.erase(entity.Id());
    for (const auto& entry : state.moduleComponentTypes) {
        if (WorldInternalAccess::HasComponent(state.world, entity, entry.second.id) &&
            std::ranges::none_of(components, [&entry](const SceneModuleComponentData& data) { return data.name == entry.first; })) {
            WorldInternalAccess::RemoveComponent(state.world, entity, entry.second.id);
        }
    }
    for (const SceneModuleComponentData& data : components) {
        const bool registered = state.moduleComponentTypes.contains(data.name);
        if (registered && Apply(state, entity, data)) {
            continue;
        }
        if (registered) {
            Note(state, "Module component '" + data.name + "' kept as saved: its values do not fit the registered fields.");
        }
        state.opaqueModuleComponents[entity.Id()].push_back(data);
    }
}

bool SceneModuleComponents::Add(SceneEntity entity, std::string_view name) {
    SceneState& state = SceneAccess::State(scene_);
    const auto type = state.moduleComponentTypes.find(std::string{ name });
    if (type == state.moduleComponentTypes.end() || !scene_.Entities().IsAlive(entity) ||
        WorldInternalAccess::HasComponent(state.world, entity, type->second.id)) {
        return false;
    }
    SceneHistoryService::NoteObjectChanging(scene_, entity);
    const std::vector<std::byte>& defaults = type->second.info->defaults;
    WorldInternalAccess::SetComponent(state.world, entity, type->second.id, defaults.size(), defaults.data());
    return true;
}

bool SceneModuleComponents::Remove(SceneEntity entity, std::string_view name) {
    SceneState& state = SceneAccess::State(scene_);
    const auto type = state.moduleComponentTypes.find(std::string{ name });
    if (type == state.moduleComponentTypes.end() || !scene_.Entities().IsAlive(entity) ||
        !WorldInternalAccess::HasComponent(state.world, entity, type->second.id)) {
        return false;
    }
    SceneHistoryService::NoteObjectChanging(scene_, entity);
    WorldInternalAccess::RemoveComponent(state.world, entity, type->second.id);
    return true;
}

void SceneModuleComponents::RestoreComponent(SceneEntity entity, std::string_view name, const SceneModuleComponentData* data) {
    std::vector<SceneModuleComponentData> components = Capture(entity);
    std::erase_if(components, [name](const SceneModuleComponentData& existing) { return existing.name == name; });
    if (data != nullptr) {
        components.push_back(*data);
    }
    Restore(entity, components);
}

std::optional<std::vector<std::string>> SceneModuleComponents::DifferingFields(const SceneModuleComponentData& left, const SceneModuleComponentData& right) {
    kb::ecs::SerializedComponent a;
    kb::ecs::SerializedComponent b;
    if (!Decode(left, a) || !Decode(right, b)) {
        return std::nullopt;
    }
    std::vector<std::string> differing;
    for (const kb::ecs::SerializedComponentField& field : a.fields) {
        const auto other = std::ranges::find(b.fields, field.name, &kb::ecs::SerializedComponentField::name);
        if (other == b.fields.end() || other->type != field.type || ValueBytes(*other) != ValueBytes(field)) {
            differing.push_back(field.name);
        }
    }
    for (const kb::ecs::SerializedComponentField& field : b.fields) {
        if (std::ranges::find(a.fields, field.name, &kb::ecs::SerializedComponentField::name) == a.fields.end()) {
            differing.push_back(field.name);
        }
    }
    return differing;
}

std::optional<std::vector<std::uint8_t>> SceneModuleComponents::FieldValue(const SceneModuleComponentData& data, std::string_view field) {
    kb::ecs::SerializedComponent component;
    if (!Decode(data, component)) {
        return std::nullopt;
    }
    const auto found = std::ranges::find(component.fields, field, &kb::ecs::SerializedComponentField::name);
    if (found == component.fields.end()) {
        return std::nullopt;
    }
    return ValueBytes(*found);
}

bool SceneModuleComponents::SetFieldValue(SceneModuleComponentData& data, std::string_view field, std::span<const std::uint8_t> value) {
    kb::ecs::SerializedComponent component;
    if (!Decode(data, component)) {
        return false;
    }
    const auto found = std::ranges::find(component.fields, field, &kb::ecs::SerializedComponentField::name);
    if (found == component.fields.end()) {
        return false;
    }
    IO::ByteReader reader{ std::vector<std::uint8_t>{ value.begin(), value.end() } };
    if (!ReadValue(reader, found->type, found->value) || !reader.Exhausted()) {
        return false;
    }
    data.bytes = Encode(component);
    return true;
}

bool SceneModuleComponents::Has(SceneEntity entity, std::string_view name) const {
    return Data(entity, name) != nullptr;
}

const void* SceneModuleComponents::Data(SceneEntity entity, std::string_view name) const {
    const SceneState& state = SceneAccess::State(scene_);
    const auto type = state.moduleComponentTypes.find(std::string{ name });
    if (type == state.moduleComponentTypes.end() || !scene_.Entities().IsAlive(entity)) {
        return nullptr;
    }
    return WorldInternalAccess::TryGetComponent(state.world, entity, type->second.id);
}

void* SceneModuleComponents::MutableData(SceneEntity entity, std::string_view name) {
    SceneState& state = SceneAccess::State(scene_);
    const auto type = state.moduleComponentTypes.find(std::string{ name });
    if (type == state.moduleComponentTypes.end() || !scene_.Entities().IsAlive(entity)) {
        return nullptr;
    }
    SceneHistoryService::NoteObjectChanging(scene_, entity);
    return WorldInternalAccess::TryGetMutableComponent(state.world, entity, type->second.id);
}

void SceneModuleComponents::MarkModified(SceneEntity entity, std::string_view name) {
    SceneState& state = SceneAccess::State(scene_);
    if (const auto type = state.moduleComponentTypes.find(std::string{ name }); type != state.moduleComponentTypes.end()) {
        WorldInternalAccess::MarkComponentModified(state.world, entity, type->second.id);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

std::span<const std::string> SceneModuleComponents::Diagnostics() const noexcept {
    return SceneAccess::State(scene_).moduleComponentDiagnostics;
}

} // namespace kb::scene
