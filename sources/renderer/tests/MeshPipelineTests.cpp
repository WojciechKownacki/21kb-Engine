#include "RendererTestSupport.hpp"

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/frame/RenderPassKind.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "kb/render/scene/SceneRenderer.hpp"

#include <array>
#include <string_view>

namespace kb::render::tests {
namespace {

[[nodiscard]] std::array<float, 16> IdentityMatrix() noexcept {
    return std::array<float, 16>{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
}

[[nodiscard]] std::array<float, 16> TranslationMatrix(float x, float y, float z) noexcept {
    std::array<float, 16> matrix = IdentityMatrix();
    matrix[12] = x;
    matrix[13] = y;
    matrix[14] = z;
    return matrix;
}

void RunMeshPassTypeNamesResolveTest() {
    Require(MeshPassTypeName(MeshPassType::Depth) == std::string_view("Depth"), "MeshPassTypeName did not resolve Depth");
    Require(MeshPassTypeName(MeshPassType::BaseOpaque) == std::string_view("BaseOpaque"), "MeshPassTypeName did not resolve BaseOpaque");
    Require(MeshPassTypeName(MeshPassType::BaseTransparent) == std::string_view("BaseTransparent"), "MeshPassTypeName did not resolve BaseTransparent");
    Require(MeshPassTypeName(MeshPassType::ShadowDepth) == std::string_view("ShadowDepth"), "MeshPassTypeName did not resolve ShadowDepth");
    Require(MeshPassTypeName(MeshPassType::SelectionId) == std::string_view("SelectionId"), "MeshPassTypeName did not resolve SelectionId");
    Require(MeshPassTypeName(MeshPassType::EditorSelection) == std::string_view("EditorSelection"), "MeshPassTypeName did not resolve EditorSelection");
    Require(MeshPassTypeName(MeshPassType::Gizmo) == std::string_view("Gizmo"), "MeshPassTypeName did not resolve Gizmo");
}

void RunStaticMeshVertexLayoutIsDefinedTest() {
    const bgfx::VertexLayout layout = RenderStaticMeshVertexLayout();
    Require(layout.getStride() == sizeof(RenderStaticMeshVertex), "Static mesh vertex layout stride does not match RenderStaticMeshVertex");
}

void RunFramePassKindsMapToMeshPassesTest() {
    Require(MeshPassForRenderPassKind(RenderPassKind::OpaqueScene).value_or(MeshPassType::Depth) == MeshPassType::BaseOpaque, "OpaqueScene did not map to BaseOpaque mesh pass");
    Require(MeshPassForRenderPassKind(RenderPassKind::TransparentScene).value_or(MeshPassType::Depth) == MeshPassType::BaseTransparent, "TransparentScene did not map to BaseTransparent mesh pass");
    Require(MeshPassForRenderPassKind(RenderPassKind::EditorSelectionMask).value_or(MeshPassType::Depth) == MeshPassType::SelectionId, "EditorSelectionMask did not map to SelectionId mesh pass");
    Require(!MeshPassForRenderPassKind(RenderPassKind::PostProcessBloomPrefilter).has_value(), "Post-process pass unexpectedly mapped to a mesh pass");
    Require(!MeshPassForRenderPassKind(RenderPassKind::FinalComposite).has_value(), "FinalComposite unexpectedly mapped to a mesh pass");
}

void RunMeshPipelineReportsMissingMeshBindingPerInstanceTest() {
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .materialAssetId = 7U },
            },
        },
    };
    RenderResourceRegistry registry;
    SceneRenderResourceMap resourceMap;
    SceneRenderDiagnostics diagnostics;

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resources = &registry,
        .resourceMap = &resourceMap,
        .diagnostics = &diagnostics,
    });

    Require(result.commands.empty(), "MeshPipeline emitted a command without a mesh binding");
    Require(result.stats.visibleMeshCount == 2U, "MeshPipeline did not count visible mesh instances");
    Require(result.stats.visibleDrawGroupCount == 1U, "MeshPipeline did not count visible draw groups");
    Require(result.stats.missingMeshBindingCount == 2U, "MeshPipeline did not count one missing mesh binding per instance");
    Require(diagnostics.events.size() == 2U, "MeshPipeline did not emit diagnostics per missing mesh instance");
    Require(diagnostics.events[0].kind == SceneRenderDiagnosticKind::MissingMeshBinding, "MeshPipeline emitted the wrong diagnostic kind");
    Require(diagnostics.events[1].entityId == 2U, "MeshPipeline diagnostic did not preserve entity id");
}

