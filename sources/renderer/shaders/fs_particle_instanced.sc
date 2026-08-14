$input v_color0, v_texcoord0, v_shadowPos, v_shadowFlags

#include <bgfx_shader.sh>

SAMPLER2D(s_particleAtlas, 0);
SAMPLER2D(s_particleSceneDepth, 1);
uniform vec4 u_particleDepthParams;
uniform vec4 u_particleFeatureParams;

void main()
{
    vec4 color = texture2D(s_particleAtlas, v_texcoord0) * v_color0;
    if (u_particleFeatureParams.x > 0.5) {
        color.a = smoothstep(0.0, 0.02, color.a);
    }
    if (u_particleDepthParams.w > 0.5) {
        vec2 screenUv = v_shadowPos.xy / max(abs(v_shadowPos.w), 0.000001) * 0.5 + 0.5;
        float sceneDeviceDepth = texture2D(s_particleSceneDepth, screenUv).x;
        float sceneDepth = abs(u_particleDepthParams.y /
            max(abs(sceneDeviceDepth - u_particleDepthParams.x), 0.000001));
        float fade = clamp((sceneDepth - v_shadowFlags.x) / u_particleDepthParams.z, 0.0, 1.0);
        color.a *= fade;
    }
    gl_FragColor = color;
}
