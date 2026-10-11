#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "localization/EditorLocalization.hpp"
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
            ? std::string{ EditorText("skeletalMeshEditor.details.none", "None") }
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
                { std::string{ EditorText("skeletalMeshEditor.details.center", "Center") }, Vector(bounds.center) },
                { std::string{ EditorText("skeletalMeshEditor.details.extents", "Extents") }, Vector(bounds.extents) },
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
            SkeletalMeshEditorDetailsModel model{ .title = std::string{ EditorText("skeletalMeshEditor.details.skeleton", "Skeleton") } };
            model.sections.push_back({ std::string{ EditorText("skeletalMeshEditor.details.asset", "Asset") }, {
                { std::string{ EditorText("skeletalMeshEditor.details.name", "Name") }, skeletonMetadata_.name },
                { std::string{ EditorText("skeletalMeshEditor.details.path", "Path") }, skeletonMetadata_.virtualPath.generic_string() },
                { std::string{ EditorText("skeletalMeshEditor.details.bones", "Bones") }, Number(skeleton_.bones.size()) },
                { std::string{ EditorText("skeletalMeshEditor.details.sockets", "Sockets") }, Number(skeleton_.sockets.size()) },
                { std::string{ EditorText("skeletalMeshEditor.details.compatibility", "Compatibility") }, Number(kb::scene::SkeletonCompatibilitySignature(skeleton_)) },
                { std::string{ EditorText("skeletalMeshEditor.details.previewMesh", "Preview Mesh") }, previewMeshName_ },
            } });
            model.sections.push_back({ std::string{ EditorText("skeletalMeshEditor.details.importSettings", "Import Settings") }, {
                { std::string{ EditorText("skeletalMeshEditor.details.category", "Category") }, skeletonMetadata_.importCategory },
                { std::string{ EditorText("skeletalMeshEditor.details.source", "Source") }, skeletonMetadata_.physicalPath.generic_string() },
                { std::string{ EditorText("skeletalMeshEditor.details.contentHash", "Content Hash") }, Number(skeletonMetadata_.contentHash) },
                { std::string{ EditorText("skeletalMeshEditor.details.runtimeLoadable", "Runtime Loadable") }, skeletonMetadata_.runtimeLoadable ? std::string{ EditorText("common.yes", "Yes") } : std::string{ EditorText("common.no", "No") } },
            } });
            model.sections.push_back({ std::string{ EditorText("skeletalMeshEditor.details.validation", "Validation") }, {
                { std::string{ EditorText("skeletalMeshEditor.details.skeleton", "Skeleton") }, validation.valid ? std::string{ EditorText("skeletalMeshEditor.details.valid", "Valid") } : validation.error },
            } });
            ApplyExpansion(model);
            cachedAssetModel_ = model;
            return model;
        }
        SkeletalMeshEditorDetailsModel model{ .title = std::string{ EditorText("skeletalMeshEditor.details.skeletalMesh", "Skeletal Mesh") } };
        model.sections.push_back({ std::string{ EditorText("skeletalMeshEditor.details.asset", "Asset") }, {
            { std::string{ EditorText("skeletalMeshEditor.details.name", "Name") }, meshMetadata_.name },
            { std::string{ EditorText("skeletalMeshEditor.details.path", "Path") }, meshMetadata_.virtualPath.generic_string() },
            { std::string{ EditorText("skeletalMeshEditor.details.skeleton", "Skeleton") }, Number(mesh_.skeletonAssetId) },
            { std::string{ EditorText("skeletalMeshEditor.details.compatibility", "Compatibility") }, Number(mesh_.skeletonCompatibilitySignature) },
            { std::string{ EditorText("skeletalMeshEditor.details.lODs", "LODs") }, Number(mesh_.lods.size()) },
            { std::string{ EditorText("skeletalMeshEditor.details.morphTargets", "Morph Targets") }, Number(mesh_.morphTargets.size()) },
        } });

        model.sections.push_back({ std::string{ EditorText("skeletalMeshEditor.details.materials", "Materials") }, {} });
        std::vector<SkeletalMeshEditorDetailsField>& materialFields = model.sections.back().fields;
        for (std::size_t lodIndex = 0U; lodIndex < mesh_.lods.size(); ++lodIndex) {
            const kb::scene::SkeletalMeshLod& lod = mesh_.lods[lodIndex];
            for (std::size_t sectionIndex = 0U; sectionIndex < lod.sections.size(); ++sectionIndex) {
                const kb::scene::SkeletalMeshSection& section = lod.sections[sectionIndex];
                materialFields.push_back(SkeletalMeshEditorDetailsField{
                    .label = EditorLocalization::Format("skeletalMeshEditor.details.lodSection", "LOD {lod} / Section {section}",
                        { { "lod", Number(lodIndex) }, { "section", Number(sectionIndex) } }),
                    .value = section.materialAssetId == 0U ? std::string{ EditorText("skeletalMeshEditor.details.none", "None") } : Number(section.materialAssetId),
                    .action = SkeletalMeshEditorDetailsAction::SectionMaterial,
                    .lodIndex = static_cast<std::uint32_t>(lodIndex),
                    .sectionIndex = static_cast<std::uint32_t>(sectionIndex),
                    .assetId = section.materialAssetId,
                });
            }
        }

        model.sections.push_back({ std::string{ EditorText("skeletalMeshEditor.details.lodPicker", "LOD Picker") }, {
            { std::string{ EditorText("skeletalMeshEditor.details.previewLOD", "Preview LOD") }, std::string{ EditorText("skeletalMeshEditor.lodAuto", "Auto") }, SkeletalMeshEditorDetailsAction::PreviewLod },
            { std::string{ EditorText("skeletalMeshEditor.details.numberOfLODs", "Number of LODs") }, Number(mesh_.lods.size()) },
        } });
        for (std::size_t index = 0U; index < mesh_.lods.size(); ++index) {
            const kb::scene::SkeletalMeshLod& lod = mesh_.lods[index];
            model.sections.push_back({ EditorLocalization::Format("skeletalMeshEditor.lod", "LOD {index}", { { "index", Number(index) } }), {
                { std::string{ EditorText("skeletalMeshEditor.details.vertices", "Vertices") }, Number(lod.vertices.size()) },
                { std::string{ EditorText("skeletalMeshEditor.details.indices", "Indices") }, Number(lod.indices.size()) },
                { std::string{ EditorText("skeletalMeshEditor.details.triangles", "Triangles") }, Number(lod.indices.size() / 3U) },
                { std::string{ EditorText("skeletalMeshEditor.details.sections", "Sections") }, Number(lod.sections.size()) },
                { std::string{ EditorText("skeletalMeshEditor.details.requiredBones", "Required Bones") }, Number(lod.requiredBones.size()) },
                { std::string{ EditorText("skeletalMeshEditor.details.perBoneBounds", "Per-Bone Bounds") }, Number(lod.boneBounds.size()) },
                { std::string{ EditorText("skeletalMeshEditor.details.minScreenCoverage", "Min Screen Coverage") }, Number(lod.minScreenCoverage),
                    SkeletalMeshEditorDetailsAction::LodScreenCoverage,
                    static_cast<std::uint32_t>(index) },
            } });
            for (std::size_t sectionIndex = 0U; sectionIndex < lod.sections.size(); ++sectionIndex) {
                const kb::scene::SkeletalMeshSection& section = lod.sections[sectionIndex];
                std::vector<SkeletalMeshEditorDetailsField>& fields = model.sections.back().fields;
                const std::string number = Number(sectionIndex);
                fields.push_back({ EditorLocalization::Format("skeletalMeshEditor.details.sectionMaterial", "Section {index} Material",
                        { { "index", number } }), section.materialAssetId == 0U
                    ? std::string{ EditorText("skeletalMeshEditor.details.none", "None") } : Number(section.materialAssetId),
                    SkeletalMeshEditorDetailsAction::SectionMaterial,
                    static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(sectionIndex),
                    section.materialAssetId });
                fields.push_back({ EditorLocalization::Format("skeletalMeshEditor.details.sectionFirstIndex", "Section {index} First Index",
                    { { "index", number } }), Number(static_cast<std::uint64_t>(section.firstIndex)) });
                fields.push_back({ EditorLocalization::Format("skeletalMeshEditor.details.sectionTriangles", "Section {index} Triangles",
                    { { "index", number } }), Number(static_cast<std::uint64_t>(section.indexCount / 3U)) });
                fields.push_back({ EditorLocalization::Format("skeletalMeshEditor.details.sectionPaletteBones", "Section {index} Palette Bones",
                    { { "index", number } }), Number(section.boneMap.size()) });
            }
        }

        model.sections.push_back({ std::string{ EditorText("skeletalMeshEditor.details.morphTargets", "Morph Targets") }, {
            { std::string{ EditorText("skeletalMeshEditor.details.count", "Count") }, Number(mesh_.morphTargets.size()) },
        } });
        for (const kb::scene::SkeletalMeshMorphTarget& morph : mesh_.morphTargets) {
            model.sections.back().fields.push_back({
                morph.name,
                EditorLocalization::Format("skeletalMeshEditor.details.morphTarget", "LOD {lod}, {deltas} deltas",
                    { { "lod", Number(static_cast<std::uint64_t>(morph.lodIndex)) }, { "deltas", Number(morph.deltas.size()) } }),
            });
        }
        model.sections.push_back({ std::string{ EditorText("skeletalMeshEditor.details.bounds", "Bounds") }, {
            { std::string{ EditorText("skeletalMeshEditor.details.mode", "Mode") }, mesh_.boundsMode == kb::scene::SkeletalMeshBoundsMode::Fixed
                ? std::string{ EditorText("skeletalMeshEditor.details.boundsFixed", "Fixed") } : std::string{ EditorText("skeletalMeshEditor.details.boundsImported", "Imported Conservative") },
                SkeletalMeshEditorDetailsAction::BoundsMode },
            { std::string{ EditorText("skeletalMeshEditor.details.importedCenter", "Imported Center") }, Vector(mesh_.conservativeBounds.center) },
            { std::string{ EditorText("skeletalMeshEditor.details.importedExtents", "Imported Extents") }, Vector(mesh_.conservativeBounds.extents) },
            { std::string{ EditorText("skeletalMeshEditor.details.fixedCenter", "Fixed Center") }, Vector(mesh_.fixedBounds.center),
                SkeletalMeshEditorDetailsAction::FixedBoundsCenter },
            { std::string{ EditorText("skeletalMeshEditor.details.fixedExtents", "Fixed Extents") }, Vector(mesh_.fixedBounds.extents),
                SkeletalMeshEditorDetailsAction::FixedBoundsExtents },
        } });
        model.sections.push_back({ std::string{ EditorText("skeletalMeshEditor.details.importSettings", "Import Settings") }, {
            { std::string{ EditorText("skeletalMeshEditor.details.category", "Category") }, meshMetadata_.importCategory },
            { std::string{ EditorText("skeletalMeshEditor.details.source", "Source") }, meshMetadata_.physicalPath.generic_string() },
            { std::string{ EditorText("skeletalMeshEditor.details.contentHash", "Content Hash") }, Number(meshMetadata_.contentHash) },
            { std::string{ EditorText("skeletalMeshEditor.details.runtimeLoadable", "Runtime Loadable") }, meshMetadata_.runtimeLoadable ? std::string{ EditorText("common.yes", "Yes") } : std::string{ EditorText("common.no", "No") } },
        } });
        const kb::scene::SkeletalMeshAssetValidationResult meshValidation = kb::scene::ValidateSkeletalMeshAsset(mesh_);
        const kb::scene::SkeletonAssetValidationResult skeletonValidation = kb::scene::ValidateSkeletonAsset(skeleton_);
        model.sections.push_back({ std::string{ EditorText("skeletalMeshEditor.details.importReimportDiagnostics", "Import / Reimport Diagnostics") }, {
            { std::string{ EditorText("skeletalMeshEditor.details.skeletalMesh", "Skeletal Mesh") }, meshValidation.valid ? std::string{ EditorText("skeletalMeshEditor.details.valid", "Valid") } : meshValidation.error },
            { std::string{ EditorText("skeletalMeshEditor.details.skeleton", "Skeleton") }, skeletonValidation.valid ? std::string{ EditorText("skeletalMeshEditor.details.valid", "Valid") } : skeletonValidation.error },
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
        std::string parent = std::string{ EditorText("skeletalMeshEditor.details.none", "None") };
        if (bone.parentIndex >= 0 && static_cast<std::size_t>(bone.parentIndex) < skeleton_.bones.size()) {
            parent = skeleton_.bones[static_cast<std::size_t>(bone.parentIndex)].name;
        }
        const auto statsIterator = boneInfluenceStats_.find(bone.id);
        const BoneInfluenceStats stats = statsIterator == boneInfluenceStats_.end()
            ? BoneInfluenceStats{}
            : statsIterator->second;
        SkeletalMeshEditorDetailsModel model{
            .title = EditorLocalization::Format("skeletalMeshEditor.details.boneTitle", "Bone: {name}", { { "name", bone.name } }),
            .sections = {{ std::string{ EditorText("skeletalMeshEditor.details.bone", "Bone") }, {
                { std::string{ EditorText("skeletalMeshEditor.details.id", "Id") }, Number(bone.id) },
                { std::string{ EditorText("skeletalMeshEditor.details.parent", "Parent") }, std::move(parent) },
                { std::string{ EditorText("skeletalMeshEditor.details.referencePosition", "Reference Position") }, Vector(bone.referencePose.position) },
                { std::string{ EditorText("skeletalMeshEditor.details.referenceRotation", "Reference Rotation") }, Rotation(bone.referencePose.rotation) },
                { std::string{ EditorText("skeletalMeshEditor.details.referenceScale", "Reference Scale") }, Vector(bone.referencePose.scale) },
            } }},
        };
        if (!skeletonDocument_) {
            std::vector<SkeletalMeshEditorDetailsField>& fields = model.sections.front().fields;
            fields.push_back({ std::string{ EditorText("skeletalMeshEditor.details.influencedVertices", "Influenced Vertices") }, Number(stats.influencedVertices) });
            fields.push_back({ std::string{ EditorText("skeletalMeshEditor.details.averageWeight", "Average Weight") }, Number(stats.influencedVertices == 0U
                ? 0.0F
                : static_cast<float>(stats.totalWeight / stats.influencedVertices)) });
            fields.push_back({ std::string{ EditorText("skeletalMeshEditor.details.peakWeight", "Peak Weight") }, Number(stats.peakWeight) });
        }
        ApplyExpansion(model);
        cachedBoneModels_.emplace(bone.id, model);
        return model;
    }

    [[nodiscard]] SkeletalMeshEditorDetailsModel BuildSocket(const kb::scene::SkeletonSocket& socket) const {
        const kb::scene::SkeletonBone* bone = FindBone(socket.boneId);
        SkeletalMeshEditorDetailsModel model{
            .title = EditorLocalization::Format("skeletalMeshEditor.details.socketTitle", "Socket: {name}", { { "name", socket.name } }),
            .sections = {{ std::string{ EditorText("skeletalMeshEditor.details.socket", "Socket") }, {
                { std::string{ EditorText("skeletalMeshEditor.details.bone", "Bone") }, bone == nullptr ? Number(socket.boneId) : bone->name },
                { std::string{ EditorText("skeletalMeshEditor.details.boneId", "Bone Id") }, Number(socket.boneId) },
                { std::string{ EditorText("skeletalMeshEditor.details.localPosition", "Local Position") }, Vector(socket.localTransform.position) },
                { std::string{ EditorText("skeletalMeshEditor.details.localRotation", "Local Rotation") }, Rotation(socket.localTransform.rotation) },
                { std::string{ EditorText("skeletalMeshEditor.details.localScale", "Local Scale") }, Vector(socket.localTransform.scale) },
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
