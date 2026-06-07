$input a_position, a_color0
$output v_color0, v_worldPos

#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_viewProj, vec4(a_position, 1.0));
    v_color0 = vec4(a_color0, 1.0);
    v_worldPos = a_position;
}
