#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SkeletalMeshAsset.hpp"

#include <iomanip>
#include <unordered_set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

struct SkeletalMeshEditorDetailsField {
    std::string label;
    std::string value;
};

struct SkeletalMeshEditorDetailsSection {
    std::string title;
    std::vector<SkeletalMeshEditorDetailsField> fields;
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
        hasDocument_ = true;
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
        return mesh_.morphTargets;
    }

private:
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
        SkeletalMeshEditorDetailsModel model{ .title = "Skeletal Mesh" };
        model.sections.push_back({ "Asset", {
            { "Name", meshMetadata_.name },
            { "Path", meshMetadata_.virtualPath.generic_string() },
            { "Skeleton", Number(mesh_.skeletonAssetId) },
            { "Compatibility", Number(mesh_.skeletonCompatibilitySignature) },
            { "LODs", Number(mesh_.lods.size()) },
            { "Morph Targets", Number(mesh_.morphTargets.size()) },
        } });
        for (std::size_t index = 0U; index < mesh_.lods.size(); ++index) {
            const kb::scene::SkeletalMeshLod& lod = mesh_.lods[index];
            model.sections.push_back({ "LOD " + Number(index), {
                { "Vertices", Number(lod.vertices.size()) },
                { "Triangles", Number(lod.indices.size() / 3U) },
                { "Sections", Number(lod.sections.size()) },
                { "Required Bones", Number(lod.requiredBones.size()) },
                { "Min Screen Coverage", Number(lod.minScreenCoverage) },
            } });
            for (std::size_t sectionIndex = 0U; sectionIndex < lod.sections.size(); ++sectionIndex) {
                const kb::scene::SkeletalMeshSection& section = lod.sections[sectionIndex];
                model.sections.push_back({ "LOD " + Number(index) + " Material " + Number(sectionIndex), {
                    { "Material", Number(section.materialAssetId) },
                    { "First Index", Number(static_cast<std::uint64_t>(section.firstIndex)) },
                    { "Triangles", Number(static_cast<std::uint64_t>(section.indexCount / 3U)) },
                    { "Palette Bones", Number(section.boneMap.size()) },
                } });
            }
        }
        model.sections.push_back(BoundsSection(
            mesh_.boundsMode == kb::scene::SkeletalMeshBoundsMode::Fixed ? "Bounds (Fixed)" : "Bounds (Imported Conservative)",
            mesh_.boundsMode == kb::scene::SkeletalMeshBoundsMode::Fixed ? mesh_.fixedBounds : mesh_.conservativeBounds));
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
        return model;
    }

    [[nodiscard]] SkeletalMeshEditorDetailsModel BuildBone(const kb::scene::SkeletonBone& bone) const {
        std::string parent = "None";
        if (bone.parentIndex >= 0 && static_cast<std::size_t>(bone.parentIndex) < skeleton_.bones.size()) {
            parent = skeleton_.bones[static_cast<std::size_t>(bone.parentIndex)].name;
        }
        std::uint64_t influencedVertices = 0U;
        double totalWeight = 0.0;
        float peakWeight = 0.0F;
        for (const kb::scene::SkeletalMeshLod& lod : mesh_.lods) {
            std::unordered_set<std::uint32_t> visitedVertices;
            for (const kb::scene::SkeletalMeshSection& section : lod.sections) {
                for (std::uint32_t offset = 0U; offset < section.indexCount; ++offset) {
                    const std::uint32_t vertexIndex = lod.indices[section.firstIndex + offset];
                    if (!visitedVertices.insert(vertexIndex).second || vertexIndex >= lod.vertices.size()) continue;
                    const kb::scene::SkeletalMeshVertex& vertex = lod.vertices[vertexIndex];
                    float vertexWeight = 0.0F;
                    for (std::size_t influence = 0U; influence < vertex.jointWeights.size(); ++influence) {
                        const std::uint16_t joint = vertex.jointIndices[influence];
                        if (joint >= section.boneMap.size() || section.boneMap[joint] != bone.id) continue;
                        vertexWeight += vertex.jointWeights[influence];
                    }
                    if (vertexWeight > 0.0F) {
                        ++influencedVertices;
                        totalWeight += vertexWeight;
                        peakWeight = std::max(peakWeight, vertexWeight);
                    }
                }
            }
        }
        return SkeletalMeshEditorDetailsModel{
            .title = "Bone: " + bone.name,
            .sections = {{ "Bone", {
                { "Id", Number(bone.id) },
                { "Parent", std::move(parent) },
                { "Reference Position", Vector(bone.referencePose.position) },
                { "Reference Rotation", Rotation(bone.referencePose.rotation) },
                { "Reference Scale", Vector(bone.referencePose.scale) },
                { "Influenced Vertices", Number(influencedVertices) },
                { "Average Weight", Number(influencedVertices == 0U ? 0.0F : static_cast<float>(totalWeight / influencedVertices)) },
                { "Peak Weight", Number(peakWeight) },
            } }},
        };
    }

    [[nodiscard]] SkeletalMeshEditorDetailsModel BuildSocket(const kb::scene::SkeletonSocket& socket) const {
        const kb::scene::SkeletonBone* bone = FindBone(socket.boneId);
        return SkeletalMeshEditorDetailsModel{
            .title = "Socket: " + socket.name,
            .sections = {{ "Socket", {
                { "Bone", bone == nullptr ? Number(socket.boneId) : bone->name },
                { "Bone Id", Number(socket.boneId) },
                { "Local Position", Vector(socket.localTransform.position) },
                { "Local Rotation", Rotation(socket.localTransform.rotation) },
                { "Local Scale", Vector(socket.localTransform.scale) },
            } }},
        };
    }

    kb::scene::SkeletalMeshAsset mesh_{};
    kb::scene::SkeletonAsset skeleton_{};
    kb::assets::AssetMetadata meshMetadata_{};
    bool hasDocument_ = false;
};

} // namespace kb::editor
