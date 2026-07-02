$input a_position, a_texcoord0, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>

void main()
{
    // MAT-77: columns 0-2 .w carry packed per-instance scalars; zero them to rebuild the affine matrix.
    mat4 model = mtxFromCols(vec4(i_data0.xyz, 0.0), vec4(i_data1.xyz, 0.0), vec4(i_data2.xyz, 0.0), vec4(i_data3.xyz, 1.0));
    gl_Position = mul(u_viewProj, mul(model, vec4(a_position, 1.0)));
    v_texcoord0 = a_texcoord0;
    v_color0 = i_data4;
}
