#include "kb/render/runtime/RuntimeContentStreamer.hpp"

#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/assets/streaming/BackgroundLoadService.hpp"
#include "engine/assets/streaming/PackBlockStream.hpp"
#include "kb/render/bake/MeshBaker.hpp"
#include "kb/render/bake/TextureBaker.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "scene/pipeline/MeshPipelineVisibility.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace kb::render {
namespace {

namespace streaming = kb::assets::streaming;

// Async read priorities are integers; a priority in [0, 1] keeps six digits, and coarser levels
// of one resource go first.
[[nodiscard]] streaming::BackgroundPriority ReadPriority(float priority, std::uint32_t level) noexcept {
    return static_cast<streaming::BackgroundPriority>(std::clamp(priority, 0.0F, 1.0F) * 1'000'000.0F) * 64 +
        static_cast<streaming::BackgroundPriority>(level);
}

[[nodiscard]] std::uint64_t MeshPayloadBytes(const RenderMeshDesc& desc) {
    return static_cast<std::uint64_t>(RenderStaticMeshVertexStride(desc.vertexFormat)) * desc.vertexCount +
        static_cast<std::uint64_t>(desc.indexCount) * (desc.indexFormat == RenderIndexFormat::Uint32 ? 4U : 2U) +
        desc.terrainLayerWeightBytes;
}

} // namespace

struct RuntimeContentStreamer::Record {
    std::uint64_t id = 0U;
    bool texture = true;
    std::uint64_t sceneId = 0U;
    RuntimeTextureAssetKey textureKey{};
    RuntimeAssetKey meshKey{};
    std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack;
    std::shared_ptr<const RenderTextureAssetData> tail;
    std::shared_ptr<const RenderMeshAssetData> coarse;
    std::vector<RenderMaterialSlotDesc> materialSlots;
    RenderTextureHandle textureHandle{};
    RenderMeshHandle meshHandle{};
    std::uint32_t floor = 0U;
    std::uint32_t uploadedLevel = 0U;
    // Bytes of the GPU resource that is alive now; a rebuild keeps it next to the new one until the frame ends.
    std::uint64_t gpuBytes = 0U;
    // The bytes of every streamed level that is resident: one mip, or one level's chunks.
    std::vector<std::vector<std::vector<std::uint8_t>>> levelData;
    std::uint64_t requestFrame = std::numeric_limits<std::uint64_t>::max();
    std::uint32_t requestLevel = 0U;
    float requestPriority = 0.0F;
    bool rebuildFailed = false;
};

struct RuntimeContentStreamer::PendingLoad {
    std::uint64_t id = 0U;
    std::uint32_t level = 0U;
    std::vector<streaming::BackgroundRequestHandle> reads;
    std::chrono::steady_clock::time_point started{};
};

RuntimeContentStreamer::RuntimeContentStreamer()
    : residency_{ RuntimeContentStreamingSettings{}.budgetBytes } {}

RuntimeContentStreamer::~RuntimeContentStreamer() {
    // The service is shared: cancel what this streamer still has queued. A read already in
    // flight finishes into its own handle, which keeps its pack alive until then.
    if (reader_ != nullptr) {
        for (const std::unique_ptr<PendingLoad>& load : pending_) {
            for (const streaming::BackgroundRequestHandle& read : load->reads) {
                static_cast<void>(reader_->Cancel(read));
            }
        }
    }
    pending_.clear();
    ForgetFiles();
    reader_.reset();
}

void RuntimeContentStreamer::Configure(const RuntimeContentStreamingSettings& settings) {
    settings_ = settings;
    residency_.SetBudget(settings.budgetBytes);
}

void RuntimeContentStreamer::EnsureReader(const kb::assets::bake::RuntimeAssetPack& pack) {
    if (reader_ == nullptr) {
        reader_ = streaming::BackgroundLoadService::Shared();
    }
    // The service keeps a file open once read; the files of every pack streamed from are let go
    // when the streamer releases its resources, so a pack is never held open past its use.
    for (std::uint32_t container = 0U; container < pack.ContainerCount(); ++container) {
        const std::filesystem::path& path = pack.ContainerPath(container);
        if (!path.empty() && std::ranges::find(files_, path) == files_.end()) {
            files_.push_back(path);
        }
    }
}

