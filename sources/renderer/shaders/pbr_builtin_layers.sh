#ifndef KB_PBR_BUILTIN_LAYERS_SH
#define KB_PBR_BUILTIN_LAYERS_SH

SAMPLER2D(s_clearcoat, 7);
SAMPLER2D(s_clearcoatRoughness, 8);
uniform vec4 u_materialLayers; // coat weight, coat roughness, subsurface weight, reserved
uniform vec4 u_materialSubsurface; // scattering RGB, reserved

vec3 KbBuiltinLayers(vec2 uv)
{
    // glTF clearcoat channels: red intensity, green roughness; missing maps are white.
    return vec3(clamp(u_materialLayers.x * texture2D(s_clearcoat, uv).r, 0.0, 1.0),
                clamp(u_materialLayers.y * texture2D(s_clearcoatRoughness, uv).g, 0.02, 1.0),
                clamp(u_materialLayers.z, 0.0, 1.0));
}

#endif
