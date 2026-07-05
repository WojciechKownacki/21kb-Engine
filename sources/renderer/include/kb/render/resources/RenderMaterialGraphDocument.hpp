#pragma once

#include <array>
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

inline constexpr std::uint32_t kRenderMaterialGraphDocumentVersion = 2U;

enum class RenderMaterialGraphNodeKind : std::uint8_t {
    MaterialOutput,
    ConstantScalar,
    ConstantVector,
    ConstantColor,
    TextureSample,
    TextureObject,
    TextureSampleCube,
    TextureObjectCube,
    TextureSampleVolume,
    TextureObjectVolume,
    TextureSample2DArray,
    TextureObject2DArray,
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
    RuntimeSwitch,
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
    ArcSineFast,
    ArcCosineFast,
    ArcTangentFast,
    ArcTangent2Fast,
    ConstantVector2,
    Time,
    DeltaTime,
    DynamicParameter,
    VertexColor,
    ScreenPosition,
    LocalPosition,
    ObjectPosition,
    WorldPosition,
    PerInstanceRandom,
    PerInstanceFadeAmount,
    PerInstanceCustomData,
    ObjectRadius,
    ObjectBounds,
    ObjectOrientation,
    PreSkinnedPosition,
    PreSkinnedNormal,
    MakeMaterialAttributes,
    BreakMaterialAttributes,
    BlendMaterialAttributes,
    GetMaterialAttributes,
    SetMaterialAttributes,
    StaticBoolParameter,
    StaticSwitch,
    StaticComponentMask,
    TextureCoordinate,
    Panner,
    Rotator,
    BumpOffset,
    ConstantBiasScale,
    RotateAboutAxis,
    ViewportUV,
    CameraPosition,
    CameraVector,
    ReflectionVector,
    LightVector,
    PixelNormalWS,
    VertexNormalWS,
    VertexTangentWS,
    ViewProperty,
    ViewSize,
    TwoSidedSign,
    SceneColor,
    SceneTexture,
    SceneDepth,
    DepthFade,
    Exponential,
    Exponential2,
    Logarithm,
    Logarithm2,
    Logarithm10,
    SrgbToLinear,
    LinearToSrgb,
    HsvToRgb,
    RgbToHsv,
    DeriveNormalZ,
    Fmod,
    InverseLerp,
    PartialDerivativeX,
    PartialDerivativeY,
    SphereMask,
    BlackBody,
    Noise,
    VectorNoise,
    Sobol,
    AppendVector,
    ColorRamp,
    AntialiasedTextureMask,
    Transform,
    TransformPosition,
    QualitySwitch,
    FeatureLevelSwitch,
    ShadingPathSwitch,
    ShaderStageSwitch,
    CustomCode,
    Reroute,
    NamedRerouteDeclaration,
    NamedRerouteUsage,
    CompositeInput,
    CompositeOutput,
    FunctionInput,
    FunctionOutput,
    MaterialFunctionCall,
    LayerStack,
    CollectionParameter,
    ConstantBool,
    PixelDepth,
    CameraDepthFade,
    DistanceCullFade,
    PixelPosition,
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

struct RenderMaterialGraphNodeSupportMatrixEntry {
    RenderMaterialGraphNodeKind kind = RenderMaterialGraphNodeKind::MaterialOutput;
    RenderMaterialGraphNodeSupport authoringSupport = RenderMaterialGraphNodeSupport::Unsupported;
    RenderMaterialGraphNodeSupport gpuForwardSupport = RenderMaterialGraphNodeSupport::Unsupported;
    RenderMaterialGraphNodeSupport gpuShadowSupport = RenderMaterialGraphNodeSupport::Unsupported;
    RenderMaterialGraphNodeSupport gpuDeferredSupport = RenderMaterialGraphNodeSupport::Unsupported;
    RenderMaterialGraphNodeSupport cpuFallbackSupport = RenderMaterialGraphNodeSupport::Unsupported;
    RenderMaterialGraphNodeSupport previewSupport = RenderMaterialGraphNodeSupport::Unsupported;
    std::string_view note;
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
    TextureCube,
    Texture3D,
    Texture2DArray,
    Sampler,
    Normal,
    Bool,
    // MAT-36: a container carrying the whole MaterialSurface (all output channels) as a single pin.
    MaterialAttributes,
};

enum class RenderMaterialGraphQualityLevel : std::uint8_t {
    Low,
    Medium,
    High,
    Epic,
};

enum class RenderMaterialGraphFeatureLevel : std::uint8_t {
    Es3,
    Sm5,
    Sm6,
};

enum class RenderMaterialGraphShadingPath : std::uint8_t {
    Forward,
    ForwardPlus,
    Deferred,
};

enum class RenderMaterialGraphShaderStage : std::uint8_t {
    Fragment,
    Vertex,
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
    TextureSamplerLimitExceeded,
    UnsupportedMaterialDomain,
    UnsupportedShadingModel,
    StaticPermutationExplosion,
    MissingMaterialFunction,
    MaterialFunctionCycle,
    MaterialFunctionSignatureMismatch,
};

enum class RenderMaterialGraphSamplerFilter : std::uint8_t {
    Linear,
    Point,
};

enum class RenderMaterialGraphSamplerWrap : std::uint8_t {
    Repeat,
    Clamp,
    Mirror,
};

struct RenderMaterialGraphSamplerState {
    RenderMaterialGraphSamplerFilter minFilter = RenderMaterialGraphSamplerFilter::Linear;
    RenderMaterialGraphSamplerFilter magFilter = RenderMaterialGraphSamplerFilter::Linear;
    RenderMaterialGraphSamplerFilter mipFilter = RenderMaterialGraphSamplerFilter::Linear;
    RenderMaterialGraphSamplerWrap wrapU = RenderMaterialGraphSamplerWrap::Repeat;
    RenderMaterialGraphSamplerWrap wrapV = RenderMaterialGraphSamplerWrap::Repeat;

