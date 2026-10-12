$input v_currentClip, v_previousClip

#include <bgfx_shader.sh>
#include "motion_vectors.sh"

SAMPLER2D(s_sceneDepth, 0);

void main()
{
    if (!KbMotionSurfaceVisible(s_sceneDepth, gl_FragCoord.xy, gl_FragCoord.z)) {
        discard;
    }
    gl_FragColor = vec4(KbMotionVelocity(v_currentClip, v_previousClip), 0.0, 1.0);
}
