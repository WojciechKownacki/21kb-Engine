#include "agent/EditorAgentPanelState.hpp"

#include "engine/core/JsonValue.hpp"
#include "localization/EditorLocalization.hpp"
#include "project/EditorProjectPaths.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace kb::editor {
namespace {

using kb::core::JsonValue;

[[nodiscard]] std::wstring Widen(std::string_view text) {
#if defined(_WIN32)
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
#else
    return std::wstring(text.begin(), text.end());
#endif
}

void AppendUtf8(std::string& text, char32_t code) {
    if (code < 0x80U) {
        text.push_back(static_cast<char>(code));
    } else if (code < 0x800U) {
        text.push_back(static_cast<char>(0xC0U | (code >> 6U)));
        text.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    } else if (code < 0x10000U) {
        text.push_back(static_cast<char>(0xE0U | (code >> 12U)));
        text.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    } else {
        text.push_back(static_cast<char>(0xF0U | (code >> 18U)));
        text.push_back(static_cast<char>(0x80U | ((code >> 12U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    }
}

[[nodiscard]] std::filesystem::path SelectionFile() {
    const std::filesystem::path agents = EditorAgentsFile();
    return agents.empty() ? std::filesystem::path{} : agents.parent_path() / "AgentSelection.txt";
}

[[nodiscard]] std::string Replace(std::string text, std::string_view from, std::string_view to) {
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
    return text;
}

[[nodiscard]] const std::string& StringOf(const JsonValue* value) {
    static const std::string empty;
    return value != nullptr && value->GetKind() == JsonValue::Kind::String ? value->AsString() : empty;
}

// A value of a tool's input as one string; a list joins its words.
[[nodiscard]] std::string ValueText(const JsonValue* value) {
    if (value == nullptr) return {};
    if (value->GetKind() == JsonValue::Kind::Array) {
        std::string joined;
        for (std::size_t index = 0; index < value->Size(); ++index) {
            const std::string word = ValueText(value->At(index));
            if (!word.empty()) joined += (joined.empty() ? "" : " ") + word;
        }
        return joined;
    }
    if (value->GetKind() == JsonValue::Kind::Number) {
        const double number = value->AsNumber();
        return number == static_cast<double>(static_cast<long long>(number)) ? std::to_string(static_cast<long long>(number)) : std::to_string(number);
    }
    return StringOf(value);
}

// An engine operation as the agent asked for it: the kb_cli command, what it does to it, and
// its options, from either the generic kb_cli tool (its words) or one of the engine's own tools.
struct EngineCall {
    std::string command;
    std::string action;
    std::map<std::string, std::string> options;
    std::vector<std::string> words;
};

[[nodiscard]] EngineCall FromWords(const JsonValue* args) {
    EngineCall call;
    std::vector<std::string> words;
    if (args != nullptr && args->GetKind() == JsonValue::Kind::Array) {
        for (std::size_t index = 0; index < args->Size(); ++index) words.push_back(StringOf(args->At(index)));
    }
    if (!words.empty() && words.front() == "kb_cli") words.erase(words.begin());
    std::size_t at = 0U;
    if (at < words.size()) call.command = words[at++];
    // Subcommands are the plain words after the command ("param add", "ground new").
    while (at < words.size() && !words[at].starts_with("--") && call.action.size() < 24U && call.command != "help") {
        call.action += (call.action.empty() ? "" : " ") + words[at++];
    }
    for (; at < words.size(); ++at) {
        if (words[at].starts_with("--")) {
            const std::string key = words[at].substr(2U);
            const bool valued = at + 1U < words.size() && !words[at + 1U].starts_with("--");
            call.options[key] = valued ? words[++at] : std::string{};
        } else {
            call.words.push_back(words[at]);
        }
    }
    return call;
}

[[nodiscard]] EngineCall FromTool(const std::string& tool, const JsonValue* input) {
    static const std::map<std::string, std::string, std::less<>> commands{
        { "entity_edit", "entity" }, { "component_edit", "component" }, { "component_schema", "schema" },
        { "scene_list", "scene-list" }, { "scene_attach", "scene-attach" }, { "scene_create", "scene-new" },
        { "script_create", "script-new" }, { "run_scene", "run" }, { "import_asset", "import" },
        { "validate_script", "validate" }, { "visual_graph", "graph" }, { "blend_space", "blend-space" },
        { "api_reference", "api" }, { "guide", "guide" }, { "code", "code" },
    };
    EngineCall call;
    const auto found = commands.find(tool);
    call.command = found != commands.end() ? found->second : tool;
    if (input != nullptr && input->GetKind() == JsonValue::Kind::Object) {
        for (std::size_t index = 0; index < input->Size(); ++index) {
            call.options[input->MemberName(index)] = ValueText(input->At(index));
        }
    }
    if (const auto action = call.options.find("action"); action != call.options.end()) {
        call.action = action->second;
        call.options.erase(action);
    }
    return call;
}

[[nodiscard]] std::string Option(const EngineCall& call, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        if (const auto found = call.options.find(key); found != call.options.end() && !found->second.empty()) return found->second;
    }
    return {};
}

[[nodiscard]] std::string WithTarget(std::string text, const std::string& target) {
    if (!target.empty()) text += ": " + (target.size() > 70U ? target.substr(0U, 70U) + "..." : target);
    return text;
}

// What a kb_cli action does, in words; anything else changes what it works on.
[[nodiscard]] std::string_view VerbText(std::string_view action) {
    const auto any = [action](std::initializer_list<std::string_view> actions) { return std::ranges::find(actions, action) != actions.end(); };
    if (any({ "new", "make", "duplicate", "init", "presets" })) return EditorText("agent.step.create", "Creating");
    if (any({ "add", "add-node", "place", "attach", "socket", "emitter" })) return EditorText("agent.step.add", "Adding");
    if (any({ "remove", "detach", "delete", "disable" })) return EditorText("agent.step.remove", "Removing");
    if (any({ "show", "list", "info", "kinds", "catalog", "sample", "check", "verify", "view",
            "schema", "modules", "map", "find", "profile", "search", "chunks" })) return EditorText("agent.step.inspect", "Checking");
    if (any({ "build", "generate", "erode", "cook", "compress", "split", "patch", "migrate", "locomotion" })) return EditorText("agent.step.build", "Building");
    if (any({ "bake" })) return EditorText("agent.step.bake", "Baking");
    if (any({ "paint", "fill", "erase", "brush", "borderize" })) return EditorText("agent.step.paint", "Painting");
    if (any({ "connect", "disconnect", "assign", "rule" })) return EditorText("agent.step.connect", "Connecting");
    if (any({ "rename" })) return EditorText("agent.step.rename", "Renaming");
    if (any({ "export" })) return EditorText("agent.step.export", "Exporting");
    if (any({ "run" })) return EditorText("agent.step.run", "Running");
    return EditorText("agent.step.change", "Changing");
}

// What a kb_cli command works on, in words; an unknown one by its own name.
[[nodiscard]] std::string_view NounText(std::string_view noun) {
    if (noun == "entity") return EditorText("agent.thing.entity", "object");
    if (noun == "component") return EditorText("agent.thing.component", "component");
    if (noun == "scene") return EditorText("agent.thing.scene", "scene");
    if (noun == "material") return EditorText("agent.thing.material", "material");
    if (noun == "material-instance") return EditorText("agent.thing.materialInstance", "material instance");
    if (noun == "material-graph") return EditorText("agent.thing.materialGraph", "material graph");
    if (noun == "script") return EditorText("agent.thing.script", "script");
    if (noun == "prefab") return EditorText("agent.thing.prefab", "prefab");
    if (noun == "graph") return EditorText("agent.thing.graph", "graph");
    if (noun == "animator") return EditorText("agent.thing.animator", "animator");
    if (noun == "blend-space") return EditorText("agent.thing.blendSpace", "blend space");
    if (noun == "skeleton") return EditorText("agent.thing.skeleton", "skeleton");
    if (noun == "ability") return EditorText("agent.thing.ability", "ability");
    if (noun == "ai") return EditorText("agent.thing.ai", "AI");
    if (noun == "audio-mixer") return EditorText("agent.thing.audioMixer", "audio mixer");
    if (noun == "input-action") return EditorText("agent.thing.inputAction", "input action");
    if (noun == "terrain") return EditorText("agent.thing.terrain", "terrain");
    if (noun == "map2d") return EditorText("agent.thing.map2d", "2D map");
    if (noun == "csg") return EditorText("agent.thing.csg", "CSG shape");
    if (noun == "rig2d") return EditorText("agent.thing.rig2d", "2D rig");
    if (noun == "vector2d") return EditorText("agent.thing.vector2d", "vector shape");
    if (noun == "mesh-lod") return EditorText("agent.thing.meshLod", "mesh LODs");
    if (noun == "navmesh") return EditorText("agent.thing.navmesh", "navigation mesh");
    if (noun == "world") return EditorText("agent.thing.world", "world");
    if (noun == "post-process") return EditorText("agent.thing.postProcess", "post-process");
    if (noun == "scene-post-process") return EditorText("agent.thing.scenePostProcess", "scene post-process");
    if (noun == "vfx") return EditorText("agent.thing.vfx", "effect");
    if (noun == "vfx-emitter") return EditorText("agent.thing.vfxEmitter", "effect emitter");
    if (noun == "vfx-module") return EditorText("agent.thing.vfxModule", "effect module");
    if (noun == "import-preset") return EditorText("agent.thing.importPreset", "import preset");
    if (noun == "layout") return EditorText("agent.thing.layout", "editor layout");
    if (noun == "physics-layers") return EditorText("agent.thing.physicsLayers", "physics layers");
    if (noun == "lighting-quality") return EditorText("agent.thing.lightingQuality", "lighting quality");
    if (noun == "particle-process") return EditorText("agent.thing.particles", "particles");
    if (noun == "render-effect") return EditorText("agent.thing.renderEffect", "render effect");
    if (noun == "scene-attach") return EditorText("agent.thing.behaviour", "behaviour");
    if (noun == "package") return EditorText("agent.thing.package", "package");
    if (noun == "mod") return EditorText("agent.thing.mod", "mod");
    if (noun == "pack") return EditorText("agent.thing.pack", "pack");
    return noun;
}

// What an engine operation does, as a short sentence: "Adding entity: Ship/EngineLight".
[[nodiscard]] std::string DescribeEngineCall(EngineCall call) {
    const std::string& command = call.command;
    if (command == "guide") return WithTarget(std::string{ EditorText("agent.step.guide", "Reading the guide") }, Option(call, { "topic" }));
    if (command == "api") return WithTarget(std::string{ EditorText("agent.step.api", "Looking up the script API") }, Option(call, { "topic", "search" }));
    if (command == "schema") return WithTarget(std::string{ EditorText("agent.step.schema", "Looking up component properties") }, Option(call, { "component" }));
    if (command == "scene-list") return WithTarget(std::string{ EditorText("agent.step.sceneList", "Looking through the scene") }, Option(call, { "scene" }));
    if (command == "validate") return WithTarget(std::string{ EditorText("agent.step.validate", "Checking a script") }, Option(call, { "path" }) + (call.words.empty() ? "" : call.words.front()));
    if (command == "run") return WithTarget(std::string{ EditorText("agent.step.playScene", "Playing the scene to test it") }, Option(call, { "scene" }));
    if (command == "capture") return WithTarget(std::string{ EditorText("agent.step.capture", "Taking a picture of the scene") }, Option(call, { "scene" }));
    if (command == "help" || command == "commands") return WithTarget(std::string{ EditorText("agent.step.help", "Looking up a command") }, call.words.empty() ? std::string{} : call.words.front());
    if (command == "code") return WithTarget(std::string{ EditorText("agent.step.code", "Searching the engine's code") }, Option(call, { "topic", "text", "symbol", "path" }));
    if (command == "import") return WithTarget(std::string{ EditorText("agent.step.import", "Importing") }, Option(call, { "source" }) + (call.words.empty() ? "" : call.words.front()));
    if (command == "scene-lightmap" || command == "scene-bake-lighting") return WithTarget(std::string{ EditorText("agent.step.bakeLighting", "Baking the lighting") }, Option(call, { "scene" }));
    if (command == "project-settings") {
        const std::string set = Option(call, { "set" });
        return set.empty() ? WithTarget(std::string{ EditorText("agent.step.readProjectSettings", "Checking the project settings") }, {})
                           : WithTarget(std::string{ EditorText("agent.step.projectSettings", "Changing the project settings") }, set);
    }
    if (command == "build") return WithTarget(std::string{ EditorText("agent.step.buildGame", "Building the game") }, Option(call, { "target" }));

    // "material-new" is a material made; the verb is the last word of the command or its action.
    std::string noun = command;
    std::string action = call.action;
    static const std::vector<std::string> trailingVerbs{ "new", "set", "show", "add", "remove", "duplicate", "list", "attach", "build",
        "cook", "run", "export", "info", "check", "profile" };
    if (const std::size_t dash = noun.rfind('-'); dash != std::string::npos &&
        std::ranges::find(trailingVerbs, noun.substr(dash + 1U)) != trailingVerbs.end()) {
        if (action.empty()) action = noun.substr(dash + 1U);
        noun.resize(dash);
    }
    if (const std::size_t space = action.rfind(' '); space != std::string::npos) action = action.substr(space + 1U);
    std::string text{ VerbText(action) };
    text += " ";
    text += NounText(noun);
    std::string target = Option(call, { "entity", "name", "node", "material", "instance", "graph", "space", "controller", "scene", "path", "out" });
    if (command == "component") {
        const std::string type = Option(call, { "type" });
        const std::string entity = Option(call, { "entity" });
        target = type + (type.empty() || entity.empty() ? "" : " \xE2\x86\x92 ") + entity;
    } else if (command == "material-instance" || noun == "material-instance") {
        target = Option(call, { "name", "instance", "parent" });
    }
    if (target.empty() && !call.words.empty()) target = call.words.front();
    return WithTarget(std::move(text), target);
}

// A tool call as one short sentence of what it does; the agent's own file tools by the file.
[[nodiscard]] std::string DescribeTool(std::string name, const JsonValue* input) {
    constexpr std::string_view enginePrefix = "mcp__kb-engine__";
    if (name.starts_with(enginePrefix)) {
        name.erase(0, enginePrefix.size());
        return DescribeEngineCall(name == "kb_cli" ? FromWords(input != nullptr ? input->Find("args") : nullptr) : FromTool(name, input));
    }
    const auto file = [&](const char* key) {
        return std::filesystem::path{ StringOf(input != nullptr ? input->Find(key) : nullptr) }.filename().string();
    };
    if (name == "Read") return WithTarget(std::string{ EditorText("agent.step.read", "Reading") }, file("file_path"));
    if (name == "Write") return WithTarget(std::string{ EditorText("agent.step.write", "Writing") }, file("file_path"));
    if (name == "Edit" || name == "MultiEdit") return WithTarget(std::string{ EditorText("agent.step.edit", "Editing") }, file("file_path"));
    if (name == "Glob") return WithTarget(std::string{ EditorText("agent.step.glob", "Looking for files") }, StringOf(input != nullptr ? input->Find("pattern") : nullptr));
    if (name == "Grep") return WithTarget(std::string{ EditorText("agent.step.grep", "Searching the files for") }, StringOf(input != nullptr ? input->Find("pattern") : nullptr));
    return name;
}

// The image files a tool call names, by full path inside the project: what a capture or an
// import wrote, shown in the conversation when the call succeeds.
[[nodiscard]] std::vector<std::filesystem::path> ImagesNamed(const JsonValue* value, const std::filesystem::path& project) {
    std::vector<std::filesystem::path> images;
    if (value == nullptr) return images;
    if (value->GetKind() == JsonValue::Kind::Array || value->GetKind() == JsonValue::Kind::Object) {
        for (std::size_t index = 0; index < value->Size(); ++index) {
            for (std::filesystem::path& image : ImagesNamed(value->At(index), project)) images.push_back(std::move(image));
        }
        return images;
    }
    const std::string& text = StringOf(value);
    std::string extension = std::filesystem::path{ text }.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".png" && extension != ".jpg" && extension != ".jpeg" && extension != ".bmp") return images;
    std::filesystem::path path = std::filesystem::path{ reinterpret_cast<const char8_t*>(text.c_str()) };
    if (text.starts_with("/Game/")) path = project / "Assets" / std::filesystem::path{ reinterpret_cast<const char8_t*>(text.c_str() + 6) };
    else if (path.is_relative()) path = project / path;
    images.push_back(path.lexically_normal());
    return images;
}

// Agents answer in Markdown; the panel shows plain text, so the markers go and lists keep a bullet.
[[nodiscard]] std::string WithoutMarkdown(std::string text) {
    text = Replace(Replace(std::move(text), "**", ""), "__", "");
    std::erase(text, '`');
    std::string out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find('\n', start), text.size());
        std::string line = text.substr(start, end - start);
        std::size_t hashes = 0;
        while (hashes < line.size() && line[hashes] == '#') ++hashes;
        if (hashes > 0U && hashes < line.size() && line[hashes] == ' ') {
            line.erase(0, hashes + 1);
        } else if (line.starts_with("- ") || line.starts_with("* ")) {
            line = "\xE2\x80\xA2 " + line.substr(2);
        }
        out += line;
        if (end < text.size()) out += '\n';
        start = end + 1;
    }
    return out;
}

// The project's asset files and when each was written: enough to tell what a step added or
// changed, without comparing contents.
[[nodiscard]] std::map<std::filesystem::path, std::filesystem::file_time_type> ProjectSnapshot(const std::filesystem::path& project) {
    std::map<std::filesystem::path, std::filesystem::file_time_type> files;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(project / "Assets", std::filesystem::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error)) {
            files.emplace(it->path().lexically_normal(), it->last_write_time(error));
        }
    }
    return files;
}

