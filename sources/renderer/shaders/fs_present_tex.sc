$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_kbFullscreenColor, 0);
SAMPLER3D(s_kbFullscreenColorGradeLut, 1);
uniform vec4 u_tonemapParams;
uniform vec4 u_colorGradeParams;
uniform vec4 u_vignetteParams;
// x: 1 runs FXAA on the display image (after tonemapping, like Unreal), yz: texel size of the source.
uniform vec4 u_displayAntiAliasing;

vec3 aces_fitted(vec3 color)
{
    color = max(color, vec3_splat(0.0));
    return saturate((color * (2.51 * color + vec3_splat(0.03))) / (color * (2.43 * color + vec3_splat(0.59)) + vec3_splat(0.14)));
}

vec3 agx_approx(vec3 color)
{
    color = max(color, vec3_splat(0.0));
    vec3 compressed = log2(color + vec3_splat(1.0));
    compressed = compressed / (compressed + vec3_splat(1.65));
    return saturate(compressed * vec3_splat(1.18));
}

vec3 tonemap_display(vec3 color)
{
    if (u_tonemapParams.z < 0.0) {
        return saturate(color);
    }
    return u_tonemapParams.z < 0.5 ? aces_fitted(color) : agx_approx(color);
}

vec3 apply_color_grade_lut(vec3 displayLinear)
{
    float lutSize = max(u_colorGradeParams.x, 2.0);
    vec3 uvw = saturate(displayLinear) * ((lutSize - 1.0) / lutSize) + vec3_splat(0.5 / lutSize);
    vec3 graded = texture3D(s_kbFullscreenColorGradeLut, uvw).rgb;
    return mix(displayLinear, graded, saturate(u_colorGradeParams.y));
}

// Darkening toward the corners: 1 at the centre, 1 - intensity at the corners, smooth in between.
float vignette_factor(vec2 uv)
{
    vec2 offset = (uv - vec2_splat(0.5)) * 2.0;
    float falloff = saturate(1.0 - 0.5 * u_vignetteParams.x * dot(offset, offset));
    return falloff * falloff;
}

// Display encoding. Gamma 2.2 (the default) is the sRGB transfer curve of the display, as in Unity,
// Unreal and Godot - a plain 1/2.2 power lifts the darks (#0A0C16 came out as #141418). Any other
// gamma (1.0 for linear debug views) is a plain power.
vec3 encode_display(vec3 display)
{
    display = max(display, vec3_splat(0.0));
    if (abs(u_tonemapParams.y - 1.0 / 2.2) < 1.0e-4) {
        vec3 low = display * 12.92;
        vec3 high = 1.055 * pow(display, vec3_splat(1.0 / 2.4)) - vec3_splat(0.055);
        return mix(low, high, step(vec3_splat(0.0031308), display));
    }
    return pow(display, vec3_splat(u_tonemapParams.y));
}

// The display image of a scene colour at `uv`: exposed, vignetted, tonemapped, graded and display encoded.
vec3 display_transform(vec3 sceneColor, vec2 uv)
{
    vec3 hdr = sceneColor * exp2(u_tonemapParams.x);
    hdr = hdr * vignette_factor(uv);
    vec3 display = tonemap_display(hdr);
    display = apply_color_grade_lut(display);
    return encode_display(display);
}

// A bilinear sample of the display image, as an LDR target holding it would return: the four source texels
// are brought to display values before they are weighted, so bright HDR texels do not bleed over an edge.
vec3 display_sample(vec2 uv)
{
    vec2 texel = u_displayAntiAliasing.yz;
    vec2 position = uv / texel - vec2_splat(0.5);
    vec2 base = floor(position);
    vec2 weight = position - base;
    vec2 uv00 = (base + vec2_splat(0.5)) * texel;
    vec2 uv10 = uv00 + vec2(texel.x, 0.0);
    vec2 uv01 = uv00 + vec2(0.0, texel.y);
    vec2 uv11 = uv00 + texel;
    vec3 c00 = display_transform(texture2D(s_kbFullscreenColor, uv00).rgb, uv00);
    vec3 c10 = display_transform(texture2D(s_kbFullscreenColor, uv10).rgb, uv10);
    vec3 c01 = display_transform(texture2D(s_kbFullscreenColor, uv01).rgb, uv01);
    vec3 c11 = display_transform(texture2D(s_kbFullscreenColor, uv11).rgb, uv11);
    return mix(mix(c00, c10, weight.x), mix(c01, c11, weight.x), weight.y);
}

// FXAA (Lottes) on the display image: edges are found in perceptual luma of the tonemapped, gamma encoded
// colours and blended there, as Unreal runs FXAA after the tonemapper.
vec3 fxaa_display(vec2 uv)
{
    vec2 texel = u_displayAntiAliasing.yz;
    vec3 rgbNW = display_sample(uv + vec2(-1.0, -1.0) * texel);
    vec3 rgbNE = display_sample(uv + vec2( 1.0, -1.0) * texel);
    vec3 rgbSW = display_sample(uv + vec2(-1.0,  1.0) * texel);
    vec3 rgbSE = display_sample(uv + vec2( 1.0,  1.0) * texel);
    vec3 rgbM = display_sample(uv);

    vec3 luma = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM = dot(rgbM, luma);
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125, 0.0078125);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2_splat(-8.0), vec2_splat(8.0)) * texel;

    vec3 rgbA = 0.5 * (
        display_sample(uv + dir * (1.0 / 3.0 - 0.5)) +
        display_sample(uv + dir * (2.0 / 3.0 - 0.5)));
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        display_sample(uv + dir * -0.5) +
        display_sample(uv + dir * 0.5));
    float lumaB = dot(rgbB, luma);
    return (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
}

void main()
{
    vec4 scene = texture2D(s_kbFullscreenColor, v_texcoord0);
    vec3 display = display_transform(scene.rgb, v_texcoord0);
    if (u_displayAntiAliasing.x > 0.5) {
        display = fxaa_display(v_texcoord0);
    }
    gl_FragColor = vec4(display, scene.a);
}
