#pragma once

#include <cstdint>
#include <cstddef>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "kb/render/resources/RenderMaterialTypeSchema.hpp"

namespace kb::render {

inline constexpr std::uint32_t kRenderMaterialGraphDocumentVersion = 1U;

enum class RenderMaterialGraphNodeKind : std::uint8_t {
    MaterialOutput,
    ConstantScalar,
    ConstantVector,
    ConstantColor,
    TextureSample,
    ParameterScalar,
    ParameterVector,
    ParameterColor,
    ParameterTexture,
    Add,
    Subtract,
    Multiply,
    Divide,
    Power,
    OneMinus,
    Clamp,
    Lerp,
    NormalUnpack,
    Uv,
    Absolute,
    Minimum,
    Maximum,
    Saturate,
    Floor,
    Ceil,
    Fraction,
    SquareRoot,
    Sine,
    Cosine,
    DotProduct,
    CrossProduct,
    Normalize,
    Length,
    Distance,
    BreakVector,
    MakeVector,
    Step,
    SmoothStep,
    If,
    Desaturate,
    Fresnel,
    Negate,
    Sign,
    Round,
    Truncate,
    Tangent,
    ArcSine,
    ArcCosine,
    ArcTangent,
    ArcTangent2,
    ConstantVector2,
};

enum class RenderMaterialGraphRenderPath : std::uint8_t {
    GpuForward,
    GpuShadow,
    GpuDeferred,
    CpuFallback,
    Preview,
};

enum class RenderMaterialGraphNodeSupport : std::uint8_t {
    Production,
    Experimental,
    FallbackOnly,
    Unsupported,
};

enum class RenderMaterialGraphArtifactFailurePolicy : std::uint8_t {
    LastGoodThenErrorMaterial,
    ErrorMaterial,
};

enum class RenderMaterialGraphArtifactCompileState : std::uint8_t {
    Ready,
    Pending,
    Failed,
};

enum class RenderMaterialGraphArtifactDecisionKind : std::uint8_t {
    UseCurrentArtifact,
    UseLastGoodArtifact,
    UseErrorMaterial,
};

enum class RenderMaterialGraphRuntimeState : std::uint8_t {
    Dirty,
    Validating,
    Compiling,
    CompileFailed,
    UsingLastGood,
    UsingErrorMaterial,
    UsingGpuGraph,
};

enum class RenderMaterialGraphCompilePhase : std::uint8_t {
    Editing,
    Validating,
    Compiling,
    Compiled,
};

struct RenderMaterialGraphRuntimeStateInput {
    RenderMaterialGraphCompilePhase phase = RenderMaterialGraphCompilePhase::Editing;
    bool validationSucceeded = true;
    bool compileSucceeded = false;
    bool hasGpuProgram = false;
    bool hasLastGood = false;
    bool fallbackApplied = false;
    RenderMaterialGraphArtifactFailurePolicy failurePolicy = RenderMaterialGraphArtifactFailurePolicy::LastGoodThenErrorMaterial;
};

enum class RenderMaterialGraphPinType : std::uint8_t {
    Unknown,
    Float,
    Float2,
    Float3,
    Float4,
    Color,
    Texture2D,
    Sampler,
    Normal,
    Bool,
};

enum class RenderMaterialGraphDiagnosticSeverity : std::uint8_t {
    Error,
    Warning,
};

enum class RenderMaterialGraphDiagnosticKind : std::uint8_t {
    DisconnectedRequiredOutput,
    TypeMismatch,
    Cycle,
    UnsupportedNode,
    MissingTexture,
    InvalidColorSpaceRole,
    UnsupportedBlendMode,
    ShaderGenerationFailed,
    DuplicateParameterStableId,
    UnsupportedRenderPathNode,
};

struct RenderMaterialGraphParameterMetadata {
    std::string stableId;
    std::string displayName;
    RenderMaterialParameterGroup group = RenderMaterialParameterGroup::Core;
    std::string defaultValueHint;
    bool hasRange = false;
    float rangeMin = 0.0F;
    float rangeMax = 1.0F;
    std::string textureRole;
    RenderMaterialTextureColorSpace expectedTextureColorSpace = RenderMaterialTextureColorSpace::Unknown;
    bool overrideSupported = true;
    std::uint32_t editorOrder = 0U;
    std::string description;
};

struct RenderMaterialGraphNode {
    std::uint32_t id = 0U;
    RenderMaterialGraphNodeKind kind = RenderMaterialGraphNodeKind::MaterialOutput;
    std::int32_t positionX = 0;
    std::int32_t positionY = 0;
    RenderMaterialGraphParameterMetadata parameter{};
};

struct RenderMaterialGraphLink {
    std::uint32_t id = 0U;
    std::uint32_t fromNodeId = 0U;
    std::uint32_t fromPinId = 0U;
    std::string fromPin;
    std::uint32_t toNodeId = 0U;
    std::uint32_t toPinId = 0U;
    std::string toPin;
};

struct RenderMaterialGraphLastGoodArtifact {
    std::uint64_t assetId = 0U;
    std::uint64_t contentHash = 0U;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return assetId != 0U && contentHash != 0U;
    }
};

