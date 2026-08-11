#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SkeletalMeshAsset.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

enum class SkeletalMeshEditorDetailsAction : std::uint8_t {
    None,
    PreviewLod,
    BoundsMode,
    FixedBoundsCenter,
    FixedBoundsExtents,
    LodScreenCoverage,
    SectionMaterial,
};

struct SkeletalMeshEditorDetailsField {
    std::string label;
    std::string value;
    SkeletalMeshEditorDetailsAction action = SkeletalMeshEditorDetailsAction::None;
    std::uint32_t lodIndex = 0U;
    std::uint32_t sectionIndex = 0U;
    std::uint64_t assetId = 0U;
};

struct SkeletalMeshEditorDetailsSection {
    std::string title;
    std::vector<SkeletalMeshEditorDetailsField> fields;
    bool expanded = true;
};

struct SkeletalMeshEditorDetailsModel {
    std::string title;
    std::vector<SkeletalMeshEditorDetailsSection> sections;
};

class SkeletalMeshEditorDetailsState {
public:
    void SetDocument(
        const kb::scene::SkeletalMeshAsset& mesh,
        const kb::scene::SkeletonAsset& skeleton,
        const kb::assets::AssetMetadata& meshMetadata) {
        mesh_ = mesh;
        skeleton_ = skeleton;
        meshMetadata_ = meshMetadata;
        cachedAssetModel_.reset();
        cachedBoneModels_.clear();
        RebuildBoneInfluenceStats();
        hasDocument_ = true;
        skeletonDocument_ = false;
    }

    void SetSkeletonDocument(
        const kb::scene::SkeletonAsset& skeleton,
        const kb::assets::AssetMetadata& skeletonMetadata,
        const kb::assets::AssetMetadata* previewMeshMetadata) {
        mesh_ = {};
        skeleton_ = skeleton;
        meshMetadata_ = {};
        skeletonMetadata_ = skeletonMetadata;
        previewMeshName_ = previewMeshMetadata == nullptr
            ? std::string{ "None" }
            : (previewMeshMetadata->virtualPath.filename().empty()
                ? previewMeshMetadata->name
                : previewMeshMetadata->virtualPath.filename().string());
        cachedAssetModel_.reset();
        cachedBoneModels_.clear();
        boneInfluenceStats_.clear();
        hasDocument_ = true;
        skeletonDocument_ = true;
    }

    [[nodiscard]] SkeletalMeshEditorDetailsModel Build(
        kb::scene::SkeletonBoneId selectedBone,
        std::string_view selectedSocket) const {
        if (!hasDocument_) return {};
        if (selectedBone != 0U) {
            if (const kb::scene::SkeletonBone* bone = FindBone(selectedBone); bone != nullptr) return BuildBone(*bone);
        }
        if (!selectedSocket.empty()) {
            if (const kb::scene::SkeletonSocket* socket = FindSocket(selectedSocket); socket != nullptr) return BuildSocket(*socket);
        }
        return BuildAsset();
    }

    [[nodiscard]] const std::vector<kb::scene::SkeletalMeshMorphTarget>& MorphTargets() const noexcept {
        return skeletonDocument_ ? emptyMorphTargets_ : mesh_.morphTargets;
    }

    [[nodiscard]] bool ToggleSection(std::string_view title) {
        const std::string key{ title };
        if (collapsedSections_.erase(key) == 0U) collapsedSections_.insert(key);
        cachedAssetModel_.reset();
        cachedBoneModels_.clear();
        scrollOffset_ = 0;
        return true;
    }

    [[nodiscard]] bool IsSectionExpanded(std::string_view title) const {
        return !collapsedSections_.contains(std::string{ title });
    }

