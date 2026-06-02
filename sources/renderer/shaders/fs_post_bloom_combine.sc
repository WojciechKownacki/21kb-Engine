$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_source, 0);
SAMPLER2D(s_bloom, 1);
uniform vec4 u_postParams;

void main()
{
    vec4 scene = texture2D(s_source, v_texcoord0);
    vec3 bloom = texture2D(s_bloom, v_texcoord0).rgb * u_postParams.x;
    gl_FragColor = vec4(scene.rgb + bloom, scene.a);
}
