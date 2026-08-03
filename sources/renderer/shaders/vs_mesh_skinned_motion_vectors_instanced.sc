$input a_position, a_indices, a_weight, i_data0, i_data1, i_data2, i_data3
$output v_currentClip, v_previousClip

#include <bgfx_shader.sh>

SAMPLER2D(s_skinningPalette, 14);
SAMPLER2D(s_previousSkinningPalette, 15);
uniform vec4 u_skinningPaletteInfo;
uniform vec4 u_previousSkinningPaletteInfo;
uniform mat4 u_motionPreviousViewProjection;

mat4 SkinMatrix(sampler2D palette, vec4 paletteInfo, vec4 indices, vec4 weights)
{
    float paletteY0 = (paletteInfo.x + indices.x + 0.5) * paletteInfo.y;
    float paletteY1 = (paletteInfo.x + indices.y + 0.5) * paletteInfo.y;
    float paletteY2 = (paletteInfo.x + indices.z + 0.5) * paletteInfo.y;
    float paletteY3 = (paletteInfo.x + indices.w + 0.5) * paletteInfo.y;
    mat4 skin0 = mtxFromCols(texture2DLod(palette, vec2(0.125, paletteY0), 0.0), texture2DLod(palette, vec2(0.375, paletteY0), 0.0), texture2DLod(palette, vec2(0.625, paletteY0), 0.0), texture2DLod(palette, vec2(0.875, paletteY0), 0.0));
    mat4 skin1 = mtxFromCols(texture2DLod(palette, vec2(0.125, paletteY1), 0.0), texture2DLod(palette, vec2(0.375, paletteY1), 0.0), texture2DLod(palette, vec2(0.625, paletteY1), 0.0), texture2DLod(palette, vec2(0.875, paletteY1), 0.0));
    mat4 skin2 = mtxFromCols(texture2DLod(palette, vec2(0.125, paletteY2), 0.0), texture2DLod(palette, vec2(0.375, paletteY2), 0.0), texture2DLod(palette, vec2(0.625, paletteY2), 0.0), texture2DLod(palette, vec2(0.875, paletteY2), 0.0));
    mat4 skin3 = mtxFromCols(texture2DLod(palette, vec2(0.125, paletteY3), 0.0), texture2DLod(palette, vec2(0.375, paletteY3), 0.0), texture2DLod(palette, vec2(0.625, paletteY3), 0.0), texture2DLod(palette, vec2(0.875, paletteY3), 0.0));
    return skin0 * weights.x + skin1 * weights.y + skin2 * weights.z + skin3 * weights.w;
}

void main()
{
    mat4 model = mtxFromCols(vec4(i_data0.xyz, 0.0), vec4(i_data1.xyz, 0.0), vec4(i_data2.xyz, 0.0), vec4(i_data3.xyz, 1.0));
    vec4 currentWorldPosition = mul(model, mul(SkinMatrix(s_skinningPalette, u_skinningPaletteInfo, a_indices, a_weight), vec4(a_position, 1.0)));
    vec4 previousWorldPosition = mul(model, mul(SkinMatrix(s_previousSkinningPalette, u_previousSkinningPaletteInfo, a_indices, a_weight), vec4(a_position, 1.0)));
    v_currentClip = mul(u_viewProj, currentWorldPosition);
    v_previousClip = mul(u_motionPreviousViewProjection, previousWorldPosition);
    gl_Position = v_currentClip;
}