// Character boundaries of a UTF-8 draft.
[[nodiscard]] std::size_t PreviousCharacter(const std::string& text, std::size_t at) noexcept {
    if (at == 0U) return 0U;
    --at;
    while (at > 0U && (static_cast<unsigned char>(text[at]) & 0xC0U) == 0x80U) --at;
    return at;
}

[[nodiscard]] std::size_t NextCharacter(const std::string& text, std::size_t at) noexcept {
    if (at >= text.size()) return text.size();
    ++at;
    while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xC0U) == 0x80U) ++at;
    return at;
}

// Letters and digits (any non-ASCII byte counts as a letter, so words with accents hold together).
[[nodiscard]] bool WordByte(char byte) noexcept {
    const unsigned char value = static_cast<unsigned char>(byte);
    return value >= 0x80U || std::isalnum(value) != 0 || value == '_';
}

[[nodiscard]] std::size_t WordLeft(const std::string& text, std::size_t at) noexcept {
    while (at > 0U && !WordByte(text[at - 1U])) at = PreviousCharacter(text, at);
    while (at > 0U && WordByte(text[at - 1U])) at = PreviousCharacter(text, at);
    return at;
}

[[nodiscard]] std::size_t WordRight(const std::string& text, std::size_t at) noexcept {
    while (at < text.size() && WordByte(text[at])) at = NextCharacter(text, at);
    while (at < text.size() && !WordByte(text[at])) at = NextCharacter(text, at);
    return at;
}

