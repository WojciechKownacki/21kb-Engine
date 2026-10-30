$input v_color0, v_texcoord0, v_shadowPos, v_shadowFlags

#include <bgfx_shader.sh>

SAMPLER2D(s_particleAtlas, 0);
SAMPLER2D(s_particleSceneDepth, 1);
uniform vec4 u_particleDepthParams;
uniform vec4 u_particleFeatureParams;
uniform vec4 u_particleVolumetricParams;

void main()
{
    // The texture modulated by the particle colour (HDR); additive output is premultiplied.
    vec4 tex = texture2D(s_particleAtlas, v_texcoord0);
    float particleAlpha = clamp(v_color0.a, 0.0, 1.0);
    float coverage = tex.a;
    bool volumetric = u_particleFeatureParams.y > 6.5 && u_particleFeatureParams.y < 7.5;
    float sceneDepth = 0.0;
    if (coverage < 0.002 && !volumetric) {
        discard;
    }

    vec3 shaded = tex.rgb * v_color0.rgb;
    bool additive = u_particleFeatureParams.z > 0.5;
    vec4 color;
    if (additive) {
        color = vec4(shaded * coverage * particleAlpha, coverage * particleAlpha);
    } else {
        color = vec4(shaded, coverage * particleAlpha);
    }

    if (u_particleFeatureParams.x > 0.5) {
        float edgeWidth = fwidth(coverage);
        float edge = smoothstep(0.0, max(edgeWidth, 0.0001), coverage);
        color.rgb *= edge;
        color.a *= edge;
    }
    if (u_particleDepthParams.w > 0.5) {
        // Sample the depth pixel covered by this fragment, including the backend viewport orientation.
        vec2 screenUv = gl_FragCoord.xy * u_viewTexel.xy;
        float sceneDeviceDepth = texture2D(s_particleSceneDepth, screenUv).x;
        // Depth textures are always 0..1; the projection may use -1..1 clip depth.
        sceneDeviceDepth = mix(sceneDeviceDepth, sceneDeviceDepth * 2.0 - 1.0, u_particleFeatureParams.w);
        sceneDepth = abs(u_particleDepthParams.y /
            max(abs(sceneDeviceDepth - u_particleDepthParams.x), 0.000001));
        float fade = clamp((sceneDepth - v_shadowFlags.x) / u_particleDepthParams.z, 0.0, 1.0);
        if (!volumetric) {
            color.rgb *= fade;
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
        float transmittance = 1.0 - exp(-opticalDepth);
        color.a *= transmittance;
        color.rgb *= transmittance;
    }
    gl_FragColor = color;
}
