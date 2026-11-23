$input a_position, a_normal, i_data0, i_data1, i_data2, i_data3
$output v_normal, v_worldPos

#include <bgfx_shader.sh>
#include "model_normal.sh"

// x = width of the outline in pixels of the view (0 for the silhouette behind other surfaces), y = 1 for that silhouette.
uniform vec4 u_outlineParams;

void main()
{
    mat4 model = mtxFromCols(vec4(i_data0.xyz, 0.0), vec4(i_data1.xyz, 0.0), vec4(i_data2.xyz, 0.0), vec4(i_data3.xyz, 1.0));
    vec4 worldPos = mul(model, vec4(a_position, 1.0));
    vec3 normal = KbModelNormal(model, a_normal);
    vec4 clip = mul(u_viewProj, worldPos);
    // The hull grows along the normal on the screen, by the same number of pixels at any distance.
    vec4 clipNormal = mul(u_viewProj, vec4(normal, 0.0));
    vec2 direction = clipNormal.xy * u_viewRect.zw;
    float length2 = dot(direction, direction);
    direction = length2 > 0.000001 ? direction * inversesqrt(length2) : vec2(0.0, 0.0);
    clip.xy += direction * (2.0 * u_outlineParams.x) / u_viewRect.zw * clip.w;
    gl_Position = clip;
    v_normal = normal;
    v_worldPos = worldPos.xyz;
}
