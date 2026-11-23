#include "CliCommands.hpp"
#include "CliMaterialLibrary.hpp"
#include "CliProgram.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/script/ScriptAgentSearch.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialGraphNodeDocs.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// What an agent gets for materials: every node described (the registry and the descriptions agree), a whole graph written and read back as
// text with errors that name the fix, a library of ready materials that all build and fit a phone, and a search that finds them by words.
namespace {

namespace fs = std::filesystem;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::fputs(message.c_str(), stderr);
        std::fputs("\n", stderr);
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] bool Contains(const std::string& text, std::string_view part) {
    return text.find(part) != std::string::npos;
}

struct Run {
    int exitCode = 0;
    std::string out;
    std::string err;
    [[nodiscard]] std::string All() const { return out + err; }
};

[[nodiscard]] Run Cli(const std::vector<std::string>& words) {
    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    const int exitCode = kb::cli::RunCli(words, in, kb::cli::CommandIo{ .out = out, .err = err });
    return Run{ exitCode, out.str(), err.str() };
}

[[nodiscard]] std::string Project(std::string_view name) {
    kb::cli::ResetCliConfinementForTests();
    const fs::path root = fs::temp_directory_path() / "21kb_kb_cli_material_agent_tests";
    std::error_code error;
    fs::remove_all(root / std::string{ name }, error);
    fs::create_directories(root, error);
    const Run made = Cli({ "new", "--parent", root.string(), "--name", std::string{ name } });
    Require(made.exitCode == 0, "the project could not be made: " + made.All());
    return (root / std::string{ name }).string();
}