void RuntimeContentStreamer::ForgetFiles() noexcept {
    if (reader_ != nullptr) {
        for (const std::filesystem::path& path : files_) {
            reader_->Forget(path);
        }
    }
    files_.clear();
}

void RuntimeContentStreamer::TrackTexture(
    RuntimeTextureAssetKey key,
    RenderTextureHandle handle,
    std::shared_ptr<const RenderTextureAssetData> asset,
    std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack) {
    if (asset == nullptr || pack == nullptr || !asset->streaming.has_value() || !asset->gpuBlocks.has_value() ||
        asset->streaming->firstLevel != asset->streaming->streamedMipCount || asset->streaming->streamedMipCount == 0U) {
        return;
    }
    if (const auto existing = textureIds_.find(key); existing != textureIds_.end()) {
        Forget(existing->second);
    }
    const RenderTextureStreamingLayout& layout = *asset->streaming;
    kb::assets::streaming::StreamingResourceDesc desc{};
    desc.levelBytes.assign(layout.streamedLevelBytes.begin(), layout.streamedLevelBytes.end());
    desc.levelBytes.push_back(asset->gpuBlocks->blocks.size());
    desc.residentFloor = layout.streamedMipCount;
    const std::uint64_t id = nextId_++;
    if (!residency_.Register(id, desc)) {
        return;
    }
    auto record = std::make_unique<Record>();
    record->id = id;
    record->texture = true;
    record->sceneId = key.sceneId;
    record->textureKey = key;
    record->pack = std::move(pack);
    record->tail = std::move(asset);
    record->textureHandle = handle;
    record->floor = layout.streamedMipCount;
    record->uploadedLevel = layout.streamedMipCount;
    record->gpuBytes = record->tail->gpuBlocks->blocks.size();
    record->levelData.resize(layout.streamedMipCount + 1U);
    textureIds_.emplace(key, id);
    EnsureReader(*record->pack);
    records_.emplace(id, std::move(record));
}

void RuntimeContentStreamer::TrackMesh(
    RuntimeAssetKey key,
    RenderMeshHandle handle,
    std::shared_ptr<const RenderMeshAssetData> asset,
    std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack,
    std::vector<RenderMaterialSlotDesc> materialSlots) {
    if (asset == nullptr || pack == nullptr || !asset->streaming.has_value()) {
        return;
    }
    const RenderMeshStreamingLayout& layout = *asset->streaming;
    const std::size_t levels = layout.lodGeometryBytes.size();
    if (levels < 2U || layout.firstLod + 1U != levels || layout.lodErrors.size() != levels) {
        return;
    }
    if (const auto existing = meshIds_.find(key); existing != meshIds_.end()) {
        Forget(existing->second);
    }
    kb::assets::streaming::StreamingResourceDesc desc{};
    desc.levelBytes.assign(layout.lodGeometryBytes.begin(), layout.lodGeometryBytes.end());
    desc.residentFloor = layout.firstLod;
    const std::uint64_t id = nextId_++;
    if (!residency_.Register(id, desc)) {
        return;
    }
    auto record = std::make_unique<Record>();
    record->id = id;
    record->texture = false;
    record->sceneId = key.sceneId;
    record->meshKey = key;
    record->pack = std::move(pack);
    record->meshHandle = handle;
    record->floor = layout.firstLod;
    record->uploadedLevel = layout.firstLod;
    record->gpuBytes = MeshPayloadBytes(asset->desc);
    record->levelData.resize(levels);
    record->levelData[layout.firstLod] = layout.chunks;
    record->materialSlots = std::move(materialSlots);
    record->coarse = std::move(asset);
    meshIds_.emplace(key, id);
    EnsureReader(*record->pack);
    records_.emplace(id, std::move(record));
}

bool RuntimeContentStreamer::IsTracking(RuntimeTextureAssetKey key) const noexcept {
    return textureIds_.contains(key);
}

bool RuntimeContentStreamer::IsTracking(RuntimeAssetKey key) const noexcept {
    return meshIds_.contains(key);
}

RuntimeContentStreamer::Record* RuntimeContentStreamer::FindTexture(RuntimeTextureAssetKey key) noexcept {
    const auto id = textureIds_.find(key);
    return id == textureIds_.end() ? nullptr : records_.at(id->second).get();
}