struct RenderMaterialGraphDocument {
    std::uint32_t documentVersion = kRenderMaterialGraphDocumentVersion;
    bool hasExplicitDocumentVersion = false;
    std::string materialDomain = "surface";
    std::string shadingModel = "lit";
    std::string storageModel = "inline-kbmat";
    std::uint32_t diagnosticSchemaVersion = 1U;
    bool persistCompileDiagnostics = true;
    RenderMaterialGraphArtifactFailurePolicy artifactFailurePolicy = RenderMaterialGraphArtifactFailurePolicy::LastGoodThenErrorMaterial;
    bool hasExplicitArtifactFailurePolicy = false;
    RenderMaterialGraphLastGoodArtifact lastGoodArtifact{};
    std::vector<RenderMaterialGraphNode> nodes;
    std::vector<RenderMaterialGraphLink> links;
};

struct RenderMaterialGraphArtifactState {
    RenderMaterialGraphArtifactCompileState compileState = RenderMaterialGraphArtifactCompileState::Pending;
    std::uint64_t currentArtifactAssetId = 0U;
};

struct RenderMaterialGraphArtifactDecision {
    RenderMaterialGraphArtifactDecisionKind kind = RenderMaterialGraphArtifactDecisionKind::UseErrorMaterial;
    std::uint64_t artifactAssetId = 0U;
};

struct RenderMaterialGraphDiagnostic {
    RenderMaterialGraphDiagnosticSeverity severity = RenderMaterialGraphDiagnosticSeverity::Error;
    RenderMaterialGraphDiagnosticKind kind = RenderMaterialGraphDiagnosticKind::UnsupportedNode;
    std::uint64_t assetId = 0U;
    std::string sourcePath;
    std::uint32_t nodeId = 0U;
    std::uint32_t linkId = 0U;
    std::uint32_t pinId = 0U;
    std::string pin;
    std::string pass;
    std::string backend;
    std::string message;
};

struct RenderMaterialGraphArtifactRuntimeDecision {
    RenderMaterialGraphArtifactDecision decision{};
    std::optional<RenderMaterialGraphDiagnostic> diagnostic;

    [[nodiscard]] bool UsesFallback() const noexcept;
};

struct RenderMaterialGraphBuildContext {
    std::uint64_t assetId = 0U;
    std::string sourcePath;
};

struct RenderMaterialGraphIrPin {
    std::string name;
    std::uint32_t stablePinId = 0U;
    RenderMaterialGraphPinType type = RenderMaterialGraphPinType::Unknown;
    bool outputPin = false;
};

struct RenderMaterialGraphIrNode {
    std::uint32_t nodeId = 0U;
    RenderMaterialGraphNodeKind kind = RenderMaterialGraphNodeKind::MaterialOutput;
    std::vector<RenderMaterialGraphIrPin> inputs;
    std::vector<RenderMaterialGraphIrPin> outputs;
};

struct RenderMaterialGraphIrLink {
    std::uint32_t linkId = 0U;
    std::uint32_t fromNodeId = 0U;
    std::uint32_t fromPinId = 0U;
    std::string fromPin;
    RenderMaterialGraphPinType fromType = RenderMaterialGraphPinType::Unknown;
    std::uint32_t toNodeId = 0U;
    std::uint32_t toPinId = 0U;
    std::string toPin;
    RenderMaterialGraphPinType toType = RenderMaterialGraphPinType::Unknown;
};