void RunMeshPipelineCanBuildPassCommandsWithoutResourceValidationTest() {
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .materialAssetId = 7U },
            },
        },
    };

    MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(result.commands.size() == 1U, "MeshPipeline did not build a command when validation was skipped");
    Require(result.commands[0].pass == MeshPassType::BaseOpaque, "MeshPipeline command stored the wrong pass");
    Require(result.commands[0].instances.size() == 2U, "MeshPipeline command did not preserve instances");

    MeshPipelineProcessor::CountCommandsAsSubmitted(result.stats, result.commands);
    Require(result.stats.submittedMeshCount == 2U, "MeshPipeline did not count submitted meshes from commands");
    Require(result.stats.submittedDrawCallCount == 1U, "MeshPipeline did not count submitted draw calls from commands");
    Require(result.stats.meshPipelineCommandCount == 1U, "MeshPipeline did not expose command count");
    Require(result.stats.meshPipelineCommandCapacity >= result.stats.meshPipelineCommandCount, "MeshPipeline did not expose command vector capacity");
    Require(result.stats.meshPipelineSortKeyCount == 1U, "MeshPipeline did not expose sort key count");
    Require(result.stats.meshPipelineScratchInstanceCapacity >= 2U, "MeshPipeline did not expose scratch instance capacity");
}

void RunMeshPipelineBuildIntoReusesCommandInstanceCapacityTest() {
    std::vector<SceneRenderMeshInstance> manyInstances;
    manyInstances.reserve(8U);
    for (std::uint64_t entityId = 1U; entityId <= 8U; ++entityId) {
        manyInstances.push_back(SceneRenderMeshInstance{ .entityId = entityId, .meshAssetId = 42U });
    }
    std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = manyInstances,
        },
    };
    MeshPipelineBuildResult reusableResult;

    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, reusableResult);
    Require(reusableResult.commands.size() == 1U, "MeshPipeline BuildInto did not emit initial command");
    const std::size_t initialCapacity = reusableResult.commands[0].instances.capacity();
    Require(initialCapacity >= 8U, "MeshPipeline BuildInto did not reserve the initial instance capacity");

    drawGroups[0].instances.resize(2U);
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, reusableResult);

    Require(reusableResult.commands.size() == 1U, "MeshPipeline BuildInto changed stable command count");
    Require(reusableResult.commands[0].instances.size() == 2U, "MeshPipeline BuildInto did not rebuild the smaller instance set");
    Require(reusableResult.commands[0].instances.capacity() >= initialCapacity, "MeshPipeline BuildInto released reusable instance capacity");
}

void RunMeshPipelineFiltersShadowCastingInstancesTest() {
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .castsShadow = true },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .castsShadow = false },
            },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::ShadowDepth,
        .drawGroups = &drawGroups,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 1U, "MeshPipeline shadow pass did not emit a command for shadow casters");
    Require(result.commands[0].instances.size() == 1U, "MeshPipeline shadow pass did not filter non-shadow-casting instances");
    Require(result.commands[0].instances[0].entityId == 1U, "MeshPipeline shadow pass kept the wrong instance");
}

void RunMeshPipelineFiltersSelectionInstancesTest() {
    const std::array<std::uint64_t, 1U> selected{2U};
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U },
            },
        },
    };

    const MeshPipelineBuildResult emptySelection = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::SelectionId,
        .drawGroups = &drawGroups,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(emptySelection.commands.empty(), "MeshPipeline selection pass rendered without selected entities");

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::SelectionId,
        .drawGroups = &drawGroups,
        .selectedEntityIds = selected,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 1U, "MeshPipeline selection pass did not emit a command for selected entities");
    Require(result.commands[0].instances.size() == 1U, "MeshPipeline selection pass did not filter unselected instances");
    Require(result.commands[0].instances[0].entityId == 2U, "MeshPipeline selection pass kept the wrong entity");
}

