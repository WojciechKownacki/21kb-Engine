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
// PucLuaTaskApi::Attach adds the Task entries below. The names and each
// wrapper's return shape must stay true to the callable sandbox. When a table, field, or return
// path changes there, update this list so generated stubs stay true to what
// scripts can actually call.
constexpr std::array<LuaBindingSpec, 154> kLuaBindings{ {
    { "Audio", "Play", "Audio.Play", ScriptApiCatalogLuaReturnKind::SingleOutput, "voice" },
    { "Audio", "SetMixer", "Audio.SetMixer", ScriptApiCatalogLuaReturnKind::SingleOutput, "assigned" },
    { "Audio", "ActiveMixer", "Audio.ActiveMixer", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Audio", "SetSnapshot", "Audio.SetSnapshot", ScriptApiCatalogLuaReturnKind::SingleOutput, "applied" },
    { "Audio", "ActiveSnapshot", "Audio.ActiveSnapshot", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Audio", "Stop", "Audio.Stop", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Audio", "Pause", "Audio.Pause", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Audio", "Resume", "Audio.Resume", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Audio", "Seek", "Audio.Seek", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Audio", "SetVolume", "Audio.SetVolume", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Audio", "SetPitch", "Audio.SetPitch", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Audio", "SetLoop", "Audio.SetLoop", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Audio", "IsPlaying", "Audio.IsPlaying", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Audio", "SetBusVolume", "Audio.SetBusVolume", ScriptApiCatalogLuaReturnKind::SingleOutput, "applied" },
    { "Audio", "ClearBusVolume", "Audio.ClearBusVolume", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Audio", "TransitionToSnapshot", "Audio.TransitionToSnapshot", ScriptApiCatalogLuaReturnKind::SingleOutput, "started" },
    { "Audio", "ConfigureOcclusion", "Audio.ConfigureOcclusion", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Audio", "OcclusionEnabled", "Audio.OcclusionEnabled", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Audio", "GetPosition", "Audio.GetPosition", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Audio", "AddMarker", "Audio.AddMarker", ScriptApiCatalogLuaReturnKind::SingleOutput, "added" },
    { "MeshRenderer", "SetMesh", "MeshRenderer.SetMesh", ScriptApiCatalogLuaReturnKind::SingleOutput, "assigned" },
    { "MeshRenderer", "SetMaterial", "MeshRenderer.SetMaterial", ScriptApiCatalogLuaReturnKind::SingleOutput, "assigned" },
    { "MeshRenderer", "SetMaterialSlot", "MeshRenderer.SetMaterialSlot", ScriptApiCatalogLuaReturnKind::SingleOutput, "assigned" },
    { "MeshRenderer", "GetMaterialSlot", "MeshRenderer.GetMaterialSlot", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "MeshRenderer", "ClearMaterialSlot", "MeshRenderer.ClearMaterialSlot", ScriptApiCatalogLuaReturnKind::SingleOutput, "cleared" },
    { "MeshRenderer", "SetMaterialInstance", "MeshRenderer.SetMaterialInstance", ScriptApiCatalogLuaReturnKind::SingleOutput, "assigned" },
    { "MeshRenderer", "ClearMaterialInstance", "MeshRenderer.ClearMaterialInstance", ScriptApiCatalogLuaReturnKind::SingleOutput, "cleared" },
    { "MaterialInstance", "Create", "MaterialInstance.Create", ScriptApiCatalogLuaReturnKind::SingleOutput, "instance" },
    { "MaterialInstance", "Release", "MaterialInstance.Release", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "MaterialInstance", "Exists", "MaterialInstance.Exists", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "MaterialInstance", "Parent", "MaterialInstance.Parent", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "MaterialInstance", "SetParameterScalar", "MaterialInstance.SetParameterScalar", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "MaterialInstance", "SetParameterBool", "MaterialInstance.SetParameterBool", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "MaterialInstance", "ClearParameter", "MaterialInstance.ClearParameter", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "PostProcess", "SetProfile", "PostProcess.SetProfile", ScriptApiCatalogLuaReturnKind::SingleOutput, "assigned" },
    { "PostProcess", "ClearProfile", "PostProcess.ClearProfile", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "PostProcess", "ActiveProfile", "PostProcess.ActiveProfile", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Particles", "Create", "Particles.Create", ScriptApiCatalogLuaReturnKind::SingleOutput, "instance" },
    { "Particles", "Release", "Particles.Release", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Particles", "Exists", "Particles.Exists", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Particles", "Play", "Particles.Play", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Particles", "Stop", "Particles.Stop", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Particles", "IsPlaying", "Particles.IsPlaying", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Particles", "SetSeed", "Particles.SetSeed", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Particles", "SetParameterScalar", "Particles.SetParameterScalar", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Particles", "ClearParameter", "Particles.ClearParameter", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Particles", "Emit", "Particles.Emit", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Particles", "LiveCount", "Particles.LiveCount", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Renderer", "IsVisible", "Renderer.IsVisible", ScriptApiCatalogLuaReturnKind::SingleOutput, "visible" },
    { "Renderer", "GetBounds", "Renderer.GetBounds", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Renderer", "TestFrustum", "Renderer.TestFrustum", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Renderer", "HasFrame", "Renderer.HasFrame", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Renderer", "WorldToScreen", "Renderer.WorldToScreen", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Renderer", "ScreenPointToRay", "Renderer.ScreenPointToRay", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Renderer", "ScreenToWorld", "Renderer.ScreenToWorld", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Renderer", "CaptureScreen", "Renderer.CaptureScreen", ScriptApiCatalogLuaReturnKind::SingleOutput, "capture" },
    { "Renderer", "CaptureStatus", "Renderer.CaptureStatus", ScriptApiCatalogLuaReturnKind::Default, "" },
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
    { "Scene", "Load", "Scene.Load", ScriptApiCatalogLuaReturnKind::SingleOutput, "id" },
    { "Scene", "Unload", "Scene.Unload", ScriptApiCatalogLuaReturnKind::SingleOutput, "unloaded" },
    { "Scene", "SetActive", "Scene.SetActive", ScriptApiCatalogLuaReturnKind::SingleOutput, "set" },
    { "Scene", "GetActive", "Scene.GetActive", ScriptApiCatalogLuaReturnKind::SingleOutput, "id" },
    { "Scene", "Find", "Scene.Find", ScriptApiCatalogLuaReturnKind::SingleOutput, "id" },
    { "Scene", "LoadProgress", "Scene.LoadProgress", ScriptApiCatalogLuaReturnKind::SingleOutput, "progress" },
    { "Time", "delta", "Time.Delta", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Transform", "GetPosition", "Transform.GetPosition", ScriptApiCatalogLuaReturnKind::GuardedTable, "found" },
    { "Transform", "SetPosition", "Transform.SetPosition", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Transform", "Translate", "Transform.Translate", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Physics", "Raycast", "Physics.Raycast", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Physics", "AddForce", "Physics.AddForce", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Physics", "AddImpulse", "Physics.AddImpulse", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Physics", "SetVelocity", "Physics.SetVelocity", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Physics", "GetVelocity", "Physics.GetVelocity", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Physics", "SetAngularVelocity", "Physics.SetAngularVelocity", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Physics", "GetAngularVelocity", "Physics.GetAngularVelocity", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Physics", "MoveKinematic", "Physics.MoveKinematic", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Physics", "Sleep", "Physics.Sleep", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Physics", "Wake", "Physics.Wake", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Physics", "IsSleeping", "Physics.IsSleeping", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Physics", "SphereCast", "Physics.SphereCast", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Physics", "BoxCast", "Physics.BoxCast", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Physics", "CapsuleCast", "Physics.CapsuleCast", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Physics", "OverlapSphere", "Physics.OverlapSphere", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Physics", "OverlapBox", "Physics.OverlapBox", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Physics", "OverlapCapsule", "Physics.OverlapCapsule", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Physics", "ClosestPoint", "Physics.ClosestPoint", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Physics", "LayerBit", "Physics.LayerBit", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Physics", "CharacterMove", "Physics.CharacterMove", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Physics", "CharacterJump", "Physics.CharacterJump", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Physics", "CharacterVelocity", "Physics.CharacterVelocity", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Physics", "CharacterIsGrounded", "Physics.CharacterIsGrounded", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Physics", "CharacterGroundNormal", "Physics.CharacterGroundNormal", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Physics", "CharacterGroundVelocity", "Physics.CharacterGroundVelocity", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Physics", "SetDebugDrawEnabled", "Physics.SetDebugDrawEnabled", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Physics", "IsDebugDrawEnabled", "Physics.IsDebugDrawEnabled", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "IsPressed", "Input.IsPressed", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "WasPressed", "Input.WasPressed", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "WasReleased", "Input.WasReleased", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "Value", "Input.Value", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "Vector2", "Input.Vector2", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Input", "Vector3", "Input.Vector3", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Input", "AddMappingContext", "Input.AddMappingContext", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "RemoveMappingContext", "Input.RemoveMappingContext", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "Rebind", "Input.Rebind", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Input", "SaveRebindProfile", "Input.SaveRebindProfile", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Input", "LoadRebindProfile", "Input.LoadRebindProfile", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Input", "ActionBool", "Input.ActionBool", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "ActionFloat", "Input.ActionFloat", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "Action2D", "Input.Action2D", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Input", "Pressed", "Input.Pressed", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "Released", "Input.Released", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "Held", "Input.Held", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "PriorityGameplay", "Input.PriorityGameplay", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "PriorityUI", "Input.PriorityUI", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "PriorityConsole", "Input.PriorityConsole", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "PriorityDebugOverlay", "Input.PriorityDebugOverlay", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "HasFocus", "Input.HasFocus", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "IsGamepadConnected", "Input.IsGamepadConnected", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "HasHaptics", "Input.HasHaptics", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Input", "SetVibration", "Input.SetVibration", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "BindHapticsUser", "Input.BindHapticsUser", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "SetUserVibration", "Input.SetUserVibration", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Input", "StopVibration", "Input.StopVibration", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Timeline", "Create", "Timeline.Create", ScriptApiCatalogLuaReturnKind::SingleOutput, "instance" },
    { "Timeline", "Release", "Timeline.Release", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Timeline", "Play", "Timeline.Play", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Timeline", "Pause", "Timeline.Pause", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Timeline", "Seek", "Timeline.Seek", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Timeline", "Skip", "Timeline.Skip", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Timeline", "Bind", "Timeline.Bind", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Timeline", "IsPlaying", "Timeline.IsPlaying", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Timeline", "Time", "Timeline.Time", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Pointer", "Position", "Pointer.Position", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Pointer", "Delta", "Pointer.Delta", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Pointer", "Button", "Pointer.Button", ScriptApiCatalogLuaReturnKind::Default, "" },
    { "Pointer", "Scroll", "Pointer.Scroll", ScriptApiCatalogLuaReturnKind::SingleOutput, "delta" },
    { "Pointer", "Ray", "Pointer.Ray", ScriptApiCatalogLuaReturnKind::OutputsTable, "" },
    { "Task", "WaitSeconds", "Task.WaitSeconds", ScriptApiCatalogLuaReturnKind::SingleOutput, "task" },
    { "Task", "WaitFixedSteps", "Task.WaitFixedSteps", ScriptApiCatalogLuaReturnKind::SingleOutput, "task" },
    { "Task", "WaitEvent", "Task.WaitEvent", ScriptApiCatalogLuaReturnKind::SingleOutput, "task" },
    { "Task", "WaitAsset", "Task.WaitAsset", ScriptApiCatalogLuaReturnKind::SingleOutput, "task" },
    { "Task", "WaitScene", "Task.WaitScene", ScriptApiCatalogLuaReturnKind::SingleOutput, "task" },
    { "Task", "IsRunning", "Task.IsRunning", ScriptApiCatalogLuaReturnKind::SingleOutput, "running" },
    { "Task", "Cancel", "Task.Cancel", ScriptApiCatalogLuaReturnKind::SingleOutput, "cancelled" },
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
            .description = function.signature.description,
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
