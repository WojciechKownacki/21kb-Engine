#pragma once

#include "engine/particles/ParticleRenderSnapshot.hpp"
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
    void Build(const kb::particles::ParticleRenderSnapshot& snapshot) noexcept;

    [[nodiscard]] const std::vector<SceneMeshBatch>& Batches() const noexcept { return batches_; }

private:
    std::vector<SceneRenderMeshInstance> instances_;
    std::vector<SceneMeshBatch> batches_;
};

} // namespace kb::render
