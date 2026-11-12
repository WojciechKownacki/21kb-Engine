#include "inspection/InspectorGenericComponentModel.hpp"

#include "localization/EditorLocalization.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/gameplay/AbilityAsset.hpp"
#include "engine/library/EngineLibraryComponentInspectorDesc.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/AIBrainComponent.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneComponentAuthoring.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "engine/script/ScriptValue.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>

namespace kb::editor {
namespace {

using kb::script::ScriptValue;
using kb::script::ScriptValueType;

constexpr std::array<InspectorGenericComponentModel::Section, 11> kSections{ {
    { "NavBounds", InspectorSectionId::NavBounds, HeroIconKind::Navigation, "Navigation Bounds" },
    { "Combatant", InspectorSectionId::Combatant, HeroIconKind::Gamepad2, "Combatant" },
    { "AIBrain", InspectorSectionId::AIBrain, HeroIconKind::Gamepad2, "AI Brain" },
    { "CharacterController2D", InspectorSectionId::CharacterController2D, HeroIconKind::RectangleGroup, "Character Controller 2D" },
    { "Sprite2D", InspectorSectionId::Sprite2D, HeroIconKind::RectangleGroup, "Sprite 2D" },
    { "Body2D", InspectorSectionId::Body2D, HeroIconKind::RectangleGroup, "Body 2D" },
    { "Collider2D", InspectorSectionId::Collider2D, HeroIconKind::RectangleGroup, "Collider 2D" },
    { "Grid", InspectorSectionId::Grid, HeroIconKind::RectangleGroup, "Grid" },
    { "Tilemap", InspectorSectionId::Tilemap, HeroIconKind::RectangleGroup, "Tilemap" },
    { "TilemapRenderer", InspectorSectionId::TilemapRenderer, HeroIconKind::RectangleGroup, "Tilemap Renderer" },
    { "TilemapCollider", InspectorSectionId::TilemapCollider, HeroIconKind::RectangleGroup, "Tilemap Collider" },

} };

// The rows every author uses; the rest of a section's rows wait behind its Advanced row. A section with no entry shows
// all of them.
constexpr std::array<std::string_view, 12> kSprite2DBasicRows{ "imageAssetId", "cellWidth", "cellHeight", "frameCount",
    "framesPerSecond", "loop", "playing", "flipX", "flipY", "tintA", "sortingOrder", "visible" };

// Navigation Bounds: the box and the size of the characters; how finely the mesh is cut waits behind Advanced.
constexpr std::array<std::string_view, 10> kNavBoundsBasicRows{ "size.x", "size.y", "size.z", "center.x", "center.y", "center.z",
    "agentRadius", "agentHeight", "maxStepHeight", "maxSlopeDegrees" };

[[nodiscard]] std::span<const std::string_view> BasicRowsOf(std::string_view component) noexcept {
    if (component == "Sprite2D") return kSprite2DBasicRows;
    if (component == "NavBounds") return kNavBoundsBasicRows;
    return {};
}

[[nodiscard]] bool IsAdvanced(std::string_view component, std::string_view property) noexcept {
    const std::span<const std::string_view> basic = BasicRowsOf(component);
    return !basic.empty() && std::ranges::find(basic, property) == basic.end();
}

// The unit shown after a number.
[[nodiscard]] std::string_view UnitOf(std::string_view component, std::string_view property) noexcept {
    if (component == "NavBounds") return property == "maxSlopeDegrees" ? "\u00B0" : "m";
    if (component != "Sprite2D") return {};
    if (property == "cellWidth" || property == "cellHeight" || property == "margin" || property == "spacing" ||
        property == "imageWidth" || property == "imageHeight" || property == "pixelsPerUnit") return "px";
    if (property == "framesPerSecond") return "fps";
    if (property == "time") return "s";
    return {};
}

// A component as Add Component creates it, in a scene of its own that is built once: what "default" means.
struct DefaultComponent {
    kb::scene::Scene scene;
    kb::scene::SceneEntity entity{};
};

[[nodiscard]] const DefaultComponent* DefaultsOf(std::string_view component) {
    // Never destroyed: a scene torn down during static destruction could outlive the services it releases into.
    static auto& built = *new std::map<std::string, std::unique_ptr<DefaultComponent>, std::less<>>();
    if (const auto found = built.find(component); found != built.end()) return found->second.get();
    auto fresh = std::make_unique<DefaultComponent>();
    fresh->entity = fresh->scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Default" });
    kb::project::ProjectDescriptor project;
    if (const kb::scene::SceneComponentKind* kind = kb::scene::SceneComponentAuthoring::Find(component);
        kind != nullptr && !kind->requiredPlugin.empty()) {
        project.plugins.push_back(kb::project::ProjectPluginReference{ .name = std::string{ kind->requiredPlugin } });
    }
    const bool added = kb::scene::SceneComponentAuthoring::Add(fresh->scene, fresh->entity, component, project).status ==
        kb::scene::SceneComponentAddStatus::Added;
    return built.emplace(std::string{ component }, added ? std::move(fresh) : nullptr).first->second.get();
}

// An AI Brain shows its rules in use and one empty rule to fill, and a value only where the
// condition or the action takes one: the list reads like the rules themselves.
[[nodiscard]] bool Visible(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::string_view component, std::string_view property) {
    if (component != "AIBrain" || !property.starts_with("rule")) return true;
    const kb::scene::AIBrainComponent* brain = scene.Components().AIBrains().TryGet(entity);
    if (brain == nullptr) return true;
    const std::size_t number = static_cast<std::size_t>(property[4] - '0');
    if (number < 1U || number > kb::scene::kAIBrainRuleCount) return true;
    std::size_t used = 0U;
    for (std::size_t index = 0U; index < kb::scene::kAIBrainRuleCount; ++index) {
        if (brain->rules[index].when != kb::scene::AICondition::Never) used = index + 1U;
    }
    if (number > used + 1U) return false;
    const kb::scene::AIRule& rule = brain->rules[number - 1U];
    const std::string_view field = property.substr(5U);
    if (field == "When") return true;
    if (rule.when == kb::scene::AICondition::Never) return false;
    if (field == "WhenValue") return kb::scene::AIConditionTakesValue(rule.when);
    if (field == "DoValue") return kb::scene::AIActionTakesValue(rule.action);
    return true;
}

// A rule's value named by what it is for its condition or action: "2. Meters", "1. Ability Slot".
[[nodiscard]] std::string ValueLabel(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::string_view component, std::string_view property) {
    if (component != "AIBrain" || !property.starts_with("rule") || !property.ends_with("Value")) return {};
    const kb::scene::AIBrainComponent* brain = scene.Components().AIBrains().TryGet(entity);
    const std::size_t number = static_cast<std::size_t>(property[4] - '0');
    if (brain == nullptr || number < 1U || number > kb::scene::kAIBrainRuleCount) return {};
    const kb::scene::AIRule& rule = brain->rules[number - 1U];
    std::string_view unit;
    if (property.ends_with("WhenValue")) {
        switch (rule.when) {
        case kb::scene::AICondition::EnemyCloserThan:
        case kb::scene::AICondition::EnemyFartherThan:
        case kb::scene::AICondition::FarFromHome: unit = "Meters"; break;
        case kb::scene::AICondition::HealthBelowPercent:
        case kb::scene::AICondition::ManaAbovePercent: unit = "Percent"; break;
        case kb::scene::AICondition::AbilityReady: unit = "Ability Slot"; break;
        default: return {};
        }
    } else {
        switch (rule.action) {
        case kb::scene::AIAction::UseAbility: unit = "Ability Slot"; break;
        case kb::scene::AIAction::KeepDistance: unit = "Meters"; break;
        default: return {};
        }
    }
    return std::to_string(number) + ". " + std::string{ EditorText("inspector.aiBrain.unit." + std::string{ unit }, unit) };
}

[[nodiscard]] std::string Number(float value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%.3g", static_cast<double>(value));
    return buffer;
}

// The asset type an id field takes, so it can offer a menu of them instead of a number.
[[nodiscard]] std::string_view AssetTypeFor(std::string_view component, std::string_view property) noexcept {
    if (component == "Combatant" && property.starts_with("ability")) return kb::gameplay::kAbilityAssetType;
    if (component == "Sprite2D" && property == "imageAssetId") return "Texture";
    if (component == "Sprite2D" && property == "materialAssetId") return "RenderMaterial";
    if (component == "Tilemap" && property == "mapAssetId") return "RenderMesh";
    return {};
}

[[nodiscard]] std::string ValueTextFor(kb::scene::Scene& scene, std::string_view component,
    const kb::script::ScriptSceneComponentPropertyDesc& desc, const ScriptValue& value) {
    switch (desc.type) {
    case ScriptValueType::Bool: return value.AsBool() ? "true" : "false";
    case ScriptValueType::Float: return Number(value.AsFloat());
    case ScriptValueType::Int: {
        const int number = value.AsInt();
        if (!desc.enumNames.empty() && number >= 0 && static_cast<std::size_t>(number) < desc.enumNames.size()) return std::string{ desc.enumNames[number] };
        return std::to_string(number);
    }
    case ScriptValueType::UInt32: return std::to_string(value.AsUInt32());
    case ScriptValueType::Hash: {
        const std::uint64_t id = value.AsUInt64();
        if (id == 0U) return std::string{ EditorText("inspector.generic.none", "None") };
        if (!AssetTypeFor(component, desc.name).empty()) {
            const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ id });
            return metadata == nullptr ? std::string{ EditorText("inspector.generic.missing", "Missing") } : metadata->virtualPath.stem().string();
        }
        return std::to_string(id);
    }
    case ScriptValueType::String: return value.AsString();
    default: return {};
    }
}

