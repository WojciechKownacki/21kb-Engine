#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::assets::bake {

// Longest name the bake cache accepts for anything that becomes a path
// component (target profile id, baker id, asset type id, block name).
//
// It BOUNDS a loose-cache path; it does not keep one inside Win32's MAX_PATH.
// Three components at this maximum plus the 32-character digest and the block
// extension already spend about 240 characters before the store root is
// counted, so an ordinary Win32 path overflows well before a realistic root
// does. LooseBakedAssetSink therefore addresses its store through an
// extended-length path on Windows and refuses, before it writes anything, a
// root that would push the worst-case block path past kMaxBakeStorePathLength.
inline constexpr std::size_t kMaxBakeCacheNameBytes = 64U;

// One rule for every name the bake cache turns into a path component. A name
// passes only if it is a portable file name on every target file system:
// non-empty, at most kMaxBakeCacheNameBytes bytes, ASCII [A-Za-z0-9._-] only,
// not starting with '.' or '-', not ending with '.', and not a reserved Win32
// device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9, with or without an
// extension). AssetBakeDigest::ToString always satisfies it.
[[nodiscard]] bool IsValidBakeCacheName(std::string_view name) noexcept;

// 128-bit content address of a bake key. NOT cryptographic: it identifies
// build artifacts, it does not authenticate them. Two lanes of FNV-1a over the
// same canonical byte stream (different offset basis and multiplier), each
// finalized with a splitmix64 avalanche that also folds in the stream length.
// Every step is byte-at-a-time and little-endian by construction, so the value
// is identical on every compiler, CPU and endianness we target.
struct AssetBakeDigest {
    std::uint64_t high = 0U;
    std::uint64_t low = 0U;

    [[nodiscard]] bool operator==(const AssetBakeDigest&) const noexcept = default;
    [[nodiscard]] auto operator<=>(const AssetBakeDigest&) const noexcept = default;

    // Exactly 32 lowercase hex characters, high half first. Always a valid
    // file name (see IsValidBakeCacheName).
    [[nodiscard]] std::string ToString() const;
};

// Stable 64-bit hashes for the two opaque inputs of AssetBakeKey
// (sourceContentHash, settingsHash). FNV-1a over raw bytes: deterministic,
// allocation-free and machine-independent. A caller that hashes structured
// settings must serialize them in a fixed field order first -- hashing a
// std::unordered_map traversal, a pointer, or a wall-clock stamp would make
// the resulting key machine-dependent and is the one way to break this cache.
[[nodiscard]] std::uint64_t HashBakeBytes(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] std::uint64_t HashBakeText(std::string_view text) noexcept;

// Identity of one baked artifact.
//
// Determinism contract: Digest() depends on nothing but the field values below.
// No addresses, no wall-clock, no locale, no environment, and no container
// traversal order -- `dependencies` is sorted and deduplicated before it is
// hashed, so a caller that collected dependencies from a std::unordered_map
// still gets the same key on every machine and every run.
//
// Invalidation contract: `bakerId` scopes `bakerVersion`. Bumping the version
// of one baker moves every key that carries that baker's id and leaves every
// other baker's keys byte-for-byte untouched, so a texture-baker fix does not
// throw away the mesh cache.
//
// Dependency contract: a dependency contributes its own Digest(), so a change
// anywhere in the dependency graph propagates all the way to the root key --
// re-baking a parent is triggered by a changed child without the parent ever
// re-reading the child's source.
struct AssetBakeKey {
    // Hash of the source bytes this artifact was produced from.
    std::uint64_t sourceContentHash = 0U;
    // Stable id of the baker that produces the artifact ("SkeletalMesh",
    // "Texture", ...). Becomes a path component, so IsValidBakeCacheName.
    std::string bakerId;
    // That baker's output-format version. Free-form text; bump it whenever the
    // baker's output stops being byte-compatible with what it wrote before.
    std::string bakerVersion;
    // BakeTargetProfile::identifier of the platform being baked for.
    std::string targetProfileId;
    // BakeTargetProfileFingerprint of that profile. A name does not change when
    // the profile behind it does, so the name alone would leave artifacts baked
    // under a since-edited profile addressable under the same key. Required to
    // be non-zero by IsValid(), so a baker cannot forget to ask: the sink
    // refuses the key rather than writing an artifact nobody can invalidate.
    std::uint64_t targetProfileHash = 0U;
    // Hash of the bake settings that are not already covered by the profile.
    // Anything positional (slot order, channel order) must be folded in here:
    // `dependencies` is an unordered set and cannot carry position.
    std::uint64_t settingsHash = 0U;
    // Digests of the artifacts this one is built from.
    std::vector<AssetBakeDigest> dependencies;

    // True when every id that becomes a path component is a portable name, the
    // baker version is present and the profile fingerprint was filled in. A
    // sink must refuse an invalid key.
    [[nodiscard]] bool IsValid() const noexcept;

    [[nodiscard]] AssetBakeDigest Digest() const;

    // Digest().ToString(): the artifact's file name in any bake store.
    [[nodiscard]] std::string ToString() const;

    [[nodiscard]] bool operator==(const AssetBakeKey&) const = default;
};

} // namespace kb::assets::bake