void RunMeshPipelineReportsShadowPassMissingResourcesOnlyForCastersTest() {
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .castsShadow = true },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .castsShadow = false },
            },
        },
    };
    RenderResourceRegistry registry;
    SceneRenderResourceMap resourceMap;
    SceneRenderDiagnostics diagnostics;

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::ShadowDepth,
        .drawGroups = &drawGroups,
        .resources = &registry,
        .resourceMap = &resourceMap,
        .diagnostics = &diagnostics,
    });

    Require(result.commands.empty(), "MeshPipeline emitted a shadow command without a mesh binding");
    Require(result.stats.visibleMeshCount == 1U, "MeshPipeline shadow pass counted non-shadow-casting instances as visible");
    Require(result.stats.visibleDrawGroupCount == 1U, "MeshPipeline shadow pass did not count the casting draw group");
    Require(result.stats.missingMeshBindingCount == 1U, "MeshPipeline shadow pass reported missing mesh bindings for filtered instances");
    Require(diagnostics.events.size() == 1U, "MeshPipeline shadow pass emitted diagnostics for filtered instances");
    Require(diagnostics.events[0].entityId == 1U, "MeshPipeline shadow pass diagnostic kept the wrong entity");
}

void RunSceneRendererValidatesExplicitMeshPassTest() {
    RenderScene renderScene;
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 1U,
        .meshAssetId = 42U,
        .castsShadow = true,
    }));
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 2U,
        .meshAssetId = 42U,
        .castsShadow = false,
    }));

    SceneRenderer renderer;
    const SceneRenderSubmitStats stats = renderer.ValidateSceneResources(renderScene, MeshPassType::ShadowDepth);
    Require(stats.visibleMeshCount == 1U, "SceneRenderer explicit shadow pass counted filtered instances");
    Require(stats.missingMeshBindingCount == 1U, "SceneRenderer explicit shadow pass did not use mesh pass validation");
}

void RunMeshPipelineBuildsCommandsPerSectionAndMaterialSlotTest() {
    const std::vector<RenderMeshSection> sections{
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 6U,
            .materialSlot = 0U,
            .bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F },
        },
        RenderMeshSection{
            .indexStart = 6U,
            .indexCount = 12U,
            .materialSlot = 1U,
            .bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F },
        },
    };
    const std::vector<RenderMaterialSlot> materialSlots{
        RenderMaterialSlot{ .defaultMaterialAssetId = 101U },
        RenderMaterialSlot{ .defaultMaterialAssetId = 102U },
    };
    RenderMeshResource mesh{};
    mesh.indexCount = 18U;
    mesh.sections = sections;
    mesh.materialSlots = materialSlots;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F };

    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .model = IdentityMatrix() },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .model = IdentityMatrix() },
            },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 2U, "MeshPipeline did not emit one command per mesh section");
    Require(result.commands[0].materialAssetId == 101U, "MeshPipeline did not use section material slot 0 default material");
    Require(result.commands[0].indexStart == 0U && result.commands[0].indexCount == 6U, "MeshPipeline section 0 did not preserve index range");
    Require(result.commands[1].materialAssetId == 102U, "MeshPipeline did not use section material slot 1 default material");
    Require(result.commands[1].indexStart == 6U && result.commands[1].indexCount == 12U, "MeshPipeline section 1 did not preserve index range");
    Require(result.commands[0].instances.size() == 2U && result.commands[1].instances.size() == 2U, "MeshPipeline section commands did not preserve instancing");
}

void RunMeshPipelineSplitsSectionCommandsByMaterialSlotOverrideTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.sections = {
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 3U,
            .materialSlot = 1U,
            .bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F },
        },
    };
    mesh.materialSlots = {
        RenderMaterialSlot{},
        RenderMaterialSlot{ .defaultMaterialAssetId = 101U },
    };
    mesh.bounds = mesh.sections[0].bounds;

    SceneRenderMeshInstance overridden{
        .entityId = 2U,
        .meshAssetId = 42U,
        .materialSlotAssetIds = { 0U, 201U },
        .materialSlotOverrideCount = 2U,
        .model = IdentityMatrix(),
    };
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .model = IdentityMatrix() },
                overridden,
            },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 2U, "MeshPipeline did not split section commands by material slot override");
    Require(result.commands[0].materialAssetId == 101U, "MeshPipeline did not keep the default slot material command");
    Require(result.commands[0].instances.size() == 1U && result.commands[0].instances[0].entityId == 1U, "MeshPipeline put the wrong instance in the default material command");
    Require(result.commands[1].materialAssetId == 201U, "MeshPipeline did not use the instance material slot override");
    Require(result.commands[1].instances.size() == 1U && result.commands[1].instances[0].entityId == 2U, "MeshPipeline put the wrong instance in the override material command");
    Require(result.stats.meshCommandLookupCapacity > 0U, "MeshPipeline did not expose command lookup scratch capacity");
}