struct Field {
    const InspectorGenericComponentModel::Section* section = nullptr;
    const kb::script::ScriptSceneComponentPropertyDesc* desc = nullptr;
};

[[nodiscard]] Field FieldAt(int editIndex) noexcept {
    const int sectionIndex = editIndex / InspectorGenericComponentModel::kFieldsPerSection;
    const int fieldIndex = editIndex % InspectorGenericComponentModel::kFieldsPerSection;
    if (editIndex < 0 || sectionIndex >= static_cast<int>(kSections.size())) return {};
    const auto properties = kb::script::ScriptSceneComponentApi::ComponentProperties(kSections[sectionIndex].component);
    if (fieldIndex >= static_cast<int>(properties.size())) return {};
    return Field{ &kSections[sectionIndex], &properties[fieldIndex] };
}

} // namespace

std::span<const InspectorGenericComponentModel::Section> InspectorGenericComponentModel::Sections() noexcept { return kSections; }

const InspectorGenericComponentModel::Section* InspectorGenericComponentModel::Find(InspectorSectionId id) noexcept {
    for (const Section& section : kSections) {
        if (section.id == id) return &section;
    }
    return nullptr;
}

std::vector<InspectorGenericComponentModel::Row> InspectorGenericComponentModel::Rows(
    kb::scene::Scene& scene, kb::scene::SceneEntity entity, const Section& section) {
    std::vector<Row> rows;
    const int sectionIndex = static_cast<int>(&section - kSections.data());
    const auto properties = kb::script::ScriptSceneComponentApi::ComponentProperties(section.component);
    for (std::size_t index = 0U; index < properties.size(); ++index) {
        const kb::script::ScriptSceneComponentPropertyDesc& desc = properties[index];
        if (!Visible(scene, entity, section.component, desc.name)) continue;
        const kb::script::ScriptSceneComponentPropertyResult value =
            kb::script::ScriptSceneComponentApi::GetProperty(scene, entity, section.component, desc.name);
        const kb::library::LibraryComponentInspectorFieldDesc* label =
            kb::library::EngineLibraryComponentInspectorRegistry::FindField(section.component, desc.name);
        Row row{ .label = label != nullptr ? std::string{ label->displayName } : std::string{ desc.name },
                 .editIndex = sectionIndex * kFieldsPerSection + static_cast<int>(index) };
        if (const std::string unit = ValueLabel(scene, entity, section.component, desc.name); !unit.empty()) row.label = unit;
        if (value.succeeded) {
            row.isBool = desc.type == ScriptValueType::Bool;
            row.boolValue = row.isBool && value.value.AsBool();
            row.value = ValueTextFor(scene, section.component, desc, value.value);
        }
        row.unit = UnitOf(section.component, desc.name);
        row.advanced = IsAdvanced(section.component, desc.name);
        rows.push_back(std::move(row));
    }
    std::stable_partition(rows.begin(), rows.end(), [](const Row& row) { return !row.advanced; });
    return rows;
}

