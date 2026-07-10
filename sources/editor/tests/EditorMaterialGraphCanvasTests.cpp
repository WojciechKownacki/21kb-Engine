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

void RunMaterialGraphCanvasZoomScalesNodeGeometryProportionallyTest() {
    MaterialGraphCanvas canvas;
    canvas.SetViewport(MaterialGraphCanvasRect{ 30.0F, 40.0F, 2000.0F, 1200.0F });
    const std::uint32_t node = canvas.AddNode(MaterialGraphCanvasNode{
        .title = "Stable Node",
        .stableId = "stable.node",
        .x = 240.0F,
        .y = 180.0F,
        .inputs = { Pin("In", "in") },
        .outputs = { Pin("Out", "out") },
        .widthOverride = 260.0F,
        .heightOverride = 150.0F,
    });

    for (const float zoom : { 0.10F, 0.72F, 1.0F, 2.50F }) {
        canvas.SetView(0.0F, 0.0F, zoom);
        const MaterialGraphCanvasPoint originLocal = canvas.WorldToLocal(MaterialGraphCanvasPoint{ 240.0F, 180.0F });
        const MaterialGraphCanvasPoint originWindow{
            canvas.Viewport().x + originLocal.x,
            canvas.Viewport().y + originLocal.y,
        };
        const MaterialGraphCanvasPoint input = canvas.PinCenterWindow(node, 0U, false);
        const MaterialGraphCanvasPoint output = canvas.PinCenterWindow(node, 0U, true);
        const MaterialGraphCanvasPoint inputOffset{ input.x - originWindow.x, input.y - originWindow.y };
        const MaterialGraphCanvasPoint outputOffset{ output.x - originWindow.x, output.y - originWindow.y };

        const float renderedWidth = 260.0F * zoom;
        const float renderedHeight = 150.0F * zoom;
        Require(NearlyEqual(inputOffset.x, 0.0F),
            "Zoom must keep an input pin attached to the scaled left node edge.");
        Require(NearlyEqual(outputOffset.x, renderedWidth),
            "Zoom must scale the node and its output pin by the same factor.");
        Require(NearlyEqual(inputOffset.y, outputOffset.y),
            "Zoom must preserve aligned pin rows while scaling node contents.");
        Require(canvas.HitTestNode(originWindow.x + renderedWidth - 1.0F, originWindow.y + renderedHeight - 1.0F) == node,
            "Zoomed node hit-testing must preserve the full proportionally scaled node rectangle.");
        Require(!canvas.HitTestNode(originWindow.x + renderedWidth + 1.0F, originWindow.y + (renderedHeight * 0.5F)).has_value(),
            "Zoomed node hit-testing must not extend beyond the scaled node rectangle.");
        const std::optional<MaterialGraphCanvasPinHit> outputHit = canvas.HitTestPin(output.x, output.y);
        Require(outputHit.has_value() && outputHit->node == node && outputHit->output,
            "Zoomed pin hit-testing must remain aligned with the proportionally scaled rendered pin.");
    }
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
    RunMaterialGraphCanvasZoomScalesNodeGeometryProportionallyTest();
    RunMaterialGraphCanvasAdapterCoversAllNodeKindsTest();
    RunMaterialGraphCanvasAdapterBuildsDocumentLinksTest();
    std::cout << "EditorMaterialGraphCanvasTests passed\n" << std::flush;
}

} // namespace kb::editor::tests
