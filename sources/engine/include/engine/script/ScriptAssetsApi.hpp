#pragma once

#include "engine/script/ScriptSceneComponentApi.hpp"

#include <span>

namespace kb::script {

class ScriptRuntimeHost;

class ScriptAssetsApi final {
public:
    ScriptAssetsApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);

    // LIB-019: the safe, script-facing fields of an asset — reuses
    // ScriptSceneComponentPropertyDesc's exact shape (name/type/writable)
    // rather than declaring a third, parallel descriptor type; both are
    // already the same shape LibraryPropertyDesc (LIB-019) aliases. Every
    // entry here is read-only and readable through Assets.GetProperty
    // (below) for any asset a reference resolves to, real project asset or
    // not — no separate "does this asset kind support properties" gate,
    // since these are metadata fields every AssetMetadata has.
    [[nodiscard]] static std::span<const ScriptSceneComponentPropertyDesc> AssetProperties() noexcept;
};

} // namespace kb::script