struct RenderMaterialGraphIrOutputBinding {
    std::string outputPin;
    std::uint32_t outputNodeId = 0U;
    std::uint32_t outputPinId = 0U;
    RenderMaterialGraphPinType outputType = RenderMaterialGraphPinType::Unknown;
    std::uint32_t sourceNodeId = 0U;
    std::uint32_t sourcePinId = 0U;
    std::string sourcePin;
    RenderMaterialGraphPinType sourceType = RenderMaterialGraphPinType::Unknown;
};

struct RenderMaterialGraphIr {
    std::vector<RenderMaterialGraphIrNode> nodes;
    std::vector<RenderMaterialGraphIrLink> links;
    std::vector<RenderMaterialGraphIrOutputBinding> outputBindings;
};

struct RenderMaterialGraphIrBuildResult {
    RenderMaterialGraphIr ir;
    std::vector<RenderMaterialGraphDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept;
};

struct RenderMaterialGraphReflectionUniform {
    std::string name;
    std::string stableId;
    RenderMaterialGraphNodeKind kind = RenderMaterialGraphNodeKind::ParameterScalar;
};

struct RenderMaterialGraphReflectionTexture {
    std::string samplerName;
    std::string stableId;
    std::uint32_t slot = 0U;
    RenderMaterialTextureColorSpace colorSpace = RenderMaterialTextureColorSpace::Unknown;
};

struct RenderMaterialGraphReflection {
    std::vector<RenderMaterialGraphReflectionUniform> uniforms;
    std::vector<RenderMaterialGraphReflectionTexture> textures;
    std::vector<std::string> requiredVaryings;
};

struct RenderMaterialGraphShaderSource {
    std::string entryPoint = "EvaluateMaterialGraph";
    std::string source;
    std::uint64_t sourceHash = 0U;
    RenderMaterialGraphReflection reflection;
};

struct RenderMaterialGraphCompileResult {
    RenderMaterialGraphShaderSource shader;
    std::vector<RenderMaterialGraphDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept;
};

struct RenderMaterialGraphDependencyHashInput {
    std::uint64_t assetId = 0U;
    std::uint64_t contentHash = 0U;
    std::string name;
};

struct RenderMaterialGraphCompileArtifactCacheKey {
    std::uint64_t graphContentHash = 0U;
    std::uint64_t dependencyHash = 0U;
    std::uint64_t shaderIncludeHash = 0U;
    std::uint64_t combinedHash = 0U;

    [[nodiscard]] bool operator==(const RenderMaterialGraphCompileArtifactCacheKey& rhs) const noexcept;
};

struct RenderMaterialGraphCompileArtifact {
    RenderMaterialGraphCompileArtifactCacheKey key{};
    RenderMaterialGraphShaderSource shader;
};

struct RenderMaterialGraphCompileArtifactCacheResult {
    RenderMaterialGraphCompileResult compile;
    RenderMaterialGraphCompileArtifactCacheKey key{};
    bool cacheHit = false;
};

class RenderMaterialGraphCompileArtifactCache {
public:
    [[nodiscard]] const RenderMaterialGraphCompileArtifact* Find(const RenderMaterialGraphCompileArtifactCacheKey& key) const noexcept;
    void Store(RenderMaterialGraphCompileArtifact artifact);
    [[nodiscard]] bool Invalidate(const RenderMaterialGraphCompileArtifactCacheKey& key);
    [[nodiscard]] std::size_t InvalidateGraphContentHash(std::uint64_t graphContentHash);
    void Clear() noexcept;
    [[nodiscard]] std::size_t Size() const noexcept;

private:
    std::vector<RenderMaterialGraphCompileArtifact> artifacts_;
};

struct RenderMaterialGraphMaterialTypeBuildResult {
    std::optional<RenderMaterialTypeDocument> document;
    std::vector<RenderMaterialGraphDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept;
};

