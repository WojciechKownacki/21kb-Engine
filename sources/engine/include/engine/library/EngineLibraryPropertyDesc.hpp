#pragma once

#include "engine/script/ScriptApiCatalog.hpp"

namespace kb::library {

// Describes one safely-exposed field of a component or an asset: the
// canonical name a script reads it by (a dotted path like
// "localPosition.x" for components, a flat name like "virtualPath" for
// assets), its ScriptValueType, and whether scripts may write it.
// kb::library reuses kb::script::ScriptApiCatalogProperty exactly rather
// than re-deriving reflection for either source: every property
// kb::script::ScriptApiCatalog::Build() already reports for a component
// (see ScriptSceneComponentApi.cpp, the only place component properties
// are registered) is already a LibraryPropertyDesc, and so is every entry
// kb::script::ScriptAssetsApi::AssetProperties() reports for an asset
// (virtualPath, type — both read-only; see Assets.GetProperty,
// ScriptAssetsApi.cpp, LIB-019) after the same trivial construction the
// component path gets for free from the catalog.
using LibraryPropertyDesc = kb::script::ScriptApiCatalogProperty;

} // namespace kb::library
