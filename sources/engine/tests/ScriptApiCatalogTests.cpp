#include "TestSupport.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptAgentGuides.hpp"
#include "engine/script/ScriptAgentProjectFiles.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptApiExport.hpp"
#include "engine/script/ScriptEventHooks.hpp"
#include "engine/script/ScriptLuaCheck.hpp"
#include "engine/script/ScriptNameSuggestion.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptSandboxGlobals.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
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
    const kb::visual::VisualGraphNodeCatalog nodeCatalog = host.CreateVisualGraphNodeCatalog();
    for (const kb::script::ScriptApiCatalogFunction& function : catalog.functions) {
        kb::tests::Require(!function.description.empty(), "Script API catalog exposed a function without documentation");
        const std::string bindingSymbol = "Function." + function.name;
        const std::string nodeId = "NativeBinding:CallNative:" + bindingSymbol;
        const std::string documentationAnchor = kb::script::ScriptApiDocumentationAnchor(function.name);
        bool foundInput = false;
        bool foundOutput = false;
        const kb::visual::VisualGraphNodeCatalogEntry* node = nodeCatalog.Find(nodeId);
        kb::tests::Require(node != nullptr, "Script API source map function has no Visual Graph palette node");
        for (const kb::script::ScriptApiCatalogSourceMapEntry& entry : catalog.sourceMap) {
            if (entry.functionName != function.name) {
                continue;
            }
            kb::tests::Require(
                entry.visualGraphNodeId == nodeId && entry.runtimeBindingSymbol == bindingSymbol &&
                    entry.documentationAnchor == documentationAnchor,
                "Script API source map drifted from its live Visual Graph runtime binding");
            foundInput = foundInput || entry.visualGraphPinDirection == "Input";
            foundOutput = foundOutput || entry.visualGraphPinDirection == "Output";
        }
        kb::tests::Require(foundInput && foundOutput, "Script API source map did not cover the Visual Graph node pins");
        for (const kb::visual::VisualGraphPinTemplate& pin : node->pins) {
            const auto mappedPin = std::find_if(catalog.sourceMap.begin(), catalog.sourceMap.end(), [&function, &nodeId, &pin](const kb::script::ScriptApiCatalogSourceMapEntry& entry) {
                return entry.functionName == function.name && entry.visualGraphNodeId == nodeId && entry.visualGraphPinName == pin.name &&
                    entry.visualGraphPinDirection == kb::visual::ToString(pin.direction);
            });
            kb::tests::Require(mappedPin != catalog.sourceMap.end(), "Script API source map omitted a Visual Graph node pin");
        }
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
    kb::tests::Require(
        Contains(json, "\"sourceMap\"") && Contains(json, "\"runtimeBindingSymbol\"") && Contains(json, "\"documentationAnchor\""),
        "Script API JSON is missing the Visual Graph to runtime documentation source map");
    kb::tests::Require(
        Contains(markdown, "<a id=\"" + kb::script::ScriptApiDocumentationAnchor("Input.Vector2") + "\"></a>"),
        "Script API markdown is missing stable function documentation anchors");

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
    // The reference is small pages an agent reads one at a time, plus how-to guides; no single
    // file holds the whole surface.
    const std::filesystem::path apiRoot = TestRoot() / ".kb" / "api";
    kb::tests::Require(std::filesystem::exists(apiRoot / "README.md"), "API reference index was not created");
    kb::tests::Require(!std::filesystem::exists(apiRoot / "script_api.md"), "the single-file API reference must no longer be written");
    kb::tests::Require(std::filesystem::exists(apiRoot / "components" / "Rigidbody.md"), "a component reference page was not created");
    kb::tests::Require(std::filesystem::exists(apiRoot / "functions" / "Physics.md"), "a function group reference page was not created");
    std::size_t largestPage = 0U;
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(apiRoot)) {
        if (entry.is_regular_file() && entry.path().extension() == ".md") {
            largestPage = std::max<std::size_t>(largestPage, static_cast<std::size_t>(entry.file_size()));
        }
    }
    kb::tests::Require(largestPage > 0U && largestPage < 24U * 1024U, "every API reference page must stay small enough to read on its own");
    kb::tests::Require(std::filesystem::exists(TestRoot() / ".kb" / "docs" / "README.md") &&
                            std::filesystem::exists(TestRoot() / ".kb" / "docs" / "objects-and-materials.md"),
        "the how-to guides were not created");
    kb::tests::Require(std::filesystem::exists(TestRoot() / "CLAUDE.md"), "CLAUDE.md was not created");
    {
        // CLIs that read their own instruction file import AGENTS.md, so the rules are written once.
        std::ifstream file(TestRoot() / "GEMINI.md", std::ios::binary);
        const std::string text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        kb::tests::Require(text == "@AGENTS.md\n", "GEMINI.md must be the one line that imports AGENTS.md");
    }
    {
        std::ifstream agents(TestRoot() / "AGENTS.md", std::ios::binary);
        const std::string text{ std::istreambuf_iterator<char>(agents), std::istreambuf_iterator<char>() };
        kb::tests::Require(text.size() < 3072U, "AGENTS.md must stay short: it is read with every request");
        // The rules every agent needs, said once and plainly: how to call, when it is done, what to do when a step fails.
        kb::tests::Require(text.find(kb::script::ScriptAgentProjectFiles::AgentRules()) != std::string::npos &&
                text.find("`commands`") != std::string::npos && text.find("Done means") != std::string::npos && text.find("Do not repeat the same call") != std::string::npos &&
                text.find("JavaScript") == std::string::npos && text.find("--project") == std::string::npos,
            "AGENTS.md must hold the shared rules, one example of each call, and no mechanics of one CLI");
        // Nothing shouts: emphasis makes a model apply a rule where it does not belong.
        kb::tests::Require(text.find("MUST") == std::string::npos && text.find("NEVER") == std::string::npos && text.find("ALWAYS") == std::string::npos, "AGENTS.md must use plain words");
        // It is an index: every recipe is named in it, short, and written beside the other guides.
        for (const kb::script::ScriptAgentGuide& guide : kb::script::ScriptAgentGuides::All()) {
            if (guide.name.starts_with("recipe-")) {
                kb::tests::Require(text.find("`" + std::string{ guide.name } + "`") != std::string::npos, "AGENTS.md must name every recipe");
                kb::tests::Require(guide.body.size() < 2600U, "a recipe is read in one step and must stay short");
                kb::tests::Require(guide.body.find("--project") == std::string_view::npos, "a recipe leaves the project out: the tool fills it in");
                kb::tests::Require(guide.body.find("\n[\"") != std::string_view::npos, "a recipe's commands are lists of words an agent sends as they are");
                kb::tests::Require(std::filesystem::exists(TestRoot() / ".kb" / "docs" / (std::string{ guide.name } + ".md")), "a recipe file was not written");
            }
        }
    }
    kb::tests::Require(kb::script::ScriptAgentGuides::Suggest("recipe-coin").find("recipe-pickup") != std::string::npos &&
            kb::script::ScriptAgentGuides::Suggest("recipe-materal").find("recipe-material") != std::string::npos,
        "a wrong guide topic must name the guides it probably meant");
    const std::optional<std::string> raycast = kb::script::ScriptApiExport::ToReferenceTopic(catalog, "physics.raycast");
    kb::tests::Require(raycast.has_value() && raycast->starts_with("# Physics.Raycast"), "an API topic must resolve one function without regard to case");
    kb::tests::Require(!kb::script::ScriptApiExport::ToReferenceTopic(catalog, "NoSuchTopic").has_value(), "an unknown API topic must resolve to nothing");
    kb::tests::Require(std::filesystem::exists(TestRoot() / ".kb" / "api" / "script_api.json"), "API JSON was not created");
    kb::tests::Require(std::filesystem::exists(TestRoot() / ".kb" / "api" / "manifest.json"), "API manifest was not created");
    kb::tests::Require(std::filesystem::exists(TestRoot() / ".kb" / "api" / "authoring_hints.json"), "API authoring hints were not created");

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

