#include "engine/script/ScriptApiCatalog.hpp"

#include "engine/script/ScriptApiNameCollector.hpp"
#include "engine/script/ScriptLifecycle.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"

#include <array>

namespace kb::script {

namespace {

constexpr std::array kLifecycleEvents{
    ScriptLifecycleEvent::Created,
    ScriptLifecycleEvent::Activated,
    ScriptLifecycleEvent::Ready,
    ScriptLifecycleEvent::FixedTick,
    ScriptLifecycleEvent::Tick,
    ScriptLifecycleEvent::LateTick,
    ScriptLifecycleEvent::BeforeRender,
    ScriptLifecycleEvent::AfterRender,
    ScriptLifecycleEvent::Deactivated,
    ScriptLifecycleEvent::Destroyed,
};

struct LuaBindingSpec {
    std::string_view tableName;
    std::string_view luaName;
    std::string_view functionName;
    ScriptApiCatalogLuaReturnKind returnKind = ScriptApiCatalogLuaReturnKind::Default;
    std::string_view returnPin;
};

// Mirrors the sandbox surface installed by PucLuaFunctionApi::Attach — both
// the names and each wrapper's return shape. When a table, field, or return
// path changes there, update this list so generated stubs stay true to what
// scripts can actually call.
constexpr std::array<LuaBindingSpec, 25> kLuaBindings{ {
    { "Audio", "Play", "Audio.Play", ScriptApiCatalogLuaReturnKind::SingleOutput, "voice" },
    { "World", "FindByName", "World.FindByName", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "World", "FindByTag", "World.FindByTag", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "World", "Exists", "World.Exists", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "World", "Name", "World.Name", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "World", "Spawn", "World.Spawn", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "World", "Destroy", "World.Destroy", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "World", "SetTag", "World.SetTag", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "World", "HasTag", "World.HasTag", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "World", "SetParent", "World.SetParent", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "World", "InstantiatePrefab", "World.InstantiatePrefab", ScriptApiCatalogLuaReturnKind::SingleOutput, "entity" },
    { "Time", "delta", "Time.Delta", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Transform", "GetPosition", "Transform.GetPosition", ScriptApiCatalogLuaReturnKind::GuardedTable, "found" },
    { "Transform", "SetPosition", "Transform.SetPosition", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Transform", "Translate", "Transform.Translate", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Physics", "Raycast", "Physics.Raycast", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Input", "IsPressed", "Input.IsPressed", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "WasPressed", "Input.WasPressed", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "WasReleased", "Input.WasReleased", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "Value", "Input.Value", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "Vector2", "Input.Vector2", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Input", "Vector3", "Input.Vector3", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Input", "AddMappingContext", "Input.AddMappingContext", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "RemoveMappingContext", "Input.RemoveMappingContext", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "", "Log", "Log", ScriptApiCatalogLuaReturnKind::Default, "" },
} };

[[nodiscard]] std::vector<ScriptApiPin> ToApiPins(const std::vector<ScriptFunctionPin>& pins) {
    std::vector<ScriptApiPin> converted;
    converted.reserve(pins.size());
    for (const ScriptFunctionPin& pin : pins) {
        converted.push_back(ScriptApiPin{ .name = pin.name, .type = pin.type, .required = pin.required });
    }
    return converted;
}

} // namespace

const ScriptApiCatalogFunction* ScriptApiCatalog::FindFunction(std::string_view name) const noexcept {
    for (const ScriptApiCatalogFunction& function : functions) {
        if (function.name == name) {
            return &function;
        }
    }
    return nullptr;
}

ScriptApiCatalog ScriptApiCatalog::Build(const ScriptRuntimeHost& host) {
    ScriptApiCatalog catalog;

    catalog.lifecycleEvents.reserve(kLifecycleEvents.size());
    for (const ScriptLifecycleEvent event : kLifecycleEvents) {
        catalog.lifecycleEvents.emplace_back(ToString(event));
    }

    const std::vector<ScriptFunctionDesc>& functions = host.Functions().Functions();
    catalog.functions.reserve(functions.size());
    for (const ScriptFunctionDesc& function : functions) {
        catalog.functions.push_back(ScriptApiCatalogFunction{
            .name = function.signature.name,
            .inputs = ToApiPins(function.signature.inputs),
            .outputs = ToApiPins(function.signature.outputs),
        });
    }

    for (const std::string_view componentName : ScriptSceneComponentApi::ComponentNames()) {
        ScriptApiCatalogComponent component;
        component.name = std::string{ componentName };
        for (const ScriptSceneComponentPropertyDesc& property : ScriptSceneComponentApi::ComponentProperties(componentName)) {
            component.properties.push_back(ScriptApiCatalogProperty{
                .name = std::string{ property.name },
                .type = property.type,
                .writable = property.writable,
            });
        }
        catalog.components.push_back(std::move(component));
    }

    catalog.luaBindings.reserve(kLuaBindings.size());
    for (const LuaBindingSpec& binding : kLuaBindings) {
        catalog.luaBindings.push_back(ScriptApiCatalogLuaBinding{
            .tableName = std::string{ binding.tableName },
            .luaName = std::string{ binding.luaName },
            .functionName = std::string{ binding.functionName },
            .returnKind = binding.returnKind,
            .returnPin = std::string{ binding.returnPin },
        });
    }

    return catalog;
}

const char* ToString(ScriptApiCatalogLuaReturnKind kind) noexcept {
    switch (kind) {
    case ScriptApiCatalogLuaReturnKind::Default:
        return "Default";
    case ScriptApiCatalogLuaReturnKind::SingleOutput:
        return "SingleOutput";
    case ScriptApiCatalogLuaReturnKind::OutputsTable:
        return "OutputsTable";
    case ScriptApiCatalogLuaReturnKind::GuardedTable:
        return "GuardedTable";
    }
    return "Default";
}

ScriptApiCatalog ScriptApiCatalog::Build(const ScriptRuntimeHost& host, kb::assets::AssetManager& assets) {
    ScriptApiCatalog catalog = Build(host);
    const ScriptApiNameCollectionResult collected = ScriptApiNameCollector::CollectProjectAssets(assets);
    catalog.projectEntries = collected.names.Entries();
    return catalog;
}

} // namespace kb::script
