#include "TestSupport.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptAgentProjectFiles.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptApiExport.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <system_error>

namespace {

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_engine_script_api_catalog_tests";
}

void ResetTestRoot() {
    std::error_code error;
    std::filesystem::remove_all(TestRoot(), error);
    std::filesystem::create_directories(TestRoot(), error);
    kb::tests::Require(!error, "Script API catalog test root could not be prepared");
}

[[nodiscard]] bool Contains(const std::string& text, std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

void RunCatalogBuildTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Script API catalog host reported diagnostics");

    const kb::script::ScriptApiCatalog catalog = kb::script::ScriptApiCatalog::Build(host);

    kb::tests::Require(catalog.lifecycleEvents.size() == 10U, "Script API catalog did not enumerate all lifecycle events");
    kb::tests::Require(catalog.lifecycleEvents.front() == "Created", "Script API catalog lifecycle order is wrong");
    kb::tests::Require(!catalog.functions.empty(), "Script API catalog did not collect registered functions");
    kb::tests::Require(!catalog.components.empty(), "Script API catalog did not collect components");

    const kb::script::ScriptApiCatalogFunction* vector2 = catalog.FindFunction("Input.Vector2");
    kb::tests::Require(vector2 != nullptr, "Script API catalog is missing Input.Vector2");
    kb::tests::Require(
        vector2->description == "Reads the current two-dimensional value of the named input action.",
        "Script API catalog did not preserve the authored Input.Vector2 description");
    kb::tests::Require(vector2->outputs.size() == 2U, "Script API catalog Input.Vector2 output contract is wrong");
    for (const kb::script::ScriptApiCatalogFunction& function : catalog.functions) {
        kb::tests::Require(!function.description.empty(), "Script API catalog exposed a function without documentation");
    }

    bool foundTransform = false;
    for (const kb::script::ScriptApiCatalogComponent& component : catalog.components) {
        if (component.name != "Transform") {
            continue;
        }
        foundTransform = true;
        bool localPositionWritable = false;
        bool worldPositionReadOnly = false;
        for (const kb::script::ScriptApiCatalogProperty& property : component.properties) {
            if (property.name == "localPosition.x" && property.writable) {
                localPositionWritable = true;
            }
            if (property.name == "worldPosition.x" && !property.writable) {
                worldPositionReadOnly = true;
            }
        }
        kb::tests::Require(localPositionWritable, "Script API catalog Transform.localPosition.x contract is wrong");
        kb::tests::Require(worldPositionReadOnly, "Script API catalog Transform.worldPosition.x contract is wrong");
    }
    kb::tests::Require(foundTransform, "Script API catalog is missing the Transform component");

    // Anti-drift gate: every Lua sugar binding must resolve to a registered
    // function so generated stubs cannot advertise callables the runtime lacks.
    // "Log" is exempt: the sandbox always installs it, but hosts register the
    // backing function only when they route logs somewhere.
    for (const kb::script::ScriptApiCatalogLuaBinding& binding : catalog.luaBindings) {
        if (binding.functionName == "Log") {
            continue;
        }
        kb::tests::Require(
            catalog.FindFunction(binding.functionName) != nullptr,
            "Script API catalog Lua binding does not resolve to a registered function");
    }
}

void RunCatalogExportTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    const kb::script::ScriptApiCatalog catalog = kb::script::ScriptApiCatalog::Build(host);

    const std::string markdown = kb::script::ScriptApiExport::ToMarkdown(catalog);
    kb::tests::Require(Contains(markdown, "### Input"), "Script API markdown is missing the Input group");
    kb::tests::Require(
        Contains(markdown, "Reads the current two-dimensional value of the named input action."),
        "Script API markdown is missing authored function descriptions");
    kb::tests::Require(Contains(markdown, "`localPosition.x`"), "Script API markdown is missing Transform properties");
    kb::tests::Require(Contains(markdown, "function Tick(self, dt)"), "Script API markdown is missing lifecycle signatures");

    const std::string json = kb::script::ScriptApiExport::ToJson(catalog);
    kb::tests::Require(Contains(json, "\"Input.Vector2\""), "Script API JSON is missing functions");
    kb::tests::Require(
        Contains(json, "\"Timeline.Create\"") &&
            Contains(json, "\"table\":\"Timeline\""),
        "Script API JSON is missing the live Timeline Lua surface");
    kb::tests::Require(
        Contains(json, "\"description\":\"Reads the current two-dimensional value of the named input action.\""),
        "Script API JSON is missing authored function descriptions");
    kb::tests::Require(Contains(json, "\"lifecycleEvents\""), "Script API JSON is missing lifecycle events");

    const std::string stubs = kb::script::ScriptApiExport::ToLuaStubs(catalog);
    kb::tests::Require(Contains(stubs, "---@meta"), "Script API stubs are missing the meta marker");
    kb::tests::Require(Contains(stubs, "function Input.Vector2(action, player) end"), "Script API stubs are missing Input.Vector2");
    kb::tests::Require(Contains(stubs, "function Task.WaitSeconds(seconds, owner) end"), "Script API stubs are missing the direct Lua Task.WaitSeconds await surface");
    kb::tests::Require(Contains(stubs, "function Task.WaitEvent(event, owner) end"), "Script API stubs are missing the direct Lua Task.WaitEvent await surface");
    kb::tests::Require(
        Contains(stubs, "function Timeline.Create(asset, entity) end") &&
            Contains(stubs, "function Timeline.Time(instance) end"),
        "Script API stubs are missing the direct Lua Timeline surface");
    kb::tests::Require(
        Contains(stubs, "---Reads the current two-dimensional value of the named input action."),
        "Script API stubs are missing authored function descriptions");
    kb::tests::Require(Contains(stubs, "---@return KbInputVector2Result"), "Script API stubs are missing multi-output result classes");
    kb::tests::Require(Contains(stubs, "function KbSelf:SetProperty(component, property, value) end"), "Script API stubs are missing self methods");
    // The Lua wrappers do not all mirror CallFunction: GetPosition strips its
    // "found" guard and returns nil, Audio.Play returns only the voice id.
    kb::tests::Require(Contains(stubs, "---@return KbTransformGetPositionResult|nil"), "Script API stubs miss the GetPosition nil contract");
    kb::tests::Require(!Contains(stubs, "KbTransformGetPositionResult\n---@field found"), "Script API stubs leak the consumed found pin");
    kb::tests::Require(Contains(stubs, "---@return integer voice"), "Script API stubs miss the Audio.Play single-output contract");
    kb::tests::Require(Contains(markdown, "or nil when found is false"), "Script API markdown misses Lua binding return semantics");

    // The stub file must be loadable Lua so the Lua Language Server (and the
    // engine sandbox) can parse it.
    kb::script::PucLuaScriptRuntime luaRuntime;
    const kb::script::PucLuaLoadResult loaded = luaRuntime.LoadScript(kb::assets::AssetId{ 9001U }, stubs, "kb.lua");
    kb::tests::Require(loaded.succeeded, "Script API stubs are not valid Lua");
}

void RunFunctionDescriptionIsRequiredAtRegistrationTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    const auto callback = [](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument>) {
        return kb::script::ScriptFunctionCallResult{ .executed = true };
    };

    kb::tests::Require(
        !host.RegisterFunction(kb::script::ScriptFunctionDesc{
            .signature = kb::script::ScriptFunctionSignature{ .name = "Plugin.Undocumented" },
            .callback = callback,
        }),
        "ScriptRuntimeHost accepted a plugin function without an authored description");
    kb::tests::Require(
        host.RegisterFunction(kb::script::ScriptFunctionDesc{
            .signature = kb::script::ScriptFunctionSignature{
                .name = "Plugin.Documented",
                .description = "Performs the documented plugin operation.",
            },
            .callback = callback,
        }),
        "ScriptRuntimeHost rejected a plugin function with an authored description");

    const kb::script::ScriptApiCatalog catalog = kb::script::ScriptApiCatalog::Build(host);
    const kb::script::ScriptApiCatalogFunction* documented = catalog.FindFunction("Plugin.Documented");
    kb::tests::Require(
        documented != nullptr && documented->description == "Performs the documented plugin operation.",
        "Script API catalog did not preserve a plugin-authored function description");
}

void RunAgentProjectFilesTest() {
    ResetTestRoot();

    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    const kb::script::ScriptApiCatalog catalog = kb::script::ScriptApiCatalog::Build(host);

    const kb::script::ScriptAgentProjectFilesResult first = kb::script::ScriptAgentProjectFiles::Write(TestRoot(), catalog);
    kb::tests::Require(first.succeeded, "Agent project files were not written");
    kb::tests::Require(std::filesystem::exists(TestRoot() / "AGENTS.md"), "AGENTS.md was not created");
    kb::tests::Require(std::filesystem::exists(TestRoot() / ".luarc.json"), ".luarc.json was not created");
    kb::tests::Require(std::filesystem::exists(TestRoot() / ".kb" / "api" / "kb.lua"), "Lua stubs were not created");
    kb::tests::Require(std::filesystem::exists(TestRoot() / ".kb" / "api" / "script_api.md"), "API markdown was not created");
    kb::tests::Require(std::filesystem::exists(TestRoot() / ".kb" / "api" / "script_api.json"), "API JSON was not created");

    const kb::script::ScriptAgentProjectFilesResult second = kb::script::ScriptAgentProjectFiles::Write(TestRoot(), catalog);
    kb::tests::Require(second.succeeded, "Agent project files rewrite failed");
    bool skippedAgents = false;
    for (const std::filesystem::path& skipped : second.skippedFiles) {
        if (skipped.filename() == "AGENTS.md") {
            skippedAgents = true;
        }
    }
    kb::tests::Require(skippedAgents, "AGENTS.md was overwritten on rewrite");
}

} // namespace

namespace kb::tests {

void RunScriptApiCatalogTests() {
    RunCatalogBuildTest();
    RunCatalogExportTest();
    RunFunctionDescriptionIsRequiredAtRegistrationTest();
    RunAgentProjectFilesTest();
}

} // namespace kb::tests
