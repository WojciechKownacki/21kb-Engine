$input v_texcoord0

#include <bgfx_shader.sh>
#include "screen_uv.sh"

SAMPLER2D(s_source, 0);
SAMPLER2D(s_bloom, 1);
SAMPLER2D(s_depth, 2);
uniform vec4 u_postParams;
uniform vec4 u_dofParams; // focus distance, full-blur distance from focus, maximum radius, homogeneous depth
uniform vec4 u_dofViewDepthPlane;
uniform mat4 u_inverseViewProjection;

float KbFocusDepth(vec2 uv)
{
    float depth = texture2DLod(s_depth,uv,0.0).x;
    if (depth <= 0.000001) return 1000000.0;
    float clipDepth = u_dofParams.w > 0.5 ? depth * 2.0 - 1.0 : depth;
    vec4 world = mul(u_inverseViewProjection,vec4(KbScreenUvToNdc(uv),clipDepth,1.0));
    world.xyz /= max(abs(world.w),0.000001);
    return abs(dot(world.xyz,u_dofViewDepthPlane.xyz) + u_dofViewDepthPlane.w);
}

void main()
{
    vec4 scene = texture2D(s_source, v_texcoord0);
    if (u_dofParams.z > 0.0) {
        float centerDepth = KbFocusDepth(v_texcoord0);
        float radius = clamp(abs(centerDepth - u_dofParams.x) / max(u_dofParams.y,0.0001),0.0,1.0) * u_dofParams.z;
        vec3 sum = scene.rgb;
        float weights = 1.0;
        for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex) {
            float turn = float(sampleIndex) * 2.39996323;
            float distance = sqrt((float(sampleIndex) + 0.5) / 16.0) * radius;
            vec2 uv = clamp(v_texcoord0 + vec2(cos(turn),sin(turn)) * distance * u_viewTexel.xy,
                vec2_splat(0.0),vec2_splat(1.0));
            float sampleDepth = KbFocusDepth(uv);
            float sampleRadius = clamp(abs(sampleDepth - u_dofParams.x) / max(u_dofParams.y,0.0001),0.0,1.0) * u_dofParams.z;
            // Keep a sharp foreground silhouette out of the blurred surface behind it.
            float weight = sampleDepth + 0.01 < centerDepth && sampleRadius < distance ? 0.0 : 1.0;
            sum += texture2DLod(s_source,uv,0.0).rgb * weight;
            weights += weight;
        }
        scene.rgb = sum / weights;
    }
    float mipCount = max(u_postParams.y, 1.0);
    vec3 bloom = vec3_splat(0.0);
    if (u_postParams.x > 0.0) {
    bloom = texture2DLod(s_bloom, v_texcoord0, 0.0).rgb * 0.42;
    if (mipCount > 1.5) {
        bloom += texture2DLod(s_bloom, v_texcoord0, 1.0).rgb * 0.26;
    }
    if (mipCount > 2.5) {
        bloom += texture2DLod(s_bloom, v_texcoord0, 2.0).rgb * 0.16;
    }
    if (mipCount > 3.5) {
        bloom += texture2DLod(s_bloom, v_texcoord0, 3.0).rgb * 0.09;
    }
    if (mipCount > 4.5) {
        bloom += texture2DLod(s_bloom, v_texcoord0, 4.0).rgb * 0.05;
    }
    if (mipCount > 5.5) {
        bloom += texture2DLod(s_bloom, v_texcoord0, 5.0).rgb * 0.02;
    }
    bloom *= u_postParams.x;
    }
    // The next target is RGBA16F: keep highlights finite before tone mapping.
    gl_FragColor = vec4(clamp(scene.rgb + bloom, vec3_splat(0.0), vec3_splat(65504.0)), scene.a);
}
