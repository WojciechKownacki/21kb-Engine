#pragma once

#include "engine/script/ScriptValue.hpp"
#include "engine/visual/VisualGraphTypes.hpp"

#include <string_view>

namespace kb::library {

// How a kb::script::ScriptValueType behaves across every boundary
// kb::library crosses. Every field already has a real, independently
// verifiable source: kb::script::ToString/ToVisualGraphValueType for
// serialization and the Visual Graph pin mapping, ScriptValue::operator==
// for comparison. LibraryTypeDesc names and collects that behavior in one
// lookup instead of introducing a second, parallel type system callers
// would have to keep in sync by hand — it never re-derives what
// ScriptValue/PucLuaValueBridge already define, only documents it.
struct LibraryTypeDesc {
    kb::script::ScriptValueType scriptType = kb::script::ScriptValueType::Void;
    // kb::script::ToString(scriptType) — the canonical serialized/display
    // name (the same string ScriptApiExport's Markdown/JSON/Lua stub
    // generation already prints).
    std::string_view canonicalName;
    // kb::script::ToVisualGraphValueType(scriptType) — the Visual Graph pin
    // type a function pin of this ScriptValueType compiles to.
    kb::visual::VisualGraphValueType visualGraphPinType = kb::visual::VisualGraphValueType::Void;
    // The Lua type the (private) PucLuaValueBridge marshals this
    // ScriptValueType to/from. Documentation only: the bridge's own code
    // is the actual behavior; this string exists so LIB-022's manifest
    // generation and human-facing docs have something to print. Verified
    // against the REAL bridge (not just asserted) by
    // RunTypeDescLuaRoundTripTest (EngineLibraryTests.cpp) — every base
    // word here (before the parenthetical detail) is checked to be the
    // actual Lua type() of a real round-tripped value, for every
    // ScriptValueType except Void. That same test also documents, with an
    // explicit regression-locked assertion, that this round-trip fidelity
    // only holds through a TYPED pin (a registered function's declared
    // input/output type, which ScriptFunctionRegistry::CoerceArgument can
    // re-tag against): SetShared has no declared target type, so an Entity
    // id passed through it comes back as Int, not Entity.
    std::string_view luaTypeName;
    // Every ScriptValueType supports structural equality via
    // ScriptValue::operator==; true for all of them today.
    bool supportsEquality = true;
    kb::script::ScriptValue defaultValue{};
};

// Looks up the LibraryTypeDesc for `type`. Defined for every
// kb::script::ScriptValueType value, including Void.
[[nodiscard]] const LibraryTypeDesc& DescribeType(kb::script::ScriptValueType type) noexcept;

} // namespace kb::library
