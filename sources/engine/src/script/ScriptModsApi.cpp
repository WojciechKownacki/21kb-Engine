#include "engine/script/ScriptModsApi.hpp"

#include "engine/mods/ModRuntime.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace kb::script {
namespace {

using Args = std::span<const ScriptFunctionArgument>;

const ScriptValue& Arg(Args args, std::string_view key) {
    static const ScriptValue empty;
    for (const auto& arg : args) {
        if (arg.name == key) return arg.value;
    }
    return empty;
}

ScriptFunctionCallResult Error(std::string message) {
    return { .executed = false, .errors = { std::move(message) } };
}

[[nodiscard]] ScriptValue Text(std::string text) {
    return ScriptValue{ std::move(text) };
}

ScriptFunctionCallResult Count(const ScriptFunctionCallContext&, Args) {
    return { .executed = true, .outputs = { { "count", ScriptValue{ static_cast<int>(kb::mods::PublishedLoadedMods().size()) } } } };
}

ScriptFunctionCallResult Get(const ScriptFunctionCallContext&, Args args) {
    const std::vector<kb::mods::LoadedMod> mods = kb::mods::PublishedLoadedMods();
    const std::int64_t index = Arg(args, "index").AsInt();
    if (index < 0 || static_cast<std::size_t>(index) >= mods.size()) {
        return Error("there is no loaded mod with that index; Mods.Count says how many there are");
    }
    const kb::mods::LoadedMod& mod = mods[static_cast<std::size_t>(index)];
    return { .executed = true, .outputs = {
        { "id", Text(mod.id) }, { "name", Text(mod.name) }, { "version", Text(mod.version) },
        { "author", Text(mod.author) }, { "entry", Text(mod.entryScene) } } };
}

ScriptFunctionCallResult IsLoaded(const ScriptFunctionCallContext&, Args args) {
    const std::string id = Arg(args, "id").AsString();
    const std::vector<kb::mods::LoadedMod> mods = kb::mods::PublishedLoadedMods();
    const auto found = std::ranges::find(mods, id, &kb::mods::LoadedMod::id);
    return { .executed = true, .outputs = {
        { "loaded", ScriptValue{ found != mods.end() } }, { "version", Text(found == mods.end() ? std::string{} : found->version) } } };
}

ScriptFunctionCallResult EntryScene(const ScriptFunctionCallContext&, Args args) {
    const std::string id = Arg(args, "id").AsString();
    const std::vector<kb::mods::LoadedMod> mods = kb::mods::PublishedLoadedMods();
    const auto found = std::ranges::find(mods, id, &kb::mods::LoadedMod::id);
    return { .executed = true, .outputs = { { "scene", Text(found == mods.end() ? std::string{} : found->entryScene) } } };
}

} // namespace

bool ScriptModsApi::Register(ScriptRuntimeHost& host) {
    using T = ScriptValueType;
    const auto add = [&](std::string name, std::string description, std::vector<ScriptFunctionPin> inputs, std::vector<ScriptFunctionPin> outputs,
                         ScriptFunctionCallback callback) {
        ScriptFunctionDesc desc;
        desc.signature.name = "Mods." + name;
        desc.signature.description = std::move(description);
        desc.signature.inputs = std::move(inputs);
        desc.signature.outputs = std::move(outputs);
        desc.callback = callback;
        return host.RegisterFunction(std::move(desc));
    };
    bool ok = add("Count", "How many mods the game has loaded.", {}, { { "count", T::Int, true } }, &Count);
    ok = add("Get", "The id, name, version, author and entry scene of a loaded mod, by index from 0 (the order the mods were mounted in).",
        { { "index", T::Int, true } },
        { { "id", T::String, true }, { "name", T::String, true }, { "version", T::String, true }, { "author", T::String, true }, { "entry", T::String, true } }, &Get) && ok;
    ok = add("IsLoaded", "Whether the mod with this id is loaded, and its version.", { { "id", T::String, true } },
        { { "loaded", T::Bool, true }, { "version", T::String, true } }, &IsLoaded) && ok;
    ok = add("EntryScene", "The entry scene a loaded mod names (a virtual path to pass to Scene.Load), or an empty string.", { { "id", T::String, true } },
        { { "scene", T::String, true } }, &EntryScene) && ok;
    return ok;
}

} // namespace kb::script
