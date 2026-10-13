#ifndef KB_MODEL_NORMAL_SH
#define KB_MODEL_NORMAL_SH

// The world normal of an object-space normal under an affine model matrix: the inverse transpose of its
// 3x3 part, as Unreal and Unity transform normals, so a non-uniformly scaled object keeps normals
// perpendicular to its surface. The cofactor matrix is the inverse transpose times the determinant; its sign
// keeps the normal outward on a mirroring (negative determinant) model.
vec3 KbModelNormal(mat4 model, vec3 normal)
{
    vec3 column0 = mul(model, vec4(1.0, 0.0, 0.0, 0.0)).xyz;
    vec3 column1 = mul(model, vec4(0.0, 1.0, 0.0, 0.0)).xyz;
    vec3 column2 = mul(model, vec4(0.0, 0.0, 1.0, 0.0)).xyz;
    vec3 cofactor0 = cross(column1, column2);
    float mirror = dot(column0, cofactor0) < 0.0 ? -1.0 : 1.0;
    vec3 world = cofactor0 * normal.x + cross(column2, column0) * normal.y + cross(column0, column1) * normal.z;
    return normalize(world * mirror);
}

#endif // KB_MODEL_NORMAL_SH