    [[nodiscard]] int ScrollOffset() const noexcept { return scrollOffset_; }
    [[nodiscard]] bool IsScrollbarDragging() const noexcept { return scrollbarDragging_; }
    [[nodiscard]] bool SetScrollOffset(int offset, int maximum) noexcept {
        const int clamped = std::clamp(offset, 0, std::max(0, maximum));
        if (scrollOffset_ == clamped) return false;
        scrollOffset_ = clamped;
        return true;
    }
    void BeginScrollbarDrag(int y) noexcept {
        scrollbarDragging_ = true;
        scrollbarDragY_ = y;
        scrollbarDragStartOffset_ = scrollOffset_;
    }
    void DragScrollbar(int y, int trackTravel, int maximum) noexcept {
        if (!scrollbarDragging_) return;
        const int delta = trackTravel <= 0 || maximum <= 0
            ? 0 : ((y - scrollbarDragY_) * maximum) / trackTravel;
        static_cast<void>(SetScrollOffset(scrollbarDragStartOffset_ + delta, maximum));
    }
    void EndScrollbarDrag() noexcept { scrollbarDragging_ = false; }

private:
    struct BoneInfluenceStats {
        std::uint64_t influencedVertices = 0U;
        double totalWeight = 0.0;
        float peakWeight = 0.0F;
    };

    void RebuildBoneInfluenceStats() {
        boneInfluenceStats_.clear();
        boneInfluenceStats_.reserve(skeleton_.bones.size());
        for (const kb::scene::SkeletonBone& bone : skeleton_.bones) {
            boneInfluenceStats_.try_emplace(bone.id);
        }

        struct VertexBoneWeight {
            kb::scene::SkeletonBoneId boneId = 0U;
            float weight = 0.0F;
        };
        for (const kb::scene::SkeletalMeshLod& lod : mesh_.lods) {
            std::vector<std::uint8_t> visitedVertices(lod.vertices.size(), 0U);
            for (const kb::scene::SkeletalMeshSection& section : lod.sections) {
                for (std::uint32_t offset = 0U; offset < section.indexCount; ++offset) {
                    const std::size_t indexPosition = static_cast<std::size_t>(section.firstIndex) + offset;
                    if (indexPosition >= lod.indices.size()) continue;
                    const std::uint32_t vertexIndex = lod.indices[indexPosition];
                    if (vertexIndex >= lod.vertices.size() || visitedVertices[vertexIndex] != 0U) continue;
                    visitedVertices[vertexIndex] = 1U;

                    const kb::scene::SkeletalMeshVertex& vertex = lod.vertices[vertexIndex];
                    std::array<VertexBoneWeight, 4U> vertexBoneWeights{};
                    std::size_t vertexBoneCount = 0U;
                    for (std::size_t influence = 0U; influence < vertex.jointWeights.size(); ++influence) {
                        const std::uint16_t joint = vertex.jointIndices[influence];
                        if (joint >= section.boneMap.size()) continue;
                        const float weight = vertex.jointWeights[influence];
                        const kb::scene::SkeletonBoneId boneId = section.boneMap[joint];
                        const auto vertexBoneEnd = vertexBoneWeights.begin() + vertexBoneCount;
                        auto existing = std::find_if(
                            vertexBoneWeights.begin(),
                            vertexBoneEnd,
                            [boneId](const VertexBoneWeight& candidate) { return candidate.boneId == boneId; });
                        if (existing != vertexBoneEnd) {
                            existing->weight += weight;
                        } else {
                            vertexBoneWeights[vertexBoneCount++] = { .boneId = boneId, .weight = weight };
                        }
                    }
                    for (std::size_t index = 0U; index < vertexBoneCount; ++index) {
                        const VertexBoneWeight& influence = vertexBoneWeights[index];
                        if (influence.weight <= 0.0F) continue;
                        BoneInfluenceStats& stats = boneInfluenceStats_[influence.boneId];
                        ++stats.influencedVertices;
                        stats.totalWeight += influence.weight;
                        stats.peakWeight = std::max(stats.peakWeight, influence.weight);
                    }
                }
            }
        }
    }

