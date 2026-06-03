$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_source, 0);
uniform vec4 u_postParams;

void main()
{
    vec4 src = texture2D(s_source, v_texcoord0);
    float brightness = max(max(src.r, src.g), src.b);
    float threshold = max(u_postParams.x, 0.0);
    float knee = threshold * saturate(u_postParams.y);
    float soft = brightness - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * max(knee, 0.0001));
    soft = (soft * soft) / max(4.0 * knee, 0.0001);
    float contribution = max(brightness - threshold, soft);
    float weight = saturate(contribution / max(brightness, 0.0001));
    gl_FragColor = vec4(src.rgb * weight, src.a);
}