void RunMeshPipelineDrawBudgetDropsOverflowCommandsTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.sections = {
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 3U,
            .materialSlot = 1U,
            .bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F },
        },
    };
    mesh.materialSlots = {
        RenderMaterialSlot{},
        RenderMaterialSlot{ .defaultMaterialAssetId = 101U },
    };
    mesh.bounds = mesh.sections[0].bounds;
    SceneRenderDiagnostics diagnostics;

    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .model = IdentityMatrix() },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .materialSlotAssetIds = { 0U, 201U }, .materialSlotOverrideCount = 2U, .model = IdentityMatrix() },
            },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .diagnostics = &diagnostics,
        .maxDrawCommands = 1U,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 1U, "MeshPipeline draw budget did not cap command count");
    Require(result.commands[0].materialAssetId == 101U, "MeshPipeline draw budget dropped the wrong command");
    Require(result.stats.droppedInstanceCount == 1U, "MeshPipeline draw budget did not count dropped instances");
    Require(diagnostics.events.size() == 1U && diagnostics.events[0].kind == SceneRenderDiagnosticKind::DroppedInstances, "MeshPipeline draw budget did not emit a drop diagnostic");
}

void RunMeshPipelineSortsCommandsBySortKeyTest() {
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 90U,
            .materialAssetId = 9U,
            .instances = { SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 90U, .materialAssetId = 9U } },
        },
        SceneRenderDrawGroup{
            .meshAssetId = 80U,
            .materialAssetId = 7U,
            .instances = { SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 80U, .materialAssetId = 7U } },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 2U, "MeshPipeline sort test did not build two commands");
    Require(result.commands[0].materialAssetId == 7U, "MeshPipeline did not sort commands by material key");
    Require(result.commands[0].sortKey < result.commands[1].sortKey, "MeshPipeline sort keys are not monotonic");
}

void RunMeshPipelineRoutesMaterialAlphaModesToPassesTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F };
    mesh.sections = {
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 3U,
            .bounds = mesh.bounds,
        },
    };
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = { SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U, .model = IdentityMatrix() } },
        },
    };
    RenderMaterialResource masked{};
    masked.alphaMode = RenderMaterialAlphaMode::Mask;
    masked.baseColor[3] = 0.25F;
    RenderMaterialResource blended{};
    blended.alphaMode = RenderMaterialAlphaMode::Blend;

    const MeshPipelineBuildResult maskedOpaque = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resolvedMaterialResource = &masked,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(maskedOpaque.commands.size() == 1U, "MeshPipeline did not keep masked materials in the opaque pass");

    const MeshPipelineBuildResult blendedOpaque = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resolvedMaterialResource = &blended,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(blendedOpaque.commands.empty(), "MeshPipeline kept blend materials in the opaque pass");

    const MeshPipelineBuildResult blendedTransparent = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseTransparent,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resolvedMaterialResource = &blended,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(blendedTransparent.commands.size() == 1U, "MeshPipeline did not route blend materials to the transparent pass");
}

void RunMeshPipelineUsesMaterialDoubleSidedStateTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.doubleSided = false;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F };
    mesh.sections = {
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 3U,
            .bounds = mesh.bounds,
        },
    };
    RenderMaterialResource material{};
    material.doubleSided = true;
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = { SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U, .model = IdentityMatrix() } },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resolvedMaterialResource = &material,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 1U, "MeshPipeline double sided material test did not emit a command");
    Require((result.commands[0].state & BGFX_STATE_CULL_CW) == 0U, "MeshPipeline ignored material double sided state");
}