    [[nodiscard]] friend bool operator==(const RenderMaterialGraphSamplerState&, const RenderMaterialGraphSamplerState&) noexcept = default;
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
    RenderMaterialGraphSamplerState samplerState{};
    bool overrideSupported = true;
    std::uint32_t editorOrder = 0U;
    std::string description;
};

struct RenderMaterialGraphCustomPin {
    std::string name;
    RenderMaterialGraphPinType type = RenderMaterialGraphPinType::Float4;
};

struct RenderMaterialGraphCustomCode {
    std::string body = "return A * B;";
    RenderMaterialGraphPinType outputType = RenderMaterialGraphPinType::Float4;
    std::vector<RenderMaterialGraphCustomPin> inputs{
        RenderMaterialGraphCustomPin{ .name = "A", .type = RenderMaterialGraphPinType::Float4 },
        RenderMaterialGraphCustomPin{ .name = "B", .type = RenderMaterialGraphPinType::Float4 },
    };
    std::vector<RenderMaterialGraphCustomPin> outputs;
    std::string defines;
    std::string includes;
};

struct RenderMaterialGraphLayerStackParameter {
    std::string pinName;
    RenderMaterialGraphPinType type = RenderMaterialGraphPinType::Float4;
    std::string valueHint;
};

struct RenderMaterialGraphLayerStackEntry {
    std::uint64_t layerFunctionAssetId = 0U;
    std::uint64_t blendFunctionAssetId = 0U;
    bool enabled = true;
    std::string layerName;
    std::string blendName;
    std::string linkState;
    std::vector<RenderMaterialGraphLayerStackParameter> layerParameters;
    std::vector<RenderMaterialGraphLayerStackParameter> blendParameters;
};

struct RenderMaterialGraphNode {
    std::uint32_t id = 0U;
    RenderMaterialGraphNodeKind kind = RenderMaterialGraphNodeKind::MaterialOutput;
    std::int32_t positionX = 0;
    std::int32_t positionY = 0;
    RenderMaterialGraphParameterMetadata parameter{};
    RenderMaterialGraphCustomCode customCode{};
    std::vector<RenderMaterialGraphLayerStackEntry> layerStack;
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

struct RenderMaterialGraphCommentBox {
    std::uint32_t id = 0U;
    std::int32_t positionX = 0;
    std::int32_t positionY = 0;
    std::int32_t width = 320;
    std::int32_t height = 180;
    std::uint32_t color = 0x4A6385U;
    std::string text;
};

struct RenderMaterialGraphCompositeSubgraph {
    std::uint32_t id = 0U;
    std::int32_t positionX = 0;
    std::int32_t positionY = 0;
    std::int32_t width = 420;
    std::int32_t height = 260;
    std::uint32_t color = 0x425B4AU;
    bool collapsed = false;
    std::string name;
    std::vector<std::uint32_t> nodeIds;
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
    std::string shadingModel = "defaultLit";
    std::string blendMode = "opaque";
    std::string storageModel = "inline-kbmat";
    std::uint32_t diagnosticSchemaVersion = 1U;
    bool persistCompileDiagnostics = true;
    RenderMaterialGraphArtifactFailurePolicy artifactFailurePolicy = RenderMaterialGraphArtifactFailurePolicy::LastGoodThenErrorMaterial;
    bool hasExplicitArtifactFailurePolicy = false;
    RenderMaterialGraphLastGoodArtifact lastGoodArtifact{};
    std::vector<RenderMaterialGraphNode> nodes;
    std::vector<RenderMaterialGraphLink> links;
    std::vector<RenderMaterialGraphCommentBox> comments;
    std::vector<RenderMaterialGraphCompositeSubgraph> composites;
};

struct RenderMaterialGraphFunctionLibraryEntry {
    std::uint64_t assetId = 0U;
    std::uint64_t contentHash = 0U;
    std::string name;
    RenderMaterialGraphDocument graph;
};

struct RenderMaterialGraphFunctionLibrary {
    std::vector<RenderMaterialGraphFunctionLibraryEntry> entries;

