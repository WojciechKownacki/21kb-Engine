#pragma once

#include "engine/script/ScriptApiCatalog.hpp"

namespace kb::library {

// Describes one safely-exposed field of a component (or, once an asset
// property system exists, an asset): the canonical dotted path a script
// reads/writes it by (e.g. "localPosition.x"), its ScriptValueType, and
// whether scripts may write it. kb::library reuses
// kb::script::ScriptApiCatalogProperty exactly rather than re-deriving
// component reflection: every property kb::script::ScriptApiCatalog::Build()
// already reports for a component (see ScriptSceneComponentApi.cpp, the
// only place component properties are registered today) is already a
// LibraryPropertyDesc. Asset properties have no script-facing accessor to
// describe yet (LIB-155+ Assets module); this alias is what that future
// module will reuse rather than a second property shape.
using LibraryPropertyDesc = kb::script::ScriptApiCatalogProperty;

} // namespace kb::library
