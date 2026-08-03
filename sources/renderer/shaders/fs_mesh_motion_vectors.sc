$input v_currentClip, v_previousClip

#include <bgfx_shader.sh>

SAMPLER2D(s_sceneDepth, 0);
uniform vec4 u_motionVectorParams;

void main()
{
    float currentW = max(abs(v_currentClip.w), 0.000001);
    vec3 currentNdc = v_currentClip.xyz / currentW;
    vec2 currentUv = vec2(currentNdc.x * 0.5 + 0.5, 0.5 - currentNdc.y * 0.5);
    float meshDepth = currentNdc.z;
    if (u_motionVectorParams.x > 0.5) {
        meshDepth = meshDepth * 0.5 + 0.5;
    }
    if (abs(texture2D(s_sceneDepth, currentUv).x - meshDepth) > 0.002) {
        discard;
    }
    vec2 previousNdc = v_previousClip.xy / max(abs(v_previousClip.w), 0.000001);
    vec2 previousUv = vec2(previousNdc.x * 0.5 + 0.5, 0.5 - previousNdc.y * 0.5);
    gl_FragColor = vec4(currentUv - previousUv, 0.0, 1.0);
}