RuntimeContentStreamer::Record* RuntimeContentStreamer::FindMesh(RuntimeAssetKey key) noexcept {
    const auto id = meshIds_.find(key);
    return id == meshIds_.end() ? nullptr : records_.at(id->second).get();
}

void RuntimeContentStreamer::Forget(std::uint64_t id) noexcept {
    const auto found = records_.find(id);
    if (found == records_.end()) {
        return;
    }
    if (found->second->texture) {
        textureIds_.erase(found->second->textureKey);
    } else {
        meshIds_.erase(found->second->meshKey);
    }
    RetainRetired(*found->second);
    residency_.Unregister(id);
    records_.erase(found);
    // A load still in flight for it finishes into nothing: CollectLoads drops loads whose record
    // is gone, and cancelling here keeps queued reads from being started at all.
    for (const std::unique_ptr<PendingLoad>& load : pending_) {
        if (load->id == id && reader_ != nullptr) {
            for (const streaming::BackgroundRequestHandle& read : load->reads) {
                static_cast<void>(reader_->Cancel(read));
            }
        }
    }
}

void RuntimeContentStreamer::RetainRetired(const Record& record) {
    retired_.push_back({record.texture ? record.textureHandle : RenderTextureHandle{},
        record.texture ? RenderMeshHandle{} : record.meshHandle, record.gpuBytes});
}

void RuntimeContentStreamer::NoteTextureUse(RuntimeTextureAssetKey key, std::uint64_t materialAssetId) {
    const auto id = textureIds_.find(key);
    if (id == textureIds_.end()) {
        return;
    }
    std::vector<std::uint64_t>& uses = materialAssetId == 0U ? materialLessTextures_ : materialTextures_[materialAssetId];
    if (std::ranges::find(uses, id->second) == uses.end()) {
        uses.push_back(id->second);
    }
}

void RuntimeContentStreamer::Request(Record& record, std::uint32_t level, float priority, std::uint64_t frame) {
    // Several views in one frame: the finest wish and the highest priority win.
    if (record.requestFrame == frame) {
        level = std::min(level, record.requestLevel);
        priority = std::max(priority, record.requestPriority);
    }
    record.requestFrame = frame;
    record.requestLevel = level;
    record.requestPriority = priority;
    residency_.Request(record.id, level, priority, frame);
}

void RuntimeContentStreamer::RequestTexture(RuntimeTextureAssetKey key, std::uint32_t level, float priority, std::uint64_t frame) {
    if (Record* record = FindTexture(key); record != nullptr) {
        Request(*record, level, priority, frame);
    }
}

void RuntimeContentStreamer::RequestMesh(RuntimeAssetKey key, std::uint32_t level, float priority, std::uint64_t frame) {
    if (Record* record = FindMesh(key); record != nullptr) {
        Request(*record, level, priority, frame);
    }
}

