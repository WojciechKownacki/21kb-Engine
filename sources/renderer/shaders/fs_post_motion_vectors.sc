$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_depth, 0);
uniform mat4 u_inverseViewProjection;
uniform mat4 u_previousViewProjection;
uniform vec4 u_temporalParams;

void main()
{
    float depth = texture2D(s_depth, v_texcoord0).x;
    if (depth <= 0.000001) {
        gl_FragColor = vec4(0.5, 0.5, 0.0, 1.0);
        return;
    }

    float clipDepth = u_temporalParams.w > 0.5 ? depth * 2.0 - 1.0 : depth;
    vec2 currentNdc = v_texcoord0 * 2.0 - 1.0;
    vec4 world = mul(u_inverseViewProjection, vec4(currentNdc, clipDepth, 1.0));
    world.xyz /= max(world.w, 0.000001);
    world.w = 1.0;

    vec4 previousClip = mul(u_previousViewProjection, world);
    vec2 previousNdc = previousClip.xy / max(previousClip.w, 0.000001);
    vec2 previousUv = previousNdc * 0.5 + 0.5;
    vec2 velocity = v_texcoord0 - previousUv;
    gl_FragColor = vec4(velocity * 0.5 + 0.5, 0.0, 1.0);
}