void RequireText(bool condition, const std::string& message) {
    kb::tests::Require(condition, message.c_str());
}

// Every finding as "line:code: message", for a failure message and for searching.
[[nodiscard]] std::string Describe(const std::vector<kb::script::ScriptLuaFinding>& findings) {
    std::string text;
    for (const kb::script::ScriptLuaFinding& finding : findings) {
        text += std::to_string(finding.line) + ":" + finding.code + ": " + finding.message + "\n";
    }
    return text.empty() ? std::string{ "(no findings)" } : text;
}

[[nodiscard]] const kb::script::ScriptLuaFinding* FindFinding(const std::vector<kb::script::ScriptLuaFinding>& findings, std::string_view code, std::string_view fragment) {
    for (const kb::script::ScriptLuaFinding& finding : findings) {
        if (finding.code == code && Contains(finding.message, fragment)) return &finding;
    }
    return nullptr;
}

// The shared checker behind `kb_cli validate` and `kb_cli lsp`: it flags what fails or never runs, with the nearest real
// name, and leaves a correct script alone.
void RunScriptLuaCheckTests() {
    using kb::script::ScriptLuaFindingSeverity;
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    const kb::script::ScriptApiCatalog catalog = kb::script::ScriptApiCatalog::Build(host);

    const std::string broken = R"(function OnTriggerEnte(self, event)
    Logg("hit")
    Foo.Bar()
    Physics.Raycat(0, 0, 0, 0, 0, 1, 10)
    self:SetProperty("Transfrom", "localPosition.x", 1)
    self:GetProperty("Transform", "localPostion.x")
    self:SetProperty("Transform", "worldPosition.x", 1)
    self:SetProperty("Transform", "localPosition.x", "far")
    Transform.Rotate(self.entity, 0, 1)
    Transform.Rotate(self.entity, "a", 1, 2)
    Transform.Rotate(self.entity, 1, 2, 3, 4, 5)
    Input:Pressed("Fire")
    World.SetPropertyFloat(self.entity, "Transform", "localPosiion.y", 1)
    require("Other")
end
)";
    const std::vector<kb::script::ScriptLuaFinding> findings = kb::script::CheckLuaScript(broken, catalog);
    const std::string all = Describe(findings);
    const auto expect = [&](std::string_view code, std::string_view fragment, std::uint32_t line, ScriptLuaFindingSeverity severity) {
        const kb::script::ScriptLuaFinding* found = FindFinding(findings, code, fragment);
        RequireText(found != nullptr && found->line == line && found->severity == severity,
            "the Lua checker missed " + std::string{ code } + " '" + std::string{ fragment } + "' on line " + std::to_string(line) + ":\n" + all);
    };
    expect("unknown-hook", "OnTriggerEnte() is not an event hook of the engine", 1U, ScriptLuaFindingSeverity::Warning);
    expect("unknown-hook", "if you meant the hook, it is OnTriggerEnter(self, event)", 1U, ScriptLuaFindingSeverity::Warning);
    expect("unknown-global", "Logg is not defined", 2U, ScriptLuaFindingSeverity::Error);
    expect("unknown-global", "did you mean Log?", 2U, ScriptLuaFindingSeverity::Error);
    expect("unknown-global", "Foo is not defined", 3U, ScriptLuaFindingSeverity::Error);
    expect("unknown-function", "Physics.Raycat does not exist; did you mean Physics.Raycast?", 4U, ScriptLuaFindingSeverity::Error);
    expect("bad-component", "there is no component called \"Transfrom\"; did you mean \"Transform\"?", 5U, ScriptLuaFindingSeverity::Error);
    expect("bad-property", "Transform has no property \"localPostion.x\"; did you mean \"localPosition.x\"?", 6U, ScriptLuaFindingSeverity::Error);
    expect("read-only-property", "Transform.worldPosition.x is read-only", 7U, ScriptLuaFindingSeverity::Error);
    expect("bad-argument-type", "the value must be a number, not a string", 8U, ScriptLuaFindingSeverity::Error);
    expect("bad-argument-count", "Transform.Rotate(entity, x, y, z, w?) needs 4 arguments, the call has 3", 9U, ScriptLuaFindingSeverity::Error);
    expect("bad-argument-type", "Transform.Rotate argument 2 (x) must be a number, not a string", 10U, ScriptLuaFindingSeverity::Error);
    expect("bad-argument-count", "takes 5 arguments, the call has 6", 11U, ScriptLuaFindingSeverity::Error);
    expect("method-call", "Input:Pressed(...) passes the table as the first argument", 12U, ScriptLuaFindingSeverity::Error);
    expect("bad-property", "Transform has no property \"localPosiion.y\"", 13U, ScriptLuaFindingSeverity::Error);
    expect("unknown-global", "require is not available", 14U, ScriptLuaFindingSeverity::Error);
    RequireText(findings.size() == 14U, "the Lua checker must report exactly the 14 mistakes of the script:\n" + all);

    // A correct script gets nothing: locals, parameters, loop variables, the globals the script defines, a shadowed engine
    // table and the calls it guards are not judged, and the real hooks, `On`-named events it emits and the other-engine
    // callbacks keep their own rules.
    const std::string correct = R"(local function helper(a) return a + 1 end
