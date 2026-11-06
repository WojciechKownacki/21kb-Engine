#include "TestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHistory.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/script/ScriptValue.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "scene/asset/io/components/SceneAssetBehaviourComponentCodec.hpp"
#include "scene/prefab/io/ScenePrefabAssetReader.hpp"
#include "scene/prefab/io/ScenePrefabAssetWriter.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using kb::script::ScriptValue;
using kb::script::ScriptValueType;

std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "kb_behaviour_variable_tests";
}

[[nodiscard]] std::vector<kb::scene::BehaviourVariableOverride> SampleOverrides() {
    return {
        { "speed", ScriptValue{ 12.5F } },
        { "lives", ScriptValue{ 3 } },
        { "armed", ScriptValue{ true } },
        { "label", ScriptValue{ std::string{ "two words, a = sign and\na new line" } } },
        { "empty", ScriptValue{ std::string{} } },
        { "seed", ScriptValue{ std::int64_t{ -9000000000LL } } },
        { "precise", ScriptValue{ 0.1 } },
        { "target", ScriptValue{ std::uint64_t{ 42U }, ScriptValueType::Entity } },
    };
}

// The overrides of every behaviour in a scene, by object name.
[[nodiscard]] std::vector<std::pair<std::string, std::vector<kb::scene::BehaviourVariableOverride>>> CollectOverrides(kb::scene::Scene& scene) {
    struct Collected {
        kb::scene::Scene* scene = nullptr;
        std::vector<std::pair<std::string, std::vector<kb::scene::BehaviourVariableOverride>>> entries;
    } collected{ &scene, {} };
    scene.Components().Behaviours().ForEach([](kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent&, void* context) {
        auto& result = *static_cast<Collected*>(context);
        const std::span<const kb::scene::BehaviourVariableOverride> overrides = result.scene->Entities().BehaviourVariableOverrides(entity);
        result.entries.emplace_back(result.scene->Entities().Name(entity), std::vector<kb::scene::BehaviourVariableOverride>{ overrides.begin(), overrides.end() });
    }, &collected);
    return std::move(collected.entries);
}

[[nodiscard]] kb::scene::SceneObject MakeBehaviourObject(kb::scene::Scene& scene, const std::string& name,
    std::vector<kb::scene::BehaviourVariableOverride> overrides) {
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = name });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = 77U,
        .backend = kb::scene::BehaviourBackend::Lua,
    });
    scene.Entities().ReplaceBehaviourVariableOverrides(object.Entity(), std::move(overrides));
    return object;
}

void RequireSame(std::span<const kb::scene::BehaviourVariableOverride> actual, const std::vector<kb::scene::BehaviourVariableOverride>& expected,
    const char* message) {
    kb::tests::Require(kb::scene::SameBehaviourVariableOverrides(actual, expected), message);
}

// The typed text form keeps every value type exactly, including strings that look like other things.
void RunValueTextTest() {
    for (const kb::scene::BehaviourVariableOverride& entry : SampleOverrides()) {
        const std::optional<ScriptValue> parsed = kb::script::TryParseTypedValueText(kb::script::FormatTypedValueText(entry.value));
        kb::tests::Require(parsed.has_value() && *parsed == entry.value, "A script value did not survive its typed text form");
    }
    kb::tests::Require(!kb::script::TryParseValueText("12x", ScriptValueType::Int).has_value(), "A number with trailing text was accepted");
    kb::tests::Require(!kb::script::TryParseValueText("", ScriptValueType::Float).has_value(), "An empty number was accepted");
    kb::tests::Require(!kb::script::TryParseValueText("maybe", ScriptValueType::Bool).has_value(), "A bool that is neither true nor false was accepted");
    kb::tests::Require(!kb::script::TryParseTypedValueText("Float").has_value() && !kb::script::TryParseTypedValueText("Colour:1").has_value(),
        "Typed text without a known type was accepted");
}

