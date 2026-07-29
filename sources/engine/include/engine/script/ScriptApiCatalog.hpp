#pragma once

#include "engine/script/ScriptApiNameRegistry.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptValue.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::assets {
class AssetManager;
}

namespace kb::script {

class ScriptRuntimeHost;

struct ScriptApiCatalogFunction {
    std::string name;
    std::string description;
    kb::core::ExecutionAffinity executionAffinity =
        kb::core::ExecutionAffinity::MainThread;
    std::vector<ScriptApiPin> inputs;
    std::vector<ScriptApiPin> outputs;
};

struct ScriptApiCatalogProperty {
    std::string name;
    ScriptValueType type = ScriptValueType::Void;
    bool writable = true;
};

struct ScriptApiCatalogComponent {
    std::string name;
    std::vector<ScriptApiCatalogProperty> properties;
};

// How a Lua sandbox wrapper hands the wrapped function's outputs back to the
// script. The wrappers in PucLuaFunctionApi do not all mirror CallFunction:
// some collapse to a single pin, some always build a table, and
// Transform.GetPosition consumes its "found" pin to return nil instead.
enum class ScriptApiCatalogLuaReturnKind : std::uint8_t {
    Default,      // CallFunction semantics: none, single value, or named table
    SingleOutput, // returns only the pin named by returnPin, drops the rest
    OutputsTable, // always returns a table with every output pin as a field
    GuardedTable, // nil when the returnPin guard is false; table of the rest otherwise
};

// A callable field the Lua sandbox exposes on a global table (or as a global
// function) that forwards to a registered script function. The pin contract is
// sourced from the registry entry named by functionName, so generated
// documentation cannot drift from the runtime as long as the binding list
// mirrors PucLuaFunctionApi::Attach.
struct ScriptApiCatalogLuaBinding {
    std::string tableName;
    std::string luaName;
    std::string functionName;
    ScriptApiCatalogLuaReturnKind returnKind = ScriptApiCatalogLuaReturnKind::Default;
    std::string returnPin;
};

// The authored Lua surface.  Runtime table installation and the exported API
// catalog both consume these definitions so the spelling and grouping of Lua
// module fields has one source of truth.
struct ScriptApiCatalogLuaBindingDefinition {
    std::string_view tableName;
    std::string_view luaName;
    std::string_view functionName;
    ScriptApiCatalogLuaReturnKind returnKind = ScriptApiCatalogLuaReturnKind::Default;
    std::string_view returnPin;
};

// A generated trace from a Visual Graph node pin to the callable catalog
// function, the live runtime binding which implements it, and the stable
// anchor in the generated API reference. One entry exists for every pin,
// including the execution pins added around CallNative nodes.
struct ScriptApiCatalogSourceMapEntry {
    std::string visualGraphNodeId;
    std::string visualGraphPinName;
    std::string visualGraphPinDirection;
    std::string visualGraphNodeCategory;
    std::string functionName;
    std::string runtimeBindingSymbol;
    std::string documentationAnchor;
};

struct ScriptApiCatalog {
    std::vector<std::string> lifecycleEvents;
    std::vector<ScriptApiCatalogFunction> functions;
    std::vector<ScriptApiCatalogComponent> components;
    std::vector<ScriptApiCatalogLuaBinding> luaBindings;
    std::vector<ScriptApiCatalogSourceMapEntry> sourceMap;
    std::vector<ScriptApiNameEntry> projectEntries;

    [[nodiscard]] const ScriptApiCatalogFunction* FindFunction(std::string_view name) const noexcept;

    [[nodiscard]] static ScriptApiCatalog Build(const ScriptRuntimeHost& host);
    [[nodiscard]] static ScriptApiCatalog Build(const ScriptRuntimeHost& host, kb::assets::AssetManager& assets);
    [[nodiscard]] static std::span<const ScriptApiCatalogLuaBindingDefinition> LuaBindingDefinitions() noexcept;
};

[[nodiscard]] const char* ToString(ScriptApiCatalogLuaReturnKind kind) noexcept;
// Produces the HTML id emitted for a catalog function's generated reference
// section. Function names are encoded so plugin-provided names are always
// safe to place in the documentation artifact.
[[nodiscard]] std::string ScriptApiDocumentationAnchor(std::string_view functionName);

} // namespace kb::script
