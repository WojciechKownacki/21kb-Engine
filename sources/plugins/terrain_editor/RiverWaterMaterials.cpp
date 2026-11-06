#include "RiverWaterMaterials.hpp"

#include <fstream>
#include <string_view>

namespace kb::terrain_editor {
namespace {

// Both water materials are unlit and translucent: the colour of the water is the base colour output. The stream
// moves its ripples with the current the river mesh carries in its vertex normals and takes its depth tint, its
// shore foam and the foam on its banks from the depth of the water above the ground (the gap between the scene
// depth and the water's own, in metres at the camera's 0.01 near plane) and the position across the water. Where
// the device can sample the colour behind the water (feature levels above ES3) the water refracts it; the ES3
// variant is the plain tint, chosen by a Feature Level Switch in the graph itself.
constexpr std::string_view kStreamMaterial = R"KBMAT(# KB material
version 1
materialType builtin.pbr
materialTypeVersion 1
baseColor 1 1 1 1
metallicFactor 0
roughnessFactor 1
normalScale 1
occlusionStrength 1
emissiveColor 0 0 0
emissiveStrength 1
alphaMode OPAQUE
alphaCutoff 0.5
translucencyBlend ALPHA
doubleSided false
writesDepth true
tiling 1 1
offset 0 0
clearcoatFactor 0
clearcoatRoughnessFactor 0
sheenColor 0 0 0
sheenRoughnessFactor 0
transmissionFactor 0
thicknessFactor 0
attenuationColor 1 1 1
attenuationDistance 0
subsurfaceColor 1 1 1
subsurfaceFactor 0
anisotropyStrength 0
anisotropyRotation 0
layerWeight 1
decalBlendMode DISABLED
layerBlendMode REPLACE
graphVersion 3
graphMaterialDomain surface
graphShadingModel unlit
graphBlendMode translucent
graphStorageModel inline-kbmat
graphDiagnosticSchemaVersion 1
graphPersistCompileDiagnostics true
graphArtifactFailurePolicy LastGoodThenErrorMaterial
graphNode 1 MaterialOutput 640 240
graphNode 2 TextureCoordinate 0 120
graphParameter 2 _ uv Core 1%201%200 _ _ _ Unknown false 0 _
graphSamplerState 2 Linear Linear Linear Repeat Repeat
graphNode 3 Time 0 240
graphParameter 3 _ time Core _ _ _ _ Unknown true 0 _
graphSamplerState 3 Linear Linear Linear Repeat Repeat
graphNode 4 VertexNormalWS 0 360
graphParameter 4 _ vnormal Core _ _ _ _ Unknown true 0 _
graphSamplerState 4 Linear Linear Linear Repeat Repeat
graphNode 5 BreakVector 0 480
graphParameter 5 _ flowParts Core _ _ _ _ Unknown true 0 _
graphSamplerState 5 Linear Linear Linear Repeat Repeat
graphNode 6 MakeVector 0 600
graphParameter 6 _ flowXZ Core _ _ _ _ Unknown true 0 _
graphSamplerState 6 Linear Linear Linear Repeat Repeat
graphNode 7 Length 0 720
graphParameter 7 _ flowLength Core _ _ _ _ Unknown true 0 _
graphSamplerState 7 Linear Linear Linear Repeat Repeat
graphNode 8 ConstantScalar 0 840
graphParameter 8 _ flowScale Core 10 _ _ _ Unknown false 0 _
graphSamplerState 8 Linear Linear Linear Repeat Repeat
graphNode 9 Multiply 0 960
graphParameter 9 _ speed Core _ _ _ _ Unknown true 0 _
graphSamplerState 9 Linear Linear Linear Repeat Repeat
graphNode 10 Multiply 0 1080
graphParameter 10 _ phase Core _ _ _ _ Unknown true 0 _
graphSamplerState 10 Linear Linear Linear Repeat Repeat
graphNode 11 Panner 0 1200
graphParameter 11 _ panA Core 0%20-0.25 _ _ _ Unknown false 0 _
graphSamplerState 11 Linear Linear Linear Repeat Repeat
graphNode 12 Panner 0 1320
graphParameter 12 _ panB Core 0.04%20-0.17 _ _ _ Unknown false 0 _
graphSamplerState 12 Linear Linear Linear Repeat Repeat
graphNode 13 Noise 0 1440
graphParameter 13 _ noiseA Core simplex%20false%204%201%203%202%200%201 _ _ _ Unknown false 0 _
graphSamplerState 13 Linear Linear Linear Repeat Repeat
graphNode 14 Noise 0 1560
graphParameter 14 _ noiseB Core simplex%20false%209%201%202%202%200%201 _ _ _ Unknown false 0 _
graphSamplerState 14 Linear Linear Linear Repeat Repeat
graphNode 15 Add 0 1680
graphParameter 15 _ noiseSum Core _ _ _ _ Unknown true 0 _
graphSamplerState 15 Linear Linear Linear Repeat Repeat
graphNode 16 ConstantScalar 0 1800
graphParameter 16 _ half Core 0.5 _ _ _ Unknown false 0 _
graphSamplerState 16 Linear Linear Linear Repeat Repeat
graphNode 17 Multiply 0 1920
graphParameter 17 _ ripple Core _ _ _ _ Unknown true 0 _
graphSamplerState 17 Linear Linear Linear Repeat Repeat
graphNode 18 ConstantScalar 0 2040
graphParameter 18 _ fadeDistance Core 0.5 _ _ _ Unknown false 0 _
graphSamplerState 18 Linear Linear Linear Repeat Repeat
graphNode 19 SceneDepth 0 2160
graphParameter 19 _ depthFadeScene Core _ _ _ _ Unknown true 0 _
graphSamplerState 19 Linear Linear Linear Repeat Repeat
graphNode 20 PixelDepth 0 2280
graphParameter 20 _ depthFadePixel Core _ _ _ _ Unknown true 0 _
graphSamplerState 20 Linear Linear Linear Repeat Repeat
graphNode 21 Subtract 0 2400
graphParameter 21 _ depthFadeGap Core _ _ _ _ Unknown true 0 _
graphSamplerState 21 Linear Linear Linear Repeat Repeat
graphNode 22 Absolute 0 2520
graphParameter 22 _ depthFadeGapAbs Core _ _ _ _ Unknown true 0 _
graphSamplerState 22 Linear Linear Linear Repeat Repeat
graphNode 23 Multiply 0 2640
graphParameter 23 _ depthFadePixelSq Core _ _ _ _ Unknown true 0 _
graphSamplerState 23 Linear Linear Linear Repeat Repeat
graphNode 24 Divide 0 2760
graphParameter 24 _ depthFadePerSq Core _ _ _ _ Unknown true 0 _
graphSamplerState 24 Linear Linear Linear Repeat Repeat
graphNode 25 ConstantScalar 0 2880
graphParameter 25 _ depthFadeNear Core 0.01 _ _ _ Unknown false 0 _
graphSamplerState 25 Linear Linear Linear Repeat Repeat
graphNode 26 Multiply 0 3000
graphParameter 26 _ depthFadeThickness Core _ _ _ _ Unknown true 0 _
graphSamplerState 26 Linear Linear Linear Repeat Repeat
graphNode 27 Divide 0 3120
graphParameter 27 _ depthFadeRatio Core _ _ _ _ Unknown true 0 _
graphSamplerState 27 Linear Linear Linear Repeat Repeat
graphNode 28 Saturate 0 3240
graphParameter 28 _ depthFade Core _ _ _ _ Unknown true 0 _
graphSamplerState 28 Linear Linear Linear Repeat Repeat
graphNode 29 OneMinus 0 3360
graphParameter 29 _ shore Core _ _ _ _ Unknown true 0 _
graphSamplerState 29 Linear Linear Linear Repeat Repeat
graphNode 30 BreakVector 0 3480
graphParameter 30 _ uvParts Core _ _ _ _ Unknown true 0 _
graphSamplerState 30 Linear Linear Linear Repeat Repeat
graphNode 31 ConstantScalar 0 3600
graphParameter 31 _ two Core 2 _ _ _ Unknown false 0 _
graphSamplerState 31 Linear Linear Linear Repeat Repeat
graphNode 32 ConstantScalar 0 3720
graphParameter 32 _ one Core 1 _ _ _ Unknown false 0 _
graphSamplerState 32 Linear Linear Linear Repeat Repeat
graphNode 33 Multiply 0 3840
graphParameter 33 _ u2 Core _ _ _ _ Unknown true 0 _
graphSamplerState 33 Linear Linear Linear Repeat Repeat
graphNode 34 Subtract 0 3960
graphParameter 34 _ across Core _ _ _ _ Unknown true 0 _
graphSamplerState 34 Linear Linear Linear Repeat Repeat
graphNode 35 Absolute 0 4080
graphParameter 35 _ acrossAbs Core _ _ _ _ Unknown true 0 _
graphSamplerState 35 Linear Linear Linear Repeat Repeat
graphNode 36 ConstantScalar 0 4200
graphParameter 36 _ bankStart Core 0.55 _ _ _ Unknown false 0 _
graphSamplerState 36 Linear Linear Linear Repeat Repeat
graphNode 37 SmoothStep 0 4320
graphParameter 37 _ bankMask Core _ _ _ _ Unknown true 0 _
graphSamplerState 37 Linear Linear Linear Repeat Repeat
graphNode 38 ConstantScalar 0 4440
graphParameter 38 _ rippleBias Core 0.25 _ _ _ Unknown false 0 _
graphSamplerState 38 Linear Linear Linear Repeat Repeat
graphNode 39 Add 0 4560
graphParameter 39 _ rippleShifted Core _ _ _ _ Unknown true 0 _
graphSamplerState 39 Linear Linear Linear Repeat Repeat
graphNode 40 Multiply 0 4680
graphParameter 40 _ shoreFoam Core _ _ _ _ Unknown true 0 _
graphSamplerState 40 Linear Linear Linear Repeat Repeat
graphNode 41 ConstantScalar 0 4800
graphParameter 41 _ bankWeight Core 0.4 _ _ _ Unknown false 0 _
graphSamplerState 41 Linear Linear Linear Repeat Repeat
graphNode 42 Multiply 0 4920
graphParameter 42 _ bankFoam Core _ _ _ _ Unknown true 0 _
graphSamplerState 42 Linear Linear Linear Repeat Repeat
graphNode 43 Add 0 5040
graphParameter 43 _ foamSum Core _ _ _ _ Unknown true 0 _
graphSamplerState 43 Linear Linear Linear Repeat Repeat
graphNode 44 ConstantScalar 0 5160
graphParameter 44 _ foamLow Core 0.35 _ _ _ Unknown false 0 _
graphSamplerState 44 Linear Linear Linear Repeat Repeat
graphNode 45 ConstantScalar 0 5280
graphParameter 45 _ foamHigh Core 0.6 _ _ _ Unknown false 0 _
graphSamplerState 45 Linear Linear Linear Repeat Repeat
graphNode 46 SmoothStep 0 5400
graphParameter 46 _ foam Core _ _ _ _ Unknown true 0 _
graphSamplerState 46 Linear Linear Linear Repeat Repeat
graphNode 47 ConstantColor 0 5520
graphParameter 47 _ shallowColor Core 0.16%200.46%200.45%201 _ _ _ Unknown false 0 _
graphSamplerState 47 Linear Linear Linear Repeat Repeat
graphNode 48 ConstantColor 0 5640
graphParameter 48 _ deepColor Core 0.03%200.17%200.24%201 _ _ _ Unknown false 0 _
graphSamplerState 48 Linear Linear Linear Repeat Repeat
graphNode 49 ConstantColor 0 5760
graphParameter 49 _ foamColor Core 0.93%200.97%200.97%201 _ _ _ Unknown false 0 _
graphSamplerState 49 Linear Linear Linear Repeat Repeat
graphNode 50 Lerp 0 5880
graphParameter 50 _ waterColor Core _ _ _ _ Unknown true 0 _
graphSamplerState 50 Linear Linear Linear Repeat Repeat
graphNode 51 ViewportUV 0 6000
graphParameter 51 _ screenUv Core _ _ _ _ Unknown true 0 _
graphSamplerState 51 Linear Linear Linear Repeat Repeat
graphNode 52 ConstantScalar 0 6120
graphParameter 52 _ bend Core 0.03 _ _ _ Unknown false 0 _
graphSamplerState 52 Linear Linear Linear Repeat Repeat
graphNode 53 Subtract 0 6240
graphParameter 53 _ rippleCentered Core _ _ _ _ Unknown true 0 _
graphSamplerState 53 Linear Linear Linear Repeat Repeat
graphNode 54 Multiply 0 6360
graphParameter 54 _ bendAmount Core _ _ _ _ Unknown true 0 _
graphSamplerState 54 Linear Linear Linear Repeat Repeat
graphNode 55 Add 0 6480
graphParameter 55 _ bentUv Core _ _ _ _ Unknown true 0 _
graphSamplerState 55 Linear Linear Linear Repeat Repeat
graphNode 56 SceneColor 0 6600
graphParameter 56 _ behind Core _ _ _ _ Unknown true 0 _
graphSamplerState 56 Linear Linear Linear Repeat Repeat
graphNode 57 Multiply 0 6720
graphParameter 57 _ behindTinted Core _ _ _ _ Unknown true 0 _
graphSamplerState 57 Linear Linear Linear Repeat Repeat
graphNode 58 ConstantScalar 0 6840
graphParameter 58 _ bodyAmount Core 0.55 _ _ _ Unknown false 0 _
graphSamplerState 58 Linear Linear Linear Repeat Repeat
graphNode 59 Multiply 0 6960
graphParameter 59 _ bodyMix Core _ _ _ _ Unknown true 0 _
graphSamplerState 59 Linear Linear Linear Repeat Repeat
graphNode 60 Lerp 0 7080
graphParameter 60 _ bodyRefracted Core _ _ _ _ Unknown true 0 _
graphSamplerState 60 Linear Linear Linear Repeat Repeat
graphNode 61 FeatureLevelSwitch 0 7200
graphParameter 61 _ body Core _ _ _ _ Unknown false 0 _
graphSamplerState 61 Linear Linear Linear Repeat Repeat
graphNode 62 Lerp 0 7320
graphParameter 62 _ surface Core _ _ _ _ Unknown true 0 _
graphSamplerState 62 Linear Linear Linear Repeat Repeat
graphNode 63 ConstantScalar 0 7440
graphParameter 63 _ opacityBase Core 0.3 _ _ _ Unknown false 0 _
graphSamplerState 63 Linear Linear Linear Repeat Repeat
graphNode 64 ConstantScalar 0 7560
graphParameter 64 _ opacityGain Core 0.65 _ _ _ Unknown false 0 _
graphSamplerState 64 Linear Linear Linear Repeat Repeat
graphNode 65 Multiply 0 7680
graphParameter 65 _ opacityDepth Core _ _ _ _ Unknown true 0 _
graphSamplerState 65 Linear Linear Linear Repeat Repeat
graphNode 66 Add 0 7800
graphParameter 66 _ opacityRaw Core _ _ _ _ Unknown true 0 _
graphSamplerState 66 Linear Linear Linear Repeat Repeat
graphNode 67 Add 0 7920
graphParameter 67 _ opacityFoam Core _ _ _ _ Unknown true 0 _
graphSamplerState 67 Linear Linear Linear Repeat Repeat
graphNode 68 Saturate 0 8040
graphParameter 68 _ opacity Core _ _ _ _ Unknown true 0 _
graphSamplerState 68 Linear Linear Linear Repeat Repeat
graphLink 1600930922 4 6561793 value 5 2756609 value
graphLink 604782220 5 2760706 x 6 2822145 x
graphLink 3605609945 5 2760708 z 6 2822146 y
graphLink 4127889711 6 2826241 value 7 2625537 value
graphLink 2822790136 7 2629633 value 9 1249281 a
graphLink 108636910 8 139265 value 9 1249282 b
graphLink 2643030375 3 4202497 value 10 1249281 a
graphLink 24059093 9 1253377 value 10 1249282 b
graphLink 4059492325 2 5775361 uv 11 5836801 coordinate
graphLink 1416022093 10 1253377 value 11 5836802 time
graphLink 3476810178 2 5775361 uv 12 5836801 coordinate
graphLink 2506470218 10 1253377 value 12 5836802 time
graphLink 1353655301 11 5840899 uv 13 8196097 value
graphLink 1711259873 12 5840899 uv 14 8196097 value
graphLink 3137445179 13 8200193 value 15 1118209 a
graphLink 2388026923 14 8200193 value 15 1118210 b
graphLink 2825944089 15 1122305 value 17 1249281 a
graphLink 302930222 16 139265 value 17 1249282 b
graphLink 3564689970 19 7020545 value 21 1183745 a
graphLink 1348409528 20 9773057 value 21 1183746 b
graphLink 3969985991 21 1187841 value 22 1773569 value
graphLink 3567505504 20 9773057 value 23 1249281 a
graphLink 3358454179 20 9773057 value 23 1249282 b
graphLink 727674108 22 1777665 value 24 1314817 a
graphLink 7495878 23 1253377 value 24 1314818 b
graphLink 3295614872 24 1318913 value 26 1249281 a
graphLink 804917004 25 139265 value 26 1249282 b
graphLink 3089650171 26 1253377 value 27 1314817 a
graphLink 1075341737 18 139265 value 27 1314818 b
graphLink 3618438856 27 1318913 value 28 1970177 value
graphLink 3303211356 28 1974273 value 29 1445889 value
graphLink 2555865147 2 5775361 uv 30 2756609 value
graphLink 1429180936 30 2760706 x 33 1249281 a
graphLink 5625009 31 139265 value 33 1249282 b
graphLink 2655423599 33 1253377 value 34 1183745 a
graphLink 127663156 32 139265 value 34 1183746 b
graphLink 2068035781 34 1187841 value 35 1773569 value
graphLink 442644659 36 139265 value 37 2953217 min
graphLink 880820612 32 139265 value 37 2953218 max
graphLink 549897751 35 1777665 value 37 2953219 value
graphLink 1426366193 17 1253377 value 39 1118209 a
graphLink 2679244156 38 139265 value 39 1118210 b
graphLink 1790871789 29 1449985 value 40 1249281 a
graphLink 2025700395 39 1122305 value 40 1249282 b
graphLink 2853392036 37 2957313 value 42 1249281 a
graphLink 1272490956 41 139265 value 42 1249282 b
graphLink 2317293300 40 1253377 value 43 1118209 a
graphLink 2403058389 42 1253377 value 43 1118210 b
graphLink 623395568 44 139265 value 46 2953217 min
graphLink 330281122 45 139265 value 46 2953218 max
graphLink 4035175758 43 1122305 value 46 2953219 value
graphLink 3135526708 47 270337 rgba 50 1576961 a
graphLink 427862968 48 270337 rgba 50 1576962 b
graphLink 3886953119 28 1974273 value 50 1576963 t
graphLink 2759877960 17 1253377 value 53 1183745 a
graphLink 1857502003 16 139265 value 53 1183746 b
graphLink 4019540927 53 1187841 value 54 1249281 a
graphLink 1096293405 52 139265 value 54 1249282 b
graphLink 666307574 51 6168577 uv 55 1118209 a
graphLink 3886020229 54 1253377 value 55 1118210 b
graphLink 183022498 55 1122305 value 56 6885377 uv
graphLink 1565799758 56 6889473 color 57 1249281 a
graphLink 2612059391 47 270337 rgba 57 1249282 b
graphLink 1851472487 28 1974273 value 59 1249281 a
graphLink 3268830990 58 139265 value 59 1249282 b
graphLink 2718095795 57 1253377 value 60 1576961 a
graphLink 1965079144 50 1581057 value 60 1576962 b
graphLink 939233555 59 1253377 value 60 1576963 t
graphLink 3274566612 50 1581057 value 61 8785921 es3
graphLink 3127987977 60 1581057 value 61 8785922 sm5
graphLink 2372839064 60 1581057 value 61 8785923 sm6
graphLink 2737586493 61 8790020 result 62 1576961 a
graphLink 1083315365 49 270337 rgba 62 1576962 b
graphLink 550276026 46 2957313 value 62 1576963 t
graphLink 3413079949 28 1974273 value 65 1249281 a
graphLink 3823329070 64 139265 value 65 1249282 b
graphLink 833028916 65 1253377 value 66 1118209 a
graphLink 597234096 63 139265 value 66 1118210 b
graphLink 2062473972 66 1122305 value 67 1118209 a
graphLink 1152394951 46 2957313 value 67 1118210 b
graphLink 4031331429 67 1122305 value 68 1970177 value
graphLink 1278298683 62 1581057 value 1 69633 baseColor
graphLink 2317568129 68 1974273 value 1 69639 alpha
)KBMAT";

// The waterfall: streaks of white water running down the fall, thinner at its edges and soft where it meets the ground.
constexpr std::string_view kWaterfallMaterial = R"KBMAT(# KB material
version 1
materialType builtin.pbr
materialTypeVersion 1
baseColor 1 1 1 1
metallicFactor 0
roughnessFactor 1
normalScale 1
occlusionStrength 1
emissiveColor 0 0 0
emissiveStrength 1
alphaMode OPAQUE
alphaCutoff 0.5
translucencyBlend ALPHA
doubleSided false
writesDepth true
tiling 1 1
offset 0 0
clearcoatFactor 0
clearcoatRoughnessFactor 0
sheenColor 0 0 0
sheenRoughnessFactor 0
transmissionFactor 0
thicknessFactor 0
attenuationColor 1 1 1
attenuationDistance 0
subsurfaceColor 1 1 1
subsurfaceFactor 0
anisotropyStrength 0
anisotropyRotation 0
layerWeight 1
decalBlendMode DISABLED
layerBlendMode REPLACE
graphVersion 3
graphMaterialDomain surface
graphShadingModel unlit
graphBlendMode translucent
graphStorageModel inline-kbmat
graphDiagnosticSchemaVersion 1
graphPersistCompileDiagnostics true
graphArtifactFailurePolicy LastGoodThenErrorMaterial
graphNode 1 MaterialOutput 640 240
graphNode 2 TextureCoordinate 0 120
graphParameter 2 _ uvA Core 3%200.5%200 _ _ _ Unknown false 0 _
graphSamplerState 2 Linear Linear Linear Repeat Repeat
graphNode 3 TextureCoordinate 0 240
graphParameter 3 _ uvB Core 7%200.9%200 _ _ _ Unknown false 0 _
graphSamplerState 3 Linear Linear Linear Repeat Repeat
graphNode 4 Time 0 360
graphParameter 4 _ time Core _ _ _ _ Unknown true 0 _
graphSamplerState 4 Linear Linear Linear Repeat Repeat
graphNode 5 Panner 0 480
graphParameter 5 _ panA Core 0%20-2.4 _ _ _ Unknown false 0 _
graphSamplerState 5 Linear Linear Linear Repeat Repeat
graphNode 6 Panner 0 600
graphParameter 6 _ panB Core 0.05%20-3.6 _ _ _ Unknown false 0 _
graphSamplerState 6 Linear Linear Linear Repeat Repeat
graphNode 7 Noise 0 720
graphParameter 7 _ noiseA Core simplex%20false%206%201%203%202%200%201 _ _ _ Unknown false 0 _
graphSamplerState 7 Linear Linear Linear Repeat Repeat
graphNode 8 Noise 0 840
graphParameter 8 _ noiseB Core simplex%20false%2011%201%202%202%200%201 _ _ _ Unknown false 0 _
graphSamplerState 8 Linear Linear Linear Repeat Repeat
graphNode 9 Add 0 960
graphParameter 9 _ noiseSum Core _ _ _ _ Unknown true 0 _
graphSamplerState 9 Linear Linear Linear Repeat Repeat
graphNode 10 ConstantScalar 0 1080
graphParameter 10 _ half Core 0.5 _ _ _ Unknown false 0 _
graphSamplerState 10 Linear Linear Linear Repeat Repeat
graphNode 11 Multiply 0 1200
graphParameter 11 _ streak Core _ _ _ _ Unknown true 0 _
graphSamplerState 11 Linear Linear Linear Repeat Repeat
graphNode 12 ConstantScalar 0 1320
graphParameter 12 _ streakLow Core 0.3 _ _ _ Unknown false 0 _
graphSamplerState 12 Linear Linear Linear Repeat Repeat
graphNode 13 ConstantScalar 0 1440
graphParameter 13 _ streakHigh Core 0.72 _ _ _ Unknown false 0 _
graphSamplerState 13 Linear Linear Linear Repeat Repeat
graphNode 14 SmoothStep 0 1560
graphParameter 14 _ white Core _ _ _ _ Unknown true 0 _
graphSamplerState 14 Linear Linear Linear Repeat Repeat
graphNode 15 ConstantColor 0 1680
graphParameter 15 _ tint Core 0.45%200.72%200.8%201 _ _ _ Unknown false 0 _
graphSamplerState 15 Linear Linear Linear Repeat Repeat
graphNode 16 ConstantColor 0 1800
graphParameter 16 _ foamColor Core 0.97%200.99%200.99%201 _ _ _ Unknown false 0 _
graphSamplerState 16 Linear Linear Linear Repeat Repeat
graphNode 17 Lerp 0 1920
graphParameter 17 _ color Core _ _ _ _ Unknown true 0 _
graphSamplerState 17 Linear Linear Linear Repeat Repeat
graphNode 18 BreakVector 0 2040
graphParameter 18 _ uvParts Core _ _ _ _ Unknown true 0 _
graphSamplerState 18 Linear Linear Linear Repeat Repeat
graphNode 19 ConstantScalar 0 2160
graphParameter 19 _ two Core 2 _ _ _ Unknown false 0 _
graphSamplerState 19 Linear Linear Linear Repeat Repeat
graphNode 20 ConstantScalar 0 2280
graphParameter 20 _ one Core 1 _ _ _ Unknown false 0 _
graphSamplerState 20 Linear Linear Linear Repeat Repeat
graphNode 21 TextureCoordinate 0 2400
graphParameter 21 _ uv Core 1%201%200 _ _ _ Unknown false 0 _
graphSamplerState 21 Linear Linear Linear Repeat Repeat
graphNode 22 BreakVector 0 2520
graphParameter 22 _ across0 Core _ _ _ _ Unknown true 0 _
graphSamplerState 22 Linear Linear Linear Repeat Repeat
graphNode 23 Multiply 0 2640
graphParameter 23 _ u2 Core _ _ _ _ Unknown true 0 _
graphSamplerState 23 Linear Linear Linear Repeat Repeat
graphNode 24 Subtract 0 2760
graphParameter 24 _ across Core _ _ _ _ Unknown true 0 _
graphSamplerState 24 Linear Linear Linear Repeat Repeat
graphNode 25 Absolute 0 2880
graphParameter 25 _ acrossAbs Core _ _ _ _ Unknown true 0 _
graphSamplerState 25 Linear Linear Linear Repeat Repeat
graphNode 26 ConstantScalar 0 3000
graphParameter 26 _ edgeStart Core 0.6 _ _ _ Unknown false 0 _
graphSamplerState 26 Linear Linear Linear Repeat Repeat
graphNode 27 SmoothStep 0 3120
graphParameter 27 _ edge Core _ _ _ _ Unknown true 0 _
graphSamplerState 27 Linear Linear Linear Repeat Repeat
graphNode 28 OneMinus 0 3240
graphParameter 28 _ edgeKeep Core _ _ _ _ Unknown true 0 _
graphSamplerState 28 Linear Linear Linear Repeat Repeat
graphNode 29 ConstantScalar 0 3360
graphParameter 29 _ alphaBase Core 0.5 _ _ _ Unknown false 0 _
graphSamplerState 29 Linear Linear Linear Repeat Repeat
graphNode 30 ConstantScalar 0 3480
graphParameter 30 _ alphaGain Core 0.45 _ _ _ Unknown false 0 _
graphSamplerState 30 Linear Linear Linear Repeat Repeat
graphNode 31 Multiply 0 3600
graphParameter 31 _ alphaWhite Core _ _ _ _ Unknown true 0 _
graphSamplerState 31 Linear Linear Linear Repeat Repeat
graphNode 32 Add 0 3720
graphParameter 32 _ alphaSum Core _ _ _ _ Unknown true 0 _
graphSamplerState 32 Linear Linear Linear Repeat Repeat
graphNode 33 Multiply 0 3840
graphParameter 33 _ alphaEdge Core _ _ _ _ Unknown true 0 _
graphSamplerState 33 Linear Linear Linear Repeat Repeat
graphNode 34 ConstantScalar 0 3960
graphParameter 34 _ softDistance Core 0.7 _ _ _ Unknown false 0 _
graphSamplerState 34 Linear Linear Linear Repeat Repeat
graphNode 35 SceneDepth 0 4080
graphParameter 35 _ softFadeScene Core _ _ _ _ Unknown true 0 _
graphSamplerState 35 Linear Linear Linear Repeat Repeat
graphNode 36 PixelDepth 0 4200
graphParameter 36 _ softFadePixel Core _ _ _ _ Unknown true 0 _
graphSamplerState 36 Linear Linear Linear Repeat Repeat
graphNode 37 Subtract 0 4320
graphParameter 37 _ softFadeGap Core _ _ _ _ Unknown true 0 _
graphSamplerState 37 Linear Linear Linear Repeat Repeat
graphNode 38 Absolute 0 4440
graphParameter 38 _ softFadeGapAbs Core _ _ _ _ Unknown true 0 _
graphSamplerState 38 Linear Linear Linear Repeat Repeat
graphNode 39 Multiply 0 4560
graphParameter 39 _ softFadePixelSq Core _ _ _ _ Unknown true 0 _
graphSamplerState 39 Linear Linear Linear Repeat Repeat
graphNode 40 Divide 0 4680
graphParameter 40 _ softFadePerSq Core _ _ _ _ Unknown true 0 _
graphSamplerState 40 Linear Linear Linear Repeat Repeat
graphNode 41 ConstantScalar 0 4800
graphParameter 41 _ softFadeNear Core 0.01 _ _ _ Unknown false 0 _
graphSamplerState 41 Linear Linear Linear Repeat Repeat
graphNode 42 Multiply 0 4920
graphParameter 42 _ softFadeThickness Core _ _ _ _ Unknown true 0 _
graphSamplerState 42 Linear Linear Linear Repeat Repeat
graphNode 43 Divide 0 5040
graphParameter 43 _ softFadeRatio Core _ _ _ _ Unknown true 0 _
graphSamplerState 43 Linear Linear Linear Repeat Repeat
graphNode 44 Saturate 0 5160
graphParameter 44 _ softFade Core _ _ _ _ Unknown true 0 _
graphSamplerState 44 Linear Linear Linear Repeat Repeat
graphNode 45 Multiply 0 5280
graphParameter 45 _ alphaSoft Core _ _ _ _ Unknown true 0 _
graphSamplerState 45 Linear Linear Linear Repeat Repeat
graphNode 46 Saturate 0 5400
graphParameter 46 _ alpha Core _ _ _ _ Unknown true 0 _
graphSamplerState 46 Linear Linear Linear Repeat Repeat
graphLink 3734937691 2 5775361 uv 5 5836801 coordinate
graphLink 3099281862 4 4202497 value 5 5836802 time
graphLink 401508297 3 5775361 uv 6 5836801 coordinate
graphLink 2764168869 4 4202497 value 6 5836802 time
graphLink 1500237169 5 5840899 uv 7 8196097 value
graphLink 3900326829 6 5840899 uv 8 8196097 value
graphLink 1825346039 7 8200193 value 9 1118209 a
graphLink 1947173451 8 8200193 value 9 1118210 b
graphLink 851557941 9 1122305 value 11 1249281 a
graphLink 3352589198 10 139265 value 11 1249282 b
graphLink 4062202288 12 139265 value 14 2953217 min
graphLink 2284773858 13 139265 value 14 2953218 max
graphLink 2423279996 11 1253377 value 14 2953219 value
graphLink 3438657319 15 270337 rgba 17 1576961 a
graphLink 3100642123 16 270337 rgba 17 1576962 b
graphLink 2847773669 14 2957313 value 17 1576963 t
graphLink 3652066967 2 5775361 uv 18 2756609 value
graphLink 285983812 21 5775361 uv 22 2756609 value
graphLink 3798958054 22 2760706 x 23 1249281 a
graphLink 1305499547 19 139265 value 23 1249282 b
graphLink 3340465763 23 1253377 value 24 1183745 a
graphLink 1462152698 20 139265 value 24 1183746 b
graphLink 36199877 24 1187841 value 25 1773569 value
graphLink 4281719443 26 139265 value 27 2953217 min
graphLink 2379871710 20 139265 value 27 2953218 max
graphLink 2910412355 25 1777665 value 27 2953219 value
graphLink 4202787217 27 2957313 value 28 1445889 value
graphLink 1684158522 14 2957313 value 31 1249281 a
graphLink 793882190 30 139265 value 31 1249282 b
graphLink 2594994131 29 139265 value 32 1118209 a
graphLink 3781138155 31 1253377 value 32 1118210 b
graphLink 2782192742 32 1122305 value 33 1249281 a
graphLink 2416561654 28 1449985 value 33 1249282 b
graphLink 2549032978 35 7020545 value 37 1183745 a
graphLink 3013455000 36 9773057 value 37 1183746 b
graphLink 349364743 37 1187841 value 38 1773569 value
graphLink 937583680 36 9773057 value 39 1249281 a
graphLink 728532355 36 9773057 value 39 1249282 b
graphLink 2962072348 38 1777665 value 40 1314817 a
graphLink 188227366 39 1253377 value 40 1314818 b
graphLink 1994198456 40 1318913 value 42 1249281 a
graphLink 1272490956 41 139265 value 42 1249282 b
graphLink 3839734427 42 1253377 value 43 1314817 a
graphLink 135422953 34 139265 value 43 1314818 b
graphLink 1557869800 43 1318913 value 44 1970177 value
graphLink 2200631753 33 1253377 value 45 1249281 a
graphLink 3725143554 44 1974273 value 45 1249282 b
graphLink 3351437891 45 1253377 value 46 1970177 value
graphLink 3054744676 17 1581057 value 1 69633 baseColor
graphLink 2079034235 46 1974273 value 1 69639 alpha
)KBMAT";

