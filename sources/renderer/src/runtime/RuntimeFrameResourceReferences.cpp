#include "kb/render/runtime/RuntimeFrameResourceReferences.hpp"

namespace kb::render {

void RuntimeFrameResourceReferences::Clear() noexcept {
    meshes_.clear();
    materials_.clear();
    textures_.clear();
}

void RuntimeFrameResourceReferences::Reserve(const RuntimeFrameResourceReferenceReserveDesc& desc) {
    if (desc.meshes > 0U) {
        meshes_.reserve(desc.meshes);
    }
    if (desc.materials > 0U) {
        materials_.reserve(desc.materials);
    }
    if (desc.textures > 0U) {
        textures_.reserve(desc.textures);
    }
}

void RuntimeFrameResourceReferences::MarkMesh(RuntimeAssetKey key) {
    meshes_.insert(key);
}

void RuntimeFrameResourceReferences::MarkMaterial(RuntimeAssetKey key) {
    materials_.insert(key);
}

void RuntimeFrameResourceReferences::MarkTexture(RuntimeAssetKey key) {
    textures_.insert(key);
}

bool RuntimeFrameResourceReferences::ContainsMesh(RuntimeAssetKey key) const {
    return meshes_.contains(key);
}

bool RuntimeFrameResourceReferences::ContainsMaterial(RuntimeAssetKey key) const {
    return materials_.contains(key);
}

bool RuntimeFrameResourceReferences::ContainsTexture(RuntimeAssetKey key) const {
    return textures_.contains(key);
}

const std::unordered_set<RuntimeAssetKey, RuntimeAssetKeyHash>& RuntimeFrameResourceReferences::Materials() const noexcept {
    return materials_;
}

RuntimeFrameResourceReferenceStats RuntimeFrameResourceReferences::Stats() const noexcept {
    return RuntimeFrameResourceReferenceStats{
        .meshCount = static_cast<std::uint32_t>(meshes_.size()),
        .materialCount = static_cast<std::uint32_t>(materials_.size()),
        .textureCount = static_cast<std::uint32_t>(textures_.size()),
        .meshCapacity = static_cast<std::uint32_t>(meshes_.bucket_count()),
        .materialCapacity = static_cast<std::uint32_t>(materials_.bucket_count()),
        .textureCapacity = static_cast<std::uint32_t>(textures_.bucket_count()),
    };
}

} // namespace kb::render