[[nodiscard]] std::string_view RenderMaterialGraphNodeKindName(RenderMaterialGraphNodeKind kind) noexcept;
[[nodiscard]] std::optional<RenderMaterialGraphNodeKind> ParseRenderMaterialGraphNodeKind(std::string_view text) noexcept;
[[nodiscard]] std::string_view RenderMaterialGraphArtifactFailurePolicyName(RenderMaterialGraphArtifactFailurePolicy policy) noexcept;
[[nodiscard]] std::optional<RenderMaterialGraphArtifactFailurePolicy> ParseRenderMaterialGraphArtifactFailurePolicy(std::string_view text) noexcept;
[[nodiscard]] std::string_view RenderMaterialGraphDiagnosticKindName(RenderMaterialGraphDiagnosticKind kind) noexcept;
[[nodiscard]] std::string_view RenderMaterialGraphDiagnosticSeverityName(RenderMaterialGraphDiagnosticSeverity severity) noexcept;
[[nodiscard]] std::string_view RenderMaterialGraphRenderPathName(RenderMaterialGraphRenderPath path) noexcept;
[[nodiscard]] std::string_view RenderMaterialGraphNodeSupportName(RenderMaterialGraphNodeSupport support) noexcept;
[[nodiscard]] RenderMaterialGraphNodeSupport RenderMaterialGraphNodeSupportStatus(RenderMaterialGraphNodeKind kind) noexcept;
[[nodiscard]] bool IsRenderMaterialGraphRenderPathProduction(RenderMaterialGraphRenderPath path) noexcept;
[[nodiscard]] RenderMaterialGraphNodeSupport RenderMaterialGraphNodeSupportForPath(RenderMaterialGraphNodeKind kind, RenderMaterialGraphRenderPath path) noexcept;
[[nodiscard]] std::string_view RenderMaterialGraphNodeSupportShortTag(RenderMaterialGraphNodeKind kind) noexcept;
[[nodiscard]] std::span<const RenderMaterialGraphNodeKind> AllRenderMaterialGraphNodeKinds() noexcept;
[[nodiscard]] RenderMaterialGraphDocument MakeDefaultRenderMaterialGraphDocument();
void WriteRenderMaterialGraphDocument(std::ostream& output, const RenderMaterialGraphDocument& graph);
[[nodiscard]] RenderMaterialGraphIrBuildResult BuildRenderMaterialGraphIr(
    const RenderMaterialGraphDocument& graph,
    RenderMaterialGraphBuildContext context = {});
[[nodiscard]] RenderMaterialGraphCompileResult CompileRenderMaterialGraphToShaderSource(
    const RenderMaterialGraphDocument& graph,
    RenderMaterialGraphBuildContext context = {});
[[nodiscard]] std::uint64_t RenderMaterialGraphCompileInvocationCount() noexcept;
[[nodiscard]] RenderMaterialGraphCompileArtifactCacheKey BuildRenderMaterialGraphCompileArtifactCacheKey(
    const RenderMaterialGraphDocument& graph,
    std::span<const RenderMaterialGraphDependencyHashInput> dependencies = {},
    std::uint64_t shaderIncludeHash = 0U);
[[nodiscard]] RenderMaterialGraphCompileArtifactCacheResult CompileRenderMaterialGraphWithArtifactCache(
    RenderMaterialGraphCompileArtifactCache& cache,
    const RenderMaterialGraphDocument& graph,
    RenderMaterialGraphBuildContext context = {},
    std::span<const RenderMaterialGraphDependencyHashInput> dependencies = {},
    std::uint64_t shaderIncludeHash = 0U);
[[nodiscard]] std::vector<RenderMaterialGraphDiagnostic> ValidateRenderMaterialGraphDocument(
    const RenderMaterialGraphDocument& graph,
    RenderMaterialGraphRenderPath renderPath = RenderMaterialGraphRenderPath::GpuForward);
[[nodiscard]] const RenderMaterialGraphNode* FindRenderMaterialGraphNode(const RenderMaterialGraphDocument& graph, std::uint32_t nodeId) noexcept;
[[nodiscard]] const RenderMaterialGraphLink* FindRenderMaterialGraphLink(const RenderMaterialGraphDocument& graph, std::uint32_t linkId) noexcept;
[[nodiscard]] bool IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept;
[[nodiscard]] bool IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept;
[[nodiscard]] std::string_view RenderMaterialGraphPinTypeName(RenderMaterialGraphPinType type) noexcept;
[[nodiscard]] RenderMaterialGraphPinType RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind kind, std::string_view pin, bool outputPin) noexcept;
[[nodiscard]] bool AreRenderMaterialGraphPinsCompatible(RenderMaterialGraphPinType from, RenderMaterialGraphPinType to) noexcept;
[[nodiscard]] bool AreRenderMaterialGraphPinsCompatible(
    RenderMaterialGraphNodeKind fromKind,
    std::string_view fromPin,
    RenderMaterialGraphNodeKind toKind,
    std::string_view toPin) noexcept;