    [[nodiscard]] const RenderMaterialGraphFunctionLibraryEntry* Find(std::uint64_t assetId) const noexcept;
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
    RenderMaterialGraphQualityLevel qualityLevel = RenderMaterialGraphQualityLevel::High;
    RenderMaterialGraphFeatureLevel featureLevel = RenderMaterialGraphFeatureLevel::Sm5;
    RenderMaterialGraphShadingPath shadingPath = RenderMaterialGraphShadingPath::Forward;
    RenderMaterialGraphShaderStage shaderStage = RenderMaterialGraphShaderStage::Fragment;
    const RenderMaterialGraphFunctionLibrary* functionLibrary = nullptr;
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

struct RenderMaterialGraphFunctionInlineResult {
    RenderMaterialGraphDocument graph;
    std::vector<RenderMaterialGraphDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept;
};

enum class RenderMaterialGraphReflectionUniformSource : std::uint8_t {
    MaterialParameter,
    ParameterCollection,
};

struct RenderMaterialGraphReflectionUniform {
    std::string name;
    std::string stableId;
    RenderMaterialGraphNodeKind kind = RenderMaterialGraphNodeKind::ParameterScalar;
    RenderMaterialGraphReflectionUniformSource source = RenderMaterialGraphReflectionUniformSource::MaterialParameter;
    std::uint64_t collectionAssetId = 0U;
    std::string collectionParameterStableId;
    std::array<float, 4U> defaultValue{};
};

// Builtin forward samplers occupy texture stages 0-5 (albedo, normal, metallicRoughness, occlusion,
// emissive, shadow), bound for every mesh draw including graph programs. Graph textures therefore
// start at stage 6 so they never collide with those builtin bindings (MAT-78).
inline constexpr std::uint32_t kRenderMaterialGraphTextureBaseSlot = 6U;
// Conservative sampler ceiling shared by every backend we target (D3D11/D3D12/Vulkan/Metal/GL3+/GLES3
// all expose >= 16 fragment samplers). Graph textures must fit in [base, ceiling).
inline constexpr std::uint32_t kRenderMaterialGraphMaxTextureSamplers = 16U;

enum class RenderMaterialGraphTextureDimension : std::uint8_t {
    Texture2D,
    TextureCube,
    Texture3D,
    Texture2DArray,
};

struct RenderMaterialGraphReflectionTexture {
    std::string samplerName;
    std::string stableId;
    std::uint32_t slot = 0U;
    RenderMaterialTextureColorSpace colorSpace = RenderMaterialTextureColorSpace::Unknown;
    RenderMaterialGraphSamplerState samplerState{};
    RenderMaterialGraphTextureDimension dimension = RenderMaterialGraphTextureDimension::Texture2D;
};

struct RenderMaterialGraphReflection {
    std::vector<RenderMaterialGraphReflectionUniform> uniforms;
    std::vector<RenderMaterialGraphReflectionTexture> textures;
    std::vector<std::string> requiredVaryings;
    // MAT-81: the graph drives a vertex-shader world-position offset, so the cook must build a generated
    // vertex shader (the EvaluateWorldPositionOffset function in the shader source) instead of the fixed VS.
    bool hasWorldPositionOffset = false;
    bool hasCustomizedUv0 = false;
    bool hasDisplacement = false;
    bool hasTangentOutput = false;
    // MAT-37: resolved surface shading model. Drives the fragment wrapper lighting branch and the program
    // key. Declared models without production shader branches fail compilation instead of falling back.
    RenderMaterialShadingModel shadingModel = RenderMaterialShadingModel::DefaultLit;
    // MAT-38: resolved blend mode. Masked makes the fragment wrapper clip on alphaClipThreshold; the
    // transparent modes select the BaseTransparent cook and the scene blend equation, and contribute to
    // the program key.
    RenderMaterialGraphBlendMode blendMode = RenderMaterialGraphBlendMode::Opaque;
    // MAT-80/#18b: the graph samples the opaque scene depth (SceneDepth / DepthFade), so the scene binds
    // the resolved opaque depth texture to the graph fragment shader in the transparent pass.
    bool usesSceneDepth = false;
    // MAT-31: the graph samples a snapshot of opaque scene color (SceneColor / SceneTexture color path).
    bool usesSceneColor = false;
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
    void SetCapacity(std::size_t maxEntries) noexcept;
    [[nodiscard]] std::size_t Capacity() const noexcept;
    [[nodiscard]] std::size_t EvictionCount() const noexcept;
    [[nodiscard]] bool Invalidate(const RenderMaterialGraphCompileArtifactCacheKey& key);
    [[nodiscard]] std::size_t InvalidateGraphContentHash(std::uint64_t graphContentHash);
    void Clear() noexcept;
    [[nodiscard]] std::size_t Size() const noexcept;

private:
    void EnforceCapacity() noexcept;