void RuntimeContentStreamer::GatherFeedback(const RuntimeContentStreamingFrame& frame, const SceneRenderer& sceneRenderer) {
    for (const std::uint64_t id : materialLessTextures_) {
        if (const auto record = records_.find(id); record != records_.end()) {
            Request(*record->second, 0U, 1.0F, frame.frame);
        }
    }
    if (frame.renderScene == nullptr || frame.camera == nullptr || frame.viewportHeight == 0U ||
        (meshIds_.empty() && materialTextures_.empty())) {
        return;
    }
    const float viewportHeight = static_cast<float>(frame.viewportHeight);
    for (const auto& [proxyId, proxy] : frame.renderScene->MeshProxies()) {
        static_cast<void>(proxyId);
        const MeshRenderProxyDesc& desc = proxy.desc;
        if (!desc.visible || desc.meshAssetId == 0U) {
            continue;
        }
        Record* mesh = FindMesh(RuntimeAssetKey{ .sceneId = frame.sceneId, .assetId = desc.meshAssetId });
        const RenderMeshResource* resource =
            sceneRenderer.Resources().FindMesh(sceneRenderer.ResourceMap().ResolveMesh(desc.meshAssetId));
        const RenderBoundsSphere localBounds = desc.boundsOverride.IsValid()
            ? desc.boundsOverride
            : (resource != nullptr ? resource->bounds : RenderBoundsSphere{});
        if (!localBounds.IsValid()) {
            continue;
        }
        const float coverage = MeshPipelineVisibility::ScreenCoverage(
            frame.camera, MeshPipelineVisibility::TransformBounds(localBounds, desc.model));
        // Diameter of the object on screen, in pixels.
        const float pixels = std::max(coverage * viewportHeight, 0.0F);
        if (mesh != nullptr && mesh->coarse != nullptr) {
            const RenderMeshStreamingLayout& layout = *mesh->coarse->streaming;
            const float radius = std::max(mesh->coarse->bounds.radius, 1.0e-6F);
            std::uint32_t wanted = 0U;
            for (std::uint32_t level = static_cast<std::uint32_t>(layout.lodErrors.size()); level > 0U; --level) {
                const float errorPixels = layout.lodErrors[level - 1U] / radius * pixels * 0.5F;
                if (errorPixels <= settings_.meshMaxScreenErrorPixels) {
                    wanted = level - 1U;
                    break;
                }
            }
            Request(*mesh, wanted, coverage, frame.frame);
        }
        const auto requestMaterial = [&](std::uint64_t materialAssetId) {
            const auto textures = materialTextures_.find(materialAssetId);
            if (materialAssetId == 0U || textures == materialTextures_.end()) {
                return;
            }
            for (const std::uint64_t id : textures->second) {
                const auto found = records_.find(id);
                if (found == records_.end() || found->second->tail == nullptr) {
                    continue;
                }
                Record& texture = *found->second;
                const RenderTextureStreamingLayout& layout = *texture.tail->streaming;
                const std::uint32_t edge = std::max<std::uint32_t>(layout.width, layout.height);
                // The coarsest level that still has at least a texel per pixel of the object.
                std::uint32_t wanted = 0U;
                while (wanted < texture.floor && static_cast<float>(std::max<std::uint32_t>(1U, edge >> (wanted + 1U))) >= pixels) {
                    ++wanted;
                }
                const std::int32_t biased = static_cast<std::int32_t>(wanted) + settings_.textureMipBias;
                wanted = static_cast<std::uint32_t>(std::clamp<std::int32_t>(biased, 0, static_cast<std::int32_t>(texture.floor)));
                Request(texture, wanted, coverage, frame.frame);
            }
        };
        requestMaterial(desc.materialAssetId);
        for (std::uint32_t slot = 0U; slot < desc.materialSlotOverrideCount && slot < desc.materialSlotAssetIds.size(); ++slot) {
            requestMaterial(desc.materialSlotAssetIds[slot]);
        }
        if (resource != nullptr) {
            for (const RenderMaterialSlot& slot : resource->materialSlots) {
                requestMaterial(slot.defaultMaterialAssetId);
            }
        }
    }
}

void RuntimeContentStreamer::CollectLoads(std::uint64_t frame) {
    for (auto load = pending_.begin(); load != pending_.end();) {
        const bool done = std::ranges::all_of((*load)->reads, [](const streaming::BackgroundRequestHandle& read) {
            return read->IsDone();
        });
        if (!done) {
            ++load;
            continue;
        }
        const auto record = records_.find((*load)->id);
        if (record != records_.end()) {
            const bool succeeded = std::ranges::all_of((*load)->reads, [](const streaming::BackgroundRequestHandle& read) {
                return read->State() == streaming::BackgroundRequestState::Completed;
            });
            if (succeeded) {
                std::vector<std::vector<std::uint8_t>> pieces;
                pieces.reserve((*load)->reads.size());
                for (const streaming::BackgroundRequestHandle& read : (*load)->reads) {
                    pieces.push_back(std::move(read->Bytes()));
                }
                record->second->levelData[(*load)->level] = std::move(pieces);
                const double milliseconds =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - (*load)->started).count();
                counters_.lastLoadMilliseconds = milliseconds;
                counters_.maxLoadMilliseconds = std::max(counters_.maxLoadMilliseconds, milliseconds);
            } else {
                ++counters_.failedLoads;
            }
            residency_.CompleteLoad((*load)->id, (*load)->level, succeeded, frame);
        }
        load = pending_.erase(load);
    }
}

