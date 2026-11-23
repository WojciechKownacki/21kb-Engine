#include "TestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHistory.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptApiNameRegistry.hpp"
#include "engine/script/ScriptValue.hpp"
#include "engine/script/ScriptRuntime.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "scene/asset/io/SceneAssetReader.hpp"
#include "scene/asset/io/SceneAssetWriter.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "scene/asset/io/SceneAssetFormat.hpp"
#include "scene/asset/io/components/SceneAssetBehaviourComponentCodec.hpp"
#include "scene/prefab/io/ScenePrefabAssetReader.hpp"
#include "scene/prefab/io/ScenePrefabAssetWriter.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using kb::scene::BehaviourComponent;

std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "kb_multi_behaviour_tests";
}

[[nodiscard]] BehaviourComponent Lua(std::uint64_t asset, kb::scene::BehaviourTickGroup group = kb::scene::BehaviourTickGroup::Gameplay,
    std::int32_t order = 0, bool enabled = true) {
    return BehaviourComponent{ .behaviourAssetId = asset, .backend = kb::scene::BehaviourBackend::Lua, .enabled = enabled, .tickGroup = group,
        .executionOrder = order };
}

[[nodiscard]] kb::scene::SceneObject MakeObject(kb::scene::Scene& scene, const std::string& name, std::vector<BehaviourComponent> scripts) {
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = name });
    for (const BehaviourComponent& script : scripts) {
        kb::tests::Require(scene.Components().Behaviours().Add(object.Entity(), script), "A script was not added to an object");
    }
    return object;
}

[[nodiscard]] std::vector<std::uint64_t> Assets(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    std::vector<std::uint64_t> assets;
    const auto behaviours = scene.Components().Behaviours();
    for (std::size_t index = 0U; index < behaviours.Count(entity); ++index) {
        assets.push_back(behaviours.TryGetAt(entity, index)->behaviourAssetId);
    }
    return assets;
}