[[nodiscard]] std::size_t LineStart(const std::string& text, std::size_t at) noexcept {
    const std::size_t newline = at == 0U ? std::string::npos : text.rfind('\n', at - 1U);
    return newline == std::string::npos ? 0U : newline + 1U;
}

[[nodiscard]] std::size_t LineEnd(const std::string& text, std::size_t at) noexcept {
    return std::min(text.find('\n', at), text.size());
}

// The offset `characters` characters into the line that starts at `start`, or its end.
[[nodiscard]] std::size_t IntoLine(const std::string& text, std::size_t start, std::size_t characters) noexcept {
    const std::size_t end = LineEnd(text, start);
    std::size_t at = start;
    for (std::size_t count = 0U; count < characters && at < end; ++count) at = NextCharacter(text, at);
    return at;
}

[[nodiscard]] std::size_t CharactersBetween(const std::string& text, std::size_t from, std::size_t to) noexcept {
    std::size_t count = 0U;
    for (std::size_t at = from; at < to; at = NextCharacter(text, at)) ++count;
    return count;
}

[[nodiscard]] std::string PathText(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return { reinterpret_cast<const char*>(text.data()), text.size() };
}

[[nodiscard]] std::filesystem::path TextPath(const std::string& text) {
    return std::filesystem::path{ std::u8string{ reinterpret_cast<const char8_t*>(text.data()), text.size() } };
}

[[nodiscard]] const char* KindName(EditorAgentEntryKind kind) noexcept {
    switch (kind) {
    case EditorAgentEntryKind::User: return "user";
    case EditorAgentEntryKind::Agent: return "agent";
    case EditorAgentEntryKind::Tool: return "tool";
    case EditorAgentEntryKind::Error: return "error";
    case EditorAgentEntryKind::Image: return "image";
    case EditorAgentEntryKind::Info: break;
    }
    return "info";
}

[[nodiscard]] EditorAgentEntryKind KindFrom(const std::string& name) noexcept {
    if (name == "user") return EditorAgentEntryKind::User;
    if (name == "agent") return EditorAgentEntryKind::Agent;
    if (name == "tool") return EditorAgentEntryKind::Tool;
    if (name == "error") return EditorAgentEntryKind::Error;
    if (name == "image") return EditorAgentEntryKind::Image;
    return EditorAgentEntryKind::Info;
}

// A new conversation's file name: when it began, unique enough for one person's project.
[[nodiscard]] std::string NewConversationId() {
    static unsigned counter = 0U;
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now).count()) + "-" + std::to_string(counter++);
}

[[nodiscard]] JsonValue ToJson(const EditorAgentConversation& conversation, bool open) {
    JsonValue entries = JsonValue::MakeArray();
    for (const EditorAgentEntry& entry : conversation.entries) {
        JsonValue item = JsonValue::MakeObject();
        item.Set("kind", JsonValue::MakeString(KindName(entry.kind)));
        item.Set("text", JsonValue::MakeString(entry.text));
        if (entry.kind == EditorAgentEntryKind::Tool && entry.step == EditorAgentStepState::Failed) item.Set("failed", JsonValue::MakeBool(true));
        if (!entry.image.empty()) item.Set("image", JsonValue::MakeString(PathText(entry.image)));
        if (!entry.images.empty()) {
            JsonValue images = JsonValue::MakeArray();
            for (const std::filesystem::path& image : entry.images) images.Append(JsonValue::MakeString(PathText(image)));
            item.Set("images", std::move(images));
        }
        entries.Append(std::move(item));
    }
    JsonValue document = JsonValue::MakeObject();
    document.Set("title", JsonValue::MakeString(conversation.title));
    document.Set("open", JsonValue::MakeBool(open));
    document.Set("session", JsonValue::MakeString(conversation.session));
    document.Set("sessionAgent", JsonValue::MakeString(conversation.sessionAgent));
    document.Set("draft", JsonValue::MakeString(conversation.draft));
    document.Set("entries", std::move(entries));
    return document;
}

[[nodiscard]] bool ReadDocument(const std::filesystem::path& file, JsonValue& document) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) return false;
    std::stringstream text;
    text << stream.rdbuf();
    std::string error;
    return JsonValue::Parse(text.str(), document, error) && document.GetKind() == JsonValue::Kind::Object;
}

[[nodiscard]] std::unique_ptr<EditorAgentConversation> FromJson(const JsonValue& document, std::string id) {
    auto conversation = std::make_unique<EditorAgentConversation>();
    conversation->id = std::move(id);
    conversation->title = StringOf(document.Find("title"));
    conversation->session = StringOf(document.Find("session"));
    conversation->sessionAgent = StringOf(document.Find("sessionAgent"));
    conversation->draft = StringOf(document.Find("draft"));
    conversation->caret = conversation->anchor = conversation->draft.size();
    if (const JsonValue* entries = document.Find("entries"); entries != nullptr && entries->GetKind() == JsonValue::Kind::Array) {
        for (std::size_t index = 0; index < entries->Size(); ++index) {
            const JsonValue* item = entries->At(index);
            EditorAgentEntry entry{ KindFrom(StringOf(item->Find("kind"))), StringOf(item->Find("text")) };
            const JsonValue* failed = item->Find("failed");
            entry.step = failed != nullptr && failed->AsBool() ? EditorAgentStepState::Failed : EditorAgentStepState::Done;
            if (const std::string& image = StringOf(item->Find("image")); !image.empty()) entry.image = TextPath(image);
            if (const JsonValue* images = item->Find("images"); images != nullptr && images->GetKind() == JsonValue::Kind::Array) {
                for (std::size_t at = 0; at < images->Size(); ++at) entry.images.push_back(TextPath(StringOf(images->At(at))));
            }
            conversation->entries.push_back(std::move(entry));
        }
    }
    return conversation;
}

[[nodiscard]] std::string Shorten(std::string text, std::size_t limit = 300U) {
    if (text.size() > limit) {
        text.resize(limit);
        text += "...";
    }
    return text;
}

// A conversation's tab title: the start of its first request.
[[nodiscard]] std::string TitleFrom(const std::string& request) {
    std::string title = request.substr(0, request.find('\n'));
    if (title.size() > 32U) {
        std::size_t cut = 32U;
        while (cut > 0U && (static_cast<unsigned char>(title[cut]) & 0xC0U) == 0x80U) --cut;
        title = title.substr(0, cut) + "...";
    }
    return title;
}

// The line a request gets for its pictures: the agent opens them with its own file tool, so any
// agent that reads images sees them, and the path stays readable in the conversation's history.
[[nodiscard]] std::string AttachmentNote(const std::vector<std::filesystem::path>& images, const std::filesystem::path& project) {
    if (images.empty()) return {};
    std::string note = "\n\nAttached pictures - open each one with your file reading tool before you answer:";
    for (const std::filesystem::path& image : images) {
        std::error_code error;
        const std::filesystem::path relative = std::filesystem::relative(image, project, error);
        note += "\n" + PathText(!error && !relative.empty() && *relative.begin() != ".." ? relative : image);
    }
    return note;
}

