$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_source, 0);
uniform vec4 u_postParams;

void main()
{
    vec2 stepUv = u_postParams.xy;
    vec3 color = texture2D(s_source, v_texcoord0).rgb * 0.227027;
    color += texture2D(s_source, v_texcoord0 + stepUv * 1.384615).rgb * 0.316216;
    color += texture2D(s_source, v_texcoord0 - stepUv * 1.384615).rgb * 0.316216;
    color += texture2D(s_source, v_texcoord0 + stepUv * 3.230769).rgb * 0.070270;
    color += texture2D(s_source, v_texcoord0 - stepUv * 3.230769).rgb * 0.070270;
    gl_FragColor = vec4(color, 1.0);
}
