#pragma once

#include <span>

namespace kb::library {

// LIB-057: a read-only, non-owning view over data the runtime returns —
// entity lists, component id lists, asset metadata lists, and every other
// "give me the current contents of this collection without a copy"
// pattern already used throughout kb::scene/kb::ecs/kb::assets (e.g.
// kb::scene::SceneHierarchyAccess's child-entity queries,
// kb::assets::AssetRegistry::All(), kb::ecs::QueryFilter's component
// lists). Those call sites already return/accept std::span<const T> —
// this is not a second, competing view type: ArrayView<T> IS
// std::span<const T>, named for the public kb::library contract the same
// way AssetRef<T> (LIB-009) names kb::assets::AssetHandle<T> instead of
// wrapping it in a new type.
//
// "niemutowalny" (immutable) is enforced by construction, not by
// convention: ArrayView<T> always resolves to a span of `const T`
// regardless of what T is passed — ArrayView<Foo> can never yield a
// mutable Foo&, even if the underlying container (a std::vector<Foo>, a
// C array, ...) is itself mutable. It has no allocation and no ownership
// of its own: it is only ever valid as long as the container it was
// constructed from stays alive and unmodified in a way that would
// invalidate iterators/pointers into it — the same lifetime contract
// every std::span already has, not a new one this type invents.
template <typename T>
using ArrayView = std::span<const T>;

} // namespace kb::library