[[nodiscard]] std::uint64_t Count(const JsonValue* value) {
    return value != nullptr ? static_cast<std::uint64_t>(std::max(0.0, value->AsNumber())) : 0U;
}

} // namespace

EditorAgentPanelState::EditorAgentPanelState() {
    NewConversation();
}

EditorAgentPanelState::~EditorAgentPanelState() {
    for (const std::unique_ptr<EditorAgentConversation>& conversation : conversations_) {
        if (conversation->saveDue) Save(*conversation);
    }
}

const EditorAgent* EditorAgentPanelState::Current() const noexcept {
    return current_ >= 0 && current_ < static_cast<int>(agents_.size()) ? &agents_[static_cast<std::size_t>(current_)] : nullptr;
}

const EditorAgentConversation& EditorAgentPanelState::Conversation(int index) const noexcept {
    return *conversations_[static_cast<std::size_t>(std::clamp(index, 0, ConversationCount() - 1))];
}

void EditorAgentPanelState::RefreshAgents() {
    std::string remembered = Current() != nullptr ? Current()->Label() : std::string{};
    std::string model = model_;
    std::string effort = effort_;
    if (remembered.empty()) {
        std::ifstream file(SelectionFile(), std::ios::binary);
        std::getline(file, remembered);
        std::getline(file, model);
        std::getline(file, effort);
    }
    prepared_ = true;
    agents_ = DetectEditorAgents();
    current_ = agents_.empty() ? -1 : 0;
    for (int index = 0; index < static_cast<int>(agents_.size()); ++index) {
        if (agents_[static_cast<std::size_t>(index)].Label() == remembered) {
            current_ = index;
            break;
        }
    }
    // A remembered model or level the agent does not offer falls back to its default.
    const EditorAgent* agent = Current();
    model_ = agent != nullptr && std::ranges::find(agent->models, model) != agent->models.end() ? model : std::string{};
    effort_ = agent != nullptr && std::ranges::find(agent->efforts, effort) != agent->efforts.end() ? effort : std::string{};
    StartAccountCheck();
}

void EditorAgentPanelState::Prepare() {
    UseProject(EditorProjectPaths::ProjectRoot());
    if (!prepared_) {
        // The first tab was made before the editor's language was chosen.
        for (const std::unique_ptr<EditorAgentConversation>& conversation : conversations_) {
            if (conversation->entries.empty()) {
                conversation->title = std::string{ EditorText("agent.newConversation", "New conversation") };
            }
        }
        RefreshAgents();
    }
    if (importedProject_ != historyProject_) {
        importedProject_ = historyProject_;
        ImportAgentSessions();
    }
}

void EditorAgentPanelState::SelectAgent(int index) {
    if (index < 0 || index >= static_cast<int>(agents_.size()) || index == current_) {
        return;
    }
    current_ = index;
    model_.clear();
    effort_.clear();
    RememberSelection();
    StartAccountCheck();
}

void EditorAgentPanelState::SelectModel(std::string model) {
    model_ = std::move(model);
    RememberSelection();
}

void EditorAgentPanelState::SelectEffort(std::string effort) {
    effort_ = std::move(effort);
    RememberSelection();
}

void EditorAgentPanelState::RememberSelection() const {
    const std::filesystem::path path = SelectionFile();
    if (path.empty() || Current() == nullptr) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << Current()->Label() << '\n' << model_ << '\n' << effort_ << '\n';
}

void EditorAgentPanelState::StartAccountCheck() {
    plan_.clear();
    signedIn_ = false;
    accountOutput_.clear();
    checkingAccount_ = false;
    const EditorAgent* agent = Current();
    if (agent == nullptr) {
        return;
    }
    if (agent->statusArguments.empty()) {
        // Nothing to ask: the agent is installed, and it will say so itself if it needs a sign-in.
        signedIn_ = true;
        return;
    }
    std::vector<std::wstring> arguments;
    for (const std::string& word : agent->statusArguments) {
        arguments.push_back(Widen(word));
    }
    accountCheck_ = std::make_unique<EditorAgentProcess>();
    std::string error;
    checkingAccount_ = accountCheck_->Start(agent->executable, arguments, {}, agent->environment, {}, error);
}

void EditorAgentPanelState::FinishAccountCheck() {
    checkingAccount_ = false;
    const EditorAgent* agent = Current();
    if (agent == nullptr) {
        return;
    }
    std::string text;
    for (const std::string& line : accountOutput_) {
        text += line;
        text += '\n';
    }
    if (agent->output == EditorAgentOutputFormat::ClaudeStreamJson) {
        JsonValue status;
        std::string error;
        if (JsonValue::Parse(text, status, error)) {
            const JsonValue* loggedIn = status.Find("loggedIn");
            signedIn_ = loggedIn != nullptr && loggedIn->AsBool();
            plan_ = StringOf(status.Find("subscriptionType"));
            if (!plan_.empty()) {
                plan_[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(plan_[0])));
            }
        }
        return;
    }
    // Codex prints one sentence: "Logged in using ChatGPT" or "Not logged in".
    signedIn_ = text.find("Logged in") != std::string::npos && text.find("Not logged in") == std::string::npos;
    if (const std::size_t at = text.find("using "); signedIn_ && at != std::string::npos) {
        plan_ = text.substr(at + 6U);
        while (!plan_.empty() && (plan_.back() == '\n' || plan_.back() == '\r')) plan_.pop_back();
    }
}

void EditorAgentPanelState::NewConversation() {
    auto conversation = std::make_unique<EditorAgentConversation>();
    conversation->id = NewConversationId();
    conversation->title = std::string{ EditorText("agent.newConversation", "New conversation") };
    conversations_.push_back(std::move(conversation));
    active_ = ConversationCount() - 1;
}

void EditorAgentPanelState::SelectConversation(int index) {
    if (index >= 0 && index < ConversationCount()) {
        active_ = index;
        pendingHighSurrogate_ = 0;
    }
}

void EditorAgentPanelState::CloseConversation(int index) {
    if (index < 0 || index >= ConversationCount()) {
        return;
    }
    EditorAgentConversation& closing = *conversations_[static_cast<std::size_t>(index)];
    if (closing.run != nullptr && closing.running) {
        closing.run->Stop();
    }
    // A closed tab stays in the history, to be opened again from the conversations menu.
    if (!closing.entries.empty() && !historyProject_.empty()) {
        std::error_code error;
        std::filesystem::create_directories(HistoryFolder(), error);
        std::ofstream file(HistoryFolder() / (closing.id + ".json"), std::ios::binary | std::ios::trunc);
        file << ToJson(closing, false).Dump();
    }
    conversations_.erase(conversations_.begin() + index);
    if (conversations_.empty()) {
        NewConversation();
    }
    active_ = std::clamp(active_ > index ? active_ - 1 : active_, 0, ConversationCount() - 1);
}

std::filesystem::path EditorAgentPanelState::HistoryFolder() const {
    return historyProject_.empty() ? std::filesystem::path{} : historyProject_ / "Saved" / "Agent" / "Conversations";
}

void EditorAgentPanelState::UseProject(const std::filesystem::path& project) {
    if (project.empty() || project == historyProject_) {
        return;
    }
    historyProject_ = project;
    // The tabs that were open when the project was last closed come back, oldest first; a blank
    // first tab gives way to them.
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> open;
    std::error_code error;
    for (std::filesystem::directory_iterator it(HistoryFolder(), error), end; !error && it != end; it.increment(error)) {
        if (it->path().extension() != ".json") continue;
        JsonValue document;
        if (ReadDocument(it->path(), document) && document.Find("open") != nullptr && document.Find("open")->AsBool()) {
            open.emplace_back(it->last_write_time(error), it->path());
        }
    }
    std::ranges::sort(open);
    for (const auto& [time, file] : open) {
        JsonValue document;
        if (ReadDocument(file, document)) {
            conversations_.push_back(FromJson(document, file.stem().string()));
        }
    }
    if (!open.empty() && conversations_.front()->entries.empty() && conversations_.front()->draft.empty() && !conversations_.front()->running) {
        conversations_.erase(conversations_.begin());
    }
    active_ = ConversationCount() - 1;
}

void EditorAgentPanelState::Save(EditorAgentConversation& conversation) {
    conversation.saveDue = false;
    conversation.savedAt = std::chrono::steady_clock::now();
    if (historyProject_.empty() || (conversation.entries.empty() && conversation.draft.empty())) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(HistoryFolder(), error);
    // Written beside and moved over, so a crash mid-write never loses the conversation.
    const std::filesystem::path file = HistoryFolder() / (conversation.id + ".json");
    const std::filesystem::path partial = HistoryFolder() / (conversation.id + ".json.partial");
    {
        std::ofstream stream(partial, std::ios::binary | std::ios::trunc);
        stream << ToJson(conversation, true).Dump();
        if (!stream) return;
    }
    std::filesystem::rename(partial, file, error);
}