local Input = { Pressed = function() return false end }
Counter = { n = 0 }
function Counter.add(n) return n + 1 end
function Counter:reset() self.n = 0 end
myGlobal = function(a, b) return a + b end

function OnTriggerEnter(self, event)
    local other = event.args.other
    for i, v in ipairs({ 1, 2 }) do Log(tostring(i + v)) end
    Log(string.format("%d", helper(1) + Counter.add(1) + myGlobal(1, 2)))
    if World.GetName and World.GetName() then Log("named") end
    if Input.Foo then Input.Foo() end
    Emit("OnCoinCollected", { amount = 1 })
    self:SetProperty("Transform", "localPosition.x", 1.5)
    self:SetProperty("Text", "content", "Coins: " .. 3)
    self:GetProperty("Audio Source", "volume")
    Transform.Rotate(self.entity, 0, 90, 0)
    Transform.Rotate(self.entity, unpack({ 0, 90, 0 }))
    Transform.Rotate({ entity = self.entity, x = 0, y = 90, z = 0 })
    Audio.Play("/Game/Audio/Coin.wav")
    local text = Counter:reset()
    World.Destroy(self.entity)
    Localization.SetCatalog("0123456789abcdef")
    World.SetTag(self.entity, "coin", 1)
    Audio.Play(5)
    self:Counter_helper()