    std::vector<RenderMaterialGraphCompileArtifact> artifacts_;
    std::size_t maxEntries_ = 0U;
    std::size_t evictionCount_ = 0U;
};

struct RenderMaterialGraphMaterialTypeBuildResult {
    std::optional<RenderMaterialTypeDocument> document;
    std::vector<RenderMaterialGraphDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept;
};

[[nodiscard]] std::string_view RenderMaterialGraphNodeKindName(RenderMaterialGraphNodeKind kind) noexcept;
[[nodiscard]] std::optional<RenderMaterialGraphNodeKind> ParseRenderMaterialGraphNodeKind(std::string_view text) noexcept;
// Authoritative input/output pin names for a node kind, in layout order. The editor uses these so every
// node renders with the correct pins without duplicating the pin schema.
[[nodiscard]] std::vector<std::string> RenderMaterialGraphNodeInputPinNames(RenderMaterialGraphNodeKind kind);
[[nodiscard]] std::vector<std::string> RenderMaterialGraphNodeOutputPinNames(RenderMaterialGraphNodeKind kind);
[[nodiscard]] std::vector<std::string> RenderMaterialGraphNodeInputPinNames(const RenderMaterialGraphNode& node);
[[nodiscard]] std::vector<std::string> RenderMaterialGraphNodeOutputPinNames(const RenderMaterialGraphNode& node);
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
[[nodiscard]] std::vector<RenderMaterialGraphNodeSupportMatrixEntry> BuildRenderMaterialGraphNodeSupportMatrix();
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
    std::uint64_t shaderIncludeHash = 0U,
    RenderMaterialGraphBuildContext context = {});
[[nodiscard]] RenderMaterialGraphCompileArtifactCacheResult CompileRenderMaterialGraphWithArtifactCache(
    RenderMaterialGraphCompileArtifactCache& cache,
    const RenderMaterialGraphDocument& graph,
    RenderMaterialGraphBuildContext context = {},
    std::span<const RenderMaterialGraphDependencyHashInput> dependencies = {},
    std::uint64_t shaderIncludeHash = 0U);
[[nodiscard]] std::vector<std::uint64_t> DiscoverRenderMaterialGraphFunctionDependencies(const RenderMaterialGraphDocument& graph);
[[nodiscard]] RenderMaterialGraphCustomCode BuildRenderMaterialFunctionCallCustomCode(const RenderMaterialGraphDocument& functionGraph);
[[nodiscard]] RenderMaterialGraphFunctionInlineResult InlineRenderMaterialGraphFunctions(
    const RenderMaterialGraphDocument& graph,
    RenderMaterialGraphBuildContext context = {});
[[nodiscard]] std::vector<RenderMaterialGraphDiagnostic> ValidateRenderMaterialGraphDocument(
    const RenderMaterialGraphDocument& graph,
    RenderMaterialGraphRenderPath renderPath = RenderMaterialGraphRenderPath::GpuForward);
[[nodiscard]] const RenderMaterialGraphNode* FindRenderMaterialGraphNode(const RenderMaterialGraphDocument& graph, std::uint32_t nodeId) noexcept;
[[nodiscard]] const RenderMaterialGraphLink* FindRenderMaterialGraphLink(const RenderMaterialGraphDocument& graph, std::uint32_t linkId) noexcept;
[[nodiscard]] bool IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept;
[[nodiscard]] bool IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept;
[[nodiscard]] bool IsRenderMaterialGraphInputPin(const RenderMaterialGraphNode& node, std::string_view pin) noexcept;
[[nodiscard]] bool IsRenderMaterialGraphOutputPin(const RenderMaterialGraphNode& node, std::string_view pin) noexcept;
[[nodiscard]] std::string_view RenderMaterialGraphPinTypeName(RenderMaterialGraphPinType type) noexcept;
[[nodiscard]] std::optional<RenderMaterialGraphPinType> ParseRenderMaterialGraphPinType(std::string_view text) noexcept;
[[nodiscard]] RenderMaterialGraphPinType RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind kind, std::string_view pin, bool outputPin) noexcept;
[[nodiscard]] RenderMaterialGraphPinType RenderMaterialGraphPinDataType(const RenderMaterialGraphNode& node, std::string_view pin, bool outputPin) noexcept;
[[nodiscard]] bool AreRenderMaterialGraphPinsCompatible(RenderMaterialGraphPinType from, RenderMaterialGraphPinType to) noexcept;
[[nodiscard]] bool AreRenderMaterialGraphPinsCompatible(
    RenderMaterialGraphNodeKind fromKind,
    std::string_view fromPin,
    RenderMaterialGraphNodeKind toKind,
    std::string_view toPin) noexcept;
[[nodiscard]] bool AreRenderMaterialGraphPinsCompatible(
    const RenderMaterialGraphNode& fromNode,
    std::string_view fromPin,
    const RenderMaterialGraphNode& toNode,
    std::string_view toPin) noexcept;
[[nodiscard]] std::uint32_t RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind kind, std::string_view pin, bool outputPin) noexcept;
[[nodiscard]] std::uint32_t RenderMaterialGraphStablePinId(const RenderMaterialGraphNode& node, std::string_view pin, bool outputPin) noexcept;
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
    float tangentOutput[3] = {1.0f, 0.0f, 0.0f};
    float roughness = 1.0f;
    float metallic = 0.0f;
    float specular = 0.5f;
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
    float deltaTime = 0.0f;
    float dynamicParameter[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float screenPosition[2] = {0.0f, 0.0f};
    float localPosition[3] = {0.0f, 0.0f, 0.0f};
    float objectPosition[3] = {0.0f, 0.0f, 0.0f};
    float perInstanceRandom = 0.0f;
    float perInstanceFadeAmount = 1.0f;
    float perInstanceCustomData = 0.0f;
    float objectRadius = 0.0f;
    float objectBounds[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float objectOrientation[3] = {0.0f, 0.0f, 1.0f};
    float preSkinnedPosition[3] = {0.0f, 0.0f, 0.0f};
    float preSkinnedNormal[3] = {0.0f, 0.0f, 1.0f};
    float cameraPosition[3] = {0.0f, 0.0f, 0.0f};
    float lightVector[3] = {0.0f, 1.0f, 0.0f};
    float viewSize[2] = {0.0f, 0.0f};
    float twoSidedSign = 1.0f;
    float fragmentDepth = 0.0f;
};

[[nodiscard]] MaterialSurface DefaultMaterialSurface() noexcept;
[[nodiscard]] MaterialGraphContext DefaultMaterialGraphContext() noexcept;

} // namespace kb::render
