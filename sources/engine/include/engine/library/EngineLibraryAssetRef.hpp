#pragma once

#include "engine/assets/AssetHandle.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/assets/ImportedAsset.hpp"
#include "engine/audio/AudioClipAsset.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/visual/VisualGraphTypes.hpp"

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

// LIB-158: a non-owning reference to a runtime asset — the weak companion to
// AssetRef<T>. Holds no strong reference, so it never keeps a payload
// resident; Lock() upgrades to a live AssetRef<T> while the asset is still
// held (by the cache under Retain, or by another AssetRef), and yields an
// empty handle once every strong holder has dropped (the observing side of
// kb::assets::AssetUnloadPolicy::ReleaseWhenUnreferenced). Exactly
// kb::assets::WeakAssetHandle<T> — no second model, named for the contract.
template <typename T>
using WeakAssetRef = kb::assets::WeakAssetHandle<T>;

// A reference to a scene *asset* on disk — a serialized
// kb::scene::SceneDocument, loadable through kb::assets::AssetManager (the
// "Scene" loader kb::scene::Scene registers by default) — never to be
// confused with kb::scene::Scene::Id() (LIB-008 EntityHandle::SceneId(),
// the runtime instance id of a live, in-memory world). SceneRef names the
// file; a kb::scene::Scene is the loaded, playing world.
using SceneRef = AssetRef<kb::scene::SceneDocument>;

// LIB-157: typed asset references for the kinds whose payload C++ type is
// owned by kb_engine and can therefore be named here. Each is exactly the
// AssetRef<T> for that kind's payload — no new handle model, just the
// public-contract name (see AssetKind for the parallel kind<->type tag
// mapping the script-facing Assets.FindTyped/KindOf surface uses).
using PrefabRef = AssetRef<kb::scene::ScenePrefab>;
using GraphRef = AssetRef<kb::visual::VisualGraphAsset>;
using AudioClipRef = AssetRef<kb::audio::AudioClipAsset>;
using AnimationRef = AssetRef<kb::assets::ImportedAsset>;
using InputActionRef = AssetRef<kb::input::InputActionAsset>;
using InputMapRef = AssetRef<kb::input::InputMappingContextAsset>;

// Renderer-owned MeshRef/MaterialRef/TextureRef aliases live in
// kb/render/resources/RenderAssetRefs.hpp, preserving the module boundary
// while still using this exact AssetRef<T> ownership model.

} // namespace kb::library
