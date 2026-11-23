$input a_position, a_normal, a_indices, a_weight, i_data0, i_data1, i_data2, i_data3
$output v_normal, v_worldPos

#include <bgfx_shader.sh>
#include "skinning.sh"
#include "model_normal.sh"

SAMPLER2D(s_skinningPalette, 15);
// x = first palette matrix, y = reciprocal palette texture height.
uniform vec4 u_skinningPaletteInfo;
// x = width of the outline in pixels of the view (0 for the silhouette behind other surfaces), y = 1 for that silhouette.
uniform vec4 u_outlineParams;

void main()
{
    mat4 model = mtxFromCols(vec4(i_data0.xyz, 0.0), vec4(i_data1.xyz, 0.0), vec4(i_data2.xyz, 0.0), vec4(i_data3.xyz, 1.0));
    vec4 joints = KbSkinJointIndices(a_indices);
    float paletteY0 = (u_skinningPaletteInfo.x + joints.x + 0.5) * u_skinningPaletteInfo.y;
    float paletteY1 = (u_skinningPaletteInfo.x + joints.y + 0.5) * u_skinningPaletteInfo.y;
    float paletteY2 = (u_skinningPaletteInfo.x + joints.z + 0.5) * u_skinningPaletteInfo.y;
    float paletteY3 = (u_skinningPaletteInfo.x + joints.w + 0.5) * u_skinningPaletteInfo.y;
    mat4 skin0 = mtxFromCols(texture2DLod(s_skinningPalette, vec2(0.125, paletteY0), 0.0), texture2DLod(s_skinningPalette, vec2(0.375, paletteY0), 0.0), texture2DLod(s_skinningPalette, vec2(0.625, paletteY0), 0.0), texture2DLod(s_skinningPalette, vec2(0.875, paletteY0), 0.0));
    mat4 skin1 = mtxFromCols(texture2DLod(s_skinningPalette, vec2(0.125, paletteY1), 0.0), texture2DLod(s_skinningPalette, vec2(0.375, paletteY1), 0.0), texture2DLod(s_skinningPalette, vec2(0.625, paletteY1), 0.0), texture2DLod(s_skinningPalette, vec2(0.875, paletteY1), 0.0));
    mat4 skin2 = mtxFromCols(texture2DLod(s_skinningPalette, vec2(0.125, paletteY2), 0.0), texture2DLod(s_skinningPalette, vec2(0.375, paletteY2), 0.0), texture2DLod(s_skinningPalette, vec2(0.625, paletteY2), 0.0), texture2DLod(s_skinningPalette, vec2(0.875, paletteY2), 0.0));
    mat4 skin3 = mtxFromCols(texture2DLod(s_skinningPalette, vec2(0.125, paletteY3), 0.0), texture2DLod(s_skinningPalette, vec2(0.375, paletteY3), 0.0), texture2DLod(s_skinningPalette, vec2(0.625, paletteY3), 0.0), texture2DLod(s_skinningPalette, vec2(0.875, paletteY3), 0.0));
    mat4 skin = skin0 * a_weight.x + skin1 * a_weight.y + skin2 * a_weight.z + skin3 * a_weight.w;
    vec3 skinnedPosition = mul(skin, vec4(a_position, 1.0)).xyz;
    vec3 skinColumn0 = skin[0].xyz;
    vec3 skinColumn1 = skin[1].xyz;
    vec3 skinColumn2 = skin[2].xyz;
    vec3 skinNormalColumn0 = cross(skinColumn1, skinColumn2);
    vec3 skinNormalColumn1 = cross(skinColumn2, skinColumn0);
    vec3 skinNormalColumn2 = cross(skinColumn0, skinColumn1);
    float skinDeterminant = dot(skinColumn0, skinNormalColumn0);
    vec3 skinnedNormal = abs(skinDeterminant) > 0.000001
        ? (skinNormalColumn0 * a_normal.x + skinNormalColumn1 * a_normal.y + skinNormalColumn2 * a_normal.z) / skinDeterminant
        : mul(skin, vec4(a_normal, 0.0)).xyz;
    vec4 worldPos = mul(model, vec4(skinnedPosition, 1.0));
    vec3 normal = KbModelNormal(model, skinnedNormal);
    vec4 clip = mul(u_viewProj, worldPos);
    vec4 clipNormal = mul(u_viewProj, vec4(normal, 0.0));
    vec2 direction = clipNormal.xy * u_viewRect.zw;
    float length2 = dot(direction, direction);
    direction = length2 > 0.000001 ? direction * inversesqrt(length2) : vec2(0.0, 0.0);
    clip.xy += direction * (2.0 * u_outlineParams.x) / u_viewRect.zw * clip.w;
    gl_Position = clip;
    v_normal = normal;
    v_worldPos = worldPos.xyz;
}
