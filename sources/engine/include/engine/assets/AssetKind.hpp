#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kb::assets {

struct AssetMetadata;

// LIB-157: the canonical set of typed asset-reference kinds the public
// scripting library recognises. Each kind maps to one or more concrete
// AssetMetadata::type strings (see AssetKind.cpp) — this enum, and the
// helpers below, are the SINGLE source of truth for that mapping, so the
// script-facing Assets.FindTyped/KindOf surface and any native typed
// AssetRef consumer agree on exactly what "a mesh" or "an input map" is.
//
// Deliberately platform/module-neutral: the mesh/material/texture kinds map
// to type strings ("RenderMesh", ...) whose C++ payload types live in the
// separate kb_render module, but a kind check is only ever a plain
// AssetMetadata::type string compare (the exact pattern
// ScriptMeshRendererApi already uses), so this header pulls in ZERO
// kb_render dependency and works in a headless kb_engine build.
//
// "animation" from LIB-157's kind list is INTENTIONALLY ABSENT: no
// animation asset type/loader exists anywhere in the engine yet (only an
// AssetImportCategory label), so a typed AnimationRef would be an orphan
// API — it is deferred to the animation work in section 14 of the backlog,
// not fabricated here. "input map" is realised as the two real registered
// input asset types: InputAction (a single action asset) and InputMap (a
// mapping-context asset).
enum class AssetKind : std::uint8_t {
    Mesh,
    Material,
    Texture,
    Audio,
    Prefab,
    Scene,
    Graph,
    InputAction,
    InputMap,
};

inline constexpr std::size_t kAssetKindCount = 9;

// The friendly, front-end-facing kind name (e.g. "Mesh", "InputMap"), the
// exact token TryParseAssetKind accepts and the script Assets.KindOf output
// pin reports. Returns "" only for an out-of-range value (never for a
// well-formed AssetKind).
[[nodiscard]] std::string_view ToString(AssetKind kind) noexcept;

// Parses a friendly kind name (case-sensitive, exactly as ToString emits)
// into its AssetKind. Returns false and leaves `out` untouched for any
// unrecognised name (an unknown kind is a malformed request, not a silent
// no-match).
[[nodiscard]] bool TryParseAssetKind(std::string_view name, AssetKind& out) noexcept;

// True when `metadata` is an asset of the given kind. The whole metadata is
// inspected (not just type) so the Audio kind can accept both a native
// "AudioClip" and an imported media asset ("ImportedAsset" whose
// importCategory is "Audio"), mirroring ScriptAudioApi::ResolveClipAssetId
// exactly.
[[nodiscard]] bool AssetMatchesKind(const AssetMetadata& metadata, AssetKind kind) noexcept;

// Reverse classification: sets `out` to the single kind `metadata` belongs
// to and returns true, or returns false (leaving `out` untouched) for an
// asset that is none of the recognised kinds (e.g. a LuaScript,
// NativeBehaviour, or a non-audio ImportedAsset). Every concrete asset type
// belongs to at most one kind, so the classification is unambiguous.
[[nodiscard]] bool TryClassifyAssetKind(const AssetMetadata& metadata, AssetKind& out) noexcept;

} // namespace kb::assets