end

function OnCoinCollected(self, event) end
function Tick(self, dt)
    local t = Time and Time.Delta and Time.Delta()
end
function CoinCollected(self, event) end
)";
    const std::vector<kb::script::ScriptLuaFinding> clean = kb::script::CheckLuaScript(correct, catalog);
    // `self:Counter_helper()` is an unknown self method on purpose: nothing else may be reported.
    RequireText(clean.size() == 1U && clean.front().code == "unknown-function" && Contains(clean.front().message, "self:Counter_helper()"),
        "the Lua checker flagged a correct script:\n" + Describe(clean));

    const std::vector<kb::script::ScriptLuaFinding> guarded = kb::script::CheckLuaScript("function Tick(self, dt)\n    local n = World.GetName and World.GetName()\n    if Missing then Missing.go() end\nend\n", catalog);
    RequireText(guarded.empty(), "a call the script guards must not be flagged:\n" + Describe(guarded));

    // Other-engine callbacks keep their own message; the closest hook is named for any On* function that is not one.
    const std::vector<kb::script::ScriptLuaFinding> foreign = kb::script::CheckLuaScript("function OnStart(self)\nend\nfunction OnUpdate(self, dt)\nend\nfunction OnCollisionEnter(self, event)\nend\n", catalog);
    RequireText(foreign.size() == 2U && FindFinding(foreign, "foreign-lifecycle", "OnStart() is never called - 21kb calls Ready(self, dt)") != nullptr &&
            FindFinding(foreign, "foreign-lifecycle", "OnUpdate() is never called - 21kb calls Tick(self, dt)") != nullptr,
        "foreign callbacks must be named without a second hook warning:\n" + Describe(foreign));
    // A hook name far from every real hook gets no made-up "closest hook"; a table the project declares itself is defined.
    const std::vector<kb::script::ScriptLuaFinding> custom = kb::script::CheckLuaScript("function OnNever(self, event) end\n", catalog);
    RequireText(custom.size() == 1U && !Contains(custom.front().message, "if you meant the hook"), "a name that is not near any hook must not be given a closest hook:\n" + Describe(custom));
    const std::string plugin = "function Tick(self, dt)\n    Plugin.Spin(self.entity)\nend\n";
    kb::script::ScriptApiCatalog declared = catalog;
    declared.declaredGlobals.push_back("Plugin");
    RequireText(!kb::script::CheckLuaScript(plugin, catalog).empty() && kb::script::CheckLuaScript(plugin, declared).empty(),
        "a global the project declares must not be called undefined, and one it does not must be");
    // A custom event of the project is not a misspelled hook.
    kb::script::ScriptApiCatalog withProject = catalog;
    withProject.projectEntries.push_back(kb::script::ScriptApiNameEntry{ .kind = kb::script::ScriptApiNameKind::Event, .name = "OnGameWon" });
    RequireText(kb::script::CheckLuaScript("function OnGameWon(self, event) end\n", withProject).empty() &&
            !kb::script::CheckLuaScript("function OnGameWon(self, event) end\n", catalog).empty(),
        "an event that a script of the project emits is not a misspelled hook");
    // The constructs outside the shared Lua come from the same checker, with their position.
    const std::vector<kb::script::ScriptLuaFinding> subset = kb::script::CheckLuaScript("local x = 7 // 2\n", catalog);
    RequireText(subset.size() == 1U && subset.front().code == "outside-subset" && subset.front().line == 1U && subset.front().column == 13U,
        "a construct outside the shared Lua must be reported with its place:\n" + Describe(subset));

    // What a script gets at run time from SetProperty and GetProperty with a name that does not exist.
    using kb::script::ScriptSceneComponentApi;
    RequireText(ScriptSceneComponentApi::UnknownNameMessage("Transform", "localPosition.x").empty() && ScriptSceneComponentApi::UnknownNameMessage("Audio Source", "volume").empty(),
        "a component and a property that exist have no error");
    RequireText(Contains(ScriptSceneComponentApi::UnknownNameMessage("Transform", "localPostion.x"), "component \"Transform\" has no property \"localPostion.x\"; did you mean \"localPosition.x\"?") &&
            Contains(ScriptSceneComponentApi::UnknownNameMessage("Transfrom", "localPosition.x"), "there is no component called \"Transfrom\"; did you mean \"Transform\"?"),
        "a wrong component or property name must be named with the nearest right one");

    // The nearest-name helper behind the suggestions.
    RequireText(kb::script::ScriptNameDistance("Logg", "Log") == 1U && kb::script::ScriptNameDistance("log", "Log") == 0U,
        "the name distance ignores case and counts letters");
    RequireText(kb::script::ScriptEventHooks::Closest("OnTriggerEnte") == "OnTriggerEnter" && kb::script::ScriptEventHooks::Closest("OnCollisionExt") == "OnCollisionExit",
        "the closest hook must be named");
}

