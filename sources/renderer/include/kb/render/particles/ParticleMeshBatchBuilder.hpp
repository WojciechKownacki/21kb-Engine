#pragma once

#include "engine/particles/ParticleRenderSnapshot.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "kb/render/scene/batch/SceneMeshBatch.hpp"

#include <cstdint>
#include <vector>

namespace kb::render {

// Builds SceneMeshBatch entries (one per Mesh-output emitter) and their SceneRenderMeshInstance
// data directly from a particle render snapshot, for particles whose ParticleOutputAsset.type is
// Mesh. This deliberately reuses the existing scene mesh pipeline (MeshPipelineProcessor,
// SceneMeshDrawCommandSubmitter) rather than the quad/billboard ParticleRenderBatcher/
// ParticleGpuRenderer path - Mesh output needs full per-instance TRS and real mesh sections/LODs/
// materials, which the compact 80-byte ParticleGpuInstance quad format cannot represent. No
// RenderScene mesh proxy is created or touched; batches are transient, per-frame, CPU-side data.
class ParticleMeshBatchBuilder final {
public:
    void Warmup(std::uint32_t particleCapacity);
    // With a camera and the resources, the particles of an emitter whose sort mode asks for it and whose material
    // is translucent are put in the emitter's draw order (back to front, front to back, by distance or by age);
    // opaque meshes are depth-tested and keep the snapshot order.
    // `renderOffset` (ParticleRenderOffset) is added to every particle position.
    void Build(const kb::particles::ParticleRenderSnapshot& snapshot, const SceneRenderCamera* camera = nullptr,
        const RenderResourceRegistry* resources = nullptr, const SceneRenderResourceMap* resourceMap = nullptr,
        kb::math::Vec3 renderOffset = {}) noexcept;

    [[nodiscard]] const std::vector<SceneMeshBatch>& Batches() const noexcept { return batches_; }

private:
    std::vector<SceneRenderMeshInstance> instances_;
    std::vector<SceneMeshBatch> batches_;
    std::vector<std::uint32_t> orderScratch_;
};

} // namespace kb::render