// A scene file stores the overrides with the behaviour, and reads them back onto a loaded scene.
void RunSceneDocumentRoundTripTest() {
    std::filesystem::create_directories(TestRoot());
    const std::filesystem::path path = TestRoot() / "Variables.21kbscene";
    kb::scene::Scene scene;
    static_cast<void>(MakeBehaviourObject(scene, "Plain", {}));
    static_cast<void>(MakeBehaviourObject(scene, "Tuned", SampleOverrides()));
    kb::tests::Require(kb::scene::SceneDocumentService::Save(scene, path, "Variables"), "The scene with overrides did not save");

    const kb::scene::SceneDocumentLoadResult loaded = kb::scene::SceneDocumentService::Load(path);
    kb::tests::Require(loaded.succeeded && loaded.document.fileVersion == kb::scene::SceneDocument::CurrentFileVersion &&
            kb::scene::SceneDocument::CurrentFileVersion >= kb::scene::SceneDocument::BehaviourVariableOverridesFileVersion,
        "The scene with overrides did not load as the current version");
    const std::span<const kb::scene::ScenePrefabNodeDesc> nodes = loaded.document.worldPrefab.Nodes();
    kb::tests::Require(nodes.size() == 2U, "The scene with overrides lost a node");
    for (const kb::scene::ScenePrefabNodeDesc& node : nodes) {
        kb::tests::Require(node.components.behaviour.has_value(), "A node lost its behaviour");
        RequireSame(node.components.behaviourVariableOverrides, node.name == "Tuned" ? SampleOverrides() : std::vector<kb::scene::BehaviourVariableOverride>{},
            "A node did not load the overrides it was saved with");
    }

    kb::scene::Scene target;
    kb::tests::Require(kb::scene::SceneDocumentService::LoadIntoScene(target, loaded.document), "The scene with overrides did not load into a scene");
    const auto loadedOverrides = CollectOverrides(target);
    kb::tests::Require(loadedOverrides.size() == 2U, "A loaded scene lost a behaviour");
    for (const auto& [name, overrides] : loadedOverrides) {
        RequireSame(overrides, name == "Tuned" ? SampleOverrides() : std::vector<kb::scene::BehaviourVariableOverride>{},
            "A loaded scene did not hold the saved overrides");
    }
    std::error_code ignored;
    std::filesystem::remove_all(TestRoot(), ignored);
}

// A behaviour written before the overrides existed has no override block and reads as having none.
void RunLegacyBehaviourReadTest() {
    std::vector<std::uint8_t> bytes;
    kb::scene::SceneAssetBinaryIO::WriteUInt64(bytes, 77U);
    kb::scene::SceneAssetBinaryIO::WriteUInt32(bytes, static_cast<std::uint32_t>(kb::scene::BehaviourBackend::Lua));
    kb::scene::SceneAssetBinaryIO::WriteUInt8(bytes, 1U);
    kb::scene::SceneAssetBinaryIO::WriteUInt32(bytes, static_cast<std::uint32_t>(kb::scene::BehaviourTickGroup::Physics));
    kb::scene::SceneAssetBinaryIO::WriteInt32(bytes, -4);

    kb::scene::SceneAssetBinaryIO::ByteReader legacy{ bytes };
    kb::scene::BehaviourComponent behaviour;
    std::vector<kb::scene::BehaviourVariableOverride> overrides{ { "stale", ScriptValue{ 1 } } };
    kb::tests::Require(kb::scene::SceneAssetBehaviourComponentCodec::Read(legacy, kb::scene::SceneDocument::BehaviourVariableOverridesFileVersion - 1U, behaviour, overrides) &&
            legacy.Exhausted() && overrides.empty() && behaviour.behaviourAssetId == 77U && behaviour.executionOrder == -4 &&
            behaviour.tickGroup == kb::scene::BehaviourTickGroup::Physics,
        "A behaviour written before overrides existed did not load unchanged");

    kb::scene::SceneAssetBinaryIO::ByteReader current{ bytes };
    kb::tests::Require(!kb::scene::SceneAssetBehaviourComponentCodec::Read(current, kb::scene::SceneDocument::BehaviourVariableOverridesFileVersion, behaviour, overrides),
        "A current behaviour without its override block was accepted");

    // Two overrides of one variable are a damaged file, not a choice between them.
    std::vector<std::uint8_t> twice = bytes;
    kb::scene::SceneAssetBinaryIO::WriteUInt32(twice, 2U);
    for (int index = 0; index < 2; ++index) {
        kb::scene::SceneAssetBinaryIO::WriteString(twice, "speed");
        kb::scene::SceneAssetBinaryIO::WriteString(twice, "Int:" + std::to_string(index));
    }
    kb::scene::SceneAssetBinaryIO::ByteReader duplicate{ twice };
    kb::tests::Require(!kb::scene::SceneAssetBehaviourComponentCodec::Read(duplicate, kb::scene::SceneDocument::CurrentFileVersion, behaviour, overrides),
        "A behaviour that overrides one variable twice was accepted");
}

