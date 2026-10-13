#ifndef KB_SKY_CUBE_SH
#define KB_SKY_CUBE_SH

// Direction through a cube map face texel, as the hardware samples cube maps (+X, -X, +Y, -Y, +Z, -Z;
// s to the right and t down the face). uv is 0..1 from the face's top-left corner.
vec3 KbCubeFaceDirection(float face, vec2 uv)
{
    float s = uv.x * 2.0 - 1.0;
    float t = uv.y * 2.0 - 1.0;
    vec3 direction = vec3(s, -t, 1.0);
    if (face < 0.5) {
        direction = vec3(1.0, -t, -s);
    } else if (face < 1.5) {
        direction = vec3(-1.0, -t, s);
    } else if (face < 2.5) {
        direction = vec3(s, 1.0, t);
    } else if (face < 3.5) {
        direction = vec3(s, -1.0, -t);
    } else if (face < 4.5) {
        direction = vec3(s, -t, 1.0);
    } else {
        direction = vec3(-s, -t, -1.0);
    }
    return normalize(direction);
}

// u_skyCubeFace: x face, y 1 when render targets start at the bottom row (OpenGL), so the face's
// top-left corner is at the top of the target on every backend.
vec2 KbCubeFaceUv(vec2 texcoord, float originBottomLeft)
{
    return vec2(texcoord.x, originBottomLeft > 0.5 ? 1.0 - texcoord.y : texcoord.y);
}

#endif // KB_SKY_CUBE_SH