void EditorAgentPanelState::Remember(EditorAgentConversation& conversation) {
    conversation.saveDue = true;
}

void EditorAgentPanelState::ImportAgentSessions() {
    if (historyProject_.empty()) {
        return;
    }
    // The sessions the history already holds.
    std::vector<std::string> known;
    for (const std::unique_ptr<EditorAgentConversation>& conversation : conversations_) {
        if (!conversation->session.empty()) known.push_back(conversation->session);
    }
    std::error_code error;
    for (std::filesystem::directory_iterator it(HistoryFolder(), error), end; !error && it != end; it.increment(error)) {
        JsonValue document;
        if (it->path().extension() == ".json" && ReadDocument(it->path(), document)) {
            if (const std::string& session = StringOf(document.Find("session")); !session.empty()) known.push_back(session);
        }
    }
    std::string folderName;
    for (const char8_t character : historyProject_.u8string()) {
        const bool plain = (character >= u8'a' && character <= u8'z') || (character >= u8'A' && character <= u8'Z') || (character >= u8'0' && character <= u8'9');
        folderName.push_back(plain ? static_cast<char>(character) : '-');
    }
    std::vector<std::pair<std::filesystem::path, std::string>> folders;
    for (const EditorAgent& agent : agents_) {
        if (!agent.sessionRecords.empty()) folders.emplace_back(agent.sessionRecords / folderName, agent.Label());
    }
    for (const auto& [folder, label] : folders) {
        for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
            const std::string session = it->path().stem().string();
            if (it->path().extension() != ".jsonl" || std::ranges::find(known, session) != known.end()) continue;
            known.push_back(session);
            EditorAgentConversation conversation;
            conversation.id = "session-" + session;
            conversation.session = session;
            conversation.sessionAgent = label;
            // No project of its own: reading an old session must not mark today's files as changed.
            std::ifstream stream(it->path(), std::ios::binary);
            for (std::string line; std::getline(stream, line);) {
                JsonValue event;
                std::string parseError;
                if (!line.empty() && line.front() == '{' && JsonValue::Parse(line, event, parseError)) {
                    const JsonValue* message = event.Find("message");
                    const JsonValue* content = message != nullptr ? message->Find("content") : nullptr;
                    // A request as typed, without the panel's notes for the agent; the rest as the
                    // panel shows a running conversation.
                    if (StringOf(event.Find("type")) == "user" && content != nullptr && content->GetKind() == JsonValue::Kind::String) {
                        std::string request = content->AsString();
                        if (request.starts_with("<")) continue;
                        request = request.substr(0U, request.find("\n\nAttached pictures - "));
                        if (conversation.title.empty()) conversation.title = TitleFrom(request);
                        conversation.entries.push_back({ EditorAgentEntryKind::User, std::move(request) });
                    } else {
                        ReadStreamJsonEvent(conversation, line);
                    }
                }
            }
            for (EditorAgentEntry& entry : conversation.entries) {
                if (entry.step == EditorAgentStepState::Running) entry.step = EditorAgentStepState::Done;
            }
            // A session with no request of the user (a sign-in check) is not a conversation.
            if (conversation.title.empty()) continue;
            std::filesystem::create_directories(HistoryFolder(), error);
            std::ofstream file(HistoryFolder() / (conversation.id + ".json"), std::ios::binary | std::ios::trunc);
            file << ToJson(conversation, false).Dump();
            error.clear();
        }
        error.clear();
    }
    pendingImages_.clear();
}

std::vector<EditorAgentSavedConversation> EditorAgentPanelState::SavedConversations() const {
    std::vector<std::pair<std::filesystem::file_time_type, EditorAgentSavedConversation>> found;
    std::error_code error;
    for (std::filesystem::directory_iterator it(HistoryFolder(), error), end; !error && it != end; it.increment(error)) {
        if (it->path().extension() != ".json") continue;
        JsonValue document;
        if (!ReadDocument(it->path(), document)) continue;
        const std::string id = it->path().stem().string();
        const bool open = std::ranges::any_of(conversations_, [&](const auto& conversation) { return conversation->id == id; });
        found.emplace_back(it->last_write_time(error), EditorAgentSavedConversation{ id, StringOf(document.Find("title")), open });
    }
    // Newest first, as a history reads.
    std::ranges::sort(found, [](const auto& left, const auto& right) { return left.first > right.first; });
    std::vector<EditorAgentSavedConversation> saved;
    for (auto& [time, conversation] : found) saved.push_back(std::move(conversation));
    return saved;
}

void EditorAgentPanelState::OpenSaved(const std::string& id) {
    for (int index = 0; index < ConversationCount(); ++index) {
        if (conversations_[static_cast<std::size_t>(index)]->id == id) {
            SelectConversation(index);
            return;
        }
    }
    JsonValue document;
    if (!ReadDocument(HistoryFolder() / (id + ".json"), document)) {
        return;
    }
    // Opened into the blank tab if that is all there is.
    if (ConversationCount() == 1 && conversations_.front()->entries.empty() && conversations_.front()->draft.empty() && !conversations_.front()->running) {
        conversations_.clear();
    }
    conversations_.push_back(FromJson(document, id));
    active_ = ConversationCount() - 1;
    Save(*conversations_.back());
}

void EditorAgentPanelState::FocusInput(bool focused) noexcept {
    inputFocused_ = focused;
    pendingHighSurrogate_ = 0;
}

void EditorAgentPanelState::TypeCharacter(wchar_t character) {
    if (character >= 0xD800 && character <= 0xDBFF) {
        pendingHighSurrogate_ = character;
        return;
    }
    std::string typed;
    if (character >= 0xDC00 && character <= 0xDFFF) {
        if (pendingHighSurrogate_ != 0) {
            AppendUtf8(typed, 0x10000U + ((static_cast<char32_t>(pendingHighSurrogate_) - 0xD800U) << 10U) +
                (static_cast<char32_t>(character) - 0xDC00U));
        }
    } else if (character >= 0x20 && character != 0x7F) {
        AppendUtf8(typed, static_cast<char32_t>(character));
    }
    pendingHighSurrogate_ = 0;
    if (!typed.empty()) {
        ReplaceSelection(typed, true);
    }
}

void EditorAgentPanelState::ReplaceSelection(std::string_view text, bool typing) {
    EditorAgentConversation& conversation = ActiveMutable();
    // One undo step per edit, a run of typed letters being one edit, capped so a long session does
    // not grow without end.
    if (!typing || !typingRun_ || text == " " || conversation.undo.empty()) {
        conversation.undo.push_back(conversation.draft);
        if (conversation.undo.size() > 200U) conversation.undo.erase(conversation.undo.begin());
    }
    typingRun_ = typing && text != " ";
    conversation.redo.clear();
    const std::size_t start = SelectionStart();
    conversation.draft.replace(start, SelectionEnd() - start, text);
    conversation.caret = conversation.anchor = start + text.size();
    Remember(conversation);
}

void EditorAgentPanelState::InsertText(std::string_view text) {
    std::string clean;
    for (const char character : text) {
        if (character == '\t') clean.push_back(' ');
        else if (character == '\n' || static_cast<unsigned char>(character) >= 0x20U) clean.push_back(character);
    }
    ReplaceSelection(clean);
}

void EditorAgentPanelState::SetInput(std::string text) {
    EditorAgentConversation& conversation = ActiveMutable();
    conversation.draft = std::move(text);
    conversation.caret = conversation.anchor = conversation.draft.size();
}

std::string EditorAgentPanelState::SelectedText() const {
    return Active().draft.substr(SelectionStart(), SelectionEnd() - SelectionStart());
}

void EditorAgentPanelState::Backspace(bool word) {
    EditorAgentConversation& conversation = ActiveMutable();
    if (conversation.caret == conversation.anchor) {
        // One whole character, however many UTF-8 bytes it took, or the word before the caret.
        conversation.anchor = word ? WordLeft(conversation.draft, conversation.caret) : PreviousCharacter(conversation.draft, conversation.caret);
    }
    if (conversation.caret != conversation.anchor) ReplaceSelection({});
}

void EditorAgentPanelState::Delete(bool word) {
    EditorAgentConversation& conversation = ActiveMutable();
    if (conversation.caret == conversation.anchor) {
        conversation.anchor = word ? WordRight(conversation.draft, conversation.caret) : NextCharacter(conversation.draft, conversation.caret);
    }
    if (conversation.caret != conversation.anchor) ReplaceSelection({});
}

