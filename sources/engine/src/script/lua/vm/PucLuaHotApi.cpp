// The hot engine calls as plain C functions for LuaJIT's FFI (see PucLuaHotApi.hpp). They make the same engine calls as the
// C API bindings in script/lua/api/PucLuaFunctionApi.cpp and the registered functions, so a hot call and an ordinary one do
// the same thing. Which Lua function each one replaces, the checks on its arguments and the shape of its result are
// generated from that function's entry in the API catalog and its registered pins; nothing about a function is written
// twice but its C implementation.
#include "script/lua/LuaVm.hpp"
#include "script/lua/PucLuaHotApi.hpp"

#include "script/lua/PucLuaRuntimeApi.hpp"
#include "script/lua/PucLuaStateUtilities.hpp"

#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptInputApi.hpp"
#include "engine/script/ScriptPhysicsApi.hpp"
#include "engine/script/ScriptTimeApi.hpp"
#include "engine/script/ScriptTransformApi.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

// Every function is handed the slot of the runtime that runs the entry (the one the API closures read their context from)
// and writes the outputs of the registered function, in pin order, to `out`. It returns 0 when it could not run (no active
// context, an error): the Lua side then calls the original function, which reports it.
[[nodiscard]] ScriptExecutionContext* ActiveContext(void* slot) noexcept {
    return slot != nullptr ? static_cast<PucLuaStateLifetime*>(slot)->activeContext : nullptr;
}