// The reference of the globals and of the event hooks: what an agent reads before it writes a script.
void RunScriptGlobalsAndHooksReferenceTests() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    const kb::script::ScriptApiCatalog catalog = kb::script::ScriptApiCatalog::Build(host);
    const auto topic = [&catalog](std::string_view name) {
        const std::optional<std::string> page = kb::script::ScriptApiExport::ToReferenceTopic(catalog, name);
        RequireText(page.has_value(), "the API reference has no topic " + std::string{ name });
        return *page;
    };

    // An exact global name wins over a function of a group with the same short name.
    const std::string log = topic("Log");
    RequireText(log.starts_with("# Log\n") && Contains(log, "Writes a value to the log") && !Contains(log, "Returns the natural logarithm"), "topic Log must be the global, not Math.Log:\n" + log);
    const std::string emit = topic("Emit");
    RequireText(emit.starts_with("# Emit\n") && Contains(emit, "custom event") && !Contains(emit, "Emits an explicit number of particles"), "topic Emit must be the global, not Particles.Emit:\n" + emit);
    RequireText(topic("Math.Log").starts_with("# Math.Log") && topic("Particles.Emit").starts_with("# Particles.Emit"), "the functions of the groups stay reachable by their full name");
    RequireText(topic("log").starts_with("# Log\n"), "a global is found in other letters before a function that only shares its short name");
    for (const std::string_view name : { "Log", "Emit", "EmitTo", "Import", "SetShared", "GetShared", "HasShared", "RemoveShared", "CallFunction", "Events", "Inspector", "assert",
             "pcall", "math", "string" }) {
        const kb::script::ScriptSandboxGlobal* global = kb::script::ScriptSandboxGlobals::Find(name);
        RequireText(global != nullptr, "the globals list misses " + std::string{ name });
        const std::string page = topic(name);
        RequireText(page.starts_with("# " + std::string{ name } + "\n") && Contains(page, global->signature), "a global must have its own page: " + std::string{ name });
    }
    const std::string assertPage = topic("assert");
    RequireText(Contains(assertPage, "exits with code 1") && Contains(assertPage, "kb_cli run") && Contains(assertPage, "without a window"),
        "assert must be documented as the way to test a script, with the exit code of run:\n" + assertPage);
    RequireText(Contains(topic("Globals"), "`assert(condition, message)`") && Contains(topic("index"), "Globals.md") && Contains(topic("index"), "EventHooks.md"), "the index must lead to the globals and the hooks");

    // Every hook the runtime calls, with its exact arguments.
    const std::string hooks = topic("EventHooks");
    for (const std::string_view name : { "OnTriggerEnter", "OnTriggerStay", "OnTriggerExit", "OnCollisionEnter", "OnCollisionStay", "OnCollisionExit", "OnControllerColliderHit",
             "OnAnimationEvent", "OnAudioMarker", "OnParticleSystemFinished", "OnTimelineEvent", "OnTimelineMarker", "OnPrefabInstantiated", "OnDamaged", "OnDeath",
             "TimerFired", "TaskCompleted", "SceneLoaded", "OnNetConnected", "Tick", "Ready" }) {
        RequireText(Contains(hooks, name), "the hooks page misses " + std::string{ name });
        RequireText(topic(name).starts_with("# " + std::string{ name }), "a hook must have its own page: " + std::string{ name });
    }
    for (const std::string& event : catalog.lifecycleEvents) {
        const kb::script::ScriptEventHook* hook = kb::script::ScriptEventHooks::Find(event);
        RequireText(hook != nullptr && hook->receives == "(self, dt)", "every lifecycle event of the catalog is a hook: " + event);
    }
    const std::string trigger = topic("OnTriggerEnter");
    for (const std::string_view argument : { "`other`", "`pointX`", "`normalZ`" }) RequireText(Contains(trigger, argument), "OnTriggerEnter must list its arguments");
    RequireText(Contains(topic("OnAnimationEvent"), "`normalizedTime`") && Contains(topic("OnAudioMarker"), "`positionSeconds`") && Contains(topic("OnParticleSystemFinished"), "`effect`") &&
            Contains(topic("OnTimelineEvent"), "`payload`"),
        "the animation, audio, particle and timeline hooks must list their arguments");
    RequireText(topic("OnRpc_Hit").starts_with("# OnRpc_<name>"), "an RPC handler is documented as OnRpc_<name>");
    RequireText(Contains(kb::script::ScriptApiExport::SearchReference(catalog, "trigger"), "OnTriggerEnter") && Contains(kb::script::ScriptApiExport::SearchReference(catalog, "assert"), "`assert`"),
        "search must find hooks and globals");

    // The pages are files too.
    bool hookFile = false;
    bool globalFile = false;
    for (const kb::script::ScriptApiReferencePage& page : kb::script::ScriptApiExport::ToReferencePages(catalog)) {
        hookFile = hookFile || page.path == "hooks/OnTriggerEnter.md";
        globalFile = globalFile || page.path == "globals/Log.md";
    }
    RequireText(hookFile && globalFile, "the hook and global pages must be written beside the others");
}

} // namespace

namespace kb::tests {

void RunScriptComponentPropertyTests();

void RunScriptApiCatalogTests() {
    RunCatalogBuildTest();
    RunCatalogExportTest();
    RunFunctionDescriptionIsRequiredAtRegistrationTest();
    RunAgentProjectFilesTest();
    RunScriptLuaCheckTests();
    RunScriptGlobalsAndHooksReferenceTests();
    RunScriptComponentPropertyTests();
}

} // namespace kb::tests
