#include "kb/render/scene/SceneRenderResourceMap.hpp"

#include "kb/render/resources/RenderResourceRegistry.hpp"

namespace kb::render {

namespace {

template <typename Handle>
void EraseHandle(std::unordered_map<std::uint64_t, Handle>& bindings, Handle handle) noexcept {
    if (!handle.IsValid()) {
        return;
    }

    for (auto it = bindings.begin(); it != bindings.end();) {
        if (it->second == handle) {
            it = bindings.erase(it);
        } else {
            ++it;
        }
    }
}

void EraseTextureHandle(
    std::unordered_map<SceneRenderResourceMap::TextureBindingKey, RenderTextureHandle, SceneRenderResourceMap::TextureBindingKeyHash>& bindings,
    RenderTextureHandle handle) noexcept {
    if (!handle.IsValid()) {
        return;
    }

    for (auto it = bindings.begin(); it != bindings.end();) {
        if (it->second == handle) {
            it = bindings.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace

std::size_t SceneRenderResourceMap::TextureBindingKeyHash::operator()(TextureBindingKey key) const noexcept {
    const std::uint64_t color = static_cast<std::uint64_t>(key.colorSpace);
    return static_cast<std::size_t>(key.assetId ^ (color + 0x9e3779b97f4a7c15ULL + (key.assetId << 6U) + (key.assetId >> 2U)));
}

void SceneRenderResourceMap::Reserve(const SceneRenderResourceMapReserveDesc& desc) {
    if (desc.meshBindings > 0U) {
        meshes_.reserve(desc.meshBindings);
    }
    if (desc.materialBindings > 0U) {
        materials_.reserve(desc.materialBindings);
    }
    if (desc.textureBindings > 0U) {
        textures_.reserve(desc.textureBindings);
        dynamicTextures_.reserve(desc.textureBindings);
    }
}

void SceneRenderResourceMap::BindMesh(std::uint64_t meshAssetId, RenderMeshHandle handle) {
    if (meshAssetId == 0U || !handle.IsValid()) {
        return;
    }
    meshes_[meshAssetId] = handle;
}

void SceneRenderResourceMap::UnbindMesh(std::uint64_t meshAssetId) noexcept {
    meshes_.erase(meshAssetId);
}

void SceneRenderResourceMap::UnbindMeshHandle(RenderMeshHandle handle) noexcept {
    EraseHandle(meshes_, handle);
}

RenderMeshHandle SceneRenderResourceMap::ResolveMesh(std::uint64_t meshAssetId) const noexcept {
    const auto it = meshes_.find(meshAssetId);
    return it == meshes_.end() ? RenderMeshHandle{} : it->second;
}

void SceneRenderResourceMap::BindMaterial(std::uint64_t materialAssetId, RenderMaterialHandle handle) {
    if (materialAssetId == 0U || !handle.IsValid()) {
        return;
    }
    materials_[materialAssetId] = handle;
}

void SceneRenderResourceMap::UnbindMaterial(std::uint64_t materialAssetId) noexcept {
    materials_.erase(materialAssetId);
}

void SceneRenderResourceMap::UnbindMaterialHandle(RenderMaterialHandle handle) noexcept {
    EraseHandle(materials_, handle);
}

RenderMaterialHandle SceneRenderResourceMap::ResolveMaterial(std::uint64_t materialAssetId) const noexcept {
    const auto it = materials_.find(materialAssetId);
    return it == materials_.end() ? RenderMaterialHandle{} : it->second;
}

void SceneRenderResourceMap::BindTexture(std::uint64_t textureAssetId, RenderTextureHandle handle) {
    BindTexture(textureAssetId, RenderTextureColorSpace::Linear, handle);
}

void SceneRenderResourceMap::BindTexture(std::uint64_t textureAssetId, RenderTextureColorSpace colorSpace, RenderTextureHandle handle) {
    if (textureAssetId == 0U || !handle.IsValid()) {
        return;
    }
    textures_[TextureBindingKey{.assetId = textureAssetId, .colorSpace = colorSpace}] = handle;
}

void SceneRenderResourceMap::BindDynamicTexture(std::uint64_t textureAssetId, RenderTextureColorSpace colorSpace, RenderTextureHandle handle) {
    if (textureAssetId == 0U || !handle.IsValid()) {
        return;
    }
    dynamicTextures_[TextureBindingKey{.assetId = textureAssetId, .colorSpace = colorSpace}] = handle;
}

void SceneRenderResourceMap::UnbindDynamicTexture(std::uint64_t textureAssetId, RenderTextureColorSpace colorSpace) noexcept {
    dynamicTextures_.erase(TextureBindingKey{.assetId = textureAssetId, .colorSpace = colorSpace});
}

void SceneRenderResourceMap::UnbindTexture(std::uint64_t textureAssetId) noexcept {
    UnbindTexture(textureAssetId, RenderTextureColorSpace::Linear);
}

void SceneRenderResourceMap::UnbindTexture(std::uint64_t textureAssetId, RenderTextureColorSpace colorSpace) noexcept {
    textures_.erase(TextureBindingKey{.assetId = textureAssetId, .colorSpace = colorSpace});
}

void SceneRenderResourceMap::UnbindTextureHandle(RenderTextureHandle handle) noexcept {
    EraseTextureHandle(textures_, handle);
    EraseTextureHandle(dynamicTextures_, handle);
}

RenderTextureHandle SceneRenderResourceMap::ResolveTexture(std::uint64_t textureAssetId) const noexcept {
    return ResolveTexture(textureAssetId, RenderTextureColorSpace::Linear);
}

RenderTextureHandle SceneRenderResourceMap::ResolveTexture(std::uint64_t textureAssetId, RenderTextureColorSpace colorSpace) const noexcept {
    const auto dynamic = dynamicTextures_.find(TextureBindingKey{.assetId = textureAssetId, .colorSpace = colorSpace});
    if (dynamic != dynamicTextures_.end()) {
        return dynamic->second;
    }
    const auto it = textures_.find(TextureBindingKey{.assetId = textureAssetId, .colorSpace = colorSpace});
    return it == textures_.end() ? RenderTextureHandle{} : it->second;
}

void SceneRenderResourceMap::PruneInvalidBindings(const RenderResourceRegistry& registry) noexcept {
    for (auto it = meshes_.begin(); it != meshes_.end();) {
        if (!registry.ContainsMesh(it->second)) {
            it = meshes_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = materials_.begin(); it != materials_.end();) {
        if (!registry.ContainsMaterial(it->second)) {
            it = materials_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = textures_.begin(); it != textures_.end();) {
        if (!registry.ContainsTexture(it->second)) {
            it = textures_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = dynamicTextures_.begin(); it != dynamicTextures_.end();) {
        if (!registry.ContainsTexture(it->second)) {
            it = dynamicTextures_.erase(it);
        } else {
            ++it;
        }
    }
}

void SceneRenderResourceMap::Clear() noexcept {
    meshes_.clear();
    materials_.clear();
    textures_.clear();
    dynamicTextures_.clear();
}

SceneRenderResourceMapStats SceneRenderResourceMap::Stats() const noexcept {
    return SceneRenderResourceMapStats{
        .meshBindingCount = static_cast<std::uint32_t>(meshes_.size()),
        .materialBindingCount = static_cast<std::uint32_t>(materials_.size()),
        .textureBindingCount = static_cast<std::uint32_t>(textures_.size() + dynamicTextures_.size()),
        .meshBindingCapacity = static_cast<std::uint32_t>(meshes_.bucket_count()),
        .materialBindingCapacity = static_cast<std::uint32_t>(materials_.bucket_count()),
        .textureBindingCapacity = static_cast<std::uint32_t>(textures_.bucket_count()),
    };
}

} // namespace kb::render
