$input a_position, a_normal, a_tangent, a_texcoord0, a_texcoord1, a_color0, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos, v_objectOrientation, v_preSkinnedNormal

#include <bgfx_shader.sh>

uniform mat4 u_shadowViewProj;

void main()
{
    // Material graph per-instance scalars are packed into the affine model's .w lanes.
    float instanceRandom = i_data0.w;
    float instanceRadius = i_data1.w;
    float instanceFadeAmount = i_data2.w;
    float instanceCustomData = i_data3.w;
    mat4 model = mtxFromCols(vec4(i_data0.xyz, 0.0), vec4(i_data1.xyz, 0.0), vec4(i_data2.xyz, 0.0), vec4(i_data3.xyz, 1.0));
    vec4 worldPos = mul(model, vec4(a_position, 1.0));
    vec3 objectOrientationRaw = mul(model, vec4(0.0, 0.0, 1.0, 0.0)).xyz;
    vec3 objectOrientation = dot(objectOrientationRaw, objectOrientationRaw) > 0.0001 ? normalize(objectOrientationRaw) : vec3(0.0, 0.0, 1.0);
    vec3 preSkinnedNormal = dot(a_normal, a_normal) > 0.0001 ? normalize(a_normal) : vec3(0.0, 0.0, 1.0);
    gl_Position = mul(u_viewProj, worldPos);
    v_worldPos = worldPos.xyz;
    // MAT-76 object-space inputs (xyz) + MAT-77 per-instance scalars carried in the free .w lanes.
    v_objectLocalPos = vec4(a_position, instanceRandom);
    v_objectWorldPos = vec4(mul(model, vec4(0.0, 0.0, 0.0, 1.0)).xyz, instanceRadius);
    v_objectOrientation = vec4(objectOrientation, instanceCustomData);
    v_preSkinnedNormal = preSkinnedNormal;
    v_shadowPos = mul(u_shadowViewProj, worldPos);
    // .x = shadow flag; .y = fade amount; .zw = second UV set (MAT-73).
    v_shadowFlags = vec4(i_data4.w >= 0.0 ? 1.0 : 0.0, instanceFadeAmount, a_texcoord1.x, a_texcoord1.y);
    vec3 normal = normalize(mul(model, vec4(a_normal, 0.0)).xyz);
    vec3 tangent = mul(model, vec4(a_tangent.xyz, 0.0)).xyz;
    vec3 fallbackAxis = abs(normal.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    tangent = dot(tangent, tangent) > 0.0001 ? normalize(tangent) : normalize(cross(fallbackAxis, normal));
    tangent = normalize(tangent - normal * dot(normal, tangent));
    float handedness = abs(a_tangent.w) > 0.0001 ? a_tangent.w : 1.0;
    v_normal = normal;
    v_tangent = tangent;
    v_bitangent = normalize(cross(normal, tangent) * handedness);
    v_texcoord0 = a_texcoord0;
    // Per-vertex color modulated by the per-instance tint; default vertex color is white
    // so existing instance tinting is unchanged (MAT-74). i_data4.w sign still encodes shadow-receive.
    v_color0 = a_color0 * vec4(i_data4.rgb, abs(i_data4.w));
}