// The mist at the foot of a waterfall: a line of up to 480 soft particles (a handheld's budget) across the fall.
constexpr std::string_view kMistEffect = R"KBMAT(21kb ParticleEffect 2
effect.formatVersion 2
effect.id 1
effect.displayName "River Mist"
effect.recipeCategory "General"
effect.determinismSeed 7737841948295254612
effect.durationSeconds 4
effect.looping true
effect.backendPolicy CpuDeterministic
effect.gpuCatchupPolicy RestartFromSeed
effect.emitterCount 1
effect.emitter[0].id 1
effect.emitter[0].authoringOrder 0
effect.emitter[0].name "Mist"
effect.emitter[0].enabled true
effect.emitter[0].localPosition 0 0 0
effect.emitter[0].localRotation 0 0 0 1
effect.emitter[0].localScale 1 1 1
effect.emitter[0].maxParticles 480
effect.emitter[0].simulationSpace World
effect.emitter[0].spawn.mode Continuous
effect.emitter[0].spawn.rate.keyCount 1
effect.emitter[0].spawn.rate.key[0].time 0
effect.emitter[0].spawn.rate.key[0].value 60
effect.emitter[0].spawn.rate.key[0].easing Linear
effect.emitter[0].spawn.burstCount 0
effect.emitter[0].spawn.lifetimeMin 1.60000002
effect.emitter[0].spawn.lifetimeMax 2.5999999
effect.emitter[0].spawn.speedMin 0.600000024
effect.emitter[0].spawn.speedMax 1.60000002
effect.emitter[0].spawn.direction 0 1 0.349999994
effect.emitter[0].spawn.spreadDegrees 38
effect.emitter[0].spawn.randomization 1
effect.emitter[0].spawn.prewarmSeconds 0
effect.emitter[0].spawn.startColor 0.860000014 0.930000007 0.949999988 0.5
effect.emitter[0].spawn.startSize 1.10000002
effect.emitter[0].spawn.shape.type Line
effect.emitter[0].spawn.shape.radius 0
effect.emitter[0].spawn.shape.boxSize 0 0 0
effect.emitter[0].spawn.shape.coneAngleDegrees 0
effect.emitter[0].spawn.shape.surfaceOnly false
effect.emitter[0].spawn.shape.upperHalfOnly false
effect.emitter[0].spawn.shape.lineStart -2 0.200000003 0
effect.emitter[0].spawn.shape.lineEnd 2 0.200000003 0
effect.emitter[0].spawn.shape.spiralArms 1
effect.emitter[0].spawn.shape.spiralTwist 0
effect.emitter[0].spawn.shape.spiralThickness 0
effect.emitter[0].moduleCount 2
effect.emitter[0].module[0].id 1
effect.emitter[0].module[0].authoringOrder 0
effect.emitter[0].module[0].type ColorOverLife
effect.emitter[0].module[0].enabled true
effect.emitter[0].module[0].payload.gradient.stopCount 3
effect.emitter[0].module[0].payload.gradient.stop[0].time 0
effect.emitter[0].module[0].payload.gradient.stop[0].rgb 0.879999995 0.949999988 0.970000029
effect.emitter[0].module[0].payload.gradient.stop[0].alpha 0.550000012
effect.emitter[0].module[0].payload.gradient.stop[1].time 0.449999988
effect.emitter[0].module[0].payload.gradient.stop[1].rgb 0.899999976 0.959999979 0.980000019
effect.emitter[0].module[0].payload.gradient.stop[1].alpha 0.319999993
effect.emitter[0].module[0].payload.gradient.stop[2].time 1
effect.emitter[0].module[0].payload.gradient.stop[2].rgb 0.949999988 0.980000019 0.99000001
effect.emitter[0].module[0].payload.gradient.stop[2].alpha 0
effect.emitter[0].module[1].id 2
effect.emitter[0].module[1].authoringOrder 1
effect.emitter[0].module[1].type SizeOverLife
effect.emitter[0].module[1].enabled true
effect.emitter[0].module[1].payload.curve.keyCount 2
effect.emitter[0].module[1].payload.curve.key[0].time 0
effect.emitter[0].module[1].payload.curve.key[0].value 1
effect.emitter[0].module[1].payload.curve.key[0].easing Linear
effect.emitter[0].module[1].payload.curve.key[1].time 1
effect.emitter[0].module[1].payload.curve.key[1].value 3.20000005
effect.emitter[0].module[1].payload.curve.key[1].easing Linear
effect.emitter[0].output.type Billboard
effect.emitter[0].output.material.assetId 0
effect.emitter[0].output.material.path "/Game/Materials/RiverMistMaterial.kbmat"
effect.emitter[0].output.mesh.assetId 0
effect.emitter[0].output.mesh.path ""
effect.emitter[0].output.textureAtlas.assetId 0
effect.emitter[0].output.textureAtlas.path ""
effect.emitter[0].output.blend Alpha
effect.emitter[0].output.sort BackToFront
effect.emitter[0].output.depthTest true
effect.emitter[0].output.depthWrite false
effect.emitter[0].output.softParticles true
effect.emitter[0].output.antiAliasing true
effect.emitter[0].output.alignment CameraFacing
effect.emitter[0].output.payload.flipbook.columns 1
effect.emitter[0].output.payload.flipbook.rows 1
effect.emitter[0].output.payload.flipbook.framesPerSecond 0
effect.emitter[0].output.payload.flipbook.looping true
effect.eventBindingCount 0
)KBMAT";