[[nodiscard]] std::string Read(const fs::path& file) {
    std::ifstream input{ file, std::ios::binary };
    return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

[[nodiscard]] Run Graph(const std::string& project, const std::string& material, const std::vector<std::string>& tail) {
    std::vector<std::string> words{ "material-graph", tail.front(), "--project", project, "--material", material };
    words.insert(words.end(), tail.begin() + 1, tail.end());
    return Cli(words);
}

// The graph text `show` prints: everything between the path line and the Parameters or Compiles line.
[[nodiscard]] std::string ShownText(const std::string& shown) {
    std::istringstream lines{ shown };
    std::string line;
    std::string text;
    bool first = true;
    while (std::getline(lines, line)) {
        if (first) { first = false; continue; }
        if (line.starts_with("Parameters:") || line.starts_with("Compiles:")) break;
        text += line + "\n";
    }
    return text;
}

// Every node kind has a description, the index lists each once, and each page's example is text the graph parser accepts.
void RunNodeReferenceTests() {
    const std::span<const kb::render::RenderMaterialGraphNodeKind> kinds = kb::render::AllRenderMaterialGraphNodeKinds();
    std::set<std::string> described;
    for (const kb::render::RenderMaterialGraphNodeDoc& doc : kb::render::RenderMaterialGraphNodeDocs()) {
        const std::string name{ kb::render::RenderMaterialGraphNodeKindName(doc.kind) };
        Require(described.insert(name).second, "the node " + name + " is described twice");
        Require(!doc.purpose.empty() && !doc.group.empty(), "the node " + name + " needs a purpose and a group");
    }
    for (const kb::render::RenderMaterialGraphNodeKind kind : kinds) {
        const std::string name{ kb::render::RenderMaterialGraphNodeKindName(kind) };
        Require(described.contains(name), "the node " + name + " has no description: add a row to RenderMaterialGraphNodeDocs.cpp");
    }
    Require(described.size() == kinds.size(), "the descriptions name a node the registry does not have");

    const std::string project = Project("MaterialNodes");
    Require(Cli({ "material-new", "--project", project, "--name", "Probe" }).exitCode == 0, "material-new Probe");
    const Run index = Cli({ "material-graph", "nodes", "--project", project });
    Require(index.exitCode == 0, "nodes: " + index.All());
    for (const kb::render::RenderMaterialGraphNodeKind kind : kinds) {
        const std::string name{ kb::render::RenderMaterialGraphNodeKindName(kind) };
        Require(Contains(index.out, "\n  " + name + " - "), "the index must list " + name);
    }
    const Run group = Cli({ "material-graph", "nodes", "--group", "pattern" });
    Require(group.exitCode == 0 && Contains(group.out, "Noise") && !Contains(group.out, "Lerp"), "a group is listed alone: " + group.out);
    const Run noGroup = Cli({ "material-graph", "nodes", "--group", "Pattren" });
    Require(noGroup.exitCode == 1 && Contains(noGroup.err, "groups:"), "an unknown group names the groups: " + noGroup.err);

    std::size_t parsed = 0U;
    for (const kb::render::RenderMaterialGraphNodeKind kind : kinds) {
        const std::string name{ kb::render::RenderMaterialGraphNodeKindName(kind) };
        const Run page = Cli({ "material-graph", "node", "--kind", name });
        Require(page.exitCode == 0 && Contains(page.out, name + " (") && Contains(page.out, "Inputs: ") && Contains(page.out, "Outputs: ") && Contains(page.out, "Example: "),
            "the page of " + name + " must give its pins and an example: " + page.All());
        if (kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput) continue;
        // The example is a line of graph text: it must parse (some nodes only compile in another domain, which is no parse error).
        const std::size_t at = page.out.find("Example: ");
        const std::string example = page.out.substr(at + 9U, page.out.find('\n', at) - at - 9U);
        const Run applied = Graph(project, "/Game/Materials/Probe.kbmat", { "text", "--text", example });
        const bool accepted = applied.exitCode == 0 || Contains(applied.err, "the graph does not compile");
        const bool unwritable = Contains(applied.err, "cannot be written in graph text");
        Require(accepted || unwritable, "the example of " + name + " is not graph text the parser takes: " + example + "\n" + applied.All());
        parsed += accepted ? 1U : 0U;
    }
    // Every node but the output and the seven that text cannot write has an example the parser takes.
    Require(parsed + 8U == kinds.size(), "every example of a node text can write must parse: " + std::to_string(parsed) + " of " + std::to_string(kinds.size()));
    const Run wrong = Cli({ "material-graph", "node", "--kind", "Lerpp" });
    Require(wrong.exitCode == 1 && Contains(wrong.err, "did you mean Lerp"), "a misspelt kind names the nearest: " + wrong.err);
}

// A whole graph in one call, read back in the same form, with mistakes named where they are.
void RunGraphTextTests() {
    const std::string project = Project("MaterialText");
    const std::string sample =
        "Domain = surface\n"
        "Tint = ParameterColor #C9A227\n"
        "Scale = ParameterScalar 8\n"
        "// grain follows the surface\n"
        "Grain = Noise \"value false 1 1 2 2 0 1\"(value=Multiply(UV, Scale))\n"
        "Shift = Panner \"0.1 0\"(coordinate=UV)\n"
        "Output.BaseColor = Lerp(Tint, #2E2E2E, Grain)\n"
        "Output.Roughness = 0.4\n"
        "Output.Emissive = \"0 0 0\"\n"
        "Output.Metallic = Saturate(BreakVector(WorldPosition).y)\n";
    const Run made = Cli({ "material-new", "--project", project, "--name", "First", "--graph", sample });
    Require(made.exitCode == 0, "material-new --graph: " + made.All());
    const Run shown = Graph(project, "/Game/Materials/First.kbmat", { "show" });
    Require(shown.exitCode == 0 && Contains(shown.out, "Compiles: yes") && Contains(shown.out, "Tint = ParameterColor #C9A227") && Contains(shown.out, "Scale = ParameterScalar 8") &&
            Contains(shown.out, "Parameters: Tint, Scale") && Contains(shown.out, "Cost: about "),
        "show prints the graph as text with its parameters and cost: " + shown.All());
    Require(Contains(shown.out, "BreakVector(WorldPosition).y") || Contains(shown.out, "BreakVector(WorldPosition)"), "a nested node is written where it is used: " + shown.out);

    // The text round trip: show -> text -> show gives the same text.
    const std::string text = ShownText(shown.out);
    Require(Cli({ "material-new", "--project", project, "--name", "Second" }).exitCode == 0, "material-new Second");
    const Run again = Graph(project, "/Game/Materials/Second.kbmat", { "text", "--text", text });
    Require(again.exitCode == 0 && Contains(again.out, "set the graph of /Game/Materials/Second.kbmat"), "the printed text is accepted back: " + again.All() + "\n" + text);
    const std::string secondText = ShownText(Graph(project, "/Game/Materials/Second.kbmat", { "show" }).out);
    Require(secondText == text, "the text must read back unchanged:\n" + text + "\n---\n" + secondText);
    Require(Graph(project, "/Game/Materials/Second.kbmat", { "show" }).out.find("Compiles: yes") != std::string::npos, "the copy compiles");

    // A parameter set in text is the one an instance changes by its name.
    const Run instance = Cli({ "material-instance-new", "--project", project, "--parent", "/Game/Materials/First.kbmat", "--name", "FirstRed", "--set", "Tint=#FF0000", "--set", "Scale=20" });
    Require(instance.exitCode == 0 && Contains(instance.out, "Tint = 1 0 0 1 (override)"), "an instance sets a parameter of the text graph: " + instance.All());

    // Mistakes: the line, the node or the pin, and what would fit; nothing is changed.
    const std::string before = Read(fs::path{ project } / "Assets" / "Materials" / "First.kbmat");
    const auto refused = [&](const std::string& badText, std::initializer_list<std::string_view> parts) {
        const Run run = Graph(project, "/Game/Materials/First.kbmat", { "text", "--text", badText });
        Require(run.exitCode == 1 && Contains(run.err, "nothing was changed"), "the text must be refused: " + badText + "\n" + run.All());
        for (const std::string_view part : parts) Require(Contains(run.err, part), "the error for '" + badText + "' must say '" + std::string{ part } + "': " + run.err);
    };
    refused("A = Lerpp(1, 2, 3)", { "line 1:", "unknown node kind 'Lerpp'", "did you mean Lerp" });
    refused("Mix = Lerp(a=1, c=2)", { "line 1:", "Mix (Lerp) has no input 'c'", "inputs: a (float4), b (float4), t (float)" });
    refused("Mix = Lerp(a=1, b=2, t=0.5)\nOutput.BaseColor = Mixx", { "line 2:", "'Mixx' is neither a node defined above nor a node kind", "did you mean Mix" });
    refused("Pic = TextureSample(texture=Time)", { "line 1:", "Time output value (float) cannot connect to Pic (TextureSample) input texture (texture2D)" });
    refused("Grain = Noise \"perlin\"", { "line 1:", "'perlin'", "the value of Noise is: function turbulence scale" });
    refused("Add = Multiply(1, 2)\nAdd = Multiply(3, 4)", { "line 2:", "already used" });
    refused("Blend Mode = glass", { "line 1:", "Blend Mode does not take 'glass'", "translucent" });
    refused("Sum = Add(1, 2, 3)", { "line 1:", "has only 2 inputs" });
    refused("Output.Roughness = 0.5\nOutput.Roughness = 0.7", { "line 2:", "set twice" });
    refused("Output.Alpha = 2\nOutput.Colour = 1", { "line 2:", "has no input 'Colour'" });
    refused("Sum = Add(1 2)", { "line 1:" });
    refused("Output.BaseColor = #FF0000\nOutput.Roughness = ", { "line 2:", "a source is missing" });
    refused("Output.BaseColor = Time", { "line 1:", "float", "color", "Multiply" });
    Require(Read(fs::path{ project } / "Assets" / "Materials" / "First.kbmat") == before, "a refused text must leave the material as it was");

    // A graph that parses but does not compile is refused as a whole, naming the node.
    const Run noCompile = Cli({ "material-new", "--project", project, "--name", "Broken", "--graph", "Domain = postProcess\nOutput.BaseColor = 1" });
    Require(noCompile.exitCode == 1 && Contains(noCompile.err, "does not compile") && Contains(noCompile.err, "nothing was written") &&
            !fs::exists(fs::path{ project } / "Assets" / "Materials" / "Broken.kbmat"),
        "a graph that does not compile is not written: " + noCompile.All());
    // A graph with a node text cannot hold is never rewritten from text.
    Require(Cli({ "material-function", "new", "--project", project, "--name", "Edge", "--preset", "fresnel-glow" }).exitCode == 0, "a material function");
    Require(Cli({ "material-function", "use", "--project", project, "--material", "/Game/Materials/First.kbmat", "--function", "Edge" }).exitCode == 0, "a material function call");
    const Run kept = Graph(project, "/Game/Materials/First.kbmat", { "text", "--text", "Output.Roughness = 0.5" });
    Require(kept.exitCode == 1 && Contains(kept.err, "MaterialFunctionCall") && Contains(kept.err, "cannot be written in graph text"), "a graph with a function call is not replaced by text: " + kept.All());
    Require(Contains(Graph(project, "/Game/Materials/First.kbmat", { "show" }).out, "MaterialFunctionCall"), "show still names the function call");
}

// Every preset builds, none is too heavy for a phone (but the post-process ones read the screen), and no two share their words.
void RunLibraryTests() {
    const std::span<const kb::cli::material::MaterialPreset> presets = kb::cli::material::MaterialPresets();
    Require(presets.size() >= 150U, "the library holds about 150 presets: " + std::to_string(presets.size()));
    static const std::set<std::string_view> families{ "metal", "plastic", "wood", "stone", "ground", "fabric", "pattern", "emissive", "effect", "toon", "glass", "water", "scifi", "ui", "post" };
    std::set<std::string_view> names;
    std::vector<std::set<std::string>> words;
    for (const kb::cli::material::MaterialPreset& preset : presets) {
        Require(names.insert(preset.name).second, "the preset " + std::string{ preset.name } + " is listed twice");
        Require(families.contains(preset.family), "the preset " + std::string{ preset.name } + " has an unknown family " + std::string{ preset.family });
        const kb::cli::material::MaterialMaster* master = kb::cli::material::FindMaterialMaster(preset.master);
        Require(master != nullptr, "the preset " + std::string{ preset.name } + " names a master that does not exist");
        const std::vector<std::string> parameters = kb::cli::material::MasterParameters(*master);
        for (const std::string& setting : kb::cli::material::PresetSettings(preset)) {
            const std::string name = setting.substr(0U, setting.find('='));
            Require(setting.find('=') != std::string::npos && std::ranges::find(parameters, name) != parameters.end(),
                "the preset " + std::string{ preset.name } + " sets '" + setting + "', which the master " + std::string{ preset.master } + " does not expose");
        }
        std::set<std::string> set;
        for (const std::string& word : kb::script::SearchWords(std::string{ preset.name } + " " + std::string{ preset.tags })) set.insert(word);
        words.push_back(std::move(set));
    }
    // One look per job: two presets whose words are nearly the same would be two answers to one question.
    for (std::size_t left = 0U; left < words.size(); ++left) {
        for (std::size_t right = left + 1U; right < words.size(); ++right) {
            std::vector<std::string> shared;
            std::ranges::set_intersection(words[left], words[right], std::back_inserter(shared));
            const double similarity = static_cast<double>(shared.size()) / static_cast<double>(words[left].size() + words[right].size() - shared.size());
            Require(similarity < 0.7, "the presets " + std::string{ presets[left].name } + " and " + std::string{ presets[right].name } + " share their words (" + std::to_string(similarity) + ")");
        }
    }

    // Every preset is made in a project: its master compiles, its values fit, and it fits a phone unless it reads the screen.
    const std::string project = Project("MaterialLibrary");
    for (const kb::cli::material::MaterialPreset& preset : presets) {
        const Run made = Cli({ "material-new", "--project", project, "--preset", std::string{ preset.name } });
        Require(made.exitCode == 0 && Contains(made.out, "created /Game/Materials/") && Contains(made.out, "from the preset " + std::string{ preset.name }),
            "the preset " + std::string{ preset.name } + " must be made: " + made.All());
        const bool heavy = Contains(made.out, "too heavy for a phone");
        Require(heavy == (preset.family == "post"), std::string{ "the preset " } + std::string{ preset.name } + (heavy ? " is too heavy for a phone: " : " must warn that it reads the screen: ") + made.out);
    }
    for (const kb::cli::material::MaterialMaster& master : kb::cli::material::MaterialMasters()) {
        const Run shown = Graph(project, "/Game/Materials/Library/" + std::string{ master.name } + ".kbmat", { "show" });
        Require(shown.exitCode == 0 && Contains(shown.out, "Compiles: yes"), "the master " + std::string{ master.name } + " must compile: " + shown.All());
    }
    // One master serves many presets: an instance, not a copy of the graph.
    Require(fs::exists(fs::path{ project } / "Assets" / "Materials" / "Gold.kbmatinst") && !fs::exists(fs::path{ project } / "Assets" / "Materials" / "Gold.kbmat"), "a preset is an instance");
    Require(Cli({ "material-new", "--project", project, "--preset", "gold" }).exitCode == 1, "a second preset of the same name is refused, not overwritten");
    const Run other = Cli({ "material-new", "--project", project, "--preset", "gold", "--name", "Ring", "--set", "Color=#FF8800", "--set", "Roughness=0.1" });
    Require(other.exitCode == 0 && Contains(other.out, "Color = 1 0.2462") && Contains(other.out, "(override)") && Contains(other.out, "Roughness = 0.1 (override)"), "a preset takes other values: " + other.All());
    const Run badValue = Cli({ "material-new", "--project", project, "--preset", "gold", "--name", "Bad", "--set", "Sparkle=1" });
    Require(badValue.exitCode == 1 && Contains(badValue.err, "Sparkle") && Contains(badValue.err, "Roughness"), "an unknown parameter names the parameters: " + badValue.All());
    const Run badPreset = Cli({ "material-new", "--project", project, "--preset", "glod" });
    Require(badPreset.exitCode == 1 && Contains(badPreset.err, "did you mean gold"), "a misspelt preset names the nearest: " + badPreset.err);
}

// The customer's words find the look: names, synonyms and endings, three answers of different kinds before one kind repeats.
void RunSearchTests() {
    const auto search = [](const std::string& words) {
        const Run run = Cli({ "guide", "--search", words });
        Require(run.exitCode == 0, "guide --search: " + run.All());
        return run.out;
    };
    Require(Contains(search("shiny gold armour with scratches"), "preset gold ("), "gold armour finds gold");
    Require(Contains(search("golden"), "preset gold ("), "a synonym finds gold");
    Require(Contains(search("lava that glows and flows"), "preset lava ("), "lava is found by its words");
    Require(Contains(search("dissolve effect when the enemy dies"), "preset dissolve-"), "a dissolve effect is found");
    Require(Contains(search("see-through bottle"), "preset glass-"), "a bottle is glass");
    const std::string mixed = search("neon toon water");
    Require(Contains(mixed, "preset neon-") && Contains(mixed, "preset toon-") && Contains(mixed, "preset water-"), "three kinds of answer for three wishes: " + mixed);
    Require(std::ranges::count(mixed, '\n') <= 3, "at most three answers: " + mixed);
    Require(Contains(search("outline for a character"), "preset toon-"), "an outline is found as a toon look");
    Require(Contains(search("snow on top of the rocks"), "preset snow-on-"), "snow on top is found");
    Require(Contains(search("glitch effect"), "preset glitch-"), "a glitch is found");
    Require(Contains(search("wind in the grass"), "preset grass-wind"), "wind on plants is found");
    Require(Contains(search("a script that moves the player"), "guide "), "guides are found too");
    Require(Contains(search("lerp two colours with a mask"), "node Lerp"), "a node is found by what it does");
    Require(Contains(search("xyzzy"), "nothing fits"), "no match says so");
    Require(Contains(Cli({ "guide", "--search", "gold", "--json" }).out, "21kb.cli.guide-search/v1"), "the search has data for --json");
}

// A preset on an object of the scene and on a prefab asset, in the same command; the heavy-for-a-phone warning of a graph.
void RunApplyTests() {
    const std::string project = Project("MaterialApply");
    Require(Cli({ "entity", "add", "--project", project, "--create", "Cube", "--name", "Crate" }).exitCode == 0, "entity add");
    const Run onObject = Cli({ "material-new", "--project", project, "--preset", "oak", "--name", "CrateWood", "--entity", "Crate" });
    Require(onObject.exitCode == 0 && Contains(onObject.out, "updated MeshRenderer"), "a preset goes on an object in one command: " + onObject.All());
    const Run shown = Cli({ "entity", "show", "--project", project, "--name", "Crate" });
    Require(Contains(shown.out, "CrateWood.kbmatinst"), "the object shows the material: " + shown.out);
    // A look laid over an object keeps the colour the object has; --set wins over it.
    Require(Cli({ "entity", "add", "--project", project, "--create", "Capsule", "--name", "Hero", "--color", "#C0392B" }).exitCode == 0, "a red object");
    Require(Cli({ "material-new", "--project", project, "--preset", "toon-ink", "--name", "HeroInk", "--entity", "Hero" }).exitCode == 0, "an outline on it");
    const Run ink = Cli({ "material-instance-show", "--project", project, "--instance", "/Game/Materials/HeroInk.kbmatinst" });
    Require(Contains(ink.All(), "0.527") || Contains(ink.All(), "C0392B"), "the outline keeps the object's red: " + ink.All());
    Require(Cli({ "material-new", "--project", project, "--preset", "toon-ink", "--name", "HeroBlue", "--entity", "Hero", "--set", "Color=#0000FF" }).exitCode == 0, "an outline with a named colour");
    Require(!Contains(Cli({ "material-instance-show", "--project", project, "--instance", "/Game/Materials/HeroBlue.kbmatinst" }).All(), "0.527"), "a colour named with --set wins");
    const Run missing = Cli({ "material-new", "--project", project, "--preset", "pine", "--name", "Lost", "--entity", "Nobody" });
    Require(missing.exitCode == 1 && Contains(missing.err, "was made, but it could not be put on Nobody"), "a missing object is named: " + missing.All());

    Require(Cli({ "prefab", "new", "--project", project, "--from-entity", "Crate", "--name", "CratePrefab" }).exitCode == 0, "prefab new");
    const Run onPrefab = Cli({ "material-new", "--project", project, "--preset", "gold", "--name", "PrefabGold", "--prefab", "/Game/Prefabs/CratePrefab.kbprefab" });
    Require(onPrefab.exitCode == 0 && Contains(onPrefab.out, "of /Game/Prefabs/CratePrefab.kbprefab"), "a preset goes on a prefab: " + onPrefab.All());
    Require(Cli({ "prefab", "place", "--project", project, "--prefab", "/Game/Prefabs/CratePrefab.kbprefab", "--name", "Copy" }).exitCode == 0, "prefab place");
    Require(Contains(Cli({ "entity", "show", "--project", project, "--name", "Copy" }).out, "PrefabGold.kbmatinst"), "a copy of the prefab carries the material");
    const Run noPart = Cli({ "material-new", "--project", project, "--preset", "pine", "--name", "Lost2", "--prefab", "/Game/Prefabs/CratePrefab.kbprefab", "--entity", "Handle" });
    Require(noPart.exitCode == 1 && Contains(noPart.err, "has no part 'Handle'"), "a missing part of a prefab is named: " + noPart.All());

    // A prefab whose parts wear different materials is not overwritten whole: the part is named, and the others keep theirs.
    Require(Cli({ "entity", "add", "--project", project, "--create", "Cube", "--name", "Arm", "--parent", "Crate" }).exitCode == 0, "a child part");
    Require(Cli({ "material-new", "--project", project, "--preset", "gold", "--name", "ArmGold", "--entity", "Crate/Arm" }).exitCode == 0, "a material on the child");
    Require(Cli({ "prefab", "new", "--project", project, "--from-entity", "Crate", "--name", "TwoParts" }).exitCode == 0, "prefab with two parts");
    const Run whole = Cli({ "material-new", "--project", project, "--preset", "pine", "--name", "WholePine", "--prefab", "/Game/Prefabs/TwoParts.kbprefab" });
    Require(whole.exitCode == 1 && Contains(whole.err, "parts with different materials") && Contains(whole.err, "--entity <Part>"), "a mixed prefab is refused: " + whole.All());
    const Run onePart = Cli({ "material-new", "--project", project, "--preset", "pine", "--name", "ArmPine", "--prefab", "/Game/Prefabs/TwoParts.kbprefab", "--entity", "Arm" });
    Require(onePart.exitCode == 0 && Contains(onePart.out, "on 1 Mesh Renderer"), "the named part takes it: " + onePart.All());

    // A heavy graph warns; a light one does not.
    Require(Cli({ "material-new", "--project", project, "--name", "Heavy", "--graph", "Output.BaseColor = Noise(value=UV)\nOutput.Emissive = SceneColor" }).exitCode == 0, "a heavy graph");
    const Run heavy = Graph(project, "/Game/Materials/Heavy.kbmat", { "show" });
    Require(Contains(heavy.out, "Warning: too heavy for a phone") && Contains(heavy.out, "instructions") && Contains(heavy.out, "scene behind"), "a heavy graph warns: " + heavy.out);
    Require(!Contains(Graph(project, "/Game/Materials/Library/Pbr.kbmat", { "show" }).out, "too heavy"), "a light graph does not");
    // A blended material and a vertex offset the culling does not know about get a note, not a warning.
    Require(Cli({ "material-new", "--project", project, "--name", "Blended", "--graph", "Blend Mode = translucent\nOutput.Alpha = 0.5" }).exitCode == 0, "a translucent graph");
    Require(Contains(Graph(project, "/Game/Materials/Blended.kbmat", { "show" }).out, "Note: blending draws every layer"), "a blended material says what blending costs");
    Require(Contains(Cli({ "material-new", "--project", project, "--name", "Moves", "--graph", "Output.WorldPositionOffset = \"0 1 0\"" }).out, "Note: ") ||
            Contains(Graph(project, "/Game/Materials/Moves.kbmat", { "show" }).out, "Max World Position Offset Displacement is 0"),
        "a vertex offset without its displacement margin is named");

    // The preview refuses what it cannot draw, names what it needs, and leaves no scene behind.
    const Run noMaterial = Cli({ "material-preview", "--project", project, "--material", "/Game/Materials/Nope.kbmat" });
    Require(noMaterial.exitCode == 1 && Contains(noMaterial.err, "Nope.kbmat"), "a missing material is named: " + noMaterial.All());
    Require(Cli({ "material-new", "--project", project, "--preset", "post-sepia" }).exitCode == 0, "a post-process preset");
    const Run two = Cli({ "material-preview", "--project", project, "--material", "/Game/Materials/PostSepia.kbmat", "--material", "/Game/Materials/PrefabGold.kbmatinst" });
    Require(two.exitCode == 1 && Contains(two.err, "alone"), "a post-process material is previewed alone: " + two.All());
    const Run drawn = Cli({ "material-preview", "--project", project, "--material", "/Game/Materials/PrefabGold.kbmatinst", "--out", "Saved/preview.png" });
    Require(drawn.exitCode == 0 || Contains(drawn.err, "kb_game"), "the preview draws or names the player it needs: " + drawn.All());
    Require(!fs::exists(fs::path{ project } / "Assets" / "Scenes" / "MaterialPreviewTemporary.21kbscene"), "the preview leaves no scene behind");
}

} // namespace

void RunMaterialAgentTests() {
    RunNodeReferenceTests();
    RunGraphTextTests();
    RunLibraryTests();
    RunSearchTests();
    RunApplyTests();
}
