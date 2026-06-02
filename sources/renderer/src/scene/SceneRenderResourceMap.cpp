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

} // namespace

void SceneRenderResourceMap::Reserve(const SceneRenderResourceMapReserveDesc& desc) {
    if (desc.meshBindings > 0U) {
        meshes_.reserve(desc.meshBindings);
    }
    if (desc.materialBindings > 0U) {
        materials_.reserve(desc.materialBindings);
    }
    if (desc.textureBindings > 0U) {
        textures_.reserve(desc.textureBindings);
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
    if (textureAssetId == 0U || !handle.IsValid()) {
        return;
    }
    textures_[textureAssetId] = handle;
}

void SceneRenderResourceMap::UnbindTexture(std::uint64_t textureAssetId) noexcept {
    textures_.erase(textureAssetId);
}

void SceneRenderResourceMap::UnbindTextureHandle(RenderTextureHandle handle) noexcept {
    EraseHandle(textures_, handle);
}

RenderTextureHandle SceneRenderResourceMap::ResolveTexture(std::uint64_t textureAssetId) const noexcept {
    const auto it = textures_.find(textureAssetId);
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
}

void SceneRenderResourceMap::Clear() noexcept {
    meshes_.clear();
    materials_.clear();
    textures_.clear();
}

SceneRenderResourceMapStats SceneRenderResourceMap::Stats() const noexcept {
    return SceneRenderResourceMapStats{
        .meshBindingCount = static_cast<std::uint32_t>(meshes_.size()),
        .materialBindingCount = static_cast<std::uint32_t>(materials_.size()),
        .textureBindingCount = static_cast<std::uint32_t>(textures_.size()),
        .meshBindingCapacity = static_cast<std::uint32_t>(meshes_.bucket_count()),
        .materialBindingCapacity = static_cast<std::uint32_t>(materials_.bucket_count()),
        .textureBindingCapacity = static_cast<std::uint32_t>(textures_.bucket_count()),
    };
}

} // namespace kb::render