void RuntimeContentStreamer::ApplyEvictions(const kb::assets::streaming::StreamingPlan& plan) {
    for (const kb::assets::streaming::StreamingEviction& eviction : plan.evictions) {
        const auto record = records_.find(eviction.resource);
        if (record == records_.end()) {
            continue;
        }
        for (std::uint32_t level = 0U; level < eviction.newResidentLevel && level < record->second->floor; ++level) {
            record->second->levelData[level].clear();
            record->second->levelData[level].shrink_to_fit();
        }
    }
}

void RuntimeContentStreamer::StartLoads(const kb::assets::streaming::StreamingPlan& plan) {
    for (const kb::assets::streaming::StreamingLoad& planned : plan.loads) {
        const auto found = records_.find(planned.resource);
        if (found == records_.end()) {
            continue;
        }
        Record& record = *found->second;
        auto load = std::make_unique<PendingLoad>();
        load->id = record.id;
        load->level = planned.level;
        load->started = std::chrono::steady_clock::now();
        bool issued = true;
        const streaming::BackgroundPriority priority = ReadPriority(planned.priority, planned.level);
        const auto read = [&](const kb::assets::bake::AssetBakeDigest& artifact, const std::string& block) {
            kb::assets::bake::AssetPackReadStatus status = kb::assets::bake::AssetPackReadStatus::NotMounted;
            streaming::BackgroundRequestHandle handle =
                streaming::ReadPackBlockAsync(*reader_, record.pack, artifact, block, priority, status);
            if (handle == nullptr) {
                issued = false;
                return;
            }
            load->reads.push_back(std::move(handle));
        };
        if (record.texture) {
            read(record.tail->streaming->artifact, bake::BakedTextureMipBlockName(planned.level));
        } else {
            const RenderMeshStreamingLayout& layout = *record.coarse->streaming;
            for (std::uint32_t chunk = 0U; chunk < layout.lodChunkCount[planned.level] && issued; ++chunk) {
                read(layout.artifact, bake::BakedMeshChunkBlockName(layout.lodFirstChunk[planned.level] + chunk));
            }
        }
        if (!issued || load->reads.empty()) {
            for (const streaming::BackgroundRequestHandle& handle : load->reads) {
                static_cast<void>(reader_->Cancel(handle));
            }
            ++counters_.failedLoads;
            residency_.CompleteLoad(record.id, planned.level, false, lastPlannedFrame_);
            continue;
        }
        pending_.push_back(std::move(load));
    }
}

bool RuntimeContentStreamer::AdmitRebuild(const Record& record, const SceneRenderer& renderer,
    std::uint64_t newBytes, std::uint64_t compositionBytes) {
    std::uint64_t workingBytes = renderer.Resources().PendingUploadBytes();
    const auto add = [&](std::uint64_t bytes) {
        workingBytes += std::min(bytes, std::numeric_limits<std::uint64_t>::max() - workingBytes);
    };
    for (const auto& item : records_) add(item.second->gpuBytes);
    for (const auto& item : retired_) add(item.bytes);
    add(newBytes); // New GPU resource, before the old one can be destroyed.
    add(newBytes); // Upload copy, retained until the backend release callback.
    add(compositionBytes);
    if (workingBytes > settings_.budgetBytes) {
        if (newBytes >= record.gpuBytes) {
            ++counters_.rebuildBudgetDeferrals;
            return false;
        }
        // A reduced budget must still allow eviction to the permanent floor.
        counters_.budgetContractionOverrunBytes = std::max(counters_.budgetContractionOverrunBytes,
            workingBytes - settings_.budgetBytes);
    }
    counters_.peakAdmittedRebuildBytes = std::max(counters_.peakAdmittedRebuildBytes, workingBytes);
    return true;
}

