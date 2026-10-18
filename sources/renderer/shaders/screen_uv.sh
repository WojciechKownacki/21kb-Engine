#ifndef KB_SCREEN_UV_SH
#define KB_SCREEN_UV_SH

// Render target rows run top to bottom on Direct3D, Vulkan and Metal, and bottom to top on
// OpenGL, so a screen uv's v grows downwards everywhere except OpenGL (vs_present.sc flips it
// there, as bgfx's caps->originBottomLeft asks).
vec2 KbNdcToScreenUv(vec2 ndc)
{
#if BGFX_SHADER_LANGUAGE_GLSL
    return ndc * 0.5 + 0.5;
#else
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
#endif
}

vec2 KbScreenUvToNdc(vec2 uv)
{
#if BGFX_SHADER_LANGUAGE_GLSL
    return uv * 2.0 - 1.0;
#else
    return vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
#endif
}

#endif // KB_SCREEN_UV_SH