void EditorAgentPanelState::MoveCaret(CaretMove move, bool extend) {
    EditorAgentConversation& conversation = ActiveMutable();
    const std::string& text = conversation.draft;
    std::size_t& caret = conversation.caret;
    // Without Shift, Left and Right first collapse a selection to its edge.
    if (!extend && caret != conversation.anchor && (move == CaretMove::Left || move == CaretMove::Right)) {
        caret = move == CaretMove::Left ? SelectionStart() : SelectionEnd();
        conversation.anchor = caret;
        return;
    }
    switch (move) {
    case CaretMove::Left: caret = PreviousCharacter(text, caret); break;
    case CaretMove::Right: caret = NextCharacter(text, caret); break;
    case CaretMove::WordLeft: caret = WordLeft(text, caret); break;
    case CaretMove::WordRight: caret = WordRight(text, caret); break;
    case CaretMove::LineStart: caret = LineStart(text, caret); break;
    case CaretMove::LineEnd: caret = LineEnd(text, caret); break;
    case CaretMove::Start: caret = 0U; break;
    case CaretMove::End: caret = text.size(); break;
    case CaretMove::Up:
    case CaretMove::Down: {
        const std::size_t start = LineStart(text, caret);
        const std::size_t column = CharactersBetween(text, start, caret);
        if (move == CaretMove::Up) {
            caret = start == 0U ? 0U : IntoLine(text, LineStart(text, start - 1U), column);
        } else {
            const std::size_t end = LineEnd(text, caret);
            caret = end >= text.size() ? text.size() : IntoLine(text, end + 1U, column);
        }
        break;
    }
    }
    if (!extend) conversation.anchor = caret;
}

void EditorAgentPanelState::PlaceCaret(std::size_t offset, bool extend) {
    EditorAgentConversation& conversation = ActiveMutable();
    offset = std::min(offset, conversation.draft.size());
    while (offset > 0U && offset < conversation.draft.size() && (static_cast<unsigned char>(conversation.draft[offset]) & 0xC0U) == 0x80U) --offset;
    conversation.caret = offset;
    if (!extend) conversation.anchor = offset;
}

void EditorAgentPanelState::SelectAll() noexcept {
    EditorAgentConversation& conversation = ActiveMutable();
    conversation.anchor = 0U;
    conversation.caret = conversation.draft.size();
}

void EditorAgentPanelState::Undo() {
    EditorAgentConversation& conversation = ActiveMutable();
    if (conversation.undo.empty()) return;
    conversation.redo.push_back(std::move(conversation.draft));
    conversation.draft = std::move(conversation.undo.back());
    conversation.undo.pop_back();
    conversation.caret = conversation.anchor = conversation.draft.size();
    Remember(conversation);
}

void EditorAgentPanelState::Redo() {
    EditorAgentConversation& conversation = ActiveMutable();
    if (conversation.redo.empty()) return;
    conversation.undo.push_back(std::move(conversation.draft));
    conversation.draft = std::move(conversation.redo.back());
    conversation.redo.pop_back();
    conversation.caret = conversation.anchor = conversation.draft.size();
    Remember(conversation);
}

void EditorAgentPanelState::Attach(std::filesystem::path image) {
    std::vector<std::filesystem::path>& attachments = ActiveMutable().attachments;
    if (std::ranges::find(attachments, image) == attachments.end()) {
        attachments.push_back(std::move(image));
    }
}