RuntimeContentStreamer::RebuildResult RuntimeContentStreamer::RebuildTexture(
    Record& record,
    SceneRenderer& sceneRenderer,
    RuntimeStreamedTextureMap& textures,
    std::uint32_t level,
    std::uint64_t& uploaded) {
    std::vector<std::span<const std::uint8_t>> levels;
    for (std::uint32_t mip = level; mip < record.floor; ++mip) {
        if (record.levelData[mip].size() != 1U) {
            return RebuildResult::Failed;
        }
        levels.emplace_back(record.levelData[mip].front());
    }
    RenderTextureAssetData composed{};
    if (!bake::ComposeBakedTextureLevels(*record.tail, levels, composed) ||
        composed.gpuBlocks->blocks.size() > std::numeric_limits<std::uint32_t>::max()) {
        return RebuildResult::Failed;
    }
    if (!AdmitRebuild(record, sceneRenderer, composed.gpuBlocks->blocks.size(),
        composed.gpuBlocks->blocks.capacity())) return RebuildResult::Deferred;
    const bgfx::Memory* memory = sceneRenderer.Resources().CopyUploadMemory(
        composed.gpuBlocks->blocks.data(), static_cast<std::uint32_t>(composed.gpuBlocks->blocks.size()));
    const RenderTextureHandle handle =
        sceneRenderer.Resources().RegisterTexture(composed.MakeDesc(memory, record.textureKey.colorSpace));
    if (!handle.IsValid()) {
        return RebuildResult::Failed;
    }
    const auto entry = textures.find(record.textureKey);
    sceneRenderer.ResourceMap().UnbindTextureHandle(record.textureHandle);
    RetainRetired(record);
    sceneRenderer.Resources().DestroyTexture(record.textureHandle);
    sceneRenderer.ResourceMap().BindTexture(record.textureKey.assetId, record.textureKey.colorSpace, handle);
    entry->second.handle = handle;
    record.textureHandle = handle;
    uploaded += composed.gpuBlocks->blocks.size();
    return RebuildResult::Succeeded;
}

RuntimeContentStreamer::RebuildResult RuntimeContentStreamer::RebuildMesh(
    Record& record,
    SceneRenderer& sceneRenderer,
    RuntimeStreamedMeshMap& meshes,
    std::uint32_t level,
    std::uint64_t& uploaded) {
    std::vector<std::vector<std::uint8_t>> chunks;
    for (std::uint32_t lod = level; lod < record.levelData.size(); ++lod) {
        if (record.levelData[lod].empty()) {
            return RebuildResult::Failed;
        }
        chunks.insert(chunks.end(), record.levelData[lod].begin(), record.levelData[lod].end());
    }
    RenderMeshAssetData mesh{};
    if (!AssembleStreamedMeshLevels(*record.coarse->streaming, level, chunks, mesh)) {
        return RebuildResult::Failed;
    }
    RenderMeshDesc desc = mesh.desc;
    if (!record.materialSlots.empty()) {
        desc.materialSlots = record.materialSlots.data();
        desc.materialSlotCount = static_cast<std::uint32_t>(record.materialSlots.size());
    }
    const auto creationStarted = std::chrono::steady_clock::now();
    const auto newBytes = MeshPayloadBytes(desc);
    std::uint64_t compositionBytes =
        mesh.vertices.capacity() * sizeof(RenderStaticMeshVertexP3N3UV2) +
        mesh.tangentVertices.capacity() * sizeof(RenderStaticMeshVertexP3N3T4UV2) +
        mesh.indices16.capacity() * sizeof(std::uint16_t) + mesh.indices32.capacity() * sizeof(std::uint32_t);
    for (const auto& chunk : chunks) compositionBytes += chunk.capacity();
    for (const auto& chunk : mesh.streaming->chunks) compositionBytes += chunk.capacity();
    if (!AdmitRebuild(record, sceneRenderer, newBytes, compositionBytes)) return RebuildResult::Deferred;
    const RenderMeshHandle handle = sceneRenderer.Resources().RegisterMesh(desc);
    counters_.maxMeshBufferCreationMilliseconds = std::max(counters_.maxMeshBufferCreationMilliseconds,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - creationStarted).count());
    if (!handle.IsValid()) {
        return RebuildResult::Failed;
    }
    const auto entry = meshes.find(record.meshKey);
    sceneRenderer.ResourceMap().UnbindMeshHandle(record.meshHandle);
    RetainRetired(record);
    sceneRenderer.Resources().DestroyMesh(record.meshHandle);
    sceneRenderer.ResourceMap().BindMesh(record.meshKey.assetId, handle);
    entry->second.handle = handle;
    record.meshHandle = handle;
    uploaded += newBytes;
    return RebuildResult::Succeeded;
}

