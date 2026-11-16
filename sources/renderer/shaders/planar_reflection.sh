#ifndef KB_PLANAR_REFLECTION_SH
#define KB_PLANAR_REFLECTION_SH

#include "screen_uv.sh"

// A planar mirror (the Planar Reflection component): the scene drawn from the camera mirrored in a plane, kept as an HDR
// picture. A surface lying on the mirror's rectangle reflects the picture where it has data (the weight is 0
// elsewhere and while no picture is ready, so the caller's probes, screen-space reflections or sky stay). The
// includer sets KB_PLANAR_REFLECTION_DEFERRED when it is the deferred lighting shader, which has another free
// sampler stage.
#if defined(KB_PLANAR_REFLECTION_DEFERRED)
SAMPLER2D(s_planarReflection,15);
#else
SAMPLER2D(s_planarReflection,13);
#endif
uniform mat4 u_planarViewProj;
// xyz the unit normal of the mirrored side, w the mirror plane's offset along it.
uniform vec4 u_planarPlane;
uniform vec4 u_planarCenter;
// xyz a unit axis of the rectangle in the plane, w half the rectangle's size along it.
uniform vec4 u_planarRight;
uniform vec4 u_planarForward;
// x 1 while a picture is ready, yz 1 / picture size in texels.
uniform vec4 u_planarParams;

// How far (m) a surface may sit off the mirror plane and still count as the mirror.
#define KB_PLANAR_REFLECTION_THICKNESS 0.06

// rgb = reflected radiance, a = how much of it replaces the caller's reflection.
vec4 KbPlanarReflection(vec3 P, vec3 N, float roughness)
{
    if (u_planarParams.x < 0.5) return vec4(0.0, 0.0, 0.0, 0.0);
    vec3 delta = P - u_planarCenter.xyz;
    float height = dot(delta, u_planarPlane.xyz);
    float inside = step(abs(height), KB_PLANAR_REFLECTION_THICKNESS)
        * step(abs(dot(delta, u_planarRight.xyz)), u_planarRight.w)
        * step(abs(dot(delta, u_planarForward.xyz)), u_planarForward.w)
        * smoothstep(0.5, 0.8, dot(N, u_planarPlane.xyz));
    // Rough surfaces keep the usual reflection: the picture is sharp, so it only suits smooth ones.
    float weight = inside * (1.0 - smoothstep(0.35, 0.9, roughness));
    if (weight <= 0.0) return vec4(0.0, 0.0, 0.0, 0.0);
    vec4 clip = mul(u_planarViewProj, vec4(P, 1.0));
    if (clip.w <= 0.0001) return vec4(0.0, 0.0, 0.0, 0.0);
    vec2 uv = KbNdcToScreenUv(clip.xy / clip.w);
    vec2 border = min(uv, vec2(1.0, 1.0) - uv);
    weight *= smoothstep(0.0, 0.03, min(border.x, border.y));
    if (weight <= 0.0) return vec4(0.0, 0.0, 0.0, 0.0);
    // A rougher surface blurs the picture: nine taps over a disc that grows with the roughness.
    vec2 radius = u_planarParams.yz * (roughness * roughness * 48.0);
    vec3 color = texture2DLod(s_planarReflection, uv, 0.0).rgb;
    if (roughness > 0.08) {
        vec3 sum = color;
        sum += texture2DLod(s_planarReflection, uv + radius * vec2( 1.0,  0.0), 0.0).rgb;
        sum += texture2DLod(s_planarReflection, uv + radius * vec2(-1.0,  0.0), 0.0).rgb;
        sum += texture2DLod(s_planarReflection, uv + radius * vec2( 0.0,  1.0), 0.0).rgb;
        sum += texture2DLod(s_planarReflection, uv + radius * vec2( 0.0, -1.0), 0.0).rgb;
        sum += texture2DLod(s_planarReflection, uv + radius * vec2( 0.7071,  0.7071), 0.0).rgb;
        sum += texture2DLod(s_planarReflection, uv + radius * vec2(-0.7071,  0.7071), 0.0).rgb;
        sum += texture2DLod(s_planarReflection, uv + radius * vec2( 0.7071, -0.7071), 0.0).rgb;
        sum += texture2DLod(s_planarReflection, uv + radius * vec2(-0.7071, -0.7071), 0.0).rgb;
        color = sum * (1.0 / 9.0);
    }
    return vec4(color, weight);
}

#endif // KB_PLANAR_REFLECTION_SH
