#ifndef KB_GBUFFER_POSITION_SH
#define KB_GBUFFER_POSITION_SH

#include "screen_uv.sh"

uniform vec4 u_deferredCameraPosition;
uniform mat4 u_deferredInverseViewProjection;
uniform vec4 u_deferredDepthParams;

vec3 ReconstructWorldPosition(vec2 uv, float depth)
{
    vec2 ndc = KbScreenUvToNdc(uv);
    float clipDepth = u_deferredDepthParams.x > 0.5 ? depth * 2.0 - 1.0 : depth;
    vec4 world = mul(u_deferredInverseViewProjection, vec4(ndc, clipDepth, 1.0));
    world.xyz /= max(world.w, 0.000001);
    return world.xyz;
}

#endif // KB_GBUFFER_POSITION_SH
