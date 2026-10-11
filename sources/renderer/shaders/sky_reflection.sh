#ifndef KB_SKY_REFLECTION_SH
#define KB_SKY_REFLECTION_SH

// Reflection of the surroundings (Unreal's SkyLight): the captured sky, prefiltered by roughness
// (SceneSkyReflection), when the scene has one; otherwise the environment colour the caller gives.
// The includer declares s_skyReflection, the prefiltered cube map, at its own sampler stage.
uniform vec4 u_skyReflectionParams; // x: 1 when the sky reflection is ready, y: its last mip

vec3 KbSkyReflection(vec3 direction, float roughness, vec3 fallback)
{
    return u_skyReflectionParams.x > 0.5
        ? textureCubeLod(s_skyReflection, direction, clamp(roughness, 0.0, 1.0) * u_skyReflectionParams.y).rgb
        : fallback;
}

#endif // KB_SKY_REFLECTION_SH
