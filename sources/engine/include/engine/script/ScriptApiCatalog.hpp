#pragma once

#include "engine/script/ScriptApiNameRegistry.hpp"
#include "engine/script/ScriptValue.hpp"

#include <cstdint>
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

struct ScriptApiCatalog {
    std::vector<std::string> lifecycleEvents;
    std::vector<ScriptApiCatalogFunction> functions;
    std::vector<ScriptApiCatalogComponent> components;
    std::vector<ScriptApiCatalogLuaBinding> luaBindings;
    std::vector<ScriptApiNameEntry> projectEntries;

    [[nodiscard]] const ScriptApiCatalogFunction* FindFunction(std::string_view name) const noexcept;

    [[nodiscard]] static ScriptApiCatalog Build(const ScriptRuntimeHost& host);
    [[nodiscard]] static ScriptApiCatalog Build(const ScriptRuntimeHost& host, kb::assets::AssetManager& assets);
};

[[nodiscard]] const char* ToString(ScriptApiCatalogLuaReturnKind kind) noexcept;

} // namespace kb::script