// A prefab keeps the overrides of the object it was captured from, and each instance gets its own copy, one at a time
// and in bulk.
void RunPrefabCaptureInstantiateTest() {
    kb::scene::Scene source;
    const kb::scene::SceneObject root = MakeBehaviourObject(source, "Root", SampleOverrides());
    const kb::scene::ScenePrefab prefab = source.Prefabs().Capture(root);
    kb::tests::Require(prefab.NodeCount() == 1U, "The prefab with overrides lost its node");
    RequireSame(prefab.Nodes()[0].components.behaviourVariableOverrides, SampleOverrides(), "Capture did not carry the overrides");

    kb::scene::Scene target;
    const kb::scene::ScenePrefabInstance one = target.Prefabs().Instantiate(prefab);
    RequireSame(target.Entities().BehaviourVariableOverrides(one.ObjectAt(0).Entity()), SampleOverrides(), "Instantiate did not apply the overrides");

    const std::vector<kb::scene::ScenePrefabInstance> many = target.Prefabs().InstantiateMany(prefab, 3U);
    kb::tests::Require(many.size() == 3U, "InstantiateMany lost an instance");
    for (const kb::scene::ScenePrefabInstance& instance : many) {
        RequireSame(target.Entities().BehaviourVariableOverrides(instance.ObjectAt(0).Entity()), SampleOverrides(), "InstantiateMany did not apply the overrides");
    }
    kb::scene::Scene batch;
    static_cast<void>(batch.Prefabs().InstantiateBatch(prefab, 4U));
    const auto batchOverrides = CollectOverrides(batch);
    kb::tests::Require(batchOverrides.size() == 4U, "InstantiateBatch lost an instance");
    for (const auto& entry : batchOverrides) {
        RequireSame(entry.second, SampleOverrides(), "InstantiateBatch did not apply the overrides to every instance");
    }
}

// An instance that changes a variable shows it as a property override; reverting restores the prefab's value, and
// applying it moves the value into the prefab.
void RunPrefabOverrideTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject root = MakeBehaviourObject(scene, "Root", { { "speed", ScriptValue{ 5.0F } }, { "lives", ScriptValue{ 3 } } });
    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().CaptureRegistered(root, "Tuned");
    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(handle);
    const kb::scene::SceneEntity entity = instance.ObjectAt(0).Entity();
    kb::tests::Require(scene.Prefabs().Overrides(instance.Handle()).Empty(), "A fresh instance with variable overrides reported overrides");

    scene.Entities().SetBehaviourVariableOverride(entity, "speed", ScriptValue{ 9.0F });
    static_cast<void>(scene.Entities().RemoveBehaviourVariableOverride(entity, "lives"));
    scene.Entities().SetBehaviourVariableOverride(entity, "armed", ScriptValue{ true });
    const kb::scene::ScenePrefabOverrideReport report = scene.Prefabs().Overrides(instance.Handle());
    const auto valueOf = [&report](const std::string& path) -> std::string {
        const auto found = std::ranges::find(report.properties, path, &kb::scene::ScenePrefabPropertyOverride::propertyPath);
        return found == report.properties.end() ? std::string{ "<none>" } : found->value;
    };
    kb::tests::Require(valueOf("behaviour.variable.speed") == "Float:9" && valueOf("behaviour.variable.lives") == "default" &&
            valueOf("behaviour.variable.armed") == "Bool:true" && report.properties.size() == 3U,
        "A changed, a removed and an added variable were not reported as property overrides");

    kb::tests::Require(scene.Prefabs().RevertOverrides(instance.Handle()), "The variable overrides did not revert");
    RequireSame(scene.Entities().BehaviourVariableOverrides(entity), { { "speed", ScriptValue{ 5.0F } }, { "lives", ScriptValue{ 3 } } },
        "Reverting did not restore the prefab's variable overrides");

    scene.Entities().SetBehaviourVariableOverride(entity, "speed", ScriptValue{ 7.5F });
    kb::tests::Require(scene.Prefabs().ApplyOverrides(instance.Handle()), "The variable override did not apply to the prefab");
    kb::tests::Require(scene.Prefabs().Overrides(instance.Handle()).Empty(), "An applied variable override is still reported");
    const kb::scene::ScenePrefab applied = scene.Prefabs().Get(handle);
    RequireSame(applied.Nodes()[0].components.behaviourVariableOverrides, { { "speed", ScriptValue{ 7.5F } }, { "lives", ScriptValue{ 3 } } },
        "Applying did not move the variable override into the prefab");
}

