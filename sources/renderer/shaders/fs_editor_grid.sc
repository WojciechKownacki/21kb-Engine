$input v_color0, v_worldPos

#include <bgfx_shader.sh>

uniform vec4 u_editorGridParams;

void main()
{
    float distFade = 1.0 - (distance(v_worldPos.xz, u_editorGridParams.xy) / u_editorGridParams.z);
    distFade = smoothstep(0.02, 0.3, distFade);
    gl_FragColor = vec4(v_color0.rgb, v_color0.a * distFade * u_editorGridParams.w);
}