// An object holds up to eight scripts in the order they were added; the first is its Behaviour component.
void RunStoreTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = MakeObject(scene, "Twin", { Lua(1U), Lua(2U), Lua(3U) });
    const kb::scene::SceneEntity entity = object.Entity();
    auto behaviours = scene.Components().Behaviours();
    kb::tests::Require(behaviours.Count(entity) == 3U && Assets(scene, entity) == std::vector<std::uint64_t>{ 1U, 2U, 3U },
        "An object did not keep its scripts in the order they were added");
    kb::tests::Require(behaviours.TryGet(entity)->behaviourAssetId == 1U && behaviours.Additional(entity).size() == 2U,
        "The first script must be the Behaviour component and the rest the additional ones");
    kb::tests::Require(!behaviours.Add(entity, Lua(2U)) && behaviours.Count(entity) == 3U, "A script an object runs already was added again");
    kb::tests::Require(behaviours.Find(entity, 3U, kb::scene::BehaviourBackend::Lua) != nullptr &&
            behaviours.Find(entity, 3U, kb::scene::BehaviourBackend::Native) == nullptr && behaviours.Find(entity, 9U, kb::scene::BehaviourBackend::Lua) == nullptr,
        "Find did not tell the scripts of an object apart");

    // Every script of every object is visited, an object's own in the order they were added.
    std::vector<std::uint64_t> visited;
    scene.Components().Behaviours().ForEach([](kb::scene::SceneEntity, const BehaviourComponent& behaviour, void* context) {
        static_cast<std::vector<std::uint64_t>*>(context)->push_back(behaviour.behaviourAssetId);
    }, &visited);
    kb::tests::Require(visited == std::vector<std::uint64_t>{ 1U, 2U, 3U }, "ForEach did not visit every script of the object in order");

    // Stamp: any change to the list moves it, so a runtime that tracks scripts looks again.
    const std::uint64_t stamp = behaviours.ChangeStamp();
    kb::tests::Require(behaviours.SetAt(entity, 1U, Lua(2U, kb::scene::BehaviourTickGroup::Physics, 5, false)) && behaviours.ChangeStamp() != stamp &&
            !behaviours.TryGetAt(entity, 1U)->enabled && behaviours.TryGetAt(entity, 1U)->executionOrder == 5,
        "SetAt did not change the second script");

    // Taking the first one away moves the next up; the others stay in order.
    kb::tests::Require(behaviours.RemoveAt(entity, 0U) && Assets(scene, entity) == std::vector<std::uint64_t>{ 2U, 3U } &&
            behaviours.TryGet(entity)->behaviourAssetId == 2U,
        "Removing the first script did not move the next one up");
    kb::tests::Require(behaviours.RemoveAt(entity, 1U) && Assets(scene, entity) == std::vector<std::uint64_t>{ 2U } && behaviours.Additional(entity).empty(),
        "Removing the last script left it behind");
    kb::tests::Require(!behaviours.RemoveAt(entity, 1U) && behaviours.RemoveAt(entity, 0U) && !behaviours.Has(entity) && behaviours.Count(entity) == 0U,
        "Removing the only script left the object with a Behaviour");

    // The limit, and a list that repeats a script or passes it.
    const kb::scene::SceneObject crowded = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Crowded" });
    for (std::uint64_t asset = 1U; asset <= kb::scene::kMaxBehavioursPerEntity; ++asset) {
        kb::tests::Require(scene.Components().Behaviours().Add(crowded.Entity(), Lua(asset)), "An object refused a script below the limit");
    }
    kb::tests::Require(!scene.Components().Behaviours().Add(crowded.Entity(), Lua(99U)) &&
            scene.Components().Behaviours().Count(crowded.Entity()) == kb::scene::kMaxBehavioursPerEntity,
        "An object took more scripts than the limit");
    scene.Components().Behaviours().SetAdditional(crowded.Entity(), std::vector<BehaviourComponent>{ Lua(1U), Lua(20U), Lua(20U), Lua(21U) });
    kb::tests::Require(Assets(scene, crowded.Entity()) == std::vector<std::uint64_t>{ 1U, 20U, 21U },
        "SetAdditional kept a repeated script");

    // Set changes the first script and leaves the others; Remove takes them all.
    scene.Components().Behaviours().Set(crowded.Entity(), Lua(30U));
    kb::tests::Require(Assets(scene, crowded.Entity()) == std::vector<std::uint64_t>{ 30U, 20U, 21U }, "Set did not replace only the first script");
    scene.Components().Behaviours().Remove(crowded.Entity());
    kb::tests::Require(scene.Components().Behaviours().Count(crowded.Entity()) == 0U && scene.Components().Behaviours().Additional(crowded.Entity()).empty(),
        "Remove left scripts on the object");

    // An object that is destroyed takes its scripts with it, and an entity created later does not inherit them.
    const kb::scene::SceneObject doomed = MakeObject(scene, "Doomed", { Lua(7U), Lua(8U) });
    const kb::scene::SceneEntity doomedEntity = doomed.Entity();
    scene.Entities().Destroy(doomedEntity);
    kb::tests::Require(scene.Components().Behaviours().Count(doomedEntity) == 0U, "A destroyed object kept its scripts");
    std::size_t left = 0U;
    scene.Components().Behaviours().ForEach([](kb::scene::SceneEntity, const BehaviourComponent&, void* context) { ++*static_cast<std::size_t*>(context); }, &left);
    kb::tests::Require(left == 0U, "A script of a destroyed object is still visited");
}