void EditorAgentPanelState::RemoveAttachment(std::size_t index) {
    std::vector<std::filesystem::path>& attachments = ActiveMutable().attachments;
    if (index < attachments.size()) {
        attachments.erase(attachments.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

bool EditorAgentPanelState::TakeProjectChanges(EditorAgentConversation& conversation) {
    if (conversation.project.empty()) return false;
    std::map<std::filesystem::path, std::filesystem::file_time_type> now = ProjectSnapshot(conversation.project);
    bool changed = false;
    for (const auto& [file, written] : now) {
        const auto before = conversation.projectFiles.find(file);
        if (before == conversation.projectFiles.end()) {
            changedFiles_[file] = EditorAgentFileChange::Added;
            changed = true;
        } else if (before->second != written) {
            changedFiles_.try_emplace(file, EditorAgentFileChange::Changed);
            changed = true;
        }
    }
    changed = changed || now.size() != conversation.projectFiles.size();
    conversation.projectFiles = std::move(now);
    return changed;
}

bool EditorAgentPanelState::AnyRunning() const noexcept {
    return std::ranges::any_of(conversations_, [](const auto& conversation) { return conversation->running; });
}

bool EditorAgentPanelState::Send(const EditorAgentWorkspace& workspace) {
    EditorAgentConversation& conversation = ActiveMutable();
    const EditorAgent* agent = Current();
    std::string request = conversation.draft;
    while (!request.empty() && (request.back() == ' ' || request.back() == '\n')) request.pop_back();
    if (conversation.running || (request.empty() && conversation.attachments.empty())) {
        return false;
    }
    const std::vector<std::filesystem::path> images = conversation.attachments;
    answerDismissed_ = false;
    if (agent == nullptr) {
        Add(EditorAgentEntryKind::Error, "No agent is installed. Install Claude Code, Codex, Gemini CLI, Qwen Code or Grok, "
            "or add your own from the agent menu, then choose Refresh.");
        return false;
    }

    // The engine's tools for agents that take an MCP configuration file on each run.
    const std::filesystem::path mcpConfig = workspace.project / "Saved" / "Agent" / "mcp.json";
    {
        std::error_code error;
        std::filesystem::create_directories(mcpConfig.parent_path(), error);
        JsonValue arguments = JsonValue::MakeArray();
        arguments.Append(JsonValue::MakeString("mcp"));
        arguments.Append(JsonValue::MakeString("--project"));
        arguments.Append(JsonValue::MakeString(workspace.project.generic_string()));
        JsonValue server = JsonValue::MakeObject();
        server.Set("command", JsonValue::MakeString(workspace.kbcli.generic_string()));
        server.Set("args", std::move(arguments));
        JsonValue servers = JsonValue::MakeObject();
        servers.Set("kb-engine", std::move(server));
        JsonValue document = JsonValue::MakeObject();
        document.Set("mcpServers", std::move(servers));
        std::ofstream file(mcpConfig, std::ios::binary | std::ios::trunc);
        file << document.Dump();
    }

    // The agent reads the limit with every request, so a mode holds for agents that have no switch for it.
    const std::string wire = (mode_ == EditorAgentMode::Plan
        ? std::string{ "Plan only. Do not change any file, scene or asset, and do not run anything that changes the project. Reply with the steps you would take.\n\n" }
        : mode_ == EditorAgentMode::Ask
            ? std::string{ "Answer only. Do not change any file, scene or asset, and do not run anything that changes the project.\n\n" }
            : std::string{}) + request + AttachmentNote(images, workspace.project);
    bool promptInArguments = false;
    const auto substitute = [&](const std::string& word) {
        std::string text = Replace(word, "{project}", workspace.project.generic_string());
        text = Replace(text, "{kbcli}", workspace.kbcli.generic_string());
        text = Replace(text, "{engine}", workspace.engine.generic_string());
        text = Replace(text, "{mcpconfig}", mcpConfig.generic_string());
        text = Replace(text, "{session}", conversation.session);
        text = Replace(text, "{model}", model_);
        text = Replace(text, "{effort}", effort_);
        text = Replace(text, "{permissionMode}", mode_ == EditorAgentMode::Agent ? "acceptEdits" : "plan");
        if (text.find("{prompt}") != std::string::npos) {
            promptInArguments = true;
            text = Replace(text, "{prompt}", wire);
        }
        return Widen(text);
    };
    // An agent that stays running gets the next request on its input, as long as it runs with the
    // same agent, model and reasoning level; otherwise it starts again and resumes the session.
    const bool persistent = !agent->requestTemplate.empty() && !promptInArguments;
    const std::string runKey = agent->Label() + "|" + model_ + "|" + effort_ + "|" + std::to_string(static_cast<int>(mode_));
    const std::string message = persistent ? Replace(agent->requestTemplate, "{request}", JsonValue::MakeString(wire).Dump()) : std::string{};
    if (persistent && conversation.run != nullptr && conversation.run->Alive() && conversation.runKey == runKey) {
        AddRequest(conversation, request, images, workspace.project);
        conversation.turnEnded = false;
        if (conversation.run->Write(message)) {
            conversation.running = true;
            conversation.requestRun = true;
            conversation.started = std::chrono::steady_clock::now();
            return true;
        }
        conversation.entries.pop_back(); // it ended meanwhile: start it again below, with the request
    }
    if (conversation.run != nullptr && conversation.run->Alive()) {
        conversation.run->Stop();
    }

    std::vector<std::wstring> arguments;
    const bool resuming = !conversation.session.empty() && conversation.sessionAgent == agent->Label();
    const bool resumeCommand = resuming && !agent->resumeRunArguments.empty();
    for (const std::string& word : resumeCommand ? agent->resumeRunArguments : agent->runArguments) {
        arguments.push_back(substitute(word));
    }
    if (persistent) {
        for (const std::string& word : agent->persistentArguments) arguments.push_back(substitute(word));
    }
    if (!model_.empty()) {
        for (const std::string& word : agent->modelArguments) arguments.push_back(substitute(word));
    }
    if (!effort_.empty()) {
        for (const std::string& word : agent->effortArguments) arguments.push_back(substitute(word));
    }
    if (resuming && !resumeCommand) {
        for (const std::string& word : agent->resumeArguments) arguments.push_back(substitute(word));
    }
    // A batch script (.cmd, .bat) runs through cmd.exe, which reads % ^ & | < > " in its command
    // line: a request in the arguments could be mangled or run commands, so such an agent must take
    // the request on standard input instead.
    const std::wstring extension = agent->executable.extension().wstring();
    if (promptInArguments && (_wcsicmp(extension.c_str(), L".cmd") == 0 || _wcsicmp(extension.c_str(), L".bat") == 0)) {
        Add(EditorAgentEntryKind::Error, agent->Label() + " is a batch script that takes the request on its command line, "
            "where cmd.exe would change it. Give it the request on standard input (leave {prompt} out of its arguments in Agents.json).");
        return false;
    }

    // kb_cli first on PATH, so an agent that runs commands itself finds the engine's tool too.
    std::vector<std::pair<std::wstring, std::wstring>> environment = agent->environment;
#if defined(_WIN32)
    std::wstring path(32767, L'\0');
    path.resize(GetEnvironmentVariableW(L"PATH", path.data(), static_cast<DWORD>(path.size())));
    environment.emplace_back(L"PATH", workspace.kbcli.parent_path().wstring() + L";" + path);
#endif

    AddRequest(conversation, request, images, workspace.project);
    conversation.turnEnded = false;
    conversation.run = std::make_unique<EditorAgentProcess>();
    std::string error;
    if (!conversation.run->Start(agent->executable, arguments, workspace.project, environment,
            persistent ? message : promptInArguments ? std::string{} : wire, error, persistent)) {
        Add(EditorAgentEntryKind::Error, agent->Label() + ": " + error + ".");
        return false;
    }
    conversation.runKey = persistent ? runKey : std::string{};
    conversation.runFormat = agent->output;
    conversation.running = true;
    conversation.requestRun = true;
    conversation.started = std::chrono::steady_clock::now();
    return true;
}

bool EditorAgentPanelState::ConnectEngineTools(const EditorAgentWorkspace& workspace) {
    EditorAgentConversation& conversation = ActiveMutable();
    const EditorAgent* agent = Current();
    if (agent == nullptr || agent->connectArguments.empty() || conversation.running) {
        return false;
    }
    std::vector<std::wstring> arguments;
    for (const std::string& word : agent->connectArguments) {
        std::string text = Replace(word, "{project}", workspace.project.generic_string());
        arguments.push_back(Widen(Replace(text, "{kbcli}", workspace.kbcli.generic_string())));
    }
    Add(EditorAgentEntryKind::Info, "Connecting the engine's tools to " + agent->Label() + "...");
    conversation.run = std::make_unique<EditorAgentProcess>();
    std::string error;
    if (!conversation.run->Start(agent->executable, arguments, workspace.project, agent->environment, {}, error)) {
        Add(EditorAgentEntryKind::Error, error + ".");
        return false;
    }
    conversation.runFormat = EditorAgentOutputFormat::PlainText;
    conversation.running = true;
    conversation.requestRun = false;
    conversation.sawError = false;
    conversation.started = std::chrono::steady_clock::now();
    return true;
}

void EditorAgentPanelState::Stop() {
    EditorAgentConversation& conversation = ActiveMutable();
    if (conversation.run != nullptr && conversation.running) {
        conversation.run->Stop();
        Add(EditorAgentEntryKind::Info, "Stopped.");
        conversation.sawError = true;
    }
}

void EditorAgentPanelState::AddRequest(EditorAgentConversation& conversation, const std::string& request,
    const std::vector<std::filesystem::path>& images, const std::filesystem::path& project) {
    if (conversation.entries.empty()) {
        conversation.title = TitleFrom(request.empty() ? std::string{ EditorText("agent.pictureRequest", "Picture") } : request);
    }
    EditorAgentEntry entry{ EditorAgentEntryKind::User, request };
    entry.images = images;
    conversation.entries.push_back(std::move(entry));
    conversation.draft.clear();
    conversation.caret = conversation.anchor = 0U;
    conversation.undo.clear();
    conversation.redo.clear();
    conversation.attachments.clear();
    conversation.scrollFromBottom = 0;
    conversation.sawError = false;
    conversation.project = project;
    conversation.projectFiles = ProjectSnapshot(project);
    conversation.requestStart = std::filesystem::file_time_type::clock::now();
    // The marks in Project Files show what the newest request did.
    changedFiles_.clear();
    Save(conversation);
}

void EditorAgentPanelState::FinishStep(EditorAgentConversation& conversation, const std::string& toolId, bool failed) {
    for (auto entry = conversation.entries.rbegin(); entry != conversation.entries.rend(); ++entry) {
        if (entry->kind == EditorAgentEntryKind::Tool && entry->toolId == toolId) {
            entry->step = failed ? EditorAgentStepState::Failed : EditorAgentStepState::Done;
            break;
        }
    }
    // A picture the step wrote (a capture of the scene) is shown under it.
    if (const auto named = pendingImages_.find(toolId); named != pendingImages_.end()) {
        for (const std::filesystem::path& image : failed ? std::vector<std::filesystem::path>{} : named->second) {
            std::error_code error;
            if (std::filesystem::is_regular_file(image, error) && std::filesystem::last_write_time(image, error) >= conversation.requestStart) {
                EditorAgentEntry entry{ EditorAgentEntryKind::Image, image.filename().string() };
                entry.image = image;
                conversation.entries.push_back(std::move(entry));
            }
        }
        pendingImages_.erase(named);
    }
    // Each step that changed the project is shown at once, not when the whole request ends.
    projectChangedNow_ = TakeProjectChanges(conversation) || projectChangedNow_;
}

void EditorAgentPanelState::Add(EditorAgentEntryKind kind, std::string text) {
    ActiveMutable().entries.push_back(EditorAgentEntry{ kind, std::move(text) });
    Remember(ActiveMutable());
}

bool EditorAgentPanelState::Pump(bool& projectChanged) {
    projectChanged = false;
    bool changed = false;
    if (accountCheck_ != nullptr && checkingAccount_) {
        for (std::string& line : accountCheck_->TakeLines()) {
            accountOutput_.push_back(std::move(line));
        }
        if (accountCheck_->ExitCode().has_value()) {
            FinishAccountCheck();
            changed = true;
        }
    }
    for (const auto& owned : conversations_) {
        EditorAgentConversation& conversation = *owned;
        if (conversation.run == nullptr || !conversation.running) {
            continue;
        }
        for (const std::string& line : conversation.run->TakeLines()) {
            ReadLine(conversation, line);
            changed = true;
            conversation.saveDue = true;
        }
        // A turn ends when the agent exits, or, for one that stays running, at its result event.
        const std::optional<int> code = conversation.turnEnded ? std::optional<int>{ 0 } : conversation.run->ExitCode();
        if (!code.has_value()) {
            continue;
        }
        conversation.turnEnded = false;
        conversation.running = false;
        changed = true;
        if (*code != 0 && !conversation.sawError) {
            conversation.entries.push_back({ EditorAgentEntryKind::Error, "The agent stopped with exit code " + std::to_string(*code) + "." });
        }
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - conversation.started).count();
        conversation.entries.push_back({ EditorAgentEntryKind::Info, "Finished in " + std::to_string(seconds) + " s." });
        // Steps left without a result were cut short.
        for (EditorAgentEntry& entry : conversation.entries) {
            if (entry.kind == EditorAgentEntryKind::Tool && entry.step == EditorAgentStepState::Running) {
                entry.step = *code == 0 && !conversation.sawError ? EditorAgentStepState::Done : EditorAgentStepState::Failed;
            }
        }
        if (conversation.requestRun) {
            projectChangedNow_ = TakeProjectChanges(conversation) || projectChangedNow_;
        }
        Save(conversation);
    }
    // While a request runs its history is written every few seconds; drafts and finished
    // conversations at once.
    const auto now = std::chrono::steady_clock::now();
    for (const auto& owned : conversations_) {
        if (owned->saveDue && (!owned->running || now - owned->savedAt > std::chrono::seconds{ 3 })) {
            Save(*owned);
        }
    }
    projectChanged = projectChangedNow_;
    projectChangedNow_ = false;
    return changed || projectChanged;
}

void EditorAgentPanelState::ReadLine(EditorAgentConversation& conversation, const std::string& line) {
    if (line.empty()) {
        return;
    }
    if (conversation.runFormat == EditorAgentOutputFormat::ClaudeStreamJson && line.front() == '{') {
        ReadStreamJsonEvent(conversation, line);
    } else if (conversation.runFormat == EditorAgentOutputFormat::CodexJson && line.front() == '{') {
        ReadCodexEvent(conversation, line);
    } else if (conversation.runFormat == EditorAgentOutputFormat::PlainText) {
        if (!conversation.entries.empty() && conversation.entries.back().kind == EditorAgentEntryKind::Agent) {
            conversation.entries.back().text += "\n" + line;
        } else {
            conversation.entries.push_back({ EditorAgentEntryKind::Agent, line });
        }
    } else if (line.find("Reading prompt from stdin") == std::string::npos) {
        conversation.entries.push_back({ EditorAgentEntryKind::Info, Shorten(line) });
    }
}

void EditorAgentPanelState::ReadStreamJsonEvent(EditorAgentConversation& conversation, const std::string& line) {
    JsonValue event;
    std::string error;
    if (!JsonValue::Parse(line, event, error)) {
        return;
    }
    const std::string& type = StringOf(event.Find("type"));
    if (type == "system" && StringOf(event.Find("subtype")) == "init") {
        conversation.session = StringOf(event.Find("session_id"));
        conversation.sessionAgent = Current() != nullptr ? Current()->Label() : std::string{};
        return;
    }
    const JsonValue* message = event.Find("message");
    const JsonValue* content = message != nullptr ? message->Find("content") : nullptr;
    // Each step of the agent reports what it sent the model; the latest step's prompt and reply
    // are what the conversation now fills of the context window.
    if (const JsonValue* usage = type == "assistant" && message != nullptr ? message->Find("usage") : nullptr; usage != nullptr) {
        const std::uint64_t filled = Count(usage->Find("input_tokens")) + Count(usage->Find("cache_read_input_tokens")) +
            Count(usage->Find("cache_creation_input_tokens")) + Count(usage->Find("output_tokens"));
        if (filled > 0U) {
            conversation.usage.contextTokens = filled;
        }
    }
    if ((type == "assistant" || type == "user") && content != nullptr && content->GetKind() == JsonValue::Kind::Array) {
        for (std::size_t index = 0; index < content->Size(); ++index) {
            const JsonValue* part = content->At(index);
            const std::string& kind = StringOf(part->Find("type"));
            if (type == "assistant" && kind == "text" && !StringOf(part->Find("text")).empty()) {
                conversation.entries.push_back({ EditorAgentEntryKind::Agent, WithoutMarkdown(StringOf(part->Find("text"))) });
            } else if (type == "assistant" && kind == "tool_use") {
                EditorAgentEntry entry{ EditorAgentEntryKind::Tool, DescribeTool(StringOf(part->Find("name")), part->Find("input")) };
                entry.toolId = StringOf(part->Find("id"));
                entry.step = EditorAgentStepState::Running;
                // Reading a picture is looking at it, not making one.
                if (StringOf(part->Find("name")) != "Read") {
                    if (std::vector<std::filesystem::path> images = ImagesNamed(part->Find("input"), conversation.project); !images.empty()) {
                        pendingImages_[entry.toolId] = std::move(images);
                    }
                }
                conversation.entries.push_back(std::move(entry));
            } else if (type == "user" && kind == "tool_result") {
                const JsonValue* failed = part->Find("is_error");
                FinishStep(conversation, StringOf(part->Find("tool_use_id")), failed != nullptr && failed->AsBool());
                if (failed != nullptr && failed->AsBool()) {
                    const JsonValue* body = part->Find("content");
                    std::string text = StringOf(body);
                    if (text.empty() && body != nullptr && body->GetKind() == JsonValue::Kind::Array && body->Size() > 0U) {
                        text = StringOf(body->At(0)->Find("text"));
                    }
                    // A refused or failed tool call is part of the agent's own work - it reads the
                    // reason and tries another way - so it is a quiet note, not an error of the request.
                    conversation.entries.push_back({ EditorAgentEntryKind::Info, Shorten(text.empty() ? std::string{ "A tool failed." } : text) });
                }
            }
        }
        return;
    }
    if (type == "result") {
        conversation.turnEnded = true;
        const JsonValue* failed = event.Find("is_error");
        if (failed != nullptr && failed->AsBool()) {
            conversation.entries.push_back({ EditorAgentEntryKind::Error, Shorten(StringOf(event.Find("result"))) });
            conversation.sawError = true;
        }
        // The run's totals: tokens the model processed over all its steps (every step reads the whole
        // conversation again, mostly from cache) and their cost. The context fill comes from the steps.
        if (const JsonValue* models = event.Find("modelUsage"); models != nullptr && models->GetKind() == JsonValue::Kind::Object) {
            for (std::size_t index = 0; index < models->Size(); ++index) {
                const JsonValue* model = models->At(index);
                conversation.usage.inputTokens += Count(model->Find("inputTokens")) + Count(model->Find("cacheReadInputTokens")) +
                    Count(model->Find("cacheCreationInputTokens"));
                conversation.usage.outputTokens += Count(model->Find("outputTokens"));
                conversation.usage.costUsd += model->Find("costUSD") != nullptr ? model->Find("costUSD")->AsNumber() : 0.0;
                if (index == 0U) {
                    conversation.usage.contextWindow = Count(model->Find("contextWindow"));
                }
            }
        }
    }
}

void EditorAgentPanelState::ReadCodexEvent(EditorAgentConversation& conversation, const std::string& line) {
    JsonValue event;
    std::string error;
    if (!JsonValue::Parse(line, event, error)) {
        return;
    }
    const std::string& type = StringOf(event.Find("type"));
    // The thread a follow-up request continues (codex exec resume <thread>).
    if (type == "thread.started") {
        conversation.session = StringOf(event.Find("thread_id"));
        conversation.sessionAgent = Current() != nullptr ? Current()->Label() : std::string{};
        return;
    }
    if (type == "turn.failed" || type == "error") {
        const JsonValue* failure = event.Find("error");
        std::string text = failure != nullptr ? StringOf(failure->Find("message")) : StringOf(event.Find("message"));
        conversation.entries.push_back({ EditorAgentEntryKind::Error, Shorten(text.empty() ? std::string{ "The agent reported an error." } : text) });
        conversation.sawError = true;
        return;
    }
    if (type == "turn.completed") {
        if (const JsonValue* usage = event.Find("usage"); usage != nullptr) {
            const std::uint64_t input = Count(usage->Find("input_tokens"));
            conversation.usage.inputTokens += input;
            conversation.usage.outputTokens += Count(usage->Find("output_tokens"));
            conversation.usage.contextTokens = input + Count(usage->Find("output_tokens"));
        }
        return;
    }
    if (type != "item.completed") {
        return;
    }
    const JsonValue* item = event.Find("item");
    if (item == nullptr) {
        return;
    }
    const std::string& kind = StringOf(item->Find("type"));
    if (kind == "agent_message") {
        conversation.entries.push_back({ EditorAgentEntryKind::Agent, WithoutMarkdown(StringOf(item->Find("text"))) });
    } else if (kind == "command_execution") {
        conversation.entries.push_back({ EditorAgentEntryKind::Tool, "$ " + Shorten(StringOf(item->Find("command")), 120U) });
        projectChangedNow_ = TakeProjectChanges(conversation) || projectChangedNow_;
    } else if (kind == "mcp_tool_call") {
        conversation.entries.push_back({ EditorAgentEntryKind::Tool,
            DescribeTool("mcp__kb-engine__" + StringOf(item->Find("tool")), item->Find("arguments")) });
        projectChangedNow_ = TakeProjectChanges(conversation) || projectChangedNow_;
    } else if (kind == "file_change") {
        std::string files;
        if (const JsonValue* changes = item->Find("changes"); changes != nullptr && changes->GetKind() == JsonValue::Kind::Array) {
            for (std::size_t index = 0; index < changes->Size(); ++index) {
                files += (files.empty() ? "" : ", ") + std::filesystem::path{ StringOf(changes->At(index)->Find("path")) }.filename().string();
            }
        }
        conversation.entries.push_back({ EditorAgentEntryKind::Tool, "edit  " + Shorten(files, 120U) });
    } else if (kind == "error") {
        conversation.entries.push_back({ EditorAgentEntryKind::Error, Shorten(StringOf(item->Find("message"))) });
    }
}

void EditorAgentPanelState::ScrollBy(int pixels, int maximum) noexcept {
    int& scroll = ActiveMutable().scrollFromBottom;
    scroll = std::clamp(scroll + pixels, 0, std::max(0, maximum));
}

} // namespace kb::editor