constexpr std::string_view kMistMaterial = R"KBMAT(# KB material
version 1
materialType builtin.pbr
materialTypeVersion 1
baseColor 0.814846694 0.896269381 0.921582043 1
metallicFactor 0
roughnessFactor 0.379999995
normalScale 1
occlusionStrength 1
emissiveColor 0.623960435 0.768151283 0.822785854
emissiveStrength 0.899999976
alphaMode BLEND
alphaCutoff 0.5
translucencyBlend ALPHA
doubleSided true
writesDepth false
tiling 1 1
offset 0 0
clearcoatFactor 0
clearcoatRoughnessFactor 0
sheenColor 0 0 0
sheenRoughnessFactor 0
transmissionFactor 0
thicknessFactor 0
attenuationColor 1 1 1
attenuationDistance 0
subsurfaceColor 1 1 1
subsurfaceFactor 0
anisotropyStrength 0
anisotropyRotation 0
layerWeight 1
decalBlendMode DISABLED
layerBlendMode REPLACE
graphVersion 3
graphMaterialDomain surface
graphShadingModel defaultLit
graphBlendMode opaque
graphStorageModel inline-kbmat
graphDiagnosticSchemaVersion 1
graphPersistCompileDiagnostics true
graphArtifactFailurePolicy LastGoodThenErrorMaterial
graphNode 1 MaterialOutput 640 240
)KBMAT";

[[nodiscard]] bool EnsureFile(const std::filesystem::path& file, std::string_view text, std::string& error) {
    std::error_code existsError;
    if (std::filesystem::exists(file, existsError)) return true;
    std::ofstream output{ file, std::ios::binary };
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    if (!output) {
        error = "River water file could not be written: " + file.generic_string();
        return false;
    }
    return true;
}

} // namespace

bool EnsureRiverWaterMaterials(const std::filesystem::path& folder, RiverWaterMaterials& files, std::string& error) {
    std::error_code directoryError;
    std::filesystem::create_directories(folder, directoryError);
    if (directoryError) {
        error = "The folder of the river water materials could not be created: " + folder.generic_string();
        return false;
    }
    files.stream = folder / "RiverWater.kbmat";
    files.waterfall = folder / "RiverWaterfall.kbmat";
    files.mist = folder / "RiverMist.kbvfx";
    return EnsureFile(files.stream, kStreamMaterial, error) && EnsureFile(files.waterfall, kWaterfallMaterial, error) &&
        EnsureFile(folder / "RiverMistMaterial.kbmat", kMistMaterial, error) && EnsureFile(files.mist, kMistEffect, error);
}

} // namespace kb::terrain_editor