// The text prefab file stores the overrides too, and a file without them still reads.
void RunPrefabTextFileTest() {
    std::filesystem::create_directories(TestRoot());
    const std::filesystem::path path = TestRoot() / "Variables.21kbprefab";
    kb::scene::Scene scene;
    const kb::scene::ScenePrefab prefab = scene.Prefabs().Capture(MakeBehaviourObject(scene, "Root", SampleOverrides()));
    kb::tests::Require(kb::scene::ScenePrefabAssetWriter::Write(path, "Variables", prefab), "The prefab with overrides did not write");
    kb::scene::ScenePrefabAssetReadResult result;
    kb::tests::Require(kb::scene::ScenePrefabAssetReader::Read(path, result) && result.prefab.NodeCount() == 1U,
        "The prefab with overrides did not read");
    RequireSame(result.prefab.Nodes()[0].components.behaviourVariableOverrides, SampleOverrides(), "The prefab file did not keep the overrides");

    const kb::scene::ScenePrefab plain = scene.Prefabs().Capture(MakeBehaviourObject(scene, "Plain", {}));
    kb::tests::Require(kb::scene::ScenePrefabAssetWriter::Write(path, "Plain", plain), "The prefab without overrides did not write");
    kb::scene::ScenePrefabAssetReadResult plainResult;
    kb::tests::Require(kb::scene::ScenePrefabAssetReader::Read(path, plainResult) && plainResult.prefab.Nodes()[0].components.behaviourVariableOverrides.empty() &&
            plainResult.prefab.Nodes()[0].components.behaviour.has_value(),
        "A prefab file without overrides did not read as having none");
    std::error_code ignored;
    std::filesystem::remove_all(TestRoot(), ignored);
}

// Undo and redo bring back the variable overrides an edit changed.
void RunHistoryTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = MakeBehaviourObject(scene, "Tuned", { { "speed", ScriptValue{ 5.0F } } });
    kb::tests::Require(scene.History().Record("Set variable"), "The history did not open a command");
    scene.Entities().SetBehaviourVariableOverride(object.Entity(), "speed", ScriptValue{ 9.0F });
    scene.History().Commit();
    kb::tests::Require(scene.History().Undo(), "The variable edit could not be undone");
    RequireSame(scene.Entities().BehaviourVariableOverrides(object.Entity()), { { "speed", ScriptValue{ 5.0F } } }, "Undo did not restore the variable override");
    kb::tests::Require(scene.History().Redo(), "The variable edit could not be redone");
    RequireSame(scene.Entities().BehaviourVariableOverrides(object.Entity()), { { "speed", ScriptValue{ 9.0F } } }, "Redo did not restore the variable override");
}

} // namespace

namespace kb::tests {

void RunSceneBehaviourVariableTests() {
    RunValueTextTest();
    RunSceneDocumentRoundTripTest();
    RunLegacyBehaviourReadTest();
    RunPrefabCaptureInstantiateTest();
    RunPrefabOverrideTest();
    RunPrefabTextFileTest();
    RunHistoryTest();
}

} // namespace kb::tests
