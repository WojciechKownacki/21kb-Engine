#pragma once
#include <string_view>
namespace kb::script {
inline constexpr std::string_view kLightingGuide = R"guide(# Local reflections, baked lightmaps and depth of field

C++ authors can supply up to two `SceneRenderReflectionProbe` entries through
`SceneRenderLightingConfig::ibl`. Use a linear cube texture, mip count, position,
box half extents or sphere radius and intensity. `parallaxCorrection` on both
the probe and IBL enables ray intersection with the bounds; `localProbeBlending`
blends overlapping influences with the global environment. Cooked probe cubes
accept RGBA8/BGRA8 sources (one mip or a full chain), preserving six faces in a
lossless resident `rgba8-cube` artifact; HDR and compressed cube inputs are rejected.
Invalid textures,
unsupported bounds and excess probes report ReflectionProbeUnavailable.

For persistent scene authoring, put `AmbientRadianceComponent` with mode
`CapturedEnvironment` and a cube `environmentAssetId` on an entity with an enabled
`RegionShapeComponent` (axis-aligned Box or Sphere). Transform supplies the center
and scale. Boxes with rotated world transforms are rejected. Without RegionShape,
CapturedEnvironment keeps its global meaning. The same components are available
to C++ Scene.Components(), Lua self:SetProperty on existing components and CLI:

```text
kb_cli component add --project . --entity LocalReflection --type "Ambient Radiance"
kb_cli component add --project . --entity LocalReflection --type RegionShape
kb_cli component set --project . --entity LocalReflection --type "Ambient Radiance" --set mode=4 --set environmentAssetId=/Game/Lighting/Room.dds
kb_cli component set --project . --entity LocalReflection --type RegionShape --set kind=3 --set size=10,6,10
```

Static diffuse lightmaps use a unique UV0 atlas. The CPU baker performs direct
point/directional lighting and hard shadow rays, pads islands, and returns sRGB
irradiance, with 0..4 diffuse bounces (`StaticLightmapSettings::bounces`, default 1)
that use the base colour of receivers and shadow casters. HDR encoding and
moving-object relighting are not provided. C++: include `engine/render/StaticLightmapBaker.hpp`,
fill world-space `LightmapTriangle` and `LightmapLight` spans, then call
`BakeStaticLightmap`. Check `Succeeded()` before saving `rgba`.
`GenerateLightmapUv` charts an unindexed triangle soup into a non-overlapping atlas.

The whole scene bakes with one command, or Build Lighting in the World menu of the editor:

```text
kb_cli scene-bake-lighting --project . --scene /Game/Scenes/Main.21kbscene --resolution 128 --bounces 1
```

Every Static mesh renderer is a receiver and every Static point/directional light
is used (static spot lights are skipped and reported). Each receiver gets a texture and
material under /Game/Baked/<scene>; a receiver whose second UV set is missing or
overlaps gets a generated lightmap UV mesh beside them, and its renderer is switched
to that mesh. Baking again replaces the previous result. The materials set
`bakedLightmapUv1`, so the second UV set is sampled.

The scene command uses static RGB point/directional lights and mesh shadow
casters, saves a TGA and material, then assigns it to the receiver:

```text
kb_cli scene-lightmap --project . --entity Floor --out /Game/Lighting/Floor.tga --resolution 128
```

The receiver must be opaque matte builtin PBR without normal/emission maps,
custom graphs or multiple material overrides. Atlas overlap and unsupported
geometry fail before outputs are written. Existing output assets are protected.
The generated material sets `RenderMaterialDesc::bakedLightmap = true`; its
emissive texture is diffuse irradiance multiplied by albedo, replacing dynamic
surface lighting. Ordinary texture/material dependencies carry the result into
cooked packages. Lights are retained for other receivers; they can be removed
after all required receivers are baked. Lua assigns the saved material through
`MeshRenderer.SetMaterial`, using the same renderer as native code.

Depth of field is authored in the scene's .kbppfx profile:

```text
kb_cli post-process-set --project . --profile /Game/PostProcess/Look.kbppfx --set depthOfFieldEnabled=1 --set depthOfFieldFocusDistance=6 --set depthOfFieldFocusRange=2 --set depthOfFieldMaxRadiusPixels=12
```

These fields also exist in public C++ `ScenePostProcessSettings`. Focus distance
and range are positive view-space metres; radius is 0..32 output pixels. The
screen-space disk filter uses scene depth, keeps sharp foreground edges out of
blurred backgrounds, and preserves alpha. It does not simulate bokeh blades or
scatter foreground blur across silhouettes. Real depth and inverse view-projection
are required; disable MSAA for depth of field. Save profiles with PostProcessProfileAssetLoader::SaveProfile; invalid
CLI edits leave the previous profile intact.
)guide";
}
