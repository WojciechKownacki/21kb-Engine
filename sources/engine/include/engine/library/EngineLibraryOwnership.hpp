#pragma once

#include "engine/assets/AssetHandle.hpp"
#include "engine/library/EngineLibraryEntityHandle.hpp"

#include <cstdint>

namespace kb::library {

// How a kb::library handle relates to the lifetime of what it refers to.
enum class LibraryOwnership : std::uint8_t {
    // The handle is the sole owner: destroying/releasing it destroys the
    // resource. No kb::library handle uses this today — every resource
    // kb::library exposes (entities, assets) is owned by a runtime system
    // (Scene, kb::assets::AssetManager), never by the handle itself.
    Owned,
    // The handle references a resource owned elsewhere; it never controls
    // the resource's lifetime and must be revalidated before use (e.g.
    // EntityHandle::IsAlive/Validate against the Scene that owns the
    // entity).
    Borrowed,
    // The handle participates in reference counting; the resource lives as
    // long as at least one Shared handle to it exists.
    Shared,
    // The handle observes a Shared or Owned resource without keeping it
    // alive, and can detect when the resource is gone. No kb::library
    // handle uses this today.
    Weak,
};

[[nodiscard]] const char* ToString(LibraryOwnership ownership) noexcept;

// Maps each kb::library handle type to its LibraryOwnership, so the
// classification is a compile-time fact tied to the type itself rather
// than prose that can drift from the code. Specialized below for every
// handle kb::library defines; a type with no specialization simply does
// not compile against LibraryOwnershipTraits<T>::value, which is
// deliberate — an unclassified handle should be a build error, not a
// silently-assumed default.
template <typename T>
struct LibraryOwnershipTraits;

// EntityHandle (LIB-008): the Scene owns the entity; the handle only names
// it and must be revalidated (IsAlive/Validate) before use.
template <>
struct LibraryOwnershipTraits<EntityHandle> {
    static constexpr LibraryOwnership value = LibraryOwnership::Borrowed;
};

// AssetRef<T> / SceneRef (LIB-009, = kb::assets::AssetHandle<T>):
// reference-counted via the handle's std::shared_ptr<const T> payload,
// owned jointly by every live handle and kb::assets::AssetManager's cache.
template <typename T>
struct LibraryOwnershipTraits<kb::assets::AssetHandle<T>> {
    static constexpr LibraryOwnership value = LibraryOwnership::Shared;
};

} // namespace kb::library
