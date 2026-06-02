$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_albedo, 0);
uniform vec4 u_materialParams;
uniform vec4 u_materialFlags;

void main()
{
    vec4 albedo = texture2D(s_albedo, v_texcoord0) * v_color0;
    if (u_materialFlags.x > 0.5 && u_materialFlags.x < 1.5 && albedo.a < u_materialParams.w) {
        discard;
    }
    gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
}