// The scene file keeps every script of a node, and a scene saved before it did loads with one script per node.
void RunSceneDocumentTest() {
    std::filesystem::create_directories(TestRoot());
    const std::filesystem::path path = TestRoot() / "Twin.21kbscene";
    kb::scene::Scene scene;
    static_cast<void>(MakeObject(scene, "Solo", { Lua(5U) }));
    static_cast<void>(MakeObject(scene, "Trio", { Lua(1U), Lua(2U, kb::scene::BehaviourTickGroup::Physics, -3), Lua(3U, kb::scene::BehaviourTickGroup::Gameplay, 0, false) }));
    kb::tests::Require(kb::scene::SceneDocumentService::Save(scene, path, "Twin"), "The scene with several scripts did not save");

    const kb::scene::SceneDocumentLoadResult loaded = kb::scene::SceneDocumentService::Load(path);
    kb::tests::Require(loaded.succeeded && loaded.document.fileVersion >= kb::scene::SceneDocument::AdditionalBehavioursFileVersion,
        "The scene with several scripts did not load as the current version");
    for (const kb::scene::ScenePrefabNodeDesc& node : loaded.document.worldPrefab.Nodes()) {
        kb::tests::Require(node.components.behaviour.has_value(), "A node lost its script");
        if (node.name == "Solo") {
            kb::tests::Require(node.components.additionalBehaviours.empty(), "A node with one script gained more");
        } else {
            kb::tests::Require(node.components.additionalBehaviours.size() == 2U &&
                    kb::scene::SameBehaviourComponent(node.components.additionalBehaviours[0], Lua(2U, kb::scene::BehaviourTickGroup::Physics, -3)) &&
                    kb::scene::SameBehaviourComponent(node.components.additionalBehaviours[1], Lua(3U, kb::scene::BehaviourTickGroup::Gameplay, 0, false)),
                "A node did not load the further scripts it was saved with");
        }
    }
    kb::scene::Scene target;
    kb::tests::Require(kb::scene::SceneDocumentService::LoadIntoScene(target, loaded.document), "The scene with several scripts did not load into a scene");
    std::size_t found = 0U;
    for (const kb::scene::SceneEntity entity : target.Hierarchy().RootEntities()) {
        const std::string name = target.Entities().Name(entity);
        if (name == "Trio") {
            kb::tests::Require(Assets(target, entity) == std::vector<std::uint64_t>{ 1U, 2U, 3U }, "A loaded object lost the order of its scripts");
            ++found;
        } else if (name == "Solo") {
            kb::tests::Require(Assets(target, entity) == std::vector<std::uint64_t>{ 5U }, "A loaded object with one script changed");
            ++found;
        }
    }
    kb::tests::Require(found == 2U, "A loaded scene lost an object");
    std::error_code ignored;
    std::filesystem::remove_all(TestRoot(), ignored);
}

// A node with one script is written as it was before nodes ran several, and the block of further scripts reads back.
void RunLegacyReadTest() {
    // A scene without a node that runs several scripts: the same bytes as before, apart from the version number.
    kb::scene::Scene single;
    static_cast<void>(MakeObject(single, "Solo", { Lua(5U) }));
    kb::scene::SceneDocument document = kb::scene::SceneDocumentService::Capture(single, "Solo");
    const std::vector<std::uint8_t> bytes = kb::scene::SceneAssetWriter::Encode(document);
    kb::tests::Require(!bytes.empty(), "The scene with one script did not encode");
    std::vector<std::uint8_t> older = bytes;
    for (std::size_t byte = 0U; byte < sizeof(std::uint32_t); ++byte) {
        older[kb::scene::SceneAssetFormat::Magic.size() + byte] = static_cast<std::uint8_t>((kb::scene::SceneDocument::StableObjectIdsFileVersion >> (8U * byte)) & 0xFFU);
    }
    const kb::scene::SceneDocumentLoadResult legacy = kb::scene::SceneAssetReader::Read(older);
    kb::tests::Require(legacy.succeeded && legacy.document.fileVersion == kb::scene::SceneDocument::StableObjectIdsFileVersion &&
            legacy.document.worldPrefab.NodeCount() == 1U && legacy.document.worldPrefab.Nodes()[0].components.behaviour.has_value() &&
            legacy.document.worldPrefab.Nodes()[0].components.additionalBehaviours.empty(),
        "A scene saved before several scripts per node did not load unchanged");

    // A node that runs several carries the block; a version before it cannot have it.
    kb::scene::Scene twin;
    static_cast<void>(MakeObject(twin, "Twin", { Lua(5U), Lua(6U) }));
    std::vector<std::uint8_t> several = kb::scene::SceneAssetWriter::Encode(kb::scene::SceneDocumentService::Capture(twin, "Twin"));
    kb::tests::Require(!several.empty() && several.size() > bytes.size() - 1U, "The scene with two scripts did not encode");
    for (std::size_t byte = 0U; byte < sizeof(std::uint32_t); ++byte) {
        several[kb::scene::SceneAssetFormat::Magic.size() + byte] = static_cast<std::uint8_t>((kb::scene::SceneDocument::StableObjectIdsFileVersion >> (8U * byte)) & 0xFFU);
    }
    kb::tests::Require(!kb::scene::SceneAssetReader::Read(several).succeeded, "A scene of a version before several scripts per node was accepted with them");

    std::vector<std::uint8_t> block;
    kb::scene::SceneAssetBinaryIO::WriteUInt32(block, 1U);
    kb::scene::SceneAssetBinaryIO::WriteUInt64(block, 78U);
    kb::scene::SceneAssetBinaryIO::WriteUInt32(block, static_cast<std::uint32_t>(kb::scene::BehaviourBackend::Lua));
    kb::scene::SceneAssetBinaryIO::WriteUInt8(block, 1U);
    kb::scene::SceneAssetBinaryIO::WriteUInt32(block, static_cast<std::uint32_t>(kb::scene::BehaviourTickGroup::Gameplay));
    kb::scene::SceneAssetBinaryIO::WriteInt32(block, 2);
    kb::scene::SceneAssetBinaryIO::ByteReader reader{ block };
    std::vector<BehaviourComponent> additional;
    kb::tests::Require(kb::scene::SceneAssetBehaviourComponentCodec::ReadAdditional(reader, additional) && reader.Exhausted() && additional.size() == 1U &&
            additional[0].behaviourAssetId == 78U && additional[0].executionOrder == 2,
        "The block of further scripts did not read back");
    std::vector<std::uint8_t> tooMany;
    kb::scene::SceneAssetBinaryIO::WriteUInt32(tooMany, static_cast<std::uint32_t>(kb::scene::kMaxBehavioursPerEntity));
    kb::scene::SceneAssetBinaryIO::ByteReader damaged{ tooMany };
    kb::tests::Require(!kb::scene::SceneAssetBehaviourComponentCodec::ReadAdditional(damaged, additional), "A node with more scripts than an object can run was accepted");
}

