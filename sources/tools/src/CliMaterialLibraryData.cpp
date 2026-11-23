#include "CliMaterialLibrary.hpp"

// The data of the material library: the masters (graph text) and the presets (a master and its parameter values). Adding a
// look is one row in kPresets; the tests compile every master, build every preset and check that no two rows share their words.
namespace kb::cli::material {
namespace {

constexpr MaterialMaster kMasters[]{
    { "Pbr", R"graph(Color = ParameterColor #808080
Metallic = ParameterScalar 0
Roughness = ParameterScalar 0.5
Glow = ParameterColor #000000
GlowStrength = ParameterScalar 1
Output.BaseColor = Color
Output.Metallic = Metallic
Output.Roughness = Roughness
Output.Emissive = Multiply(Glow, GlowStrength)
)graph" },
    { "Mottled", R"graph(Color = ParameterColor #8A8A8A
Color2 = ParameterColor #5A5A5A
Tiling = ParameterVector "6 6 1"
Contrast = ParameterScalar 1.5
Roughness = ParameterScalar 0.8
Roughness2 = ParameterScalar 0.6
Metallic = ParameterScalar 0
Grain = Noise "value false 1 1 2 2 0 1"(value=Multiply(UV, Tiling))
Mask = Saturate(Power(base=Grain, exponent=Contrast))
Output.BaseColor = Lerp(Color, Color2, Mask)
Output.Roughness = Lerp(Roughness, Roughness2, Mask)
Output.Metallic = Metallic
)graph" },
    { "Rusted", R"graph(Color = ParameterColor #8C8F94
RustColor = ParameterColor #8B4A2B
Tiling = ParameterVector "5 5 1"
Amount = ParameterScalar 0.5
Roughness = ParameterScalar 0.35
RustRoughness = ParameterScalar 0.9
Patches = Noise "value false 3 1 2 2 0 1"(value=Multiply(UV, Tiling))
Mask = SmoothStep(min=Amount, max=Add(Amount, 0.2), value=Patches)
Output.BaseColor = Lerp(Color, RustColor, Mask)
Output.Roughness = Lerp(Roughness, RustRoughness, Mask)
Output.Metallic = Subtract(1, Mask)
)graph" },
    { "Speckled", R"graph(Color = ParameterColor #7A7468
SpeckleColor = ParameterColor #2B2825
Tiling = ParameterScalar 40
Amount = ParameterScalar 0.7
Roughness = ParameterScalar 0.85
Metallic = ParameterScalar 0
Speck = Noise "value false 1 1 1 2 0 1"(value=Multiply(UV, Tiling))
Mask = SmoothStep(min=Amount, max=Add(Amount, 0.08), value=Speck)
Output.BaseColor = Lerp(Color, SpeckleColor, Mask)
Output.Roughness = Roughness
Output.Metallic = Metallic
)graph" },
    { "Checker", R"graph(Color = ParameterColor #E8E8E8
Color2 = ParameterColor #202020
Frequency = ParameterVector "8 8 1"
Roughness = ParameterScalar 0.3
Metallic = ParameterScalar 0
Output.BaseColor = Checkerboard(uv=UV, colorA=Color, colorB=Color2, frequency=Frequency)
Output.Roughness = Roughness
Output.Metallic = Metallic
)graph" },
    { "Stripes", R"graph(Color = ParameterColor #D8D8D8
Color2 = ParameterColor #2A4F9E
Direction = ParameterVector "1 0 0"
Count = ParameterScalar 10
Roughness = ParameterScalar 0.7
Metallic = ParameterScalar 0
Phase = Fraction(Multiply(DotProduct(UV, Direction), Count))
Output.BaseColor = Lerp(Color, Color2, Step(0.5, Phase))
Output.Roughness = Roughness
Output.Metallic = Metallic
)graph" },
    { "Emissive", R"graph(Color = ParameterColor #101010
Glow = ParameterColor #FF2D95
Intensity = ParameterScalar 4
PulseSpeed = ParameterScalar 0
PulseDepth = ParameterScalar 0
Roughness = ParameterScalar 0.5
Wave = Multiply(Add(Sine(Multiply(Time, PulseSpeed)), 1), 0.5)
Output.BaseColor = Color
Output.Roughness = Roughness
Output.Emissive = Multiply(Multiply(Glow, Intensity), Lerp(1, Wave, PulseDepth))
)graph" },
    { "Lava", R"graph(Color = ParameterColor #1A0A05
Glow = ParameterColor #FF5A10
Intensity = ParameterScalar 3
Speed = ParameterScalar 0.05
Tiling = ParameterScalar 4
Flow = Add(Multiply(UV, Tiling), Multiply(Time, Speed))
Heat = SmoothStep(0.35, 0.7, Noise "simplex false 1 1 2 2 0 1"(value=Flow))
Output.BaseColor = Lerp(Color, Multiply(Glow, 0.3), Heat)
Output.Roughness = 0.9
Output.Emissive = Multiply(Multiply(Glow, Intensity), Heat)
)graph" },
    { "Toon", R"graph(Shading Model = unlit
Color = ParameterColor #E04040
ShadowColor = ParameterColor #7A1E3A
Threshold = ParameterScalar 0.3
Softness = ParameterScalar 0.03
RimColor = ParameterColor #FFFFFF
RimAmount = ParameterScalar 0
OutlineColor = ParameterColor #101010
OutlineWidth = ParameterScalar 0.1
Facing = DotProduct(PixelNormalWS, LightVector)
Lit = SmoothStep(Subtract(Threshold, Softness), Add(Threshold, Softness), Facing)
Rim = Fresnel "3 0"
Edge = Step(Subtract(1, OutlineWidth), Fresnel "1 0")
Shaded = Add(Lerp(ShadowColor, Color, Lit), Multiply(RimColor, Multiply(Rim, RimAmount)))
Output.BaseColor = Lerp(Shaded, OutlineColor, Edge)
)graph" },
    { "Glass", R"graph(Blend Mode = translucent
Two Sided = true
Color = ParameterColor #CFE8FF
Opacity = ParameterScalar 0.15
EdgeOpacity = ParameterScalar 0.8
Roughness = ParameterScalar 0.02
Output.BaseColor = Color
Output.Roughness = Roughness
Output.Alpha = Lerp(Opacity, EdgeOpacity, Fresnel "3 0.04")
Output.Specular = 1
)graph" },
    { "Water", R"graph(Blend Mode = translucent
Color = ParameterColor #3FA7C8
Color2 = ParameterColor #0B3C5D
Speed = ParameterScalar 0.03
Tiling = ParameterScalar 6
Opacity = ParameterScalar 0.75
Roughness = ParameterScalar 0.05
Flow = Add(Multiply(UV, Tiling), Multiply(Time, Speed))
Ripple = Noise "gradient false 1 1 2 2 0 1"(value=Flow)
Output.BaseColor = Lerp(Color, Color2, Ripple)
Output.Alpha = Opacity
Output.Roughness = Roughness
Output.Specular = 1
)graph" },
    { "Hologram", R"graph(Shading Model = unlit
Blend Mode = additive
Two Sided = true
Color = ParameterColor #29E3FF
Intensity = ParameterScalar 3
LineDensity = ParameterScalar 40
ScrollSpeed = ParameterScalar 0.5
Flicker = ParameterScalar 0.1
Edge = Fresnel "2 0"
Lines = Step(0.5, Fraction(Add(Multiply(BreakVector(WorldPosition).y, LineDensity), Multiply(Time, ScrollSpeed))))
Flick = Add(1, Multiply(Sine(Multiply(Time, 40)), Flicker))
Output.BaseColor = Multiply(Multiply(Color, Intensity), Multiply(Add(Multiply(Edge, 2), Multiply(Lines, 0.5)), Flick))
)graph" },
    { "Dissolve", R"graph(Blend Mode = masked
Two Sided = true
Color = ParameterColor #8C6B4A
EdgeColor = ParameterColor #FF6A00
Amount = ParameterScalar 0.4
EdgeWidth = ParameterScalar 0.08
Tiling = ParameterScalar 6
Intensity = ParameterScalar 6
Roughness = ParameterScalar 0.7
Pattern = Noise "simplex false 1 1 2 2 0 1"(value=Multiply(UV, Tiling))
Alive = Step(Amount, Pattern)
Edge = Multiply(Alive, OneMinus(Step(Add(Amount, EdgeWidth), Pattern)))
Output.BaseColor = Color
Output.Roughness = Roughness
Output.Emissive = Multiply(Multiply(EdgeColor, Intensity), Edge)
Output.Alpha = Alive
Output.AlphaClipThreshold = 0.5
)graph" },
    { "Rim", R"graph(Color = ParameterColor #3A4A66
RimColor = ParameterColor #66CCFF
RimPower = ParameterScalar 3
RimIntensity = ParameterScalar 2
Roughness = ParameterScalar 0.5
Metallic = ParameterScalar 0
Output.BaseColor = Color
Output.Roughness = Roughness
Output.Metallic = Metallic
Output.Emissive = Multiply(Multiply(RimColor, RimIntensity), Fresnel(exponent=RimPower))
)graph" },
    { "Scroll", R"graph(Color = ParameterColor #101820
Glow = ParameterColor #2EE6FF
Direction = ParameterVector "1 0 0"
Tiling = ParameterScalar 8
Speed = ParameterScalar 0.5
Duty = ParameterScalar 0.5
Intensity = ParameterScalar 3
Phase = Fraction(Subtract(Multiply(DotProduct(UV, Direction), Tiling), Multiply(Time, Speed)))
Output.BaseColor = Color
Output.Roughness = 0.5
Output.Emissive = Multiply(Multiply(Glow, Intensity), Step(Duty, Phase))
)graph" },
    { "Wobble", R"graph(Max World Position Offset Displacement = 0.5
Color = ParameterColor #6BD66B
Roughness = ParameterScalar 0.3
Amplitude = ParameterScalar 0.05
Frequency = ParameterScalar 6
Speed = ParameterScalar 3
Phase = Add(Multiply(Time, Speed), Multiply(DotProduct(LocalPosition, "1 1 1"), Frequency))
Output.BaseColor = Color
Output.Roughness = Roughness
Output.WorldPositionOffset = Multiply(VertexNormalWS, Multiply(Sine(Phase), Amplitude))
)graph" },
    { "Wind", R"graph(Max World Position Offset Displacement = 1
Two Sided = true
Color = ParameterColor #4C9A2A
Color2 = ParameterColor #A6D854
Strength = ParameterScalar 0.15
Speed = ParameterScalar 1.5
Frequency = ParameterScalar 0.5
Roughness = ParameterScalar 0.8
Height = Saturate(BreakVector(LocalPosition).y)
Sway = Multiply(Sine(Add(Multiply(Time, Speed), Multiply(BreakVector(WorldPosition).x, Frequency))), Multiply(Strength, Multiply(Height, Height)))
Output.BaseColor = Lerp(Color, Color2, Height)
Output.Roughness = Roughness
Output.WorldPositionOffset = MakeVector(x=Sway, y=0, z=Multiply(Sway, 0.5))
)graph" },
    { "UiGradient", R"graph(Domain = userInterface
Blend Mode = translucent
ColorTop = ParameterColor #3A7BD5
ColorBottom = ParameterColor #1B2A49
Opacity = ParameterScalar 1
Output.Emissive = Lerp(ColorTop, ColorBottom, Saturate(BreakVector(UV).y))
Output.Alpha = Opacity
)graph" },
    { "PostGrade", R"graph(Domain = postProcess
Tint = ParameterColor #FFFFFF
Saturation = ParameterScalar 1
Brightness = ParameterScalar 1
Vignette = ParameterScalar 0
Graded = Multiply(Multiply(Lerp(SceneColor, DotProduct(SceneColor, "0.3 0.59 0.11"), OneMinus(Saturation)), Tint), Brightness)
Dark = Saturate(OneMinus(Multiply(SmoothStep(0.35, 0.85, Distance(ScreenPosition, "0.5 0.5 0")), Vignette)))
Output.Emissive = Multiply(Graded, Dark)
)graph" },
    { "Flipbook", R"graph(Shading Model = unlit
Blend Mode = additive
Sheet = ParameterTexture none
Color = ParameterColor #FFFFFF
Rows = ParameterScalar 4
Columns = ParameterScalar 4
Speed = ParameterScalar 8
Intensity = ParameterScalar 1
Cell = FlipBook(animationPhase=Multiply(Time, Speed), numberOfRows=Rows, numberOfColumns=Columns, uv=UV)
Output.BaseColor = Multiply(Multiply(TextureSample(texture=Sheet, uv=Cell), Color), Multiply(Intensity, Ellipse(uv=UV)))
)graph" },
    { "SnowTop", R"graph(Color = ParameterColor #5A5A58
SnowColor = ParameterColor #F4F8FB
Amount = ParameterScalar 0.5
Roughness = ParameterScalar 0.85
Facing = Saturate(BreakVector(VertexNormalWS).y)
Cap = SmoothStep(Amount, Add(Amount, 0.15), Facing)
Output.BaseColor = Lerp(Color, SnowColor, Cap)
Output.Roughness = Lerp(Roughness, 0.6, Cap)
)graph" },
    { "Glitch", R"graph(Shading Model = unlit
Color = ParameterColor #101820
GlitchColor = ParameterColor #19F0FF
Density = ParameterScalar 12
Rate = ParameterScalar 8
Threshold = ParameterScalar 0.8
Block = Floor(Add(Multiply(BreakVector(WorldPosition).y, Density), Floor(Multiply(Time, Rate))))
Rand = Fraction(Multiply(Sine(Multiply(Block, 12.9898)), 43758.5453))
Output.BaseColor = Lerp(Color, Multiply(GlitchColor, 2), Step(Threshold, Rand))
)graph" },
    { "Terrain", R"graph(Sand = ParameterColor #D9C28F
Grass = ParameterColor #4E8A2E
Dirt = ParameterColor #6B4A2E
Rock = ParameterColor #7A7873
Snow = ParameterColor #F4F8FB
Heights = ParameterVector "1 4 8"
HeightTop = ParameterScalar 12
Blend = ParameterScalar 0.12
SlopeRock = ParameterScalar 0.6
Tiling = ParameterScalar 0.15
Where = BreakVector(WorldPosition)
Band = BreakVector(Heights)
Wobble = Noise "value false 1 1 2 2 -1 1"(value=Multiply(WorldPosition, Tiling))
Break = Noise "value false 1 1 1 2 0 1"(value=Multiply(WorldPosition, 0.03))
Level = Add(Where.y, Multiply(Wobble, Multiply(Blend, 20)))
ToGrass = SmoothStep(Band.x, Add(Band.x, 1.5), Level)
ToDirt = SmoothStep(Band.y, Add(Band.y, 2), Level)
ToRock = SmoothStep(Band.z, Add(Band.z, 2), Level)
ToSnow = SmoothStep(HeightTop, Add(HeightTop, 2), Level)
Steep = SmoothStep(SlopeRock, Add(SlopeRock, 0.15), OneMinus(Saturate(BreakVector(VertexNormalWS).y)))
Low = Lerp(Lerp(Sand, Grass, ToGrass), Dirt, ToDirt)
High = Lerp(Lerp(Low, Rock, ToRock), Snow, ToSnow)
Varied = Lerp(High, Multiply(High, 0.8), Break)
Output.BaseColor = Lerp(Varied, Multiply(Rock, Lerp(1, 0.7, Break)), Steep)
Output.Roughness = 0.9
)graph" },
    { "Wet", R"graph(Color = ParameterColor #5A5A5A
Wetness = ParameterScalar 0.8
Roughness = ParameterScalar 0.85
Tiling = ParameterScalar 5
Puddles = SmoothStep(0.45, 0.6, Noise "value false 1 1 2 2 0 1"(value=Multiply(UV, Tiling)))
Wet = Multiply(Puddles, Wetness)
Output.BaseColor = Multiply(Color, Lerp(1, 0.55, Wet))
Output.Roughness = Lerp(Roughness, 0.04, Wet)
)graph" },
};

// name, family, master, the words a customer says, the parameters it sets ("Name=value|Name=value").
constexpr MaterialPreset kPresets[]{
    // Metals
    { "gold", "metal", "Pbr", "gold golden yellow precious jewelry coin treasure shiny", "Color=#D4AF37|Metallic=1|Roughness=0.25" },
    { "silver", "metal", "Pbr", "silver grey precious cutlery trophy shiny", "Color=#C8C8CC|Metallic=1|Roughness=0.2" },
    { "copper", "metal", "Pbr", "copper orange red pipe wire pan", "Color=#B87333|Metallic=1|Roughness=0.3" },
    { "bronze", "metal", "Pbr", "bronze statue antique medal brown", "Color=#8C6A3F|Metallic=1|Roughness=0.4" },
    { "brass", "metal", "Pbr", "brass yellow fittings instrument handle", "Color=#B5A642|Metallic=1|Roughness=0.35" },
    { "rusty-metal", "metal", "Rusted", "rust rusty rusted corroded oxidized metal old weathered crate barrel pipe", "Amount=0.5" },
    { "rusty-iron", "metal", "Rusted", "rust rusty iron gate fence nail chain worn heavily eaten", "Color=#4A4A4D|RustColor=#7A3D1E|Amount=0.3|Tiling=8 8 1" },
    { "iron", "metal", "Pbr", "iron cast dark armor armour knight", "Color=#5C5C5F|Metallic=1|Roughness=0.55" },
    { "steel", "metal", "Pbr", "steel industrial blade sword armor armour", "Color=#A6A9AD|Metallic=1|Roughness=0.3" },
    { "chrome", "metal", "Pbr", "chrome mirror polished bumper reflective", "Color=#E8E8EA|Metallic=1|Roughness=0.3" },
    { "aluminium", "metal", "Pbr", "aluminium aluminum light can foil", "Color=#BFC1C4|Metallic=1|Roughness=0.45" },
    { "rose-gold", "metal", "Pbr", "rose gold pink jewelry ring", "Color=#B76E79|Metallic=1|Roughness=0.25" },
    { "gunmetal", "metal", "Pbr", "gunmetal weapon gun dark grey", "Color=#2C3539|Metallic=1|Roughness=0.4" },
    { "brushed-steel", "metal", "Pbr", "brushed satin appliance fridge stainless", "Color=#9EA2A6|Metallic=1|Roughness=0.6" },
    { "black-metal", "metal", "Pbr", "black matte dark metal", "Color=#1C1C1E|Metallic=1|Roughness=0.35" },
    // Plastic, rubber, paint, ceramic
    { "plastic-red", "plastic", "Pbr", "plastic red toy glossy", "Color=#D42B2B|Metallic=0|Roughness=0.35" },
    { "plastic-blue", "plastic", "Pbr", "plastic blue toy glossy", "Color=#2B5BD4|Metallic=0|Roughness=0.35" },
    { "plastic-green", "plastic", "Pbr", "plastic green toy glossy", "Color=#2BA84A|Metallic=0|Roughness=0.35" },
    { "plastic-yellow", "plastic", "Pbr", "plastic yellow toy glossy bricks", "Color=#F2C82E|Metallic=0|Roughness=0.35" },
    { "plastic-white", "plastic", "Pbr", "plastic white appliance casing", "Color=#F2F2F2|Metallic=0|Roughness=0.4" },
    { "plastic-black", "plastic", "Pbr", "plastic black casing electronics", "Color=#1A1A1A|Metallic=0|Roughness=0.4" },
    { "rubber-black", "plastic", "Pbr", "rubber black tyre tire grip", "Color=#151515|Metallic=0|Roughness=0.92" },
    { "rubber-red", "plastic", "Pbr", "rubber red ball soft", "Color=#8E1B1B|Metallic=0|Roughness=0.9" },
    { "ceramic-white", "plastic", "Pbr", "ceramic white porcelain cup plate glazed", "Color=#F4F1EA|Metallic=0|Roughness=0.15" },
    { "car-paint-red", "plastic", "Pbr", "car paint red sports racing lacquer", "Color=#B01010|Metallic=0.35|Roughness=0.12" },
    { "car-paint-blue", "plastic", "Pbr", "car paint blue metallic pearl lacquer", "Color=#0F3E91|Metallic=0.35|Roughness=0.12" },
    { "terracotta", "plastic", "Pbr", "terracotta clay pot orange fired", "Color=#B5653A|Metallic=0|Roughness=0.9" },
    { "chalk", "plastic", "Pbr", "chalk white matte powder", "Color=#ECECE6|Metallic=0|Roughness=1" },
    { "matte-black", "plastic", "Pbr", "matte black flat dark paint", "Color=#0E0E0E|Metallic=0|Roughness=0.95" },
    // Wood
    { "oak", "wood", "Mottled", "wood oak timber furniture floor planks light", "Color=#B58B54|Color2=#8F6A3A|Tiling=3 36 1|Contrast=1.2|Roughness=0.6|Roughness2=0.5" },
    { "pine", "wood", "Mottled", "wood pine timber cabin pale knotty", "Color=#D9B77C|Color2=#B88A4E|Tiling=3 30 1|Contrast=1.4|Roughness=0.65|Roughness2=0.55" },
    { "walnut", "wood", "Mottled", "wood walnut dark brown furniture desk", "Color=#5B3A24|Color2=#3B2314|Tiling=3 40 1|Contrast=1.2|Roughness=0.5|Roughness2=0.4" },
    { "mahogany", "wood", "Mottled", "wood mahogany red brown antique cabinet", "Color=#7A3B22|Color2=#4D2013|Tiling=3 40 1|Contrast=1.3|Roughness=0.45|Roughness2=0.35" },
    { "birch", "wood", "Mottled", "wood birch pale white scandinavian", "Color=#E6D3AE|Color2=#C9AD7C|Tiling=3 28 1|Contrast=1.5|Roughness=0.6|Roughness2=0.5" },
    { "cherry-wood", "wood", "Mottled", "wood cherry reddish furniture warm", "Color=#A5532F|Color2=#7C3820|Tiling=3 38 1|Contrast=1.2|Roughness=0.5|Roughness2=0.4" },
    { "bamboo", "wood", "Mottled", "wood bamboo stalk green yellow floor", "Color=#D8C27A|Color2=#B59C4A|Tiling=2 12 1|Contrast=1.6|Roughness=0.55|Roughness2=0.45" },
    { "dark-wood", "wood", "Mottled", "wood dark ebony black coffin", "Color=#3A2A1E|Color2=#241811|Tiling=3 36 1|Contrast=1.3|Roughness=0.5|Roughness2=0.4" },
    { "old-wood", "wood", "Mottled", "wood old weathered rotten barn fence worn", "Color=#7B6A55|Color2=#4F4234|Tiling=4 30 1|Contrast=2|Roughness=0.92|Roughness2=0.8" },
    { "driftwood", "wood", "Mottled", "wood driftwood beach bleached grey", "Color=#A59C8F|Color2=#7E7468|Tiling=3 24 1|Contrast=1.8|Roughness=0.9|Roughness2=0.8" },
    // Stone and building
    { "marble-white", "stone", "Mottled", "marble white stone polished veins statue", "Color=#EDEDED|Color2=#B9BDC4|Tiling=3 3 1|Contrast=3|Roughness=0.12|Roughness2=0.2" },
    { "marble-black", "stone", "Mottled", "marble black stone polished veins luxury", "Color=#1D1D20|Color2=#6A6A72|Tiling=3 3 1|Contrast=3|Roughness=0.12|Roughness2=0.2" },
    { "sandstone", "stone", "Mottled", "sandstone sand stone desert wall ruins", "Color=#D2B48C|Color2=#B8946A|Tiling=6 14 1|Contrast=1.5|Roughness=0.9|Roughness2=0.85" },
    { "limestone", "stone", "Mottled", "limestone pale stone cliff wall", "Color=#D8D2C2|Color2=#BDB59F|Tiling=5 5 1|Contrast=1.6|Roughness=0.9|Roughness2=0.85" },
    { "slate", "stone", "Mottled", "slate dark grey roof tile stone layered", "Color=#4A5360|Color2=#2F353D|Tiling=4 12 1|Contrast=1.4|Roughness=0.7|Roughness2=0.6" },
    { "basalt", "stone", "Mottled", "basalt black volcanic stone rock", "Color=#2D2F31|Color2=#1B1C1E|Tiling=5 5 1|Contrast=1.5|Roughness=0.75|Roughness2=0.65" },
    { "cobblestone", "stone", "Mottled", "cobblestone street path paving rough", "Color=#7B7B78|Color2=#55544F|Tiling=14 14 1|Contrast=2.2|Roughness=0.9|Roughness2=0.85" },
    { "brick-red", "stone", "Mottled", "brick red wall house building", "Color=#A5472E|Color2=#7A3220|Tiling=10 6 1|Contrast=2|Roughness=0.9|Roughness2=0.85" },
    { "concrete", "stone", "Mottled", "concrete grey wall floor urban cement rough", "Color=#9A9A98|Color2=#7F7F7C|Tiling=8 8 1|Contrast=1.6|Roughness=0.92|Roughness2=0.85" },
    { "plaster", "stone", "Mottled", "plaster white wall room painted interior", "Color=#E8E2D6|Color2=#D2CABA|Tiling=6 6 1|Contrast=1.2|Roughness=0.95|Roughness2=0.9" },
    { "adobe", "stone", "Mottled", "adobe clay mud brick desert hut", "Color=#C58C5C|Color2=#A66C40|Tiling=5 5 1|Contrast=1.5|Roughness=0.95|Roughness2=0.9" },
    { "lava-rock", "stone", "Mottled", "lava rock volcanic black porous", "Color=#2A2523|Color2=#463C38|Tiling=9 9 1|Contrast=2.4|Roughness=0.95|Roughness2=0.8" },
    // Ground
    { "grass", "ground", "Mottled", "grass lawn meadow green field turf", "Color=#4E8A2E|Color2=#2F5E1B|Tiling=30 30 1|Contrast=1.4|Roughness=0.95|Roughness2=0.9" },
    { "dry-grass", "ground", "Mottled", "dry grass savanna straw hay yellow", "Color=#B2A558|Color2=#8A7D3A|Tiling=26 26 1|Contrast=1.4|Roughness=0.95|Roughness2=0.9" },
    { "moss", "ground", "Mottled", "moss green forest damp lichen", "Color=#4C6B2A|Color2=#2E4719|Tiling=18 18 1|Contrast=1.8|Roughness=1|Roughness2=0.9" },
    { "dirt", "ground", "Mottled", "dirt soil earth brown path garden", "Color=#5A3F2B|Color2=#3E2A1B|Tiling=14 14 1|Contrast=1.6|Roughness=0.95|Roughness2=0.9" },
    { "mud", "ground", "Mottled", "mud wet swamp puddle brown sticky", "Color=#4A3426|Color2=#2F2018|Tiling=8 8 1|Contrast=1.8|Roughness=0.35|Roughness2=0.8" },
    { "sand", "ground", "Mottled", "sand beach dune yellow", "Color=#E1C79A|Color2=#CBAE7C|Tiling=40 40 1|Contrast=1.2|Roughness=0.98|Roughness2=0.95" },
    { "snow", "ground", "Mottled", "snow white winter cold frost", "Color=#F4F8FB|Color2=#D8E4EE|Tiling=16 16 1|Contrast=1.5|Roughness=0.7|Roughness2=0.55" },
    { "forest-floor", "ground", "Mottled", "forest floor leaves needles woodland litter", "Color=#3B2F1D|Color2=#2A3A18|Tiling=22 22 1|Contrast=2|Roughness=1|Roughness2=0.95" },
    { "gravel", "ground", "Speckled", "gravel pebbles stones driveway rocks", "Color=#8D8A84|SpeckleColor=#4F4C47|Tiling=70|Amount=0.55|Roughness=0.95" },
    { "terrain-alpine", "ground", "Terrain", "terrain mountain alpine landscape layers height slope blend sand grass rock snow world", "" },
    { "terrain-desert", "ground", "Terrain", "terrain desert dunes canyon landscape layers height slope sandstone", "Sand=#E3C28B|Grass=#C9A66B|Dirt=#A66C40|Rock=#8C6A4F|Snow=#E8D9BF|Heights=2 5 9|HeightTop=14" },
    { "terrain-volcanic", "ground", "Terrain", "terrain volcanic island ash lava landscape layers height slope black rock", "Sand=#2A2523|Grass=#3B3835|Dirt=#4A3A30|Rock=#5C5754|Snow=#8A8581|Heights=1 4 8|HeightTop=12" },
    // Fabric and leather
    { "denim", "fabric", "Mottled", "denim jeans blue cloth fabric", "Color=#2F4A7A|Color2=#243B63|Tiling=90 90 1|Contrast=1.4|Roughness=0.95|Roughness2=0.92" },
    { "linen", "fabric", "Mottled", "linen cloth fabric beige curtain cream", "Color=#D8CDB5|Color2=#C4B79C|Tiling=80 80 1|Contrast=1.3|Roughness=0.95|Roughness2=0.9" },
    { "burlap", "fabric", "Mottled", "burlap sack hessian rough fabric", "Color=#A98B5B|Color2=#826A41|Tiling=45 45 1|Contrast=1.8|Roughness=1|Roughness2=0.95" },
    { "wool", "fabric", "Mottled", "wool knit sweater fluffy fabric warm", "Color=#E4DDD0|Color2=#D1C9B8|Tiling=60 60 1|Contrast=1.6|Roughness=1|Roughness2=0.95" },
    { "velvet-red", "fabric", "Mottled", "velvet red couch curtain luxury fabric", "Color=#7A1022|Color2=#560A17|Tiling=40 40 1|Contrast=1.2|Roughness=0.75|Roughness2=0.6" },
    { "leather-brown", "fabric", "Mottled", "leather brown sofa bag boots", "Color=#6B3F22|Color2=#4F2C16|Tiling=40 40 1|Contrast=1.8|Roughness=0.55|Roughness2=0.45" },
    { "leather-black", "fabric", "Mottled", "leather black jacket seat", "Color=#1C1A19|Color2=#121110|Tiling=40 40 1|Contrast=1.8|Roughness=0.5|Roughness2=0.4" },
    { "canvas", "fabric", "Mottled", "canvas tent sail cotton painting", "Color=#BDB08A|Color2=#A39770|Tiling=70 70 1|Contrast=1.4|Roughness=0.95|Roughness2=0.9" },
    { "stripes-blue", "fabric", "Stripes", "stripes striped blue white shirt towel", "Color=#E8E8E8|Color2=#2A4F9E|Direction=1 0 0|Count=12" },
    { "stripes-red", "fabric", "Stripes", "stripes striped red white tent circus", "Color=#F2F2F2|Color2=#B22222|Direction=1 0 0|Count=10" },
    { "candy-stripes", "fabric", "Stripes", "candy cane stripes diagonal pink white sweet", "Color=#FFFFFF|Color2=#E0245E|Direction=1 1 0|Count=8" },
    { "hazard-stripes", "fabric", "Stripes", "hazard warning caution stripes yellow black danger", "Color=#F5C400|Color2=#151515|Direction=1 1 0|Count=10|Roughness=0.6" },
    { "zebra", "fabric", "Stripes", "zebra animal stripes black white fur", "Color=#F2F2F2|Color2=#151515|Direction=1 0.3 0|Count=8" },
    { "barber-pole", "fabric", "Stripes", "barber pole spiral red white stripes", "Color=#FFFFFF|Color2=#D8232A|Direction=0.5 1 0|Count=6|Roughness=0.3" },
    // Patterns and speckles
    { "tiles-black-white", "pattern", "Checker", "tiles checkerboard black white floor kitchen", "Color=#F2F2F2|Color2=#1A1A1A|Frequency=8 8 1|Roughness=0.2" },
    { "chessboard", "pattern", "Checker", "chessboard chess board game checker wood", "Color=#EDE6D0|Color2=#4A3B2A|Frequency=8 8 1|Roughness=0.4" },
    { "bathroom-tiles", "pattern", "Checker", "bathroom tiles ceramic wall pool clean blue", "Color=#E8F1F2|Color2=#B9D4DA|Frequency=16 16 1|Roughness=0.12" },
    { "kitchen-tiles", "pattern", "Checker", "kitchen tiles retro red cream floor", "Color=#F5E6C8|Color2=#C0392B|Frequency=10 10 1|Roughness=0.2" },
    { "grid-floor", "pattern", "Checker", "grid floor tech lab test dark squares", "Color=#3A3F47|Color2=#2B2F36|Frequency=24 24 1|Roughness=0.4" },
    { "carbon-fiber", "pattern", "Checker", "carbon fiber weave racing dark pattern", "Color=#202020|Color2=#0E0E0E|Frequency=60 60 1|Roughness=0.25" },
    { "picnic-cloth", "pattern", "Checker", "picnic cloth gingham tablecloth red white", "Color=#FFFFFF|Color2=#D33B3B|Frequency=12 12 1|Roughness=1" },
    { "granite", "pattern", "Speckled", "granite speckled counter stone kitchen flecks", "Color=#8E8E8A|SpeckleColor=#2E2E2E|Tiling=60|Amount=0.62|Roughness=0.3" },
    { "terrazzo", "pattern", "Speckled", "terrazzo chips floor mosaic modern", "Color=#E6E1D6|SpeckleColor=#B25A4A|Tiling=24|Amount=0.78|Roughness=0.25" },
    { "asphalt", "pattern", "Speckled", "asphalt road tarmac street black", "Color=#3A3A3C|SpeckleColor=#6A6A6C|Tiling=80|Amount=0.6|Roughness=0.95" },
    { "night-granite", "pattern", "Speckled", "granite black night sparkle flecks counter", "Color=#1F2328|SpeckleColor=#9AA3AD|Tiling=70|Amount=0.8|Roughness=0.25" },
    // Emissive and neon
    { "neon-pink", "emissive", "Emissive", "neon pink glow magenta sign nightclub", "Glow=#FF2D95|Intensity=4" },
    { "neon-blue", "emissive", "Emissive", "neon blue glow azure sign tube", "Glow=#2D8CFF|Intensity=4" },
    { "neon-green", "emissive", "Emissive", "neon green glow lime toxic sign", "Glow=#39FF14|Intensity=4" },
    { "neon-red", "emissive", "Emissive", "neon red glow crimson bar open", "Glow=#FF1E3C|Intensity=4" },
    { "neon-yellow", "emissive", "Emissive", "neon yellow glow lemon sign lightning", "Glow=#FFE81F|Intensity=4" },
    { "neon-purple", "emissive", "Emissive", "neon purple glow violet club cyber", "Glow=#B13DFF|Intensity=4" },
    { "neon-orange", "emissive", "Emissive", "neon orange glow amber sunset sign", "Glow=#FF7A1A|Intensity=4" },
    { "neon-cyan", "emissive", "Emissive", "neon cyan glow turquoise aqua tube", "Glow=#12F0F0|Intensity=4" },
    { "led-blue", "emissive", "Emissive", "led blue indicator light small panel", "Glow=#2D6BFF|Intensity=2|Roughness=0.3" },
    { "lamp-warm", "emissive", "Emissive", "lamp bulb warm light yellow lantern", "Color=#FFE9C0|Glow=#FFC878|Intensity=3|Roughness=0.2" },
    { "lamp-cold", "emissive", "Emissive", "lamp cold white light fluorescent tube", "Color=#E8F2FF|Glow=#CFE6FF|Intensity=3|Roughness=0.2" },
    { "alarm-light", "emissive", "Emissive", "alarm siren warning red blinking flashing pulse", "Glow=#FF2020|Intensity=5|PulseSpeed=6|PulseDepth=1" },
    { "breathing-blue", "emissive", "Emissive", "breathing pulse blue glow soft heartbeat idle", "Glow=#3D8BFF|Intensity=3|PulseSpeed=1.5|PulseDepth=0.6" },
    { "glow-stick", "emissive", "Emissive", "glow stick green rave party light stick", "Color=#1F5A1F|Glow=#7CFF4A|Intensity=3|Roughness=0.3" },
    // Lava, fire and energy
    { "lava", "effect", "Lava", "lava glows flows flowing molten volcano hot fire magma", "" },
    { "magma", "effect", "Lava", "magma deep hot red crust volcano", "Color=#2B0B04|Glow=#FF3B00|Intensity=4|Tiling=3" },
    { "hot-coal", "effect", "Lava", "coal embers hot barbecue glowing ash", "Color=#140A08|Glow=#E8480C|Speed=0.01|Tiling=10" },
    { "molten-gold", "effect", "Lava", "molten gold liquid treasure forge melted", "Color=#3A2A08|Glow=#FFC21A|Intensity=3.5|Speed=0.03" },
    { "radioactive-goo", "effect", "Lava", "radioactive goo toxic green slime acid nuclear", "Color=#0B2A08|Glow=#6BFF1A|Intensity=3|Speed=0.04" },
    { "conveyor", "effect", "Scroll", "conveyor belt moving yellow stripes factory", "Color=#1A1A1A|Glow=#F2C500|Direction=0 1 0|Tiling=12|Speed=0.6|Intensity=1.5" },
    { "energy-flow-blue", "effect", "Scroll", "energy flow blue scrolling lines power pipe", "" },
    { "energy-flow-green", "effect", "Scroll", "energy flow green scrolling lines slime tube", "Color=#06180B|Glow=#3DFF7A" },
    { "energy-flow-red", "effect", "Scroll", "energy flow red scrolling lines danger core", "Color=#1C0606|Glow=#FF3B3B|Speed=1" },
    { "matrix-code", "effect", "Scroll", "matrix code digital rain falling green hacker", "Color=#021006|Glow=#25FF55|Direction=0 1 0|Tiling=30|Speed=1|Duty=0.8|Intensity=2" },
    { "neon-strips", "effect", "Scroll", "neon strips running lights purple club floor", "Color=#12061F|Glow=#C03DFF|Direction=1 1 0|Tiling=6|Speed=0.8|Duty=0.7" },
    // Toon
    { "toon-red", "toon", "Toon", "toon cel cartoon red apple ruby", "" },
    { "toon-blue", "toon", "Toon", "toon cel cartoon blue sky ocean", "Color=#3F7FE0|ShadowColor=#1B2F6B" },
    { "toon-green", "toon", "Toon", "toon cel cartoon green leaf frog", "Color=#4CC04C|ShadowColor=#1F6B2E" },
    { "toon-yellow", "toon", "Toon", "toon cel cartoon yellow banana sun", "Color=#F2D13A|ShadowColor=#9E7A12" },
    { "toon-purple", "toon", "Toon", "toon cel cartoon purple grape wizard", "Color=#9B5BE0|ShadowColor=#4A2A7A" },
    { "toon-skin", "toon", "Toon", "toon cel cartoon skin character face peach", "Color=#F2C6A0|ShadowColor=#B4725A|OutlineWidth=0.08" },
    { "toon-ink", "toon", "Toon", "toon ink comic outline thick black white manga", "Color=@object:#F2F2F2|ShadowColor=@object*0.55:#BFC3CC|OutlineColor=#0A0A0A|OutlineWidth=0.35" },
    { "toon-rim-gold", "toon", "Toon", "toon rim light gold highlight glow hero stylized", "Color=@object:#E8C547|ShadowColor=@object*0.55:#8A5A12|RimColor=#FFF4C2|RimAmount=0.8" },
    { "stylized-low-poly", "toon", "Toon", "stylized low poly flat faceted soft shading", "Color=#6FB98F|ShadowColor=#3B7A5C|Threshold=0.45|Softness=0.25|OutlineWidth=0" },
    // Glass
    { "glass-clear", "glass", "Glass", "glass clear transparent window see-through pane", "" },
    { "glass-blue", "glass", "Glass", "glass blue tinted bottle sea window", "Color=#9CC8FF|Opacity=0.25" },
    { "glass-green", "glass", "Glass", "glass green bottle wine beer", "Color=#A3E6B0|Opacity=0.3" },
    { "glass-amber", "glass", "Glass", "glass amber orange bottle whiskey honey", "Color=#E0A040|Opacity=0.35" },
    { "frosted-glass", "glass", "Glass", "frosted glass bathroom blurred rough matte window", "Color=#E8F0F8|Opacity=0.55|EdgeOpacity=0.8|Roughness=0.5" },
    { "ice", "glass", "Glass", "ice frozen cold crystal blocks glacier", "Color=#CFEFFF|Opacity=0.45|EdgeOpacity=0.9|Roughness=0.1" },
    { "crystal-pink", "glass", "Glass", "crystal pink gem rose quartz magical", "Color=#FFB3E6|Opacity=0.4|EdgeOpacity=0.95|Roughness=0.03" },
    { "crystal-purple", "glass", "Glass", "crystal purple amethyst gem magic violet", "Color=#C9A0FF|Opacity=0.4|EdgeOpacity=0.95|Roughness=0.03" },
    // Water
    { "water-ocean", "water", "Water", "water ocean sea waves blue deep", "Color=#1E6FA8|Color2=#0A2E55|Speed=0.04|Tiling=5" },
    { "water-lake", "water", "Water", "water lake calm pond green blue", "Color=#3C8FA0|Color2=#17495A|Speed=0.02|Tiling=4|Opacity=0.8" },
    { "water-pool", "water", "Water", "water pool swimming turquoise clear bright", "Color=#4FD1E8|Color2=#1A9CC0|Opacity=0.6|Tiling=8" },
    { "water-swamp", "water", "Water", "water swamp bog murky green dirty", "Color=#4B6B3A|Color2=#24331A|Speed=0.01|Opacity=0.92|Roughness=0.2" },
    { "water-toxic", "water", "Water", "water toxic poison acid green waste", "Color=#7CFF3A|Color2=#1B6B10|Speed=0.03|Opacity=0.85" },
    { "water-river", "water", "Water", "water river stream flowing fast rapids", "Color=#6FA8B8|Color2=#3A6677|Speed=0.12|Tiling=10" },
    // Sci-fi
    { "hologram-blue", "scifi", "Hologram", "hologram blue scifi projection scanlines futuristic", "" },
    { "hologram-green", "scifi", "Hologram", "hologram green scifi terminal scanlines hacker", "Color=#39FF88" },
    { "hologram-red", "scifi", "Hologram", "hologram red scifi alert enemy scanlines", "Color=#FF3355" },
    { "hologram-orange", "scifi", "Hologram", "hologram orange amber scifi display scanlines", "Color=#FF9A2B" },
    { "force-field", "scifi", "Hologram", "force field shield barrier energy bubble forcefield", "Color=#5FB3FF|LineDensity=8|Flicker=0.3|Intensity=2" },
    { "shield-gold", "scifi", "Hologram", "shield golden magic aura ward protective", "Color=#FFD25F|LineDensity=6|Flicker=0.15|Intensity=2.5" },
    { "ghost", "scifi", "Hologram", "ghost spirit ethereal white spooky phantom", "Color=#CFE9FF|LineDensity=60|ScrollSpeed=0.2|Intensity=1.5" },
    { "rim-blue", "scifi", "Rim", "rim light blue edge glow outline cool highlight", "Color=@object:#3A4A66" },
    { "rim-orange", "scifi", "Rim", "rim light orange edge glow warm highlight", "Color=@object:#4A2B1A|RimColor=#FF9A3C" },
    { "rim-red", "scifi", "Rim", "rim light red edge glow danger enemy highlight", "Color=@object:#3A1A1A|RimColor=#FF4040|RimPower=2" },
    { "rim-green", "scifi", "Rim", "rim light green edge glow alien highlight", "Color=@object:#1C3A1C|RimColor=#6BFF6B" },
    { "rim-white", "scifi", "Rim", "rim light white edge glow selection highlight", "Color=@object:#2A2E36|RimColor=#FFFFFF|RimPower=4|RimIntensity=1.5" },
    // Effects
    { "dissolve-fire", "effect", "Dissolve", "dissolve burn burning fire disappear die death orange edge", "Color=@object:#8C6B4A" },
    { "dissolve-ice", "effect", "Dissolve", "dissolve ice frost shatter freeze fade blue edge", "Color=@object:#BFE6FF|EdgeColor=#6FD0FF" },
    { "dissolve-toxic", "effect", "Dissolve", "dissolve toxic acid melt green slime edge", "Color=@object:#3A5A2A|EdgeColor=#7CFF3A" },
    { "dissolve-void", "effect", "Dissolve", "dissolve void dark magic purple vanish teleport edge", "Color=@object:#1A1030|EdgeColor=#B13DFF" },
    { "dissolve-gold", "effect", "Dissolve", "dissolve golden sparkle spawn appear reveal edge", "Color=@object:#8C6B4A|EdgeColor=#FFD84A" },
    { "dissolve-ghost", "effect", "Dissolve", "dissolve ghost fade away pale spirit edge", "Color=@object:#E6EEF5|EdgeColor=#A0F0FF|EdgeWidth=0.12" },
    { "jelly", "effect", "Wobble", "jelly wobble wobbly bouncy gelatin green slime vertex", "" },
    { "slime-pink", "effect", "Wobble", "slime pink wobble gooey blob bubblegum", "Color=#FF7AC8|Roughness=0.1|Amplitude=0.08|Frequency=4|Speed=2" },
    { "breathing-skin", "effect", "Wobble", "breathing alive organic pulse body creature", "Color=#D98A7A|Roughness=0.5|Amplitude=0.03|Frequency=0.5|Speed=1.5" },
    { "grass-wind", "effect", "Wind", "grass wind sway blowing foliage meadow", "" },
    { "leaves-wind", "effect", "Wind", "leaves leaf wind sway tree bush rustling", "Color=#3E7B2B|Color2=#7DB840|Strength=0.2|Speed=2" },
    { "flag-red", "effect", "Wind", "flag red waving wind banner cloth", "Color=#B01E1E|Color2=#E24B4B|Strength=0.3|Frequency=2|Speed=3" },
    { "banner-blue", "effect", "Wind", "banner blue hanging cloth wind castle", "Color=#1F4E9E|Color2=#4F86D8|Strength=0.2|Frequency=1.2|Speed=1.5" },
    { "flipbook-4x4", "effect", "Flipbook", "flipbook sprite sheet animation frames 4x4 explosion smoke fire", "" },
    { "flipbook-8x8", "effect", "Flipbook", "flipbook sprite sheet animation frames 8x8 large detailed", "Rows=8|Columns=8|Speed=16" },
    { "flipbook-slow", "effect", "Flipbook", "flipbook sprite sheet slow 2x2 simple loop", "Rows=2|Columns=2|Speed=2" },
    // Techniques
    { "snow-on-rock", "effect", "SnowTop", "snow top cap covered rock mountain winter dusting upward faces", "" },
    { "snow-on-roof", "effect", "SnowTop", "snow roof house cabin winter covered shingles", "Color=#6B3A2A|Amount=0.35" },
    { "frosted-pine", "effect", "SnowTop", "frosted pine tree snowy needles winter forest", "Color=#2F5E2A|Amount=0.4|SnowColor=#E6F0F5" },
    { "glitch-cyan", "effect", "Glitch", "glitch cyan digital corrupted signal error screen bands", "" },
    { "glitch-magenta", "effect", "Glitch", "glitch magenta vhs static broken tv distortion bands", "GlitchColor=#FF2FD0|Rate=14|Threshold=0.7" },
    { "glitch-red", "effect", "Glitch", "glitch red alarm hacked virus damaged robot flicker", "Color=#200808|GlitchColor=#FF2A2A|Density=20|Threshold=0.85" },
    { "water-stylized", "water", "Water", "water stylized cartoon flat low poly pond bright simple", "Color=#5EC8E5|Color2=#2A8FBF|Speed=0.08|Tiling=3|Opacity=0.85|Roughness=0.25" },
    { "wet-asphalt", "effect", "Wet", "wet asphalt road rain puddles shiny street night", "Color=#2E2E30|Roughness=0.9" },
    { "wet-cobblestone", "effect", "Wet", "wet cobblestone rain puddles stone path damp", "Color=#6B6B68|Wetness=0.7|Tiling=8" },
    { "puddle-mud", "effect", "Wet", "puddle mud rain wet ground track soaked", "Color=#4A3426|Wetness=1|Tiling=3" },
    { "rainy-wood", "effect", "Wet", "rainy wet wood deck planks soaked damp", "Color=#6B4A2E|Wetness=0.6|Roughness=0.7" },
    // Interface and post-processing
    { "ui-gradient-blue", "ui", "UiGradient", "ui gradient blue panel button interface background", "" },
    { "ui-gradient-purple", "ui", "UiGradient", "ui gradient purple violet panel button interface", "ColorTop=#8E54E9|ColorBottom=#4776E6" },
    { "ui-gradient-sunset", "ui", "UiGradient", "ui gradient sunset orange pink warm banner interface", "ColorTop=#FF7E5F|ColorBottom=#FD3A84" },
    { "ui-gradient-dark", "ui", "UiGradient", "ui gradient dark grey menu panel interface night", "ColorTop=#2B2F3A|ColorBottom=#12141A" },
    { "ui-glass-panel", "ui", "UiGradient", "ui glass panel frosted translucent window overlay interface", "ColorTop=#FFFFFF|ColorBottom=#C8D6E5|Opacity=0.35" },
    { "ui-health-red", "ui", "UiGradient", "ui health bar red life hearts hp interface", "ColorTop=#FF5A5A|ColorBottom=#B01E1E" },
    { "post-sepia", "post", "PostGrade", "post sepia old photo vintage brown film screen", "Tint=#D8B88A|Saturation=0.2" },
    { "post-night-vision", "post", "PostGrade", "post night vision goggles green screen military", "Tint=#66FF66|Saturation=0|Brightness=1.4|Vignette=0.8" },
    { "post-grayscale", "post", "PostGrade", "post grayscale black white monochrome screen noir", "Saturation=0" },
    { "post-cold", "post", "PostGrade", "post cold blue tint winter screen freezing", "Tint=#B8D4FF" },
    { "post-warm", "post", "PostGrade", "post warm orange tint sunset screen cozy", "Tint=#FFE0B8" },
    { "post-vignette", "post", "PostGrade", "post vignette dark corners screen focus cinematic", "Vignette=0.8" },
    { "post-horror", "post", "PostGrade", "post horror scary dark desaturated vignette screen creepy", "Tint=#9FB89F|Saturation=0.3|Brightness=0.8|Vignette=0.9" },
    { "post-dream", "post", "PostGrade", "post dream soft pink bright saturated screen fantasy", "Tint=#FFD6F0|Saturation=1.2|Brightness=1.15" },
};

} // namespace

std::span<const MaterialMaster> MaterialMasters() noexcept {
    return kMasters;
}

std::span<const MaterialPreset> MaterialPresets() noexcept {
    return kPresets;
}

} // namespace kb::cli::material
