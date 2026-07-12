#pragma once

#include "scene/material/MaterialEditorParameterModels.hpp"

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

struct MaterialEditorGraphDiagnosticMarker {
    std::uint32_t nodeId = 0U;
    std::uint32_t linkId = 0U;
    std::uint32_t pinId = 0U;
    std::string pin;
    kb::render::RenderMaterialGraphDiagnosticSeverity severity = kb::render::RenderMaterialGraphDiagnosticSeverity::Error;
    kb::render::RenderMaterialGraphDiagnosticKind kind = kb::render::RenderMaterialGraphDiagnosticKind::UnsupportedNode;
    std::string message;
};

enum class MaterialEditorGraphMenuCommand : std::uint8_t {
    None,
    CreateTextureSample,
    CreateTextureParameter,
    CreateTextureObject,
    CreateTextureSampleCube,
    CreateTextureObjectCube,
    CreateTextureSampleVolume,
    CreateTextureObjectVolume,
    CreateTextureSample2DArray,
    CreateTextureObject2DArray,
    CreateUv,
    CreateScalar,
    CreateBool,
    CreateVector2,
    CreateVector,
    CreateColor,
    CreateScalarParameter,
    CreateVectorParameter,
    CreateColorParameter,
    CreateCollectionParameter,
    CreateAdd,
    CreateSubtract,
    CreateMultiply,
    CreateDivide,
    CreatePower,
    CreateOneMinus,
    CreateAbsolute,
    CreateMinimum,
    CreateMaximum,
    CreateSaturate,
    CreateFloor,
    CreateCeil,
    CreateFraction,
    CreateSquareRoot,
    CreateSine,
    CreateCosine,
    CreateExponential,
    CreateExponential2,
    CreateLogarithm,
    CreateLogarithm2,
    CreateSrgbToLinear,
    CreateLinearToSrgb,
    CreateLogarithm10,
    CreateHsvToRgb,
    CreateRgbToHsv,
    CreateDeriveNormalZ,
    CreateFmod,
    CreateInverseLerp,
    CreatePartialDerivativeX,
    CreatePartialDerivativeY,
    CreateSphereMask,
    CreateBlackBody,
    CreateNoise,
    CreateVectorNoise,
    CreateSobol,
    CreateAppendVector,
    CreateColorRamp,
    CreateAntialiasedTextureMask,
    CreateTransform,
    CreateTransformPosition,
    CreateDotProduct,
    CreateCrossProduct,
    CreateNormalize,
    CreateLength,
    CreateDistance,
    CreateBreakVector,
    CreateMakeVector,
    CreateStep,
    CreateSmoothStep,
    CreateIf,
    CreateSwitch,
    CreateDesaturate,
    CreateFresnel,
    CreateNegate,
    CreateSign,
    CreateRound,
    CreateTruncate,
    CreateTangent,
    CreateArcSine,
    CreateArcCosine,
    CreateArcTangent,
    CreateArcTangent2,
    CreateArcSineFast,
    CreateArcCosineFast,
    CreateArcTangentFast,
    CreateArcTangent2Fast,
    CreateClamp,
    CreateLerp,
    CreateNormalUnpack,
    CreateTime,
    CreateDeltaTime,
    CreateDynamicParameter,
    CreateVertexColor,
    CreateScreenPosition,
    CreatePixelPosition,
    CreateLocalPosition,
    CreateObjectPosition,
    CreateWorldPosition,
    CreatePerInstanceRandom,
    CreatePerInstanceFadeAmount,
    CreatePerInstanceCustomData,
    CreateObjectRadius,
    CreateObjectBounds,
    CreateObjectOrientation,
    CreatePreSkinnedPosition,
    CreatePreSkinnedNormal,
    CreateMakeMaterialAttributes,
    CreateBreakMaterialAttributes,
    CreateBlendMaterialAttributes,
    CreateGetMaterialAttributes,
    CreateSetMaterialAttributes,
    CreateStaticBoolParameter,
    CreateStaticSwitch,
    CreateStaticComponentMask,
    CreateQualitySwitch,
    CreateFeatureLevelSwitch,
    CreateShadingPathSwitch,
    CreateShaderStageSwitch,
    CreateTextureCoordinate,
    CreatePanner,
    CreateRotator,
    CreateBumpOffset,
    CreateConstantBiasScale,
    CreateRotateAboutAxis,
    CreateViewportUV,
    CreateCameraPosition,
    CreateCameraVector,
    CreateReflectionVector,
    CreateLightVector,
    CreatePixelNormalWS,
    CreateVertexNormalWS,
    CreateVertexTangentWS,
    CreateViewProperty,
    CreateViewSize,
    CreateTwoSidedSign,
    CreateSceneColor,
    CreateSceneTexture,
    CreateSceneDepth,
    CreateDepthFade,
    CreateCustomCode,
    CreateReroute,
    CreateNamedRerouteDeclaration,
    CreateNamedRerouteUsage,
    CreateCompositeInput,
    CreateCompositeOutput,
    CreateFunctionInput,
    CreateFunctionOutput,
    CreateMaterialFunctionCall,
    CreateLayerStack,
    CreateComposite,
    CreateComment,
    FrameSelected,
    SelectUpstream,
    SelectDownstream,
    AlignLeft,
    AlignCenter,
    AlignRight,
    AlignTop,
    AlignMiddle,
    AlignBottom,
    DistributeHorizontal,
    DistributeVertical,
    PromoteToParameter,
    DisconnectSelected,
    DeleteSelected,
    CreatePixelDepth,
    CreateCameraDepthFade,
    CreateDistanceCullFade,
};

enum class MaterialEditorGraphAlignMode : std::uint8_t {
    Left,
    Center,
    Right,
    Top,
    Middle,
    Bottom,
};

enum class MaterialEditorGraphDistributeAxis : std::uint8_t {
    Horizontal,
    Vertical,
};

enum class MaterialEditorGraphNodePropertyKind : std::uint8_t {
    Text,
    Numeric,
    Color,
    Enum,
    TextureAsset,
};

struct MaterialEditorGraphNodePropertyOption {
    std::string value;
    std::string label;
};

struct MaterialEditorGraphNodeProperty {
    std::uint32_t nodeId = 0U;
    std::string stableId;
    std::string displayName;
    MaterialEditorGraphNodePropertyKind kind = MaterialEditorGraphNodePropertyKind::Numeric;
    kb::render::RenderMaterialParameterType type = kb::render::RenderMaterialParameterType::Scalar;
    MaterialEditorParameterValue value{};
    std::optional<kb::render::RenderMaterialParameterRange> range;
    std::vector<MaterialEditorGraphNodePropertyOption> options;
    std::size_t componentIndex = 0U;
    bool dropdownOpen = false;
    bool enabled = true;
};

struct GraphHintNumericPropertyDefinition {
    std::string_view stableId;
    std::string_view displayName;
    float defaultValue = 0.0F;
    float rangeMin = 0.0F;
    float rangeMax = 1.0F;
};

} // namespace kb::editor
