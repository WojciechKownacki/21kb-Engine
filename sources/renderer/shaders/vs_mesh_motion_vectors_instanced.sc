$input a_position, i_data0, i_data1, i_data2, i_data3
$output v_currentClip, v_previousClip

#include <bgfx_shader.sh>

uniform mat4 u_motionPreviousViewProjection;

void main()
{
    mat4 model = mtxFromCols(vec4(i_data0.xyz, 0.0), vec4(i_data1.xyz, 0.0), vec4(i_data2.xyz, 0.0), vec4(i_data3.xyz, 1.0));
    vec4 worldPosition = mul(model, vec4(a_position, 1.0));
    v_currentClip = mul(u_viewProj, worldPosition);
    v_previousClip = mul(u_motionPreviousViewProjection, worldPosition);
    gl_Position = v_currentClip;
}