void RuntimeContentStreamer::Rebuild(
    SceneRenderer& sceneRenderer,
    RuntimeStreamedTextureMap& textures,
    RuntimeStreamedMeshMap& meshes) {
    // The runtime cache may have destroyed or replaced a resource since it was tracked; such a
    // record is stale and goes.
    std::vector<std::uint64_t> stale;
    std::vector<Record*> due;
    for (const auto& [id, record] : records_) {
        const bool alive = record->texture
            ? [&] {
                  const auto entry = textures.find(record->textureKey);
                  return entry != textures.end() && entry->second.handle == record->textureHandle;
              }()
            : [&] {
                  const auto entry = meshes.find(record->meshKey);
                  return entry != meshes.end() && entry->second.handle == record->meshHandle;
              }();
        if (!alive) {
            stale.push_back(id);
            continue;
        }
        if (residency_.ResidentLevel(id) != record->uploadedLevel && !record->rebuildFailed) {
            due.push_back(record.get());
        }
    }
    for (const std::uint64_t id : stale) {
        Forget(id);
    }
    // Most important first, so a capped frame spends its uploads where they show.
    std::ranges::sort(due, [](const Record* lhs, const Record* rhs) {
        return lhs->requestPriority > rhs->requestPriority || (lhs->requestPriority == rhs->requestPriority && lhs->id < rhs->id);
    });
    std::uint32_t& rebuilt = frameRebuilds_;
    std::uint64_t& uploaded = frameUploadedBytes_;
    std::uint64_t& transient = frameTransientBytes_;
    const std::uint64_t previouslyUploaded = uploaded;
    for (Record* record : due) {
        if (rebuilt >= settings_.maxRebuildsPerFrame ||
            (uploaded != 0U && uploaded >= settings_.maxUploadBytesPerFrame) ||
            (transient != 0U && settings_.maxTransientBytesPerFrame != 0U && transient >= settings_.maxTransientBytesPerFrame)) {
            break;
        }
        const std::uint32_t level = residency_.ResidentLevel(record->id);
        std::uint64_t requiredBytes = 0U;
        if (record->texture) {
            requiredBytes = record->tail->gpuBlocks->blocks.size();
            for (std::uint32_t mip = level; mip < record->floor; ++mip) {
                for (const auto& block : record->levelData[mip]) {
                    requiredBytes += block.size();
                }
            }
        } else {
            for (std::uint32_t lod = level; lod < record->levelData.size(); ++lod) {
                requiredBytes += record->coarse->streaming->lodGeometryBytes[lod];
            }
        }
        // Reserve the next old-resource and staging charge before creating its replacement.
        // The first indivisible rebuild may exceed the per-frame limit on its own.
        if (rebuilt != 0U && settings_.maxTransientBytesPerFrame != 0U) {
            const auto remaining = settings_.maxTransientBytesPerFrame - transient;
            if (record->gpuBytes > remaining || requiredBytes > remaining - record->gpuBytes) {
                break;
            }
        }
        // Keep the next resource for a fresh frame if it does not fit. The first resource
        // is indivisible and may exceed the byte limit, but cannot share that frame.
        if (rebuilt != 0U && requiredBytes > settings_.maxUploadBytesPerFrame - uploaded) {
            break;
        }
        std::uint64_t compositionBytes = requiredBytes;
        if (!record->texture) {
            for (std::uint32_t lod = level; lod < record->levelData.size(); ++lod)
                for (const auto& block : record->levelData[lod]) compositionBytes += 2U * block.size();
        }
        if (!AdmitRebuild(*record, sceneRenderer, requiredBytes, compositionBytes)) continue;
        const std::uint64_t uploadedBefore = uploaded;
        const auto rebuildStarted = std::chrono::steady_clock::now();
        const RebuildResult result = record->texture ? RebuildTexture(*record, sceneRenderer, textures, level, uploaded)
                                               : RebuildMesh(*record, sceneRenderer, meshes, level, uploaded);
        counters_.lastRebuildMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - rebuildStarted).count();
        double& maximum = record->texture ? counters_.maxTextureRebuildMilliseconds : counters_.maxMeshRebuildMilliseconds;
        maximum = std::max(maximum, counters_.lastRebuildMilliseconds);
        if (result == RebuildResult::Succeeded) {
            const std::uint64_t newBytes = uploaded - uploadedBefore;
            if (newBytes > settings_.maxUploadBytesPerFrame) {
                ++counters_.oversizedUploads;
            }
            transient += record->gpuBytes + newBytes;
            record->gpuBytes = newBytes;
            record->uploadedLevel = level;
            ++rebuilt;
            ++counters_.rebuilds;
        } else if (result == RebuildResult::Failed) {
            // Bytes that do not compose are a damaged pack; keep drawing what is there.
            record->rebuildFailed = true;
        }
    }
    counters_.uploadedBytes += uploaded - previouslyUploaded;
    counters_.peakUploadBytesPerFrame = std::max(counters_.peakUploadBytesPerFrame, uploaded);
    if (transient != 0U) {
        const kb::assets::streaming::StreamingResidencyStats residency = residency_.Stats();
        counters_.peakRebuildTransientBytes = std::max(counters_.peakRebuildTransientBytes, transient);
        counters_.peakCommittedWithTransientBytes = std::max(counters_.peakCommittedWithTransientBytes,
            residency.residentBytes + residency.inFlightBytes + transient);
    }
}