    [[nodiscard]] static std::string Number(std::uint64_t value) { return std::to_string(value); }
    [[nodiscard]] static std::string Number(float value) {
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::setprecision(5) << value;
        return output.str();
    }
    [[nodiscard]] static std::string Vector(const kb::math::Vec3& value) {
        return "(" + Number(value.x) + ", " + Number(value.y) + ", " + Number(value.z) + ")";
    }
    [[nodiscard]] static std::string Rotation(const kb::math::Quat& value) {
        return "(" + Number(value.x) + ", " + Number(value.y) + ", " + Number(value.z) + ", " + Number(value.w) + ")";
    }
    [[nodiscard]] static SkeletalMeshEditorDetailsSection BoundsSection(
        std::string title, const kb::scene::SkeletalMeshBounds& bounds) {
        return SkeletalMeshEditorDetailsSection{
            .title = std::move(title),
            .fields = {
                { "Center", Vector(bounds.center) },
                { "Extents", Vector(bounds.extents) },
            },
        };
    }

    [[nodiscard]] const kb::scene::SkeletonBone* FindBone(kb::scene::SkeletonBoneId id) const noexcept {
        for (const kb::scene::SkeletonBone& bone : skeleton_.bones) {
            if (bone.id == id) return &bone;
        }
        return nullptr;
    }

    [[nodiscard]] const kb::scene::SkeletonSocket* FindSocket(std::string_view name) const noexcept {
        for (const kb::scene::SkeletonSocket& socket : skeleton_.sockets) {
            if (socket.name == name) return &socket;
        }
        return nullptr;
    }