void RunMeshPipelineSortsTransparentCommandsBackToFrontTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F };
    mesh.sections = {
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 3U,
            .bounds = mesh.bounds,
        },
    };
    RenderMaterialResource material{};
    material.alphaMode = RenderMaterialAlphaMode::Blend;
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = { SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U, .model = TranslationMatrix(0.0F, 0.0F, 0.0F) } },
        },
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = { SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .materialAssetId = 7U, .model = TranslationMatrix(0.0F, 0.0F, 0.1F) } },
        },
    };
    const SceneRenderCamera camera{
        .view = IdentityMatrix(),
        .projection = IdentityMatrix(),
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseTransparent,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resolvedMaterialResource = &material,
        .camera = &camera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 2U, "MeshPipeline transparent sort test did not emit two commands");
    Require(result.commands[0].instances[0].entityId == 2U, "MeshPipeline did not sort transparent commands back to front");
    Require(result.commands[0].sortKey < result.commands[1].sortKey, "MeshPipeline transparent sort keys are not monotonic");
}

void RunMeshPipelineCpuCullsByFrustumBoundsTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 0.25F };
    mesh.sections = {
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 3U,
            .bounds = mesh.bounds,
        },
    };

    const SceneRenderCamera camera{
        .view = IdentityMatrix(),
        .projection = IdentityMatrix(),
    };
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .model = TranslationMatrix(0.0F, 0.0F, 0.0F) },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .model = TranslationMatrix(4.0F, 0.0F, 0.0F) },
            },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .camera = &camera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 1U, "MeshPipeline culling test did not emit the visible command");
    Require(result.commands[0].instances.size() == 1U, "MeshPipeline did not CPU-cull the out-of-frustum instance");
    Require(result.commands[0].instances[0].entityId == 1U, "MeshPipeline kept the wrong culled instance");
    Require(result.commands[0].instances[0].worldBounds.IsValid(), "MeshPipeline did not store per-instance world bounds");
    Require(result.stats.visibleMeshCount == 1U, "MeshPipeline culling stats did not count visible instances");
    Require(result.stats.culledInstanceCount == 1U, "MeshPipeline culling stats did not count culled instances");
}

void RunRenderScenePropagatesMeshPassFlagsTest() {
    RenderScene renderScene;
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 1U,
        .meshAssetId = 42U,
        .castsShadow = false,
        .receivesShadow = false,
    }));

    std::vector<SceneRenderDrawGroup> groups;
    renderScene.BuildDrawGroups(groups);
    Require(groups.size() == 1U && groups[0].instances.size() == 1U, "RenderScene did not build the expected draw group");
    Require(!groups[0].instances[0].castsShadow, "RenderScene did not propagate castsShadow to mesh pipeline instances");
    Require(!groups[0].instances[0].receivesShadow, "RenderScene did not propagate receivesShadow to mesh pipeline instances");
}

} // namespace

void RunMeshPipelineTests() {
    RunMeshPassTypeNamesResolveTest();
    RunStaticMeshVertexLayoutIsDefinedTest();
    RunFramePassKindsMapToMeshPassesTest();
    RunMeshPipelineReportsMissingMeshBindingPerInstanceTest();
    RunMeshPipelineCanBuildPassCommandsWithoutResourceValidationTest();
    RunMeshPipelineBuildIntoReusesCommandInstanceCapacityTest();
    RunMeshPipelineFiltersShadowCastingInstancesTest();
    RunMeshPipelineFiltersSelectionInstancesTest();
    RunMeshPipelineReportsShadowPassMissingResourcesOnlyForCastersTest();
    RunSceneRendererValidatesExplicitMeshPassTest();
    RunMeshPipelineBuildsCommandsPerSectionAndMaterialSlotTest();
    RunMeshPipelineSplitsSectionCommandsByMaterialSlotOverrideTest();
    RunMeshPipelineDrawBudgetDropsOverflowCommandsTest();
    RunMeshPipelineSortsCommandsBySortKeyTest();
    RunMeshPipelineRoutesMaterialAlphaModesToPassesTest();
    RunMeshPipelineUsesMaterialDoubleSidedStateTest();
    RunMeshPipelineSortsTransparentCommandsBackToFrontTest();
    RunMeshPipelineCpuCullsByFrustumBoundsTest();
    RunRenderScenePropagatesMeshPassFlagsTest();
}

} // namespace kb::render::tests
