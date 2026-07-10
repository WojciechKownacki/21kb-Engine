#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "rendering/material_graph/MaterialGraphCanvasDocumentAdapter.hpp"
#include "rendering/material_graph/MaterialGraphCanvas.hpp"

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace kb::editor::tests {
namespace {

[[nodiscard]] MaterialGraphCanvasPin Pin(
    std::string label,
    std::string stableId,
    MaterialGraphCanvasPinType type = MaterialGraphCanvasPinType::Float4) {
    return MaterialGraphCanvasPin{
        .label = std::move(label),
        .stableId = std::move(stableId),
        .type = type,
    };
}

[[nodiscard]] MaterialGraphCanvas MakeSimpleCanvas() {
    MaterialGraphCanvas canvas;
    canvas.SetViewport(MaterialGraphCanvasRect{ 40.0F, 60.0F, 900.0F, 600.0F });
    canvas.SetView(0.0F, 0.0F, 1.0F);

    MaterialGraphCanvasNode source{
        .title = "Image Texture",
        .stableId = "node.texture",
        .x = 120.0F,
        .y = 120.0F,
        .inputs = { Pin("UV", "uv", MaterialGraphCanvasPinType::Float2) },
        .outputs = {
            Pin("RGBA", "rgba"),
            Pin("R", "r", MaterialGraphCanvasPinType::Float),
        },
        .widthOverride = 420.0F,
        .texturePreview = MaterialGraphCanvasTexturePreview{ .enabled = true, .stableId = "preview.texture" },
    };
    static_cast<void>(canvas.AddNode(std::move(source)));

    MaterialGraphCanvasNode output{
        .title = "Material Output",
        .stableId = "node.output",
        .x = 680.0F,
        .y = 132.0F,
        .inputs = {
            Pin("Base Color", "baseColor"),
            Pin("Normal", "normal"),
        },
        .isOutput = true,
    };
    static_cast<void>(canvas.AddNode(std::move(output)));
    return canvas;
}

void RunMaterialGraphCanvasPinRowsAreForgivingTest() {
    MaterialGraphCanvas canvas = MakeSimpleCanvas();

    const MaterialGraphCanvasPoint baseColor = canvas.PinCenterWindow(1U, 0U, false);
    const std::optional<MaterialGraphCanvasPinHit> inputEdgeHit = canvas.HitTestPin(
        baseColor.x + 24.0F,
        baseColor.y);
    Require(inputEdgeHit.has_value(), "Material graph canvas should hit an input pin from its row edge.");
    Require(inputEdgeHit->node == 1U, "Material graph canvas hit the wrong input node.");
    Require(inputEdgeHit->pin == 0U, "Material graph canvas hit the wrong input pin.");
    Require(!inputEdgeHit->output, "Material graph canvas input edge should not hit as output.");
    const std::optional<MaterialGraphCanvasPinHit> inputLabelHit = canvas.HitTestPin(
        baseColor.x + 160.0F,
        baseColor.y);
    Require(inputLabelHit.has_value(), "Material graph canvas should hit a one-sided input across the row.");
    Require(inputLabelHit->node == 1U && inputLabelHit->pin == 0U && !inputLabelHit->output,
        "Material graph canvas one-sided input row should preserve node, pin, and direction.");

    const MaterialGraphCanvasPoint rgba = canvas.PinCenterWindow(0U, 0U, true);
    const std::optional<MaterialGraphCanvasPinHit> outputEdgeHit = canvas.HitTestPin(
        rgba.x - 24.0F,
        rgba.y);
    Require(outputEdgeHit.has_value(), "Material graph canvas should hit a texture output from its row edge.");
    Require(outputEdgeHit->node == 0U, "Material graph canvas hit the wrong output node.");
    Require(outputEdgeHit->pin == 0U, "Material graph canvas hit the wrong output pin.");
    Require(outputEdgeHit->output, "Material graph canvas output edge should hit as output.");
    Require(canvas.HitTestPin(rgba.x - 70.0F, rgba.y).has_value(),
        "Material graph canvas should hit a texture output from the reference side lane.");
    Require(!canvas.HitTestPin(40.0F + 120.0F + 210.0F, rgba.y).has_value(),
        "Material graph canvas texture preview center should stay reserved for picker interaction.");
}

void RunMaterialGraphCanvasCanonicalPinGeometryTest() {
    kb::render::RenderMaterialGraphNode vectorParameter{
        .id = 17U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterVector,
        .positionX = 120,
        .positionY = 80,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "tint",
            .displayName = "Tint",
            .defaultValueHint = "0.2 0.4 0.8",
        },
    };
    MaterialGraphCanvas canvas;
    canvas.SetViewport(MaterialGraphCanvasRect{ 30.0F, 40.0F, 800.0F, 600.0F });
    canvas.SetView(10.0F, 5.0F, 1.5F);
    const std::uint32_t vectorNode = canvas.AddNode(BuildMaterialGraphCanvasNode(vectorParameter));
    const MaterialGraphCanvasNode* canonicalNode = canvas.NodeAt(vectorNode);
    Require(canonicalNode != nullptr, "Canonical geometry should preserve the vector parameter node.");
    const MaterialGraphCanvasPoint vectorOutput = canvas.PinCenterWorld(vectorNode, 0U, true);
    const MaterialGraphCanvasRect vectorBounds = canvas.NodeBoundsWorld(*canonicalNode);
    Require(vectorOutput.y > vectorBounds.y && vectorOutput.y < vectorBounds.y + vectorBounds.height,
        "ParameterVector output anchor must stay inside the rendered node body.");
    Require(NearlyEqual(vectorOutput.y, vectorParameter.positionY + 51.0),
        "ParameterVector must center its single output in the canonical node body.");
    const MaterialGraphCanvasPoint vectorOutputWindow = canvas.PinCenterWindow(vectorNode, 0U, true);
    const std::optional<MaterialGraphCanvasPinHit> vectorHit = canvas.HitTestPin(vectorOutputWindow.x, vectorOutputWindow.y);
    Require(vectorHit.has_value() && vectorHit->node == vectorNode && vectorHit->pin == 0U && vectorHit->output,
        "ParameterVector rendered center, wire anchor, and hit target must resolve to the same canonical pin.");

    kb::render::RenderMaterialGraphNode asymmetric{
        .id = 18U,
        .kind = kb::render::RenderMaterialGraphNodeKind::CustomCode,
        .positionX = 420,
        .positionY = 80,
    };
    asymmetric.customCode.inputs = {
        kb::render::RenderMaterialGraphCustomPin{ .name = "Input", .type = kb::render::RenderMaterialGraphPinType::Float4 },
    };
    asymmetric.customCode.outputs = {
        kb::render::RenderMaterialGraphCustomPin{ .name = "A", .type = kb::render::RenderMaterialGraphPinType::Float4 },
        kb::render::RenderMaterialGraphCustomPin{ .name = "B", .type = kb::render::RenderMaterialGraphPinType::Float4 },
        kb::render::RenderMaterialGraphCustomPin{ .name = "C", .type = kb::render::RenderMaterialGraphPinType::Float4 },
        kb::render::RenderMaterialGraphCustomPin{ .name = "D", .type = kb::render::RenderMaterialGraphPinType::Float4 },
    };
    const std::uint32_t asymmetricNode = canvas.AddNode(BuildMaterialGraphCanvasNode(asymmetric));
    const MaterialGraphCanvasPoint asymmetricInput = canvas.PinCenterWindow(asymmetricNode, 0U, false);
    const std::optional<MaterialGraphCanvasPinHit> asymmetricHit = canvas.HitTestPin(asymmetricInput.x, asymmetricInput.y);
    Require(asymmetricHit.has_value() && asymmetricHit->node == asymmetricNode && asymmetricHit->pin == 0U && !asymmetricHit->output,
        "Asymmetric CustomCode input render, wire, and hit geometry must share the max-lane layout.");
}

void RunMaterialGraphCanvasNearestCompatiblePinTest() {
    MaterialGraphCanvas canvas;
    canvas.SetViewport(MaterialGraphCanvasRect{ 0.0F, 0.0F, 640.0F, 480.0F });
    const std::uint32_t node = canvas.AddNode(MaterialGraphCanvasNode{
        .title = "Two Inputs",
        .stableId = "two.inputs",
        .x = 120.0F,
        .y = 80.0F,
        .inputs = { Pin("First", "first"), Pin("Second", "second") },
    });
    const MaterialGraphCanvasPoint first = canvas.PinCenterWindow(node, 0U, false);
    const MaterialGraphCanvasPoint second = canvas.PinCenterWindow(node, 1U, false);
    const float overlapY = ((first.y + second.y) * 0.5F) + 1.0F;
    const std::optional<MaterialGraphCanvasPinHit> nearest = canvas.HitTestPin(first.x, overlapY);
    Require(nearest.has_value() && nearest->pin == 1U,
        "Overlapping pin hit-zones must choose the geometrically nearest pin.");

    const std::optional<MaterialGraphCanvasPinHit> nearestCompatible = canvas.HitTestPin(
        first.x,
        overlapY,
        [](const MaterialGraphCanvasPinHit& candidate) { return candidate.pin == 0U; });
    Require(nearestCompatible.has_value() && nearestCompatible->pin == 0U,
        "Pin hit-testing must skip a nearer incompatible pin and choose the nearest compatible candidate.");
}

void RunMaterialGraphCanvasViewportClippingTest() {
    MaterialGraphCanvas canvas;
    canvas.SetViewport(MaterialGraphCanvasRect{ 100.0F, 120.0F, 240.0F, 180.0F });
    const std::uint32_t node = canvas.AddNode(MaterialGraphCanvasNode{
        .title = "Clipped",
        .stableId = "clipped",
        .x = -2.0F,
        .y = 40.0F,
        .inputs = { Pin("Input", "input") },
    });
    const MaterialGraphCanvasPoint center = canvas.PinCenterWindow(node, 0U, false);
    Require(center.x < canvas.Viewport().x, "Viewport clipping fixture must place the pin center outside the viewport.");
    Require(!canvas.HitTestPin(canvas.Viewport().x, center.y).has_value(),
        "A clipped pin must remain inactive even when its forgiving hit band overlaps the viewport.");
    Require(!canvas.HitTestPin(center.x, center.y).has_value(),
        "Pin hit-testing must reject pointer coordinates outside the graph viewport.");
}

void RunMaterialGraphCanvasLinkOcclusionTest() {
    MaterialGraphCanvas canvas;
    canvas.SetViewport(MaterialGraphCanvasRect{ 0.0F, 0.0F, 900.0F, 600.0F });
    const std::uint32_t source = canvas.AddNode(MaterialGraphCanvasNode{
        .title = "Source",
        .stableId = "source",
        .x = 40.0F,
        .y = 80.0F,
        .outputs = { Pin("Out", "out") },
    });
    const std::uint32_t target = canvas.AddNode(MaterialGraphCanvasNode{
        .title = "Target",
        .stableId = "target",
        .x = 640.0F,
        .y = 80.0F,
        .inputs = { Pin("In", "in") },
    });
    canvas.AddLink(MaterialGraphCanvasLink{
        .fromNode = source,
        .fromPin = 0U,
        .toNode = target,
        .toPin = 0U,
        .stableId = "source.target",
    });
    const MaterialGraphCanvasPoint from = canvas.PinCenterWindow(source, 0U, true);
    const MaterialGraphCanvasPoint to = canvas.PinCenterWindow(target, 0U, false);
    const MaterialGraphCanvasPoint midpoint{ (from.x + to.x) * 0.5F, (from.y + to.y) * 0.5F };
    Require(canvas.HitTestLink(midpoint.x, midpoint.y).has_value(),
        "Link occlusion fixture must initially hit the visible wire.");

    static_cast<void>(canvas.AddNode(MaterialGraphCanvasNode{
        .title = "Opaque Node",
        .stableId = "occluder.node",
        .x = midpoint.x - 80.0F,
        .y = midpoint.y - 40.0F,
        .widthOverride = 160.0F,
        .heightOverride = 80.0F,
    }));
    Require(!canvas.HitTestLink(midpoint.x, midpoint.y).has_value(),
        "A wire hidden under an opaque node must not be hit-testable.");

    MaterialGraphCanvas commentCanvas;
    commentCanvas.SetViewport(MaterialGraphCanvasRect{ 0.0F, 0.0F, 900.0F, 600.0F });
    const std::uint32_t commentSource = commentCanvas.AddNode(MaterialGraphCanvasNode{
        .title = "Source",
        .stableId = "comment.source",
        .x = 40.0F,
        .y = 260.0F,
        .outputs = { Pin("Out", "out") },
    });
    const std::uint32_t commentTarget = commentCanvas.AddNode(MaterialGraphCanvasNode{
        .title = "Target",
        .stableId = "comment.target",
        .x = 640.0F,
        .y = 260.0F,
        .inputs = { Pin("In", "in") },
    });
    commentCanvas.AddLink(MaterialGraphCanvasLink{ .fromNode = commentSource, .toNode = commentTarget, .stableId = "comment.link" });
    const MaterialGraphCanvasPoint commentFrom = commentCanvas.PinCenterWindow(commentSource, 0U, true);
    const MaterialGraphCanvasPoint commentTo = commentCanvas.PinCenterWindow(commentTarget, 0U, false);
    const MaterialGraphCanvasPoint commentMidpoint{
        (commentFrom.x + commentTo.x) * 0.5F,
        (commentFrom.y + commentTo.y) * 0.5F,
    };
    commentCanvas.AddOccluderWorld(MaterialGraphCanvasRect{
        commentMidpoint.x - 70.0F,
        commentMidpoint.y - 35.0F,
        140.0F,
        70.0F,
    });
    Require(!commentCanvas.HitTestLink(commentMidpoint.x, commentMidpoint.y).has_value(),
        "A wire hidden under an opaque comment must not be hit-testable.");
}

void RunMaterialGraphCanvasConnectDragTest() {
    MaterialGraphCanvas canvas = MakeSimpleCanvas();
    const MaterialGraphCanvasPoint rgba = canvas.PinCenterWindow(0U, 0U, true);
    const MaterialGraphCanvasPoint baseColor = canvas.PinCenterWindow(1U, 0U, false);

    Require(canvas.OnPointerDown(rgba.x - 70.0F, rgba.y, false, false), "Material graph canvas should start wire drag from the texture side lane.");
    Require(canvas.OnPointerMove(baseColor.x + 160.0F, baseColor.y), "Material graph canvas should update wire drag across the input row.");
    Require(canvas.OnPointerUp(baseColor.x + 160.0F, baseColor.y), "Material graph canvas should complete wire drag across the input row.");

    Require(canvas.LinkCount() == 1U, "Material graph canvas should create one link after output-to-input drag.");
    const MaterialGraphCanvasLink& link = canvas.Links().front();
    Require(link.fromNode == 0U && link.fromPin == 0U, "Material graph canvas created link from wrong output.");
    Require(link.toNode == 1U && link.toPin == 0U, "Material graph canvas created link to wrong input.");
}

void RunMaterialGraphCanvasBreakLinkTest() {
    MaterialGraphCanvas canvas = MakeSimpleCanvas();
    canvas.AddLink(MaterialGraphCanvasLink{
        .fromNode = 0U,
        .fromPin = 0U,
        .toNode = 1U,
        .toPin = 0U,
        .stableId = "link.rgba.baseColor",
    });

    const MaterialGraphCanvasPoint from = canvas.PinCenterWindow(0U, 0U, true);
    const MaterialGraphCanvasPoint to = canvas.PinCenterWindow(1U, 0U, false);
    const float cutX = (from.x + to.x) * 0.5F;
    const float cutY = (from.y + to.y) * 0.5F;

    Require(canvas.BreakLinkAt(cutX, cutY), "Material graph canvas should cut the closest link.");
    Require(canvas.LinkCount() == 0U, "Material graph canvas should remove a cut link in unbound mode.");

    std::vector<MaterialGraphCanvasEdit> edits = canvas.TakeEmittedEdits();
    Require(edits.size() == 1U, "Material graph canvas should emit one disconnect edit for a cut link.");
    Require(edits.front().kind == MaterialGraphCanvasEditKind::Disconnect, "Material graph canvas should emit disconnect edit.");
    Require(edits.front().linkId == "link.rgba.baseColor", "Material graph canvas disconnect should preserve link id.");
}

void RunMaterialGraphCanvasNodeDragEmitsMoveTest() {
    MaterialGraphCanvas canvas = MakeSimpleCanvas();
    canvas.SetBound(true);

    const std::optional<std::uint32_t> node = canvas.HitTestNode(190.0F, 200.0F);
    Require(node.has_value() && *node == 0U, "Material graph canvas should hit the first node body.");
    Require(canvas.OnPointerDown(190.0F, 200.0F, false, false), "Material graph canvas should start node drag.");
    Require(canvas.OnPointerMove(240.0F, 235.0F), "Material graph canvas should move selected node.");
    Require(canvas.OnPointerUp(240.0F, 235.0F), "Material graph canvas should finish node drag.");

    const MaterialGraphCanvasNode* moved = canvas.NodeAt(0U);
    Require(moved != nullptr, "Material graph canvas moved node should exist.");
    Require(NearlyEqual(moved->x, 170.0), "Material graph canvas node x should update during drag.");
    Require(NearlyEqual(moved->y, 155.0), "Material graph canvas node y should update during drag.");

    std::vector<MaterialGraphCanvasEdit> edits = canvas.TakeEmittedEdits();
    Require(edits.size() == 1U, "Material graph canvas should emit one move edit.");
    Require(edits.front().kind == MaterialGraphCanvasEditKind::Move, "Material graph canvas should emit move edit.");
    Require(edits.front().nodeId == "node.texture", "Material graph canvas move should preserve node id.");
}

void RunMaterialGraphCanvasDuplicatePreservesInternalLinksTest() {
    MaterialGraphCanvas canvas = MakeSimpleCanvas();
    canvas.AddLink(MaterialGraphCanvasLink{
        .fromNode = 0U,
        .fromPin = 0U,
        .toNode = 1U,
        .toPin = 0U,
        .stableId = "link.rgba.baseColor",
    });

    Require(canvas.SelectNode(0U, false, false), "Material graph canvas should select source node.");
    Require(canvas.SelectNode(1U, true, false), "Material graph canvas should add output node to selection.");
    Require(canvas.DuplicateSelected(), "Material graph canvas should duplicate selected editable nodes.");

    Require(canvas.NodeCount() == 3U, "Material graph canvas should duplicate only non-output nodes.");
    Require(canvas.LinkCount() == 1U, "Material graph canvas should not clone links pointing into the output node.");
    Require(canvas.SelectedCount() == 1U, "Material graph canvas should select the duplicate node.");

    const MaterialGraphCanvasNode* duplicate = canvas.NodeAt(2U);
    Require(duplicate != nullptr, "Material graph canvas duplicate node should exist.");
    Require(NearlyEqual(duplicate->x, 146.0), "Material graph canvas duplicate should be offset on x.");
    Require(NearlyEqual(duplicate->y, 146.0), "Material graph canvas duplicate should be offset on y.");

    MaterialGraphCanvas multi;
    multi.SetViewport(MaterialGraphCanvasRect{ 0.0F, 0.0F, 800.0F, 600.0F });
    const std::uint32_t a = multi.AddNode(MaterialGraphCanvasNode{
        .title = "A",
        .stableId = "a",
        .x = 10.0F,
        .y = 10.0F,
        .outputs = { Pin("Out", "out") },
    });
    const std::uint32_t b = multi.AddNode(MaterialGraphCanvasNode{
        .title = "B",
        .stableId = "b",
        .x = 260.0F,
        .y = 20.0F,
        .inputs = { Pin("In", "in") },
    });
    multi.AddLink(MaterialGraphCanvasLink{ .fromNode = a, .fromPin = 0U, .toNode = b, .toPin = 0U, .stableId = "ab" });
    Require(multi.SelectAll(), "Material graph canvas should select all nodes.");
    Require(multi.DuplicateSelected(), "Material graph canvas should duplicate linked nodes.");
    Require(multi.NodeCount() == 4U, "Material graph canvas should duplicate both selected non-output nodes.");
    Require(multi.LinkCount() == 2U, "Material graph canvas should preserve links fully inside the duplicated selection.");
    const MaterialGraphCanvasLink& clonedLink = multi.Links().back();
    Require(clonedLink.fromNode == 2U && clonedLink.toNode == 3U, "Material graph canvas cloned link should point at clones.");
}

[[nodiscard]] kb::render::RenderMaterialGraphLink MakeRenderLink(
    const kb::render::RenderMaterialGraphNode& fromNode,
    std::string fromPin,
    const kb::render::RenderMaterialGraphNode& toNode,
    std::string toPin) {
    kb::render::RenderMaterialGraphLink link{
        .fromNodeId = fromNode.id,
        .fromPinId = kb::render::RenderMaterialGraphStablePinId(fromNode, fromPin, true),
        .fromPin = std::move(fromPin),
        .toNodeId = toNode.id,
        .toPinId = kb::render::RenderMaterialGraphStablePinId(toNode, toPin, false),
        .toPin = std::move(toPin),
    };
    link.id = kb::render::MakeRenderMaterialGraphLinkId(link);
    return link;
}

void RunMaterialGraphCanvasAdapterCoversAllNodeKindsTest() {
    std::size_t covered = 0U;
    for (const kb::render::RenderMaterialGraphNodeKind kind : kb::render::AllRenderMaterialGraphNodeKinds()) {
        kb::render::RenderMaterialGraphNode renderNode{
            .id = static_cast<std::uint32_t>(covered + 1U),
            .kind = kind,
            .positionX = static_cast<std::int32_t>(covered * 17U),
            .positionY = static_cast<std::int32_t>(covered * 7U),
        };
        if (kind == kb::render::RenderMaterialGraphNodeKind::ParameterColor ||
            kind == kb::render::RenderMaterialGraphNodeKind::ConstantColor) {
            renderNode.parameter.defaultValueHint = "0.25 0.5 0.75 1";
        }

        const MaterialGraphCanvasNode canvasNode = BuildMaterialGraphCanvasNode(renderNode);
        Require(!canvasNode.title.empty(), "Material graph canvas adapter should give every node a title.");
        Require(canvasNode.stableId == std::to_string(renderNode.id), "Material graph canvas adapter should preserve node id.");
        Require(canvasNode.inputs.size() == kb::render::RenderMaterialGraphNodeInputPinNames(renderNode).size(),
            "Material graph canvas adapter input pin count should match the render schema.");
        Require(canvasNode.outputs.size() == kb::render::RenderMaterialGraphNodeOutputPinNames(renderNode).size(),
            "Material graph canvas adapter output pin count should match the render schema.");
        for (const MaterialGraphCanvasPin& pin : canvasNode.inputs) {
            Require(!pin.stableId.empty(), "Material graph canvas adapter input pin should have a stable id.");
        }
        for (const MaterialGraphCanvasPin& pin : canvasNode.outputs) {
            Require(!pin.stableId.empty(), "Material graph canvas adapter output pin should have a stable id.");
        }
        ++covered;
    }
    Require(covered == kb::render::AllRenderMaterialGraphNodeKinds().size(),
        "Material graph canvas adapter should cover every current render node kind.");
}

void RunMaterialGraphCanvasAdapterBuildsDocumentLinksTest() {
    kb::render::RenderMaterialGraphDocument document = kb::render::MakeDefaultRenderMaterialGraphDocument();
    Require(!document.nodes.empty(), "Default material graph should contain an output node.");
    kb::render::RenderMaterialGraphNode color{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 40,
        .positionY = 80,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 0 0 1" },
    };
    document.nodes.push_back(color);
    document.links.push_back(MakeRenderLink(
        document.nodes.back(),
        "rgba",
        document.nodes.front(),
        "baseColor"));

    MaterialGraphCanvasDocumentBuildResult result = BuildMaterialGraphCanvasFromDocument(document);
    Require(result.skippedLinks == 0U, "Material graph canvas adapter should not skip valid links.");
    Require(result.canvas.NodeCount() == document.nodes.size(), "Material graph canvas adapter should preserve node count.");
    Require(result.canvas.LinkCount() == 1U, "Material graph canvas adapter should preserve valid links.");

    const MaterialGraphCanvasLink& link = result.canvas.Links().front();
    Require(link.fromNode == 1U && link.fromPin == 0U, "Material graph canvas adapter should map output link endpoint.");
    Require(link.toNode == 0U && link.toPin == 0U, "Material graph canvas adapter should map input link endpoint.");
    Require(link.stableId == std::to_string(document.links.front().id), "Material graph canvas adapter should preserve link id.");
}

} // namespace

void RunEditorMaterialGraphCanvasTests() {
    RunMaterialGraphCanvasPinRowsAreForgivingTest();
    RunMaterialGraphCanvasCanonicalPinGeometryTest();
    RunMaterialGraphCanvasNearestCompatiblePinTest();
    RunMaterialGraphCanvasViewportClippingTest();
    RunMaterialGraphCanvasLinkOcclusionTest();
    RunMaterialGraphCanvasConnectDragTest();
    RunMaterialGraphCanvasBreakLinkTest();
    RunMaterialGraphCanvasNodeDragEmitsMoveTest();
    RunMaterialGraphCanvasDuplicatePreservesInternalLinksTest();
    RunMaterialGraphCanvasAdapterCoversAllNodeKindsTest();
    RunMaterialGraphCanvasAdapterBuildsDocumentLinksTest();
    std::cout << "EditorMaterialGraphCanvasTests passed\n" << std::flush;
}

} // namespace kb::editor::tests