int InspectorGenericComponentModel::BasicCount(const std::vector<Row>& rows) noexcept {
    return static_cast<int>(std::ranges::count_if(rows, [](const Row& row) { return !row.advanced; }));
}

std::string_view InspectorGenericComponentModel::Tooltip(int editIndex) {
    const Field field = FieldAt(editIndex);
    if (field.desc == nullptr) return {};
    const kb::library::LibraryComponentInspectorFieldDesc* label =
        kb::library::EngineLibraryComponentInspectorRegistry::FindField(field.section->component, field.desc->name);
    return label != nullptr ? label->tooltip : std::string_view{};
}

std::vector<int> InspectorGenericComponentModel::ChangedRows(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, const Section& section) {
    std::vector<int> changed;
    const DefaultComponent* defaults = DefaultsOf(section.component);
    if (defaults == nullptr) return changed;
    // Reading a property takes the scene by reference; nothing is written.
    kb::scene::Scene& readable = const_cast<kb::scene::Scene&>(scene);
    kb::scene::Scene& defaultScene = const_cast<kb::scene::Scene&>(defaults->scene);
    const int sectionIndex = static_cast<int>(&section - kSections.data());
    const auto properties = kb::script::ScriptSceneComponentApi::ComponentProperties(section.component);
    for (std::size_t index = 0U; index < properties.size(); ++index) {
        const kb::script::ScriptSceneComponentPropertyDesc& desc = properties[index];
        if (!desc.writable || !AssetTypeFor(section.component, desc.name).empty() || desc.type == ScriptValueType::Hash) continue;
        const kb::script::ScriptSceneComponentPropertyResult now =
            kb::script::ScriptSceneComponentApi::GetProperty(readable, entity, section.component, desc.name);
        const kb::script::ScriptSceneComponentPropertyResult fresh =
            kb::script::ScriptSceneComponentApi::GetProperty(defaultScene, defaults->entity, section.component, desc.name);
        if (now.succeeded && fresh.succeeded && !(now.value == fresh.value)) {
            changed.push_back(sectionIndex * kFieldsPerSection + static_cast<int>(index));
        }
    }
    return changed;
}

