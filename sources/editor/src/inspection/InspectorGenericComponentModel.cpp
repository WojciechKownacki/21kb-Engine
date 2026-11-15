#include "inspection/InspectorGenericComponentModel.hpp"

#include "localization/EditorLocalization.hpp"
#include "platform/win32/Utf8Gdi.hpp"
#include "scene/EditorSceneContext.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/gameplay/AbilityAsset.hpp"
#include "engine/project/ProjectDescriptor.hpp"
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
#include <map>
#include <memory>

namespace kb::editor {
namespace {

using kb::script::ScriptValue;
using kb::script::ScriptValueType;
using Property = InspectorGenericComponentModel::Property;

[[nodiscard]] bool Set(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const Property& field, const ScriptValue& value) {
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
[[nodiscard]] bool ChooseAsset(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const Property& field, std::string_view type) {
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

bool InspectorGenericComponentModel::Click(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, int editIndex) {
    const Property field = InspectorGenericComponentModel::Locate(editIndex);
    if (field.desc == nullptr || !field.desc->writable) return false;
    const kb::script::ScriptSceneComponentPropertyResult current = kb::script::ScriptSceneComponentApi::GetProperty(
        sceneContext.Scene(), entity, field.section->component, field.desc->name);
    if (!current.succeeded) return false;
    if (field.desc->type == ScriptValueType::Bool) return Set(sceneContext, entity, field, ScriptValue{ !current.value.AsBool() });
    if (field.desc->type == ScriptValueType::Int && !field.desc->enumNames.empty()) {
        const int next = (current.value.AsInt() + 1) % static_cast<int>(field.desc->enumNames.size());
        return Set(sceneContext, entity, field, ScriptValue{ next });
    }
    if (const std::string_view type = InspectorGenericComponentModel::AssetTypeOf(field.section->component, field.desc->name); !type.empty()) {
        return ChooseAsset(sceneContext, entity, field, type);
    }
    sceneContext.Inspector().BeginTextEdit(InspectorPropertyId::GenericComponentField,
        InspectorGenericComponentModel::ValueText(sceneContext.Scene(), field.section->component, *field.desc, current.value));
    sceneContext.Inspector().SetEditIndex(editIndex);
    return true;
}

bool InspectorGenericComponentModel::ApplyText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, int editIndex, std::string_view text) {
    const Property field = InspectorGenericComponentModel::Locate(editIndex);
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