// A prefab keeps the scripts of the object it was captured from, in a file as well, and each instance gets all of them.
void RunPrefabTest() {
    kb::scene::Scene source;
    const kb::scene::SceneObject root = MakeObject(source, "Root", { Lua(1U), Lua(2U), Lua(3U) });
    const kb::scene::ScenePrefab prefab = source.Prefabs().Capture(root);
    kb::tests::Require(prefab.NodeCount() == 1U && prefab.Nodes()[0].components.additionalBehaviours.size() == 2U, "Capture did not carry the further scripts");

    kb::scene::Scene target;
    const kb::scene::ScenePrefabInstance one = target.Prefabs().Instantiate(prefab);
    kb::tests::Require(Assets(target, one.ObjectAt(0).Entity()) == std::vector<std::uint64_t>{ 1U, 2U, 3U }, "Instantiate did not give the object all its scripts");
    for (const kb::scene::ScenePrefabInstance& instance : target.Prefabs().InstantiateMany(prefab, 3U)) {
        kb::tests::Require(Assets(target, instance.ObjectAt(0).Entity()) == std::vector<std::uint64_t>{ 1U, 2U, 3U }, "InstantiateMany lost a script");
    }
    kb::scene::Scene batch;
    static_cast<void>(batch.Prefabs().InstantiateBatch(prefab, 4U));
    std::size_t objects = 0U;
    batch.Components().Behaviours().ForEach([](kb::scene::SceneEntity, const BehaviourComponent&, void* context) { ++*static_cast<std::size_t*>(context); }, &objects);
    kb::tests::Require(objects == 12U, "InstantiateBatch did not give every instance all three scripts");

    std::filesystem::create_directories(TestRoot());
    const std::filesystem::path path = TestRoot() / "Twin.21kbprefab";
    kb::tests::Require(kb::scene::ScenePrefabAssetWriter::Write(path, "Twin", prefab), "The prefab with several scripts did not write");
    kb::scene::ScenePrefabAssetReadResult result;
    kb::tests::Require(kb::scene::ScenePrefabAssetReader::Read(path, result) && result.prefab.NodeCount() == 1U &&
            result.prefab.Nodes()[0].components.additionalBehaviours.size() == 2U &&
            result.prefab.Nodes()[0].components.additionalBehaviours[1].behaviourAssetId == 3U,
        "The prefab file did not keep the further scripts");
    std::error_code ignored;
    std::filesystem::remove_all(TestRoot(), ignored);
}

// Undo and redo bring back the scripts an edit added or took away.
void RunHistoryTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = MakeObject(scene, "Twin", { Lua(1U) });
    kb::tests::Require(scene.History().Record("Add script"), "The history did not open a command");
    kb::tests::Require(scene.Components().Behaviours().Add(object.Entity(), Lua(2U)), "The second script was not added");
    scene.History().Commit();
    kb::tests::Require(Assets(scene, object.Entity()) == std::vector<std::uint64_t>{ 1U, 2U }, "The second script is missing");
    kb::tests::Require(scene.History().Undo() && Assets(scene, object.Entity()) == std::vector<std::uint64_t>{ 1U }, "Undo did not take the added script off");
    kb::tests::Require(scene.History().Redo() && Assets(scene, object.Entity()) == std::vector<std::uint64_t>{ 1U, 2U }, "Redo did not bring the script back");

    kb::tests::Require(scene.History().Record("Remove first script"), "The history did not open a second command");
    kb::tests::Require(scene.Components().Behaviours().RemoveAt(object.Entity(), 0U), "The first script was not removed");
    scene.History().Commit();
    kb::tests::Require(Assets(scene, object.Entity()) == std::vector<std::uint64_t>{ 2U }, "The second script did not move up");
    kb::tests::Require(scene.History().Undo() && Assets(scene, object.Entity()) == std::vector<std::uint64_t>{ 1U, 2U }, "Undo did not bring the first script back in front");
}

