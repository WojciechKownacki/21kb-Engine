$input a_position, a_texcoord0, a_texcoord1
$output v_texcoord0, v_screenUILocal, v_screenUIPosition

#include <bgfx_shader.sh>

uniform vec4 u_screenUIViewport;

void main()
{
    vec2 position = a_position.xy;
    vec2 ndc = vec2(
        position.x * u_screenUIViewport.z * 2.0 - 1.0,
        1.0 - position.y * u_screenUIViewport.w * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_texcoord0 = a_texcoord0;
    v_screenUILocal = a_texcoord1;
    v_screenUIPosition = position;
}