void RuntimeContentStreamer::Update(
    const RuntimeContentStreamingFrame& frame,
    SceneRenderer& sceneRenderer,
    RuntimeStreamedTextureMap& textures,
    RuntimeStreamedMeshMap& meshes) {
    std::erase_if(retired_, [&](const RetiredResource& resource) {
        return !sceneRenderer.Resources().RetainsTexture(resource.texture) &&
            !sceneRenderer.Resources().RetainsMesh(resource.mesh);
    });
    if (records_.empty()) {
        materialTextures_.clear();
        materialLessTextures_.clear();
        return;
    }
    if (uploadFrame_ != frame.frame) {
        uploadFrame_ = frame.frame;
        frameRebuilds_ = 0U;
        frameUploadedBytes_ = 0U;
        frameTransientBytes_ = 0U;
    }
    GatherFeedback(frame, sceneRenderer);
    materialTextures_.clear();
    materialLessTextures_.clear();
    CollectLoads(frame.frame);
    // One plan per frame, however many views submit; later views only add their wishes.
    if (frame.frame != lastPlannedFrame_ && reader_ != nullptr) {
        lastPlannedFrame_ = frame.frame;
        const kb::assets::streaming::StreamingPlan plan = residency_.Plan(frame.frame,
            kb::assets::streaming::StreamingPlanLimits{ .maxLoadsInFlight = settings_.maxLoadsInFlight });
        ApplyEvictions(plan);
        StartLoads(plan);
    }
    Rebuild(sceneRenderer, textures, meshes);
}

void RuntimeContentStreamer::ReleaseScene(std::uint64_t sceneId) noexcept {
    std::vector<std::uint64_t> ids;
    for (const auto& [id, record] : records_) {
        if (record->sceneId == sceneId) {
            ids.push_back(id);
        }
    }
    for (const std::uint64_t id : ids) {
        Forget(id);
    }
}

void RuntimeContentStreamer::ReleaseAll() noexcept {
    std::vector<std::uint64_t> ids;
    for (const auto& [id, record] : records_) {
        static_cast<void>(record);
        ids.push_back(id);
    }
    for (const std::uint64_t id : ids) {
        Forget(id);
    }
    ForgetFiles();
}

std::int32_t RuntimeContentStreamer::UploadedLevel(RuntimeTextureAssetKey key) const noexcept {
    const auto id = textureIds_.find(key);
    return id == textureIds_.end() ? -1 : static_cast<std::int32_t>(records_.at(id->second)->uploadedLevel);
}

std::int32_t RuntimeContentStreamer::UploadedLevel(RuntimeAssetKey key) const noexcept {
    const auto id = meshIds_.find(key);
    return id == meshIds_.end() ? -1 : static_cast<std::int32_t>(records_.at(id->second)->uploadedLevel);
}

RuntimeContentStreamingStats RuntimeContentStreamer::Stats() const {
    RuntimeContentStreamingStats stats = counters_;
    stats.residency = residency_.Stats();
    for (const auto& resource : retired_) stats.pendingGpuReleaseBytes += resource.bytes;
    stats.streamedTextures = static_cast<std::uint32_t>(textureIds_.size());
    stats.streamedMeshes = static_cast<std::uint32_t>(meshIds_.size());
    stats.loadsInFlight = static_cast<std::uint32_t>(pending_.size());
    return stats;
}

} // namespace kb::render
