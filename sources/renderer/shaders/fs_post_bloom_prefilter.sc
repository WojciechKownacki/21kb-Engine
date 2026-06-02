$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_source, 0);
uniform vec4 u_postParams;

void main()
{
    vec4 src = texture2D(s_source, v_texcoord0);
    float brightness = max(max(src.r, src.g), src.b);
    float weight = saturate((brightness - u_postParams.x) / max(brightness, 0.0001));
    gl_FragColor = vec4(src.rgb * weight, src.a);
}
