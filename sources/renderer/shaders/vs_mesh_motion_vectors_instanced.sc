$input a_position, i_data0, i_data1, i_data2, i_data3
$output v_currentClip, v_previousClip

#include <bgfx_shader.sh>
#include "motion_vectors.sh"

SAMPLER2D(s_motionPreviousModels, 15);

void main()
{
    vec4 column0 = vec4(i_data0.xyz, 0.0);
    vec4 column1 = vec4(i_data1.xyz, 0.0);
    vec4 column2 = vec4(i_data2.xyz, 0.0);
    vec4 column3 = vec4(i_data3.xyz, 1.0);
    mat4 model = mtxFromCols(column0, column1, column2, column3);
    mat4 previousModel = KbMotionPreviousModel(s_motionPreviousModels, float(gl_InstanceID), column0, column1, column2, column3);
    vec4 worldPos = mul(model, vec4(a_position, 1.0));
    vec4 previousWorldPos = mul(previousModel, vec4(a_position, 1.0));
    gl_Position = mul(u_viewProj, worldPos);
    v_currentClip = mul(u_motionCurrentViewProjection, worldPos);
    v_previousClip = mul(u_motionPreviousViewProjection, previousWorldPos);
}