    [[nodiscard]] SkeletalMeshEditorDetailsModel BuildAsset() const {
        if (cachedAssetModel_.has_value()) return *cachedAssetModel_;
        if (skeletonDocument_) {
            const kb::scene::SkeletonAssetValidationResult validation =
                kb::scene::ValidateSkeletonAsset(skeleton_);
            SkeletalMeshEditorDetailsModel model{ .title = "Skeleton" };
            model.sections.push_back({ "Asset", {
                { "Name", skeletonMetadata_.name },
                { "Path", skeletonMetadata_.virtualPath.generic_string() },
                { "Bones", Number(skeleton_.bones.size()) },
                { "Sockets", Number(skeleton_.sockets.size()) },
                { "Compatibility", Number(kb::scene::SkeletonCompatibilitySignature(skeleton_)) },
                { "Preview Mesh", previewMeshName_ },
            } });
            model.sections.push_back({ "Import Settings", {
                { "Category", skeletonMetadata_.importCategory },
                { "Source", skeletonMetadata_.physicalPath.generic_string() },
                { "Content Hash", Number(skeletonMetadata_.contentHash) },
                { "Runtime Loadable", skeletonMetadata_.runtimeLoadable ? "Yes" : "No" },
            } });
            model.sections.push_back({ "Validation", {
                { "Skeleton", validation.valid ? "Valid" : validation.error },
            } });
            ApplyExpansion(model);
            cachedAssetModel_ = model;
            return model;
        }
        SkeletalMeshEditorDetailsModel model{ .title = "Skeletal Mesh" };
        model.sections.push_back({ "Asset", {
            { "Name", meshMetadata_.name },
            { "Path", meshMetadata_.virtualPath.generic_string() },
            { "Skeleton", Number(mesh_.skeletonAssetId) },
            { "Compatibility", Number(mesh_.skeletonCompatibilitySignature) },
            { "LODs", Number(mesh_.lods.size()) },
            { "Morph Targets", Number(mesh_.morphTargets.size()) },
        } });

        model.sections.push_back({ "Materials", {} });
        std::vector<SkeletalMeshEditorDetailsField>& materialFields = model.sections.back().fields;
        for (std::size_t lodIndex = 0U; lodIndex < mesh_.lods.size(); ++lodIndex) {
            const kb::scene::SkeletalMeshLod& lod = mesh_.lods[lodIndex];
            for (std::size_t sectionIndex = 0U; sectionIndex < lod.sections.size(); ++sectionIndex) {
                const kb::scene::SkeletalMeshSection& section = lod.sections[sectionIndex];
                materialFields.push_back(SkeletalMeshEditorDetailsField{
                    .label = "LOD " + Number(lodIndex) + " / Section " + Number(sectionIndex),
                    .value = section.materialAssetId == 0U ? "None" : Number(section.materialAssetId),
                    .action = SkeletalMeshEditorDetailsAction::SectionMaterial,
                    .lodIndex = static_cast<std::uint32_t>(lodIndex),
                    .sectionIndex = static_cast<std::uint32_t>(sectionIndex),
                    .assetId = section.materialAssetId,
                });
            }
        }

        model.sections.push_back({ "LOD Picker", {
            { "Preview LOD", "Auto", SkeletalMeshEditorDetailsAction::PreviewLod },
            { "Number of LODs", Number(mesh_.lods.size()) },
        } });
        for (std::size_t index = 0U; index < mesh_.lods.size(); ++index) {
            const kb::scene::SkeletalMeshLod& lod = mesh_.lods[index];
            model.sections.push_back({ "LOD " + Number(index), {
                { "Vertices", Number(lod.vertices.size()) },
                { "Indices", Number(lod.indices.size()) },
                { "Triangles", Number(lod.indices.size() / 3U) },
                { "Sections", Number(lod.sections.size()) },
                { "Required Bones", Number(lod.requiredBones.size()) },
                { "Per-Bone Bounds", Number(lod.boneBounds.size()) },
                { "Min Screen Coverage", Number(lod.minScreenCoverage),
                    SkeletalMeshEditorDetailsAction::LodScreenCoverage,
                    static_cast<std::uint32_t>(index) },
            } });
            for (std::size_t sectionIndex = 0U; sectionIndex < lod.sections.size(); ++sectionIndex) {
                const kb::scene::SkeletalMeshSection& section = lod.sections[sectionIndex];
                std::vector<SkeletalMeshEditorDetailsField>& fields = model.sections.back().fields;
                const std::string prefix = "Section " + Number(sectionIndex) + " ";
                fields.push_back({ prefix + "Material", section.materialAssetId == 0U
                    ? "None" : Number(section.materialAssetId),
                    SkeletalMeshEditorDetailsAction::SectionMaterial,
                    static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(sectionIndex),
                    section.materialAssetId });
                fields.push_back({ prefix + "First Index", Number(static_cast<std::uint64_t>(section.firstIndex)) });
                fields.push_back({ prefix + "Triangles", Number(static_cast<std::uint64_t>(section.indexCount / 3U)) });
                fields.push_back({ prefix + "Palette Bones", Number(section.boneMap.size()) });
            }
        }

        model.sections.push_back({ "Morph Targets", {
            { "Count", Number(mesh_.morphTargets.size()) },
        } });
        for (const kb::scene::SkeletalMeshMorphTarget& morph : mesh_.morphTargets) {
            model.sections.back().fields.push_back({
                morph.name,
                "LOD " + Number(static_cast<std::uint64_t>(morph.lodIndex)) + ", " +
                    Number(morph.deltas.size()) + " deltas",
            });
        }
        model.sections.push_back({ "Bounds", {
            { "Mode", mesh_.boundsMode == kb::scene::SkeletalMeshBoundsMode::Fixed
                ? "Fixed" : "Imported Conservative", SkeletalMeshEditorDetailsAction::BoundsMode },
            { "Imported Center", Vector(mesh_.conservativeBounds.center) },
            { "Imported Extents", Vector(mesh_.conservativeBounds.extents) },
            { "Fixed Center", Vector(mesh_.fixedBounds.center),
                SkeletalMeshEditorDetailsAction::FixedBoundsCenter },
            { "Fixed Extents", Vector(mesh_.fixedBounds.extents),
                SkeletalMeshEditorDetailsAction::FixedBoundsExtents },
        } });
        model.sections.push_back({ "Import Settings", {
            { "Category", meshMetadata_.importCategory },
            { "Source", meshMetadata_.physicalPath.generic_string() },
            { "Content Hash", Number(meshMetadata_.contentHash) },
            { "Runtime Loadable", meshMetadata_.runtimeLoadable ? "Yes" : "No" },
        } });
        const kb::scene::SkeletalMeshAssetValidationResult meshValidation = kb::scene::ValidateSkeletalMeshAsset(mesh_);
        const kb::scene::SkeletonAssetValidationResult skeletonValidation = kb::scene::ValidateSkeletonAsset(skeleton_);
        model.sections.push_back({ "Import / Reimport Diagnostics", {
            { "Skeletal Mesh", meshValidation.valid ? "Valid" : meshValidation.error },
            { "Skeleton", skeletonValidation.valid ? "Valid" : skeletonValidation.error },
        } });
        ApplyExpansion(model);
        cachedAssetModel_ = model;
        return model;
    }