[[nodiscard]] std::uint32_t RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind kind, std::string_view pin, bool outputPin) noexcept;
[[nodiscard]] std::uint32_t MakeRenderMaterialGraphLinkId(const RenderMaterialGraphLink& link) noexcept;
[[nodiscard]] bool IsRenderMaterialGraphParameterNode(RenderMaterialGraphNodeKind kind) noexcept;
[[nodiscard]] RenderMaterialTypeSchema BuildRenderMaterialGraphParameterSchema(
    const RenderMaterialGraphDocument& graph,
    std::string typeName,
    std::uint32_t typeVersion);
[[nodiscard]] RenderMaterialGraphMaterialTypeBuildResult BuildRenderMaterialGraphMaterialTypeDocument(
    const RenderMaterialGraphDocument& graph,
    std::string typeName,
    std::uint32_t typeVersion,
    RenderMaterialGraphBuildContext context = {});
[[nodiscard]] RenderMaterialGraphArtifactDecision ResolveRenderMaterialGraphArtifactDecision(
    const RenderMaterialGraphDocument& graph,
    const RenderMaterialGraphArtifactState& state) noexcept;
[[nodiscard]] RenderMaterialGraphArtifactRuntimeDecision ResolveRenderMaterialGraphArtifactRuntimeDecision(
    const RenderMaterialGraphDocument& graph,
    const RenderMaterialGraphArtifactState& state,
    RenderMaterialGraphBuildContext context = {});
[[nodiscard]] bool HasGraphAuthoringData(const RenderMaterialGraphDocument& graph) noexcept;
[[nodiscard]] RenderMaterialGraphRuntimeState ResolveRenderMaterialGraphRuntimeState(const RenderMaterialGraphRuntimeStateInput& input) noexcept;
[[nodiscard]] std::string_view RenderMaterialGraphRuntimeStateName(RenderMaterialGraphRuntimeState state) noexcept;
[[nodiscard]] bool RenderMaterialGraphRuntimeStateUsesFallback(RenderMaterialGraphRuntimeState state) noexcept;

enum class MaterialSurfaceAlphaMode : std::uint8_t {
    Opaque,
    Mask,
    Blend,
};

enum class MaterialSurfaceBlendMode : std::uint8_t {
    Opaque,
    AlphaBlend,
    Additive,
};

enum class MaterialSurfaceRenderQueue : std::uint8_t {
    Opaque,
    Transparent,
};

struct MaterialSurface {
    float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float normal[3] = {0.0f, 0.0f, 1.0f};
    float roughness = 1.0f;
    float metallic = 0.0f;
    float occlusion = 1.0f;
    float emissive[3] = {0.0f, 0.0f, 0.0f};
    float alpha = 1.0f;
    float alphaClipThreshold = 0.5f;
    MaterialSurfaceAlphaMode alphaMode = MaterialSurfaceAlphaMode::Opaque;
    MaterialSurfaceBlendMode blendMode = MaterialSurfaceBlendMode::Opaque;
    bool twoSided = false;
    bool depthWrite = true;
    bool castShadow = true;
    MaterialSurfaceRenderQueue renderQueue = MaterialSurfaceRenderQueue::Opaque;
};

struct MaterialGraphContext {
    float uv0[2] = {0.0f, 0.0f};
    float uv1[2] = {0.0f, 0.0f};
    float normal[3] = {0.0f, 0.0f, 1.0f};
    float tangent[3] = {1.0f, 0.0f, 0.0f};
    float bitangent[3] = {0.0f, 1.0f, 0.0f};
    float worldPos[3] = {0.0f, 0.0f, 0.0f};
    float viewDir[3] = {0.0f, 0.0f, 1.0f};
    float vertexColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float time = 0.0f;
};

[[nodiscard]] MaterialSurface DefaultMaterialSurface() noexcept;
[[nodiscard]] MaterialGraphContext DefaultMaterialGraphContext() noexcept;

} // namespace kb::render
