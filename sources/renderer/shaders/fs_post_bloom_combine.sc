$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_source, 0);
SAMPLER2D(s_bloom, 1);
uniform vec4 u_postParams;

void main()
{
    vec4 scene = texture2D(s_source, v_texcoord0);
    float mipCount = max(u_postParams.y, 1.0);
    vec3 bloom = texture2DLod(s_bloom, v_texcoord0, 0.0).rgb * 0.42;
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
    gl_FragColor = vec4(scene.rgb + bloom, scene.a);
}