// Two scripts on one object both run: lifecycle, ticks and events, each with a self and a variable set of its own.
void RunRuntimeTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "The two-script host did not start");
    constexpr kb::assets::AssetId kFirst{ 0x7A01U };
    constexpr kb::assets::AssetId kSecond{ 0x7A02U };
    constexpr kb::assets::AssetId kThird{ 0x7A03U };
    const kb::scene::SceneObject twin = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Twin" });
    const kb::scene::SceneObject other = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Other" });

    // The first two scripts expose `speed`, with a default of their own each.
    const std::vector<kb::script::ScriptApiPin> pins{ kb::script::ScriptApiPin{ .name = "speed", .type = kb::script::ScriptValueType::Float } };
    const std::vector<kb::script::ScriptValue> firstDefaults{ kb::script::ScriptValue{ 1.0F } };
    const std::vector<kb::script::ScriptValue> secondDefaults{ kb::script::ScriptValue{ 2.0F } };
    const std::vector<std::uint8_t> hasDefaults{ 1U };
    host.LuaRuntime().SetScriptExposedVariables(kFirst, pins, firstDefaults, hasDefaults);
    host.LuaRuntime().SetScriptExposedVariables(kSecond, pins, secondDefaults, hasDefaults);
    kb::tests::Require(scene.Components().Behaviours().Add(twin.Entity(), Lua(kFirst.value)) &&
            scene.Components().Behaviours().Add(twin.Entity(), Lua(kSecond.value, kb::scene::BehaviourTickGroup::Gameplay, 7)) &&
            scene.Components().Behaviours().Add(other.Entity(), Lua(kThird.value)),
        "The scripts were not added");
    kb::tests::Require(host.LuaRuntime().LoadScript(kFirst, R"(
local ticks = 0
function Created(self, dt) SetShared("createdA", true) end
function Tick(self, dt)
    ticks = ticks + 1
    SetShared("ticksA", ticks)
    SetShared("speedA", self:GetVariable("speed"))
    SetShared("orderA", self:GetProperty("Behaviour", "executionOrder"))
end
function Ping(self, event) SetShared("pingA", (GetShared("pingA") or 0) + 1) end
)").succeeded, "The first script did not load");
    kb::tests::Require(host.LuaRuntime().LoadScript(kSecond, R"(
local ticks = 100
function Created(self, dt) SetShared("createdB", true) end
function Tick(self, dt)
    ticks = ticks + 1
    SetShared("ticksB", ticks)
    SetShared("speedB", self:GetVariable("speed"))
    SetShared("orderB", self:GetProperty("Behaviour", "executionOrder"))
    if ticks == 103 then self:SetProperty("Behaviour", "enabled", false) end
end
function Ping(self, event) SetShared("pingB", (GetShared("pingB") or 0) + 1) end
)").succeeded, "The second script did not load");
    kb::tests::Require(host.LuaRuntime().LoadScript(kThird, R"(
function Created(self, dt) SetShared("createdC", true) end
function Ping(self, event) SetShared("pingC", (GetShared("pingC") or 0) + 1) end
)").succeeded, "The third script did not load");
    kb::tests::Require(host.InstallSceneSystem(), "The two-script scene system did not install");
    static_cast<void>(scene.Runtime().Update(0.016F));
    static_cast<void>(scene.Runtime().Update(0.016F));

    const auto number = [&host](const std::string& key) {
        const auto value = host.SharedState().Get(key);
        return value.has_value() ? value->AsFloat() : -1.0F;
    };
    const auto has = [&host](const std::string& key) { return host.SharedState().Get(key).has_value(); };
    kb::tests::Require(has("createdA") && has("createdB") && has("createdC"), "Every script must be created, both of an object's too");
    kb::tests::Require(number("ticksA") == 2.0F && number("ticksB") == 102.0F, "Both scripts of the object must tick, each with the state of its own");
    kb::tests::Require(number("speedA") == 1.0F && number("speedB") == 2.0F, "Each script must read its own exposed variable");
    // The Behaviour component read through self is the one of the script that asks.
    kb::tests::Require(number("orderA") == 0.0F && number("orderB") == 7.0F, "self:GetProperty(\"Behaviour\") must be the Behaviour of the script that asks");

    // An event for the object reaches both of its scripts and nobody else; one for everybody reaches every script that handles it.
    kb::script::ScriptEvent ping;
    ping.name = "Ping";
    ping.target = twin.Entity();
    const kb::script::ScriptRuntimeExecutionResult targeted = host.Runtime().DispatchEvent(scene, ping, 0.016F);
    kb::tests::Require(targeted.Succeeded() && targeted.executedBehaviours == 2U && number("pingA") == 1.0F && number("pingB") == 1.0F && !has("pingC"),
        "An event for one object must reach both of its scripts and nobody else");
    ping.target = kb::scene::SceneEntity{};
    const kb::script::ScriptRuntimeExecutionResult broadcast = host.Runtime().DispatchEvent(scene, ping, 0.016F);
    kb::tests::Require(broadcast.Succeeded() && broadcast.executedBehaviours == 3U && number("pingA") == 2.0F && number("pingB") == 2.0F && number("pingC") == 1.0F,
        "An event for everybody must reach every script that handles it");

    // A script that switches itself off through self stops alone: its place on the object is its own.
    static_cast<void>(scene.Runtime().Update(0.016F));
    static_cast<void>(scene.Runtime().Update(0.016F));
    kb::tests::Require(number("ticksB") == 103.0F && number("ticksA") == 4.0F, "A script that switched itself off must stop, and the other must go on");
    kb::tests::Require(scene.Components().Behaviours().TryGetAt(twin.Entity(), 0U)->enabled && !scene.Components().Behaviours().TryGetAt(twin.Entity(), 1U)->enabled,
        "self:SetProperty(\"Behaviour\", \"enabled\") must switch off the script that asks and no other");

    // Taking one script off stops it and only it.
    kb::tests::Require(scene.Components().Behaviours().RemoveAt(twin.Entity(), 0U), "The first script was not removed");
    static_cast<void>(scene.Runtime().Update(0.016F));
    kb::tests::Require(number("ticksA") == 4.0F && number("ticksB") == 103.0F, "A script that was taken off kept running");
}

// The scripts of one object run by tick group and execution order, and the others in the order they were added.
void RunOrderTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "The order host did not start");
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Ordered" });
    // Added as C, A, B; A is in an earlier tick group, B and C tie and run in the order they were added.
    constexpr kb::assets::AssetId kA{ 0x7B01U };
    constexpr kb::assets::AssetId kB{ 0x7B02U };
    constexpr kb::assets::AssetId kC{ 0x7B03U };
    kb::tests::Require(scene.Components().Behaviours().Add(object.Entity(), Lua(kC.value)) &&
            scene.Components().Behaviours().Add(object.Entity(), Lua(kA.value, kb::scene::BehaviourTickGroup::Input)) &&
            scene.Components().Behaviours().Add(object.Entity(), Lua(kB.value)),
        "The ordered scripts were not added");
    for (const auto& [asset, letter] : { std::pair{ kA, "A" }, std::pair{ kB, "B" }, std::pair{ kC, "C" } }) {
        kb::tests::Require(host.LuaRuntime().LoadScript(asset, std::string{ "function Tick(self, dt) SetShared(\"order\", (GetShared(\"order\") or \"\") .. \"" } + letter + "\") end").succeeded,
            "An ordered script did not load");
    }
    kb::tests::Require(host.InstallSceneSystem(), "The order scene system did not install");
    static_cast<void>(scene.Runtime().Update(0.016F));
    const auto order = host.SharedState().Get("order");
    kb::tests::Require(order.has_value() && order->AsString() == "ACB", "The scripts of one object must run by tick group, then in the order they were added");
}

} // namespace

namespace kb::tests {

void RunSceneMultiBehaviourTests() {
    RunStoreTest();
    RunSceneDocumentTest();
    RunLegacyReadTest();
    RunPrefabTest();
    RunHistoryTest();
    RunRuntimeTest();
    RunOrderTest();
}

} // namespace kb::tests