template <typename Body>
int Run(void* slot, Body&& body) noexcept {
    ScriptExecutionContext* context = ActiveContext(slot);
    if (context == nullptr) return 0;
    try {
        return body(*context) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

[[nodiscard]] kb::scene::SceneEntity AsEntity(double id) noexcept { return kb::scene::SceneEntity{ static_cast<std::uint64_t>(id) }; }
[[nodiscard]] float AsFloat(double value) noexcept { return static_cast<float>(value); }
[[nodiscard]] double Flag(bool value) noexcept { return value ? 1.0 : 0.0; }

// ---- Transform -----------------------------------------------------------------------------------------------------

int HotGetPosition(void* slot, double entity, double* out) noexcept {
    return Run(slot, [&](ScriptExecutionContext& context) {
        float x = 0.0F;
        float y = 0.0F;
        float z = 0.0F;
        out[0] = Flag(ScriptTransformApi::GetLocalPosition(context.GetScene(), AsEntity(entity), x, y, z));
        out[1] = x;
        out[2] = y;
        out[3] = z;
        return true;
    });
}

int HotSetPosition(void* slot, double entity, double x, double y, double z, double* out) noexcept {
    return Run(slot, [&](ScriptExecutionContext& context) {
        out[0] = Flag(ScriptTransformApi::SetLocalPosition(context.GetScene(), AsEntity(entity), AsFloat(x), AsFloat(y), AsFloat(z)));
        return true;
    });
}

int HotTranslate(void* slot, double entity, double x, double y, double z, double* out) noexcept {
    return Run(slot, [&](ScriptExecutionContext& context) {
        out[0] = Flag(ScriptTransformApi::TranslateLocalPosition(context.GetScene(), AsEntity(entity), AsFloat(x), AsFloat(y), AsFloat(z)));
        return true;
    });
}

int HotGetLocalRotation(void* slot, double entity, double* out) noexcept {
    return Run(slot, [&](ScriptExecutionContext& context) {
        kb::scene::Quat rotation{};
        out[0] = Flag(ScriptTransformApi::GetLocalQuaternion(context.GetScene(), AsEntity(entity), rotation.x, rotation.y, rotation.z, rotation.w));
        out[1] = rotation.x;
        out[2] = rotation.y;
        out[3] = rotation.z;
        out[4] = rotation.w;
        return true;
    });
}

int HotSetLocalRotation(void* slot, double entity, double x, double y, double z, double w, double* out) noexcept {
    return Run(slot, [&](ScriptExecutionContext& context) {
        out[0] = Flag(ScriptTransformApi::SetLocalQuaternion(context.GetScene(), AsEntity(entity), AsFloat(x), AsFloat(y), AsFloat(z), AsFloat(w)));
        return true;
    });
}

int HotGetRotation(void* slot, double entity, double* out) noexcept {
    return Run(slot, [&](ScriptExecutionContext& context) {
        float x = 0.0F;
        float y = 0.0F;
        float z = 0.0F;
        out[0] = Flag(ScriptTransformApi::GetRotationDegrees(context.GetScene(), AsEntity(entity), x, y, z));
        out[1] = x;
        out[2] = y;
        out[3] = z;
        return true;
    });
}

int HotSetRotation(void* slot, double entity, double x, double y, double z, double* out) noexcept {
    return Run(slot, [&](ScriptExecutionContext& context) {
        out[0] = Flag(ScriptTransformApi::SetRotationDegrees(context.GetScene(), AsEntity(entity), AsFloat(x), AsFloat(y), AsFloat(z)));
        return true;
    });
}

// ---- Input ---------------------------------------------------------------------------------------------------------

template <bool (kb::input::InputSubsystem::*Query)(std::string_view) const>
int HotAction(void* slot, const char* action, int player, double* out) noexcept {
    return Run(slot, [&](ScriptExecutionContext& context) {
        out[0] = Flag((context.GetScene().Input(ScriptInputApi::UserForPlayer(player)).*Query)(std::string_view{ action }));
        return true;
    });
}

int HotActionBool(void* slot, const char* action, int player, double* out) noexcept {
    return Run(slot, [&](ScriptExecutionContext& context) {
        out[0] = Flag(ScriptInputApi::ActionValue(context.GetScene(), player, std::string_view{ action }).AsBool());
        return true;
    });
}

int HotActionFloat(void* slot, const char* action, int player, double* out) noexcept {
    return Run(slot, [&](ScriptExecutionContext& context) {
        out[0] = static_cast<double>(ScriptInputApi::ActionValue(context.GetScene(), player, std::string_view{ action }).AsAxis1D());
        return true;
    });
}

int HotAction2D(void* slot, const char* action, int player, double* out) noexcept {
    return Run(slot, [&](ScriptExecutionContext& context) {
        const kb::input::InputValue value = ScriptInputApi::ActionValue(context.GetScene(), player, std::string_view{ action });
        out[0] = static_cast<double>(value.x);
        out[1] = static_cast<double>(value.y);
        return true;
    });
}

int HotAction3D(void* slot, const char* action, int player, double* out) noexcept {
    return Run(slot, [&](ScriptExecutionContext& context) {
        const kb::input::InputValue value = ScriptInputApi::ActionValue(context.GetScene(), player, std::string_view{ action });
        out[0] = static_cast<double>(value.x);
        out[1] = static_cast<double>(value.y);
        out[2] = static_cast<double>(value.z);
        return true;
    });
}

// ---- Time ----------------------------------------------------------------------------------------------------------

int HotDelta(void* slot, double* out) noexcept {
    return Run(slot, [&](ScriptExecutionContext& context) {
        out[0] = static_cast<double>(ScriptTimeApi::ScaledDelta(&context.GetScene(), context.DeltaSeconds()));
        return true;
    });
}

// ---- Physics -------------------------------------------------------------------------------------------------------

int HotRaycast(void* slot, double originX, double originY, double originZ, double directionX, double directionY, double directionZ,
    double distance, int layerMask, double* out) noexcept {
    return Run(slot, [&](ScriptExecutionContext& context) {
        ScriptRaycastHit hit;
        if (ScriptPhysicsApi::Raycast(context.GetScene(), AsFloat(originX), AsFloat(originY), AsFloat(originZ), AsFloat(directionX),
                AsFloat(directionY), AsFloat(directionZ), AsFloat(distance), ScriptPhysicsApi::LayerMaskFromInt(layerMask), hit) != nullptr) {
            return false;
        }
        out[0] = Flag(hit.hit);
        out[1] = static_cast<double>(hit.entity);
        out[2] = hit.distance;
        out[3] = hit.x;
        out[4] = hit.y;
        out[5] = hit.z;
        out[6] = hit.normalX;
        out[7] = hit.normalY;
        out[8] = hit.normalZ;
        return true;
    });
}

template <kb::scene::PhysicsVectorResult (*Read)(kb::scene::Scene&, kb::scene::SceneEntity) noexcept>
int HotVectorRead(void* slot, double entity, double* out) noexcept {
    return Run(slot, [&](ScriptExecutionContext& context) {
        const kb::scene::PhysicsVectorResult result = Read(context.GetScene(), AsEntity(entity));
        out[0] = Flag(result.found);
        out[1] = result.value.x;
        out[2] = result.value.y;
        out[3] = result.value.z;
        return true;
    });
}

template <bool (*Write)(kb::scene::Scene&, kb::scene::SceneEntity, kb::scene::Vec3) noexcept>
int HotVectorWrite(void* slot, double entity, double x, double y, double z, double* out) noexcept {
    return Run(slot, [&](ScriptExecutionContext& context) {
        out[0] = Flag(Write(context.GetScene(), AsEntity(entity), kb::scene::Vec3{ AsFloat(x), AsFloat(y), AsFloat(z) }));
        return true;
    });
}

// ---- The bindings --------------------------------------------------------------------------------------------------

// The C type text of a function type, to compare with the one the pins of its registered function call for.
template <typename T> struct CTypeName;
template <> struct CTypeName<int> { static constexpr const char* value = "int"; };
template <> struct CTypeName<double> { static constexpr const char* value = "double"; };
template <> struct CTypeName<double*> { static constexpr const char* value = "double*"; };
template <> struct CTypeName<void*> { static constexpr const char* value = "void*"; };
template <> struct CTypeName<const char*> { static constexpr const char* value = "const char*"; };

template <typename Result, typename... Arguments>
std::string SignatureText(Result (*)(Arguments...) noexcept) {
    std::string text = std::string{ CTypeName<Result>::value } + "(";
    const char* separator = "";
    ((text += separator + std::string{ CTypeName<Arguments>::value }, separator = ","), ...);
    return text + ")";
}

// How many outputs a call can write (the length of the shared result array).
constexpr std::size_t kMaxOutputs = 16;

// One Lua function of the API catalog served by a C function. `defaults` are the values of its optional inputs left out of
// a call, in order.
struct HotBinding {
    std::string_view table;
    std::string_view name;
    void* function;
    std::string signature;
    std::vector<double> defaults;
};

template <typename Function>
HotBinding Bind(std::string_view table, std::string_view name, Function* function, std::vector<double> defaults = {}) {
    return HotBinding{ table, name, reinterpret_cast<void*>(function), SignatureText(function), std::move(defaults) };
}

[[nodiscard]] std::vector<HotBinding> Bindings() {
    using Input = kb::input::InputSubsystem;
    using Backend = kb::scene::PhysicsBackend;
    const std::vector<double> player{ 0.0 };
    const std::vector<double> ray{ 1000.0, static_cast<double>(kb::scene::kPhysicsAllLayers) };
    return {
        Bind("Time", "delta", &HotDelta),
        Bind("Time", "Delta", &HotDelta),
        Bind("Transform", "GetPosition", &HotGetPosition),
        Bind("Transform", "GetPositionXYZ", &HotGetPosition),
        Bind("Transform", "LocalPosition", &HotGetPosition),
        Bind("Transform", "SetPosition", &HotSetPosition),
        Bind("Transform", "SetLocalPosition", &HotSetPosition),
        Bind("Transform", "Translate", &HotTranslate),
        Bind("Transform", "LocalRotation", &HotGetLocalRotation),
        Bind("Transform", "SetLocalRotation", &HotSetLocalRotation),
        Bind("Transform", "Rotation", &HotGetRotation),
        Bind("Transform", "SetRotation", &HotSetRotation),
        Bind("Input", "IsPressed", &HotAction<&Input::IsActionPressed>, player),
        Bind("Input", "Held", &HotAction<&Input::IsActionPressed>, player),
        Bind("Input", "WasPressed", &HotAction<&Input::WasActionStarted>, player),
        Bind("Input", "Pressed", &HotAction<&Input::WasActionStarted>, player),
        Bind("Input", "WasReleased", &HotAction<&Input::WasActionReleased>, player),
        Bind("Input", "Released", &HotAction<&Input::WasActionReleased>, player),
        Bind("Input", "ActionBool", &HotActionBool, player),
        Bind("Input", "Value", &HotActionFloat, player),
        Bind("Input", "ActionFloat", &HotActionFloat, player),
        Bind("Input", "Vector2", &HotAction2D, player),
        Bind("Input", "Vector2XY", &HotAction2D, player),
        Bind("Input", "Action2D", &HotAction2D, player),
        Bind("Input", "Vector3", &HotAction3D, player),
        Bind("Physics", "Raycast", &HotRaycast, ray),
        Bind("Physics", "GetVelocity", &HotVectorRead<&Backend::GetVelocity>),
        Bind("Physics", "GetAngularVelocity", &HotVectorRead<&Backend::GetAngularVelocity>),
        Bind("Physics", "SetVelocity", &HotVectorWrite<&Backend::SetVelocity>),
        Bind("Physics", "SetAngularVelocity", &HotVectorWrite<&Backend::SetAngularVelocity>),
    };
}

// A binding together with what the catalog and the registry say about the function it serves.
struct Resolved {
    const HotBinding* binding = nullptr;
    const ScriptFunctionSignature* signature = nullptr;
    ScriptApiCatalogLuaReturnKind kind = ScriptApiCatalogLuaReturnKind::Default;
    std::string_view returnPin;
};

[[nodiscard]] const char* CType(ScriptValueType type) noexcept {
    switch (type) {
    case ScriptValueType::Entity:
    case ScriptValueType::Float:
    case ScriptValueType::Double:
        return "double";
    case ScriptValueType::Int:
    case ScriptValueType::Bool:
        return "int";
    case ScriptValueType::String:
        return "const char*";
    default:
        return nullptr;
    }
}

// The C signature the pins of a function call for: the slot, its inputs, and the array its outputs are written to.
[[nodiscard]] std::string PinSignature(const ScriptFunctionSignature& signature) {
    std::string text = "int(void*";
    for (const ScriptFunctionPin& input : signature.inputs) {
        const char* type = CType(input.type);
        text += type != nullptr ? std::string{ "," } + type : ",?";
    }
    return text + (signature.outputs.empty() ? ")" : ",double*)");
}

[[nodiscard]] std::optional<std::size_t> OutputIndex(const ScriptFunctionSignature& signature, std::string_view pin) {
    for (std::size_t index = 0; index < signature.outputs.size(); ++index) {
        if (signature.outputs[index].name == pin) return index;
    }
    return std::nullopt;
}

// Finds the catalog entry of the binding (an authored Lua binding, or the plain "Table.Name" one of a registered function)
// and its registered signature. Nothing is returned, and the reason added to `problems`, when they do not fit the binding.
[[nodiscard]] std::optional<Resolved> Resolve(const HotBinding& binding, const ScriptFunctionRegistry& registry, std::vector<std::string>& problems) {
    const std::string label = std::string{ binding.table } + "." + std::string{ binding.name };
    Resolved resolved{ .binding = &binding };
    std::string functionName = label;
    for (const ScriptApiCatalogLuaBindingDefinition& definition : ScriptApiCatalog::LuaBindingDefinitions()) {
        if (definition.tableName == binding.table && definition.luaName == binding.name) {
            functionName = std::string{ definition.functionName };
            resolved.kind = definition.returnKind;
            resolved.returnPin = definition.returnPin;
            break;
        }
    }
    resolved.signature = registry.FindSignature(functionName);
    if (resolved.signature == nullptr || ScriptApiCatalog::GenericLuaTable(functionName).empty()) {
        problems.push_back(label + " is not a function of the API catalog");
        return std::nullopt;
    }
    if (binding.signature != PinSignature(*resolved.signature)) {
        problems.push_back(label + " has the C signature " + binding.signature + ", its pins call for " + PinSignature(*resolved.signature));
        return std::nullopt;
    }
    std::size_t optional = 0;
    for (const ScriptFunctionPin& input : resolved.signature->inputs) optional += input.required ? 0U : 1U;
    if (optional != binding.defaults.size()) {
        problems.push_back(label + " has " + std::to_string(binding.defaults.size()) + " defaults for " + std::to_string(optional) + " optional inputs");
        return std::nullopt;
    }
    if (resolved.signature->outputs.size() > kMaxOutputs) {
        problems.push_back(label + " has more outputs than the result array holds");
        return std::nullopt;
    }
    const bool guarded = resolved.kind == ScriptApiCatalogLuaReturnKind::GuardedTable || resolved.kind == ScriptApiCatalogLuaReturnKind::GuardedValues;
    if ((guarded || resolved.kind == ScriptApiCatalogLuaReturnKind::SingleOutput) && !OutputIndex(*resolved.signature, resolved.returnPin)) {
        problems.push_back(label + " names an output its function does not have");
        return std::nullopt;
    }
    return resolved;
}

// ---- The Lua side --------------------------------------------------------------------------------------------------

[[nodiscard]] std::string ArgumentName(std::size_t index) { return "a" + std::to_string(index + 1U); }

// The test that a Lua value has the form the C function takes: a number below 2^53 for an entity, a 32-bit integer for an
// integer input. A value of any other form is a call the original function answers.
[[nodiscard]] std::string ArgumentCheck(ScriptValueType type, const std::string& a) {
    switch (type) {
    case ScriptValueType::Entity:
        return "type(" + a + ") == \"number\" and " + a + " >= 0 and " + a + " < 9007199254740992 and floor(" + a + ") == " + a;
    case ScriptValueType::Int:
        return "type(" + a + ") == \"number\" and floor(" + a + ") == " + a + " and " + a + " >= -2147483648 and " + a + " <= 2147483647";
    case ScriptValueType::String:
        return "type(" + a + ") == \"string\"";
    case ScriptValueType::Bool:
        return "type(" + a + ") == \"boolean\"";
    default:
        return "type(" + a + ") == \"number\"";
    }
}

[[nodiscard]] std::string OutputValue(const ScriptFunctionSignature& signature, std::size_t index) {
    const std::string slotText = "out[" + std::to_string(index) + "]";
    return signature.outputs[index].type == ScriptValueType::Bool ? slotText + " ~= 0" : slotText;
}

// The outputs except `skip`, as the Lua values of a call result: a table by pin name, or separate values.
[[nodiscard]] std::string OutputsText(const ScriptFunctionSignature& signature, std::optional<std::size_t> skip, bool table) {
    std::string text = table ? "{ " : "";
    const char* separator = "";
    for (std::size_t index = 0; index < signature.outputs.size(); ++index) {
        if (skip && *skip == index) continue;
        text += separator + (table ? "[\"" + signature.outputs[index].name + "\"] = " : std::string{}) + OutputValue(signature, index);
        separator = ", ";
    }
    return text + (table ? " }" : "");
}

// The statements that return what the original function returns for the outputs the C function wrote.
[[nodiscard]] std::string ReturnText(const Resolved& resolved) {
    const ScriptFunctionSignature& signature = *resolved.signature;
    const std::optional<std::size_t> pin = OutputIndex(signature, resolved.returnPin);
    switch (resolved.kind) {
    case ScriptApiCatalogLuaReturnKind::SingleOutput:
        return "return " + OutputValue(signature, *pin);
    case ScriptApiCatalogLuaReturnKind::OutputsTable:
        return "return " + OutputsText(signature, std::nullopt, true);
    case ScriptApiCatalogLuaReturnKind::GuardedTable:
        return "if out[" + std::to_string(*pin) + "] == 0 then return nil end return " + OutputsText(signature, pin, true);
    case ScriptApiCatalogLuaReturnKind::GuardedValues:
        return "if out[" + std::to_string(*pin) + "] == 0 then return nil end return " + OutputsText(signature, pin, false);
    case ScriptApiCatalogLuaReturnKind::Values:
        return "return " + OutputsText(signature, std::nullopt, false);
    case ScriptApiCatalogLuaReturnKind::Default:
        break;
    }
    if (signature.outputs.empty()) return "return nil";
    return "return " + OutputsText(signature, std::nullopt, signature.outputs.size() > 1U);
}

// The Lua that replaces one function of the environment: a call whose arguments all have the C function's forms makes the
// fast call; any other (a missing or mistyped argument, the table form) is the original function's.
[[nodiscard]] std::string ReplacementText(const Resolved& resolved, std::size_t index) {
    const ScriptFunctionSignature& signature = *resolved.signature;
    std::string names;
    std::string checks;
    std::string passed;
    std::size_t optional = 0;
    for (std::size_t input = 0; input < signature.inputs.size(); ++input) {
        const ScriptFunctionPin& pin = signature.inputs[input];
        const std::string a = ArgumentName(input);
        names += (input == 0U ? "" : ", ") + a;
        const std::string check = ArgumentCheck(pin.type, a);
        if (pin.required) {
            checks += (checks.empty() ? "" : " and ") + check;
            passed += ", " + (pin.type == ScriptValueType::Bool ? "(" + a + " and 1 or 0)" : a);
        } else {
            checks += (checks.empty() ? "" : " and ") + std::string{ "(" } + a + " == nil or " + check + ")";
            passed += ", (" + a + " or " + std::to_string(resolved.binding->defaults[optional++]) + ")";
        }
    }
    const std::string table{ resolved.binding->table };
    const std::string name{ resolved.binding->name };
    std::string text = "  do\n    local target = env." + table + "\n    local original = target and target." + name + "\n    if original then\n      local call = calls[" +
        std::to_string(index + 1U) + "]\n      target." + name + " = function(...)\n";
    if (!signature.inputs.empty()) text += "        local " + names + " = ...\n";
    text += "        if " + (checks.empty() ? std::string{ "true" } : checks) + " then\n";
    text += "          if call(slot" + passed + (signature.outputs.empty() ? "" : ", out") + ") ~= 0 then " + ReturnText(resolved) + " end\n        end\n";
    text += "        return original(...)\n      end\n    end\n  end\n";
    return text;
}

// The chunk that, given the ffi library, the C functions and the slot, returns the function that replaces the functions of
// one script environment.
[[nodiscard]] std::string InstallerText(const std::vector<Resolved>& plan) {
    std::string declarations;
    std::string casts;
    std::string replacements;
    for (std::size_t index = 0; index < plan.size(); ++index) {
        const std::string type = "KbHot" + std::to_string(index + 1U);
        std::string parameters = plan[index].binding->signature.substr(plan[index].binding->signature.find('(') + 1U);
        parameters.pop_back();
        declarations += "typedef int (*" + type + ")(" + parameters + ");\n";
        casts += "calls[" + std::to_string(index + 1U) + "] = ffi.cast(\"" + type + "\", pointers[" + std::to_string(index + 1U) + "])\n";
        replacements += ReplacementText(plan[index], index);
    }
    return "local ffi, pointers, slotPointer = ...\nffi.cdef[[\n" + declarations + "]]\nlocal slot = ffi.cast(\"void*\", slotPointer)\n" +
        "local out = ffi.new(\"double[" + std::to_string(kMaxOutputs) + "]\")\nlocal type, floor = type, math.floor\nlocal calls = {}\n" + casts +
        "return function(env)\n" + replacements + "end\n";
}

[[nodiscard]] std::vector<Resolved> Plan(const std::vector<HotBinding>& bindings, const ScriptFunctionRegistry& registry, std::vector<std::string>& problems) {
    std::vector<Resolved> plan;
    for (const HotBinding& binding : bindings) {
        if (std::optional<Resolved> resolved = Resolve(binding, registry, problems)) plan.push_back(*resolved);
    }
    return plan;
}

// Registry key of the function the chunk returns (false when it could not be built).
constexpr char kInstallerKey = 0;

// Builds the installer in `state`: the ffi library is opened for the runtime only and removed from the globals again, so
// no script reaches it. Leaves the installer on the stack; on failure leaves the stack as it was and adds the reason.
bool BuildInstaller(lua_State* state, const std::vector<HotBinding>& bindings, const ScriptFunctionRegistry& registry, void* slot, std::vector<std::string>& problems) {
    const std::vector<Resolved> plan = Plan(bindings, registry, problems);
    const std::string source = InstallerText(plan);
    const int top = lua_gettop(state);
    lua_pushcfunction(state, luaopen_ffi);
    lua_pushstring(state, LUA_FFILIBNAME);
    lua_call(state, 1, 1);
    lua_pushnil(state);
    lua_setglobal(state, LUA_FFILIBNAME);
    bool built = false;
    if (luaL_loadbuffer(state, source.data(), source.size(), "=hot-api") == 0) {
        lua_pushvalue(state, -2);
        lua_createtable(state, static_cast<int>(plan.size()), 0);
        for (std::size_t index = 0; index < plan.size(); ++index) {
            lua_pushlightuserdata(state, plan[index].binding->function);
            lua_rawseti(state, -2, static_cast<int>(index + 1U));
        }
        lua_pushlightuserdata(state, slot);
        built = lua_pcall(state, 3, 1, 0) == 0 && lua_isfunction(state, -1) != 0;
    }
    if (!built) {
        problems.push_back(lua_isstring(state, -1) != 0 ? lua_tostring(state, -1) : "the generated Lua did not load");
        lua_settop(state, top);
        return false;
    }
    lua_replace(state, top + 1);
    lua_settop(state, top + 1);
    return true;
}

// Pushes the installer of the state, built on first use from the functions registered then.
bool PushInstaller(lua_State* state, const ScriptFunctionRegistry& registry) {
    if (lua_rawgetp(state, LUA_REGISTRYINDEX, &kInstallerKey) == LUA_TFUNCTION) return true;
    const bool failedBefore = lua_type(state, -1) == LUA_TBOOLEAN;
    lua_pop(state, 1);
    if (failedBefore) return false;
    std::vector<std::string> problems;
    const bool built = BuildInstaller(state, Bindings(), registry, PucLuaRuntimeApi::ContextSlot(state), problems);
    if (built) {
        lua_pushvalue(state, -1);
    } else {
        lua_pushboolean(state, 0);
    }
    lua_rawsetp(state, LUA_REGISTRYINDEX, &kInstallerKey);
    return built;
}

} // namespace

void PucLuaHotApi::Attach(lua_State* state, int environmentIndex, ScriptExecutionContext& context) {
    const ScriptFunctionRegistry* registry = context.Functions();
    if (registry == nullptr) return;
    environmentIndex = lua_absindex(state, environmentIndex);
    if (!PushInstaller(state, *registry)) return;
    lua_pushvalue(state, environmentIndex);
    if (lua_pcall(state, 1, 0, 0) != 0) lua_pop(state, 1);
}

std::vector<std::string> PucLuaHotApi::Verify(const ScriptFunctionRegistry& functions) {
    std::vector<std::string> problems;
    lua_State* scratch = luaL_newstate();
    if (scratch == nullptr) return { "a script state could not be created" };
    luaL_openlibs(scratch);
    if (BuildInstaller(scratch, Bindings(), functions, nullptr, problems)) lua_pop(scratch, 1);
    lua_close(scratch);
    return problems;
}

} // namespace kb::script
