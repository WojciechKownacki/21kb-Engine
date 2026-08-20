$input v_color0, v_texcoord0, v_shadowPos, v_shadowFlags

#include <bgfx_shader.sh>

SAMPLER2D(s_particleAtlas, 0);
SAMPLER2D(s_particleSceneDepth, 1);
uniform vec4 u_particleDepthParams;
uniform vec4 u_particleFeatureParams;
uniform vec4 u_particleVolumetricParams;

void main()
{
    vec4 color = texture2D(s_particleAtlas, v_texcoord0) * v_color0;
    bool volumetric = u_particleFeatureParams.y > 6.5 && u_particleFeatureParams.y < 7.5;
    float sceneDepth = 0.0;
    if (u_particleFeatureParams.x > 0.5) {
        color.a = smoothstep(0.0, 0.02, color.a);
    }
    if (u_particleDepthParams.w > 0.5) {
        vec2 screenUv = v_shadowPos.xy / max(abs(v_shadowPos.w), 0.000001) * 0.5 + 0.5;
        float sceneDeviceDepth = texture2D(s_particleSceneDepth, screenUv).x;
        sceneDepth = abs(u_particleDepthParams.y /
            max(abs(sceneDeviceDepth - u_particleDepthParams.x), 0.000001));
        float fade = clamp((sceneDepth - v_shadowFlags.x) / u_particleDepthParams.z, 0.0, 1.0);
        if (!volumetric) {
            color.a *= fade;
        }
    }
    if (volumetric) {
        float radialDistanceSquared = dot(v_shadowFlags.yz, v_shadowFlags.yz);
        if (radialDistanceSquared >= 1.0) discard;
        float radius = max(v_shadowFlags.w, 0.0001);
        float chordLength = 2.0 * radius * sqrt(1.0 - radialDistanceSquared);
        float visibleLength = chordLength;
        if (u_particleDepthParams.w > 0.5) {
            float frontDepth = v_shadowFlags.x - radius;
            if (sceneDepth <= frontDepth) discard;
            visibleLength = min(chordLength, max(sceneDepth - frontDepth, 0.0));
        }
        float stepCount = clamp(floor(u_particleVolumetricParams.z + 0.5), 1.0, 256.0);
        float stepLength = visibleLength / stepCount;
        float opticalDepth = 0.0;
        for (int stepIndex = 0; stepIndex < 256; ++stepIndex) {
            if (float(stepIndex) >= stepCount) break;
            opticalDepth += u_particleVolumetricParams.x * stepLength;
        }
        color.a *= 1.0 - exp(-opticalDepth);
    }
    gl_FragColor = color;
}
