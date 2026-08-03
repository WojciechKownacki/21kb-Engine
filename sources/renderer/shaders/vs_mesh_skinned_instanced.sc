$input a_position, a_normal, a_tangent, a_texcoord0, a_color0, a_indices, a_weight, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos, v_objectOrientation, v_preSkinnedNormal

#include <bgfx_shader.sh>

uniform mat4 u_shadowViewProj;
uniform sampler2D s_skinningPalette;
// x = first palette matrix, y = reciprocal palette texture height.
uniform vec4 u_skinningPaletteInfo;

void main()
{
    float instanceRandom = i_data0.w;
    float instanceRadius = i_data1.w;
    float instanceFadeAmount = i_data2.w;
    float instanceCustomData = i_data3.w;
    mat4 model = mtxFromCols(vec4(i_data0.xyz, 0.0), vec4(i_data1.xyz, 0.0), vec4(i_data2.xyz, 0.0), vec4(i_data3.xyz, 1.0));
    float paletteY0 = (u_skinningPaletteInfo.x + a_indices.x + 0.5) * u_skinningPaletteInfo.y;
    float paletteY1 = (u_skinningPaletteInfo.x + a_indices.y + 0.5) * u_skinningPaletteInfo.y;
    float paletteY2 = (u_skinningPaletteInfo.x + a_indices.z + 0.5) * u_skinningPaletteInfo.y;
    float paletteY3 = (u_skinningPaletteInfo.x + a_indices.w + 0.5) * u_skinningPaletteInfo.y;
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
    vec3 skinnedTangent = mul(skin, vec4(a_tangent.xyz, 0.0)).xyz;
    vec4 worldPos = mul(model, vec4(skinnedPosition, 1.0));
    vec3 objectOrientationRaw = mul(model, vec4(0.0, 0.0, 1.0, 0.0)).xyz;
    vec3 objectOrientation = dot(objectOrientationRaw, objectOrientationRaw) > 0.0001 ? normalize(objectOrientationRaw) : vec3(0.0, 0.0, 1.0);
    vec3 preSkinnedNormal = dot(a_normal, a_normal) > 0.0001 ? normalize(a_normal) : vec3(0.0, 0.0, 1.0);
    gl_Position = mul(u_viewProj, worldPos);
    v_worldPos = worldPos.xyz;
    v_objectLocalPos = vec4(a_position, instanceRandom);
    v_objectWorldPos = vec4(mul(model, vec4(0.0, 0.0, 0.0, 1.0)).xyz, instanceRadius);
    v_objectOrientation = vec4(objectOrientation, instanceCustomData);
    v_preSkinnedNormal = preSkinnedNormal;
    v_shadowPos = mul(u_shadowViewProj, worldPos);
    v_shadowFlags = vec4(i_data4.w >= 0.0 ? 1.0 : 0.0, instanceFadeAmount, 0.0, 0.0);
    vec3 normal = normalize(mul(model, vec4(skinnedNormal, 0.0)).xyz);
    vec3 tangent = mul(model, vec4(skinnedTangent, 0.0)).xyz;
    vec3 fallbackAxis = abs(normal.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    tangent = dot(tangent, tangent) > 0.0001 ? normalize(tangent) : normalize(cross(fallbackAxis, normal));
    tangent = normalize(tangent - normal * dot(normal, tangent));
    float handedness = abs(a_tangent.w) > 0.0001 ? a_tangent.w : 1.0;
    v_normal = normal;
    v_tangent = tangent;
    v_bitangent = normalize(cross(normal, tangent) * handedness);
    v_texcoord0 = a_texcoord0;
    v_color0 = vec4(a_color0.rgb, 1.0) * vec4(i_data4.rgb, abs(i_data4.w));
}
