$input a_position, a_texcoord0, a_texcoord1
$output v_texcoord0, v_widgetLocal

#include <bgfx_shader.sh>

uniform vec4 u_widgetViewport;

void main()
{
    vec2 ndc = vec2(
        a_position.x * u_widgetViewport.z * 2.0 - 1.0,
        1.0 - a_position.y * u_widgetViewport.w * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_texcoord0 = a_texcoord0;
    v_widgetLocal = a_texcoord1;
}
