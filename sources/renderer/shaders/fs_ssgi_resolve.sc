$input v_texcoord0

#include <bgfx_shader.sh>
#include "gbuffer_position.sh"

SAMPLER2D(s_gbufferNormal, 1);
SAMPLER2D(s_gbufferDepth, 4);

#include "ssgi.sh"
#include "ssgi_gather.sh"
#include "ssgi_voxel.sh"
#include "ssao.sh"

// Gathers this frame's bounce light and ambient occlusion and blends them with the previous frame's,
// reprojected by world position and rejected where the stored surface depth no longer matches
// (disocclusion). Target 0 holds the radiance with the view depth in a, target 1 the visibility.
void main()
{
    vec4 encodedNormal = texture2D(s_gbufferNormal, v_texcoord0);
    if (encodedNormal.a < 0.5) {
        gl_FragData[0] = vec4(0.0, 0.0, 0.0, 0.0);
        gl_FragData[1] = vec4(1.0, 0.0, 0.0, 0.0);
        return;
    }
    vec3 normal = normalize(encodedNormal.xyz * 2.0 - 1.0);
    vec3 worldPos = ReconstructWorldPosition(v_texcoord0, texture2D(s_gbufferDepth, v_texcoord0).x);
    vec3 gi = vec3(0.0, 0.0, 0.0);
    if (u_giParams.x > 0.0) {
        gi = u_voxelInfo.x > 0.5 ? KbVoxelGi(worldPos, normal, gl_FragCoord.xy) : KbScreenSpaceGi(worldPos, normal, gl_FragCoord.xy);
    }
    float ao = u_aoParams.x > 0.0 ? KbScreenSpaceAo(worldPos, normal, gl_FragCoord.xy) : 1.0;
    vec3 giHistory;
    float aoHistory;
    float weight = u_giTemporal.x * KbScreenSpaceAccumulated(worldPos, giHistory, aoHistory);
    gl_FragData[0] = vec4(mix(gi, giHistory, weight), mul(u_giViewProj, vec4(worldPos, 1.0)).w);
    gl_FragData[1] = vec4(mix(ao, aoHistory, weight), 0.0, 0.0, 0.0);
}
