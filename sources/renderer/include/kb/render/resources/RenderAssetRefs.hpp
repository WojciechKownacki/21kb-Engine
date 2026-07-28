#pragma once

#include "engine/library/EngineLibraryAssetRef.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

namespace kb::render {

using MeshRef = kb::library::AssetRef<RenderMeshAssetData>;
using MaterialRef = kb::library::AssetRef<RenderMaterialAssetData>;
using MaterialInstanceRef = kb::library::AssetRef<RenderMaterialInstanceAssetData>;
using TextureRef = kb::library::AssetRef<RenderTextureAssetData>;

} // namespace kb::render
