$input a_position, a_texcoord0
$output v_texcoord0

// The world backdrop of the forward path: a fullscreen triangle whose depth (a_position.z) is the far
// plane, drawn after the opaque geometry so the depth test leaves every drawn surface in front of it.

#include <bgfx_shader.sh>

void main()
{
    gl_Position = vec4(a_position.xy, a_position.z, 1.0);
    v_texcoord0 = a_texcoord0;
#if BGFX_SHADER_LANGUAGE_GLSL
    // OpenGL render targets start at the bottom row (caps->originBottomLeft); see screen_uv.sh.
    v_texcoord0.y = 1.0 - v_texcoord0.y;
#endif
}
