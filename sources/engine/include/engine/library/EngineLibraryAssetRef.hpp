#pragma once

#include "engine/assets/AssetHandle.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneDocument.hpp"

namespace kb::library {

// A stable, runtime reference to a loaded asset of type T. AssetId
// (kb::assets::AssetId) is a deterministic hash of the asset's logical
// (virtual) path plus its type — stable across sessions and builds — never
// the OS-specific physical path, which kb::assets::AssetManager resolves
// separately and does not expose here. kb::library does not introduce a
// second asset cache or refcount model: AssetRef<T> is exactly
// kb::assets::AssetHandle<T> (id + the kb::assets::AssetManager-owned
// shared_ptr payload), named for the public contract.
template <typename T>
using AssetRef = kb::assets::AssetHandle<T>;

// A reference to a scene *asset* on disk — a serialized
// kb::scene::SceneDocument, loadable through kb::assets::AssetManager (the
// "Scene" loader kb::scene::Scene registers by default) — never to be
// confused with kb::scene::Scene::Id() (LIB-008 EntityHandle::SceneId(),
// the runtime instance id of a live, in-memory world). SceneRef names the
// file; a kb::scene::Scene is the loaded, playing world.
using SceneRef = AssetRef<kb::scene::SceneDocument>;

} // namespace kb::library
