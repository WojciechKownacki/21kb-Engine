#include "inspection/InspectorGenericComponentModel.hpp"

#include "localization/EditorLocalization.hpp"
#include "platform/win32/Utf8Gdi.hpp"
#include "scene/EditorSceneContext.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/gameplay/AbilityAsset.hpp"
#include "engine/library/EngineLibraryComponentInspectorDesc.hpp"
#include "engine/scene/AIBrainComponent.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentAuthoring.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "engine/script/ScriptValue.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace kb::editor {
namespace {

using kb::script::ScriptValue;
using kb::script::ScriptValueType;

constexpr std::array<InspectorGenericComponentModel::Section, 10> kSections{ {
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
[[nodiscard]] std::string_view AssetTypeOf(std::string_view component, std::string_view property) noexcept {
    if (component == "Combatant" && property.starts_with("ability")) return kb::gameplay::kAbilityAssetType;
    if (component == "Sprite2D" && property == "imageAssetId") return "Texture";
    if (component == "Sprite2D" && property == "materialAssetId") return "RenderMaterial";
    if (component == "Tilemap" && property == "mapAssetId") return "RenderMesh";
    return {};
}

[[nodiscard]] std::string ValueText(kb::scene::Scene& scene, std::string_view component,
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
        if (!AssetTypeOf(component, desc.name).empty()) {
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

[[nodiscard]] Field Decode(int editIndex) noexcept {
    const int sectionIndex = editIndex / InspectorGenericComponentModel::kFieldsPerSection;
    const int fieldIndex = editIndex % InspectorGenericComponentModel::kFieldsPerSection;
    if (editIndex < 0 || sectionIndex >= static_cast<int>(kSections.size())) return {};
    const auto properties = kb::script::ScriptSceneComponentApi::ComponentProperties(kSections[sectionIndex].component);
    if (fieldIndex >= static_cast<int>(properties.size())) return {};
    return Field{ &kSections[sectionIndex], &properties[fieldIndex] };
}

[[nodiscard]] bool Set(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const Field& field, const ScriptValue& value) {
    if (!sceneContext.BeginSceneEditTransaction("Edit " + std::string{ field.section->title })) return false;
    const kb::script::ScriptSceneComponentMutationResult result = kb::script::ScriptSceneComponentApi::SetProperty(
        sceneContext.Scene(), entity, field.section->component, field.desc->name, value);
    if (!result.succeeded) {
        sceneContext.CancelSceneEditTransaction();
        return false;
    }
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

// The assets of the type the slot takes, by name, and None.
[[nodiscard]] bool ChooseAsset(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const Field& field, std::string_view type) {
    auto assets = sceneContext.Scene().Assets().Manager().Registry().ByType(type);
    if (field.section->component == "Tilemap") std::erase_if(assets, [](const auto& asset) { return asset.virtualPath.extension() != ".kbmap2d"; });
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return false;
    constexpr UINT_PTR kNone = 1U;
    constexpr UINT_PTR kFirst = 100U;
    kb::editor::AppendMenuUtf8(menu, MF_STRING, kNone, std::string{ EditorText("inspector.generic.none", "None") }.c_str());
    for (std::size_t index = 0U; index < assets.size(); ++index) {
        kb::editor::AppendMenuUtf8(menu, MF_STRING, kFirst + index, assets[index].virtualPath.stem().string().c_str());
    }
    if (assets.empty()) {
        kb::editor::AppendMenuUtf8(menu, MF_STRING | MF_GRAYED, 0U,
            std::string{ EditorText("inspector.generic.noAssets", "No matching assets in the project yet") }.c_str());
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, cursor.x, cursor.y, 0, GetActiveWindow(), nullptr);
    DestroyMenu(menu);
    if (command == kNone) return Set(sceneContext, entity, field, ScriptValue{ std::uint64_t{ 0U }, ScriptValueType::Hash });
    if (command >= kFirst && command - kFirst < assets.size()) {
        return Set(sceneContext, entity, field, ScriptValue{ assets[command - kFirst].id.value, ScriptValueType::Hash });
    }
    return false;
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
            row.value = ValueText(scene, section.component, desc, value.value);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

bool InspectorGenericComponentModel::Click(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, int editIndex) {
    const Field field = Decode(editIndex);
    if (field.desc == nullptr || !field.desc->writable) return false;
    const kb::script::ScriptSceneComponentPropertyResult current = kb::script::ScriptSceneComponentApi::GetProperty(
        sceneContext.Scene(), entity, field.section->component, field.desc->name);
    if (!current.succeeded) return false;
    if (field.desc->type == ScriptValueType::Bool) return Set(sceneContext, entity, field, ScriptValue{ !current.value.AsBool() });
    if (field.desc->type == ScriptValueType::Int && !field.desc->enumNames.empty()) {
        const int next = (current.value.AsInt() + 1) % static_cast<int>(field.desc->enumNames.size());
        return Set(sceneContext, entity, field, ScriptValue{ next });
    }
    if (const std::string_view type = AssetTypeOf(field.section->component, field.desc->name); !type.empty()) {
        return ChooseAsset(sceneContext, entity, field, type);
    }
    sceneContext.Inspector().BeginTextEdit(InspectorPropertyId::GenericComponentField,
        ValueText(sceneContext.Scene(), field.section->component, *field.desc, current.value));
    sceneContext.Inspector().SetEditIndex(editIndex);
    return true;
}

bool InspectorGenericComponentModel::ApplyText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, int editIndex, std::string_view text) {
    const Field field = Decode(editIndex);
    if (field.desc == nullptr) return false;
    const auto parse = [text]<typename T>(T& output) {
        const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), output);
        return code == std::errc{} && end == text.data() + text.size();
    };
    switch (field.desc->type) {
    case ScriptValueType::Float: {
        float value = 0.0F;
        return parse(value) && std::isfinite(value) && Set(sceneContext, entity, field, ScriptValue{ value });
    }
    case ScriptValueType::Int: {
        int value = 0;
        return parse(value) && Set(sceneContext, entity, field, ScriptValue{ value });
    }
    case ScriptValueType::UInt32: {
        std::uint32_t value = 0U;
        return parse(value) && Set(sceneContext, entity, field, ScriptValue{ value });
    }
    case ScriptValueType::Hash: {
        std::uint64_t value = 0U;
        return parse(value) && Set(sceneContext, entity, field, ScriptValue{ value, ScriptValueType::Hash });
    }
    case ScriptValueType::String: return Set(sceneContext, entity, field, ScriptValue{ std::string{ text } });
    default: return false;
    }
}

} // namespace kb::editor
