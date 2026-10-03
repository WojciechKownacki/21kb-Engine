$input v_texcoord0

#include <bgfx_shader.sh>
#include "gbuffer_position.sh"

SAMPLER2D(s_gbufferNormal, 1);
SAMPLER2D(s_gbufferDepth, 4);

#include "ssgi.sh"
#include "ssgi_gather.sh"

// Gathers this frame's bounce light and blends it with the previous frame's, reprojected by world
// position and rejected where the stored surface depth no longer matches (disocclusion).
void main()
{
    vec4 encodedNormal = texture2D(s_gbufferNormal, v_texcoord0);
    if (encodedNormal.a < 0.5) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    vec3 normal = normalize(encodedNormal.xyz * 2.0 - 1.0);
    vec3 worldPos = ReconstructWorldPosition(v_texcoord0, texture2D(s_gbufferDepth, v_texcoord0).x);
    vec3 gi = KbScreenSpaceGi(worldPos, normal, gl_FragCoord.xy);
    vec4 history = KbGiAccumulated(worldPos);
    gi = mix(gi, history.rgb, u_giTemporal.x * history.w);
    gl_FragColor = vec4(gi, mul(u_giViewProj, vec4(worldPos, 1.0)).w);
}
