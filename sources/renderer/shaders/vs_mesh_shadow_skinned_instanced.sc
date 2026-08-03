$input a_position, a_texcoord0, a_indices, a_weight, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_skinningPalette, 14);
uniform vec4 u_skinningPaletteInfo;

void main()
{
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
    gl_Position = mul(u_viewProj, mul(model, mul(skin, vec4(a_position, 1.0))));
    v_texcoord0 = a_texcoord0;
    v_color0 = i_data4;
}
