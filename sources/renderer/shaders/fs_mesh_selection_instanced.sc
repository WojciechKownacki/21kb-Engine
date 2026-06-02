$input v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent

#include <bgfx_shader.sh>

void main()
{
    gl_FragColor = vec4(1.0, 1.0, 1.0, max(abs(v_color0.a), 1.0));
}