    void ApplyExpansion(SkeletalMeshEditorDetailsModel& model) const {
        for (SkeletalMeshEditorDetailsSection& section : model.sections) {
            section.expanded = IsSectionExpanded(section.title);
        }
    }

    [[nodiscard]] SkeletalMeshEditorDetailsModel BuildBone(const kb::scene::SkeletonBone& bone) const {
        if (const auto cached = cachedBoneModels_.find(bone.id); cached != cachedBoneModels_.end()) {
            return cached->second;
        }
        std::string parent = "None";
        if (bone.parentIndex >= 0 && static_cast<std::size_t>(bone.parentIndex) < skeleton_.bones.size()) {
            parent = skeleton_.bones[static_cast<std::size_t>(bone.parentIndex)].name;
        }
        const auto statsIterator = boneInfluenceStats_.find(bone.id);
        const BoneInfluenceStats stats = statsIterator == boneInfluenceStats_.end()
            ? BoneInfluenceStats{}
            : statsIterator->second;
        SkeletalMeshEditorDetailsModel model{
            .title = "Bone: " + bone.name,
            .sections = {{ "Bone", {
                { "Id", Number(bone.id) },
                { "Parent", std::move(parent) },
                { "Reference Position", Vector(bone.referencePose.position) },
                { "Reference Rotation", Rotation(bone.referencePose.rotation) },
                { "Reference Scale", Vector(bone.referencePose.scale) },
            } }},
        };
        if (!skeletonDocument_) {
            std::vector<SkeletalMeshEditorDetailsField>& fields = model.sections.front().fields;
            fields.push_back({ "Influenced Vertices", Number(stats.influencedVertices) });
            fields.push_back({ "Average Weight", Number(stats.influencedVertices == 0U
                ? 0.0F
                : static_cast<float>(stats.totalWeight / stats.influencedVertices)) });
            fields.push_back({ "Peak Weight", Number(stats.peakWeight) });
        }
        ApplyExpansion(model);
        cachedBoneModels_.emplace(bone.id, model);
        return model;
    }

    [[nodiscard]] SkeletalMeshEditorDetailsModel BuildSocket(const kb::scene::SkeletonSocket& socket) const {
        const kb::scene::SkeletonBone* bone = FindBone(socket.boneId);
        SkeletalMeshEditorDetailsModel model{
            .title = "Socket: " + socket.name,
            .sections = {{ "Socket", {
                { "Bone", bone == nullptr ? Number(socket.boneId) : bone->name },
                { "Bone Id", Number(socket.boneId) },
                { "Local Position", Vector(socket.localTransform.position) },
                { "Local Rotation", Rotation(socket.localTransform.rotation) },
                { "Local Scale", Vector(socket.localTransform.scale) },
            } }},
        };
        ApplyExpansion(model);
        return model;
    }

    kb::scene::SkeletalMeshAsset mesh_{};
    kb::scene::SkeletonAsset skeleton_{};
    kb::assets::AssetMetadata meshMetadata_{};
    kb::assets::AssetMetadata skeletonMetadata_{};
    std::string previewMeshName_;
    std::vector<kb::scene::SkeletalMeshMorphTarget> emptyMorphTargets_;
    mutable std::optional<SkeletalMeshEditorDetailsModel> cachedAssetModel_{};
    mutable std::unordered_map<kb::scene::SkeletonBoneId, SkeletalMeshEditorDetailsModel> cachedBoneModels_{};
    std::unordered_map<kb::scene::SkeletonBoneId, BoneInfluenceStats> boneInfluenceStats_{};
    std::unordered_set<std::string> collapsedSections_{};
    int scrollOffset_ = 0;
    int scrollbarDragY_ = 0;
    int scrollbarDragStartOffset_ = 0;
    bool scrollbarDragging_ = false;
    bool hasDocument_ = false;
    bool skeletonDocument_ = false;
};

} // namespace kb::editor