bool InspectorGenericComponentModel::ResetRow(kb::scene::Scene& scene, kb::scene::SceneEntity entity, int editIndex) {
    const Field field = FieldAt(editIndex);
    if (field.desc == nullptr || !field.desc->writable) return false;
    const DefaultComponent* defaults = DefaultsOf(field.section->component);
    if (defaults == nullptr) return false;
    const kb::script::ScriptSceneComponentPropertyResult fresh = kb::script::ScriptSceneComponentApi::GetProperty(
        const_cast<kb::scene::Scene&>(defaults->scene), defaults->entity, field.section->component, field.desc->name);
    return fresh.succeeded && kb::script::ScriptSceneComponentApi::SetProperty(
        scene, entity, field.section->component, field.desc->name, fresh.value).succeeded;
}

bool InspectorGenericComponentModel::Capture(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, const Section& section, Values& out) {
    out.clear();
    kb::scene::Scene& readable = const_cast<kb::scene::Scene&>(scene);
    for (const kb::script::ScriptSceneComponentPropertyDesc& desc : kb::script::ScriptSceneComponentApi::ComponentProperties(section.component)) {
        if (!desc.writable) continue;
        const kb::script::ScriptSceneComponentPropertyResult value =
            kb::script::ScriptSceneComponentApi::GetProperty(readable, entity, section.component, desc.name);
        if (value.succeeded) out.emplace_back(std::string{ desc.name }, value.value);
    }
    return !out.empty();
}

bool InspectorGenericComponentModel::Apply(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const Section& section, const Values& values) {
    bool any = false;
    for (const auto& [name, value] : values) {
        any = kb::script::ScriptSceneComponentApi::SetProperty(scene, entity, section.component, name, value).succeeded || any;
    }
    return any;
}

InspectorGenericComponentModel::Property InspectorGenericComponentModel::Locate(int editIndex) noexcept {
    const Field field = FieldAt(editIndex);
    return Property{ field.section, field.desc };
}

std::string_view InspectorGenericComponentModel::AssetTypeOf(std::string_view component, std::string_view property) noexcept {
    return AssetTypeFor(component, property);
}

std::string InspectorGenericComponentModel::ValueText(kb::scene::Scene& scene, std::string_view component,
    const kb::script::ScriptSceneComponentPropertyDesc& desc, const kb::script::ScriptValue& value) {
    return ValueTextFor(scene, component, desc, value);
}

} // namespace kb::editor
